# HTML support implementation

## Current checkpoint

- Goal: implement the confirmed HTML gaps from the 2026-11-18 audit, with
  ownership, focused verification, and updates to the public support matrix.
- Checkout: `/Users/henry/Projects/lambda`, `master` at `1bcd44f41`; Radiant
  `6f38618`, lambda-input `1e48876`. The resumed goal started from clean tracked
  trees with the previous milestones and slot paint changes committed.
- Updated 2026-11-18. Use the main checkout's split build and keep scratch
  artifacts under `temp/html-support-audit/`.
- Ownership follows **D9.1.1v4 / D9.1.3v2 / D9.5.6v2**. Lambda owns script
  preparation and DOM semantics. Lambda-input owns HTML tree construction.
  Radiant owns native layout and paint. **D10.2.1** governs form editing policy.
- Textarea milestone: the painter can run before the form's value cache is
  initialized. It now reads the shared initial-value calculation when paint
  has no canonical value. Paint leaves the state store untouched and a live
  empty value takes precedence (**D10.2.1**).
- List milestone: `type` now supplies the HTML marker hint for `ol`, `ul`
  and `li` through the existing CSS author-hint fallback. Native ownership
  follows **D9.1.1v4 / D9.5.6v2**.
- Canvas milestone: `getTransform()` snapshots the current native affine
  matrix into an independent realm `DOMMatrix`. Provider and rooting
  contracts follow **D9.1.3v2 / D9.5.6v2 / D6.3.3**.
  `ellipse()` and `arc()` now share one rotated cubic arc builder. Opposing
  large finite sweeps normalize in constant time, and negative radii raise
  the Web API's `IndexSizeError`. `arcTo()` uses the same builder for tangent
  arcs with the current path and handles the straight-line cases.
- Select parsing milestone: Input follows the Living Standard's in-body
  rules for select. It preserves rich option trees and the custom button
  without changing old fixture expectations. A fostered select keeps its
  descendants together outside the table (**D9.1.1v4 / D9.1.3v2**).
  The shared option traversal now follows those wrappers from collection
  reads, form submission, size calculation and native paint. Rich option
  text comes from the same HTML-aware text helper for layout and paint.
  That restores the `select-many-options.tentative` layout baseline.
- Selected content milestone. JavaScript documents mirror cloned option
  children during binding, script selection and form reset. The clone
  excludes script and embedded browsing content, and structural changes
  coalesce in a microtask. The option arrays stay precisely rooted across
  setters and reset (**D9.1.3v2 / D6.3.3**).
- Dialog milestone. Author markup and live `show()` can open a non-modal
  dialog. Reflection updates the `open` attribute, and the UA box centers
  on the inline axis while retaining its static block position. The JS
  interface and the engine behavior follow **D9.1.3v2 / D9.5.4 / D9.5.6v2**.
- Audit evidence: the original binary produces identical scanlines for initial
  and empty textarea fixtures. A broken inline image paints its alt text.
  Conversion of a customizable select drops its button and selectedcontent.
- Evidence: split debug build and a second split host under
  `temp/html-support-audit/lambda-select.exe` succeeded. The focused CLI
  suites pass 14/14 on the second host. All four new CLI checks fail on
  the original binary. Six direct PNG textarea checks also pass. SVG and
  PDF probes show that flat vector output still omits control text.
- Input verification. The full input baseline passed 5,333 checks with the
  select migration, including 83 native, 126 negative and 364 html5lib checks.
  Three tests skipped. The baseline used the pinned YAML corpus and local
  loopback access. The CLI roundtrip retains the button, selectedcontent
  and option span.
- Radiant baseline. The aggregate on the earlier host reported 5,628 full
  and 295 partial layout passes. A CSS Inline case failed. The cascade-memory
  failure reproduces on the untouched binary. A paged grayscale test could
  not open its font fixture. The document, Scene3D, and UI suites reported
  idle timeouts. The aggregate finished with failures. See
  `temp/html-support-audit/radiant-baseline.log`.
- Shadow milestone. Reconciliation reaches the connected host for shadow
  mutations and invalidates its layout and paint. Raster, flat vector and
  hit-test traversal share the layout child list and its stacking ancestry.
  Live markup and append checks cover open and closed roots, empty-host
  suppression, local CSS isolation and positioned descendants. PNG, SVG
  and PDF samples contain native shadow content (**D9.1.1v4 / D9.1.3v2**).
- Shadow DOM API milestone. Composed root lookup traverses nested host links
  and stops at the detached host or Document. Read-only root properties are
  derived from native state. The named-slot DOM APIs share layout's slot
  ordering and handle live reassignment, text, fallback flattening and closed
  roots. The focused checks pass under forced GC (**D9.1.3v2 / D6.3.3**).
