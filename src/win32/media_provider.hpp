#pragma once

// Windows-only adapter. WinRT callbacks signal a dedicated MTA worker;
// only the window thread applies published immutable snapshots.
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace lynx {

struct MediaSnapshot {
    bool available = false;
    bool playing = false;
    bool canToggle = false;
    bool canNext = false, canPrevious = false;
    std::int64_t positionMs = 0, durationMs = 0;
    std::uint64_t measuredAtMs = 0;
    std::wstring source;
    std::wstring title;
    std::wstring artist;
    // Human-readable diagnostic only; never display as a successful action.
    std::wstring error;
};

class WindowsMediaProvider {
public:
    using Sink = std::function<void(MediaSnapshot)>;

    explicit WindowsMediaProvider(Sink sink) : sink_(std::move(sink)) {
        thread_ = std::thread([this] { run(); });
    }
    WindowsMediaProvider(const WindowsMediaProvider&) = delete;
    WindowsMediaProvider& operator=(const WindowsMediaProvider&) = delete;

    ~WindowsMediaProvider() {
        {
            std::lock_guard<std::mutex> guard(signal_->mutex);
            signal_->stopping = true;
        }
        signal_->cv.notify_one();
        if (thread_.joinable()) thread_.join();
    }

    void togglePlayPause() { request(Command::Toggle); }
    void skipNext() { request(Command::Next); }
    void skipPrevious() { request(Command::Previous); }

private:
    enum class Command : std::uint8_t { None,Toggle,Previous,Next };
    void request(Command command) {
        {
            std::lock_guard<std::mutex> guard(signal_->mutex);
            if(signal_->stopping)return;
            signal_->command=command;
        }
        signal_->cv.notify_one();
    }
    using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
    using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
    using PlaybackStatus = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

    struct Signals {
        std::mutex mutex;
        std::condition_variable cv;
        bool dirty = true;
        bool stopping = false;
        Command command=Command::None;
    };

    // Event handlers may outlive the subscription-removal call briefly;
    // their only capture is a weak pointer to independently owned signals.
    static auto notifyHandler(const std::weak_ptr<Signals>& weak) {
        return [weak](auto const&, auto const&) {
            if (auto signal = weak.lock()) {
                {
                    std::lock_guard<std::mutex> guard(signal->mutex);
                    if (signal->stopping) return;
                    signal->dirty = true;
                }
                signal->cv.notify_one();
            }
        };
    }

    static MediaSnapshot readSession(Session const& session) {
        MediaSnapshot snapshot;
        if (!session) return snapshot;
        snapshot.available = true;
        snapshot.source = session.SourceAppUserModelId().c_str();
        auto playback = session.GetPlaybackInfo();
        if (playback) {
            snapshot.playing = playback.PlaybackStatus() == PlaybackStatus::Playing;
            auto controls = playback.Controls();
            snapshot.canToggle = controls && controls.IsPlayPauseToggleEnabled();
            snapshot.canNext = controls && controls.IsNextEnabled();
            snapshot.canPrevious = controls && controls.IsPreviousEnabled();
        }
        try {
            const auto timeline=session.GetTimelineProperties();
            if(timeline) {
                constexpr std::int64_t ticksPerMs=10000;
                const auto start=timeline.StartTime().count();
                const auto end=timeline.EndTime().count();
                snapshot.durationMs=std::max<std::int64_t>(0,(end-start)/ticksPerMs);
                snapshot.positionMs=std::clamp<std::int64_t>(
                    (timeline.Position().count()-start)/ticksPerMs,
                    0,snapshot.durationMs);
                snapshot.measuredAtMs=GetTickCount64();
            }
        } catch(winrt::hresult_error const&) {
            // Timeline support is optional for each media source.
        }
        try {
            const auto props = session.TryGetMediaPropertiesAsync().get();
            if (props) {
                snapshot.title = props.Title().c_str();
                snapshot.artist = props.Artist().c_str();
            }
        } catch (winrt::hresult_error const&) {
            // Metadata can fail while playback state is still available.
        }
        if (snapshot.title.empty()) snapshot.title = L"Now Playing";
        return snapshot;
    }

    void run() noexcept {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            auto manager = Manager::RequestAsync().get();
            Session observed{nullptr};
            winrt::event_token managerCurrent{};
            winrt::event_token managerSessions{};
            winrt::event_token playbackToken{};
            winrt::event_token metadataToken{};
            winrt::event_token timelineToken{};
            bool managerCurrentRegistered = false;
            bool managerSessionsRegistered = false;
            bool observedRegistered = false;
            const std::weak_ptr<Signals> weak{signal_};

            managerCurrent = manager.CurrentSessionChanged(notifyHandler(weak));
            managerCurrentRegistered = true;
            managerSessions = manager.SessionsChanged(notifyHandler(weak));
            managerSessionsRegistered = true;

            MediaSnapshot lastPublished;
            bool published = false;
            for (;;) {
                Command command=Command::None;
                {
                    std::unique_lock<std::mutex> lock(signal_->mutex);
                    signal_->cv.wait(lock, [this] {
                        return signal_->stopping || signal_->dirty || signal_->command!=Command::None;
                    });
                    if (signal_->stopping) break;
                    command=std::exchange(signal_->command,Command::None);
                    signal_->dirty = false;
                }

                try {
                    Session selected = manager.GetCurrentSession();
                    if (selected != observed) {
                        if (observed && observedRegistered) {
                            observed.PlaybackInfoChanged(playbackToken);
                            observed.MediaPropertiesChanged(metadataToken);
                observed.TimelinePropertiesChanged(timelineToken);
                            observed.TimelinePropertiesChanged(timelineToken);
                        }
                        observedRegistered = false;
                        observed = selected;
                        if (observed) {
                            playbackToken = observed.PlaybackInfoChanged(notifyHandler(weak));
                            metadataToken = observed.MediaPropertiesChanged(notifyHandler(weak));
                            timelineToken = observed.TimelinePropertiesChanged(notifyHandler(weak));
                            observedRegistered = true;
                        }
                    }
                    if(command!=Command::None && observed) {
                        auto info=observed.GetPlaybackInfo();
                        auto controls=info?info.Controls():nullptr;
                        if(controls) {
                            if(command==Command::Toggle && controls.IsPlayPauseToggleEnabled())
                                (void)observed.TryTogglePlayPauseAsync().get();
                            else if(command==Command::Previous && controls.IsPreviousEnabled())
                                (void)observed.TrySkipPreviousAsync().get();
                            else if(command==Command::Next && controls.IsNextEnabled())
                                (void)observed.TrySkipNextAsync().get();
                        }
                    }
                    auto snapshot = readSession(observed);
                    const bool changed = !published ||
                        snapshot.available != lastPublished.available ||
                        snapshot.playing != lastPublished.playing ||
                        snapshot.canToggle != lastPublished.canToggle ||
                        snapshot.canNext != lastPublished.canNext ||
                        snapshot.canPrevious != lastPublished.canPrevious ||
                        snapshot.positionMs != lastPublished.positionMs ||
                        snapshot.durationMs != lastPublished.durationMs ||
                        snapshot.source != lastPublished.source ||
                        snapshot.title != lastPublished.title ||
                        snapshot.artist != lastPublished.artist ||
                        snapshot.error != lastPublished.error;
                    if (changed) {
                        sink_(snapshot);
                        lastPublished = std::move(snapshot);
                        published = true;
                    }
                } catch (winrt::hresult_error const& e) {
                    MediaSnapshot failed;
                    failed.error = L"Media session unavailable (HRESULT " +
                        std::to_wstring(static_cast<unsigned long>(e.code().value)) + L")";
                    if (!published || failed.error != lastPublished.error) {
                        sink_(failed);
                        lastPublished = std::move(failed);
                        published = true;
                    }
                }
            }
            if (observed && observedRegistered) {
                observed.PlaybackInfoChanged(playbackToken);
                observed.MediaPropertiesChanged(metadataToken);
            }
            if (managerCurrentRegistered) manager.CurrentSessionChanged(managerCurrent);
            if (managerSessionsRegistered) manager.SessionsChanged(managerSessions);
        } catch (winrt::hresult_error const& e) {
            MediaSnapshot failed;
            failed.error = L"Windows media manager unavailable (HRESULT " +
                std::to_wstring(static_cast<unsigned long>(e.code().value)) + L")";
            sink_(std::move(failed));
        } catch (...) {
            MediaSnapshot failed;
            failed.error = L"Windows media provider failed";
            sink_(std::move(failed));
        }
    }

    Sink sink_;
    std::shared_ptr<Signals> signal_ = std::make_shared<Signals>();
    std::thread thread_;
};
} // namespace lynx
