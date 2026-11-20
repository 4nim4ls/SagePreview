// Diagnose AVIF decoding availability without installing or registering anything.
// Return: 0 = successful full decode; 77 = no usable WIC codec; 1 = other error.
#include <windows.h>
#include <ole2.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <iostream>
#include <iomanip>
#include <cstdint>
#include <limits>

using Microsoft::WRL::ComPtr;

int wmain(int argc, wchar_t* argv[]) {
    if (argc != 2) {
        std::wcerr << L"Usage: SageAvifCodecCheck.exe <test.avif>\n";
        return 1;
    }
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(init)) return 1;
    int result = 1;
    {
        ComPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(factory.GetAddressOf()));
        if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
        if (SUCCEEDED(hr)) {
            ComPtr<IWICBitmapDecoder> decoder;
            hr = factory->CreateDecoderFromFilename(argv[1], nullptr, GENERIC_READ,
                 WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
            if (FAILED(hr)) {
                std::wcerr << L"No WIC decoder accepted this AVIF. HRESULT=0x"
                           << std::hex << static_cast<unsigned>(hr) << L"\n"
                           << L"If the file is valid, check HEIF Image Extensions and "
                           << L"AV1 Video Extension in Windows.\n";
                result = 77;
            } else {
                ComPtr<IWICBitmapFrameDecode> frame;
                hr = decoder->GetFrame(0, frame.GetAddressOf());
                UINT width=0, height=0;
                if (SUCCEEDED(hr)) hr = frame->GetSize(&width, &height);
                if (SUCCEEDED(hr) && width > 0 && height > 0 &&
                    static_cast<std::uint64_t>(width) * height <= 100'000'000ull) {
                    ComPtr<IWICFormatConverter> converter;
                    hr = factory->CreateFormatConverter(converter.GetAddressOf());
                    if (SUCCEEDED(hr)) hr = converter->Initialize(frame.Get(),
                        GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                        nullptr, 0.0, WICBitmapPaletteTypeCustom);
                    // Force actual decode, not just recognizing the AVIF header.
                    if (SUCCEEDED(hr)) {
                        BYTE sample[4]{};
                        const WICRect onePixel{0, 0, 1, 1};
                        hr = converter->CopyPixels(&onePixel, 4, 4, sample);
                    }
                    if (SUCCEEDED(hr)) {
                        std::wcout << L"AVIF decoded successfully: " << width << L"x"
                                   << height << L". Windows WIC codec is usable.\n";
                        result = 0;
                    }
                }
                if (result != 0) {
                    std::wcerr << L"WIC recognizes the file but could not decode "
                               << L"its pixels. HRESULT=0x" << std::hex
                               << static_cast<unsigned>(hr) << L"\n";
                }
            }
        }
    }
    CoUninitialize();
    return result;
}
