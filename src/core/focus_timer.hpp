#pragma once
#include "activity_coordinator.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
namespace lynx {
// No wall-clock dependence or drift: caller injects monotonic milliseconds.
// Semantics: time elapsed while asleep counts toward the session.
class FocusTimer {
public:
    enum class State { Ready, Running, Paused, Completed };
    State state() const { return state_; }
    uint64_t generation() const { return generation_; }
    int durationSeconds() const { return duration_; }
    int remainingSeconds(std::uint64_t now) const {
        const auto remaining = remainingMs(now);
        return static_cast<int>((remaining + 999) / 1000);
    }
    void start(int seconds, std::uint64_t now) {
        duration_ = std::clamp(seconds,1,24*3600);
        ++generation_;
        revision_ = 0;
        frozenMs_ = static_cast<std::uint64_t>(duration_)*1000;
        deadline_ = now + frozenMs_;
        state_=State::Running;
    }
    bool pause(std::uint64_t now) {
        if(state_!=State::Running)return false;
        frozenMs_=remainingMs(now);
        state_=frozenMs_?State::Paused:State::Completed;
        return true;
    }
    bool resume(std::uint64_t now) {
        if(state_!=State::Paused || !frozenMs_)return false;
        deadline_=now+frozenMs_;
        state_=State::Running;
        return true;
    }
    void reset() { state_=State::Ready; frozenMs_=0; ++generation_; ++revision_; }
    // Returns true exactly once at completion.
    bool advance(std::uint64_t now) {
        if(state_!=State::Running || now<deadline_)return false;
        frozenMs_=0;
        state_=State::Completed;
        return true;
    }
    bool publish(ActivityCoordinator& coordinator,std::uint64_t now) {
        if(!generation_)return false;
        if(state_==State::Ready || state_==State::Completed)
            return coordinator.end(kId,generation_,++revision_);
        std::wstring title=clock(remainingSeconds(now));
        const wchar_t* subtitle=state_==State::Running?L"Focus · Running":L"Focus · Paused";
        return coordinator.upsert(Activity{kId,generation_,++revision_,40,
                std::move(title),subtitle,true});
    }
    static std::wstring clock(int seconds) {
        seconds=std::max(0,seconds);
        auto minutes=seconds/60,secs=seconds%60;
        return std::to_wstring(minutes)+L":"+(secs<10?L"0":L"")+std::to_wstring(secs);
    }
    static constexpr std::uint32_t kId=200;
private:
    std::uint64_t remainingMs(std::uint64_t now) const {
        if(state_==State::Running)return now>=deadline_?0:deadline_-now;
        return frozenMs_;
    }
    State state_=State::Ready;
    std::uint64_t generation_=0,revision_=0,frozenMs_=0,deadline_=0;
    int duration_=25*60;
};
} // namespace lynx
