# LYNX — Agent & Contributor Guidelines

Applies to the entire repository. This file owns **general working rules and skill routing only**.

## Source-of-truth map

| File | Owns |
| --- | --- |
| DESIGN.md | Agreed visual language, geometry, motion, states and interaction guidance |
| .agents/product.md | User job, product behavior, MVP scope, assumptions and acceptance proof |
| .agents/engineering-design.md | System responsibilities, state ownership, platform boundaries, architectural decisions and unresolved engineering risks |
| AGENTS.md | Workflow, routing and engineering guardrails (this file) |

Do not copy the same contract into multiple files. Cross-reference the relevant owner. If an implementation reveals a contradiction, update the owning document and explicitly identify the decision that changed.

## Working principles

1. **User instructions take precedence.** Follow explicit task scope and named skill invocations.
2. **Read before changing.** Inspect the relevant source files and design/product/system contracts before modifications. This repo may evolve after the initial documentation-only state.
3. **Work in small verifiable slices.** Understand → model consequential behavior → implement → verify actual risk → review diff → report.
4. **No speculative features.** For the initial release, preserve the general-user core. Developer/productivity extensions are future capabilities, not default UI.
5. **No architecture theater.** Prefer one native process, clear ownership and existing mechanisms unless evidence justifies further abstractions or processes.
6. **No unsupported claims.** Distinguish designed/prototyped, compiled, tested on Windows, and released. HTML prototype success is not proof of native behavior.
7. **Never quietly change agreed visual decisions.** DESIGN.md v3 is the baseline; discuss consequential changes rather than substituting arbitrary aesthetics.
8. **Prioritize input correctness and resource efficiency.** No invisible click-blocking overlay, unnecessary privilege escalation, continuous idle render loops or unrequested network telemetry.
9. **Respect accessibility.** Keyboard operation, focus handling, UI Automation, high contrast and reduced motion are functional requirements.
10. **Document only durable decisions.** Prefer tests and precise diffs over repetitive status documents.

## Skill router

Use the user's named saved skill when explicitly invoked. Spelling variations are understood when unambiguous. Skills are procedures, not ordinary labels; retrieve their actual source if available, and never invent a missing specification.

**Default SDLC routing:** use **$agentflow** to choose the smallest useful specialist set when orchestrating substantial coding tasks. Use the user's canonical RTK.md for the router if accessible; this repository does not duplicate or override that external skill source.

| Skill | Invoke when decision concerns |
| --- | --- |
| $product-design | User problem, job, scope, outcome or acceptance behavior |
| $engineering-design | Architecture, boundaries, state ownership, contracts, migration, OS integration or material guarantees |
| $design-graph | Screens/surfaces, interaction graph, state variants, focus, keyboard and responsive presentation |
| $design-thinking | Computational execution, types/shapes, errors, runtime dependencies and implementation plan |
| $test-engineering | Nontrivial proof, races, async/OS behavior, recovery and test reliability |
| $code-review | Correctness, regressions, maintainability and adherence to contracts |
| $security-review | Privilege, input authority, OS boundaries, sensitive data or untrusted integrations |
| $production-ops | Packaging, startup, recoverability, telemetry policy, crash diagnosis and operations |
| $graph-protocol | Delegation/parallel work when independence justifies coordination cost |
| $call-graph-output | Rendering an established execution/responsibility graph when it improves clarity |

Avoid invoking every skill mechanically. Simple local fixes do not require a full design exercise. A complex risk can still be handled by one worker. Explicit user invocation must be honored when relevant.

## LYNX-specific engineering guardrails

- Target: Windows 11 native desktop utility; no Electron or browser-dependent core.
- First milestone: M0 native feasibility proof. Do **not** mistake the compositor choice for a settled decision.
- Preserve two placements: Floating and Top-attached, sharing one activity/presentation model.
- Ensure Native Window, Rendering, Composition, UI State, Activity and Platform Adapters have clear responsibilities; refine component boundaries only when implementation evidence justifies them.
- Do not couple provider data directly to drawing functions. The UI receives coherent presentation snapshots.
- Serialized presentation updates are required; asynchronous OS callbacks cannot mutate the visual tree from arbitrary threads.
- Activity updates should be event-driven. No per-frame polling or redraw at rest.
- Handle unsupported battery information and absent media sessions honestly.
- Use the least permissions needed; never inject into Explorer or modify OS internals by default.
- Consider monitor/DPI changes, fullscreen, sleep/resume, device loss, and process restart in the relevant slice.

## Verification and reporting

Before claiming a milestone completed, name the exact checks actually performed and their outcomes. Distinguish:

- Static/design review.
- Native Windows compilation.
- Functional interaction testing on Windows.
- Profiling (CPU, working set, frame pacing, GPU).
- Accessibility and platform edge-case tests.

If no Windows runtime is available, say so. Keep unknowns and failed checks visible; do not mark them passed based on browser mocks. Compare the implemented graph with .agents/product.md, .agents/engineering-design.md and DESIGN.md and report deviations.

## Repository conventions

- All product/design decisions are documented in the existing source-of-truth files above.
- Add ADRs only for expensive-to-reverse cross-cutting decisions (e.g., compositor or activation model), and link them from engineering design.
- Prefer explicit, reversible commits; do not overwrite existing work without inspecting it.
- Do not add dependencies, plugins, accounts, services, storage or permissions solely because a template suggests them.
