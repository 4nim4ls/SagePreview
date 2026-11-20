#include "kra.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {
struct Memory { std::vector<std::uint8_t> data; std::size_t readBytes = 0; };
bool ReadAt(void* opaque, std::uint64_t pos, void* out, std::size_t count) {
    auto& memory = *static_cast<Memory*>(opaque);
    if (pos > memory.data.size() || count > memory.data.size() - pos) return false;
    std::copy_n(memory.data.data() + static_cast<std::size_t>(pos), count,
                static_cast<std::uint8_t*>(out));
    memory.readBytes += count;
    return true;
}
std::vector<std::uint8_t> ReadFile(const std::filesystem::path& name) {
    std::ifstream input(name, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto folder = std::filesystem::path(argv[1]);
    const auto expected = ReadFile(folder / "preview-test.png");
    if (expected.empty()) return 2;
    for (const auto* filename : {"synthetic-deflate.kra", "synthetic-stored.kra",
                                  "synthetic-zip64local.kra", "synthetic-zip64eocd.kra"}) {
        Memory file{ReadFile(folder / filename)};
        if (file.data.empty()) return 3;
        std::vector<std::uint8_t> output;
        std::string error;
        if (!sagepreview::ExtractKraPreviewFromReader(file.data.size(), &ReadAt,
                    &file, output, error) || output != expected) {
            std::cerr << filename << ": failed: " << error << "\n";
            return 1;
        }
        std::vector<std::uint8_t> filenameOutput;
        if (!sagepreview::ExtractKraPreview(folder / filename, filenameOutput, error) ||
            filenameOutput != expected) {
            std::cerr << filename << ": file path version failed: " << error << "\n";
            return 1;
        }
        if (file.readBytes > 2'000'000) {
            std::cerr << "Excessive archive I/O.\n";
            return 1;
        }
    }
    for (const auto* filename : {"synthetic-missing.kra", "synthetic-crc.kra",
                                  "synthetic-duplicate.kra", "synthetic-truncated.kra"}) {
        Memory file{ReadFile(folder / filename)};
        std::vector<std::uint8_t> output;
        std::string error;
        if (sagepreview::ExtractKraPreviewFromReader(file.data.size(), &ReadAt,
                              &file, output, error)) {
            std::cerr << filename << " unexpectedly accepted.\n";
            return 1;
        }
    }
    std::vector<std::uint8_t> output;
    std::string error;
    if (sagepreview::ExtractKraPreviewFromReader(0, nullptr, nullptr, output, error))
        return 1;
    std::cout << "Krita stored/deflated/ZIP64-local extraction and rejection tests passed.\n";
    return 0;
}
