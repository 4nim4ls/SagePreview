// Krita .kra embedded mergedimage.png extractor; no layer decoding.
// ZIP metadata is parsed with strict bounds, while zlib handles raw DEFLATE.
#include "kra.h"

#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace sagepreview {
namespace {
constexpr std::uint64_t kMaxDirectory = 32ull * 1024 * 1024;
constexpr std::uint64_t kMaxPng = 96ull * 1024 * 1024;
constexpr std::uint64_t kMaxCompressedPng = 128ull * 1024 * 1024;
constexpr std::uint64_t kMaxEntries = 300'000;
constexpr std::size_t kChunk = 64 * 1024;

std::uint16_t U16(const std::uint8_t* b) {
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}
std::uint32_t U32(const std::uint8_t* b) {
    return static_cast<std::uint32_t>(b[0]) |
        (static_cast<std::uint32_t>(b[1]) << 8) |
        (static_cast<std::uint32_t>(b[2]) << 16) |
        (static_cast<std::uint32_t>(b[3]) << 24);
}
std::uint64_t U64(const std::uint8_t* b) {
    return static_cast<std::uint64_t>(U32(b)) | (static_cast<std::uint64_t>(U32(b + 4)) << 32);
}
bool Fits(std::uint64_t offset, std::uint64_t bytes, std::uint64_t total) {
    return offset <= total && bytes <= total - offset;
}
struct Reader {
    std::uint64_t size;
    KraReadAtCallback readAt;
    void* context;
    bool Read(std::uint64_t offset, void* destination, std::size_t count) const {
        return Fits(offset, count, size) && readAt(context, offset, destination, count);
    }
};
bool Fail(std::string& error, const char* message) {
    error = message;
    return false;
}
struct Entry {
    std::uint64_t compressed = 0;
    std::uint64_t uncompressed = 0;
    std::uint64_t localOffset = 0;
    std::uint32_t crc = 0;
    std::uint16_t method = 0;
    std::uint16_t flags = 0;
};

bool LocateMergedImage(const Reader& reader, Entry& target, std::string& error) {
    if (reader.size < 22) return Fail(error, "Not a ZIP/Krita archive.");
    const auto suffixSize = static_cast<std::size_t>(std::min<std::uint64_t>(reader.size, 65557));
    std::vector<std::uint8_t> suffix(suffixSize);
    if (!reader.Read(reader.size - suffixSize, suffix.data(), suffix.size()))
        return Fail(error, "Could not read the ZIP footer.");
    std::size_t end = suffixSize;
    bool found = false;
    for (std::size_t i = suffixSize - 22 + 1; i-- > 0;) {
        if (U32(suffix.data() + i) == 0x06054b50 &&
            i + 22 + U16(suffix.data() + i + 20) == suffixSize) {
            end = i;
            found = true;
            break;
        }
    }
    if (!found) return Fail(error, "ZIP end-of-directory record not found.");
    const auto* e = suffix.data() + end;
    if (U16(e + 4) != 0 || U16(e + 6) != 0 || U16(e + 8) != U16(e + 10))
        return Fail(error, "Multi-volume ZIP archives are unsupported.");
    std::uint64_t entries = U16(e + 10), dirSize = U32(e + 12), dirOffset = U32(e + 16);
    if (entries == 0xffff || dirSize == 0xffffffffu || dirOffset == 0xffffffffu) {
        // ZIP64 locator is immediately before the classic EOCD.
        const auto eocdOffset = reader.size - suffixSize + end;
        if (eocdOffset < 20) return Fail(error, "Missing ZIP64 locator.");
        std::array<std::uint8_t, 20> locator{};
        if (!reader.Read(eocdOffset - 20, locator.data(), locator.size()) ||
            U32(locator.data()) != 0x07064b50 || U32(locator.data() + 4) != 0 ||
            U32(locator.data() + 16) != 1)
            return Fail(error, "Unsupported ZIP64 locator.");
        const auto zip64Offset = U64(locator.data() + 8);
        std::array<std::uint8_t, 56> record{};
        if (!reader.Read(zip64Offset, record.data(), record.size()) ||
            U32(record.data()) != 0x06064b50 || U64(record.data() + 4) < 44 ||
            U32(record.data() + 16) != 0 || U32(record.data() + 20) != 0 ||
            U64(record.data() + 24) != U64(record.data() + 32))
            return Fail(error, "Invalid ZIP64 central-directory record.");
        entries = U64(record.data() + 32);
        dirSize = U64(record.data() + 40);
        dirOffset = U64(record.data() + 48);
    }
    if (!entries || entries > kMaxEntries || dirSize > kMaxDirectory ||
        !Fits(dirOffset, dirSize, reader.size))
        return Fail(error, "ZIP central directory exceeds safety limits.");
    std::vector<std::uint8_t> directory(static_cast<std::size_t>(dirSize));
    if (!reader.Read(dirOffset, directory.data(), directory.size()))
        return Fail(error, "Could not read ZIP central directory.");
    bool matched = false;
    std::size_t cursor = 0;
    for (std::uint64_t i = 0; i < entries; ++i) {
        if (!Fits(cursor, 46, directory.size()))
            return Fail(error, "Truncated ZIP central directory.");
        const auto* h = directory.data() + cursor;
        if (U32(h) != 0x02014b50) return Fail(error, "Bad ZIP directory signature.");
        const auto filenameLength = U16(h + 28);
        const auto extraLength = U16(h + 30);
        const auto commentLength = U16(h + 32);
        const std::size_t fullLength = 46u + filenameLength + extraLength + commentLength;
        if (!Fits(cursor, fullLength, directory.size()))
            return Fail(error, "ZIP directory entry is truncated.");
        constexpr char kName[] = "mergedimage.png";
        if (filenameLength == sizeof(kName) - 1 &&
            std::memcmp(h + 46, kName, sizeof(kName) - 1) == 0) {
            if (matched) return Fail(error, "Ambiguous duplicate mergedimage.png entries.");
            matched = true;
            target.flags = U16(h + 8);
            target.method = U16(h + 10);
            target.crc = U32(h + 16);
            target.compressed = U32(h + 20);
            target.uncompressed = U32(h + 24);
            target.localOffset = U32(h + 42);
            auto disk = static_cast<std::uint32_t>(U16(h + 34));
            const bool bigUncompressed = target.uncompressed == 0xffffffffu;
            const bool bigCompressed = target.compressed == 0xffffffffu;
            const bool bigOffset = target.localOffset == 0xffffffffu;
            const bool bigDisk = disk == 0xffff;
            bool foundZip64 = false;
            std::size_t off = 46u + filenameLength;
            const std::size_t extraEnd = off + extraLength;
            while (off + 4 <= extraEnd) {
                const auto type = U16(h + off);
                const auto length = U16(h + off + 2);
                off += 4;
                if (off + length > extraEnd) return Fail(error, "Malformed ZIP extra data.");
                if (type == 1) {
                    foundZip64 = true;
                    std::size_t at = off;
                    auto take64 = [&](std::uint64_t& value) -> bool {
                        if (at + 8 > off + length) return false;
                        value = U64(h + at); at += 8; return true;
                    };
                    if ((bigUncompressed && !take64(target.uncompressed)) ||
                        (bigCompressed && !take64(target.compressed)) ||
                        (bigOffset && !take64(target.localOffset)))
                        return Fail(error, "Truncated ZIP64 entry sizes.");
                    if (bigDisk) {
                        if (at + 4 > off + length) return Fail(error, "Truncated ZIP64 disk index.");
                        disk = U32(h + at);
                    }
                }
                off += length;
            }
            if (off != extraEnd || ((bigUncompressed || bigCompressed || bigOffset || bigDisk) && !foundZip64) || disk)
                return Fail(error, "Unsupported or malformed ZIP64 entry.");
        }
        cursor += fullLength;
    }
    if (!matched) return Fail(error, "Krita mergedimage.png is missing (possibly a .krz file).");
    if (target.flags & 1u) return Fail(error, "Encrypted ZIP files are unsupported.");
    if (target.method != 0 && target.method != 8)
        return Fail(error, "Krita merged image uses unsupported ZIP compression.");
    if (!target.uncompressed || target.uncompressed > kMaxPng ||
        target.compressed > kMaxCompressedPng)
        return Fail(error, "Embedded preview exceeds the configured memory limit.");
    if (target.method == 0 && target.compressed != target.uncompressed)
        return Fail(error, "Stored ZIP entry has inconsistent lengths.");
    return true;
}

bool ExtractEntry(const Reader& reader, const Entry& entry,
                  std::vector<std::uint8_t>& png, std::string& error) {
    std::array<std::uint8_t, 30> local{};
    if (!reader.Read(entry.localOffset, local.data(), local.size()) ||
        U32(local.data()) != 0x04034b50 || U16(local.data() + 8) != entry.method ||
        (U16(local.data() + 6) & 1u))
        return Fail(error, "Invalid ZIP local file record.");
    const auto localName = U16(local.data() + 26);
    const auto localExtra = U16(local.data() + 28);
    const std::uint64_t headerSize = 30u + localName + localExtra;
    if (!Fits(entry.localOffset, headerSize, reader.size))
        return Fail(error, "Truncated ZIP local header.");
    constexpr char kName[] = "mergedimage.png";
    std::array<char, sizeof(kName) - 1> name{};
    if (localName != name.size() ||
        !reader.Read(entry.localOffset + 30, name.data(), name.size()) ||
        std::memcmp(name.data(), kName, name.size()) != 0)
        return Fail(error, "ZIP local filename differs from the merged image entry.");
    const std::uint64_t dataOffset = entry.localOffset + headerSize;
    if (!Fits(dataOffset, entry.compressed, reader.size))
        return Fail(error, "Truncated ZIP image data.");

    std::array<std::uint8_t, kChunk> incoming{};
    std::array<std::uint8_t, kChunk> outgoing{};
    png.clear();
    png.reserve(static_cast<std::size_t>(entry.uncompressed));
    std::uint64_t position = dataOffset;
    std::uint64_t remaining = entry.compressed;
    if (entry.method == 0) {
        while (remaining) {
            const auto bytes = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, incoming.size()));
            if (!reader.Read(position, incoming.data(), bytes))
                return Fail(error, "Could not read stored PNG data.");
            png.insert(png.end(), incoming.data(), incoming.data() + bytes);
            remaining -= bytes;
            position += bytes;
        }
    } else {
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            return Fail(error, "Could not initialize ZIP DEFLATE.");
        bool success = false;
        for (;;) {
            if (stream.avail_in == 0 && remaining) {
                const auto bytes = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, incoming.size()));
                if (!reader.Read(position, incoming.data(), bytes)) break;
                position += bytes;
                remaining -= bytes;
                stream.next_in = incoming.data();
                stream.avail_in = static_cast<uInt>(bytes);
            }
            stream.next_out = outgoing.data();
            stream.avail_out = static_cast<uInt>(outgoing.size());
            const int status = inflate(&stream, Z_NO_FLUSH);
            const auto produced = outgoing.size() - stream.avail_out;
            if (produced > entry.uncompressed - png.size()) break;
            png.insert(png.end(), outgoing.data(), outgoing.data() + produced);
            if (status == Z_STREAM_END) {
                success = remaining == 0 && stream.avail_in == 0 &&
                          png.size() == entry.uncompressed;
                break;
            }
            if (status != Z_OK || (!remaining && stream.avail_in == 0 && produced == 0)) break;
        }
        inflateEnd(&stream);
        if (!success) return Fail(error, "Invalid or truncated ZIP compressed data.");
    }
    if (png.size() != entry.uncompressed)
        return Fail(error, "ZIP uncompressed size mismatch.");
    uLong checksum = crc32(0L, Z_NULL, 0);
    // Use bounded chunks: zlib's length parameter differs on Windows vs Linux.
    for (std::size_t at = 0; at < png.size();) {
        const auto bytes = std::min(png.size() - at, kChunk);
        checksum = crc32(checksum, png.data() + at, static_cast<uInt>(bytes));
        at += bytes;
    }
    if (checksum != entry.crc)
        return Fail(error, "ZIP embedded image CRC does not match.");
    constexpr std::array<std::uint8_t, 8> pngSignature = {137,80,78,71,13,10,26,10};
    if (png.size() < 24 || !std::equal(pngSignature.begin(), pngSignature.end(), png.begin()))
        return Fail(error, "Merged-image entry is not a PNG.");
    return true;
}
} // namespace

