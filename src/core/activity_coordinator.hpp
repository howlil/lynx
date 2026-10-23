#pragma once
#include "island_core.hpp"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace lynx {

// Each source owns monotonically increasing (generation, revision) tuples.
// Tombstones remain so delayed callbacks cannot bring back a completed activity.
struct Activity {
    std::uint32_t id = 0;
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    int priority = 0;
    std::wstring title;
    std::wstring subtitle;
    bool active = false;
};

class ActivityCoordinator {
public:
    View view() const {
        if (fullscreen_) return View::Hidden;
        if (transient_) return View::Transient;
        if (expanded_) return View::Expanded;
        return primary().has_value() ? View::Compact : View::Ambient;
    }
    bool fullscreen() const { return fullscreen_; }
    bool expanded() const { return expanded_; }

    // Reject old/same revision, including updates after an end tombstone.
    bool upsert(Activity next) {
        if (!next.id || !next.generation || !next.revision) return false;
        const auto it = activities_.find(next.id);
        if (it != activities_.end() &&
            ((!it->second.active && next.generation == it->second.generation) ||
             !isNewer(next, it->second))) return false;
        next.active = true;
        activities_[next.id] = std::move(next);
        return true;
    }

    bool end(std::uint32_t id, std::uint64_t generation, std::uint64_t revision) {
        if (!id || !generation || !revision) return false;
        const auto it = activities_.find(id);
        if (it != activities_.end()) {
            if (generation < it->second.generation ||
                (generation == it->second.generation && revision <= it->second.revision))
                return false;
        }
        Activity tombstone;
        tombstone.id = id;
        tombstone.generation = generation;
        tombstone.revision = revision;
        tombstone.active = false;
        activities_[id] = std::move(tombstone);
        // Keep expanded open as a neutral overview if its source disappears.
        if (selected_ == id) selected_ = 0;
        return true;
    }

    std::optional<Activity> primary() const {
        // While expanded, a live selected activity remains pinned. It cannot
        // be displaced by an unrelated noncritical event.
        if (expanded_ && selected_) {
            if (const auto it = activities_.find(selected_);
                it != activities_.end() && it->second.active) return it->second;
        }
        std::optional<Activity> best;
        for (const auto& [id, value] : activities_) {
            if (!value.active) continue;
            if (!best || value.priority > best->priority ||
                (value.priority == best->priority && value.id < best->id))
                best = value;
        }
        return best;
    }

    std::optional<Activity> secondary() const {
        auto main = primary();
        if (!main) return std::nullopt;
        std::optional<Activity> result;
        for (const auto& [id, value] : activities_) {
            if (!value.active || id == main->id) continue;
            if (!result || value.priority > result->priority ||
                (value.priority == result->priority && value.id < result->id))
                result = value;
        }
        return result;
    }

    void expand() {
        if (fullscreen_) return;
        // An explicit user action dismisses transients.
        transient_.reset();
        selected_ = primary() ? primary()->id : 0;
        expanded_ = true;
    }
    void collapse() {
        expanded_ = false;
        selected_ = 0;
    }

    // An ordinary notification never takes over a user's expanded controls.
    // A monotonically increasing token prevents an obsolete timeout from
    // cancelling a newer event.
    std::uint64_t transient(std::wstring message) {
        if (fullscreen_ || expanded_) return 0;
        ++nextTransientId_;
        transient_ = Transient{nextTransientId_,std::move(message)};
        return nextTransientId_;
    }
    bool expireTransient(std::uint64_t token) {
        if (!transient_ || !token || token != transient_->token) return false;
        transient_.reset();
        return true;
    }
    const std::wstring& transientText() const {
        static const std::wstring empty;
        return transient_ ? transient_->text : empty;
    }
    void setFullscreen(bool value) {
        if (fullscreen_ == value) return;
        fullscreen_ = value;
        if (value) transient_.reset();
        // Returning to the desktop recomputes from current active activities.
    }
private:
    struct Transient {
        std::uint64_t token;
        std::wstring text;
    };
    static bool isNewer(const Activity& incoming, const Activity& old) {
        return incoming.generation > old.generation ||
               (incoming.generation == old.generation && incoming.revision > old.revision);
    }
    std::map<std::uint32_t,Activity> activities_;
    std::optional<Transient> transient_;
    std::uint64_t nextTransientId_ = 0;
    std::uint32_t selected_ = 0;
    bool expanded_ = false;
    bool fullscreen_ = false;
};
} // namespace lynx
