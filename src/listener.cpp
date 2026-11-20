// SagePreviewListener (Windows 11 prototype)
// A manually launched, tray-free Explorer Spacebar listener.
// Never injects into Explorer, modifies the registry, or loads preview codecs.

#include <windows.h>
#include "preview_messages.h"
// WIN32_LEAN_AND_MEAN leaves several COM declarations out of windows.h.
// The Shell's exdisp.h header needs `interface`, IDispatch, VARIANT, etc.
// Declare them explicitly BEFORE exdisp.h to avoid a cascade of MSVC errors.
#include <ole2.h>
#include <oaidl.h>
#include <exdisp.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <servprov.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kWindowClass[] = L"SagePreviewListenerMessageWindow";
constexpr wchar_t kMutexName[] = L"Local\\SagePreviewListener-v0.3";
constexpr UINT kToggleMessage = WM_APP + 1;
constexpr UINT kDiagnosticMessage = WM_APP + 2;

HWND g_window = nullptr;
HHOOK g_hook = nullptr;
bool g_caughtSpaceDown = false; // Used only by the hook/message-loop thread.
bool g_forwardedLeftDown = false;
bool g_forwardedRightDown = false;
bool g_forwardedEscapeDown = false;
std::atomic<DWORD> g_previewProcessId{0}; // Owned by worker; read by keyboard hook.
bool g_diagnose = false;

std::mutex g_workMutex;
std::condition_variable g_workCv;
bool g_pendingToggle = false;
bool g_stopWorker = false;
struct RequestedView { HWND explorer = nullptr; HWND focused = nullptr; };
RequestedView g_requestedView;

std::wstring WindowClass(HWND hwnd) {
    wchar_t name[128] = {};
    if (hwnd) GetClassNameW(hwnd, name, static_cast<int>(std::size(name)));
    return name;
}

void Trace(const std::wstring& message) {
    if (!g_diagnose) return;
    wchar_t temp[MAX_PATH + 1] = {};
    const DWORD chars = GetTempPathW(MAX_PATH, temp);
    if (chars == 0 || chars >= MAX_PATH) return;
    const std::wstring filename = std::wstring(temp) + L"SagePreview-listener.log";
    HANDLE file = CreateFileW(filename.c_str(), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t prefix[64] = {};
    swprintf_s(prefix, L"[%02u:%02u:%02u] ", now.wHour, now.wMinute, now.wSecond);
    const std::wstring line = std::wstring(prefix) + message + L"\r\n";
    const int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size > 1) {
        std::string utf8(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1,
                            utf8.data(), size, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size() - 1), &written, nullptr);
    }
    CloseHandle(file);
}

bool IsTextEditorClass(const std::wstring& name) {
    // No keyboard stealing while Explorer is editing a filename or text field.
    std::wstring lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    return lower == L"edit" || lower == L"combobox" ||
           lower.find(L"richedit") != std::wstring::npos ||
           lower.find(L"textinput") != std::wstring::npos;
}

struct ExplorerFocus {
    HWND window = nullptr;
    HWND focused = nullptr;
    std::wstring explanation;
};

// A deliberately conservative focus gate. The actual Windows Shell view is
// identified by an ancestor named SHELLDLL_DefView. This is NOT a guarantee
// on every Windows 11 build; unsupported variants fail closed (normal Space).
ExplorerFocus FindFocusedExplorerView() {
    ExplorerFocus result;
    const HWND foreground = GetForegroundWindow();
    if (WindowClass(foreground) != L"CabinetWClass") {
        result.explanation = L"foreground is not Explorer: " + WindowClass(foreground);
        return result;
    }

    const DWORD threadId = GetWindowThreadProcessId(foreground, nullptr);
    GUITHREADINFO info{};
    info.cbSize = sizeof(info);
    if (!GetGUIThreadInfo(threadId, &info) || !info.hwndFocus) {
        result.explanation = L"Explorer has no accessible focused child";
        return result;
    }

    bool inFileView = false;
    for (HWND cursor = info.hwndFocus; cursor; cursor = GetParent(cursor)) {
        const std::wstring cls = WindowClass(cursor);
        if (IsTextEditorClass(cls)) {
            result.explanation = L"focused on an Explorer text/editor control";
            return result;
        }
        if (cls == L"SHELLDLL_DefView") {
            inFileView = true;
            break;
        }
        if (cursor == foreground) break;
    }
    if (!inFileView) {
        result.explanation = L"Explorer focus is outside its file list (focused class: " +
                             WindowClass(info.hwndFocus) + L")";
        return result;
    }
    result.window = foreground;
    result.focused = info.hwndFocus;
    result.explanation = L"file view focused (" + WindowClass(info.hwndFocus) + L")";
    return result;
}

