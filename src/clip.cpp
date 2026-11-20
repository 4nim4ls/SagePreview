// Clip Studio Paint container reader and preview extractor.
// Independently implemented using the documented CSFCHUNK / CHNKSQLi layout
// and SQLite's standard API. No Rust source code is incorporated.

#include "clip.h"

#ifdef _WIN32
#include <windows.h>
#include <winsqlite/winsqlite3.h> // Bundled with the Windows SDK
#else
#include <sqlite3.h>           // Allows portable extractor tests on Linux
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <string_view>

namespace sagepreview {
namespace {

// Deliberately conservative initial limits. Real-world artwork may require
// increasing these after we measure the size of metadata in actual .clip files.
constexpr std::uint64_t kMaximumMetadataSize = 128ull * 1024 * 1024;
constexpr int kMaximumPreviewSize = 32 * 1024 * 1024;
constexpr unsigned kMaximumChunksToInspect = 65'536;
constexpr int kMaximumPreviewRows = 32;
constexpr std::array<std::uint8_t, 8> kPngSignature = {
    0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a
};

std::uint64_t BigEndian64(const std::uint8_t* p) {
    std::uint64_t result = 0;
    for (int i = 0; i < 8; ++i) result = (result << 8) | p[i];
    return result;
}

struct Reader {
    ClipReadAtCallback readAt = nullptr;
    void* context = nullptr;
    std::uint64_t size = 0;
};

bool ReadAt(Reader& reader, std::uint64_t position,
            void* destination, std::size_t count) {
    if (!reader.readAt || position > reader.size ||
        count > reader.size - position) return false;
    return reader.readAt(reader.context, position, destination, count);
}

bool ReadFileAt(void* context, std::uint64_t position,
                void* destination, std::size_t count) {
    auto& file = *static_cast<std::ifstream*>(context);
    if (position > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        count > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        return false;
    }
    file.clear();
    file.seekg(static_cast<std::streamoff>(position), std::ios::beg);
    if (!file) return false;
    return static_cast<bool>(file.read(static_cast<char*>(destination),
                                       static_cast<std::streamsize>(count)));
}

struct Chunk {
    std::array<char, 8> name{};
    std::uint64_t dataPosition = 0;
    std::uint64_t dataLength = 0;
};

bool ReadChunk(Reader& file, std::uint64_t fileSize,
               std::uint64_t position, Chunk& chunk) {
    // Verify the declared chunk fits within the real file BEFORE seeking or
    // adding offsets; never trust chunk sizes from a user-supplied document.
    if (position > fileSize || fileSize - position < 16) return false;
    std::array<std::uint8_t, 16> header{};
    if (!ReadAt(file, position, header.data(), header.size())) return false;
    std::copy_n(reinterpret_cast<const char*>(header.data()), 8, chunk.name.data());
    chunk.dataPosition = position + 16;
    chunk.dataLength = BigEndian64(header.data() + 8);
    return chunk.dataLength <= fileSize - chunk.dataPosition;
}

bool HasName(const Chunk& chunk, std::string_view name) {
    return name.size() == chunk.name.size() &&
           std::equal(name.begin(), name.end(), chunk.name.begin());
}

bool FindDatabase(Reader& file, std::uint64_t fileSize,
                  Chunk& database, std::string& error) {
    std::array<std::uint8_t, 24> header{};
    if (!ReadAt(file, 0, header.data(), header.size()) ||
        std::memcmp(header.data(), "CSFCHUNK", 8) != 0) {
        error = "Not a recognized Clip Studio .clip container (missing CSFCHUNK header).";
        return false;
    }
    const std::uint64_t firstChunkOffset = BigEndian64(header.data() + 16);
    if (firstChunkOffset < header.size()) {
        error = "Invalid CLIP header: first chunk offset.";
        return false;
    }

    Chunk first{};
    if (ReadChunk(file, fileSize, firstChunkOffset, first) &&
        HasName(first, "CHNKHead") && first.dataLength >= 16) {
        std::array<std::uint8_t, 16> headData{};
        if (ReadAt(file, first.dataPosition, headData.data(), headData.size())) {
            const std::uint64_t databaseChunkOffset = BigEndian64(headData.data() + 8);
            // The fast pointer is only a hint; always validate its target.
            if (ReadChunk(file, fileSize, databaseChunkOffset, database) &&
                HasName(database, "CHNKSQLi")) return true;
        }
    }

    // Some files lack a usable Head pointer. Skip directly over chunk payloads;
    // never scan/read the large raster-data sections themselves.
    std::uint64_t position = firstChunkOffset;
    for (unsigned i = 0; i < kMaximumChunksToInspect; ++i) {
        Chunk current{};
        if (!ReadChunk(file, fileSize, position, current)) break;
        if (std::memcmp(current.name.data(), "CHNK", 4) != 0) break;
        if (HasName(current, "CHNKSQLi")) {
            database = current;
            return true;
        }
        position = current.dataPosition + current.dataLength; // already bounds checked
    }
    error = "No valid CHNKSQLi metadata database was found in this .clip file.";
    return false;
}

// SQLite frees a SQLITE_DESERIALIZE_FREEONCLOSE buffer when the connection
// closes. Its own allocator must be used for that buffer.
struct DatabaseCloser {
    void operator()(sqlite3* db) const { if (db) sqlite3_close(db); }
};
using DatabasePtr = std::unique_ptr<sqlite3, DatabaseCloser>;
struct StatementCloser {
    void operator()(sqlite3_stmt* statement) const {
        if (statement) sqlite3_finalize(statement);
    }
};
using StatementPtr = std::unique_ptr<sqlite3_stmt, StatementCloser>;

using DeserializeFunction = int (*)(sqlite3*, const char*, unsigned char*,
                                    sqlite3_int64, sqlite3_int64, unsigned);

DeserializeFunction GetDeserializeFunction() {
#ifdef _WIN32
    // Resolve separately because the API isn't declared by every historical
    // Windows SDK header, even where the Windows 11 DLL exports it.
    HMODULE module = GetModuleHandleW(L"winsqlite3.dll");
    if (!module) return nullptr;
    return reinterpret_cast<DeserializeFunction>(
        GetProcAddress(module, "sqlite3_deserialize"));
#else
    return &sqlite3_deserialize;
#endif
}

bool ReadPreviewFromDatabase(const std::vector<std::uint8_t>& metadata,
                             std::vector<std::uint8_t>& png,
                             std::string& error) {
    constexpr char kSqliteMagic[] = "SQLite format 3";
    if (metadata.size() < 100 ||
        std::memcmp(metadata.data(), kSqliteMagic, sizeof(kSqliteMagic)) != 0) {
        error = "The embedded CLIP metadata is not a valid SQLite database.";
        return false;
    }

    DeserializeFunction deserialize = GetDeserializeFunction();
    if (!deserialize) {
        error = "This Windows SQLite library does not expose sqlite3_deserialize.";
        return false;
    }

    sqlite3* rawDb = nullptr;
    if (sqlite3_open(":memory:", &rawDb) != SQLITE_OK) {
        if (rawDb) sqlite3_close(rawDb);
        error = "Windows SQLite could not create an in-memory database.";
        return false;
    }
    DatabasePtr db(rawDb);

    // 20 extra bytes accommodate SQLite's documented malformed-input overread
    // allowance. Never write a temporary database or modify the .clip file.
    const auto allocationSize = static_cast<int>(metadata.size()) + 20;
    auto* bytes = static_cast<unsigned char*>(sqlite3_malloc(allocationSize));
    if (!bytes) {
        error = "Not enough memory for the CLIP metadata database.";
        return false;
    }
    std::memcpy(bytes, metadata.data(), metadata.size());
    std::memset(bytes + metadata.size(), 0, 20);
    // SQLite cannot deserialize a WAL-mode image, so force read-only rollback
    // semantics in our PRIVATE memory copy. The source document is untouched.
    if (bytes[18] == 2 || bytes[19] == 2) {
        bytes[18] = 1;
        bytes[19] = 1;
    }

    constexpr unsigned kFreeOnClose = 1;
    constexpr unsigned kReadOnly = 4;
    const int status = deserialize(db.get(), "main", bytes,
                                   static_cast<sqlite3_int64>(metadata.size()),
                                   static_cast<sqlite3_int64>(metadata.size() + 20),
                                   kFreeOnClose | kReadOnly);
    // SQLite owns bytes even when deserialize fails (FREEONCLOSE contract).
    if (status != SQLITE_OK) {
        error = "Windows SQLite could not read the embedded CLIP metadata.";
        return false;
    }

    // Disallow unexpectedly large SQL values/outputs in this preview-only path.
    sqlite3_limit(db.get(), SQLITE_LIMIT_LENGTH, kMaximumPreviewSize + 4096);

    // Selecting only ImageData from the known CanvasPreview table avoids
    // accidentally showing a layer thumbnail or a different embedded PNG.
    sqlite3_stmt* rawStatement = nullptr;
    const int prepared = sqlite3_prepare_v2(db.get(),
        "SELECT ImageData FROM CanvasPreview", -1, &rawStatement, nullptr);
    if (prepared != SQLITE_OK) {
        error = "The CLIP metadata does not have a readable CanvasPreview.ImageData field.";
        return false;
    }
    StatementPtr statement(rawStatement);

    for (int row = 0; row < kMaximumPreviewRows; ++row) {
        const int next = sqlite3_step(statement.get());
        if (next == SQLITE_DONE) break;
        if (next != SQLITE_ROW) {
            error = "SQLite failed while reading the CLIP preview row.";
            return false;
        }
        if (sqlite3_column_type(statement.get(), 0) != SQLITE_BLOB) continue;
        const int size = sqlite3_column_bytes(statement.get(), 0);
        if (size < static_cast<int>(kPngSignature.size()) ||
            size > kMaximumPreviewSize ||
            static_cast<std::size_t>(size) <= png.size()) continue;
        const auto* data = static_cast<const std::uint8_t*>(
            sqlite3_column_blob(statement.get(), 0));
        if (!data || !std::equal(kPngSignature.begin(), kPngSignature.end(), data))
            continue;
        png.assign(data, data + size);
    }
    if (png.empty()) {
        error = "The .clip file has no recognized embedded PNG canvas preview.";
        return false;
    }
    return true;
}

} // namespace

bool ExtractClipPreviewFromReader(std::uint64_t fileSize,
                                  ClipReadAtCallback readAt, void* context,
                                  std::vector<std::uint8_t>& png,
                                  std::string& error) {
    png.clear();
    error.clear();
    if (!readAt || fileSize < 24) {
        error = "The .clip file is too short or unreadable.";
        return false;
    }
    Reader source{readAt, context, fileSize};
    Chunk database{};
    if (!FindDatabase(source, fileSize, database, error)) return false;
    if (database.dataLength < 100 || database.dataLength > kMaximumMetadataSize) {
        error = "The embedded CLIP metadata database exceeds this prototype's 128 MiB limit (or is invalid).";
        return false;
    }
    std::vector<std::uint8_t> metadata(
        static_cast<std::size_t>(database.dataLength));
    if (!ReadAt(source, database.dataPosition, metadata.data(), metadata.size())) {
        error = "Could not read the CLIP metadata database.";
        return false;
    }
    return ReadPreviewFromDatabase(metadata, png, error);
}

bool ExtractClipPreview(const std::filesystem::path& clipPath,
                        std::vector<std::uint8_t>& png,
                        std::string& error) {
    png.clear();
    error.clear();
    std::ifstream file(clipPath, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "Could not open the .clip file for reading.";
        return false;
    }
    const std::streamoff size = file.tellg();
    if (size < 24) {
        error = "The .clip file is too short or unreadable.";
        return false;
    }
    return ExtractClipPreviewFromReader(static_cast<std::uint64_t>(size),
                                        &ReadFileAt, &file, png, error);
}

} // namespace sagepreview
