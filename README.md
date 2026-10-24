# LYNX

A native Windows 11 Dynamic Island utility, starting with a **core feasibility experiment**.

**Current status: M4-C compositor candidate A feasibility, stacked on M4-B/M4-A. Input and performance acceptance remain open; not a completed release.** Product and visual decisions are in [DESIGN.md](DESIGN.md), [.agents/product.md](.agents/product.md), and [.agents/engineering-design.md](.agents/engineering-design.md).

## Post-merge runtime review fixes (experimental)

Branch: `fix/post-merge-runtime-review`, based on merged M0–M4-C `master`.

- The main LYNX window now listens for foreground-window **location/size changes** as well as foreground switches; this covers in-place browser F11 transitions without polling.
- C++/WinRT media requests (manager, metadata and playback toggle) use one bounded `wait_for` per operation and request cancellation on timeout, instead of blocking worker shutdown indefinitely on `get()`. Cancellation is best effort; actual unresponsive COM providers and non-async synchronous calls still require Windows reliability testing.
- Candidate A now creates its Win32 **window region** from the Direct2D silhouette using flattened vertices and `SetWindowRgn`. Region tests run at 100/150/200% effective scaling; unlike HTTRANSPARENT, the region excludes off-shape parts of the HWND itself. Manual cross-process mouse testing is still mandatory.
- Ambient/Expanded and placement changes now **retarget from the interpolated geometry** even during an active morph. A 16 ms timer updates only the native input clip region; the visual path still animates using Windows Composition. This introduces CPU cost during morph and may reveal pacing artifacts; measure it on real Windows hardware before choosing the compositor.
- Diagnostic renderer text was removed from Release's Expanded Island but remains available in Debug.

**Still two separate executable targets**, not an implicit switch to the candidate. Do not release or select a compositor solely because CI is green.

## M4-C — Candidate A Win32 Windows.UI.Composition probe

Branch: feat/m4c-composition-probe (stacked on M4-B). This is a **standalone shape/compositor feasibility executable**, not a switch of the production LYNX renderer.

### Two executables in the Windows CI artifact

- `lynx_m0.exe`: existing **GDI+ layered-window** complete prototype, unchanged.
- `lynx_composition_probe.exe`: isolated **Win32 + Windows.UI.Composition** candidate A. It hosts a ShapeVisual in a DesktopWindowTarget with Direct2D-generated CompositionPathGeometry. Native PathKeyFrameAnimation morphs Floating/Top-attached Ambient to Expanded. A 16 ms timer reconciles the OS window region only during morph; composition still owns visual interpolation.

### Probe controls

- **Ctrl+Alt+I**: expand/collapse.
- **Ctrl+Alt+P**: switch Floating / Top-attached.
- **Ctrl+Alt+Q**: quit, export candidate metrics.
- Click inside the currently visible silhouette to expand, or right-click to change placement.
- Repeated user input during a morph retargets from the interpolated pose. Exact native visual/input synchronization remains subject to on-device verification.

**Close any existing LYNX overlay first** to avoid overlapping capsules. The probe has no clock, battery, activity controls, tray, or UI Automation, and is not an equal-feature replacement. Do not run both when comparing resource usage.

### Candidate metrics

Normal quit via Ctrl+Alt+Q exports:

    %TEMP%\lynx-composition-metrics.csv

Samples cover **CPU-side path construction and compositor animation submission only**, measured with QPC. They do not measure individual displayed frames. Existing GDI+ samples include **full CPU frame construction + UpdateLayeredWindow**; these are not equivalent workloads. Do not infer a winner from the shared CSV schema. GPU frame pacing still requires WPR/WPA.

The probe now uses **SetWindowRgn** with a polygon produced by flattening its Direct2D path. HWND regions exclude off-shape points at the OS window boundary. This replaces the unsupported reliance on HTTRANSPARENT for cross-process click-through, but physical Windows acceptance is still required.

### Windows 11 manual acceptance

