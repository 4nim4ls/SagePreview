// SagePreview .psd thumbnail provider, Windows 11 x64.
// In-process COM DLL hosted by the Shell's default isolated thumbnail host.
// Implements the Shell-required IInitializeWithStream interface; never asks
// Windows to disable thumbnail-handler process isolation.

#include <windows.h>
#include <ole2.h>
#include <propsys.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "psd.h"
#include "psd_thumbnail_ids.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
std::atomic<long> g_liveObjects{0};
std::atomic<long> g_serverLocks{0};
constexpr UINT kMaximumRequestedSize = 4096;
constexpr std::uint64_t kMaximumSourcePixels = 100'000'000;

bool ReadStreamAt(void* context, std::uint64_t offset,
                  void* destination, std::size_t count) {
    if (count > std::numeric_limits<ULONG>::max() ||
        offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()))
        return false;
    auto* stream = static_cast<IStream*>(context);
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (FAILED(stream->Seek(position, STREAM_SEEK_SET, nullptr))) return false;
    // IStream implementations can return a partial read even before EOF.
    // Continue until the requested range has actually arrived.
    auto* bytes = static_cast<BYTE*>(destination);
    std::size_t remaining = count;
    while (remaining != 0) {
        ULONG actual = 0;
        const HRESULT hr = stream->Read(bytes, static_cast<ULONG>(remaining), &actual);
        if (FAILED(hr) || actual == 0 || actual > remaining) return false;
        bytes += actual;
        remaining -= actual;
    }
    return true;
}

HRESULT StreamSize(IStream* stream, std::uint64_t& size) {
    STATSTG stat{};
    if (SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME))) {
        size = stat.cbSize.QuadPart;
        return S_OK;
    }
    // Some marshaled streams do not support Stat; seeking to end is enough.
    LARGE_INTEGER offset{};
    ULARGE_INTEGER end{};
    const HRESULT hr = stream->Seek(offset, STREAM_SEEK_END, &end);
    if (SUCCEEDED(hr)) size = end.QuadPart;
    return hr;
}

HRESULT PsdToBitmap(const sagepreview::PsdImage& image, UINT requestedSize,
                    HBITMAP* output, WTS_ALPHATYPE* alphaType) {
    if (!image.width || !image.height || image.bgra.empty() ||
        static_cast<std::uint64_t>(image.width) * image.height > kMaximumSourcePixels)
        return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                              CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));
    }
    if (FAILED(hr)) return hr;
    ComPtr<IWICBitmap> input;
    hr = factory->CreateBitmapFromMemory(image.width, image.height,
        GUID_WICPixelFormat32bppBGRA, image.width * 4u,
        static_cast<UINT>(image.bgra.size()),
        const_cast<BYTE*>(image.bgra.data()), input.GetAddressOf());
    if (FAILED(hr)) return hr;
    const double ratio = std::min({1.0, static_cast<double>(requestedSize) / image.width,
                                   static_cast<double>(requestedSize) / image.height});
    const UINT outputWidth = std::max(1u, static_cast<UINT>(std::lround(image.width * ratio)));
    const UINT outputHeight = std::max(1u, static_cast<UINT>(std::lround(image.height * ratio)));
    ComPtr<IWICBitmapScaler> scaler;
    IWICBitmapSource* source = input.Get();
    if (outputWidth != image.width || outputHeight != image.height) {
        hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
        if (FAILED(hr)) return hr;
        hr = scaler->Initialize(input.Get(), outputWidth, outputHeight,
                                WICBitmapInterpolationModeFant);
        if (FAILED(hr)) return hr;
        source = scaler.Get();
    }
    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return hr;
    hr = converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return hr;
    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(outputWidth);
    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(outputHeight);
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP result = CreateDIBSection(nullptr, &bitmapInfo, DIB_RGB_COLORS,
                                       &pixels, nullptr, 0);
    if (!result || !pixels) return HRESULT_FROM_WIN32(GetLastError());
    const UINT stride = outputWidth * 4;
    const UINT bufferSize = stride * outputHeight;
    hr = converter->CopyPixels(nullptr, stride, bufferSize,
                               static_cast<BYTE*>(pixels));
    if (FAILED(hr)) { DeleteObject(result); return hr; }
    *output = result;
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
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --refCount_;
        if (remaining == 0) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE Initialize(IStream* input, DWORD mode) override {
        if (!input) return E_INVALIDARG;
        // Thumbnail reading must never modify an image provided by the Shell.
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

        std::uint64_t length = 0;
        HRESULT hr = StreamSize(stream_.Get(), length);
        if (FAILED(hr)) return hr;
        sagepreview::PsdImage composite;
        std::string error;
        if (!sagepreview::DecodePsdCompositeFromReader(
                length, &ReadStreamAt, stream_.Get(), composite, error)) {
            return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
        }
        return PsdToBitmap(composite, cx, bitmap, alphaType);
    }

private:
    std::atomic<ULONG> refCount_{1};
    std::mutex mutex_;  // Seek + Read is a combined operation for this IStream.
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
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --refCount_;
        if (remaining == 0) delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid,
                                               void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* provider = new (std::nothrow) ThumbnailProvider();
        if (!provider) return E_OUTOFMEMORY;
        const HRESULT hr = provider->QueryInterface(iid, result);
        provider->Release(); // balance initial ref after QueryInterface
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) ++g_serverLocks;
        else --g_serverLocks;
        return S_OK;
    }
private:
    std::atomic<ULONG> refCount_{1};
};
} // namespace

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* result) {
    if (!result) return E_POINTER;
    *result = nullptr;
    if (!IsEqualCLSID(clsid, sagepreview::kClsidPsdThumbnailProvider))
        return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) ThumbnailClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(iid, result);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow(void) {
    return (g_liveObjects.load() == 0 && g_serverLocks.load() == 0)
        ? S_OK : S_FALSE;
}
