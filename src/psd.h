#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sagepreview {
using PsdReadAtCallback = bool (*)(void* context, std::uint64_t offset,
                                   void* destination, std::size_t size);

struct PsdImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Unpremultiplied, top-down BGRA8, suitable for WIC's 32bppBGRA format.
    std::vector<std::uint8_t> bgra;
};

// PSD v1 saved *composite* only. No layers, smart objects or proprietary
// reconstruction. RGB/gray 8- and 16-bit, Raw/RLE/ZIP; ZIP prediction for 8-bit.
// 32-bit, CMYK/Lab, indexed/palette, PSB, and 16-bit ZIP predictor are
// intentionally rejected. Caller receives a specific reason.
bool DecodePsdCompositeFromReader(std::uint64_t size, PsdReadAtCallback readAt,
                                  void* context, PsdImage& image, std::string& error);
bool DecodePsdComposite(const std::filesystem::path& file,
                        PsdImage& image, std::string& error);
} // namespace sagepreview
