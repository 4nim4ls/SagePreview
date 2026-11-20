#include "preview_layout.h"

#include <algorithm>
#include <cmath>

namespace sagepreview {

double SnapPreviewSpan(double imageSpan, int viewportSpan) {
    if (!std::isfinite(imageSpan) || imageSpan <= 0.0 || viewportSpan <= 0) return imageSpan;
    return std::abs(imageSpan - viewportSpan) <= 0.5
        ? static_cast<double>(viewportSpan) : imageSpan;
}

double ClampPreviewPan(double requested, double imageSpan, int viewportSpan) {
    if (!std::isfinite(requested) || !std::isfinite(imageSpan) ||
        viewportSpan <= 0 || imageSpan <= viewportSpan) return 0.0;
    const double range = (imageSpan - viewportSpan) * 0.5;
    return std::clamp(requested, -range, range);
}


double InitialPreviewScale(unsigned imageWidth, unsigned imageHeight,
                           int maxWidth, int maxHeight) {
    if (!imageWidth || !imageHeight) return 1.0;
    return std::min({1.0,
        static_cast<double>(std::max(1, maxWidth)) / imageWidth,
        static_cast<double>(std::max(1, maxHeight)) / imageHeight});
}

PreviewRect CenteredPreviewRect(int workLeft, int workTop,
                                int workWidth, int workHeight,
                                int maxWidth, int maxHeight,
                                unsigned imageWidth, unsigned imageHeight,
                                double baseScale, double zoom) {
    const int boundedWidth = std::clamp(maxWidth, 1, std::max(1, workWidth));
    const int boundedHeight = std::clamp(maxHeight, 1, std::max(1, workHeight));
    const auto pixelSize = [](unsigned pixels, double scale, int limit) {
        if (pixels == 0 || !std::isfinite(scale) || scale <= 0.0) return 1;
        return std::clamp(static_cast<int>(std::lround(std::min(
            static_cast<double>(limit), static_cast<double>(pixels) * scale))), 1, limit);
    };
    const double scale = baseScale * zoom;
    const int width = pixelSize(imageWidth, scale, boundedWidth);
    const int height = pixelSize(imageHeight, scale, boundedHeight);
    return {workLeft + (workWidth - width) / 2,
            workTop + (workHeight - height) / 2,
            width, height};
}

} // namespace sagepreview
