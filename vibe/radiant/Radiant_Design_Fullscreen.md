# Radiant Fullscreen

**Date:** 2026-10-14

**Status:** window fullscreen implemented; validation is recorded in Appendix A.
Element fullscreen is a separate follow-on stage.

**Scope:** native presentation for Lambda/Radiant's viewer and editor, shared
by Lambda DOM operations and the future JavaScript Fullscreen API.

**Specification linkage:** D7.5.3 and D7.3.3 govern the DOM catalog/provider
boundary; D7.1.4v2 governs windowless and null-backend profiles; D4.5.2 and
D5.3.3 govern retained values and precise roots; S12.1.1v2 and S12.1.3 govern
procedural mutations and pure reactive rendering. See [Formal Design](../../doc/Lambda_Formal_Design.md)
and [Formal Semantics](../../doc/Lambda_Formal_Semantics.md). This adds a Radiant
capability within those contracts; it changes no existing formal ruling.

## 1. Two presentation layers

| Stage | Behavior | Entry points |
|---|---|---|
| 1 — window | The complete application occupies one monitor | `view`/`edit --fullscreen`, F11, Lambda DOM operations |
| 2 — element | A selected element occupies the presentation viewport | Lambda DOM operations and the standard JS Fullscreen API |

Window fullscreen intent and document fullscreen state have separate owners.
F11 changes window mode without assigning `document.fullscreenElement`.
An element session records the prior shell mode and restores it on completion.
Window fullscreen survives document navigation; element fullscreen ends on
document unload. Stage 1 does not advertise element fullscreen support.

## 2. Window controller

`UiContext` owns requested and actual mode, pending request, saved normal
window bounds, maximized state, and pending notifications. Normal bounds are
tracked while windowed, so a maximized window retains its original restore
rectangle. No script value or document pointer is retained (D4.5.2).

Native transitions use the existing GLFW window and graphics context. Select
the monitor with the greatest overlap with the window, falling back to the
primary monitor. Request its current resolution and refresh rate; do not add
a display-resolution setting. On exit, restore saved bounds and maximization.
If the old monitor is gone, place the restore rectangle within a remaining
monitor's work area. GLFW restores decoration and resize attributes on exit.

