#include "navigation.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main() {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto folder = fs::temp_directory_path() /
        ("sagepreview-navigation-test-" + std::to_string(timestamp));
    std::error_code ec;
    if (!fs::create_directories(folder, ec) || ec) return 2;
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); }
    } cleanup{folder};

    for (const auto* name : {"art10.clip", "art2.PNG", "art1.jpg", "notes.txt", "image.avif", "art3.kra", "art4.psd"}) {
        std::ofstream(folder / name) << "fixture";
    }
    fs::create_directory(folder / "not-a-file.clip", ec);
    fs::create_directory(folder / "subfolder", ec);
    std::ofstream(folder / "subfolder" / "art3.jpg") << "fixture";

    sagepreview::SiblingImages navigator;
    if (!navigator.Load(folder / "art2.PNG") || navigator.Count() != 6) {
        std::cerr << "Folder enumeration or extension filtering failed\n";
        return 1;
    }
    const auto previous = navigator.Next(-1);
    const auto next = navigator.Next(1);
    if (!previous || previous->filename() != "art1.jpg" ||
        !next || next->filename() != "art3.kra") {
        std::cerr << "Natural filename order failed\n";
        return 1;
    }
    if (!navigator.SetCurrent(folder / "ART10.CLIP") ||
        !navigator.Next(1) || navigator.Next(1)->filename() != "image.avif") {
        std::cerr << "Case-insensitive selection or end-of-list boundary failed\n";
        return 1;
    }
    if (!navigator.SetCurrent(folder / "image.avif") || navigator.Next(1) ||
        !navigator.SetCurrent(folder / "art1.jpg") || navigator.Next(-1)) {
        std::cerr << "Start-of-list boundary failed\n";
        return 1;
    }
    if (navigator.SetCurrent(folder / "notes.txt") || navigator.Next(1)) {
        std::cerr << "Non-image should never be selected\n";
        return 1;
    }
    if (sagepreview::IsPreviewableImagePath(folder / "notes.txt") ||
        !sagepreview::IsPreviewableImagePath(folder / "PAINT.CLIP") ||
        !sagepreview::IsPreviewableImagePath(folder / "PAINT.KRA") ||
        !sagepreview::IsPreviewableImagePath(folder / "PAINT.PSD") ||
        sagepreview::IsPreviewableImagePath(folder / "ART.HEIF")) {
        std::cerr << "Extension matching failed\n";
        return 1;
    }
    std::cout << "Sibling filtering, natural order, boundaries and casing passed.\n";
    return 0;
}
