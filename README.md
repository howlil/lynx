# LYNX

A native Windows 11 Dynamic Island utility, starting with a **core feasibility experiment**.

**Current status: M0 experiment, not a completed application.** Product and visual decisions are in [DESIGN.md](DESIGN.md), [.agents/product.md](.agents/product.md), and [.agents/engineering-design.md](.agents/engineering-design.md).

## M1 — Island activity core (stacked implementation)

Branch: \`feat/m1-activity-core\` (based on \`feat/m0-native-shell\`).

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

Download latest successful \`lynx-native-windows-x64\` executable from the feature branch's GitHub Actions run. End the previously running LYNX process before launching a newly extracted build.

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
2. Download the latest **lynx-native-windows-x64** artifact from the newest successful GitHub Actions run on the feature branch, extract it, then run \`lynx_m0.exe\`.
3. Check the **top-center of the monitor where the mouse cursor was located at startup**. Move Task Manager aside: it may cover the overlay when the windows overlap.
4. If still invisible, open the tray overflow (up arrow near the system clock), right-click **LYNX M0** and choose **Show Island on this monitor**. Alternatively try **Ctrl+Alt+Space**.
5. Inspect \`%TEMP%\lynx-m0.log\` (paste that path into File Explorer). This log records the first layered frame, window visibility, size/DPI, per-pixel alpha sample and Windows error codes. Share it when reporting the issue. Do not assume a successful CI compile proves visible pixels.
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