1. Verify Floating rounded geometry and Top-attached concave shoulders without opaque rectangular backdrop.
2. At 100%, 125%, 150%, 200% DPI and multiple monitors, inspect visual scaling, positioning, clipping.
3. Against Explorer and browser, click outside the visible silhouette but inside its nominal HWND rectangle; verify input passthrough.
4. Check repeated shortcuts during morph, click focus, safe app exit and Windows sleep/resume. Retargeting must preserve continuity and the OS input region must follow the visible contour.
5. Measure frame pacing/GPU with WPR/WPA on actual hardware. Record CPU, memory, DPI, monitor count and OS build.
6. Before choosing a final compositor, add identical text, interactions and activity rendering, then benchmark matching workloads.

**ADR-001 stays OPEN.** A native compositor compiled demo proves API wiring, not visual/input correctness, performance wins or complete app parity.

References: https://learn.microsoft.com/en-us/windows/uwp/composition/using-the-visual-layer-with-win32 and https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/blob/master/cpp/HelloVectors/HelloVectors_win32.cpp

## M4-B — Renderer baseline and accessibility

Branch: feat/m4b-render-accessibility (stacked on M4-A; not production signoff).

### Accessible contextual actions

- Replaces single-button UIA with a fragment root and action buttons for Media Play/Pause, Focus Pause/Resume/Reset and File Shelf Reveal/Clear.
- The context-actions contract is shared by mouse, keyboard and UI Automation. Child fragments have names, runtime identities, bounding rectangles, parent/sibling navigation, focus and Invoke.
- While Expanded: Tab/Shift+Tab chooses an action; Enter/Space invokes; Escape collapses. A visible blue outline follows keyboard focus.
- UIA requests post to the HWND owner, which re-checks the currently active activity and its generation to avoid executing a stale command.
- Required native tests: Inspect, Accessibility Insights, Narrator, tab order, focus events and actual action bounds at multiple DPI scales. Compilation is not screen-reader acceptance.

### Opt-in renderer CPU baseline

The current implementation still uses a GDI+ per-pixel-alpha layered window. Set the environment variable LYNX_RENDER_METRICS=1 before launching LYNX, run a representative workload, and **quit normally through the system tray**. Upon quit, the UI thread exports:

    %TEMP%\lynx-render-metrics.csv

Measured data: draw calls, failed draws, mean and recent p95 QPC wall time, max draw time, bounded recent sample count. Render instrumentation buffers samples in memory and does not write every frame.

PowerShell:

    $env:LYNX_RENDER_METRICS = "1"
    .\lynx_m0.exe
    # When finished, use tray > Quit LYNX.
    Import-Csv "$env:TEMP\lynx-render-metrics.csv" | Format-List

For both idle and animation runs, record CPU/GPU model, Windows build, monitor configuration, DPI, workload, duration, draw counts, and CPU/memory results from scripts/measure-runtime.ps1. GPU/frame pacing requires a WPR/WPA trace and is not captured by this CSV.

To compare measured CPU-render traces **after a real alternative renderer exists**:

    .\scripts\compare-render-metrics.ps1 -Baseline .\layered.csv -Candidate .\composition.csv

The comparator rejects traces with very few draws and prints relative draw-cost differences. It does not establish visual fidelity, GPU performance, actual display latency or FPS.

### Production acceptance still open

- **ADR-001 compositor selection is still open.** Windows.UI.Composition/DirectComposition must be implemented as comparable candidates before claiming a winner.
- Native UI Automation tree, Narrator and focus behavior not yet validated on hardware.
- M4-A fullscreen, monitor, audio-device and resource acceptance not yet complete.
- Packaging/signing/updater/crash policy not finalized.

## M4-A — Windows runtime hardening (first slice)

Branch: feat/m4-runtime-hardening (based on merged master). **Incremental hardening only; not a production certification.**

### Runtime changes

- **Fullscreen visibility:** event-driven WinEvent foreground observer checks whether a visible non-minimized foreground window covers the *physical monitor*. Normal maximized windows leaving the taskbar visible do not trigger hiding. Island restores when fullscreen ends, preserving activities and focus.
- **Display recovery:** removed/disconnected monitor handles fall back to a live display. Layered drawing follows the selected monitor's effective DPI, including when the user changes the monitor through tray.
- **Audio hot-swap:** IMMNotificationClient listens for default multimedia output changes. The UI thread releases the previous endpoint, subscribes to the new one, and resets the volume baseline without showing a false HUD notification.
- **Session recovery:** Focus timers reconcile immediately after sleep/resume; tray icon is recreated after Explorer restarts.
- **Metrics:** scripts/measure-runtime.ps1 records process CPU, working set, private bytes and handle count. It does *not* measure GPU/frame pacing.