- Gauge milestone. The normalized HTML numbers feed native progress and
  meter paint and JavaScript reflection. The same paint plan emits CSS-pixel
  rectangles in raster, flat SVG and flat PDF. Numeric clipping, threshold
  colors, logical direction, accent and appearance are covered
  (**D5.5.1 / D9.1.3v2**).
- Datalist milestone. The live options collection shares the HTML collection
  path. Input `list` checks type applicability and resolves the first matching
  ID in the input's own document or shadow tree (**D9.1.3v2 / D9.5.6v2**).
- Label milestone. `HTMLLabelElement` exposes `control` and `form` through the
  native association algorithm. The same live `NodeList` survives moves, ID
  changes and hidden-input transitions. Label and datalist ID resolution skip
  inert template descendants. The WPT `input-labels` and `labelable-elements`
  cases pass (**D9.1.3v2 / D9.5.6v2**).
- Label activation milestone. The DOM behavior package shares the label
  association with the IDL and forwards native clicks through a fresh Event
  record. The old checkbox-only path bypassed click listeners and interactive
  descendants. The forwarding path preserves dispatch cancellation, device
  fields and trust. Disabled markup and fieldsets are checked before reflow.
  `focus()` delegates through the same association when the label has no
  focusable area. The WPT proxy-click, noninteractive-labelable, and focus
  cases pass (**D10.2.1**).
- Current verification. The rebuilt `./lambda.exe` passed 29 focused HTML
  and Canvas CLI checks and 49 positioned paint, grid and pseudo regressions.
  Fourteen Canvas, form-control and shadow checks passed under every-allocation
  GC with heap poisoning. The native DOM range executable passed all 84 cases.
  All 378 required form layout entries are at baseline, including 368
  full and 10 partial matches. The current visual layout rerun retains the
  CSS Inline failure. The full WPT form run reported two intermittent iframe
  submission failures. Both failed and passed in direct retries on the saved
  original and current hosts. See `qualified-html-label-focus-all.log`,
  `qualified-html-label-focus-gc.log`, `qualified-label-focus-wpt.log`,
  `qualified-form-wpt-full.log`, `form-iframe-flake-original.log`,
  `form-iframe-flake-current.log`,
  `current-form-verified.log` and `verified-layout-baseline.log` in the scratch directory.
- Remaining audit work: dynamic external scripts and modules, shadow selector
  and resource scope, broader slot layout and declarative roots,
  form pickers and datalist suggestions, gauge animation, label press state,
  select parser and post-connection timing and picker paint,
  form validation and vector control paint,
  dialog top layer, opening and restoring focus, toggle events and native
  close gestures, submit and form-data event interfaces, iframe sandbox behavior, media,
  canvas, responsive image auto sizing and viewport, and additional MathML
  and ruby cases. Web Workers and WebSocket are
  browser-host work. Review each area against its current owner evidence.

## Dynamic classic script milestone

- Connected DOM-created inline classics run synchronously through the document
  evaluator. The post-connection snapshot pins each candidate until its turn
  and revalidates its document and connection. Fragment markup is inert and
  clones copy the started bit. The parser's cursor remains with `document.write`.
  Nested AST and MIR entries keep the enclosing microtask checkpoint
  (**D9.1.3v2 / D8.4.4 / D6.3.3**).
