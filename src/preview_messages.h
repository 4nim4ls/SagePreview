#pragma once
#include <windows.h>

// Messages sent by our own listener to our own viewer when Windows retains
// keyboard focus in Explorer after CreateProcess. No Explorer code injection.
namespace sagepreview {
inline constexpr UINT kListenerNavigate = WM_APP + 40; // wParam: VK_LEFT or VK_RIGHT
inline constexpr UINT kListenerClose = WM_APP + 41;
} // namespace sagepreview
