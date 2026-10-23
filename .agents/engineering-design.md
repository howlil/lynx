# LYNX — Engineering Design

Status: research and architecture proposal; **not yet validated on native Windows**
Updated: 24 October 2026
Product contract: product.md
Design baseline: ../DESIGN.md
Contributor/routing rules: ../AGENTS.md

## 1. System context and constraints

LYNX is a small always-available Windows 11 desktop utility. Its native overlay displays Ambient clock/power at rest and compact contextual activities when required. It must support **Floating** and **Top-attached concave notch** shapes without two separate UI implementations.

Requirements driving architecture:
- Native, compact GUI; no Electron/WebView core.
- Fine control of topmost placement, alpha/transparency, input passthrough and activation.
- Smooth interruptible shape transitions and no continuous render loop when static.
- Event-driven clock/power/media adapters with honest capability fallbacks.
- Keyboard/UI Automation access, reduced motion, correct focus restoration.
- Multi-monitor, DPI changes, lock/sleep/resume, fullscreen, resource/device recovery.
- General-user core before any developer/productivity extension.

Do not treat browser mock success as evidence of Windows API compatibility. Native runtime and benchmarks are pending.

## 2. Decision summary

| Decision | Recommendation | Status |
| --- | --- | --- |
| Runtime language | C++20 | Proposed baseline |
| Host/window | Standalone Win32 HWND, single user process | Proposed baseline |
| Drawing | Direct2D + DirectWrite for custom visuals/text | Candidate |
| Composition approach A | Win32 + Windows.UI.Composition / Visual Layer | **M0 evaluation candidate, preferred to try first** |
| Composition approach B | Win32 + DirectComposition + Direct2D | **M0 comparison candidate** |
| Layered window fallback | Per-pixel-alpha layered HWND if required | Experiment only |
| State management | Single serialized presentation owner/coordinator | Architecture decision |
| Placement | One geometry contract, Floating and Top-attached strategies | Architecture decision |
| Settings persistence | Local lightweight preferences; no database | Architecture decision |
| Third-party extensions | Stable internal activity contract first; no plugin host | Deferred |
| Package/distribution | Decide after native core is stable | Open |

**Compositor is NOT decided.** Select it only after proving shape morphing, transparency, hit testing, focus, recovery, CPU/memory and accessibility. Do not start a full feature build on an unverified drawing stack.

## 3. Responsibilities / dependency graph

    Windows OS
      | clock, power, media, display, input, accessibility signals
      v
    Platform Adapters
      | validated normalized observations, availability/capabilities, errors
      v
    Activity Coordinator
      | activity lifecycle, prioritization, transient restoration
      v
    Presentation Controller
      | immutable presentation snapshot (ambient/compact/expanded/etc.)
      +-----------------------+
      |                       |
      v                       v
    Placement Controller   Motion Controller
      | geometry target       | interpolation/interrupt handling
      +-----------+-----------+
                  |
                  v
          Native Renderer / HWND
          (geometry, content, clipping, hit-testing)
                  |
                  v
             Windows desktop

    Settings Store -> Presentation / Placement / Motion configuration
    UI input -> Presentation Controller -> Activity action dispatcher -> owning provider

Responsibilities should remain grouped within **one process** unless an actual lifecycle/security/reliability requirement forces separation. This is a conceptual separation, not a requirement to create one class or thread per box.

## 4. State ownership and invariants

| State | Authoritative owner | Lifecycle |
| --- | --- | --- |
| Clock snapshot | Clock/OS adapter | System-local event/minute update; refresh on timezone/time change |
| Power snapshot | Power/OS adapter | Power notifications; unavailable/no battery are distinct valid outcomes |
| Media session snapshot | Media adapter | Session added/changed/removed; supported actions are capabilities |
| Active activities and precedence | Activity Coordinator | Upsert/end/expire; reject stale generation updates |
| Visible state and selected activity | Presentation Controller | Ambient, Compact, Minimal, Expanded, Transient, Hidden |
| Placement/monitor | Placement Controller | Reconcile configuration and live monitor bounds |
| Current motion target | Motion Controller | Retarget from current visible transition, stop once settled |
| User settings | Settings Store | Load on startup; validated modifications persisted locally |
| GPU/brush/text/path objects | Renderer | Recreate after device loss; **never** own business activity truth |

System invariants:

