#include "../src/core/island_core.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace lynx;

int main() {
    auto floating = target(Placement::Floating, View::Ambient);
    auto attached = target(Placement::Attached, View::Ambient);
    assert(floating.width == 232.0f && floating.height == 42.0f);
    assert(floating.shoulder == 0.0f && floating.topOffset == 13.0f);
    assert(attached.width == 264.0f && attached.height == 42.0f);
    assert(attached.shoulder == 16.0f && attached.topOffset == 0.0f);
    for (auto p : {Placement::Floating, Placement::Attached}) {
        auto expanded = target(p, View::Expanded);
        assert(expanded.width > target(p, View::Ambient).width);
        assert(expanded.height > target(p, View::Ambient).height);
        assert(target(p, View::Hidden).width == 0.0f);
    }

    Spring spring(42.0f);
    spring.retarget(190.0f);
    for (int i = 0; i < 24; ++i) spring.step(1.0f/60);
    const float beforeRetarget = spring.value();
    assert(beforeRetarget > 42.0f && std::isfinite(beforeRetarget));
    spring.retarget(42.0f);
    assert(std::abs(spring.value() - beforeRetarget) < 0.01f); // interrupt, no snap
    for (int i = 0; i < 240; ++i) spring.step(1.0f/60);
    assert(!spring.moving() && std::abs(spring.value() - 42.0f) < .01f);

    Presentation sm;
    assert(sm.view() == View::Ambient);
    sm.compact();
    sm.transient();
    assert(sm.view() == View::Transient);
    sm.expireTransient();
    assert(sm.view() == View::Compact);
    sm.expand();
    sm.transient(); // passive event cannot hijack expanded controls
    assert(sm.view() == View::Expanded);
    sm.setFullscreen(true);
    assert(sm.view() == View::Hidden);
    sm.setFullscreen(false);
    assert(sm.view() == View::Expanded);
    sm.collapse();
    assert(sm.view() == View::Compact);
    sm.ambient();
    assert(sm.view() == View::Ambient);
    std::cout << "lynx core: geometry, spring and state transitions PASS\n";
}
