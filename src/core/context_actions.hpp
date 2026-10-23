#pragma once
#include "activity_coordinator.hpp"
#include "focus_timer.hpp"
#include "file_shelf.hpp"
#include "media_activity.hpp"
#include <array>
#include <cstdint>
#include <string_view>

namespace lynx {

enum class ShellAction : std::uint8_t { None, MediaToggle, FocusToggle, FocusReset, FileReveal, FileClear };

struct ActionInfo {
    ShellAction action = ShellAction::None;
    std::wstring_view name{};
};

struct ContextActions {
    std::array<ActionInfo,2> items{};
    std::uint8_t count = 0;
    const ActionInfo* at(int index) const {
        return index >= 0 && index < count ? &items[static_cast<std::size_t>(index)] : nullptr;
    }
};

// One source of truth for Window keyboard, visible labels, hit targets and UIA.
// The active activity's ID is authoritative; never invoke a hidden provider.
inline ContextActions contextActions(View view, std::uint32_t activityId,
            FocusTimer::State focusState, bool mediaCanToggle, bool mediaPlaying,
            bool filePresent) {
    ContextActions out;
    if (view != View::Expanded) return out;
    switch (activityId) {
        case MediaActivityBridge::kId:
            if (mediaCanToggle) {
                out.items[0] = {ShellAction::MediaToggle,
                    mediaPlaying ? L"Pause media" : L"Play media"};
                out.count = 1;
            }
            break;
        case FocusTimer::kId:
            if (focusState == FocusTimer::State::Running ||
                focusState == FocusTimer::State::Paused) {
                out.items[0] = {ShellAction::FocusToggle,
                    focusState == FocusTimer::State::Running ? L"Pause focus" : L"Resume focus"};
                out.items[1] = {ShellAction::FocusReset,L"Reset focus"};
                out.count = 2;
            }
            break;
        case FileShelf::kId:
            if (filePresent) {
                out.items[0] = {ShellAction::FileReveal,L"Reveal file in Explorer"};
                out.items[1] = {ShellAction::FileClear,L"Clear file shelf"};
                out.count = 2;
            }
            break;
    }
    return out;
}

} // namespace lynx
