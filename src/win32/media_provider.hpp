#pragma once

// Windows-only adapter. WinRT callbacks signal a dedicated MTA worker;
// only the window thread applies published immutable snapshots.
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <atomic>
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
        // Request cancellation of the pending WinRT operation before join.
        // This is cooperative: OS implementations may not honor cancellation.
        winrt::Windows::Foundation::IAsyncInfo pending{nullptr};
        {
            std::lock_guard<std::mutex> guard(operationMutex_);
            pending=pendingOperation_;
        }
        if(pending) {
            try { pending.Cancel(); }
            catch(winrt::hresult_error const&) {}
        }
        if (thread_.joinable()) thread_.join();
    }

    void togglePlayPause() {
        {
            std::lock_guard<std::mutex> guard(signal_->mutex);
            if (signal_->stopping) return;
            signal_->toggleRequested = true;
        }
        signal_->cv.notify_one();
    }

private:
    using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
    using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
    using PlaybackStatus = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

    struct Signals {
        std::mutex mutex;
        std::condition_variable cv;
        bool dirty = true;
        bool stopping = false;
        bool toggleRequested = false;
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

    template<typename Operation>
    auto waitForOs(Operation operation) -> decltype(operation.get()) {
        {
            std::lock_guard<std::mutex> guard(operationMutex_);
            pendingOperation_=operation.template as<winrt::Windows::Foundation::IAsyncInfo>();
        }
        try {
            auto result=operation.get();
            {
                std::lock_guard<std::mutex> guard(operationMutex_);
                pendingOperation_=nullptr;
            }
            return result;
        } catch(...) {
            std::lock_guard<std::mutex> guard(operationMutex_);
            pendingOperation_=nullptr;
            throw;
        }
    }

    MediaSnapshot readSession(Session const& session) {
        MediaSnapshot snapshot;
        if (!session) return snapshot;
        snapshot.available = true;
        snapshot.source = session.SourceAppUserModelId().c_str();
        auto playback = session.GetPlaybackInfo();
        if (playback) {
            snapshot.playing = playback.PlaybackStatus() == PlaybackStatus::Playing;
            auto controls = playback.Controls();
            snapshot.canToggle = controls && controls.IsPlayPauseToggleEnabled();
        }
        try {
            const auto props = waitForOs(session.TryGetMediaPropertiesAsync());
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
            auto manager = waitForOs(Manager::RequestAsync());
            Session observed{nullptr};
            winrt::event_token managerCurrent{};
            winrt::event_token managerSessions{};
            winrt::event_token playbackToken{};
            winrt::event_token metadataToken{};
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
                bool toggle = false;
                {
                    std::unique_lock<std::mutex> lock(signal_->mutex);
                    signal_->cv.wait(lock, [this] {
                        return signal_->stopping || signal_->dirty || signal_->toggleRequested;
                    });
                    if (signal_->stopping) break;
                    toggle = std::exchange(signal_->toggleRequested, false);
                    signal_->dirty = false;
                }

                try {
                    Session selected = manager.GetCurrentSession();
                    if (selected != observed) {
                        if (observed && observedRegistered) {
                            observed.PlaybackInfoChanged(playbackToken);
                            observed.MediaPropertiesChanged(metadataToken);
                        }
                        observedRegistered = false;
                        observed = selected;
                        if (observed) {
                            playbackToken = observed.PlaybackInfoChanged(notifyHandler(weak));
                            metadataToken = observed.MediaPropertiesChanged(notifyHandler(weak));
                            observedRegistered = true;
                        }
                    }
                    if (toggle && observed) {
                        auto info = observed.GetPlaybackInfo();
                        if (info && info.Controls() && info.Controls().IsPlayPauseToggleEnabled()) {
                            // Windows decides whether the controlling app supports this.
                            (void) waitForOs(observed.TryTogglePlayPauseAsync());
                        }
                    }
                    auto snapshot = readSession(observed);
                    const bool changed = !published ||
                        snapshot.available != lastPublished.available ||
                        snapshot.playing != lastPublished.playing ||
                        snapshot.canToggle != lastPublished.canToggle ||
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
    std::mutex operationMutex_;
    winrt::Windows::Foundation::IAsyncInfo pendingOperation_{nullptr};
    std::shared_ptr<Signals> signal_ = std::make_shared<Signals>();
    std::thread thread_;
};
} // namespace lynx