bool NoModifiersPressed() {
    return !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
           !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
           !(GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
           !(GetAsyncKeyState(VK_LWIN) & 0x8000) &&
           !(GetAsyncKeyState(VK_RWIN) & 0x8000);
}

LRESULT CALLBACK KeyboardHook(int code, WPARAM message, LPARAM data) {
    if (code != HC_ACTION) return CallNextHookEx(g_hook, code, message, data);
    const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
    if (key->flags & LLKHF_INJECTED) {
        return CallNextHookEx(g_hook, code, message, data);
    }

    // Windows sometimes leaves keyboard focus in Explorer after a background
    // listener launches the topmost preview. Forward navigation/close only
    // while the SAME Explorer file view remains focused AND our viewer lives.
    // Never intercept other applications, text fields, or background tabs.
    if (key->vkCode == VK_LEFT || key->vkCode == VK_RIGHT || key->vkCode == VK_ESCAPE) {
        bool& consumedDown = key->vkCode == VK_LEFT ? g_forwardedLeftDown :
                             key->vkCode == VK_RIGHT ? g_forwardedRightDown : g_forwardedEscapeDown;
        if (message == WM_KEYUP || message == WM_SYSKEYUP) {
            if (consumedDown) { consumedDown = false; return 1; }
            return CallNextHookEx(g_hook, code, message, data);
        }
        if (message != WM_KEYDOWN || !NoModifiersPressed())
            return CallNextHookEx(g_hook, code, message, data);
        if (consumedDown) return 1; // No unwanted key repeat.

        const DWORD expectedPid = g_previewProcessId.load(std::memory_order_acquire);
        if (expectedPid == 0 || !FindFocusedExplorerView().window)
            return CallNextHookEx(g_hook, code, message, data);
        const HWND viewer = FindWindowW(L"SagePreviewStandaloneWindow", nullptr);
        DWORD actualPid = 0;
        if (viewer) GetWindowThreadProcessId(viewer, &actualPid);
        if (actualPid != expectedPid) return CallNextHookEx(g_hook, code, message, data);

        const UINT request = key->vkCode == VK_ESCAPE
            ? sagepreview::kListenerClose : sagepreview::kListenerNavigate;
        if (PostMessageW(viewer, request, static_cast<WPARAM>(key->vkCode), 0)) {
            consumedDown = true;
            return 1;
        }
        return CallNextHookEx(g_hook, code, message, data);
    }
    if (key->vkCode != VK_SPACE) {
        return CallNextHookEx(g_hook, code, message, data);
    }

    if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        if (g_caughtSpaceDown) {
            g_caughtSpaceDown = false;
            return 1; // Consume the matching release, even after focus changes.
        }
        return CallNextHookEx(g_hook, code, message, data);
    }
    if (message != WM_KEYDOWN || !NoModifiersPressed()) {
        return CallNextHookEx(g_hook, code, message, data);
    }
    if (g_caughtSpaceDown) return 1; // Ignore auto-repeat.

    // No COM, decoding, file access, or process launches in this callback.
    const ExplorerFocus focus = FindFocusedExplorerView();
    if (!focus.window) {
        if (g_diagnose && g_window) PostMessageW(g_window, kDiagnosticMessage, 0, 0);
        return CallNextHookEx(g_hook, code, message, data);
    }
    if (!g_window || !PostMessageW(g_window, kToggleMessage,
                                    reinterpret_cast<WPARAM>(focus.window),
                                    reinterpret_cast<LPARAM>(focus.focused))) {
        return CallNextHookEx(g_hook, code, message, data);
    }
    g_caughtSpaceDown = true;
    return 1;
}

std::wstring ModuleDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD written = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!written) return {};
        if (written < path.size()) {
            path.resize(written);
            return std::filesystem::path(path).parent_path().wstring();
        }
        if (path.size() >= 32768) return {};
        path.resize(path.size() * 2);
    }
}

bool IsPreviewableFile(const std::wstring& filename) {
    std::wstring ext = std::filesystem::path(filename).extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](wchar_t c) { return std::towlower(c); });
    static constexpr const wchar_t* extensions[] = {
        L".clip", L".kra", L".psd", L".png", L".jpg", L".jpeg", L".bmp", L".gif",
        L".tif", L".tiff", L".webp", L".avif"
    };
    for (const auto* accepted : extensions) {
        if (ext == accepted) return true;
    }
    return false;
}

