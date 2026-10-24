#pragma once
#include "island_core.hpp"
#include <algorithm>
#include <cstdint>
namespace lynx {

// A monotonic, interruptible morph contract shared by renderer and hit region.
// Each retarget begins from the interpolated geometry, never a stale endpoint.
// A desktop timer need only reconcile input; the compositor can draw the path.
class GeometryMorph {
public:
    void snap(Geometry geometry) {
        from_ = to_ = geometry;
        active_ = false;
    }
    Geometry sample(std::uint64_t now) const {
        if(!active_)return to_;
        const double elapsed=now>started_ ? static_cast<double>(now-started_):0.;
        const float t=static_cast<float>(std::clamp(
            elapsed/static_cast<double>(durationMs_),0.,1.));
        return {
            from_.width+(to_.width-from_.width)*t,
            from_.height+(to_.height-from_.height)*t,
            from_.shoulder+(to_.shoulder-from_.shoulder)*t,
            from_.topOffset+(to_.topOffset-from_.topOffset)*t
        };
    }
    Geometry retarget(Geometry geometry,std::uint64_t now,int durationMs=350) {
        const auto source=sample(now);
        from_=source;
        to_=geometry;
        started_=now;
        durationMs_=std::max(durationMs,1);
        active_=true;
        return source;
    }
    bool finished(std::uint64_t now) const {
        return !active_ || (now>=started_ &&
            now-started_>=static_cast<std::uint64_t>(durationMs_));
    }
    void settle() {from_=to_;active_=false;}
    bool active() const {return active_;}
    Geometry destination() const {return to_;}
private:
    Geometry from_{},to_{};
    std::uint64_t started_=0;
    int durationMs_=350;
    bool active_=false;
};
} // namespace lynx
