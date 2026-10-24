#pragma once
#include "island_core.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace lynx::v3 {

enum class Scene : std::uint8_t { Idle, Media, Timer, Shelf, Alert };
enum class IdleMode : std::uint8_t { Persistent, Adaptive };
enum class CenterMode : std::uint8_t { Lens, Dot, Day };
enum class Motion : std::uint8_t { Smooth, Snappy, Reduced };

struct Preferences {
    IdleMode idle = IdleMode::Persistent;
    CenterMode center = CenterMode::Lens;
    Motion motion = Motion::Smooth;
};

struct Box { float x=0,y=0,w=0,h=0; };
struct Layout {
    Geometry geometry{};
    Scene scene = Scene::Idle;
    bool handle = false;
    std::array<Box,3> actions{};
    std::uint8_t count = 0;
    Box secondary{};
};

constexpr float kTopOffset=13.f;
inline Geometry geometry(Placement placement, View view, Scene scene,
                         bool adaptiveHandle=false) {
    const float extra=placement==Placement::Attached?32.f:0.f;
    const float offset=placement==Placement::Attached?0.f:kTopOffset;
    if(view==View::Hidden) return {0,0,0,offset};
    if(view==View::Ambient && adaptiveHandle) {
        return { (placement==Placement::Attached?96.f:56.f),
                 (placement==Placement::Attached?12.f:9.f),
                 0.f,offset};
    }
    if(view==View::Ambient) return {232.f+extra,42.f,
                                   placement==Placement::Attached?16.f:0.f,offset};
    if(view==View::Compact) return {182.f+extra,38.f,
                                   placement==Placement::Attached?16.f:0.f,offset};
    if(view==View::Transient) return {244.f+extra,54.f,
                                     placement==Placement::Attached?16.f:0.f,offset};
    const float h = scene==Scene::Timer ?167.f:
                    scene==Scene::Idle ?154.f:
                    scene==Scene::Alert ?103.f:155.f;
    return {330.f+extra,h,placement==Placement::Attached?16.f:0.f,offset};
}
inline float radius(View view) {
    return view==View::Expanded?27.f:view==View::Compact?20.f:22.f;
}
inline float stiffness(Motion motion) {
    return motion==Motion::Snappy?830.f:440.f;
}
inline float damping(Motion motion) {
    return motion==Motion::Snappy?51.f:40.f;
}
// X/Y are in the *painted capsule's* DIP coordinate system, not screen space;
// the native host translates by its layered-window left/top and monitor DPI.
// Keep hit testing, keyboard selection, accessibility bounds and drawing
// dependent on these same rectangles.
inline Layout layout(Placement placement, View view, Scene scene,
                     bool adaptiveHandle=false) {
    Layout result{};
    result.geometry=geometry(placement,view,scene,adaptiveHandle);
    result.scene=scene;
    result.handle=adaptiveHandle && view==View::Ambient;
    const float w=result.geometry.width;
    const float shoulder=result.geometry.shoulder;
    if(view==View::Expanded) {
        if(scene==Scene::Media) {
            const float center=w*.5f;
            result.actions[0]={center-75.f,109.f,35.f,32.f};
            result.actions[1]={center-17.f,109.f,35.f,32.f};
            result.actions[2]={center+40.f,109.f,35.f,32.f};
            result.count=3;
        } else if(scene==Scene::Timer) {
            result.actions[0]={w*.5f-45.f,126.f,39.f,39.f};
            result.actions[1]={w*.5f+6.f,126.f,39.f,39.f};
            result.count=2;
        } else if(scene==Scene::Shelf) {
            result.actions[0]={shoulder+62.f,108.f,85.f,32.f};
            result.actions[1]={w-shoulder-147.f,108.f,85.f,32.f};
            result.count=2;
        } else if(scene==Scene::Idle) {
            result.actions[0]={w*.5f-43.f,114.f,86.f,29.f};
            result.count=1;
        } else if(scene==Scene::Alert) {
            result.actions[0]={w-shoulder-42.f,12.f,27.f,27.f};
            result.count=1;
        }
    }
    result.secondary={w+8.f,8.f,36.f,36.f};
    return result;
}
inline bool inside(Box b,float x,float y) {
    return x>=b.x && x<b.x+b.w && y>=b.y && y<b.y+b.h;
}
inline float focusProgress(int remainingSeconds,int durationSeconds) {
    if(durationSeconds<=0)return 0.f;
    return std::clamp(1.f-static_cast<float>(remainingSeconds)/durationSeconds,0.f,1.f);
}
inline float progressRatio(std::int64_t positionMs,std::int64_t durationMs) {
    if(durationMs<=0)return 0.f;
    return std::clamp(static_cast<float>(
        static_cast<double>(positionMs)/static_cast<double>(durationMs)),0.f,1.f);
}
} // namespace lynx::v3