### Windows 11 manual acceptance (not proved by CI)

1. Same-monitor fullscreen video/borderless apps: Island hides and restores. Normal maximized apps must not hide it. Repeat on Floating and Top-attached.
2. Switch target monitor between screens with 100/125/150/200% scale. Disconnect/reconnect displays; test click-through outside capsule and no off-screen placement.
3. Change default audio output across speakers/USB/Bluetooth. Volume feedback should follow current default **multimedia** device; no fictitious volume event when devices switch.
4. Sleep during a short Focus timer. Resume should settle expired timer exactly once.
5. Restart Explorer; tray icon recovers, app still quits, no duplicate instances.
6. Record stable Ambient idle and active runs separately. From the repository root, with LYNX running:

~~~powershell
powershell -ExecutionPolicy Bypass -File scripts/measure-runtime.ps1 -Seconds 120 -OutputCsv lynx-idle.csv
~~~

Record OS build, CPU/RAM/GPU, monitor layout, display DPI and current workload. Use WPR/WPA for GPU/frame pacing. Sampling CSV alone cannot prove animation smoothness.

### Still open for production

- The CPU-updated per-pixel alpha renderer has **not** been objectively compared with Windows Composition or DirectComposition; ADR-001 stays open until hardware evidence exists.
- Expanded contextual controls require complete Windows UI Automation child nodes, keyboard focus order, and screen-reader acceptance.
- Exclusive fullscreen, HDR, multi-monitor overlay compatibility and additional apps remain untested.
- Installer/signing/updater, crash diagnostics, and startup distribution decisions remain open.

CI green means compilable and targeted unit tests pass, **not** production acceptance.

## M3 — General-user utilities (native focus, volume HUD, file shelf)

Branch: `feat/m3-general-utilities` (stacked on M2, M1 and M0). **This is an experimental build, not a completed Windows release.**

### Working implementation

- **Focus Timer:** 5/25-minute sessions based on `GetTickCount64()` deadlines, pause/resume/reset, completion feedback, no idle ticking. Windows sleep time counts toward the session. Activity status uses the existing coordinator, with a single completion transition. The current in-progress timer is **not persisted across application exit**, intentionally.
- **System Feedback:** the current default multimedia output's `IAudioEndpointVolumeCallback` emits volume/mute changes to the HWND message queue without polling. Island shows a short passive HUD and restores the previous activity, without taking focus. **Output device switching while running is not yet subscribed**; a restart reacquires the default device.
- **File Shelf (MVP):** drop Explorer files/folders onto the visible Island using OLE `IDropTarget`. At most five distinct paths are kept **in memory only**: no copy/upload, no file contents read, no cloud or persistent history. Open the shelf via tray; Expanded shows first item, **Reveal** selects it in Explorer (does not execute the file), **Clear** forgets references. **Drag-out and item-by-item removal are not yet implemented** and should not be claimed as complete.
- **No redesign:** Floating, Top-attached, ambient clock/lens/battery and spring motion continue from prior milestones. The shell uses context actions while Expanded; other activities are not replaced while interacting.

### Windows manual acceptance

1. End the older LYNX process, download the `lynx-native-windows-x64` artifact of the latest **successful** M3 CI run, extract, and run `lynx_m0.exe`.
2. Tray or right-click Island → **Start Focus · 5 min / 25 min**. Verify timer counts down while Compact, Pause freezes at correct remaining time, Resume does not reset, Reset removes activity, and complete triggers feedback once. Test sleep/resume; elapsed sleep counts.
3. While Focus runs, play supported Windows media; Focus stays primary by priority unless another Expanded activity is explicitly selected. Use tray → **Show File Shelf** after dropping.
4. Raise/lower volume and mute/unmute the **current default multimedia output** using Windows controls. Check transient HUD and restoration; check no focus theft and no repeated idle redraw.
5. From Explorer, drag a small file or folder onto Island. The drop must preserve the source file; **Show File Shelf**, **Reveal** must open/select in Explorer without launching the file, and **Clear** removes it from Island. Try duplicate and sixth file; these should be ignored.
6. Verify OLE drag/drop on Floating **and** Top-attached, plus other windows' click-through. Drag/drop requires a normal foreground desktop (not necessarily compatible with elevated applications).
7. Repeat M0/M1/M2 smoke tests for media, power, DPI scaling, accessibility, fullscreen, lock/resume and resource usage. These behaviors are **not verified by CI alone**.

