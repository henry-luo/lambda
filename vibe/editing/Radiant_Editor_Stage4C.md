# Radiant Rich Editor — Stage 4C: The Editor's Test Suite Green on the Lambda Runtime (two phases)

**Date:** 2026-07-01 · **Status:** Proposal (pre-implementation) — grounded in empirical spikes (§3); two-phase plan agreed; Lane B initial pick set (§4.2).
**Scope/goal:** Prove the **plain-DOM JS editor** works on the runtime it ships on, in two phases:
- **Phase A — `lambda.exe js`:** the **full ~1931-test plain-DOM suite** runs and passes on the headless **LambdaJS** runtime — *does the editor's own test corpus pass at the JS level under Lambda?*
- **Phase B — `lambda.exe view` + `event_sim`:** a **curated subset** (§4.2) runs as true **end-to-end UI automation** under the full Radiant stack (real layout, event dispatch, caret/selection rendering) — *does real interactive editing work end-to-end?*

Where the runtime falls short, **enhance DOM fidelity (`js_dom`) and `event_sim`** rather than weaken the tests (CLAUDE.md #1).
**Builds on:** [Radiant_Editor_Stage4B.md](Radiant_Editor_Stage4B.md) (the plain-DOM editor + common C++ substrate + the `view --event-file` testdriver), [JS_13_Web_DOM.md](../../doc/dev/js/JS_13_Web_DOM.md) (LambdaJS DOM surface + known issues).

---

## 0. TL;DR

The editor's **1960-test vitest suite is green under `jsdom` (Node)** — a browser-API emulation — but has **never run on LambdaJS/Radiant**, the runtime it ships on; only a **13-fixture smoke subset** (`test/ui/editor4b`) exercises it under Radiant. **Stage 4C** makes the shipping runtime a first-class test target, in two complementary phases:

- **Phase A (breadth, cheap, fast):** run the **whole plain-DOM suite (~1931 tests; React excluded)** headless under **`lambda.exe js`** and get it green. Proves the editor's logic + DOM-level behavior are correct on **LambdaJS**. Spikes already show the engine + every DOM API the tests use work (§3); the residual is **harness + fixture-inlining infrastructure** (because `fs`/`require` are absent in-engine).
- **Phase B (depth, slower, high-fidelity):** run a **curated ~8-file interaction subset** under **`lambda.exe view` + `event_sim`** — real events in, real DOM/selection out — to prove true end-to-end editing (typing, keys, selection, paste, drag, toolbar, indent, caret preservation) through the full Radiant stack. This is where **`js_dom` fidelity + `event_sim`** get enhanced.

**Why two vehicles:** `lambda.exe js` is fast and covers breadth (all ~1931) but only exercises the JS engine + a minimal DOM — no layout, no real event routing, no caret rendering. `lambda.exe view` exercises the *full* stack but is slower to set up per case, so it validates a representative **subset** deeply. Together: **breadth on A, depth on B.**

---

## Progress (2026-07-01)

**Phase 0 — harness foundation: DONE & proven.**
- `test/editor-js/harness/inengine.ts` — a vitest-compatible in-engine shim (`describe`/`it`/`it.skip`/`expect` + the matchers the suite uses + `beforeEach`/`afterEach` + `vi.fn`). Prints `HARNESS pass=N fail=M skip=K` + one `FAIL <name>: <msg>` per failure. Sync-only (async tests not yet supported).
- `test/editor-js/tools/build-conformance.mjs` — esbuild-bundles chosen `.test.ts` files to a classic IIFE, aliases `vitest`→the shim, appends `__harnessRun()`. Rejects `.tsx`.
- **Verified:** the editor's real `.test.ts` files run **unmodified** under `lambda.exe js`. Core editor (commands+model+input, 209 tests) → **198 pass / 11 fail**; a 94-test slice → 92/94. The pipeline works end-to-end (bundle → run → machine-readable report).

**Phase A gap inventory (empirical, from the 11 core failures — all in tight clusters):**
1. **`Intl` not defined** (2) — `src/commands/caret.ts:97` uses `new Intl.Segmenter(…,{granularity:'word'})` for word-boundary nav. → implement `Intl.Segmenter` in LambdaJS, or a word-seg fallback.
2. **`parseHtmlToDoc` DOM-parse fidelity** — inline-atom HTML round-trip + **all drawing tests (~28)** fail ("null is not iterable"); the LambdaJS DOM parser (`--document`) doesn't match jsdom for this markup. → §5 (drawing itself is Stage-5 scope, excluded from the green target).
3. **LambdaJS numeric type-inference bug in JS→MIR lowering — ROOT-CAUSED & FIXED (2026-07-01)** (fixed `selMap` 1 + gap-cursor 4 + `cmdMoveNode` 3 → core bundle **198→207/209**).
   - **Fix:** `lambda/js/js_mir_statement_lowering.cpp` — for a var/const whose initializer is a `CALL_EXPRESSION` inferred INT/FLOAT, keep it **boxed** (widen to ANY) unless it's a guaranteed-numeric `Math.*` builtin. A user function's numeric return type is speculative; the native var path unboxed it with an unchecked `it2d`/`d2i` that collapsed the boxed object to 0.
   - **Regression check: ZERO.** node-baseline's 2 flagged "regressions" + `test_js_gtest`'s 1 fail all fail identically on the pre-fix binary (pre-existing/stale-baseline), verified by git-stash + rebuild. (`vm_runincontext_cross_unit` shows the same object→undefined signature — a same-class case my narrow fix doesn't reach; follow-up candidate.)
   - *(original root-cause detail below)*
   - **Symptom:** `const m = stepMap(step, p)` inside a `for...of` loop → `m === 0` (should be the boxed `SourcePos`). Inline `stepMap(...)` works; the `const` binding fails.
   - **Root cause (from `temp/js_mir_dump.txt`):** the lowering **statically mis-infers `const m` as numeric** and emits `js_profiled_it2d` (item→double unbox) + `d2i` on the **boxed-object** call result before binding `m` (top-level `_js_anon0_165`, immediately after `js_call_function_1462` = the `stepMap` result). Unboxing an object as a double gives `0`. Same class as the Lambda-core *unsound speculative-int inference*, but in the JS transpiler.
   - **Layer:** MIR **lowering**, not JIT codegen — reproduces identically under `JS_MIR_INTERP=1` and default. Fix domain: `lambda/js/js_mir_expression_lowering.cpp` numeric inference / the `it2d` emission for a call-result binding.
   - **Module-state dependent** (fragile to minimize). Reliable repros: `temp/4c-spikes/min.js` (real bundled `stepMap`) + `temp/4c-spikes/depth.js` (self-contained). Diagnostics: `JS_MIR_DUMP=1 ./lambda.exe js <bundle>` → `temp/js_mir_dump.txt`; `JS_MIR_INTERP=1` = interpreter (JIT-vs-lowering bisection).
   - **⚠️ Fix caution:** this numeric/int inference is load-bearing (GC rooting, deltablue); scope the fix to call-result bindings and run the full lambda + node baselines.

**Not yet started:** fixture-inlining for the tier corpora (≈1601; LambdaJS has no `fs`/`require` — verified), the `view/*.ts` tests under `--document`, and all of Phase B. Spikes live in `./temp/4c-spikes/` (ephemeral; regenerate via `build-conformance.mjs`).

---

## 1. Goal, scope, target number

### 1.1 Goals
- **Phase A:** `make editor-4c-js` runs the ~1931 plain-DOM tests under `lambda.exe js` and reports **0 failed**, at parity with the jsdom oracle.
- **Phase B:** `make editor-4c-view` runs the Lane B subset (§4.2) under `lambda.exe view` + `event_sim` and reports **0 failed**, exercising every distinct editing interaction end-to-end.

### 1.2 Target number — ~1931 (plain-DOM, React excluded)

The user directive is explicit: **the plain JS DOM version, not React.** The **29 React `.test.tsx`** tests (`editor-view`, `paste-integration`, `render`, `tab-keybinding`) need a React host and are **excluded from both phases**; each already has a plain-DOM twin that *is* in scope (`editor-view-dom` / `full-editor-dom` / `render-vnode` / `tab keybinding via structural-commands`), so no behavioral coverage is lost. React-under-Radiant remains the separately-deferred milestone (4B §3.2).

Corpus reconciliation (verified counts):

| Bucket | Tests | Phase A | Phase B |
|---|---:|:--:|:--:|
| Unit — `commands/*` (10 files) | 124 | ✅ all | subset |
| Unit — `model/*` (6) · `input/*` (1) · `helpers` · `smoke` | 77+8+6+3 = 94 | ✅ all | — |
| Unit — `drawing/*` (5) | 50 | ✅ all | — (Stage 5 scope) |
| Unit — `view/*.ts` (7 plain-DOM) | 62 | ✅ all | subset |
| Tier corpora — Slate 293 · PM 212 · HTML 520 · Chromium 163 · structural 22 · drawing 391 | 1601 | ✅ all | — |
| **React `.test.tsx`** (4) | 29 | ❌ excluded | ❌ |
| **Phase A total** | **≈1931** | | |

(Exact count pinned at Phase 0 when the harness enumerates the suite. 124+94+50+62+1601 = **1931**.)

### 1.3 Non-goals
- React under Radiant (deferred; separate milestone).
- Retiring the vitest/jsdom suite — it **stays** as the fast dev loop and the parity oracle; the Radiant lanes must agree with it.
- A real-*browser* (Chrome/Playwright) run — orthogonal; jsdom remains the Node reference.

---

## 2. Why this matters

The editor is validated only against **jsdom**. Its shipping runtime — LambdaJS + the Radiant DOM/layout/event stack — is proven for a **13-fixture** subset only, so any LambdaJS/`js_dom` divergence from browser semantics is invisible today. Phase A closes the **breadth** gap (does the whole corpus pass on LambdaJS?); Phase B closes the **depth/fidelity** gap (does real interaction work through the full stack?). Every divergence becomes a failing test with a clear owner: engine / `js_dom` / substrate vs. a legitimate editor difference.

---

## 3. Empirical gap analysis (spikes run this session)

Spikes under `./temp/4c-spikes/` against the current `lambda.exe`. **They substantially de-risk Phase A:**

### 3.1 What already works under `lambda.exe js`
| Probe | Result |
|---|---|
| Modern JS (spread, destructuring, classes, generators, `Map`/`Set`, optional chaining, `??`, `structuredClone`, template literals) | ✅ 13/13 |
| **Real editor logic** — `cmdInsertText`/`cmdDeleteBackward`/list-join, `src/` bundled → IIFE via esbuild (36 kB, 12 ms) | ✅ 3/3 assertions green |
| **DOM APIs the view tests use** (`js --document`): `createElement`, `appendChild`, `textContent`, `setAttribute`/`getAttribute`, `classList`, `dataset`, `querySelector(All)`, `getSelection`, `createRange`, `Range.setStart/setEnd/toString`, `selection.removeAllRanges/addRange/rangeCount`, `getBoundingClientRect`, `addEventListener` | ✅ all present |

**Implication:** no language or major-API blocker. The editor engine + DOM surface are already runnable under LambdaJS.

### 3.2 Confirmed gaps
| Gap | Evidence | Nature / phase |
|---|---|---|
| **No in-engine test harness** | vitest can't run under LambdaJS | Phase A: a ~150-line `describe`/`it`/`expect` shim + runner |
| **`fs`/`require`/`module` = undefined** | probe: `typeof fs === 'undefined'` | Phase A: tier corpora load fixtures via Node `fs` (`tier_*/run.test.ts`, `helpers/fixture-runner.ts`) → **inline fixtures at build time** (the one real infra lift) |
| **TS ESM, no loader** | `require`/`module` undefined | Phase A: bundle to a classic **IIFE** (4B constraint) |
| **DOM behavioral fidelity** (APIs present, *semantics* imperfect) | 4B known issues | Phase A (js_dom) + Phase B (full stack): property↔attribute reflection, stale geometry for JS subtrees post-edit, `selectionchange`→nested-CE — §5 |
| **`MutationObserver` = undefined** | probe | **Not needed** — neither `src/` nor the view tests use it (verified) |

---

## 4. The two phases

### 4.1 Phase A — full suite under `lambda.exe js` (breadth)

Run the whole ~1931-test plain-DOM suite headless on LambdaJS. Vehicle: **`lambda.exe js <bundle.js>`** (and/or `js --document <page.html>` for the `view/*.ts` DOM tests), the same headless runner `test_js_gtest` already uses.

**Harness/bundling pipeline** (`tools/build-conformance.mjs`, sibling of `tools/build-dom-page.mjs`):
1. **esbuild** compiles `src/` + the vitest `.test.ts` files (**excluding `*.test.tsx`**) to JS.
2. Swap the vitest imports (`describe`/`it`/`expect`/`vi`) for a **~150-line in-engine shim** that records results and prints `PASS n / FAIL m` + per-failure detail to stdout.
3. Replace Node **`fs`/`path`** fixture reads with **build-time inlined** data (walk the tier fixture dirs → emit `fixtures.generated.ts`) — the one real infra lift (~1601 tier cases depend on it).
4. Bundle to **classic IIFE** module(s) (no `type="module"`). For the ~7 `view/*.ts` DOM tests, run under `js --document <minimal.html>` so `js_dom` is present.
5. `make editor-4c-js`: build → run the bundle(s) → grep the summary → fail on `FAIL != 0`. (May batch via `js-test-batch` for speed; split the corpus across a few bundles if one page strains the retained runtime.)

**What Phase A proves:** the editor's logic + DOM-level behavior are correct on LambdaJS. The ~7 `view/*.ts` tests will surface any `js_dom` *semantic* gaps (property/attr reflection, `Range`/text-node/live-collection edge cases) — fixed root-cause in `js_dom` here (§5).

**Phase A exit:** ~1931 green under `lambda.exe js`; pass set matches the jsdom oracle (parity report).

### 4.2 Phase B — curated subset under `lambda.exe view` + `event_sim` (depth)

Run a representative subset as **event-driven UI automation** through the full Radiant stack — the `test/ui/editor4b` path generalized. This validates what `lambda.exe js` cannot: real event routing (`beforeinput` → intent → command → reconcile → caret), layout-driven geometry, coordinate hit-testing, caret/selection rendering, `selectionchange` delivery, clipboard, IME.

**Initial Lane B pick (~8 files, ~117 tests — covers every distinct editing interaction; expand/reduce later):**

| File | Tests | Interactions exercised end-to-end |
|---|---:|---|
| `view/full-editor-dom.ts` | 14 | toolbar (bold/italic/color, block type), **Tab/Shift-Tab** indent, **paste/copy**, **drag-reorder**, image-resize, gap caret, link popover, table col-resize |
| `view/editor-view-dom.ts` | 9 | `beforeinput` → command → **reconcile** pipeline; mount/render |
| `view/reconcile.ts` | 6 | keyed reconcile + **caret/selection preservation** across edits |
| `commands/text-commands.ts` | 41 | **type**, **Enter** (split), **Shift+Enter**, **Backspace/Delete** (+joins), **Cmd+A**, block type |
| `commands/structural-commands.ts` | 26 | Enter-in-list, list indent/outdent, split/join |
| `commands/caret.ts` | 11 | **Arrow-key** caret navigation |
| `commands/paste.ts` | 10 | **Paste** slices *(candidate to drop — overlaps full-editor-dom)* |
| `commands/input-rules.ts` | 10 | **Markdown autoformat** (typing `- `/`# ` → list/heading) |

**Interaction coverage:** typing ✓ · Enter/Shift+Enter ✓ · Backspace/Delete + joins ✓ · Cmd+A ✓ · arrows ✓ · indent/outdent ✓ · bold/italic/color ✓ · paste/copy ✓ · drag-reorder ✓ · image/link/table ✓ · autoformat ✓ · caret preservation ✓. **Expansion candidates** (add when wanted): `gap-cursor` + `inline-atom` (block/inline atoms), `dom-bridge` (selection↔path mapping), `table-cells` (deeper table ops), IME (via `event_sim ime_compose`; no dedicated file).

**How the command-file cases become event fixtures:** each command test's *(initial doc, operation, expected model)* is re-expressed as *(load/type initial content, drive the real gesture, assert on DOM/selection)*. A generated fixture per case, or a data-driven harness page that the fixture steps through — decided at Phase-B start. The `view/*.ts` files map most directly (they already mount + dispatch).

**Phase B exit:** the subset green under `lambda.exe view` + `event_sim`; `make test-radiant-baseline` unchanged.

---

## 5. DOM-fidelity enhancements (`js_dom` / substrate)

APIs exist (§3.1); **semantics** need hardening. Surfaced by Phase A's `view/*.ts` tests (js_dom) and required by Phase B (full stack):

1. **Property ↔ content-attribute reflection.** JS *property* sets (`el.className`, `el.title`, `classList.toggle`) don't reflect to the content attribute under Radiant, so `[title=…]`/`.is-active` selectors miss (4B worked around editor-side with `setAttribute`). Make property/attribute stay in sync in `js_dom`.
2. **Layout geometry for JS-created subtrees after an edit.** Open 4B follow-up: the Phase-16 incremental-skip (`radiant/layout_block.cpp:4363-4389`) bypasses JS subtrees post-edit → orphaned toolbar geometry → coordinate-resolved `click` misses. Required for Phase B chrome clicks; needs a build+trace to reconcile "incremental-skip" vs. the "full reflow also fails" note before fixing.
3. **`selectionchange` → nested contenteditable.** Proven for *plain* CE, drops for the editor's nested CE (runtime topology; 4B chose an editor-side just-in-time sync over the native fix). Phase B cases relying on live `selectionchange` need the topology fix (retain runtime pointers on `DocState`) or the editor-side sync.
4. **`innerHTML`/parser round-trip fidelity** for `html-parser.test.ts` (already has an `innerHTML`-setter fallback) — verify round-trip parity.
5. **Whatever the `view/*.ts` tests surface** (Range boundary math, `childNodes` live-ness, text-node splitting) — discovered by running them under Phase A, fixed minimally in `js_dom`.

**Principle:** fix root causes in `js_dom`/substrate; do not weaken assertions. A *documented* per-case test-side adaptation (as 4B did for `.title`/`is-active`) is the fallback only where a faithful C++ fix is disproportionate (§11).

## 6. `event_sim` enhancements (Phase B)

`event_sim` is already rich (verified in `radiant/event_sim.cpp`): `type`, `key_press`/`key_down`/`key_up`/**`key_combo`** (modifiers), **`ime_compose`**, **`paste_text`**, `mouse_*`/`drag_and_drop`, `set_editing_selection`, `click`/`dblclick`/`tripleclick`, and assertions `assert_text`/`assert_caret`/`assert_selection`/`assert_editing_selection`/`assert_clipboard`/`assert_value`/`assert_checked`/`assert_visible`/`assert_focus`/`assert_count`. Gaps for the editor subset:

1. **Structured-model assertion.** Vitest asserts on the **serialized doc structure** (tags + marks + nesting); no current assertion covers it (`assert_text` is text-only). Add **`assert_editor_doc`** (compare a page-exposed `editor.serialize()` / `data-doc-json` against `equals`/JSON) and/or **`assert_html`** (compare surface `innerHTML`). Interim: the harness page exposes the serialized model in a DOM node and Lane B uses `assert_text`.
2. **Selection addressed by source-path.** `assert_caret` is view-tree-addressed (`view_type`+`char_offset`); the editor model is source-path-addressed. Add an assertion/harness hook to assert `{path, offset}` in editor terms.
3. **Drag-reorder & image-resize gestures** for `full-editor-dom` — verify `mouse_drag`/`drag_and_drop` produce the editor's expected reorder/resize (may need refinement).
4. **Timer/microtask draining** between an action and its assert — 4B added `js_event_loop_pump_nowait`; verify it suffices for reconcile-then-assert sequences.

---

## 7. Corpus → phase mapping

| vitest group | Tests | Phase A (`js`) | Phase B (`view`+sim) |
|---|---:|:--:|:--:|
| `commands/text-commands` · `structural-commands` · `caret` · `paste` · `input-rules` | 41+26+11+10+10 | ✅ | ✅ (pick) |
| `commands/{gap-cursor,inline-atom,move-node,set-mark,table-cells}` | 26 | ✅ | expansion candidates |
| `model/*` · `input/*` · `helpers` · `smoke` | 94 | ✅ | — |
| `drawing/*` | 50 | ✅ | — (Stage 5) |
| `view/{full-editor-dom,editor-view-dom,reconcile}` | 29 | ✅ | ✅ (pick) |
| `view/{dom-bridge,render-vnode,html-parser,use-editor-state}` | 33 | ✅ | dom-bridge is a candidate |
| tier corpora (Slate/PM/HTML/Chromium/structural/drawing) | 1601 | ✅ | — (data-driven; not gesture-expressible at scale) |
| React `.test.tsx` | 29 | ❌ | ❌ |

---

## 8. Phases & milestones

| Milestone | Deliverable | Exit gate |
|---|---|---|
| **0 — Harness foundation** | `build-conformance.mjs` (bundle + `describe/it/expect` shim + fixture inlining); `make editor-4c-js`; a **10-test slice** green under `lambda.exe js` | Slice green; exact ~1931 count enumerated; pipeline reproducible |
| **A1 — Logic/tier bulk under `js`** | model/commands/input/drawing + 6 tier corpora green under `lambda.exe js` | ≈1869 green; jsdom parity |
| **A2 — `view/*.ts` under `js`** | the 7 plain-DOM view tests green under `js --document`; **`js_dom` fidelity fixes** (§5.1,5.4,5.5) landed | ~1931 green under `js`; parity report clean |
| **B0 — event_sim + geometry prerequisites** | structured-model assertion (§6.1); the JS-subtree geometry fix (§5.2) so chrome clicks land | editor4b still 10/10; new assertions usable |
| **B1 — Lane B subset** | the ~8-file pick as event-driven fixtures under `lambda.exe view` | subset green; `make test-radiant-baseline` unchanged |
| **3 — Integration & parity** | `make editor-4c` (A + B) + a parity report vs. vitest; CI wiring; docs | ~1931 (A) + subset (B) green; 0 undocumented divergences |

**Guard (every milestone):** never regress `make test-radiant-baseline` or the jsdom suite; triage every divergence to a cause (LambdaJS/`js_dom`/substrate **or** a legitimate editor difference) before accepting or fixing it.

---

## 9. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Fixture-inlining bloats the bundle / strains the retained runtime | medium | Split the corpus across a few bundles; the tier data is text and compresses; batch via `js-test-batch` |
| `js_dom` semantic gaps deeper than expected (Range/text-node/live-collection) | medium | Surfaced early by Phase A2's `view/*.ts`; fix root-cause; §11 governs the fallback |
| Geometry/orphaned-subtree (4B follow-up) blocks Phase B chrome clicks | medium | Milestone B0 does the dedicated build+trace before B1 |
| Command-test → event-fixture translation is lossy (model assert vs. gesture) | medium | Use structured-model assertion (§6.1); keep those cases' authoritative form in Phase A; Phase B asserts the observable e2e result |
| Harness drifts from vitest (two sources of truth) | medium | Generate the harness *from* the same `.test.ts` files (compile, don't re-author); parity report is the gate |
| `selectionchange`-nested-CE limits some Phase B cases | low–med | editor-side just-in-time sync (shipped) or the `DocState` runtime-retention fix |
| React-under-`js` temptation | low | Explicitly excluded (§1.2) |

---

## 10. Acceptance criteria

- **Phase A:** `make editor-4c-js` runs the ~1931 plain-DOM tests under `lambda.exe js` → **0 failed**, pass set == jsdom oracle.
- **Phase B:** `make editor-4c-view` runs the Lane B subset under `lambda.exe view` + `event_sim` → **0 failed**, covering every distinct editing interaction (§4.2 checklist).
- Both use the **real LambdaJS/Radiant stack** (no jsdom).
- `make test-radiant-baseline` and the vitest/jsdom suite remain green (no regressions from the `js_dom`/`event_sim`/substrate enhancements).
- A **parity report** cross-checks Radiant vs. jsdom pass sets; matches, or each difference is a recorded, intentional distinction.

---

## 11. Open item (one)

**DOM-fidelity bar (§5).** Default: **fix `js_dom`/substrate to full fidelity** so tests pass unmodified. Fallback where a faithful C++ fix is disproportionate: a **documented** per-case test-side adaptation (as 4B did for `.title`/`is-active`/selection sync). Confirm the default + whether the fallback is permitted.

*(Settled by directive: Phase A vehicle = `lambda.exe js`; target = ~1931 plain-DOM, React excluded; Phase B vehicle = `view` + `event_sim` on the §4.2 subset, expandable later.)*

---

## 12. Summary

Stage 4C promotes the editor's own suite onto the runtime it ships on, in two phases. **Phase A** runs the **whole ~1931-test plain-DOM suite headless under `lambda.exe js`** — cheap breadth that proves the editor's logic + DOM behavior on LambdaJS; spikes show the engine and DOM APIs already work, so the lift is a **harness + fixture-inlining pipeline** plus targeted `js_dom` fidelity fixes. **Phase B** runs a **curated ~8-file interaction subset under `lambda.exe view` + `event_sim`** — slower, high-fidelity depth that proves real end-to-end editing through the full Radiant stack, and is where DOM fidelity and `event_sim` get hardened. React stays out (the separate deferred path). Done when ~1931 pass under `js` (A) and the interaction subset passes under `view` (B), both at parity with the jsdom oracle.
