#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace sagepreview {

// The same conservative extension set accepted by the v0.3 listener.
// Membership means "worth trying to decode", not that the necessary
// Windows Imaging Component codec is installed.
bool IsPreviewableImagePath(const std::filesystem::path& path);

// Snapshot of directly adjacent images in the same filesystem folder.
// This deliberately does not enumerate recursive folders or change Explorer.
class SiblingImages {
public:
    bool Load(const std::filesystem::path& currentFile);
    std::optional<std::filesystem::path> Next(int direction) const;
    bool SetCurrent(const std::filesystem::path& file);
    std::size_t Count() const { return files_.size(); }

private:
    std::vector<std::filesystem::path> files_;
    std::optional<std::size_t> current_;
};

} // namespace sagepreview