### Remaining gaps

- Full file drag-out, per-item controls and keyboard/UIA accessibility for each contextual button.
- Core Audio default-device hot-swap via `IMMNotificationClient`, advanced multi-device UI and brightness controls.
- Durable timer persistence policy, user-editable timer presets and notification sounds.
- Compositor comparison, native performance benchmark and robust fullscreen policy remain M0/M4 work.

## M2 — Windows providers (real media + persistent settings)

Branch: `feat/m2-windows-providers` (stacked on M1, then M0). **This is the first OS-integration slice, not a final product release.**

### Implemented in this branch

- **Windows media sessions:** a C++/WinRT GlobalSystemMediaTransportControlsSessionManager (GSMTC) provider subscribes to CurrentSessionChanged, SessionsChanged, PlaybackInfoChanged and MediaPropertiesChanged. The dedicated MTA worker posts owned immutable snapshots into the Win32 window queue. No media polling/render loop.
- **Core media boundary:** a portable MediaActivityBridge reconciles media title, artist, playing/paused state, source switches, and disconnect/reconnect with ActivityCoordinator. Priority 30 puts real media ahead of the mock activities; it remains pinned when the user has expanded it.
- **Media actions:** Expanded Island has a context-sensitive Play/Pause surface. In Expanded, Space controls a supported session; the right-click tray menu also exposes Play/Pause. Disabled when Windows or the source app does not support the toggle.
- **Power notification subscriptions:** Windows power source and battery percent changes are observed through RegisterPowerSettingNotification; the battery snapshot is read on startup and meaningful changes rather than each animation frame. Charging can surface a passive transient.
- **Settings persistence:** save Floating/Top-attached and Reduced Motion into validated DWORDs under `HKCU\Software\LYNX`. Preferences reload at next startup and use system Reduced Motion when no explicit override exists.
- **Cleanup:** revoke WinRT event handlers, join the media worker, and drain queued media messages before tearing down the HWND.

### Smoke test on a real Windows 11 desktop

1. End the old `lynx_m0.exe` instance (single-instance mutex); download the `lynx-native-windows-x64` artifact from the latest successful M2 CI build and launch the extracted executable.
2. Right-click tray/Island, select **Top-attached**, then **Reduced motion**. Exit and relaunch; the preferences should persist.
3. Play a song in a supported app (e.g., Spotify or a browser player exposing Windows system media controls). The Island should transition from Ambient to Compact with the **real title** and display a subtitle in Expanded. Changing tracks should update it. Stopping/disconnecting that session should restore Ambient.
4. In Expanded, click **Play/Pause**, or press **Space** while the Island has focus. The button/menu must be absent or disabled if the media source does not expose that control.
5. While music is playing, create the simulated focus timer from the menu. Confirm the Activity Coordinator still handles both activities, and that an ordinary charging event does not take over an Expanded interaction.
6. Unplug/replug charging power and change battery percentage. Confirm the displayed percentage is valid and the transient does not steal focus. On a battery-less PC, show AC or omit the battery percentage; do not fabricate 0%.
7. Change placement and Reduced Motion, quit via tray, reopen, and verify settings persisted. On corruption/invalid registry values, fallback to documented defaults.
8. Check Task Manager CPU and working set after 5–10 minutes of stable idle and with media active. **No performance result has been measured from CI alone.**

### Known constraints / risks

- **GSMTC** depends on Windows support, a participating media app/session, and the `globalMediaControl` capability. The unpackaged Win32 build may need additional packaging/manifest decisions on some systems. If access fails, the provider falls back without crashing the shell. Inspect `%TEMP%\lynx-m0.log` and UI state.
- Windows system media source selection is the OS current session, **not** a custom multi-player picker yet.
- Media artwork/seek/skip, native timer engine, tray icon branding, rich keyboard/screen-reader accessibility, and robust fullscreen detection are separate future slices.
- The CPU-drawn layered-window spike remains in place; the final compositor and resource budget are **not yet approved**.

### Technical references