- Only one authority chooses the visible primary activity. Providers cannot directly open/close the Island.
- No OS callback may mutate the native visual tree off its designated UI context.
- A stale provider update must not resurrect a completed or replaced activity.
- Hidden does not cancel the work represented by an activity.
- Transient completion restores a *still-valid* prior state, not blindly a stale cached visual.
- Placement/animation do not mutate activity ownership.
- Renderer cache/device loss cannot corrupt session or preference state.
- Controls execute actions only if the owning provider still exposes the capability.
- No persistent animation/tick renders at rest without a visible change.

## 5. Internal contract sketches (system level, not implementation ABI)

**ActivitySnapshot** should represent at minimum: stable ID, source/provider ID, generation/revision, status, attention level, compact identity, detail text, optional progress, supported action descriptors, and relevant termination/expiry semantics.

**PresentationSnapshot** should represent: presentation kind, selected primary activity ID (if any), optional secondary identity, transient event (if any), placement and monitor environment, current content model, and accessible labels/states.

**PlatformObservation** must identify its source/capabilities and distinguish available, temporarily unavailable, unsupported, and failed. Untrusted integrations in future must be validated at their boundary.

**UserIntent / ActionResult:** an interaction includes the target activity/action identity. The dispatch layer verifies that the activity generation/action remains current; a rejected/unsupported action returns a perceivable, recoverable result.

**GeometryTarget:** placement mode + current presentation + width/height/shoulder/radii + monitor/DPI context. Each placement maps the **same semantic presentation** to a different contour and anchor.

The above are conceptual contracts, not final type layouts or a required class hierarchy. Final shapes and error handling belong to $design-thinking.

## 6. Rendering, geometry and windowing

### Native surface

- Standalone top-center overlay on a valid display; test the needed combination of topmost/tool-window/nonactivating styles.
- Prefer a window whose footprint is as small as practical; if maximum expansion bounds are retained, alpha + hit testing **must not** capture input outside the visible contour.
- Differentiate **visual transparency** from **pointer/activation transparency**. Test both rather than assuming one implies the other.
- WM_NCHITTEST, per-pixel alpha, window region and style choices are candidates. HTTRANSPARENT is **not** a guaranteed global cross-process click-through solution.
- Default Compact/passive event behavior must not grab foreground focus. Expanded needs an intentional accessible activation/keyboard route; may require a separate interaction model/host if testing shows it is necessary.
- Tray and global shortcut provide recovery when shell is hidden.

### Floating geometry

Full capsule, offset ~13 DIP from top of monitor, Ambient ~232x42 DIP, reference radius ~22 DIP. Expand symmetrically around selected top anchor.

### Top-attached geometry

Continuous dynamic silhouette flush with monitor top, Ambient frame ~264x42 DIP, two **concave shoulder** fillets ~16x16 DIP into convex rounded lower corners. Expanded reference body ~330 DIP, frame including shoulders ~362 DIP.

Dynamic Bézier/path geometry must clip correctly and match pointer hit-testing during animation. Do not approximate with a rectangular alpha blocker or a bottom-rounded rectangle.

### Native composition candidates

**A: Windows.UI.Composition / Visual Layer**: shape visuals, CompositionPathGeometry / PathKeyFrameAnimation; investigate direct Win32 hosting and shape/window alpha synchronization. Microsoft's HelloVectors sample shows path morphing but **does not alone prove** our exact topmost transparent window and input model.

**B: DirectComposition / Direct2D**: retained composition visuals plus custom path drawing; investigate how contour interpolation, resizing, alpha, hit-testing and text quality behave. Consider B if A cannot meet platform constraints cleanly.

**C: layered fallback**: compare per-pixel alpha surface and UpdateLayeredWindow behavior only if A/B require an alternative; measure animation overhead.

Avoid a commitment to one implementation before M0 evidence.

## 7. Motion and update policy

Agreed visual intent: smooth, damped, interruption-safe geometry morph; restrained content reveal; no idle decorative animation.

Prototype tuning: smooth stiffness 440/damping 40; snappy stiffness 830/damping 51. These are **prototype values, not equivalent native compositor physical settings**. Native tuning depends on timing model, units and measured refresh/frame pacing.

Rendering update sources:
- Clock: schedule near minute boundary, update on system time/timezone changes.
- Power: observe battery/AC notifications, do not poll every frame.
- Media: subscribe to OS/session events and capabilities.
- Shell motion: render/compose only during a visual transition or visible content update.
- Reduced Motion: no spatial interpolation; same behavior and information.

