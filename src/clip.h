#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sagepreview {

// Random-access byte reader so Windows Shell thumbnails can consume an IStream
// directly. No source filename or temporary copy is required.
using ClipReadAtCallback = bool (*)(void* context, std::uint64_t offset,
                                    void* output, std::size_t count);

bool ExtractClipPreviewFromReader(std::uint64_t fileSize,
                                  ClipReadAtCallback readAt, void* context,
                                  std::vector<std::uint8_t>& png,
                                  std::string& error);


// Extracts the SAVED preview PNG from a Clip Studio Paint .clip file.
// Does not render the complete editable document or modify the source file.
// Uses only the embedded metadata database, skipping large layer payloads.
// Returns false and a human-readable reason for malformed/unsupported files.
bool ExtractClipPreview(const std::filesystem::path& clipPath,
                        std::vector<std::uint8_t>& png,
                        std::string& error);

} // namespace sagepreview
