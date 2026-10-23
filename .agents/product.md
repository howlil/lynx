# LYNX — Product Contract

Status: research synthesis / agreed direction, 24 October 2026
Stage: pre-implementation; next is M0 native feasibility
Audience: general Windows 11 desktop users
Design reference: ../DESIGN.md

## 1. Problem and job

**Actor:** a person using a Windows desktop who needs occasional awareness of time, battery/power, and an ongoing background activity while working in another application.

**Trigger:** using the computer, starting an activity (e.g. media or timer), or receiving a meaningful short-lived system event.

**Current friction:** relevant information and small controls are spread across the taskbar, Quick Settings, and application windows; checking status may require context switching. An overly active overlay can instead introduce distractions, blocked clicks, or focus theft.

**Job to be done:** while using Windows, glance at essential ambient information and complete a small relevant action without losing focus in the primary task.

**Desired observable outcome:** LYNX gives a quiet persistent glance surface, promotes meaningful activities when needed, supports intentional direct action, and safely returns the user to their previous context.

No claim has yet been established about market size, user retention or quantitative productivity improvement. Those require user research.

## 2. Agreed product boundaries

**Core first, not a feature marketplace.** LYNX starts as a general-user Island shell, using Apple-inspired presentation principles but built for Windows.

Agreed v3:
- Two selectable appearances: **Floating** and **Top-attached with concave shoulders**.
- **Persistent Ambient** by default: local clock (left), quiet center lens, valid battery/power information (right). Optional center dot/weekday and optional auto-hide policy may be configured later.
- One primary activity in Compact; a secondary Minimal indicator when there is a meaningful second activity.
- Expanded gives direct contextual controls. Short system events use Transient and then restore prior context.
- Smooth, interruptible animation; reduced motion is a must; no continuous decorative idle motion.
- Fullscreen can hide the visual overlay without cancelling activity.
- Shell/theme/motion/placement are shared across any future modules.

### Core scope (M0–M2)

- Native Windows overlay surface and placement.
- Ambient and valid system clock / battery fallback.
- Presentation states: Ambient, Compact, Minimal, Expanded, Transient, Hidden.
- Mouse/keyboard invocation, dismissal, focus-preserving notification behavior.
- Presentation arbitration, recovery to previous activity, no unintended takeover.
- Tray access and minimal settings for placement, visibility, motion and quit.
- DPI/monitor/fullscreen, accessibility and error/unavailable presentation.
- **Simulated activities during native feasibility**; real data integration follows once the shell is proven.

### First integration and extensions

- **First provider after core:** OS media activity, to prove that the generic activity contract can work with a real Windows source.
- **General edition after core:** timer/stopwatch, system HUD, file shelf; clipboard and notifications need additional privacy/permission treatment.
- **Future extension types:** productivity, calendar/meeting, developer agent and other modules. The core must be extendable without showing these by default.

### Explicit non-goals for initial core

- Full notification center, mini browser, permanent weather/dashboard, stock ticker, always-on CPU charts.
- Developer-specific UI, AI agent approval or deployment workflows in the default product.
- Multi-process backend, cloud accounts, third-party plugin runtime or persistent activity database.
- System injection, taskbar replacement, global input capture, administrator requirements.
- Pixel-perfect or binary replication of Apple's proprietary implementation.

## 3. Core behavior graph

    App starts
      -> read valid preferences and environment
      -> Ambient (clock / lens / battery or honest fallback)
         -> User opens -> Expanded overview -> close -> Ambient
         -> Activity appears -> Compact primary
             -> User opens -> Expanded activity -> action -> result
             -> User closes -> Compact primary
             -> Second activity -> Compact primary + Minimal secondary
         -> Transient event -> brief feedback -> restore previous view
         -> Fullscreen -> Hidden -> restore previous scene on exit
         -> No relevant activity -> Ambient

**Interaction and attention rules**

1. Persistent Ambient is default; auto-hide is a user preference, not baseline.
2. A passive event cannot steal focus or forcibly replace an expanded interaction.
3. One primary Compact activity is visible; a second gets Minimal only if useful.
4. Selection/order is deterministic and must not oscillate as frequent events arrive.
5. A transient event ends or expires and restores the valid previous state; do not restore a terminated activity.
6. User dismissal is respected; an identical stale callback cannot immediately reopen a dismissed activity.
7. Hidden means hidden UI, not cancelled work. User must be able to recover via shortcut/tray.
8. Click-to-expand is the baseline; hover-expand is optional, off by default.
9. Reduced Motion changes animation only; it does not remove an action or state.