- Microsoft GSMTC API: https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager
- Microsoft Win32 + C++/WinRT: https://learn.microsoft.com/en-us/windows/apps/develop/cpp-winrt/get-started
- Microsoft power notifications: https://learn.microsoft.com/en-us/windows/win32/power/registering-for-power-events

## M1 — Island activity core (stacked implementation)

Branch: `feat/m1-activity-core` (based on `feat/m0-native-shell`).

The native silhouette and motion you verified visually in M0 are preserved. The new iteration replaces the temporary demo presentation reducer with one cross-platform **ActivityCoordinator**:

- Stable activity identity with generation/revision, deterministic priority and optional secondary activity.
- Stale callbacks are ignored; once an activity generation ends it cannot be resurrected by a later revision from that same generation.
- User-selected expanded activity remains pinned while it is valid; unrelated system events do not hijack expanded controls.
- Tokenized transients reject older completion timers; fullscreen hides the view without discarding activities.
- Native demo actions use the same coordinator as unit tests rather than directly changing renderer modes.
- **Basic Windows UI Automation:** the shell advertises a name, control state and Invoke action to assistive technology. Rich expanded controls do not yet have a child accessibility tree.

**M1 is a core infrastructure iteration, not a native media/timer integration.** The menu scenarios are simulated; live OS clock and battery remain real.

### M1 manual check

1. Launch: ambient clock/lens/battery remains visually consistent with M0.
2. Right-click Island or tray → **Start / update simulated media**, then **Start / update simulated focus timer**. Higher-priority focus becomes primary; compact shows a secondary dot.
3. Expand by clicking capsule or Ctrl+Alt+Space. Trigger **Simulate transient charging** while Expanded: the visible selection should not be replaced.
4. Close Expanded (Escape, Enter/Space while focused, or click shell). Trigger a transient while Compact and verify it returns to the primary activity.
5. Use **Clear simulated activities** to return to Ambient. Toggle Floating/Attached and Reduced Motion; no change to activity ownership.
6. In the Windows Accessibility Insights/Inspect tool, query the LYNX shell UIA element. Check it exposes an accessible name and **Invoke** action. **The expanded child controls remain future work.**
7. Re-run existing M0 multi-DPI, click-through, focus, fullscreen and resource checks on actual hardware.

Download latest successful `lynx-native-windows-x64` executable from the feature branch's GitHub Actions run. End the previously running LYNX process before launching a newly extracted build.

## M0-A/B/C status

| Area | What exists now | What is NOT proven |
| --- | --- | --- |
| Floating + attached geometry | C++ shared geometry targets and native custom drawn silhouette | Visual quality on Windows hardware and all DPI scales |
| Native HWND | WS_EX_LAYERED / topmost tool window experiment | Focus, hit-test and z-order reliability across systems |
| Rendering | **Layered-window GDI+ per-pixel alpha spike (candidate C)** | This is **not** the final GPU compositor or most resource-efficient solution |
| Motion | Interruptible damped spring, only tick while moving | Frame pacing, compositor acceleration and Apple-like fidelity on hardware |
| Ambient | Real system local time, GetSystemPowerStatus with AC/no-battery fallback | All laptop models and power event patterns |
| Events | Simulated media/charging using native popup menu | Real media, activities, file shelf, clipboard |
| Accessibility | Keyboard hotkey, Escape when interactive | Complete UI Automation and screen-reader proof |
| Tests | Platform-independent geometry/reducer/spring tests | Interactive/native runtime acceptance, GPU/CPU benchmark |

**Important:** this spike uses a CPU-updated premultiplied-alpha DIB and UpdateLayeredWindow specifically to isolate windowing, shape and hit-testing behavior. This is **candidate C**, not a decision against the preferred **Windows.UI.Composition (A)** or **DirectComposition (B)**. M0 remains incomplete until those candidates and native behavior have been evaluated with comparable measurements.

## Build (on Windows 11)

Prerequisites: Visual Studio 2022 with Desktop development with C++, Windows 10/11 SDK, CMake >= 3.20.

~~~powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\lynx_m0.exe
~~~

The same portable core tests also build on Linux/macOS with CMake, but the overlay executable requires Windows.

## Demo controls

