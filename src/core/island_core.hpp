#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lynx {

enum class Placement : std::uint8_t { Floating, Attached };
enum class View : std::uint8_t { Ambient, Compact, Expanded, Transient, Hidden };

struct Geometry {
    float width = 232.0f;    // entire silhouette width, in DIPs
    float height = 42.0f;    // entire silhouette height, in DIPs
    float shoulder = 0.0f;   // concave shoulder extent, in DIPs
    float topOffset = 13.0f;
};

inline Geometry target(Placement placement, View view) {
    float bodyWidth = 232.0f;
    float height = 42.0f;
    switch (view) {
        case View::Ambient: break;
        case View::Compact: bodyWidth = 190.0f; break;
        case View::Expanded: bodyWidth = 336.0f; height = 190.0f; break;
        case View::Transient: bodyWidth = 244.0f; height = 54.0f; break;
        case View::Hidden: bodyWidth = 0.0f; height = 0.0f; break;
    }
    const float shoulder = placement == Placement::Attached && view != View::Hidden ? 16.0f : 0.0f;
    return {bodyWidth + 2.0f * shoulder, height, shoulder,
            placement == Placement::Attached ? 0.0f : 13.0f};
}

// Stable single-axis damped spring. Retargeting preserves current velocity.
// Run only while changing; stepping is capped for window drag / resume spikes.
class Spring {
public:
    explicit Spring(float initial = 0) : position_(initial), target_(initial) {}
    void retarget(float target) { target_ = target; }
    void snap(float target) { position_ = target_ = target; velocity_ = 0.0f; }
    void step(float elapsedSeconds, float stiffness = 440.0f, float damping = 40.0f) {
        float remaining = std::clamp(elapsedSeconds, 0.0f, 0.05f);
        while (remaining > 0.0f) {
            const float dt = std::min(remaining, 1.0f / 240.0f);
            const float acceleration = stiffness * (target_ - position_) - damping * velocity_;
            velocity_ += acceleration * dt;
            position_ += velocity_ * dt;
            remaining -= dt;
        }
        if (std::abs(target_ - position_) < 0.08f && std::abs(velocity_) < 0.3f) snap(target_);
    }
    bool moving() const {
        return std::abs(target_ - position_) > 0.001f || std::abs(velocity_) > 0.001f;
    }
    float value() const { return position_; }
    float destination() const { return target_; }
private:
    float position_ = 0.0f;
    float velocity_ = 0.0f;
    float target_ = 0.0f;
};

// M0-only presentation reducer. Real providers will feed its parent coordinator
// after M0; shell geometry is intentionally independent of provider data.
class Presentation {
public:
    View view() const { return view_; }
    View resumeView() const { return resume_; }
    bool fullscreen() const { return fullscreen_; }
    void expand() {
        if (fullscreen_) return;
        if (view_ != View::Expanded) {
            resume_ = (view_ == View::Transient) ? resume_ : view_;
            view_ = View::Expanded;
        }
    }
    void collapse() {
        if (view_ == View::Expanded) view_ = resume_ == View::Expanded ? View::Ambient : resume_;
    }
    void compact() {
        if (fullscreen_) return;
        view_ = View::Compact;
        resume_ = View::Compact;
    }
    void ambient() {
        if (fullscreen_) return;
        view_ = View::Ambient;
        resume_ = View::Ambient;
    }
    void transient() {
        if (fullscreen_) return;
        if (view_ == View::Expanded) return; // no focus-hijacking event takeover
        resume_ = view_;
        view_ = View::Transient;
    }
    void expireTransient() {
        if (view_ == View::Transient) view_ = resume_;
    }
    void setFullscreen(bool enabled) {
        if (fullscreen_ == enabled) return;
        fullscreen_ = enabled;
        if (enabled) { beforeFullscreen_ = view_; view_ = View::Hidden; }
        else view_ = beforeFullscreen_;
    }
private:
    View view_ = View::Ambient;
    View resume_ = View::Ambient;
    View beforeFullscreen_ = View::Ambient;
    bool fullscreen_ = false;
};
} // namespace lynx
