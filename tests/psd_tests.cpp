#include "psd.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
struct Memory { std::vector<std::uint8_t> bytes; std::uint64_t readBytes = 0; };
bool ReadAt(void* ctx, std::uint64_t offset, void* dst, std::size_t n) {
    auto& memory = *static_cast<Memory*>(ctx);
    if (offset > memory.bytes.size() || n > memory.bytes.size() - offset) return false;
    if (n) std::copy_n(memory.bytes.data() + static_cast<std::size_t>(offset), n,
                        static_cast<std::uint8_t*>(dst));
    memory.readBytes += n;
    return true;
}
std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
const std::uint8_t kExpected[] = {
    0,0,255,255, 0,255,0,255, 255,0,0,255,
    100,90,80,255, 220,64,45,255, 128,128,128,255
};
bool Compare(const sagepreview::PsdImage& image, bool alpha) {
    if (image.width != 3 || image.height != 2 || image.bgra.size() != sizeof(kExpected))
        return false;
    for (std::size_t i = 0; i < image.bgra.size(); ++i) {
        std::uint8_t expected = kExpected[i];
        if (alpha && (i % 4 == 3)) {
            constexpr std::uint8_t a[] = {255,255,128,0,255,96};
            expected = a[i/4];
        }
        if (image.bgra[i] != expected) return false;
    }
    return true;
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto folder = std::filesystem::path(argv[1]);
    for (const char* name : {
        "synthetic-raw.psd", "synthetic-rle.psd", "synthetic-zip.psd",
        "synthetic-zip-pred.psd", "synthetic-rgba.psd", "synthetic-rgb16.psd"}) {
        Memory memory{ReadFile(folder / name)};
        sagepreview::PsdImage output;
        std::string error;
        if (memory.bytes.empty() ||
            !sagepreview::DecodePsdCompositeFromReader(memory.bytes.size(), &ReadAt,
                &memory, output, error) ||
            !Compare(output, std::string(name) == "synthetic-rgba.psd")) {
            std::cerr << name << " failed: " << error << '\n'; return 1;
        }
        sagepreview::PsdImage fromPath;
        if (!sagepreview::DecodePsdComposite(folder / name, fromPath, error) ||
            fromPath.bgra != output.bgra) {
            std::cerr << name << " path decoder failed: " << error << '\n'; return 1;
        }
        if (memory.readBytes > 1'000'000) {
            std::cerr << name << " excessively read entire PSD.\n"; return 1;
        }
    }
    for (const char* name : {"synthetic-gray.psd", "synthetic-gray-alpha.psd"}) {
        Memory memory{ReadFile(folder / name)};
        sagepreview::PsdImage image;
        std::string error;
        if (!sagepreview::DecodePsdCompositeFromReader(memory.bytes.size(), &ReadAt,
                &memory, image, error) || image.width != 3 || image.height != 2 ||
            image.bgra[0] != 0 || image.bgra[4] != 127 || image.bgra[8] != 255 ||
            image.bgra[11] != (std::string(name) == "synthetic-gray-alpha.psd" ? 128 : 255)) {
            std::cerr << "Grayscale failed: " << name << " " << error << '\n'; return 1;
        }
    }
    for (const char* name : {"synthetic-truncated.psd", "synthetic-psb.psd",
        "synthetic-bad-mode.psd", "synthetic-too-big.psd",
        "synthetic-zip-pred16.psd", "synthetic-bad-rle.psd"}) {
        Memory memory{ReadFile(folder / name)};
        sagepreview::PsdImage image;
        std::string error;
        if (sagepreview::DecodePsdCompositeFromReader(memory.bytes.size(), &ReadAt,
                &memory, image, error) || error.empty() || !image.bgra.empty()) {
            std::cerr << "Invalid PSD unexpectedly accepted: " << name << '\n'; return 1;
        }
    }
    sagepreview::PsdImage image;
    std::string error;
    if (sagepreview::DecodePsdCompositeFromReader(0, nullptr, nullptr, image, error))
        return 1;
    std::cout << "PSD raw, RLE, ZIP, ZIP predictor, 16-bit, grayscale, alpha and invalid-file tests passed.\n";
    return 0;
}
