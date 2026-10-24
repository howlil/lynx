# M5 — HTML v3 native parity: design graph and implementation contract

Source of truth: ../design/reference/windows-island-apple-ambient-v3.html. Native implementation is a Win32 projection of this browser interaction prototype. Mock/test harness controls in HTML do not imply production users can change physical battery or fabricate system media sessions.

## 1. Design Graph: surfaces C / V / N

| Surface | Primary content / actions | Rendered variants | Entry condition |
|--|--|--|--|
| Ambient floating or attached | local clock / center indicator / battery / open | Lens, Dot, Day; unknown battery | no selected activity |
| Compact Media | art mark, title, playback waveform, open | playing / paused | available GSMTC source |
| Compact Timer | timer icon / remaining count / open | running / paused | active Focus Timer |
| Compact Shelf | filename / open | 1–5 file paths | shelf active |
| Secondary activity bubble | second highest-priority source, select | media or timer | coordinator.secondary exists |
| Expanded Media | art, title/artist, timeline, Previous/Play/Next | capability-aware disabled controls; missing timeline | selected media session |
| Expanded Timer | circular progress, remaining, Reset/Pause/Resume | paused / running | selected Focus Timer |
| Expanded Idle | large clock, day/battery, empty state, Start Focus | real clock/power | no selected activity |
| Expanded Shelf | file info, Reveal / Clear | invalid path handled by Windows | selected shelf |
| Transient/Expanded alert | charging / low battery / focus complete, dismiss | compact → detail → expire | Windows power/timer event |
| Fullscreen | hidden presentation, active state preserved | hide / restore | foreground covers target monitor |

Transitions:

    OS/media/focus observation → coordinator
      → Ambient / Compact / Transient / Hidden
      → click → Expanded
      → select action → OS operation or focus timer
      → OS snapshot → update same UI.

    Secondary bubble → selected activity Expanded.
    Outside foreground click or Escape → collapse; restore legitimate focus.
    Adaptive idle → small handle → mouse preview → Ambient.
    User tray → choose idle/center/motion → persist HKCU.

## 2. Geometry contract

Native size is in DIPs before monitor scale. The independent C++ contract is in src/core/v3_presentation.hpp; the v3 GDI+ renderer and native UIA/WM_LBUTTONUP use the same button boxes.

| View | Floating | Top-attached |
|--|--|--|
| Ambient | 232 × 42, +13 top | 264 × 42, flush top |
| Compact | 182 × 38 | 214 × 38 |
| Media Expanded | 330 × 155 | 362 × 155 |
| Timer Expanded | 330 × 167 | 362 × 167 |
| Idle Expanded | 330 × 154 | 362 × 154 |
| Shelf Expanded | 330 × 155 | 362 × 155 |
| Alert Expanded | 330 × 103 | 362 × 103 |
| Adaptive handle | 56 × 9 | 96 × 12 |

Expanded radius 27 DIP. Presets: Smooth spring 440/40, Snappy 830/51, Reduced snap. Dynamic height belongs to scene geometry, not one global Expanded size. Ambient Center Lens/Dot/Day is a presentation variant, not a domain activity.

## 3. Design Thinking: responsibility and execution graph

Shapes:
- Scene = Idle / Media / Timer / Shelf / Alert
- UserPreferences = idle policy, center mode, motion preset
- Layout = geometry, action hit boxes, secondary bubble box
- MediaSnapshot = OS playback state, previous/toggle/next capabilities and optional time range

Flow:

    OS callback on WinRT MTA / HWND power or timer
      → normalize immutable OS data
      → publish to HWND owner
      → reconcile ActivityCoordinator
      → select surface & layout from pure v3 contract
      → retarget spring
      → paint GDI+ and per-pixel-alpha layered HWND
      → update UIA bounds from same action layout.

Control flow:

    pointer / Tab + Enter / UIA Invoke
      → action identity and shared box
      → validate current scene, activity generation, capability
      → media command / Focus Timer / file action
      → next observable state.

Failures:
- GSMTC previous/next/toggle disabled by Windows ⇒ controls visible but disabled, no fake command success.
- GSMTC timeline missing ⇒ show --:-- and no fabricated progress.
- No real album thumbnail ⇒ illustrative album placeholder from the HTML design is used.
- No battery ⇒ display unknown/AC rather than mock 82%.
- The HTML Try music CTA requires simulated tracks; in Windows Idle this is replaced by a functioning Start Focus CTA.
- Adaptive floating idle has a visible tiny native handle rather than a fully invisible browser hotspot to keep native pointer access possible.
- Warnings are UI events, do not steal desktop focus until explicitly opened.

Resource scope:
- HWND owns animation springs, GDI+, timers, command routing, accessibility nodes and tray.
- Media MTA owns GSMTC, posts immutable snapshots only.
- Controls are laid out once in the contract; UIA and pointer share exact bounds.
- Idle and expanded rendering are clipped to the alpha capsule, not the desktop rectangle.

## 4. Proof boundary

Automated: Windows x64 native build; Linux portable unit tests for the numeric geometry matrix, action availability, hitbox containment, timeline and focus ratio math.

Manual Windows 11 acceptance: pixel comparisons at 100/125/150/200% DPI for Ambient/Compact/Expanded Media/Timer/Idle/Shelf/Alert, concave shoulder fidelity, native text metrics, shadow and motion, media control integration, secondary bubble clickability, Narrrator/Inspect, battery transitions, fullscreen and outside-click behavior. Test click-through against Explorer/browser, monitor swap and sleep. Record on-device CPU/GPU and frame-pacing traces.

No claim of pixel-perfect screenshot parity, working real album thumbnails, full invisible-hotspot parity, or completed production acceptance until manual checks pass.
