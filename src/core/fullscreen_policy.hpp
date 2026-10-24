#pragma once
#include <algorithm>
#include <cstdint>

namespace lynx {

// Decision only: the OS adapter is responsible for retrieving trustworthy
// monitor/window rectangles and filtering shell/desktop/cloaked windows.
struct ScreenRect {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool valid() const { return right > left && bottom > top; }
    bool intersects(const ScreenRect& other) const {
        return valid() && other.valid() &&
            std::max(left, other.left) < std::min(right, other.right) &&
            std::max(top, other.top) < std::min(bottom, other.bottom);
    }
};

inline bool coversMonitor(ScreenRect foreground, ScreenRect monitor,
                          int pixelTolerance = 2) {
    if (!foreground.valid() || !monitor.valid()) return false;
    const int t = std::clamp(pixelTolerance, 0, 8);
    // Fullscreen is coverage of the *physical monitor*, not work area:
    // a maximized ordinary window usually stops before the taskbar.
    return foreground.intersects(monitor) &&
        foreground.left <= monitor.left + t &&
        foreground.top <= monitor.top + t &&
        foreground.right >= monitor.right - t &&
        foreground.bottom >= monitor.bottom - t;
}
} // namespace lynx
