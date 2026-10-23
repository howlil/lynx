#pragma once
#include "activity_coordinator.hpp"
#include <cstdint>
#include <string>

namespace lynx {

// Portable normalization and lifecycle boundary between a platform media
// provider and the activity presentation model. Runs on the UI owner thread.
struct MediaObservation {
    bool available = false;
    bool playing = false;
    std::wstring source;
    std::wstring title;
    std::wstring artist;
};

class MediaActivityBridge {
public:
    bool reconcile(ActivityCoordinator& coordinator, const MediaObservation& media) {
        if (!media.available) {
            if (!active_) return false;
            coordinator.end(kId,generation_,++revision_);
            active_=false;
            ++generation_; // disconnected source may legally reconnect
            revision_=0;
            source_.clear();
            return true;
        }
        const auto nextSource=media.source.empty() ? L"Unknown media source" : media.source;
        if (active_ && source_ != nextSource) {
            coordinator.end(kId,generation_,++revision_);
            ++generation_;
            revision_=0;
            active_=false;
        }
        source_=nextSource;
        const std::wstring title = media.title.empty() ? L"Now Playing" : media.title;
        const std::wstring status = media.playing ? L"Playing" : L"Paused";
        const std::wstring subtitle = media.artist.empty() ?
            status : (media.artist + L" · " + status);
        active_ = coordinator.upsert(Activity{kId,generation_,++revision_,30,
                                  title,subtitle,true});
        return active_;
    }
    static constexpr std::uint32_t kId=100;
    bool active() const { return active_; }
    std::uint64_t generation() const { return generation_; }
private:
    std::uint64_t generation_=1;
    std::uint64_t revision_=0;
    std::wstring source_;
    bool active_=false;
};
} // namespace lynx
