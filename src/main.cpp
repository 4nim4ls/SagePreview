// SagePreview: native Windows 11 image previewer.
// The preview presentation stays entirely outside Explorer and requires no UI framework.

#include "clip.h"
#include "kra.h"
#include "psd.h"
#include "navigation.h"
#include "preview_layout.h"
#include "preview_messages.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <wincodec.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

// An intentionally conservative memory budget for this first prototype.
// Very large source images are decoded at a smaller preview resolution.
constexpr double kMaxDecodedPixels = 16'000'000.0;
constexpr double kMaxDecodedDimension = 8'192.0;
constexpr double kMinZoom = 0.1;
constexpr double kMaxZoom = 20.0;

std::wstring HResultText(HRESULT hr) {
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

std::wstring ChooseImageFile() {
    // When launched with F5 in Visual Studio, open a normal file picker instead
    // of requiring launch arguments or relying on the IDE's working directory.
    wchar_t buffer[32768] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(std::size(buffer));
    dialog.lpstrFilter = L"Images (*.clip;*.kra;*.psd;*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.avif)\0"
                         L"*.clip;*.kra;*.psd;*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.avif\0"
                         L"All files (*.*)\0*.*\0";
    dialog.nFilterIndex = 1;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&dialog) ? std::wstring(buffer) : std::wstring{};
}

class PreviewApp final {
public:
    const std::wstring& ErrorDetails() const { return decodeError_; }

    HRESULT Initialize(const std::wstring& filename) {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       d2dFactory_.GetAddressOf());
        if (FAILED(hr)) return hr;