std::wstring SelectedFileFromView(IShellView* view) {
    ComPtr<IFolderView2> folder;
    if (FAILED(view->QueryInterface(IID_PPV_ARGS(folder.GetAddressOf())))) return {};
    ComPtr<IShellItemArray> selected;
    if (FAILED(folder->GetSelection(FALSE, selected.GetAddressOf())) || !selected) return {};
    DWORD count = 0;
    if (FAILED(selected->GetCount(&count)) || count != 1) return {};
    ComPtr<IShellItem> item;
    if (FAILED(selected->GetItemAt(0, item.GetAddressOf()))) return {};
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || !path) return {};
    const std::wstring result(path);
    CoTaskMemFree(path);
    if (!IsPreviewableFile(result)) return {};
    const DWORD attrs = GetFileAttributesW(result.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return result;
}

std::wstring GetExplorerSelection(HWND explorer, HWND focused) {
    // Windows 11 tabs may have the SAME top-level HWND in IShellWindows.
    // Prefer the IShellView that actually owns the focused file-list child,
    // not the first ShellWindows entry matching the HWND.
    ComPtr<IShellWindows> windows;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(windows.GetAddressOf())))) {
        Trace(L"Could not connect to the ShellWindows collection");
        return {};
    }
    LONG count = 0;
    if (FAILED(windows->get_Count(&count))) return {};

    ComPtr<IShellView> onlyVisible;
    HWND onlyVisibleHwnd = nullptr;
    bool ambiguousVisible = false;
    for (LONG index = 0; index < count; ++index) {
        VARIANT position{};
        position.vt = VT_I4;
        position.lVal = index;
        ComPtr<IDispatch> disp;
        if (FAILED(windows->Item(position, disp.GetAddressOf())) || !disp) continue;
        ComPtr<IWebBrowserApp> webBrowser;
        if (FAILED(disp.As(&webBrowser))) continue;
        SHANDLE_PTR browserHwnd = 0;
        if (FAILED(webBrowser->get_HWND(&browserHwnd)) ||
            reinterpret_cast<HWND>(browserHwnd) != explorer) continue;

        ComPtr<IServiceProvider> services;
        if (FAILED(disp.As(&services))) continue;
        ComPtr<IShellBrowser> shellBrowser;
        if (FAILED(services->QueryService(SID_STopLevelBrowser,
                                          IID_PPV_ARGS(shellBrowser.GetAddressOf())))) continue;
        ComPtr<IShellView> view;
        if (FAILED(shellBrowser->QueryActiveShellView(view.GetAddressOf())) || !view) continue;
        HWND viewHwnd = nullptr;
        if (FAILED(view->GetWindow(&viewHwnd)) || !viewHwnd) continue;
        if (!IsWindowVisible(viewHwnd)) continue;

        // Strongest match: the file-list focus is a descendant of this view.
        if (viewHwnd == focused || IsChild(viewHwnd, focused)) {
            const std::wstring result = SelectedFileFromView(view.Get());
            Trace(result.empty() ? L"Focused tab has no single supported image selected"
                                 : L"Selected image in focused Explorer tab");
            return result;
        }
        // Fallback only when exactly one *distinct* Shell view is visible.
        if (!onlyVisible) {
            onlyVisible = view;
            onlyVisibleHwnd = viewHwnd;
        } else if (onlyVisibleHwnd != viewHwnd) {
            ambiguousVisible = true;
        }
    }
    if (onlyVisible && !ambiguousVisible) {
        Trace(L"Using sole visible Explorer Shell view as fallback");
        return SelectedFileFromView(onlyVisible.Get());
    }
    Trace(L"No unambiguous active Explorer tab; refusing to preview wrong file");
    return {};
}

struct PreviewProcess {
    HANDLE process = nullptr;
    DWORD processId = 0;

    ~PreviewProcess() { if (process) CloseHandle(process); }

    bool Running() const {
        return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }
    void Clear() {
        if (process) CloseHandle(process);
        process = nullptr;
        processId = 0;
    }
};

struct WindowSearch {
    DWORD processId;
    HWND found = nullptr;
};

BOOL CALLBACK FindViewerWindow(HWND window, LPARAM context) {
    auto* search = reinterpret_cast<WindowSearch*>(context);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == search->processId &&
        WindowClass(window) == L"SagePreviewStandaloneWindow") {
        search->found = window;
        return FALSE;
    }
    return TRUE;
}

