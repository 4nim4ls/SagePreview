#include "preview_layout.h"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
    using namespace sagepreview;
    assert(InitialPreviewScale(400, 300, 1800, 900) == 1.0);
    auto small = CenteredPreviewRect(0, 0, 1920, 1080, 1800, 960,
                                     400, 300, 1.0, 1.0);
    assert(small.left == 760 && small.top == 390 && small.width == 400 && small.height == 300);
    const double largeScale = InitialPreviewScale(4000, 3000, 1800, 900);
    assert(std::abs(largeScale - 0.3) < 1e-9);
    auto large = CenteredPreviewRect(0, 0, 1920, 1080, 1800, 900,
                                     4000, 3000, largeScale, 1.0);
    assert(large.width == 1200 && large.height == 900 && large.left == 360 && large.top == 90);
    auto zoomed = CenteredPreviewRect(0, 0, 1920, 1080, 1800, 900,
                                      4000, 3000, largeScale, 2.0);
    assert(zoomed.width == 1800 && zoomed.height == 900);
    auto secondary = CenteredPreviewRect(-1920, -200, 1920, 1080, 1800, 900,
                                         400, 300, 1.0, 1.0);
    assert(secondary.left == -1160 && secondary.top == 190);
    auto tiny = CenteredPreviewRect(0, 0, 1920, 1080, 1800, 900,
                                    1, 1, 1.0, 0.1);
    assert(tiny.width == 1 && tiny.height == 1);
    // No gray viewport exposed when dragging an image that already fits.
    assert(ClampPreviewPan(100.0, 600.0, 800) == 0.0);
    assert(ClampPreviewPan(-100.0, 800.0, 800) == 0.0);
    // Oversized images pan only as far as their edges.
    assert(ClampPreviewPan(800.0, 1600.0, 800) == 400.0);
    assert(ClampPreviewPan(-900.0, 1600.0, 800) == -400.0);
    assert(ClampPreviewPan(150.0, 1600.0, 800) == 150.0);
    assert(SnapPreviewSpan(799.6, 800) == 800.0);
    assert(SnapPreviewSpan(1200.0, 800) == 1200.0);
    std::cout << "Preview layout tests passed\n";
}
