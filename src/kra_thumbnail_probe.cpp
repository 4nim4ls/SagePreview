// Exerciser for SageKraThumbnailProvider.dll WITHOUT COM registration or Explorer.
// Usage: SageThumbProbe.exe input.kra output.png [requested-pixels]

#include <windows.h>
#include <ole2.h>
#include <ocidl.h>
#include <propsys.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <wrl/client.h>

#include "kra_thumbnail_ids.h"

#include <filesystem>
#include <iterator>
#include <cwchar>
#include <iostream>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {
using GetClassObjectFn = HRESULT (__stdcall *)(REFCLSID, REFIID, void**);

std::wstring ErrorCode(HRESULT hr) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned>(hr));
    return buffer;
}

HRESULT WritePng(HBITMAP bitmap, const std::wstring& filename) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return hr;
    ComPtr<IWICBitmap> source;
    hr = factory->CreateBitmapFromHBITMAP(bitmap, nullptr,
                                           WICBitmapUseAlpha, source.GetAddressOf());
    if (FAILED(hr)) return hr;
    UINT width = 0, height = 0;
    hr = source->GetSize(&width, &height);
    if (FAILED(hr)) return hr;

    ComPtr<IWICStream> output;
    hr = factory->CreateStream(output.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = output->InitializeFromFilename(filename.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) return hr;
    ComPtr<IWICBitmapEncoder> encoder;
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                                encoder.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = encoder->Initialize(output.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) return hr;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), options.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = frame->Initialize(options.Get());
    if (FAILED(hr)) return hr;
    hr = frame->SetSize(width, height);
    if (FAILED(hr)) return hr;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&format);
    if (FAILED(hr)) return hr;
    hr = frame->WriteSource(source.Get(), nullptr);
    if (FAILED(hr)) return hr;
    hr = frame->Commit();
    if (FAILED(hr)) return hr;
    return encoder->Commit();
}

HRESULT Probe(const std::wstring& file, const std::wstring& outputFile, UINT size) {
    // Load the DLL from our build folder explicitly, without COM registration.
    wchar_t executablePath[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executablePath,
                                             static_cast<DWORD>(std::size(executablePath)));
    if (length == 0 || length == std::size(executablePath))
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    const auto dllPath = std::filesystem::path(executablePath).parent_path() /
                         L"SageKraThumbnailProvider.dll";
    HMODULE dll = LoadLibraryExW(dllPath.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                 LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) return HRESULT_FROM_WIN32(GetLastError());

    HRESULT hr = E_FAIL;
    // Wrap the DLL lifetime: release all COM objects before unloading it.
    {
        const auto getClassObject = reinterpret_cast<GetClassObjectFn>(
            GetProcAddress(dll, "DllGetClassObject"));
        if (!getClassObject) {
            hr = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        } else {
            ComPtr<IClassFactory> classFactory;
            hr = getClassObject(sagepreview::kClsidKraThumbnailProvider,
                                IID_PPV_ARGS(classFactory.GetAddressOf()));
            if (SUCCEEDED(hr)) {
                ComPtr<IThumbnailProvider> provider;
                hr = classFactory->CreateInstance(nullptr,
                         IID_PPV_ARGS(provider.GetAddressOf()));
                if (SUCCEEDED(hr)) {
                    ComPtr<IInitializeWithStream> initializer;
                    hr = provider.As(&initializer);
                    if (SUCCEEDED(hr)) {
                        ComPtr<IStream> input;
                        hr = SHCreateStreamOnFileEx(file.c_str(),
                                STGM_READ | STGM_SHARE_DENY_NONE,
                                FILE_ATTRIBUTE_NORMAL, FALSE, nullptr,
                                input.GetAddressOf());
                        if (SUCCEEDED(hr)) hr = initializer->Initialize(input.Get(), STGM_READ);
                        if (SUCCEEDED(hr)) {
                            HBITMAP bitmap = nullptr;
                            WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
                            hr = provider->GetThumbnail(size, &bitmap, &alpha);
                            if (SUCCEEDED(hr)) {
                                if (!bitmap) {
                                    hr = E_UNEXPECTED;
                                } else {
                                    BITMAP information{};
                                    GetObjectW(bitmap, sizeof(information), &information);
                                    std::wcout << L"Provider output: " << information.bmWidth
                                               << L" x " << information.bmHeight
                                               << L", alpha type " << alpha << L"\n";
                                    hr = WritePng(bitmap, outputFile);
                                }
                            }
                            if (bitmap) DeleteObject(bitmap);
                        }
                    }
                }
            }
        }
    }
    FreeLibrary(dll);
    return hr;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 3 && argc != 4) {
        std::wcerr << L"Usage: SageThumbProbe.exe <input.kra> <output.png> [pixels: 1..4096]\n";
        return 2;
    }
    UINT size = 256;
    if (argc == 4) {
        wchar_t* end = nullptr;
        const unsigned long value = wcstoul(argv[3], &end, 10);
        if (!argv[3][0] || (end && *end) || value < 1 || value > 4096) {
            std::wcerr << L"Thumbnail size must be between 1 and 4096.\n";
            return 2;
        }
        size = static_cast<UINT>(value);
    }
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init)) {
        std::wcerr << L"COM initialization failed: " << ErrorCode(init) << L"\n";
        return 1;
    }
    const HRESULT hr = Probe(argv[1], argv[2], size);
    CoUninitialize();
    if (FAILED(hr)) {
        std::wcerr << L"Thumbnail probe failed: " << ErrorCode(hr) << L"\n";
        return 1;
    }
    std::wcout << L"Thumbnail written to: " << argv[2] << L"\n";
    return 0;
}
