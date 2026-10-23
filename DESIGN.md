# LYNX — Design System

Status: agreed visual baseline (Island Core v3, 24 October 2026)
Scope: Windows 11 native Dynamic Island shell; general users first

## 1. Purpose and source of truth

LYNX is a compact, interactive Windows desktop activity surface. Its design must let users glance at ambient information and finish small actions without leaving their primary application.

This document owns **appearance and interaction design**. Product requirements and priorities live in **.agents/product.md**; runtime architecture and unresolved implementation decisions live in **.agents/engineering-design.md**; contributor/agent workflow lives in **AGENTS.md**.

The agreed baseline is the **Island Core v3 — Ambient & Curved Notch** prototype. This document describes it self-sufficiently so implementation does not depend on availability of a prototype file.

## 2. Design principles

1. **One island, not a dashboard.** The shell should stay compact and surface one primary activity at a time.
2. **Ambient is useful but quiet.** At rest it shows local clock, subdued center lens, and valid battery/power information.
3. **One continuous shape.** Compact and expanded content should feel attached to the same black surface; avoid nested cards.
4. **Continuity over spectacle.** Preserve visual identity and relative placement when morphing; do not loop decorative animations at idle.
5. **Attention is earned.** Passive system events never steal keyboard focus or forcibly expand an in-progress interaction.
6. **Native-feeling and accessible.** Clear typography, efficient layout, DPI awareness, keyboard access, reduced motion, high contrast, and sensible focus restoration.
7. **Apple-inspired, not Apple-owned.** Follow human interface principles, not a claim of Apple-private geometry, official token values, or licensed Apple system assets.

## 3. Core experience and surfaces

### Ambient (default idle, persistently visible)

- **Leading:** local 24-hour or OS-preferred time (e.g. 09:41), with tabular figures; update at minute boundaries and after clock/timezone changes.
- **Center:** non-animated, low-contrast lens / optical recess; intentional negative space. Optional alternatives: a restrained status dot or weekday text. The **lens is the agreed default**.
- **Trailing:** battery percent and battery icon. Neutral when discharging, green when charging, amber when low. Never fabricate a value when Windows reports no battery or unavailable data.
- **Battery-less desktop fallback:** retain the clock and center; trailing slot may be omitted or show an explicitly supported relevant power status. Final fallback copy is an implementation validation item.
- Whole visible capsule is discoverable as an entry point to an overview; keyboard shortcut/tray provide alternate access.

### Activity presentations

- **Compact:** leading identity (artwork/symbol), trailing relevant status (time, playback or progress), no unrelated information.
- **Minimal secondary:** restrained companion bubble for a second ongoing activity where meaningful; do not create multiple full-sized islands.
- **Expanded:** detail and direct actions for the selected activity; controls must be reachable and have meaningful enabled/disabled states.
- **Transient:** charging, completion, or similar brief feedback; restore prior primary activity or Ambient afterwards.
- **Hidden:** fullscreen/presentation policy hides the surface but does not automatically cancel underlying activity.

Default interaction: **click to expand**, Escape/collapse/outside dismissal to close. Hover expansion is not enabled by default to avoid accidental activation.

### Multiple activities

1. Preserve the currently interacted-with expanded activity unless an exceptional high-priority event explicitly requires interruption.
2. Choose one primary compact activity deterministically.
3. Show a secondary minimal indicator only when it communicates meaningful concurrent progress.
4. A transient event may temporarily replace compact presentation, then restore the previous state.
5. System feedback must not take keyboard focus.

## 4. Placement geometry

All sizes are **design targets in device-independent pixels (DIP)**, not promises about physical pixels. Respect DPI, monitor edges and safe margins.

| Property | Floating | Top-attached |
| --- | --- | --- |
| Top offset | 13 DIP | 0 DIP |
| Ambient width | 232 DIP | 264 DIP, including shoulders |
| Ambient height | 42 DIP | 42 DIP |
| Typical corner radius | 22 DIP | Bottom radius about 22 DIP |
| Shoulder geometry | None | ~16 DIP width and ~16 DIP depth each |
| Expansion | Symmetric growth around anchor | Grows downward from top screen edge |
| Expanded reference | ~330 DIP body width | ~330 DIP body / ~362 DIP total frame |

**Floating:** detached fully-rounded black capsule.

