#include "../src/core/media_activity.hpp"
#include "../src/core/focus_timer.hpp"
#include "../src/core/file_shelf.hpp"
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

    // Media snapshots are normalized in a portable boundary, not the renderer.
    ActivityCoordinator mediaCoordinator;
    MediaActivityBridge mediaBridge;
    assert(!mediaBridge.reconcile(mediaCoordinator,MediaObservation{}));
    assert(mediaCoordinator.view() == View::Ambient);
    assert(mediaBridge.reconcile(mediaCoordinator,MediaObservation{
        true,true,L"spotify",L"Track A",L"Artist"}));
    assert(mediaCoordinator.primary()->title==L"Track A");
    assert(mediaCoordinator.primary()->subtitle==L"Artist · Playing");
    assert(mediaBridge.reconcile(mediaCoordinator,MediaObservation{
        true,false,L"spotify",L"Track A",L"Artist"}));
    assert(mediaCoordinator.primary()->subtitle==L"Artist · Paused");
    const auto firstGeneration=mediaBridge.generation();
    assert(mediaBridge.reconcile(mediaCoordinator,MediaObservation{}));
    assert(mediaCoordinator.view()==View::Ambient);
    assert(mediaBridge.generation()>firstGeneration);
    assert(mediaBridge.reconcile(mediaCoordinator,MediaObservation{
        true,true,L"spotify",L"Track B",L"Artist"}));
    assert(mediaCoordinator.primary()->title==L"Track B");
    const auto secondGeneration=mediaBridge.generation();
    assert(mediaBridge.reconcile(mediaCoordinator,MediaObservation{
        true,true,L"browser",L"Podcast",L""}));
    assert(mediaBridge.generation()>secondGeneration);
    assert(mediaCoordinator.primary()->title==L"Podcast");

    // Fully deterministic timer: no sleeping test, clock supplied explicitly.
    ActivityCoordinator focusCoordinator;
    FocusTimer focus;
    assert(focus.state()==FocusTimer::State::Ready);
    focus.start(25*60,1000);
    assert(focus.remainingSeconds(1000)==1500);
    assert(focus.publish(focusCoordinator,1000));
    assert(focusCoordinator.primary()->id==FocusTimer::kId);
    assert(focus.remainingSeconds(1501)==1500);
    assert(focus.remainingSeconds(2000)==1499);
    assert(focus.pause(31'000));
    assert(focus.state()==FocusTimer::State::Paused);
    assert(focus.remainingSeconds(5'000'000)==1470);
    assert(focus.resume(7'000'000));
    assert(focus.remainingSeconds(7'000'000)==1470);
    assert(!focus.advance(8'469'999));
    assert(focus.advance(8'470'000));
    assert(!focus.advance(8'470'100)); // completion exactly once
    assert(focus.publish(focusCoordinator,8'470'000));
    assert(focusCoordinator.view()==View::Ambient);
    focus.start(60,20'000'000);
    assert(focus.publish(focusCoordinator,20'000'000));
    assert(focusCoordinator.primary()->title==L"1:00");
    focus.reset();
    assert(focus.publish(focusCoordinator,20'000'000));
    assert(focusCoordinator.view()==View::Ambient);
    // If a user clicks Pause just after the deadline, the timer reaches
    // Completed without waiting for a potentially delayed WM_TIMER.
    focus.start(5,30'000'000);
    assert(focus.publish(focusCoordinator,30'000'000));
    assert(focus.pause(30'005'000));
    assert(focus.state()==FocusTimer::State::Completed);
    assert(focus.publish(focusCoordinator,30'005'000));
    assert(focusCoordinator.view()==View::Ambient);

    // Shelf paths are references, not copies; duplicate and over-limit refused.
    FileShelf shelf;
    ActivityCoordinator shelfCoordinator;
    assert(shelf.add(L"/documents/report.pdf"));
    assert(!shelf.add(L"/documents/report.pdf"));
    assert(shelf.add(L"/documents/budget.xlsx"));
    assert(shelf.reconcile(shelfCoordinator));
    assert(shelfCoordinator.primary()->id==FileShelf::kId);
    assert(shelf.files().size()==2);
    assert(shelf.firstName()==L"report.pdf");
    assert(!shelf.remove(5));
    assert(shelf.remove(0));
    assert(shelf.reconcile(shelfCoordinator));
    assert(shelf.firstName()==L"budget.xlsx");
    shelf.clear();
    assert(shelf.reconcile(shelfCoordinator));
    assert(shelfCoordinator.view()==View::Ambient);
    assert(shelf.add(L"/new/again.txt"));
    assert(shelf.reconcile(shelfCoordinator));
    assert(shelfCoordinator.primary()->id==FileShelf::kId);
    shelf.clear(); shelf.reconcile(shelfCoordinator);
    for(int i=0;i<static_cast<int>(FileShelf::kLimit);++i)
        assert(shelf.add(L"/sample/"+std::to_wstring(i)));
    assert(!shelf.add(L"/sample/overflow"));

    // User-chosen shelf must stay selected even when timer/media has higher priority.
    ActivityCoordinator combined;
    FocusTimer selectedFocus;
    selectedFocus.start(300,3000);
    assert(selectedFocus.publish(combined,3000));
    FileShelf selectedShelf;
    assert(selectedShelf.add(L"/inbox/notes.txt"));
    assert(selectedShelf.reconcile(combined));
    assert(combined.primary()->id==FocusTimer::kId);
    assert(combined.select(FileShelf::kId));
    assert(combined.view()==View::Expanded);
    assert(combined.primary()->id==FileShelf::kId);
    assert(!combined.select(99999));
    assert(combined.primary()->id==FileShelf::kId);
    assert(combined.transient(L"Muted")==0);
    combined.collapse();
    assert(combined.primary()->id==FocusTimer::kId);

    std::cout << "lynx core: geometry, lifecycle, media, timer and shelf PASS\n";
}
