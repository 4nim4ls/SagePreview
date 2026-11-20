#include "navigation.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>
#include <system_error>

namespace sagepreview {
namespace {

bool AsciiDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

// Natural, case-insensitive filename order: artwork2 < artwork10.
// Does not require an Explorer process, locale, or external library.
int CompareNatural(std::wstring_view a, std::wstring_view b) {
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (AsciiDigit(a[i]) && AsciiDigit(b[j])) {
            const std::size_t aStart = i, bStart = j;
            while (i < a.size() && AsciiDigit(a[i])) ++i;
            while (j < b.size() && AsciiDigit(b[j])) ++j;
            const std::size_t aEnd = i, bEnd = j;
            std::size_t aSignificant = aStart, bSignificant = bStart;
            while (aSignificant < aEnd && a[aSignificant] == L'0') ++aSignificant;
            while (bSignificant < bEnd && b[bSignificant] == L'0') ++bSignificant;
            const auto aLength = aEnd - aSignificant;
            const auto bLength = bEnd - bSignificant;
            if (aLength != bLength) return aLength < bLength ? -1 : 1;
            for (std::size_t k = 0; k < aLength; ++k) {
                if (a[aSignificant + k] != b[bSignificant + k])
                    return a[aSignificant + k] < b[bSignificant + k] ? -1 : 1;
            }
            // Keep the shorter spelling of numerically equal runs first.
            if (aEnd - aStart != bEnd - bStart)
                return (aEnd - aStart) < (bEnd - bStart) ? -1 : 1;
            continue;
        }
        const wchar_t ca = std::towlower(a[i]);
        const wchar_t cb = std::towlower(b[j]);
        if (ca != cb) return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    if (i == a.size() && j == b.size()) return 0;
    return i == a.size() ? -1 : 1;
}

bool EqualCaseInsensitive(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    }
    return true;
}

bool FilenameLess(const std::filesystem::path& a, const std::filesystem::path& b) {
    const auto left = a.filename().wstring();
    const auto right = b.filename().wstring();
    const int result = CompareNatural(left, right);
    if (result != 0) return result < 0;
    return left < right;
}

} // namespace

bool IsPreviewableImagePath(const std::filesystem::path& path) {
    const auto extension = path.extension().wstring();
    constexpr std::wstring_view supported[] = {
        L".clip", L".kra", L".psd", L".png", L".jpg", L".jpeg", L".bmp", L".gif",
        L".tif", L".tiff", L".webp", L".avif"
    };
    for (const auto candidate : supported) {
        if (EqualCaseInsensitive(extension, candidate)) return true;
    }
    return false;
}

bool SiblingImages::Load(const std::filesystem::path& currentFile) {
    files_.clear();
    current_.reset();
    std::error_code error;
    const auto folder = currentFile.parent_path();
    if (folder.empty()) return false;
    std::filesystem::directory_iterator iterator(folder,
        std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::directory_iterator end;
    if (error) return false;
    for (; iterator != end; iterator.increment(error)) {
        if (error) break;
        const auto& entry = *iterator;
        if (!IsPreviewableImagePath(entry.path())) continue;
        if (entry.is_regular_file(error) && !error)
            files_.push_back(entry.path());
        if (error) error.clear(); // A single inaccessible entry must not stop navigation.
    }
    std::sort(files_.begin(), files_.end(), FilenameLess);
    return SetCurrent(currentFile);
}

bool SiblingImages::SetCurrent(const std::filesystem::path& file) {
    current_.reset();
    // Paths from directory_iterator share a parent with the opening file,
    // so filename comparison is sufficient (including Windows case folding).
    for (std::size_t index = 0; index < files_.size(); ++index) {
        if (EqualCaseInsensitive(files_[index].filename().wstring(),
                                 file.filename().wstring())) {
            current_ = index;
            return true;
        }
    }
    return false;
}

std::optional<std::filesystem::path> SiblingImages::Next(int direction) const {
    if (!current_ || direction == 0) return std::nullopt;
    if (direction < 0) {
        if (*current_ == 0) return std::nullopt;
        return files_[*current_ - 1];
    }
    if (*current_ + 1 >= files_.size()) return std::nullopt;
    return files_[*current_ + 1];
}

} // namespace sagepreview