- Left click the visible Island: expand/collapse.
- Right click the Island or the **LYNX M0** tray icon: choose **Show Island on this monitor**, Floating/Top-attached, Ambient/Media mock, transient event, reduced motion, fullscreen hide simulation, or quit.
- **Ctrl+Alt+Space:** attempt to expand/collapse via global shortcut.
- **Escape:** close Expanded if LYNX currently has keyboard focus.
- If the Island is hidden by the fullscreen simulation, use the tray icon to restore it.

Media in M0 is intentionally **fake demonstration content**, not Spotify or Windows media integration. Clock and battery/power state are read from Windows. No cloud, analytics, clipboard capture, Explorer injection or administrator privilege required.

## Troubleshooting: process exists but no Island is visible

A Task Manager process row only proves the executable is alive. LYNX is intentionally a tool window, so it is absent from regular taskbar/Alt+Tab windows.

1. **Close any older LYNX M0 process** in Task Manager. Only one instance can run; launching the new EXE while the old process holds the mutex will quietly exit the new instance.
2. Download the latest **lynx-native-windows-x64** artifact from the newest successful GitHub Actions run on the feature branch, extract it, then run `lynx_m0.exe`.
3. Check the **top-center of the monitor where the mouse cursor was located at startup**. Move Task Manager aside: it may cover the overlay when the windows overlap.
4. If still invisible, open the tray overflow (up arrow near the system clock), right-click **LYNX M0** and choose **Show Island on this monitor**. Alternatively try **Ctrl+Alt+Space**.
5. Inspect `%TEMP%\lynx-m0.log` (paste that path into File Explorer). This log records the first layered frame, window visibility, size/DPI, per-pixel alpha sample and Windows error codes. Share it when reporting the issue. Do not assume a successful CI compile proves visible pixels.
6. If startup rendering returns a failure, the updated version shows a **diagnostic message box and exits**, rather than continuing as a hidden background process.

The preview should be a **small, ~232x42 DIP black pill near the very top of the screen**, not a full desktop application window. Its internal surface is experimental, not the complete prototype design. If the latest native build continues to be invisible but the log says "first layered frame OK", the remaining problem may involve desktop composition, monitor selection or topmost ordering and requires a Windows desktop test.

## Manual acceptance matrix — must run on Windows

Do not mark any of these as passed without a dated test environment and results.

1. Shape: compare Floating and Top-attached at **100%, 125%, 150%, 200%** scale, noting visible seams and corners.
2. Hit testing: drag/select desktop and other app controls **outside** Island silhouette (including within bounding window) and confirm clicks pass through.
3. Input: hover, click-to-expand, Escape/shortcut, Alt+Tab visibility, focus restoration to original app; verify other apps are not interrupted by passive notifications.
4. Motion: rapid expand/collapse while spring in flight; verify no jumps, flicker or sticky transparency. Toggle reduced motion.
5. Ambient: verify correct clock, AC/charging and battery-less device fallback; update Windows clock/timezone, unplug charger, sleep/resume.
6. Environment: multiple monitors, mixed DPI, unplugging secondary monitor, fullscreen game/video, lock/unlock.
7. Resource: measure **process idle CPU/working set and frame pacing** with Windows Performance Recorder / Analyzer; record OS build/hardware/scaling and measurement duration.
8. Accessibility: keyboard-only interactions, UIA tree, screen-reader labels and high contrast. **Not yet implemented** in spike.
9. Reliability: exit/relaunch single instance, Explorer restart, device loss/recovery.

## Next engineering work

1. Run Windows CI and fix compile issues; validate this layered spike on real hardware.
2. Build **candidate A (Win32 + Windows.UI.Composition / Visual Layer)** using official Microsoft's Win32 Composition/HelloVectors samples as guidance; compare custom path morph and alpha-hit-testing with this spike.
3. If candidate A cannot satisfy it cleanly, evaluate **candidate B (DirectComposition + Direct2D)**.
4. Record a final compositor ADR only after objective M0 gates pass. Then implement coherent Activity Coordinator and genuine media provider in later milestones.

Official references:
- [Win32 layered window behavior](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)
- [Visual Layer with Win32](https://learn.microsoft.com/en-us/windows/uwp/composition/using-the-visual-layer-with-win32)
- [HelloVectors native Win32](https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/blob/master/cpp/HelloVectors/HelloVectors_win32.cpp)