Reference: [GLFW window guide](https://www.glfw.org/docs/latest/window_guide.html#window_monitor).

Requests commit on the UI thread after callbacks return. Repeated requests
coalesce; the last requested mode wins. Requesting the current mode succeeds
without another change event. Read back native state after applying a
transition: failed entry must not publish fullscreen=true. Monitor removal
reconciles actual state with GLFW without retaining dead monitor pointers.
Loss of focus alone is not treated as leaving fullscreen.

The controller has a small internal platform interface so tests can exercise
coalescing, restoration, failure, and external exits without a physical display.
Production uses GLFW; tests explicitly supply their platform implementation.

## 3. Lambda API and notifications

Append operations to `lambda/dom/dom_api.def`, preserving published ordinals:

```lambda
dom.set_window_fullscreen(node, true)
dom.set_window_fullscreen(node, false)
dom.window_fullscreen(node)
dom.window_fullscreen_enabled(node)
```

`node` identifies the presented top-level document, as for window title and
close operations. Detached, stale, embedded, and non-presented documents are
refused. The setter is `pn` and returns whether a request was accepted; this
does not guarantee the subsequent platform transition succeeds. Queries read
published host state using the existing DOM query convention. Templates
receive notifications in handlers and render their own state (S12.1.3).

`windowfullscreenchange` dispatches after an actual-mode change and current
viewport metrics are published. `windowfullscreenerror` reports failure of an
accepted transition. Both use existing window notifications and
`dom.watch_window`; handlers can query actual mode. Logs use `fullscreen:`.

The path is DOM catalog → neutral engine seam → Radiant module provider →
native controller. No lambda-rt → Radiant dependency is introduced (D7.5.3,
D7.3.3).

## 4. Input and launch behavior

- `lambda view document --fullscreen` and `lambda edit document --fullscreen`
  enter fullscreen before initial layout.
- F11 toggles the latest requested mode. Key repeat does not toggle.
- Escape exits fullscreen and releases relative mouse. Consume that press so
  it cannot also close the viewer or dismiss an editor action.
- Already-windowed Escape preserves existing viewer/editor behavior.
- Native callbacks and event simulation use the same fullscreen key gate.
- Headless execution reports unavailable. Combining `--headless` and
  `--fullscreen` is a CLI error, even with a hidden graphics window.
  New GLFW symbols have null implementations in `radiant/headless.cpp`
  (D7.1.4v2).

GLFW fullscreen is the initial portable mode. Native macOS Spaces integration
would be a separate adapter under the same controller contract.

## 5. Geometry, rendering and lifecycle

Reuse render-time metric publication: query logical window and physical
framebuffer sizes in the same turn, update device scale, recreate the surface
as needed, and reflow only on CSS viewport changes. Fullscreen does not
duplicate layout or resize dispatch. Skip zero-sized framebuffers while
minimized. Saved geometry is float; discrete GLFW conversions stay at the
native boundary with dimension-lint annotations.

Position, size, and maximization callbacks maintain restore bounds without
running layout. Transition callbacks cannot overwrite saved windowed bounds.
Pending requests and callbacks end with the owning UI context.

## 6. Element fullscreen follow-on

Add `Element.requestFullscreen()`, `Document.exitFullscreen()`,
`Document.fullscreenElement`, `Document.fullscreenEnabled`, `fullscreenchange`,
and `fullscreenerror`, with Lambda equivalents in the shared DOM catalog.
JS methods return Promises with specified settlement and event ordering.

This needs a document-owned fullscreen stack and reusable top-layer layout,
painting, and hit testing. DOM parentage stays intact. Implement real
`:fullscreen` matching, `::backdrop`, and viewport-based UA styles. Coordinate
top-layer ordering with modal dialogs and popovers.

Implement transient user activation, connected/active-document checks,
embedded permissions policy, removal/unload cleanup, and the specified
Promise/event lifecycle before claiming web compatibility. Retained nodes use
generation-checked `DomNodeRef`; retained Promises use registered precise roots
(D4.5.2, D5.3.3).

Reference: [WHATWG Fullscreen API](https://fullscreen.spec.whatwg.org/).

## 7. Validation and delivery

Stage 1 gates:

- Controller tests: idempotence, coalescing, normal/maximized restore, failure,
  external exit, monitor choice, and restore geometry.
- Lambda/DOM integration: catalog publication, unavailable results, invalid
  document handles, and shared key handling.
- Native checks on macOS/Linux/Windows: real entry/exit, Escape, startup,
  mixed-DPI displays, monitor removal, and focus loss.
- Lambda/Radiant baselines, null-backend linking, and
  `make lint ARGS='--rule ^no-int-cast-radiant$'`.

Stage 2 adds top-layer/hit-testing regressions, activation rejection,
Promise/event ordering, removal/navigation cleanup, and applicable fullscreen
WPT tests. Record automated results and unavailable native checks below;
headless tests do not establish physical display behavior.

## Appendix A — Implementation and validation (2026-10-14)

Window fullscreen is implemented in `radiant/window_fullscreen.{hpp,cpp}`
(controller and platform contract) and `radiant/window.cpp` (GLFW integration,
launch, input, and notification publication). `UiContext` owns its state.
The three catalog operations use the existing engine seam and Radiant module
provider; launch options reach viewer, editor, graph/directory transforms,
and paged preview. The null backend supplies the added GLFW entry points.

The native replay keeps its accepted request and observed mode in template
state (S12.1.5v2), so viewport regeneration preserves them. Reflow may
regenerate authored output; adding a transient DOM attribute before the mode
switch is not a reliable way to retain application status across that action.

Automated checks:

- The Radiant text-query/native runner passes 20/20 tests, including 14
  fullscreen controller, geometry, key, and Lambda API checks.
- Native macOS entry/exit (12 assertions) and startup (5 assertions) replays
  pass; the entry/exit replay also passes all 12 assertions under
  `LAMBDA_EXEC_BACKEND=jit`. Their commands are below.
  Desktop access is required: the sandboxed attempt timed out in macOS window
  services. The replay also checks committed viewport geometry and restoration.
- The added null-backend symbols compile, link, and return unavailable/no-op
  results in a windowless smoke check.
- `make lint ARGS='--rule ^no-int-cast-radiant$'` passes.
- `make test-lambda-baseline`: 6,564/6,566 pass. The forced-GC sweep timed out
  at `regression_music_layout`; `edit_view_only` has a Markdown math round-trip
  mismatch, reproduced in isolation. Its golden and harness are unchanged.
- `make test-radiant-baseline` with desktop access: 5,477 pass, 351 partial,
  one skipped, and five failures. The remaining failures are the Markdown
  `md_nuxt-readme` layout baseline and four editor fixtures: `doc_editor_dct_image_pdf`,
  `doc_editor_eml_html_preview`, `doc_editor_eml_preview`, and
  `doc_editor_tsv_table`. All 190 Radiant View cases, all 28 Scene3D cases,
  and the page snapshots pass. The layout failure and all four editor
  failures reproduce in isolated reruns. No baseline expectations were changed.

```sh
./test/test_radiant_text_query_gtest.exe --gtest_filter='WindowFullscreen*.*:RadiantFrameDelivery.Fullscreen*'
./lambda.exe view test/native/fullscreen/window.ls --event-file test/native/fullscreen/window.json
./lambda.exe view test/native/fullscreen/window.ls --fullscreen --event-file test/native/fullscreen/startup.json
```

Linux/Windows and physical multi-monitor, mixed-DPI, hotplug, and focus-loss
checks remain open. Stage 2's document model, top layer, user activation, CSS,
and standard JS API are not implemented by this delivery.