        // WIC2 is present on Windows 11, but the fallback is harmless.
        hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(wicFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(wicFactory_.GetAddressOf()));
        }
        if (FAILED(hr)) return hr;
        return LoadImage(filename);
    }

    // Decode into local COM objects first. A corrupt sibling file must never
    // replace the successfully displayed image or leave a blank render target.
    HRESULT LoadImage(const std::wstring& filename) {
        decodeError_.clear();
        HRESULT hr = S_OK;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapSource> frame;
        const std::wstring extension = std::filesystem::path(filename).extension().wstring();
        std::wstring extensionLower = extension;
        std::transform(extensionLower.begin(), extensionLower.end(),
                       extensionLower.begin(), [](wchar_t ch) { return std::towlower(ch); });
        if (extensionLower == L".clip") {
            // CSP stores a PNG canvas preview in its embedded SQLite metadata.
            // Extract only that small database, not the many megabytes of layers.
            std::vector<std::uint8_t> png;
            std::string reason;
            if (!sagepreview::ExtractClipPreview(filename, png, reason)) {
                decodeError_.assign(reason.begin(), reason.end());
                return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
            }
            ComPtr<IStream> pngStream;
            pngStream.Attach(SHCreateMemStream(png.data(), static_cast<UINT>(png.size())));
            if (!pngStream) {
                decodeError_ = L"Could not create the in-memory PNG stream.";
                return E_OUTOFMEMORY;
            }
            hr = wicFactory_->CreateDecoderFromStream(
                pngStream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                decoder.GetAddressOf());
            if (FAILED(hr)) {
                decodeError_ = L"Windows Imaging Component could not decode the embedded PNG.";
                return hr;
            }
        } else if (extensionLower == L".kra") {
            // Read only Krita's saved composite. Never reconstruct its layers.
            std::vector<std::uint8_t> png;
            std::string reason;
            if (!sagepreview::ExtractKraPreview(filename, png, reason)) {
                decodeError_.assign(reason.begin(), reason.end());
                return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
            }
            ComPtr<IStream> pngStream;
            pngStream.Attach(SHCreateMemStream(png.data(), static_cast<UINT>(png.size())));
            if (!pngStream) {
                decodeError_ = L"Could not create a Krita image stream.";
                return E_OUTOFMEMORY;
            }
            hr = wicFactory_->CreateDecoderFromStream(
                pngStream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                decoder.GetAddressOf());
            if (FAILED(hr)) {
                decodeError_ = L"Windows could not decode Krita's merged image.";
                return hr;
            }
        } else if (extensionLower == L".psd") {
            // Saved PSD composite only; never reconstruct layers.
            sagepreview::PsdImage composite;
            std::string reason;
            if (!sagepreview::DecodePsdComposite(filename, composite, reason)) {
                decodeError_.assign(reason.begin(), reason.end());
                return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
            }
            ComPtr<IWICBitmap> bitmap;
            hr = wicFactory_->CreateBitmapFromMemory(
                composite.width, composite.height, GUID_WICPixelFormat32bppBGRA,
                composite.width * 4u, static_cast<UINT>(composite.bgra.size()),
                composite.bgra.data(), bitmap.GetAddressOf());
            if (FAILED(hr)) {
                decodeError_ = L"Windows could not create the PSD composite bitmap.";
                return hr;
            }
            frame = bitmap.Get();
        } else {
            hr = wicFactory_->CreateDecoderFromFilename(
                filename.c_str(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
            if (FAILED(hr)) {
                decodeError_ = extensionLower == L".avif"
                    ? L"Windows has no WIC decoder for this AVIF file.\n"
                      L"Check Windows' HEIF Image Extensions and AV1 Video Extension."
                    : L"No available Windows decoder accepted this image.";
                return hr;
            }
        }

        if (!frame) {
            ComPtr<IWICBitmapFrameDecode> decodedFrame;
            hr = decoder->GetFrame(0, decodedFrame.GetAddressOf());
            if (FAILED(hr)) return hr;
            frame = decodedFrame.Get();
        }

        UINT sourceWidth = 0;
        UINT sourceHeight = 0;
        hr = frame->GetSize(&sourceWidth, &sourceHeight);
        if (FAILED(hr)) return hr;
        if (sourceWidth == 0 || sourceHeight == 0) return E_FAIL;

        // Cap decoder output before allocating a D2D bitmap. This also avoids
        // loading a giant full-resolution artwork merely to fit it on screen.
        const double sourcePixels = static_cast<double>(sourceWidth) * sourceHeight;
        const double ratio = std::min({
            1.0,
            kMaxDecodedDimension / sourceWidth,
            kMaxDecodedDimension / sourceHeight,
            std::sqrt(kMaxDecodedPixels / sourcePixels)
        });
        const UINT newImageWidth = std::max(1u, static_cast<UINT>(sourceWidth * ratio));
        const UINT newImageHeight = std::max(1u, static_cast<UINT>(sourceHeight * ratio));

        ComPtr<IWICBitmapSource> source;
        if (ratio < 1.0) {
            ComPtr<IWICBitmapScaler> scaler;
            hr = wicFactory_->CreateBitmapScaler(scaler.GetAddressOf());
            if (FAILED(hr)) return hr;
            hr = scaler->Initialize(frame.Get(), newImageWidth, newImageHeight,
                                    WICBitmapInterpolationModeFant);
            if (FAILED(hr)) return hr;
            source = scaler.Get();
        } else {
            source = frame.Get();
        }

        ComPtr<IWICFormatConverter> converter;
        hr = wicFactory_->CreateFormatConverter(converter.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return hr;

        // Materialize the pixels before replacing the currently displayed
        // bitmap. WIC can otherwise defer corrupt-file errors until painting.
        ComPtr<IWICBitmap> readyBitmap;
        hr = wicFactory_->CreateBitmapFromSource(converter.Get(),
                                                WICBitmapCacheOnLoad,
                                                readyBitmap.GetAddressOf());
        if (FAILED(hr)) {
            decodeError_ = L"Windows could not finish decoding this image.";
            return hr;
        }

        // Commit only after the new pixels are safely decoded.
        bitmap_.Reset();
        renderTarget_.Reset();
        imageSource_ = readyBitmap.Get();
        imageWidth_ = newImageWidth;
        imageHeight_ = newImageHeight;
        filename_ = filename;
        zoom_ = 1.0;
        panX_ = 0.0;
        panY_ = 0.0;
        if (hwnd_) {
            // A new image gets its own native-pixel / shrink-to-fit layout.
            RefreshImagePlacement();
            SetWindowTextW(hwnd_,
                (L"SagePreview — " + std::filesystem::path(filename_).filename().wstring()).c_str());
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return S_OK;
    }

    int Run(HINSTANCE instance, int /*showCommand*/) {
        // Open on the monitor containing the active Explorer window (or the
        // foreground application for a standalone launch).
        const HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(),
                                                   MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!GetMonitorInfoW(monitor, &monitorInfo)) {
            MessageBoxW(nullptr, L"Could not identify a monitor.", L"SagePreview", MB_ICONERROR);
            return 1;
        }
        monitorRect_ = monitorInfo.rcMonitor;
        workRect_ = monitorInfo.rcWork;

        // One alpha-blended black window dims the desktop without changing the
        // image's opacity or requiring a per-pixel layered image renderer.
        WNDCLASSEXW overlayClass{};
        overlayClass.cbSize = sizeof(overlayClass);
        overlayClass.lpfnWndProc = &PreviewApp::OverlayProc;
        overlayClass.hInstance = instance;
        overlayClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        overlayClass.lpszClassName = L"SagePreviewDimmingOverlay";
        if (!RegisterClassExW(&overlayClass)) {
            MessageBoxW(nullptr, L"Could not register the dimming overlay.",
                        L"SagePreview", MB_ICONERROR);
            return 1;
        }

        const int monitorWidth = monitorRect_.right - monitorRect_.left;
        const int monitorHeight = monitorRect_.bottom - monitorRect_.top;
        overlay_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            overlayClass.lpszClassName, L"", WS_POPUP,
            monitorRect_.left, monitorRect_.top, monitorWidth, monitorHeight,
            nullptr, nullptr, instance, this);
        if (!overlay_ || !SetLayeredWindowAttributes(overlay_, 0, kOverlayOpacity, LWA_ALPHA)) {
            if (overlay_) DestroyWindow(overlay_);
            overlay_ = nullptr;
            MessageBoxW(nullptr, L"Could not create the dimming overlay.",
                        L"SagePreview", MB_ICONERROR);
            return 1;
        }

        WNDCLASSEXW viewerClass{};
        viewerClass.cbSize = sizeof(viewerClass);
        viewerClass.lpfnWndProc = &PreviewApp::WindowProc;
        viewerClass.hInstance = instance;
        viewerClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        viewerClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        viewerClass.hbrBackground = nullptr;
        viewerClass.lpszClassName = L"SagePreviewStandaloneWindow";
        if (!RegisterClassExW(&viewerClass)) {
            DestroyWindow(overlay_);
            overlay_ = nullptr;
            MessageBoxW(nullptr, L"Could not register the preview window.",
                        L"SagePreview", MB_ICONERROR);
            return 1;
        }

        const std::wstring title =
            L"SagePreview — " + std::filesystem::path(filename_).filename().wstring();
        const auto bounds = PreviewBounds(1.0);
        // Owned windows stay above their owner. Both are topmost, while
        // neither appears as a second taskbar application.
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            viewerClass.lpszClassName, title.c_str(), WS_POPUP,
            bounds.left, bounds.top, bounds.width, bounds.height,
            overlay_, nullptr, instance, this);
        if (!hwnd_) {
            DestroyWindow(overlay_);
            overlay_ = nullptr;
            MessageBoxW(nullptr, L"Could not create the preview window.",
                        L"SagePreview", MB_ICONERROR);
            return 1;
        }

        ShowWindow(overlay_, SW_SHOWNOACTIVATE);
        UpdateWindow(overlay_); // Ensure the dimmer is painted before the viewer appears.
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        SetForegroundWindow(hwnd_); // Best effort; Windows foreground rules still apply.
        if (GetForegroundWindow() == hwnd_) SetFocus(hwnd_);
        // If Windows keeps focus in Explorer, the listener forwards our own
        // navigation/close messages without stealing shortcuts from other apps.

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        // Destroy the owner only after its owned viewer has finished closing;
        // destroying it during the viewer's WM_DESTROY can reenter shutdown.
        if (overlay_) {
            DestroyWindow(overlay_);
            overlay_ = nullptr;
        }
        return static_cast<int>(msg.wParam);
    }

private:
    static constexpr BYTE kOverlayOpacity = 184; // ~72% black
    static constexpr int kImageMargin = 40; // Physical pixels around the image

    int AvailableWidth() const {
        return static_cast<int>(std::max(1L, workRect_.right - workRect_.left - 2 * kImageMargin));
    }
    int AvailableHeight() const {
        return static_cast<int>(std::max(1L, workRect_.bottom - workRect_.top - 2 * kImageMargin));
    }
    double BaseScale() const {
        return sagepreview::InitialPreviewScale(
            imageWidth_, imageHeight_, AvailableWidth(), AvailableHeight());
    }
    sagepreview::PreviewRect PreviewBounds(double zoom) const {
        return sagepreview::CenteredPreviewRect(
            workRect_.left, workRect_.top,
            workRect_.right - workRect_.left,
            workRect_.bottom - workRect_.top,
            AvailableWidth(), AvailableHeight(),
            imageWidth_, imageHeight_, BaseScale(), zoom);
    }
    void RefreshImagePlacement() {
        if (!hwnd_) return;
        const auto rect = PreviewBounds(zoom_);
        SetWindowPos(hwnd_, nullptr, rect.left, rect.top,
                     rect.width, rect.height, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void RefreshMonitorPlacement() {
        if (!overlay_) return;
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        const HMONITOR monitor = MonitorFromWindow(overlay_, MONITOR_DEFAULTTONEAREST);
        if (!GetMonitorInfoW(monitor, &info)) return;
        monitorRect_ = info.rcMonitor;
        workRect_ = info.rcWork;
        SetWindowPos(overlay_, nullptr,
                     monitorRect_.left, monitorRect_.top,
                     monitorRect_.right - monitorRect_.left,
                     monitorRect_.bottom - monitorRect_.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        // A display layout change is rare; reset the zoom so the image is
        // guaranteed to fit the new usable area without offscreen cropping.
        zoom_ = 1.0;
        panX_ = panY_ = 0.0;
        RefreshImagePlacement();
    }

    static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT message,
                                        WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        if (message == WM_LBUTTONDOWN) {
            const auto* self = reinterpret_cast<PreviewApp*>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self && self->hwnd_) PostMessageW(self->hwnd_, WM_CLOSE, 0, 0);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void Navigate(int direction) {
        if (!navigationLoaded_) {
            navigationLoaded_ = true;
            navigation_.Load(std::filesystem::path(filename_));
        }
        // Only supported files are offered by the folder scan. If an image
        // happens to be corrupt or its codec is unavailable, skip it quietly.
        // No popup or change to the current image until a candidate decodes.
        const auto original = std::filesystem::path(filename_);
        for (std::size_t tried = 0; tried < navigation_.Count(); ++tried) {
            const auto candidate = navigation_.Next(direction);
            if (!candidate) break; // Stop at folder boundaries (no wrap).
            if (!navigation_.SetCurrent(*candidate)) break;
            if (SUCCEEDED(LoadImage(candidate->wstring()))) return;
        }
        // Nothing new could be decoded. Restore the original position.
        navigation_.SetCurrent(original);
        decodeError_.clear();
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message,
                                       WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            auto* self = static_cast<PreviewApp*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto* self = reinterpret_cast<PreviewApp*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return self ? self->HandleMessage(hwnd, message, wParam, lParam)
                    : DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HRESULT EnsureRenderTarget() {
        if (renderTarget_) return S_OK;
        RECT client{};
        GetClientRect(hwnd_, &client);
        const UINT width = std::max(1L, client.right - client.left);
        const UINT height = std::max(1L, client.bottom - client.top);

        HRESULT hr = d2dFactory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(width, height)),
            renderTarget_.GetAddressOf());
        if (FAILED(hr)) return hr;

        // Window client coordinates and D2D drawing coordinates both use pixels.
        renderTarget_->SetDpi(96.0f, 96.0f);
        hr = renderTarget_->CreateBitmapFromWicBitmap(
            imageSource_.Get(), nullptr, bitmap_.GetAddressOf());
        if (FAILED(hr)) {
            renderTarget_.Reset();
            return hr;
        }
        return S_OK;
    }

    D2D1_RECT_F ImageRect(int clientWidth, int clientHeight) const {
        const double scale = BaseScale() * zoom_;
        // Match integer window sizing at the nearest pixel: otherwise a
        // subpixel strip of gray may appear along an image edge.
        const double width = sagepreview::SnapPreviewSpan(imageWidth_ * scale, clientWidth);
        const double height = sagepreview::SnapPreviewSpan(imageHeight_ * scale, clientHeight);
        const double left = (clientWidth - width) * 0.5 +
            sagepreview::ClampPreviewPan(panX_, width, clientWidth);
        const double top = (clientHeight - height) * 0.5 +
            sagepreview::ClampPreviewPan(panY_, height, clientHeight);
        return D2D1::RectF(static_cast<float>(left), static_cast<float>(top),
                           static_cast<float>(left + width),
                           static_cast<float>(top + height));
    }

    void ConstrainPan() {
        if (!hwnd_) return;
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width <= 0 || height <= 0) return;
        const double scale = BaseScale() * zoom_;
        panX_ = sagepreview::ClampPreviewPan(panX_,
            sagepreview::SnapPreviewSpan(imageWidth_ * scale, width), width);
        panY_ = sagepreview::ClampPreviewPan(panY_,
            sagepreview::SnapPreviewSpan(imageHeight_ * scale, height), height);
    }

    void Paint() {
        const HRESULT hr = EnsureRenderTarget();
        if (FAILED(hr)) return;

        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        renderTarget_->BeginDraw();
        renderTarget_->Clear(D2D1::ColorF(0x202125));
        renderTarget_->DrawBitmap(bitmap_.Get(), ImageRect(width, height),
                                  1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        if (renderTarget_->EndDraw() == D2DERR_RECREATE_TARGET) {
            bitmap_.Reset();
            renderTarget_.Reset();
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }

    void ZoomAtCursor(int wheelDelta, POINT screenCursor) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width <= 0 || height <= 0) return;

        POINT cursor = screenCursor;
        ScreenToClient(hwnd_, &cursor);
        const auto oldRect = ImageRect(width, height);
        const double oldWidth = oldRect.right - oldRect.left;
        const double oldHeight = oldRect.bottom - oldRect.top;
        if (oldWidth <= 0.0 || oldHeight <= 0.0) return;

        // Remember the image point under the mouse before changing either
        // zoom or window geometry. Works when a small preview grows to fit.
        const double relativeX = (cursor.x - oldRect.left) / oldWidth;
        const double relativeY = (cursor.y - oldRect.top) / oldHeight;
        const double nextZoom = std::clamp(
            zoom_ * std::pow(1.15, static_cast<double>(wheelDelta) / WHEEL_DELTA),
            kMinZoom, kMaxZoom);
        if (nextZoom == zoom_) return;

        const auto nextBounds = PreviewBounds(nextZoom);
        const double scale = BaseScale() * nextZoom;
        const double newImageWidth = imageWidth_ * scale;
        const double newImageHeight = imageHeight_ * scale;
        const double newLocalCursorX = screenCursor.x - nextBounds.left;
        const double newLocalCursorY = screenCursor.y - nextBounds.top;
        panX_ = newLocalCursorX - relativeX * newImageWidth -
                (nextBounds.width - newImageWidth) * 0.5;
        panY_ = newLocalCursorY - relativeY * newImageHeight -
                (nextBounds.height - newImageHeight) * 0.5;
        zoom_ = nextZoom;
        SetWindowPos(hwnd_, nullptr, nextBounds.left, nextBounds.top,
                     nextBounds.width, nextBounds.height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        ConstrainPan();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd, &ps);
            Paint();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_SIZE:
            if (renderTarget_ && LOWORD(lParam) > 0 && HIWORD(lParam) > 0) {
                renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED:
        case WM_DISPLAYCHANGE:
        case WM_SETTINGCHANGE:
            RefreshMonitorPlacement();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case sagepreview::kListenerNavigate:
            if (wParam == VK_LEFT || wParam == VK_RIGHT) Navigate(wParam == VK_LEFT ? -1 : 1);
            return 0;
        case sagepreview::kListenerClose:
            DestroyWindow(hwnd);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_LEFT || wParam == VK_RIGHT) {
                // Ignore OS key-repeat; each physical keypress advances once.
                if ((lParam & (1LL << 30)) == 0)
                    Navigate(wParam == VK_LEFT ? -1 : 1);
                return 0;
            }
            if (wParam == VK_ESCAPE || wParam == VK_SPACE) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (wParam == '0') {
                zoom_ = 1.0;
                panX_ = 0.0;
                panY_ = 0.0;
                RefreshImagePlacement();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_MOUSEWHEEL: {
            POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ZoomAtCursor(GET_WHEEL_DELTA_WPARAM(wParam), cursor);
            return 0;
        }
        case WM_LBUTTONDOWN:
            dragging_ = true;
            dragStart_ = POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCapture(hwnd);
            return 0;
        case WM_MOUSEMOVE:
            if (dragging_) {
                const POINT current{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                panX_ += current.x - dragStart_.x;
                panY_ += current.y - dragStart_.y;
                ConstrainPan();
                dragStart_ = current;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (dragging_) {
                dragging_ = false;
                ReleaseCapture();
            }
            return 0;
        case WM_CAPTURECHANGED:
            dragging_ = false;
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    HWND hwnd_ = nullptr;
    HWND overlay_ = nullptr;
    RECT monitorRect_{};
    RECT workRect_{};
    std::wstring filename_;
    std::wstring decodeError_;
    UINT imageWidth_ = 0;
    UINT imageHeight_ = 0;
    double zoom_ = 1.0;
    double panX_ = 0.0;
    double panY_ = 0.0;
    bool dragging_ = false;
    POINT dragStart_{};
    bool navigationLoaded_ = false;
    sagepreview::SiblingImages navigation_;

    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;
    ComPtr<IWICBitmapSource> imageSource_;
    ComPtr<ID2D1HwndRenderTarget> renderTarget_;
    ComPtr<ID2D1Bitmap> bitmap_;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring filename;
    if (argv && argc >= 2) filename = argv[1];
    if (argv) LocalFree(argv);
    if (filename.empty()) filename = ChooseImageFile();
    if (filename.empty()) return 0; // User canceled the Open dialog.

    // An IDE's working directory can be deep inside out/build/x64-Debug.
    // Resolve relative paths and distinguish nonexistent files from bad codecs.
    std::error_code ec;
    const auto absolutePath = std::filesystem::absolute(std::filesystem::path(filename), ec);
    if (!ec) filename = absolutePath.wstring();
    const DWORD attributes = GetFileAttributesW(filename.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        const std::wstring message =
            L"File not found (or unavailable):\n" + filename +
            L"\n\nIf you passed a relative path in Visual Studio, use an absolute path "
            L"or run the application without arguments to choose a file.";
        MessageBoxW(nullptr, message.c_str(), L"SagePreview — missing file",
                    MB_OK | MB_ICONERROR);
        return 2;
    }

    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comHr)) {
        MessageBoxW(nullptr, L"Could not initialize COM.", L"SagePreview", MB_ICONERROR);
        return 1;
    }

    int exitCode = 1;
    {
        PreviewApp app;
        const HRESULT hr = app.Initialize(filename);
        if (FAILED(hr)) {
            std::wstring message = L"Could not open image:\n" + filename +
                                   L"\n\nWindows error: " + HResultText(hr);
            if (!app.ErrorDetails().empty())
                message += L"\n\n" + app.ErrorDetails();
            MessageBoxW(nullptr, message.c_str(), L"SagePreview", MB_OK | MB_ICONERROR);
        } else {
            exitCode = app.Run(instance, showCommand);
        }
    }
    CoUninitialize();
    return exitCode;
}
