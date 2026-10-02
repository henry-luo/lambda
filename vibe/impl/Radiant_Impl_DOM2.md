# Radiant DOM Implementation Plan 2 — Library Ladder + Conformance Widening

**Status:** in progress — checkpoint updated 2026-07-18
**Predecessor:** [Radiant_Impl_DOM.md](./Radiant_Impl_DOM.md) (Phases 0–6 complete: jQuery 3.7.1 + Bootstrap 5.3.3 fully supported, observers native, 5 WPT runners pinned, 21-fixture `test/ui/dom/` suite in CI).
**Scope doc:** [Radiant_DOM_Support.md](./Radiant_DOM_Support.md) — this plan implements its §2.5 supporting cleanups (#18, #19), the APIs implied by the §5.1 near-term library ladder, three new WPT runners (`input-events/`, `html/dom/` reflection, `dom/ranges/`), the §5.2 mid-term library set (Alpine.js, Floating UI + Tippy, Splide/Swiper, GSAP, Tabulator), and the §5.3 flagship (CodeMirror 6).
**Implementation path:** unchanged — every new DOM API lands as module-owned dispatch behind the Jube `radiant` bridge ([Lambda_Jube_DOM.md](../Lambda_Jube_DOM.md) / [Lambda_Jube_DOM2.md](../Lambda_Jube_DOM2.md)); no new ad-hoc branches in `js_dom.cpp`.
**Roadmap ledger:** items here continue the `Radiant_DOM_Support.md` §2 numbering — #18–#19 (carried over) and new #20–#26 (defined below). On completion, tick them in the support doc and extend its §1.7/§5 tables.

### Progress checkpoint — 2026-07-18

| Phase | State | Evidence / remaining gate |
|---|---|---|
| **0 — vendoring + red baselines** | ✅ Complete | All planned bundles and three WPT runners are in-tree with pinned versions/baselines. |
| **1 — console/performance/navigator** | ✅ Implemented | Real document console formatting, monotonic timing, and capability fields are exercised by `dom_console_perf`. |
| **2 — History/location** | ✅ Implemented | Same-document history state/traversal and hash behavior are covered by `dom_history`; the planned L3 `history_hash` fixture is still absent. |
| **3 — input value types** | ✅ Implemented | Date/time/color/file value-state coverage is present in `dom_input_value_types`. |
| **4 — near-term libraries** | 🟡 Implemented | Six L2 goldens and six L3 fixtures are present. Re-run the full aggregate before declaring Milestone A final. |
| **5 — WPT widening** | ✅ Exit gate reached | Passing baselines ratcheted from ranges **6→9**, input-events **5→8**, reflection **0→1**. |
| **6 — mid-term libraries** | 🟡 Implemented | Alpine, Floating UI/Tippy, Splide, GSAP, and Tabulator goldens/fixtures are present. GSAP and Tabulator focused gates are green; aggregate revalidation remains. |
| **7 — CodeMirror 6** | 🟡 In progress | L2 golden and paste L3 fixture pass. The type/navigation fixture no longer crashes, but delayed DOM-selection reconciliation still changes a caret at 10 into a stale backward `0..4` selection. |
| **8 — CI/docs** | ⏳ Pending | New runner binaries are registered in `build_lambda_config.json`, but aggregate wiring, support-doc synchronization, full clean verification, and the exact `make test-layout-baseline` zero-failure gate remain. |

This table records implementation progress, not a final green claim. The terminal acceptance command has **not** passed yet because `codemirror_type` still ends as `Radiant DOM2 alpha` instead of `Radiant DOM2`.

### Explicitly deferred / out of scope

- **Full `Touch`/`TouchList`/`TouchEvent` model stays deferred** (decision carried from Plan 1 Phase 5: Bootstrap's pointer branch sufficed). All gesture-driven work in this plan (Splide/Swiper, carousels) is driven through the **PointerEvent path** (`pointerType: "touch"`, already supported by `event_sim`'s `pointer_drag`). If a target library hard-requires `TouchEvent` with no pointer fallback, record it as a KIV note in the support doc — do not implement the touch model as a side effect of a library golden.
- All §3 non-goals of the support doc are unchanged except the contenteditable wording discovered during CodeMirror integration: `execCommand` and rich-formatting default actions remain out of scope, but an **unprevented basic text insertion/replacement** now performs the browser-owned contenteditable mutation and publishes observer records. A canceled `beforeinput` remains exclusively script-owned. Shadow DOM/customElements and WebSocket/Worker remain out of scope; IndexedDB remains KIV.

---

## 0. Ground rules and test pyramid

Identical to Plan 1. Every phase ships tests at up to three levels; a phase is done only when its exit gate is green **and** all pinned baselines hold (`make test-lambda-baseline`, `make test-radiant-baseline`, full `test_js_gtest`, all existing `test_wpt_*` baselines, `make dom-ui`).

| Level | Location | Mechanism | What it proves |
|---|---|---|---|
| **L1 — WPT conformance** | `test/wpt/` runners over `ref/wpt/` | gtest runner + `wpt_testharness_shim.js`, pinned passing-file baseline that only ratchets up | spec-correct JS API behavior |
| **L2 — library goldens** | `test/js/*.js/.html/.txt` | `test_js_gtest` DOM-mode (subprocess with `--document`) | real library code paths work end-to-end at the JS level |
| **L3 — UI end-to-end** | `test/ui/dom/*.json` | `./lambda.exe view <page> --event-file <json> --headless` via event_sim | real input → event funnel → JS → layout → assertion |

**Library vendoring pattern** (established by `dom_bootstrap.js`): vendor the library's single-file UMD/IIFE production build as `test/js/<name>.min.js`, load it from the driver via `(0, eval)(fs.readFileSync('test/js/<name>.min.js', 'utf8'))`, and record the exact upstream version + source URL in a header comment and in this doc's implementation log. Libraries that ship only as ES modules (CodeMirror 6, Floating UI core) are vendored as a one-time bundled IIFE (built offline, committed with the bundler command recorded in the file header) — no build-time bundling in the repo.

New WPT runners clone the `test/wpt/test_wpt_selection_gtest.cpp` pattern: recursive discovery under a pinned `ref/wpt/` subtree, helper-script allowlist, shim injection, stdout result parsing, passing-file `*_baseline.txt` that only ratchets upward. Extend `wpt_testharness_shim.js` when a suite needs a missing harness feature; never swap in upstream testharness.js.

Coding rules per `CLAUDE.md`: C+ convention, `lib/` containers only, `log_debug/info/error` with distinct prefixes, root-cause comments at fix points, no hardcoded workarounds.

---

## Phase 0 — Vendoring and red baselines

No engine changes; establish ground truth before touching anything.

1. **Vendor the library set** (versions pinned at vendoring time, recorded in the log): Sortable.js, flatpickr, htmx, Tom Select, noUiSlider, Micromodal (near-term); Alpine.js, `@floating-ui/dom` + Tippy.js, Splide, GSAP, Tabulator (mid-term); CodeMirror 6 bundle (flagship). Swiper is vendored only if Splide proves insufficient as the carousel/gesture representative — prefer the lighter library first.
2. **Boot probes**: for each library, a minimal `test/js/lib_<name>.js` that loads the bundle and reports load success + a one-call smoke (e.g. `Sortable.create(el)` returns an instance). Run all; record which libraries already boot, which throw, and the first missing API each one hits. These probes grow into the real goldens in Phases 4–6; the probe results decide phase ordering details.
3. **Three new WPT runners, red-baselined** (the baseline pins current truth; passing sets ratchet later):
   - `test_wpt_input_events_gtest` over `ref/wpt/input-events/` (29 files) — acceptance for the §3.1 editing contract: prevented input is script-owned, while unprevented basic text input uses the native default action. This is prerequisite confidence for CodeMirror 6. Note: `input-events-exec-command.html` and other execCommand-dependent files are non-goal skips, listed as such in the baseline header.
   - `test_wpt_dom_ranges_gtest` over `ref/wpt/dom/ranges/` (42 files) — hardens the Range surface CodeMirror leans on.
   - `test_wpt_html_reflection_gtest` over the curated `reflection-*.html` + `aria-*reflection*.html` set in `ref/wpt/html/dom/` — attribute ↔ IDL reflection. Curated, not the whole `html/dom/` tree (it contains unrelated suites like `directionality/`, `documents/`).
4. Record all numbers (probe results, three red baselines) in the implementation log at the bottom of this doc.

**Exit gate:** all vendored bundles committed with version records; boot probes executing; three runners registered with pinned (initially red) baselines; log section records the starting numbers.

---

## Phase 1 — Platform cleanups (#18, #19)

Small and immediate; do first because working `console` output unblocks debugging every later phase.

### 1.1 Real `console` + `performance.now` (#18)

- Replace the no-op `console` stub and the `performance.now → 0` stub (`radiant/script_runner.cpp:1204–1208`) with native functions:
  - `console.log/info/warn/error/debug` route to `lib/log.h` (`log_info`/`log_warn`/`log_error`/`log_debug`) with a distinct `js-console:` prefix, serializing arguments the way the existing JS `console` does in non-document mode — reuse that formatter, do not write a second one. `console.dir`/`table` may alias `log` v1.
  - `performance.now()` returns real monotonic milliseconds since document time origin (the frame clock's time base, so rAF timestamps and `performance.now()` agree — GSAP compares them). `performance.timeOrigin` consistent. `mark`/`measure`/`getEntries*` stay inert stubs (no target library reads them; keep them present for feature detection).
- Note: in `--document` batch mode L2 goldens assert on `console.log` output today via the harness stdout capture — the browser-document `console` must keep writing to the same capture stream the harness parses, in addition to `log.txt`. The goldens are the regression net for this.

### 1.2 `navigator` accuracy (#19)

- Keep the fixed object (`script_runner.cpp:1189`) but make the sniffed fields truthful to actual capability: `maxTouchPoints` must be `0` while the Touch model stays deferred yet pointer events report `pointerType:"touch"` — **decide and document the value** (recommendation: keep `maxTouchPoints: 1` so pointer-based touch-gesture branches in Splide/Swiper activate, and record the rationale here; this is exactly the field those libraries branch on). Add the handful of fields the Phase 0 probes show libraries reading (`vendor`, `userAgentData` absent-by-design, etc.) rather than speculating.

### Tests

- L2: `dom_console_perf.js` — `console.log` reaches harness output; two `performance.now()` calls straddling a timer are monotonic and plausibly spaced; rAF timestamp and `performance.now()` share a time base.
- Existing suites: full `test_js_gtest` (console formatter is shared surface — watch for golden drift, fix formatter once, regenerate affected goldens in the same change with justification).

**Exit gate:** `dom_console_perf` green; no unexplained golden drift; GSAP boot probe stops reporting a zero clock.

---

## Phase 2 — History API + location hardening (#20) — the htmx gate

Current state (`lambda/js/js_dom.cpp:14181–14233`): `pushState`/`replaceState` rewrite `doc->url` and discard state; `back`/`forward`/`go` are no-ops; `history.length` is pinned at 1; no `popstate`, no `history.state`, no `hashchange`. htmx's history support (`hx-push-url`, back-button restore) needs the real machine.

- **Session history stack** owned by the document (module-side, `lambda/module/radiant/`): entries of `{state, url}`; `pushState` appends + truncates the forward list, `replaceState` replaces in place; both go through the structured-clone-lite path used for `postMessage` for the state value (same supported-type subset; document the limits). `history.length` and `history.state` live.
- **Traversal**: `back`/`forward`/`go(n)` move the index, update `location`, and fire `popstate` (with the entry's state) as a task on the page event loop — same-document traversal only; cross-document navigation remains out of scope (single-page engine embedding).
- **`hashchange`**: fragment-only URL changes (via `location.hash` assignment or same-document traversal) fire `hashchange` with `oldURL`/`newURL`.
- **`location` interface hardening**: `location` is currently the document proxy; ensure `location.href/hash/search/pathname` setters route through the history machine (fragment-only set → `hashchange` + history entry; non-fragment set → out-of-scope navigation, log + no-op, documented). `scrollRestoration` accepted and stored, behavior KIV.

### Tests

- L2: `dom_history.js` — push/replace/state round-trip, `length`, `go(-1)` fires `popstate` with the right state, `hashchange` on fragment set.
- L3: `test/ui/dom/history_hash.json` — click handler pushes states, `go(-1)`, `assert_text` shows the popstate-restored view.
- L1: no dedicated runner (the `html/browsers/history/` suite drags in cross-document navigation); the htmx golden in Phase 4 is the acceptance.

**Exit gate:** `dom_history` green; htmx boot probe's history feature-detect passes.

---

## Phase 3 — Form control value types (#21) — the flatpickr gate

`FormControlType` (`radiant/view.hpp:1945`) has no date/time/color/file kinds; those inputs currently render and behave as plain text. This phase implements **spec value semantics, not picker chrome** — Radiant is an embedded engine; native picker popups are explicitly not the goal (flatpickr exists precisely to replace them).

- **`type` reflection**: `input.type` must reflect the authored type (`"date"`, `"color"`, …) even where the control renders via the text path — libraries feature-detect by setting `type` and reading it back (flatpickr's mobile detection does exactly this). Decide per type: reflect honestly (`"date"`) when we implement its value state below; keep the current normalize-to-`"text"` for types we don't, so feature-detection stays truthful.
- **Date/time family** (`date`, `time`, `datetime-local`, `month`, `week`): value sanitization to the spec grammar (invalid → `""`), `valueAsNumber` and `valueAsDate` get/set, `min`/`max`/`step` participation in constraint validation (`rangeUnderflow`/`rangeOverflow`/`stepMismatch`). Rendering stays the text control.
- **`color`**: value sanitization to `#rrggbb` lowercase (invalid → `#000000`), reuse the CSS engine's color parser — do not write a second one.
- **`file`**: read-only `value` (`C:\fakepath\…` form), `files` returning a `FileList` (the `File`/`FileList` types exist in `js_formdata.cpp`), programmatic population hook for tests (no OS file dialog — headless), participates in `FormData` serialization.
- New `FormControlType` members only if layout/behavior actually diverges from text; otherwise keep the text control and put the semantics at the IDL/value-state layer (module dispatch), which is where the spec puts them too.

### Tests

- L1: the existing `test_wpt_form_gtest` tree already contains the relevant `forms/` value-sanitization files — ratchet its baseline (record delta).
- L2: `dom_input_value_types.js` — sanitization, `valueAsNumber`/`valueAsDate` round-trips, color normalization, file `FileList`/`FormData`.
- L2: flatpickr golden (Phase 4 does the full golden; this phase just needs the probe to pass its type-detection).

**Exit gate:** `dom_input_value_types` green; `wpt_form` baseline ratchets; flatpickr probe passes feature detection.

---

## Phase 4 — Near-term library goldens (§5.1 ladder) (#22)

Each library gets an L2 golden exercising its core interactions programmatically, plus an L3 fixture where real input is the point. Grow each Phase 0 probe into the golden. Independent of each other; parallelizable after Phases 1–3.

| Library | L2 golden asserts | L3 fixture |
|---|---|---|
| **Sortable.js** | `Sortable.create`, programmatic sort, `onEnd` payload (`oldIndex`/`newIndex`) | `sortable_drag.json` — `mouse_drag` reorders items, `assert_text` order (external validation of Stage 4C drag machinery) |
| **flatpickr** | open calendar, select date, `input.value` format, `onChange` fires | `flatpickr_pick.json` — click input → click day cell → `assert_value` |
| **htmx** | `hx-get` swap from a `file://` sibling, `hx-push-url` history entry, `htmx:afterSwap` event | `htmx_click_swap.json` — click → content swapped via XHR → `assert_text`; back → `popstate` restore |
| **Tom Select** | init on `<select>`, keyboard filter, selection updates underlying select's `selectedOptions` | `tomselect_keyboard.json` — `type` to filter, `key_down`+Enter, `assert_value` |
| **noUiSlider** | create, programmatic `set`, `update` event values, `matchMedia`-driven config | `nouislider_drag.json` — `pointer_drag` handle, `assert_attr` aria-valuenow |
| **Micromodal** | show/close, focus trap list computation | `micromodal_focus.json` — open → `Tab` cycle stays inside → `Esc` closes → focus returns to trigger (`assert_focus`) |

Bugs surfaced by goldens are engine bugs to root-cause and fix at the source (with the fix-point comment rule) — never worked around in the golden.

**Exit gate (= Milestone A):** all six goldens + fixtures green; `Radiant_DOM_Support.md` §5.1 table gains a status column, all ✅.

---

## Phase 5 — Conformance widening: the three new WPT runners green-baselined (#23)

The Phase 0 runners exist and are red; this phase fixes what they expose and pins ratcheting green baselines. Ordered by payoff for Phase 7:

1. **`dom/ranges/`** — Range boundary/mutation semantics (range adjustment on tree mutation is the classic gap — `Range-mutations-*.html`). Fixes land in `radiant/dom_range.cpp` / module dispatch.
2. **`input-events/`** — `beforeinput`/`input` ordering, `inputType` values, `getTargetRanges()` in editable regions, cut/paste input events. execCommand-dependent files stay non-goal skips. This suite is the conformance statement of the §3.1 split contract: canceled `beforeinput` is script-owned; unprevented basic text insertion/replacement receives a native default mutation.
3. **`html/dom/` reflection (curated)** — attribute ↔ IDL reflection: enumerated attributes (limited-to-known-values), boolean reflection, `long`/unsigned reflection with defaults, URL-reflecting attributes. Expect this to drive a small table-driven reflection helper in the module dispatch layer rather than per-property fixes (CLAUDE.md rule 13 — extract the shared shape at the third near-identical case).

Shim work as needed (e.g. reflection tests use `format_value`; ranges tests use `assert_nodes_equal`) — extend `wpt_testharness_shim.js`, never import upstream.

**Exit gate:** all three baselines pinned with a recorded passing count that is strictly greater than Phase 0's red count; no regressions in the five existing WPT baselines.

---

## Phase 6 — Mid-term library goldens (§5.2 ladder) (#24)

These are stress tests of the observer/transition/frame infrastructure Plan 1 built. Same golden pattern as Phase 4.

| Library | Stresses | L2 golden asserts | L3 fixture |
|---|---|---|---|
| **Alpine.js** | MutationObserver (component discovery/teardown purely by observing mutations) | `x-data`/`x-show`/`x-on` boot, dynamically inserted component initializes, removed component tears down | `alpine_counter.json` — click increments `x-text`, `assert_text` |
| **Floating UI + Tippy.js** | ResizeObserver + geometry math (Popper's successor) | `computePosition` placements match expected coordinates; Tippy show/hide lifecycle; reposition on anchor resize | `tippy_hover.json` — hover → `assert_visible` + `assert_position` near anchor |
| **Splide** (Swiper only if Splide under-covers) | transitionend + pointer gestures | init, programmatic `go()`, transition-driven `moved` event | `splide_swipe.json` — `pointer_drag` (`pointerType:"touch"`) advances slide, `assert_class` active |
| **GSAP** | frame stepping / rAF throughput | `gsap.to()` tween completes, `onComplete` fires, final computed style exact; timeline sequencing order | `gsap_tween.json` — click starts tween → `wait` → `assert_style` |
| **Tabulator** | geometry-on-read, virtual scroll, `scrollTop` round-trips | table builds from data, sort click reorders, programmatic scroll updates rendered row window | `tabulator_scroll.json` — `scroll` → `assert_text` of first visible row |

Watch items (from the risk register): Alpine is the sharpest MutationObserver-timing arbiter we have — delivery deviations that WPT tolerates may still break it; GSAP doubles as the style-write throughput benchmark — record wall-clock in the log, and if the Phase 1 flush counter shows thrash, that is a GSAP write-batching characteristic to document, not an engine bug to "fix" with stale geometry.

**Exit gate (= Milestone B):** all five goldens + fixtures green; support-doc §5.2 table all ✅.

---

## Phase 7 — CodeMirror 6 flagship (#25)

The proof that dropping `execCommand` costs nothing for modern editors. CodeMirror handles many commands from its own model, but real CM6 also leaves ordinary text `beforeinput` unprevented and expects the browser to mutate contenteditable before MutationObserver reconciliation. The required contract is therefore: canceled events stay script-owned; unprevented basic insert/replace operations mutate the DOM, collapse Selection to the inserted end, publish MutationObserver records, and let CodeMirror reconcile. Selection/Range are used throughout. Everything else it needs exists after Phase 5; this phase is integration and bug-fixing, not rich-editing API expansion.

- Vendor a minimal CM6 bundle (`@codemirror/state` + `@codemirror/view` + basic setup) as a committed IIFE.
- L2 `lib_codemirror.js`: create editor, programmatic transaction (insert/delete), `state.doc` round-trip, selection set/read.
- L3 `codemirror_type.json`: focus → `type` text → arrow keys → select-all → `type` replacement → `assert_text`; a second fixture for paste (`clipboardData` path).
- Expected friction points, in order: `beforeinput` `inputType`/`getTargetRanges` fidelity (Phase 5.2 hardened this), Selection change events during composition, MutationObserver reconciliation timing, `Range.getClientRects` for cursor placement (Phase 5.1). Root-cause each; the WPT baselines are the regression net for the fixes.

**Exit gate (= Milestone C):** both fixtures + golden green. Support doc §5.3 updated with a dated status line.

---

## Phase 8 — CI consolidation (#26)

- All new L2 goldens run in `test_js_gtest` by existence (discovery is automatic once the `.txt` golden exists).
- New L3 fixtures join `test/ui/dom/` and run under the existing `make dom-ui` target (already in `make test-extended` via `dom-ui-run`); after a stable soak, promote `dom-ui` into `make test-radiant-baseline` (the promotion Plan 1 anticipated).
- Three new WPT runners wired into the same aggregate the existing five use.
- Support-doc upkeep: tick #18–#26, refresh §1.6/§1.7, add status columns to §5.1/§5.2/§5.3, note the Touch/TouchList deferral outcome (any KIV entries from Phase 6).

**Exit gate:** one green run of the full aggregate from a clean build, recorded in the implementation log.

---

## Sequencing and dependencies

```
Phase 0 (vendor + red baselines)
   ├─ Phase 1 (console/perf/navigator) ── first; unblocks debugging everywhere
   ├─ Phase 2 (History) ────────┐
   ├─ Phase 3 (form values) ────┼─ Phase 4 (near-term goldens, Milestone A)
   └─ Phase 5 (WPT: ranges → input-events → reflection)
                                 │
Phases 1+Plan-1 infra ─────────── Phase 6 (mid-term goldens, Milestone B)
Phase 5 ───────────────────────── Phase 7 (CodeMirror 6, Milestone C)
All ───────────────────────────── Phase 8 (CI + doc upkeep)
```

- Phases 1–3 and 5 are mutually independent after Phase 0; Phase 6 needs only Phase 1 (GSAP needs the real clock) plus Plan-1 infrastructure; Phase 7 wants Phase 5 done first (ranges + input-events fixes are its foundation).
- htmx (Phase 4) is the one near-term golden gated on Phase 2; the other five need only Phases 1/3.

## Risks

- **Library bundles as moving targets**: vendor once, pin the version, never auto-update. A library bug worked around upstream in a later version is *not* a reason to bump mid-plan; record and move on.
- **`console` formatter drift** (Phase 1): the formatter is shared with non-document JS — goldens across the whole `test/js/` tree assert on its output. Change it once, regenerate affected goldens in one commit with justification.
- **History semantics creep** (Phase 2): full navigation (cross-document, bfcache) is a browser, not an embedded engine. The stack is same-document only; anything else is a logged no-op. Resist widening.
- **Reflection sprawl** (Phase 5.3): hundreds of reflected properties exist; implement the table-driven helper and populate it for the attributes the curated suite + vendored libraries actually touch, not the whole HTML spec. The baseline pins the honest subset.
- **Alpine MutationObserver timing** (Phase 6): Alpine's component lifecycle is the most timing-sensitive MO consumer known; if it breaks while WPT passes, the bug is real — use it to sharpen the MO tests, not to special-case delivery.
- **Gesture coverage without Touch model**: Splide/Swiper pointer branches must be verified against `navigator.maxTouchPoints` and `PointerEvent` feature detection (Phase 1.2 decision). If a library's touch path is unreachable via pointers alone, that is the documented KIV boundary, not a reason to implement TouchEvent ad hoc.
- **CM6 bundle size/eval cost** (Phase 7): the bundle is large; if `--document` subprocess runs get slow, the golden moves to `test/js/slow/` (the existing slow-tier convention) rather than being trimmed into unrepresentativeness.

## Doc upkeep

- After Phase 0: record vendored versions + red baselines below.
- After each phase: tick the corresponding #-items in [Radiant_DOM_Support.md](./Radiant_DOM_Support.md) §2 (extend with #20–#26), update its §1.6/§1.7/§5 tables.
- Keep the implementation log below (dated entries, verification runs), in the style of Plan 1.

---

## Implementation log

### 2026-07-18 — implementation checkpoint

#### Vendored library set

The planned ladder is pinned in `test/js/` (no runtime download or build-time bundling):

| Library | Pinned version |
|---|---:|
| Sortable.js | 1.15.7 |
| flatpickr | 4.6.13 |
| htmx | 2.0.10 |
| Tom Select | 2.6.2 |
| noUiSlider | 15.8.1 |
| Micromodal | 0.7.0 |
| Alpine.js | 3.15.12 |
| `@floating-ui/dom` | 1.8.0 |
| Tippy.js | 6.3.7 |
| Splide | 4.1.4 |
| GSAP | 3.15.0 |
| Tabulator | 6.5.2 |
| CodeMirror umbrella/state/view | 6.0.2 / 6.7.1 / 6.43.6 |

The near-term and mid-term L2 files are present as `lib_<name>.js/.txt` (with an `.html` document where required), and their L3 interaction fixtures are present under `test/ui/dom/`. Swiper was not added because Splide covers the planned pointer-gesture representative.

#### Platform and conformance work landed

- Phase 1–3 focused goldens are present: `dom_console_perf`, `dom_history`, and `dom_input_value_types`.
- Window/DOM constructor branding, declared Jube interface dispatch, Selection/Range behavior, MutationObserver delivery, input/clipboard event plumbing, ARIA reflection, and testdriver support were widened while bringing up the libraries.
- The three new WPT binaries are registered through `build_lambda_config.json`. Their pinned pass sets moved as follows:

  | Runner | Initial | Current | Delta |
  |---|---:|---:|---:|
  | `dom/ranges` | 6 | 9 | +3 |
  | `input-events` | 5 | 8 | +3 |
  | curated HTML/ARIA reflection | 0 | 1 | +1 |

- Range and text mutation code now shares native string/text helpers instead of duplicating allocation and replacement logic across the JS bridge and Radiant editing path.
- The unprevented rich-editing default path now performs basic insert/replace mutations, collapses the canonical DOM selection, and publishes MutationObserver records. `preventDefault()` continues to suppress that mutation.

#### Library ladder results

- The six Phase 4 near-term goldens and fixtures are in-tree. The missing planned artifact is the dedicated `history_hash` L3 fixture; htmx has its own click/swap/history coverage.
- Alpine, Floating UI/Tippy, Splide, GSAP, and Tabulator L2/L3 artifacts are in-tree.
- GSAP's timing path has a permanent engine fix and regression coverage rather than a fixture workaround.
- The Tabulator crash is resolved. Its L2 golden and L3 virtual-scroll fixture pass, including `scrollTop`/rendered-window behavior.

#### CodeMirror 6 progress and root causes

- `lib_codemirror` L2 passes: editor creation, transactions, document round-trip, and model selection work.
- `codemirror_paste` L3 passes through the clipboard/input path.
- `codemirror_type` now loads the full `basicSetup` and completes without the former crash.
- Library bring-up exposed and fixed general LambdaJS issues, each with focused regression coverage: named-class self binding, destructuring aliases shadowing a class name, Unicode binary-property regex classes, constructor method availability before field initialization, ordinary object methods named `call`/`apply`, and Window constructor/prototype branding.
- The final CodeMirror crash was a MIR loop-specialization bug. A semi-native loop cached a dynamic member bound before entering the loop; CodeMirror SearchCursor then executed `splice()` under `i < matches.length`, but the compiled test retained the old length and accessed an element after the array became empty. Semi-native loop tests now re-evaluate the live member bound on every iteration while retaining the native comparison. `for_dynamic_member_bound` covers both shrinking and growing member bounds and passes its focused GTest.
- The remaining failure is selection reconciliation, not a crash or document-model mismatch. After native insertion, CodeMirror first reconciles `seed alpha` with both model and DOM selection collapsed at offset 10. A later selection update changes the native selection to backward `4→0`, and CodeMirror consequently adopts model selection `0..4`. Select-all briefly reaches `0..10`, but the stale `0..4` selection returns before replacement input, yielding `Radiant DOM2 alpha`. The next root-cause task is to identify the delayed writer of that native selection and repair the canonical Selection/event-loop boundary.

#### Verification at this checkpoint

- `make build` — green.
- `JavaScriptTests/JsFileTest.Run/for_dynamic_member_bound` — green (1/1).
- `git diff --check` — green.
- CodeMirror L2 and paste L3 — green; type/navigation L3 — one assertion failure described above, no crash.
- The full aggregate, full baseline suites, lint gate, and exact `make test-layout-baseline` zero-failure acceptance still need to run after the remaining CodeMirror selection fix and Phase 8 wiring. No final DOM2 completion claim is made at this checkpoint.