void TogglePreview(PreviewProcess& preview, const ExplorerFocus& focus) {
    if (preview.Running()) {
        WindowSearch search{preview.processId};
        EnumWindows(FindViewerWindow, reinterpret_cast<LPARAM>(&search));
        if (search.found) {
            PostMessageW(search.found, WM_CLOSE, 0, 0);
            Trace(L"Closed running preview");
        } else {
            Trace(L"Preview process exists but has no window yet; not launching another");
        }
        return;
    }
    preview.Clear();
    g_previewProcessId.store(0, std::memory_order_release);
    if (!focus.window) return;
    const std::wstring image = GetExplorerSelection(focus.window, focus.focused);
    if (image.empty()) return;

    const std::filesystem::path executable = std::filesystem::path(ModuleDirectory()) / L"SagePreview.exe";
    if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Trace(L"SagePreview.exe missing beside listener");
        MessageBoxW(nullptr, L"SagePreview.exe was not found next to SagePreviewListener.exe.\n"
                        L"Build All and keep both executables in the same folder.",
                    L"SagePreviewListener", MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring command = L"\"" + executable.wstring() + L"\" \"" + image + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION created{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &startup, &created)) {
        Trace(L"Failed to start SagePreview.exe (Win32 error " +
              std::to_wstring(GetLastError()) + L")");
        return;
    }
    preview.process = created.hProcess;
    preview.processId = created.dwProcessId;
    g_previewProcessId.store(created.dwProcessId, std::memory_order_release);
    CloseHandle(created.hThread);
    Trace(L"Started preview process");
}

void WorkLoop() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) {
        Trace(L"Failed to initialize COM on worker thread");
        return;
    }
    PreviewProcess preview;
    for (;;) {
        RequestedView request;
        {
            std::unique_lock<std::mutex> lock(g_workMutex);
            g_workCv.wait(lock, [] { return g_stopWorker || g_pendingToggle; });
            if (g_stopWorker) break;
            request = g_requestedView;
            g_pendingToggle = false;
        }
        // Preserve the folder view from the original key event. Re-checking
        // foreground focus here can race with the preview becoming active.
        if (IsWindow(request.explorer) && IsWindow(request.focused)) {
            TogglePreview(preview, ExplorerFocus{request.explorer, request.focused, L""});
        }
    }
    g_previewProcessId.store(0, std::memory_order_release);
    CoUninitialize();
}

LRESULT CALLBACK ListenerWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case kToggleMessage: {
        std::lock_guard<std::mutex> lock(g_workMutex);
        g_requestedView = RequestedView{reinterpret_cast<HWND>(wParam),
                                        reinterpret_cast<HWND>(lParam)};
        g_pendingToggle = true;
        g_workCv.notify_one();
        return 0;
    }
    case kDiagnosticMessage: {
        Trace(FindFocusedExplorerView().explanation);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring mode = argc >= 2 && arguments ? arguments[1] : L"";
    if (arguments) LocalFree(arguments);

    if (mode == L"--stop" || mode == L"--status") {
        HWND existing = FindWindowExW(HWND_MESSAGE, nullptr, kWindowClass, nullptr);
        if (mode == L"--stop" && existing) PostMessageW(existing, WM_CLOSE, 0, 0);
        MessageBoxW(nullptr, mode == L"--stop"
                    ? (existing ? L"SagePreview listener stop requested." : L"Listener is not running.")
                    : (existing ? L"SagePreview listener is running." : L"Listener is not running."),
                    L"SagePreviewListener", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    g_diagnose = mode == L"--diagnose";
    HANDLE singleton = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!singleton) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(singleton);
        return 0;
    }

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = ListenerWndProc;
    cls.hInstance = instance;
    cls.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&cls)) {
        CloseHandle(singleton);
        return 1;
    }
    g_window = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, instance, nullptr);
    if (!g_window) {
        CloseHandle(singleton);
        return 1;
    }

    std::thread worker(WorkLoop);
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook, instance, 0);
    if (!g_hook) {
        MessageBoxW(nullptr, L"Could not install the Spacebar listener.\n"
                        L"No changes were made to Explorer.",
                    L"SagePreviewListener", MB_OK | MB_ICONERROR);
    } else {
        Trace(L"Listener started; no startup registration and no tray icon");
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_workMutex);
        g_stopWorker = true;
    }
    g_workCv.notify_all();
    worker.join();
    if (IsWindow(g_window)) DestroyWindow(g_window);
    g_window = nullptr;
    Trace(L"Listener stopped");
    CloseHandle(singleton);
    return 0;
}
