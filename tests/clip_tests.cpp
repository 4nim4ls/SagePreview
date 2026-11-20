#include "clip.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: SageClipTests <synthetic-test.clip>\n";
        return 2;
    }
    const std::filesystem::path clip = argv[1];
    const auto expected = ReadFile(clip.parent_path() / "preview-test.png");
    if (expected.empty()) {
        std::cerr << "Could not read expected PNG fixture.\n";
        return 2;
    }
    std::vector<std::uint8_t> actual;
    std::string error;
    if (!sagepreview::ExtractClipPreview(clip, actual, error)) {
        std::cerr << "Synthetic .clip failed: " << error << "\n";
        return 1;
    }
    if (actual != expected) {
        std::cerr << "Extracted PNG does not match the expected bytes.\n";
        return 1;
    }
    // If the Head's fast pointer is invalid, the guarded chunk walk should work.
    actual.clear();
    if (!sagepreview::ExtractClipPreview(clip.parent_path() / "synthetic-fallback.clip",
                                         actual, error) || actual != expected) {
        std::cerr << "Chunk-walk fallback failed: " << error << "\n";
        return 1;
    }
    // A nonexistent file must fail gracefully, not report success.
    actual.clear();
    if (sagepreview::ExtractClipPreview(clip.parent_path() / "nonexistent-clip-fixture.clip",
                                        actual, error) || actual.size() != 0) {
        std::cerr << "Missing file was not rejected.\n";
        return 1;
    }
    std::cout << "CLIP extraction, chunk-walk fallback, and missing-file checks passed.\n";
    return 0;
}