Animation retargeting must handle rapid expand/collapse and new OS events without discontinuous jumps, lost focus, duplicated completion feedback or transient flicker.

## 8. Critical runtime flows

### Startup

    Single-instance check
      -> load/validate preferences
      -> discover monitor/DPI
      -> create native window/compositor resources
      -> subscribe to clock/power and other enabled providers
      -> normalize available ambient data
      -> render Ambient
      -> wait for events

Failed power/media provider does not fail the shell. Failure to initialize the renderer must be reported through an appropriate accessible or tray-level path, not a silently invisible broken process.

### Platform event

    Provider callback
      -> normalize + validate + tag generation
      -> enqueue into serialized Activity Coordinator context
      -> reduce activity state
      -> select eligible primary/transient/secondary
      -> immutable PresentationSnapshot
      -> GeometryTarget + motion retarget
      -> native composition

Out-of-order/late callbacks are ignored when superseded by a newer generation.

### User expands and acts

    Click/shortcut
      -> intentional activation of interactive surface
      -> expanded presentation + UIA semantics
      -> user invokes supported action
      -> dispatch to current owning provider
      -> show result / recoverable error
      -> Escape/collapse -> restore valid focus and compact/ambient

Do not assume WS_EX_NOACTIVATE makes a full interactive keyboard panel work automatically.

### Transient during another interaction

    Ordinary power/status event
      -> coordinator checks attention and current interaction
      -> suppress/defer noncritical feedback if user is acting
      -> otherwise transient compact
      -> expire -> recompute valid previous presentation

### Fullscreen / display change

    Windows foreground/display notification
      -> reconcile policy and monitor
      -> hide/move/resize without cancelling activity
      -> restore only on valid monitor/bounds and without focus theft

### Renderer/device loss

    Graphics resources lost
      -> suspend rendering / release stale resources
      -> recreate target resources
      -> redraw from latest PresentationSnapshot
      -> resume event delivery without duplicate activity

## 9. Performance and operating guarantees

These are **proposed budgets**, not measured results:

| Metric | Design budget / proof |
| --- | --- |
| Idle CPU | Aim <0.5% average on a defined reference Windows machine |
| Idle working set | Initial aim <=60 MB; measure under a documented workload |
| Stable Ambient redraw | No continuous frame loop |
| Animation | Smooth 60/120 Hz where supported; measure actual frame pacing rather than asserting FPS |
| Interaction | No unexpected focus theft or blocked desktop clicks |
| Recovery | Exit/return from fullscreen, monitor disconnect, sleep/resume, OS shell restart |
| Accessibility | Keyboard and Windows UI Automation access; reduced motion/high contrast |

Use Windows Performance Recorder / Analyzer (ETW) for CPU, memory, GPU and frame-pacing evidence. Benchmarks must include hardware, OS build, DPI, monitor setup, idle duration and measurement method; otherwise comparisons are misleading.

No remote telemetry or network connectivity is required by the core. Store only necessary local preferences. Any future clipboard/notification integration requires separate explicit privacy analysis and permissions.

## 10. Risks and experiments: M0 native feasibility

| Experiment | What to prove | Fail condition |
| --- | --- | --- |
| M0-A Window/transparency | Alpha edges render correctly, no invisible click shield | Clicks outside shape blocked or focus stolen |
| M0-B Shape | Both placements, top shoulder joins, DPI 100/125/150/200% | Seams, sharp corners, visible clipping defects |
| M0-C Motion | Morph and rapid retargeting without jumping/flickering | Visual discontinuity or incessant idle frames |
| M0-D Input and accessibility | Expand/close from keyboard, focus restore, UIA available | Keyboard-only path broken or screen reader cannot perceive essential controls |
| M0-E Environment | Multi-monitor, fullscreen, lock/sleep/resume, display changes | Stuck off-screen or unintentional overlay over fullscreen |
| M0-F Resource baseline | Profile idle CPU/memory and active frame pacing | Persistent idle render/polling or unacceptable measured overhead |

Implement M0 with **simulated activities** and the agreed v3 geometry. **Do not** build file shelf, clipboard, AI/developer integration or a plugin system for M0.