bool ExtractKraPreviewFromReader(std::uint64_t size, KraReadAtCallback readAt,
                                 void* context, std::vector<std::uint8_t>& png,
                                 std::string& error) {
    png.clear();
    error.clear();
    if (!readAt || size < 22) return Fail(error, "Invalid or empty Krita archive.");
    const Reader reader{size, readAt, context};
    Entry entry{};
    return LocateMergedImage(reader, entry, error) && ExtractEntry(reader, entry, png, error);
}

bool ExtractKraPreview(const std::filesystem::path& path,
                       std::vector<std::uint8_t>& png, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return Fail(error, "Could not find or open the .kra file.");
    std::ifstream file(path, std::ios::binary);
    if (!file) return Fail(error, "Could not open the .kra file for reading.");
    const auto read = [](void* context, std::uint64_t position,
                         void* output, std::size_t count) -> bool {
        auto& input = *static_cast<std::ifstream*>(context);
        if (position > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
            count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) return false;
        input.clear();
        input.seekg(static_cast<std::streamoff>(position), std::ios::beg);
        if (!input) return false;
        input.read(static_cast<char*>(output), static_cast<std::streamsize>(count));
        return input.good() || input.gcount() == static_cast<std::streamsize>(count);
    };
    return ExtractKraPreviewFromReader(size, read, &file, png, error);
}
} // namespace sagepreview
