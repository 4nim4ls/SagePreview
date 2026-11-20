// Small, bounded Adobe PSD v1 composite decoder. Does not parse layer pixels.
// Based on Adobe Photoshop File Formats Specification, Image Data Section.
#include "psd.h"
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <vector>

namespace sagepreview {
namespace {
constexpr std::uint64_t kMaximumPixels = 25'000'000;
constexpr std::uint64_t kMaximumDecodedBytes = 160ull * 1024 * 1024;
constexpr std::size_t kChunk = 64 * 1024;

std::uint16_t Big16(const std::uint8_t* p) {
    return (static_cast<std::uint16_t>(p[0]) << 8) | p[1];
}
std::uint32_t Big32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}
bool Fail(std::string& error, const char* why) { error = why; return false; }
bool Fits(std::uint64_t offset, std::uint64_t length, std::uint64_t size) {
    return offset <= size && length <= size - offset;
}
struct Reader {
    std::uint64_t size;
    PsdReadAtCallback read;
    void* opaque;
    bool At(std::uint64_t offset, void* output, std::size_t length) const {
        return Fits(offset, length, size) && read(opaque, offset, output, length);
    }
    bool Chunked(std::uint64_t offset, std::uint8_t* output, std::uint64_t count) const {
        for (std::uint64_t consumed = 0; consumed < count;) {
            const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(kChunk, count - consumed));
            if (!At(offset + consumed, output + static_cast<std::size_t>(consumed), chunk)) return false;
            consumed += chunk;
        }
        return true;
    }
};

bool UnpackPackBits(const std::uint8_t* packed, std::size_t packedSize,
                    std::uint8_t* decoded, std::size_t decodedSize) {
    std::size_t in = 0, out = 0;
    while (in < packedSize) {
        const auto code = static_cast<std::int8_t>(packed[in++]);
        if (code >= 0) {
            const auto count = static_cast<std::size_t>(code) + 1;
            if (count > packedSize - in || count > decodedSize - out) return false;
            std::memcpy(decoded + out, packed + in, count);
            in += count;
            out += count;
        } else if (code != -128) {
            const auto count = static_cast<std::size_t>(1 - static_cast<int>(code));
            if (in >= packedSize || count > decodedSize - out) return false;
            std::memset(decoded + out, packed[in++], count);
            out += count;
        }
    }
    return out == decodedSize;
}

bool DecodeZip(const Reader& reader, std::uint64_t offset, std::vector<std::uint8_t>& planar,
               std::string& error) {
    std::array<std::uint8_t, kChunk> incoming{};
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) return Fail(error, "Unable to initialize PSD ZIP decoder.");
    std::uint64_t position = offset;
    std::size_t written = 0;
    bool good = false;
    // Never allocate from attacker-declared zlib sizes: destination was bounded
    // by PSD dimensions and channels before we enter this function.
    while (true) {
        if (!stream.avail_in && position < reader.size) {
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(incoming.size(), reader.size - position));
            if (!reader.At(position, incoming.data(), count)) break;
            position += count;
            stream.next_in = incoming.data();
            stream.avail_in = static_cast<uInt>(count);
        }
        // Allow zlib to consume its trailing checksum even if the destination
        // filled exactly; an extra decoded byte is an invalid overlong image.
        std::uint8_t overflowByte{};
        const auto room = static_cast<uInt>(
            std::min<std::size_t>(planar.size() - written, kChunk));
        stream.next_out = room ? planar.data() + written : &overflowByte;
        stream.avail_out = room ? room : 1u;
        const int rc = inflate(&stream, Z_NO_FLUSH);
        const auto produced = static_cast<std::size_t>((room ? room : 1u) - stream.avail_out);
        if (!room && produced) break;
        written += produced;
        if (rc == Z_STREAM_END) { good = written == planar.size(); break; }
        if (rc != Z_OK || (produced == 0 && !stream.avail_in && position == reader.size)) break;
    }
    inflateEnd(&stream);
    if (!good) return Fail(error, "PSD ZIP composite is truncated, oversized, or invalid.");
    return true;
}

bool DecodeRle(const Reader& reader, std::uint64_t offset,
               std::uint32_t width, std::uint32_t height, std::uint16_t channels,
               std::uint32_t bytesPerPixel, std::vector<std::uint8_t>& planar,
               std::string& error) {
    const auto count = static_cast<std::size_t>(channels) * height;
    const auto rowBytes = static_cast<std::size_t>(width) * bytesPerPixel;
    const auto tableBytes = count * 2;
    if (!Fits(offset, tableBytes, reader.size))
        return Fail(error, "PSD RLE scanline-length table is truncated.");
    std::vector<std::uint8_t> table(tableBytes);
    if (!reader.Chunked(offset, table.data(), tableBytes))
        return Fail(error, "Could not read PSD RLE scanline lengths.");
    offset += tableBytes;
    std::vector<std::uint8_t> encoded;
    for (std::size_t row = 0; row < count; ++row) {
        const auto bytes = Big16(table.data() + row * 2);
        if (!Fits(offset, bytes, reader.size))
            return Fail(error, "Truncated PSD RLE scanline.");
        encoded.resize(bytes);
        if (!reader.At(offset, encoded.data(), encoded.size()) ||
            !UnpackPackBits(encoded.data(), encoded.size(),
                            planar.data() + row * rowBytes, rowBytes))
            return Fail(error, "Invalid PackBits compression in PSD composite.");
        offset += bytes;
    }
    return true;
}

bool SkipSection(const Reader& reader, std::uint64_t& cursor, std::string& error) {
    std::array<std::uint8_t, 4> length{};
    if (!reader.At(cursor, length.data(), length.size()))
        return Fail(error, "PSD header section is truncated.");
    cursor += 4;
    const auto n = Big32(length.data());
    if (!Fits(cursor, n, reader.size)) return Fail(error, "PSD header section exceeds file bounds.");
    cursor += n;
    return true;
}
} // namespace