## 4. Material broken paths and recovery

| Condition | Required observable behavior |
| --- | --- |
| No battery (desktop PC) | Do not fabricate percentage; preserve clock/lens with honest trailing fallback or omit trailing data |
| Power/media API unavailable | Ambient and other capabilities remain usable; unavailable controls are not presented as functional |
| Clock locale/timezone changes | Show correct system-local time, not Jakarta demo time |
| Activity finishes during expansion | Do not render stale interactive controls; resolve to valid terminal state then restore |
| Multiple simultaneous events | No flicker or unbounded stack of expanding notifications |
| Transient arrives during active keyboard interaction | Defer/suppress normal feedback instead of hijacking focus |
| Two activities exist | Primary and optional secondary are distinguishable and accessible |
| Fullscreen app | Hide according to policy; restore safely, without grabbing focus |
| Display/DPI/monitor changes | Keep Island within usable monitor bounds |
| Reduced Motion / high contrast | Preserve discoverability, text, actions and status |
| Missing OS permission/capability | Clear unavailable state or hide irrelevant action; no false success |
| App restarts | Load stable preferences; no duplicate instances or stranded overlay |

## 5. Constraints and assumptions

### Confirmed constraints/preferences

- Windows 11 desktop target, native GUI, low CPU/memory overhead, no Electron/WebView core.
- Apple-inspired visual language, black capsule, semantic accent, compact presentation.
- Two placements share one core.
- Idle clock and power are persistent by design.
- First build prioritizes core and general users; no developer-specific features yet.

### Assumptions requiring proof

- A native Windows renderer can reproduce the animated concave shoulder silhouette without input/alpha artifacts.
- Fullscreen and multi-DPI handling can remain reliable across supported machines.
- Battery information is available on some devices but not on all desktop PCs; fallback design must be usability-tested.
- End users prefer persistent Ambient with a quiet center lens; validate through usability tests.
- The chosen activation and dismissal behavior won't disrupt keyboard-heavy workflows.

No arbitrary user-volume or retention targets are asserted.

## 6. Acceptance traces (product proof)

| ID | Given / When | Then (observable) |
| --- | --- | --- |
| P01 | Start with no active activity | Ambient shows accurate local time and valid power data/fallback |
| P02 | Select Floating / Top-attached | Content and actions stay equivalent; only placement/geometry change |
| P03 | Start media in a supported source | Compact reports current activity, expandable to supported controls |
| P04 | Expand by click/shortcut, then Escape | Detail is reachable; close restores appropriate compact/ambient and focus |
| P05 | Receive ordinary charging feedback while interacting | Feedback does not steal focus or invalidate current action |
| P06 | Transient expires | Prior valid activity or Ambient is restored |
| P07 | Two activities are active | Deterministic primary + clear secondary without duplicate Island |
| P08 | OS reports no battery | UI does not display a fabricated percentage |
| P09 | App becomes fullscreen / exits | Island hides/restores per policy without cancelling activity |
| P10 | Turn on Reduced Motion | All actions/states remain operable without spatial animation |
| P11 | Media provider fails/stops | Overlay remains responsive; unavailable controls are not actionable |
| P12 | Activity ends while another event arrives | No stale control, flicker, or incorrect restoration |

These define intended outcomes, **not passed tests**. The HTML prototype exercises parts of the interaction graph only, not native OS integration.

## 7. Decisions vs open questions

**Agreed:** persistent Ambient with clock/lens/battery, both placements, compact Apple-inspired visuals, user-initiated expansion, smooth/reduced motion, general first.

**Still requires usability/engineering validation:** desktop-without-battery fallback wording, source priority when several real providers compete, thresholds for promoting events, whether an optional auto-hide mode should be exposed in first Settings, and actual platform/accessibility behavior.

## 8. Handoff

- **Design expression:** ../DESIGN.md
- **Engineering responsibilities and feasibility:** engineering-design.md
- **Repository workflow/routing:** ../AGENTS.md

Do not add a capability to initial scope unless it closes a product job/acceptance path. Extend the activity contract for future features without changing the shell's basic behavior.