**Top-attached:** one continuous sculpted silhouette flush with the top edge. Two **concave shoulders** transition from the top edge into a downward capsule with rounded convex lower corners. The shoulders must be dynamically recomputed with width/height; a rectangle with only bottom border-radius is insufficient. Avoid seams at the top edge and sharp shoulder corners.

The two placements share content, activity contracts, motion semantics, and interaction behavior. They are **placement strategies**, not separate applications.

## 5. Appearance tokens

Custom LYNX tokens based on Apple-inspired dark semantics; these HEX values are **not** official Apple internal tokens.

| Semantic token | Baseline |
| --- | --- |
| surface.primary | #000000 |
| text.primary | #F5F5F7 |
| text.secondary | #A1A1A6 |
| accent.primary | #0A84FF |
| status.success | #30D158 |
| status.warning | #FF9F0A |
| status.error | #FF453A |

Apply color to states and meaningful controls, never as decorative gradients. In Ambient, use neutral white/gray; charging and low-battery states may use semantic color. Use an opaque black shell by default for legibility and economical rendering. Minimize shadows/glow; prefer clean silhouette and spacing.

Typography: **Segoe UI Variable** / Windows fallback in native implementation. Do not bundle Apple SF Pro or other licensed font files without permission. Icons: compact vector outlines with a coherent weight and accessible labels; Fluent System Icons are suitable native alternatives.

## 6. Motion contract

Motion must convey a continuous surface, not a popup replacement:

- **Ambient → Compact:** discreet content change, no exaggerated zoom.
- **Compact → Expanded:** interruptible, spring-like width/height/shoulder/radius morph, with content reveal once space is available.
- **Expanded → Compact:** content withdraws before or in coordination with contraction; no clipping flashes.
- **Transient feedback:** brief entrance/exit, return to previous activity.
- **Multi-activity:** secondary indicator appears without moving active controls unexpectedly.
- **Idle:** no breathing, pulsing, live waveform or continuous compositor work just for decoration.
- **Reduced Motion:** switch instantly or with minimal non-spatial fade; never remove actions or status.

Prototype reference spring presets (dimensionless prototype tuning, **not directly portable as Windows API physical constants**):

| Preset | Stiffness | Damping |
| --- | ---: | ---: |
| Smooth (default) | 440 | 40 |
| Snappy (optional) | 830 | 51 |

Final native tuning and frame pacing are engineering verification tasks, not claimed achieved results. Motion must be interruptible and preserve the current visual position/velocity as supported by the chosen compositor.

## 7. Input, accessibility, environments

- Compact and passive notifications must not steal foreground keyboard focus.
- Expanded interactive controls must be keyboard-operable and exposed through Windows UI Automation.
- Escape must close an expanded surface and restore valid focus.
- Interactive affordances need accessible names and state change feedback.
- Adapt to reduced-motion/high-contrast settings without violating the core job.
- Do not block clicks outside the visible geometry.
- Handle display scaling, multiple monitors, taskbar overlap, lock/unlock, sleep/resume, and fullscreen policies.
- Avoid over-designing for a hypothetical mobile form factor; LYNX targets Windows desktop.

## 8. Visual acceptance

- [ ] Ambient shows correct clock and valid power data/fallback.
- [ ] Center lens stays quiet while idle.
- [ ] Floating and attached feel like variants of the **same** surface.
- [ ] Attached concave shoulders have no visible seams/pixel gaps across supported DPI scales.
- [ ] Compact → expanded preserves identity and spatial continuity.
- [ ] Background interaction outside the surface passes through.
- [ ] Keyboard operation, focus restoration and reduced motion work.
- [ ] No animation or continuous redraw occurs just because Ambient is visible.

## 9. References

- Apple HIG, Live Activities: https://developer.apple.com/design/human-interface-guidelines/live-activities
- Apple HIG, Color: https://developer.apple.com/design/human-interface-guidelines/color
- Apple HIG, Motion: https://developer.apple.com/design/human-interface-guidelines/motion
- Windows Composition overview: https://learn.microsoft.com/en-us/windows/uwp/composition/using-the-visual-layer-with-win32

Prototype visual verification completed only in a browser; **Windows native UI, resource/performance, focus, compositor, and hit-test behavior remain unverified**.