- Both script controls pass on AST and MIR. AST also passes with
  every-allocation GC and heap poisoning. The same-origin XHR reduction now
  runs `SETTINGS_FINISHED` before `SETTINGS_APPEND_RETURNED` and paints green.
  The combined CLI checks pass 80 cases. See `qualified-html-dynamic-all.log`,
  `qualified-dynamic-script-ast-gc.log`, `qualified-dynamic-script-mir.log`,
  and `qualified-dynamic-http-probe.log` in the scratch directory and the
  [script issue](../Lambda_Issue_Ledger.md#online-dynamic-script-preparation).
- The native document cases for `document.write` and rich select rendering pass,
  as do four native View script regressions (`qualified-dynamic-native-document.log`
  and `qualified-dynamic-native-view.log`).

## Scoped shadow stylesheet milestone

- The native style owner carries its scoped inline sheet. Mutation records
  reconcile owner insertions, text changes and tree moves before CSSOM reads.
  The cascade visits author sheets in source order per shadow tree. Child and
  descendant combinators stop at the fragment boundary. Open and closed roots,
  sibling and document isolation, `insertRule()`, media, movement, and final
  PNG paint have one focused control (**D9.1.1v4 / D9.1.3v2 / D5.5.1**).
- All 85 focused CLI checks pass (`qualified-html-shadow-css-all.log`). The
  shadow-style and both script checks pass under forced GC
  (`qualified-shadow-css-gc.log`). The full input baseline passed 5,333 checks
  after the selector change (`input-baseline-shadow-css.log`). The Radiant
  no-int-cast lint passed (`qualified-html-shadow-css-lint.log`).
- `ShadowRoot.styleSheets` uses the existing weakly cached CSSOM collection
  keyed by native root. Collection reads and the cascade share the same walk.
  List identity, media inactivity, source order, adoption between roots in one
  document and connecting a formerly detached root pass
  (`qualified-shadow-sheet-list.log`, `qualified-shadow-list-dialog-gc.log`).

## Dialog close requests

- `requestClose()` dispatches a trusted cancelable event against the live
  dialog, then uses the shared close path for successful requests. The open
  attribute transition ends the active request when a listener closes and
  reopens a dialog. The receiver, return-value conversion and event stay rooted
  across script callbacks (**D9.1.3v2 / D6.3.3**).
- The pinned WPT `dialog-requestclose.html` runs with the WPT harness and
  passes 75 subtests (`qualified-dialog-request-close-wpt.log`). All 21 focused
  HTML-control and stylesheet checks pass (`qualified-dialog-request-close.log`).
  The dialog and shadow sheet checks also pass with every-allocation GC and
  poisoned freed memory (`qualified-shadow-list-dialog-gc.log`).

## Resumed slot projection milestone

- The resumed host passed all 87 focused CLI checks. Named projected boxes and
  fallback text match an equivalent light-tree PNG. The positive geometry and
  point-hit checks pass (`qualified-shadow-slot-paint.log`).
- The form, CSS Display and CSS Inline recorded suites meet baseline. CSS Lists
  has one failure in `list-style-image-zoom-dynamic`; its body loses 16 px of
  collapsed margin. The slot counter controls pass (`qualified-slot-layout.log`,
  `qualified-slot-lists-detailed.log`).
- A live shadow-slot reduction reproduced a stale client rect for a now
  unassigned node and loss of the wrapper's collapsed margin after reassignment.
  Distribution mutations now reset both branches through the host. Filtered
  nodes expose no client rects. Normal-flow margin collapse follows the
  flattened adjacency and the rendered parent's border and padding. A parent
  that acquires a boundary during collapse records its original margin
  (**D9.1.1v4 / D9.1.3v2**).
- After the repair, all 88 focused CLI checks passed. Both slot paint and
  geometry checks passed under every-allocation GC and heap poisoning. Flat
  SVG and PDF contain the same assigned and fallback colors as PNG, and all
  three report the same rects through reassignment. The focused layout gate
  stayed at the resumed baseline, with only the pre-existing Lists zoom
  failure (`qualified-slot-margin-edges*.log`, `slot-*-final-output.log`).

## Named slot events and template query boundaries

- Distribution snapshots compare native identities in assignment order. The
  agent microtask snapshots pending observers and signal slots, delivers
  observer records, then dispatches trusted bubbling `slotchange` events.
  Work queued by callbacks remains pending for the next checkpoint. Event
  paths follow assigned slots and preserve original boundaries during DOM
  mutation. Target and related-target retargeting, phase changes, closed
  path filtering and legacy global event visibility use the captured path
  (**D9.1.3v2 / D6.3.3 / D5.5.1**).
- Native Document queries and realm element and fragment queries share the
  source-tree walker. Parsed nested template contents enter query scope only
  through their own fragments. The nested shadow WPT fixtures now construct
  the intended trees (**D9.1.3v2**).
- The 71 focused CLI cases pass and the four new or updated controls pass
  under every-allocation GC and poisoning. The 156 subtests across the
  pinned assignment, event delivery, composed-path, mutation and retargeting
  files pass.
  The full input baseline passes 5,337 tests with three skips
  (`qualified-shadow-events-aggregate.log`, `qualified-shadow-all-gc.log`,
  `qualified-related-wpt-detailed-*.log`, `qualified-input-template-queries.log`).

## Manual slot assignment and nested projection

- `attachShadow()` accepts named and manual distribution modes. The native
  lifecycle registry transfers weak manual bindings on adoption and releases
  them when either endpoint retires. Variadic assignment validates the whole
  request before replacing it. Assignment changes use the existing
  `slotchange` queue and a host-scoped invalidation record
  (**D9.1.3v2 / D5.5.1**).
- A shared iterator drives assignment reads, directionality, layout and
  painting. Layout enters a projected slot before changing leaf formatting
  links, so nested distribution still uses the slot's source root
  (**D9.1.1v4 / D9.1.3v2**).
- All 35 selected imperative and lifecycle WPT subtests pass, including
  cross-document node and slot moves. Both imperative PNG reftests match.
  All 73 focused CLI controls pass and both manual paint controls pass under
  every-allocation GC plus poisoning. The 752-entry focused layout inventory
  retains only the existing Lists zoom failure
  (`qualified-final-manual-wpt-manual-*.log`, `qualified-nested-slot-aggregate.log`,
  `qualified-nested-slot-gc.log`, `qualified-manual-slot-layout.log`).

## Slot display and HTML hidden states

- Slots use the HTML `display:contents` default. `display:none` cuts off
  the same flattened subtree for layout, paint, hit testing, and client rects.
  Slot attribute and style edits reset the affected host projection. Named
  and manual assignment remain visible to the DOM (**D9.1.1v4 / D9.1.3v2**).
- `HTMLElement.hidden` reflects the HTML union setter. An until-found
  element gets its concealment from the UA cascade, retains its own
  box and suppresses descendant paint. Computed style follows the shared
  property serializer, including an author override (**D9.1.3v2**).
- The pinned hidden reflection and cascade tests pass all 26 subtests. Both
  until-found paint reftests match. The full CLI sample passes 75 checks and
  both new visibility checks pass under precise GC stress. The full Input
  baseline after manual binding passes 5,337 cases with three skips. The
  focused layout inventory still has only RI-139
  (`qualified-cascade-wpt-hidden-*.log`, `qualified-hidden-cascade-aggregate.log`,
  `qualified-hidden-cascade-gc.log`, `qualified-input-manual-slot-lifecycle.log`,
  `qualified-html-hidden-slot-layout.log`).

## Fragment navigation and ancestor reveal

- Hash writes normalize the fragment and resolve the indicated source
  element. Native anchor activation and hash writes share the DOM reveal
  path. It pins the target and freezes the flat-tree ancestor work before
  dispatch. Closed details open when the path crosses their non-summary
  children. Event listeners can move the target, remove it, or change
  the hidden state before the post-event check. Native scroll skips
  targets that lose their boxes (**D9.1.1v4 / D9.1.3v2 / D9.5.6v2**).
- The pinned element-fragment suite passes all 10 subtests and four other
  ancestor controls pass. Chromium 152 passes the pinned
  `beforematch-element-removal-002.html`, which expects continuation
  after an until-found state changes. The current HTML algorithm aborts
  that ancestor walk. The Lambda test times out waiting for the additional
  event. Native viewport click replay passes both scroll and target geometry
  assertions and logs a trusted event while the target is still hidden.
- All 76 focused CLI cases pass. The four selected controls also pass with
  every-allocation GC and poisoning. The Radiant layout lint passes
  (`qualified-boxless-wpt-reveal-*.log`, `qualified-reveal-spec-residue.log`,
  `qualified-reveal-chromium.log`, `qualified-reveal-native-anchor.log`,
  `qualified-reveal-final-aggregate.log`, `qualified-reveal-final-gc.log`).

## Next confirmed gap

Dialog form submission still routes through ordinary navigation. Start with
the shared DOM close path and the Lambda form behavior entry point. Verify
button activation, `requestSubmit()`, direct `submit()`, and submitter
overrides alongside the existing close-event controls. Image-button
coordinates and the form-data event need their current ownership checked.

### Dialog form submission and image buttons

- The Lambda form behavior and JS submission methods now use a canonical
  method lookup and a single dialog close operation. The chosen dialog is
  the nearest source-tree ancestor of the form. Button value reflection and
  the empty boolean validation attributes use the normal DOM paths
  (**D9.1.3v2 / D9.5.6v2 / D6.3.3**).
- The image submitter uses the shared image loader and painter and retains the
  selected coordinate in integer CSS pixels. Open-attribute removal rechecks
  focus before native input continues. `dialog_image_submit` passes all five
  assertions normally and under forced GC. The image pixel matches in PNG,
  flat SVG and flat PDF. The JPEG probe also paints and reports `12,16`.
- The focused aggregate passes 77/77. Four focus replays pass 108 assertions.
  Radiant's integer-cast lint passes. The 752-entry layout inventory has the
  same pre-existing Lists zoom failure (RI-139) and no new failures.
- The unusual-action WPT passes 2/2 and the close-task file passes 1/1. The
  main file passes 6/8 with native pointer actions. The public click helper
  requires `elementsFromPoint()`; the pointer recording otherwise exposes a
  fractional event offset compared with HTML's integer selected point. The
  direct-submit expectation follows Chromium 152, which clears the value.
  The current HTML algorithm supplies a null result in this case and preserves
  the dialog's value.
- Scratch results include `qualified-dialog-form-final-aggregate.log`,
  `qualified-dialog-image-ui-gc.log`, `qualified-dialog-form-layout.log`
  and `qualified-native-action-dialog-form-layout.log`. See JS13-L12 and RI-140
  for the remaining event, image sizing and fallback work. The next forms
  increment is `SubmitEvent` branding and shared `formdata` dispatch.
