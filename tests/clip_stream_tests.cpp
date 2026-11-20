#include "clip.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {
struct InMemoryFile {
    std::vector<std::uint8_t> data;
    std::size_t calls = 0;
    std::size_t totalBytes = 0;
};
bool ReadAt(void* opaque, std::uint64_t position, void* output, std::size_t n) {
    auto& file = *static_cast<InMemoryFile*>(opaque);
    ++file.calls;
    if (position > file.data.size() || n > file.data.size() - position)
        return false;
    std::copy_n(file.data.data() + position, n, static_cast<std::uint8_t*>(output));
    file.totalBytes += n;
    return true;
}
std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::filesystem::path fixture(argv[1]);
    const auto expected = ReadFile(fixture.parent_path() / "preview-test.png");
    if (expected.empty()) return 2;
    for (const char* name : {"synthetic-test.clip", "synthetic-fallback.clip"}) {
        InMemoryFile input{ReadFile(fixture.parent_path() / name)};
        std::vector<std::uint8_t> png;
        std::string error;
        if (!sagepreview::ExtractClipPreviewFromReader(input.data.size(),
                     &ReadAt, &input, png, error) || png != expected) {
            std::cerr << "Stream extraction failed: " << name << " / " << error << "\n";
            return 1;
        }
        if (input.calls > 20 || input.totalBytes >= input.data.size() * 2) {
            std::cerr << "Too many file reads.\n";
            return 1;
        }
    }
    InMemoryFile corrupt{ReadFile(fixture)};
    std::vector<std::uint8_t> png;
    std::string error;
    if (sagepreview::ExtractClipPreviewFromReader(0, &ReadAt, &corrupt, png, error) ||
        sagepreview::ExtractClipPreviewFromReader(corrupt.data.size(),
               nullptr, &corrupt, png, error) ||
        sagepreview::ExtractClipPreviewFromReader(24, &ReadAt, &corrupt, png, error)) {
        std::cerr << "Invalid reader or truncated stream was accepted.\n";
        return 1;
    }
    std::cout << "Seekable read-only byte-source .clip tests passed.\n";
    return 0;
}
