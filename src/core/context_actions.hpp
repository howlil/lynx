#pragma once
#include "activity_coordinator.hpp"
#include "focus_timer.hpp"
#include "file_shelf.hpp"
#include "media_activity.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace lynx {
enum class ShellAction : std::uint8_t {
    None, MediaPrevious, MediaToggle, MediaNext,
    FocusToggle, FocusReset, FileReveal, FileClear, StartFocus
};
struct ActionInfo {
    ShellAction action=ShellAction::None;
    std::wstring_view name{};
    bool enabled=true;
};
struct ContextActions {
    std::array<ActionInfo,3> items{};
    std::uint8_t count=0;
    const ActionInfo* at(int index) const {
        return index>=0 && index<count?&items[static_cast<std::size_t>(index)]:nullptr;
    }
};

// Identical action order in the v3 geometry, UIA, keyboard and pointer input.
// Availability is a native OS/provider capability, never a fake HTML control.
inline ContextActions contextActions(View view,std::uint32_t activityId,
            FocusTimer::State focusState,bool mediaCanToggle,bool mediaPlaying,
            bool filePresent,bool mediaCanPrevious=false,bool mediaCanNext=false) {
    ContextActions result;
    if(view!=View::Expanded)return result;
    switch(activityId) {
        case MediaActivityBridge::kId:
            result.items[0]={ShellAction::MediaPrevious,L"Previous track",mediaCanPrevious};
            result.items[1]={ShellAction::MediaToggle,
                mediaPlaying?L"Pause media":L"Play media",mediaCanToggle};
            result.items[2]={ShellAction::MediaNext,L"Next track",mediaCanNext};
            result.count=3;
            break;
        case FocusTimer::kId:
            if(focusState==FocusTimer::State::Running ||
               focusState==FocusTimer::State::Paused) {
                result.items[0]={ShellAction::FocusReset,L"Reset focus"};
                result.items[1]={ShellAction::FocusToggle,
                    focusState==FocusTimer::State::Running?L"Pause focus":L"Resume focus"};
                result.count=2;
            }
            break;
        case FileShelf::kId:
            if(filePresent) {
                result.items[0]={ShellAction::FileReveal,L"Reveal file in Explorer"};
                result.items[1]={ShellAction::FileClear,L"Clear file shelf"};
                result.count=2;
            }
            break;
        case 0:
            result.items[0]={ShellAction::StartFocus,L"Start Focus"};
            result.count=1;
            break;
    }
    return result;
}
} // namespace lynx
