#include "../src/core/activity_coordinator.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
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

    ActivityCoordinator coordinator;
    assert(coordinator.view() == View::Ambient);

    Activity music{1,1,1,10,L"Now playing",L"Local music",true};
    Activity timer{2,1,1,20,L"25:00",L"Focus timer",true};
    assert(coordinator.upsert(music));
    assert(coordinator.view() == View::Compact);
    assert(coordinator.primary()->id == 1);

    // Two concurrent activities: deterministic priority and secondary.
    assert(coordinator.upsert(timer));
    assert(coordinator.primary()->id == 2);
    assert(coordinator.secondary()->id == 1);

    coordinator.expand();
    assert(coordinator.view() == View::Expanded);
    assert(coordinator.primary()->id == 2);
    // While interacting, a higher priority background update may arrive but
    // must not steal the selected expanded panel.
    assert(coordinator.upsert(Activity{3,1,1,100,L"Background",L"",true}));
    assert(coordinator.primary()->id == 2);
    assert(coordinator.transient(L"Power connected") == 0);
    assert(coordinator.view() == View::Expanded);

    // End selected activity. A late update from the ended generation cannot
    // resurrect it; the presentation must reconcile.
    assert(coordinator.end(2,1,2));
    assert(!coordinator.upsert(Activity{2,1,1,200,L"Stale",L"",true}));
    assert(!coordinator.upsert(Activity{2,1,200,200,L"Post-completion",L"",true}));
    assert(coordinator.primary()->id == 3);
    assert(coordinator.view() == View::Expanded);
    coordinator.collapse();
    assert(coordinator.view() == View::Compact);
    assert(coordinator.primary()->id == 3);

    // A new generation of the same source can legitimately restart.
    assert(coordinator.upsert(Activity{2,2,1,150,L"New focus",L"",true}));
    assert(coordinator.primary()->id == 2);
    assert(!coordinator.end(2,1,99)); // older generation
    assert(coordinator.primary()->id == 2);

    const auto charging = coordinator.transient(L"Power connected");
    assert(charging > 0);
    assert(coordinator.view() == View::Transient);
    const auto completed = coordinator.transient(L"Timer completed");
    assert(completed > charging);
    assert(!coordinator.expireTransient(charging)); // stale timer ignored
    assert(coordinator.view() == View::Transient);
    assert(coordinator.transientText() == L"Timer completed");
    assert(coordinator.expireTransient(completed));
    assert(coordinator.view() == View::Compact);

    coordinator.expand();
    coordinator.setFullscreen(true);
    assert(coordinator.view() == View::Hidden);
    assert(coordinator.transient(L"Ignore while hidden") == 0);
    assert(coordinator.view() == View::Hidden);
    coordinator.setFullscreen(false);
    assert(coordinator.view() == View::Expanded);
    coordinator.collapse();

    assert(coordinator.end(1,1,2));
    assert(coordinator.end(2,2,2));
    assert(coordinator.end(3,1,2));
    assert(coordinator.view() == View::Ambient);
    assert(!coordinator.primary().has_value());
    assert(!coordinator.secondary().has_value());
    assert(!coordinator.upsert(Activity{3,1,1,90,L"Late",L"",true}));

    std::cout << "lynx core: geometry, spring, concurrency, and lifecycle PASS\n";
}
