#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sagepreview {
// Read an exact range from the archive; called only for bounded, validated spans.
using KraReadAtCallback = bool (*)(void* context, std::uint64_t position,
                                   void* output, std::size_t bytes);

// Extract Krita's saved mergedimage.png; never parse or reconstruct paint layers.
// Supports ordinary ZIP and ZIP64 central-directory records with stored/deflated
// entries. Does not write to the archive. Output is bounded to 96 MiB.
bool ExtractKraPreviewFromReader(std::uint64_t size, KraReadAtCallback readAt,
                                 void* context, std::vector<std::uint8_t>& png,
                                 std::string& error);

bool ExtractKraPreview(const std::filesystem::path& path,
                       std::vector<std::uint8_t>& png, std::string& error);
} // namespace sagepreview
