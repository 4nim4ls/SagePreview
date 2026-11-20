#pragma once

// The geometry is platform-independent so it can be tested without Explorer,
// a graphics device, or even Windows. All coordinates/sizes are physical pixels.
namespace sagepreview {

struct PreviewRect {
    int left;
    int top;
    int width;
    int height;
};

// Keep panning within the image edges. If the image fits, disable panning.
double ClampPreviewPan(double requested, double imageSpan, int viewportSpan);
// Snap a rendered dimension to the viewport when only rounding differs.
double SnapPreviewSpan(double imageSpan, int viewportSpan);

// Images fit on screen without being enlarged automatically.
double InitialPreviewScale(unsigned imageWidth, unsigned imageHeight,
                           int maxWidth, int maxHeight);

// The zoomed viewer grows up to the available work-area size. It remains
// centered; the renderer handles panning when the image exceeds the viewport.
PreviewRect CenteredPreviewRect(int workLeft, int workTop,
                                int workWidth, int workHeight,
                                int maxWidth, int maxHeight,
                                unsigned imageWidth, unsigned imageHeight,
                                double baseScale, double zoom);

} // namespace sagepreview