bool DecodePsdCompositeFromReader(std::uint64_t size, PsdReadAtCallback readAt,
                                  void* context, PsdImage& image, std::string& error) {
    image = {};
    error.clear();
    if (!readAt || size < 26) return Fail(error, "Not a complete PSD file.");
    const Reader reader{size, readAt, context};
    std::array<std::uint8_t, 26> h{};
    if (!reader.At(0, h.data(), h.size()) || std::memcmp(h.data(), "8BPS", 4) != 0)
        return Fail(error, "Not an Adobe PSD file.");
    if (Big16(h.data() + 4) != 1) return Fail(error, "PSB or unsupported PSD version.");
    for (std::size_t i = 6; i < 12; ++i)
        if (h[i]) return Fail(error, "PSD reserved header bytes must be zero.");
    const auto channels = Big16(h.data() + 12);
    const auto height = Big32(h.data() + 14);
    const auto width = Big32(h.data() + 18);
    const auto depth = Big16(h.data() + 22);
    const auto mode = Big16(h.data() + 24);
    if (!width || !height || width > 30000 || height > 30000 ||
        static_cast<std::uint64_t>(width) * height > kMaximumPixels)
        return Fail(error, "PSD dimensions exceed the configured preview safety limit.");
    if (depth != 8 && depth != 16)
        return Fail(error, "Only 8-bit and 16-bit PSD composites are supported.");
    if (!((mode == 3 && (channels == 3 || channels == 4)) ||
          (mode == 1 && (channels == 1 || channels == 2))))
        return Fail(error, "Only RGB and grayscale PSD composites are supported (no CMYK/Lab). ");
    const auto rowBytes = static_cast<std::uint64_t>(width) * (depth / 8u);
    const auto planeBytes = rowBytes * height;
    const auto decodedBytes = planeBytes * channels;
    if (decodedBytes > kMaximumDecodedBytes)
        return Fail(error, "PSD composite exceeds the configured decoding memory limit.");
    std::uint64_t cursor = 26;
    for (int i = 0; i < 3; ++i) {
        if (!SkipSection(reader, cursor, error)) return false;
    }
    std::array<std::uint8_t, 2> compressionBytes{};
    if (!reader.At(cursor, compressionBytes.data(), compressionBytes.size()))
        return Fail(error, "PSD has no saved composite image.");
    cursor += 2;
    const auto compression = Big16(compressionBytes.data());
    if (compression > 3) return Fail(error, "Unsupported PSD composite compression.");
    if (compression == 3 && depth == 16)
        return Fail(error, "16-bit PSD ZIP-prediction is not supported.");
    std::vector<std::uint8_t> planar(static_cast<std::size_t>(decodedBytes));
    if (compression == 0) {
        if (!Fits(cursor, decodedBytes, size) ||
            !reader.Chunked(cursor, planar.data(), decodedBytes))
            return Fail(error, "PSD raw composite data is truncated.");
    } else if (compression == 1) {
        if (!DecodeRle(reader, cursor, width, height, channels, depth / 8u, planar, error))
            return false;
    } else {
        if (!DecodeZip(reader, cursor, planar, error)) return false;
        if (compression == 3) {
            const auto row = static_cast<std::size_t>(rowBytes);
            for (std::size_t start = 0; start < planar.size(); start += row)
                for (std::size_t at = start + 1; at < start + row; ++at)
                    planar[at] = static_cast<std::uint8_t>(planar[at] + planar[at - 1]);
        }
    }
    const auto pixels = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> bgra(pixels * 4);
    const auto sampleStride = static_cast<std::size_t>(depth / 8);
    const auto planeLength = static_cast<std::size_t>(planeBytes);
    auto sample = [&](std::size_t channel, std::size_t at) -> std::uint8_t {
        // 16-bit values are big-endian in PSD; the high byte is a fast,
        // non-color-managed 16-bit-to-8-bit downconversion.
        return planar[channel * planeLength + at * sampleStride];
    };
    for (std::size_t i = 0; i < pixels; ++i) {
        const auto grayOrRed = sample(0, i);
        bgra[i * 4 + 0] = mode == 1 ? grayOrRed : sample(2, i);
        bgra[i * 4 + 1] = mode == 1 ? grayOrRed : sample(1, i);
        bgra[i * 4 + 2] = grayOrRed;
        bgra[i * 4 + 3] = ((mode == 3 && channels == 4) ||
                            (mode == 1 && channels == 2))
                               ? sample(channels - 1, i) : 255;
    }
    image.width = width;
    image.height = height;
    image.bgra = std::move(bgra);
    return true;
}

bool DecodePsdComposite(const std::filesystem::path& file,
                        PsdImage& image, std::string& error) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) return Fail(error, "Could not open PSD file.");
    const auto end = in.tellg();
    if (end < 0) return Fail(error, "Could not determine PSD file size.");
    struct Input { std::ifstream* stream; } input{&in};
    auto at = [](void* context, std::uint64_t offset, void* output,
                 std::size_t count) -> bool {
        auto* s = static_cast<Input*>(context)->stream;
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
            count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            return false;
        s->clear();
        s->seekg(static_cast<std::streamoff>(offset));
        if (!*s) return false;
        s->read(static_cast<char*>(output), static_cast<std::streamsize>(count));
        return static_cast<std::size_t>(s->gcount()) == count;
    };
    return DecodePsdCompositeFromReader(static_cast<std::uint64_t>(end), at,
                                         &input, image, error);
}
} // namespace sagepreview