At M0 exit: select compositor based on evidence and record consequential ADR. If A fails, try B; use C only if required. The feasibility milestone cannot be declared successful from the browser HTML prototype.

## 11. Subsequent milestones (direction, not a delivery claim)

- **M1 — Native shell/core:** presentation state machine, geometry strategies, motion, accessibility, clean expand/collapse.
- **M2 — Windows ambient/providers:** local clock, battery/power, display environment, tray, preferences, then media provider.
- **M3 — General-user modules:** timer, system HUD and file shelf after core stabilizes.
- **M4 — Hardening:** profiling, device-loss recovery, packaging, platform regression matrix, safe startup/exit.

Feature implementations may adjust sequencing if their actual dependencies warrant it, but must not bypass M0 correctness gates.

## 12. Architectural decision records (only when justified)

1. **ADR-001 — compositor selection** (pending M0 comparative evidence).
2. **ADR-002 — activity state ownership** (recommended: serialized single authoritative coordinator; preserve in contract).
3. **ADR-003 — overlay activation / input model** (pending native keyboard, hit-test and focus tests).

Do not create empty ADR files merely to signal progress; record an actual decision and rationale when made.

## 13. References and verification status

- Win32 Visual Layer: https://learn.microsoft.com/en-us/windows/uwp/composition/using-the-visual-layer-with-win32
- Microsoft native HelloVectors example: https://github.com/microsoft/Windows.UI.Composition-Win32-Samples/blob/master/cpp/HelloVectors/HelloVectors_win32.cpp
- Layered windows: https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features
- Direct2D path geometry: https://learn.microsoft.com/en-us/windows/win32/direct2d/path-geometries-overview
- Apple HIG Live Activities: https://developer.apple.com/design/human-interface-guidelines/live-activities

**Verified so far:** v3 browser mock exercises visual/interaction concepts. **Not verified:** C++ compile, native Windows compositor, transparent input, OS providers, performance, screen reader support or fullscreen reliability. Do not report these as completed.


## M2 implementation record — real Windows providers (24 October 2026)

**Status:** coded, compiled and core-tested in stacked draft PR #3; manual native Windows acceptance is still open. This note does not change ADR-001: GDI+ layered HWND remains a feasibility spike, not the final compositor decision.

**Current path:** `WindowsMediaProvider (GSMTC / C++/WinRT worker MTA)` → immutable `MediaSnapshot` message → `MediaActivityBridge` (portable lifecycle reducer) → `ActivityCoordinator` → existing shell. Event handlers merely enqueue refresh work to a worker; no direct renderer mutation or continuous media polling. The provider unsubscribes, joins the worker and drains owned snapshot messages at shutdown. Missing/unsupported media sessions degrade to Ambient.

**Presentation:** supported Now Playing title/artist, playing/paused status, priority 30, source switching and disconnect/reconnect. The Expanded surface and tray expose Play/Pause only when Windows reports that the current session permits it. Artwork, seek, skip and user-selected source routing remain out of scope.

**Power:** `RegisterPowerSettingNotification` for `GUID_ACDC_POWER_SOURCE` and `GUID_BATTERY_PERCENTAGE_REMAINING`; the cached status is read on startup and meaningful changes, rather than once per animation frame. A charging transition may generate a passive transient without stealing focus.

**Preferences:** `HKCU\Software\LYNX` stores validated DWORDs for placement and reduced-motion override. A missing override follows the OS animation preference.

**Evidence:** Windows x64 compile/CTest and portable Linux tests passed at `7402df0d` in GitHub Actions run 40034238982. Tests cover source switching, playback status, disappearing/reappearing sessions and coordinator prioritization. **Not verified**: live GSMTC access/permission on the user's device, player compatibility, actual Play/Pause, charging event visual UX, persistence on reboot, Windows UI Automation details and idle CPU/GPU resource budget. GSMTC documentation declares the `globalMediaControl` capability; packaging/identity/permissions may need follow-up based on the observed HRESULT.

**Handoff:** before finishing M2 or merging, test the downloadable Windows executable with an actual media player, inspect fallback/error handling, then run the existing DPI/focus/performance acceptance matrix. No new service, cloud backend or native extension system is justified by this integration.


## M3 implementation record — general-user native utilities (24 October 2026)

**Stage:** stacked implementation on M2; Windows CI validates compilation and portable core contracts, not actual desktop usability or native-performance claims.

