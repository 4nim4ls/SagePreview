// SagePreview v0.9: optional AVIF thumbnail COM provider using existing WIC codecs.
// No codec is bundled. Do not register unless the system decodes real AVIF files.
// The Shell's standard thumbnail-host isolation remains enabled.
#include <windows.h>
#include <ole2.h>
#include <propsys.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "avif_thumbnail_ids.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <new>

using Microsoft::WRL::ComPtr;

namespace {
std::atomic<long> g_liveObjects{0};
std::atomic<long> g_serverLocks{0};
constexpr UINT kMaximumRequestedSize = 4096;
constexpr std::uint64_t kMaximumSourcePixels = 64'000'000;
constexpr UINT kMaximumDimension = 16384;

HRESULT CreateWicFactory(IWICImagingFactory** output) {
    if (!output) return E_POINTER;
    *output = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(output));
    if (FAILED(hr)) {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(output));
    }
    return hr;
}

HRESULT DecodeThumbnail(IStream* stream, UINT requested, HBITMAP* bitmap,
                        WTS_ALPHATYPE* alphaType) {
    // Precondition: pointer and requested size checked by GetThumbnail.
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CreateWicFactory(factory.GetAddressOf());
    if (FAILED(hr)) return hr;

    // Never assume a Shell-provided stream's cursor begins at zero.
    LARGE_INTEGER zero{};
    hr = stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    if (FAILED(hr)) return hr;
    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream, nullptr,
            WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
    if (FAILED(hr)) return hr; // WINCODEC_ERR_COMPONENTNOTFOUND if AVIF codec absent.
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) return hr;
    UINT width = 0, height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr)) return hr;
    if (width == 0 || height == 0 || width > kMaximumDimension ||
        height > kMaximumDimension ||
        static_cast<std::uint64_t>(width) * height > kMaximumSourcePixels)
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);

    const double factor = std::min({1.0,
        static_cast<double>(requested) / width,
        static_cast<double>(requested) / height});
    const UINT outW = std::max(1u, static_cast<UINT>(std::lround(width * factor)));
    const UINT outH = std::max(1u, static_cast<UINT>(std::lround(height * factor)));

    ComPtr<IWICBitmapScaler> scaler;
    IWICBitmapSource* source = frame.Get();
    if (outW != width || outH != height) {
        hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = scaler->Initialize(frame.Get(), outW, outH,
                                WICBitmapInterpolationModeFant);
        if (FAILED(hr)) return hr;
        source = scaler.Get();
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return hr;

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(outW);
    info.bmiHeader.biHeight = -static_cast<LONG>(outH);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP dib = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
                                   &pixels, nullptr, 0);
    if (!dib || !pixels) {
        if (dib) DeleteObject(dib);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    hr = converter->CopyPixels(nullptr, outW * 4u, outW * outH * 4u,
                               static_cast<BYTE*>(pixels));
    if (FAILED(hr)) {
        DeleteObject(dib);
        return hr;
    }
    *bitmap = dib;
    *alphaType = WTSAT_ARGB;
    return S_OK;
}

class ThumbnailProvider final : public IThumbnailProvider, public IInitializeWithStream {
public:
    ThumbnailProvider() { ++g_liveObjects; }
    ~ThumbnailProvider() { --g_liveObjects; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, __uuidof(IThumbnailProvider)))
            *result = static_cast<IThumbnailProvider*>(this);
        else if (IsEqualIID(iid, __uuidof(IInitializeWithStream)))
            *result = static_cast<IInitializeWithStream*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = --refs_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Initialize(IStream* input, DWORD mode) override {
        if (!input) return E_INVALIDARG;
        if ((mode & 3u) != STGM_READ) return STG_E_ACCESSDENIED;
        std::lock_guard lock(mutex_);
        if (stream_) return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        stream_ = input;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT cx, HBITMAP* bitmap,
                                            WTS_ALPHATYPE* alphaType) override {
        if (!bitmap || !alphaType) return E_POINTER;
        *bitmap = nullptr;
        *alphaType = WTSAT_UNKNOWN;
        if (cx == 0 || cx > kMaximumRequestedSize) return E_INVALIDARG;
        std::lock_guard lock(mutex_);
        if (!stream_) return E_UNEXPECTED;
        return DecodeThumbnail(stream_.Get(), cx, bitmap, alphaType);
    }
private:
    std::atomic<ULONG> refs_{1};
    std::mutex mutex_;
    ComPtr<IStream> stream_;
};

class ThumbnailClassFactory final : public IClassFactory {
public:
    ThumbnailClassFactory() { ++g_liveObjects; }
    ~ThumbnailClassFactory() { --g_liveObjects; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (!IsEqualIID(iid, IID_IUnknown) && !IsEqualIID(iid, IID_IClassFactory))
            return E_NOINTERFACE;
        *result = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG remaining = --refs_;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid,
                                               void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* provider = new (std::nothrow) ThumbnailProvider();
        if (!provider) return E_OUTOFMEMORY;
        HRESULT hr = provider->QueryInterface(iid, result);
        provider->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) ++g_serverLocks;
        else --g_serverLocks;
        return S_OK;
    }
private:
    std::atomic<ULONG> refs_{1};
};
} // namespace

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (!IsEqualCLSID(clsid, sagepreview::kClsidAvifThumbnailProvider))
        return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) ThumbnailClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, result);
    factory->Release();
    return hr;
}
STDAPI DllCanUnloadNow(void) {
    return g_liveObjects.load() == 0 && g_serverLocks.load() == 0 ? S_OK : S_FALSE;
}
