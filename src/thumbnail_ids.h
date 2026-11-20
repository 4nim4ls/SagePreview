#pragma once
#include <guiddef.h>

namespace sagepreview {
// Project-specific COM class ID. This is NOT the system thumbnail category GUID.
inline constexpr GUID kClsidThumbnailProvider = {
    0x768559df, 0xba6b, 0x4f4b, {0xbc, 0xc0, 0xba, 0xbd, 0xda, 0x49, 0x11, 0xb8}
};
} // namespace sagepreview