**Focus Timer:** `src/core/focus_timer.hpp` owns deadline, monotonic elapsed time, pause/resume, completion-once and ActivityCoordinator publication. `GetTickCount64` is the platform clock on the HWND thread. A Windows timer wakes once per second **only while running** and the presentation redraws when a visible value changes; timer state is not persisted on shutdown. Time asleep/hibernated counts as elapsed by explicit policy.

**System feedback:** `src/win32/volume_feedback.hpp` owns Core Audio endpoint notification registration and teardown. Callback `IAudioEndpointVolumeCallback::OnNotify` only posts a message, which the UI thread resolves into a transient with a volume/mute value. Reacquiring a new default output device while running is not implemented yet; the present registration follows the endpoint selected at startup. Power feedback from M2 is retained.

**File Shelf:** `src/core/file_shelf.hpp` owns at most five in-memory file/folder paths. `src/win32/file_drop_target.hpp` implements OLE `IDropTarget` for `CF_HDROP`; initialization and `RegisterDragDrop` occur on the UI STA with a normal message pump, with paired unregister and COM teardown. A valid drop adds path references only, never copies/reads/upload files. Expanded Reveal opens Explorer selection via shell PIDLs without executing the dropped file. Clear removes references. **Drag-out and per-item removal are not yet supported.** These are material limits, not completed functionality.

**Shared presentation:** a user may explicitly select timer or shelf, pinning it in Expanded while unrelated higher-priority events do not steal focus. Timer completion can dismiss its own Expanded surface and show terminal feedback. Simulated legacy scenarios are no longer in the Release context menu, avoiding user-facing debug clutter.

**Validation gate:** recheck Windows x64 build and Linux portable tests at latest head; manual Windows 11 testing still required for OLE drag/drop on the alpha-layered HWND, cursor hit-testing, timer after sleep, audio callback delivery and default device switching, native UIA control actions, 100–200% display scaling, file path error conditions, and idle CPU/memory. No claim of a production-ready file transfer or volume system.

**Risk:** `OleInitialize` requires a compatible UI apartment; the media MTA worker remains isolated. RegisterDragDrop failure must not crash the shell. The CPU-layered renderer still needs compositor comparison before committing to a final Windows rendering stack.


## M4-A runtime hardening record — 24 October 2026

This is a **scoped reliability slice**, not a completed M4 or release approval.

- **Fullscreen/foreground graph:** SetWinEventHook(EVENT_SYSTEM_FOREGROUND) posts a UI-thread notification. The UI owner compares the current foreground window bounds with the selected *physical monitor* bounds and suppresses only genuine monitor-covering applications, not ordinary maximized windows which stop at the taskbar. On suppression the activity model remains authoritative; only presentation is hidden. Foreground activation is not taken back when the window hides. Source-independent coverage math is verified in portable tests.
- **Monitor/DPI recovery:** invalid HMONITOR is replaced using a live monitor; layered surface dimensions derive from the selected monitor effective DPI via GetDpiForMonitor. A manual tray move invalidates the previous DPI cache.
- **Audio lifecycle:** IMMNotificationClient observes default multimedia render endpoint changes and posts to HWND; the UI STA rebinds IAudioEndpointVolume, unsubscribing from the old device. Audio callbacks never access the drawing or activity model directly. Rebinding also resets previous volume/mute baseline to avoid spurious notifications.
- **Wake/shell recovery:** Focus deadline checked immediately on sleep/resume; Explorer's TaskbarCreated notification causes tray icon reconstruction.
- **Proof infrastructure:** scripts/measure-runtime.ps1 reports measured process CPU, private bytes, working set and handle count. These are not reported as achieved metrics without a documented Windows device sample. GPU/frame pacing requires ETW Windows Performance Recorder/Analyzer.

**Evidence status:** compilation/unit tests are verified by GitHub CI on the M4 branch. Correct behavior across true fullscreen apps, hardware audio output hot-swap, multi-DPI monitor disconnect, Explorer restart and sleep must still be accepted on a Windows device. The existing GDI+ layered renderer is an experimental baseline only. Full UIA child controls, compositor comparison (ADR-001), installer/updater and performance gates are **still open**.

Important interface contracts retained: v3 Floating and Top-attached geometry, Ambient clock/lens/power, coordinator-controlled activity presentation and non-activating passive feedback. None of these require a new cloud process, plugin host or developer-focused surface.
