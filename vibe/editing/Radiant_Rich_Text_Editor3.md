# Radiant Rich Text Editor — Stage 3: WPT Conformance + ProseMirror-Derived Hardening

**Date:** 2026-05-19
**Status:** Proposal
**Builds on:** [Radiant_Rich_Text_Editing.md](Radiant_Rich_Text_Editing.md) (Stage 1 design),
[Radiant_Rich_Text_Editing2.md](Radiant_Rich_Text_Editing2.md) (Stage 2 prototype),
[radiant/Radiant_Design_Selection.md](radiant/Radiant_Design_Selection.md),
[radiant/Radiant_Clipboard_WPT_Status.md](radiant/Radiant_Clipboard_WPT_Status.md)

---

## 0. TL;DR

Two questions drive this proposal.

1. **Is there value in enforcing WPT contenteditable / editing test suites at the DOM
   level?** — **Partial, tiered value.** The selection / DOM-Range / `beforeinput` /
   `InputEvent.getTargetRanges()` / KeyboardEvent / composition layers are a real
   web-platform *contract* our editor sits on, and we should enforce a curated subset
   at 100 %. The `ref/wpt/editing/run/*` `execCommand` corpus, by contrast, codifies
   legacy browser *quirks* that a ProseMirror-class editor deliberately rejects —
   enforcing it as a baseline would be an anti-goal. It belongs on an *informational*
   gauge, never a gate.

2. **Can ProseMirror's test suite be adapted to test the Radiant editor?** —
   **Yes, and it is the higher-value path.** PM's `prosemirror-model` and
   `prosemirror-transform` tests are DOM-free specifications of document-model and
   transaction correctness. We port the *cases and invariants* (not the JS) onto the
   already-shipped `mod_step.ls` / `mod_transaction.ls` / `mod_source_pos.ls` modules,
   via a Lambda re-implementation of PM's tagged-document test builder. PM's
   `prosemirror-view` tests are jsdom-bound — we reuse their *scenarios* as
   `test/ui/*.json` UI-automation, not their code.

The proposal identifies six concrete ProseMirror design primitives the current
Lambda editor modules are missing and that the PM test corpus would immediately
expose (§3) — **and, crucially, presents the Slate.js path-native alternative for
the two of those (§3.1 position mapping, §3.3 schema enforcement) where Slate's
model is the better fit for our parser-sourced, path-based reality** (§3, Part D).
PM gives the step/transaction rigor; Slate gives the path-native position math and
the load-then-normalize model that match how Radiant actually gets its documents.
They are complementary, and Stage 3 takes from both.

**Scope of this stage — get the *local* editing model right.** Stage 3 delivers
and proves the single-user editing model: positions, steps, mapping, schema,
commands, and the web-platform contract. **Collaboration is documented (Part F)
but its sync engine is *not* implemented in Stage 3.** The local model carries
exactly **one** forward-compatibility obligation so collab is a later *capability*,
not a later *rewrite*: **stable node identity from birth + ID-addressed
operations + no assumption of a central server or contiguous integer positions**
(§3.8). That constraint is something §3.1's mapping stability wants anyway, so it
is nearly free now and keeps OT, op-CRDT, *and* decentralized tree-merge all
reachable. The chosen *eventual* direction (Part F) is **decentralized,
offline-first, ID-anchored δ-state tree merge** — not OT (needs a central
linearizer) and not heuristic git-style merge (does not converge across N peers).

---

## 1. Where We Actually Are (verified against the tree)

### 1.1 Editor model — substantially built

The Stage-1/2 modules under [lambda/package/editor/](../lambda/package/editor/) already
implement a credible PM-shaped core:

| Concern | File | Reality |
|---|---|---|
| Doc model (`{kind:'node'…}` / `{kind:'text'…}`) | [mod_doc.ls](../lambda/package/editor/mod_doc.ls) | shipped |
| Positions, `resolve_pos`, `resolve_before/after`, ancestors | [mod_source_pos.ls](../lambda/package/editor/mod_source_pos.ls) | shipped (partial $pos surface — §3.4) |
| 7 step kinds with `apply`/`invert`/`map`/`map_bias` | [mod_step.ls](../lambda/package/editor/mod_step.ls) | shipped (no `Mapping`/`MapResult` — §3.1) |
| Transaction (`tx_begin/step/invert/map_pos`, meta) | [mod_transaction.ls](../lambda/package/editor/mod_transaction.ls) | shipped (no accumulated `Mapping`, no `storedMarks` — §3.5) |
| History, schema (md / commonmark / html5), commands, paste, decorations, collab skeleton | `mod_history.ls`, `mod_md_schema.ls`, `mod_edit_schema.ls`, `mod_commands.ls` (84 KB), `mod_paste.ls`, `mod_html_paste.ls`, `mod_decorations.ls`, `mod_collab.ls` | shipped |
| C↔Lambda position bridge | [radiant/source_pos_bridge.hpp](../radiant/source_pos_bridge.hpp) | header + contract; DOM-side glue still no-ops pending `render_map` path field |

So Stage 3 is **not** "build the editor" — it is "prove it correct and pin its
platform contract."

### 1.2 WPT infrastructure — what exists vs. what the design docs assume

Present on this branch:

- `test/wpt/test_wpt_html_parser_gtest.cpp` — HTML5 WPT parser corpus (in `make
  test-input-baseline`).
- `test/wpt/test_wpt_css_syntax_gtest.cpp` — WPT CSS syntax conformance.
- `ref/wpt/` — a **full WPT checkout** (editing, selection, input-events, uievents,
  clipboard-apis, dom, domparsing, …).
- Selection / DOM-Range conformance is exercised by the **native**
  `test/test_dom_range_gtest.cpp` (a GTest over `radiant/dom_range.*`), *not* by a
  WPT-driven runner.

Referenced by the design docs but **not present on this branch** (target state /
other branch): `test/wpt/test_wpt_selection_gtest.cpp`,
`test/wpt/test_wpt_clipboard_gtest.cpp`, `test/wpt/wpt_testharness_shim.js`. The
Clipboard WPT Status doc describes the *mature pattern* we should copy: a GTest
runner that drives `ref/wpt/<area>/*.html` through a `testharness.js` shim, a
`SKIP_SUBSTRINGS` list with per-bucket rationale, a headline pass matrix, and a
companion `*_WPT_Status.md`. **Stage 3 reuses that exact pattern** for the editor
areas.

---

## 2. Part A — Which WPT API Suites to Enforce

### 2.1 The governing principle

A ProseMirror-class editor owns its DOM. WPT tests split cleanly into two kinds:

- **Contract tests** — pin the *web-platform API surface our editor consumes or
  exposes*: Selection/Range geometry, `selectionchange`, `beforeinput`/`input`,
  `InputEvent.inputType`, `InputEvent.getTargetRanges()`, KeyboardEvent
  `key`/`code`, composition event ordering. These are exactly the seams
  `mod_input_intent.ls`, `source_pos_bridge`, and `dom_range` are built on. **Enforce
  at 100 %.**
- **Quirk tests** — `ref/wpt/editing/run/*` (`bold.html`, `delete.html`,
  `createlink.html`, `indent.html`, …) codify what `document.execCommand` does to
  markup in legacy browsers. PM was built to *escape* this. Passing it would mean
  emulating the very mess we reject. **Never gate; track as an informational
  number only.**

### 2.2 Suite-by-suite recommendation

| WPT area (`ref/wpt/…`) | What it pins | Stage-3 disposition |
|---|---|---|
| `selection/` (root: `setBaseAndExtent`, `addRange`, `extend`, `collapse`, `getRangeAt`, `stringifier*`) | Selection/Range core API | **Tier A — enforce.** Already covered structurally by `test_dom_range_gtest`; migrate the executable subset onto a WPT runner so the *spec text* is the oracle, not our own asserts. |
| `selection/contenteditable/` (`collapse`, `modify*`, `cefalse-on-boundaries`) | Caret/selection behaviour *inside editable hosts* | **Tier A — enforce subset.** Directly tests the §7.4 DOM↔source sync. `modify*.tentative` → informational. |
| `selection/textcontrols/` (`selectionchange`, `selectionchange-bubble`, `focus`) | `selectionchange` firing + bubbling on text controls | **Tier A — enforce.** The `<input>`/`<textarea>` path is the fast path we must not regress. |
| `input-events/` (`input-events-get-target-ranges*`, `input-events-typing`, `input-events-cut-paste`, `*-inputType*`, `idlharness`) | `beforeinput`/`input`, `InputEvent.inputType`, `getTargetRanges()`, `dataTransfer` on paste | **Tier A — enforce the non-`tentative` subset; track `*.tentative` informationally.** This is the precise contract `mod_input_intent.ls` claims to honour — it must be pinned to the spec, not to our own intuition. |
| `uievents/keyboard/` + `uievents/constructors` + `uievents/legacy` | `KeyboardEvent.key`/`code`/`keyCode`, modifier state, event order | **Tier A — enforce keyboard subset.** Underpins the §7.2 keymap and the `event_sim` `key_press`/`key_combo` vocabulary. |
| `editing/event.html` + composition (IME) | `compositionstart/update/end` ordering | **Tier A — but as a *behavioral cross-check*, not the contract.** The real IME *integration contract* is the platform text-input layer `RdTextInputClient` (§3.9) ↔ TSF / `NSTextInputClient` / IBus. WPT pins the observable ordering + "one transaction per session"; the per-platform call sequence is tested via the `ime_compose` driver (§3.9). |
| `dom/ranges/` + `domparsing/` | `Range` mutation, `Selection.toString`, `innerHTML` round-trip | **Tier A — enforce subset.** Mostly already satisfied via `dom_range`; the WPT subset is a cheap regression net. |
| `clipboard-apis/` | Async clipboard, `DataTransfer`, paste sanitisation | **Already Tier A.** 19/19 + 8 documented skips per [Radiant_Clipboard_WPT_Status.md](radiant/Radiant_Clipboard_WPT_Status.md). Editor cut/copy/paste rides this. |
| `editing/run/*` (`bold`, `delete`, `indent`, `createlink`, `forwarddelete`, `backcolor`, …) | Legacy `execCommand` markup quirks | **Tier B — informational gauge only. NEVER a baseline gate.** Report `passed/total` per quarter; each failure is consciously triaged "spec bug to fix" vs. "quirk we intentionally don't emulate." |
| `editing/other/*`, `editing/whitespaces/*`, `selection/caret/*`, `selection/move-by-word-*` | Deletion / caret-navigation / word-boundary *behavioural* scenarios | **Tier C — scenario mining only.** Don't run as WPT. Transcribe the scenarios into our own model tests (§3) and `test/ui/*.json` UI-automation. |

### 2.3 Tiered enforcement model

- **Tier A — Conformance baseline (must be 100 % on the curated subset):**
  `selection/*`, `selection/contenteditable/*`, `selection/textcontrols/*`,
  `input-events/*` (non-tentative), `uievents/keyboard/*` (subset),
  `editing/event.html` + composition, `dom/ranges/*` (subset), `clipboard-apis/*`.
  Wired into `make test-radiant-baseline`.
- **Tier B — Informational gauge (tracked, never gates CI):** `editing/run/*`
  `execCommand` corpus. A single number in the status doc; movement is reviewed,
  not required.
- **Tier C — Scenario source (not executed as WPT):** `editing/other`,
  `editing/whitespaces`, caret/word-boundary tests → mined into Tier-1 model tests
  and UI-automation (§4).

### 2.4 Harness (copy the Clipboard pattern verbatim)

1. `test/wpt/wpt_testharness_shim.js` — already designed/implemented for clipboard;
   extend (not fork) it with the editing/selection helpers
   (`editor-test-utils.js`-style tagged selection, `getTargetRanges` capture).
2. `test/wpt/test_wpt_editor_gtest.cpp` — one GTest binary that discovers Tier-A
   files under `ref/wpt/{selection,input-events,uievents,editing}`, drives each via
   `./lambda.exe view … --headless`, with a documented `SKIP_SUBSTRINGS` list.
3. `vibe/radiant/Radiant_Editor_WPT_Status.md` — the living matrix (headline
   numbers, per-file table, per-bucket skip rationale, Tier-B gauge number), exactly
   like `Radiant_Clipboard_WPT_Status.md`.
4. Makefile: add the Tier-A binary to `test-radiant-baseline`; Tier-B runs in a
   separate non-gating `make wpt-editing-gauge` target.

---

## 3. Part B — ProseMirror Design References to Harden the Current Design

The Stage-1/2 modules are PM-*shaped* but incomplete. Six concrete primitives are
missing; each is something the PM and/or Slate test corpus (§4) will expose
immediately, so this section, §4 and Part D are tightly coupled. For §3.1 and
§3.3, **the Slate.js model is the recommended primary** because our chosen
position model is paths and our documents come from arbitrary parsers — Part D
develops this; the subsections below state the decision and the PM fallback.

### 3.1 Position mapping with deletion tracking — **path-native (Slate), not integer `StepMap` (PM)** *(highest priority)*

**Today:** `step_map(step, pos) → pos` ([mod_step.ls:304](../lambda/package/editor/mod_step.ls))
returns only a position. `mod_transaction.tx_map_pos` re-walks the step list each
call. `mod_collab.mapping` is a bare step list. None can express "this endpoint was
deleted."

**PM model:** `Mappable.mapResult(pos, assoc) → MapResult{pos, deleted,
deletedBefore, deletedAfter, recover}`, with `Mapping` accumulating `StepMap`s
(ordered token ranges over **flat integer positions**) plus mirror tracking so a
forward map inverts for collab rebase.

**Slate model:** `Path.transform(path, op, {affinity}) → path | null` and
`Point.transform(point, op, {affinity}) → point | null`, returning `null` exactly
when the location was deleted (Slate's equivalent of PM's `deleted`). Operations
compose by sequential transform; inversion is per-operation (`Operation.inverse`)
so the accumulated list inverts trivially for rebase.

**Decision: adopt the Slate path-native algebra.** Our `SourcePos` *is*
`{path, offset}` (Stage-1 §4.3, mirrored in
[source_pos_bridge.hpp](../radiant/source_pos_bridge.hpp)). PM's `StepMap` is built
for a flat integer token stream and assumes a uniform open/close model; retrofitting
it onto a heterogeneous path tree (elements/maps/arrays/strings) reintroduces
exactly the impedance mismatch Stage-1 §4.3 rejected. Slate's `Path.transform`
operates *natively* on child-index paths — the same shape we already have — and its
`null`-return is a clean `deleted` signal.

**Recommendation:**

- Add `pos_transform(step, pos, affinity) → {pos, deleted}` to `mod_step.ls`, built
  on a `path_transform(path, step, affinity) → path | null` core (port Slate's
  `Path.transform` cases for insert/remove/move/merge/split, expressed over our
  step kinds).
- Build `mod_mapping.ls` as an **ordered step list with per-step inverse mirrors**
  (Slate-style), exposing `mapping_new/append/map/map_result/invert` — the API
  shape PM's `Mapping` exposes, the implementation Slate's path transform provides.
- Route `tx_map_pos` / `sel_map` / `mod_collab.rebase_steps` through it.
- **Conformance is triple-sourced:** PM's `test-mapping.js` (`!`-suffixed =
  deleted), Slate's `Path.transform`/`Point.transform` fixtures (`null` =
  deleted), **and CodeMirror 6's `ChangeSet`/`mapPos` + changeset-composition
  test suite** (the position-mapping property tested from the text/code side; CM6
  is the editor under Obsidian, Part E.3) must all pass. Three independent oracles
  — integer-step (PM), path (Slate), changeset (CM6) — for the one property the
  model actually runs on; passing all three is the strongest possible signal.
- *PM fallback:* if a path case proves intractable, fall back to a local integer
  `StepMap` scoped to a single text leaf only (never document-wide).

### 3.2 `Slice` with `openStart`/`openEnd` + a fitting algorithm

**Today:** `step_replace`'s `slice` is a flat child list; `step_replace_around`
hand-rolls a single gap. There is no open-depth concept and no
`Fragment.fitsTrivially`/`Fitter` equivalent.

**PM:** `Slice{content, openStart, openEnd}` + `replace.ts`'s fitting algorithm is
what makes split/join across block boundaries, lift/wrap, and HTML-paste coercion
correct.

**Why it matters:** `mod_html_paste.ls` cannot robustly coerce a pasted fragment
into a schema-valid position without open-depth + fitting; backspace-at-block-
boundary join (a §7.2 command) is the same operation. PM's `test-replace.js` /
`test-structure.js` are exactly this and will fail against the flat-slice model.

**Recommendation:** add `slice(content, open_start, open_end)` to `mod_doc.ls`;
re-express `step_replace` as PM's single `ReplaceStep` over a `Slice`; port PM's
`replace`/`fitSlice` algorithm. This *reduces* step-kind count (replace,
replace_around subsume replace_text/add_mark via mark steps).

### 3.3 Schema enforcement — **load-then-normalize (Slate), with PM strictness only at the step boundary**

**Today:** `step_apply` mutates structurally with **no schema check**. The schemas
in `mod_md_schema.ls` / `mod_edit_schema.ls` are declared but not enforced.

**PM model:** every step returns a `StepResult`; `Node.check()` + `ContentMatch`
reject any step that would produce a schema-invalid doc. Invalid states are
*impossible to construct*.

**Slate model:** documents are constructed freely; an idempotent `normalizeNode`
pass runs after every operation batch and *repairs* the tree toward the schema
(unwrap disallowed nesting, merge adjacent same-mark text, lift orphaned inlines,
insert required children).

**Decision: Slate's load-then-normalize is the primary model; PM strictness
applies only to interactive steps.** The reason is structural to Radiant: editor
documents are produced by *arbitrary parsers* — markdown, HTML, LaTeX, wiki, PDF
(Stage-1 §2). Those trees are routinely schema-*dirty* (an `<h2>` containing a
block, inline text directly under `doc`, the very `type=224/160/96` inline-scalar
variants flagged in [Radiant_Rich_Text_Editing2.md §3a](Radiant_Rich_Text_Editing2.md)).
PM's "reject the step" model **cannot even load** such input. Slate's "construct,
then normalize to valid" can.

**Recommendation:**

- Add a `ContentMatch` content-expression matcher over the already-declared schema
  (needed by both models; this is the shared substrate).
- **On load** (`edit_open`): run a Slate-style `normalize_doc(doc, schema)` repair
  pass so any parser output becomes a schema-valid editing document. This also
  retires the §3a `build_dom_tree` inline-scalar blocker by coercing unknown inline
  variants to `text` leaves in one principled place. Structure this as
  **CKEditor-style upcast/downcast converters** (Part E): the parser → Mark-tree
  step is *upcast*, the Mark-tree → `render_map`/DOM step is *downcast*, and
  HTML-paste coercion (Stage-1 §6.3) is just upcast scoped to a fragment — one
  testable conversion layer instead of per-format ad-hoc code.
- **On interactive steps** (`step_apply`): apply PM strictness — return
  `{doc} | {failed: reason}` and reject schema-violating steps, so user edits
  cannot *re-introduce* invalidity. (Normalization is the safety net for *imported*
  data; rejection is the guarantee for *live* edits. The two compose: a rejected
  step never runs, a normalized load is already valid.)
- Conformance: PM `test-content.js`/`test-structure.js` (rejection side) **and**
  Slate normalization fixtures (repair side) must both pass.

### 3.4 Complete the `ResolvedPos` surface

**Today:** `resolve_pos` yields `node/parent/parent_index/depth/ancestors/found`
plus `resolve_before/after`. Missing: `$pos.index(d)`, `$pos.indexAfter(d)`,
`$pos.marks()`, `$pos.sharedDepth(other)`, and crucially `$pos.blockRange(other,
pred) → NodeRange`.

**Why it matters:** `NodeRange`/`blockRange` is the single primitive PM's
`lift`, `wrap`, `wrapInList`, `liftListItem`, `setBlockType`, `join` commands are
written against. `mod_commands.ls` (84 KB) almost certainly re-derives ad-hoc range
logic per command; PM's `test-trans.js` / `test-commands` will expose the
divergences.

**Recommendation:** extend `mod_source_pos.ls` with the missing accessors and a
`NodeRange` constructor; refactor the list/blockquote/setBlockType commands onto
it.

### 3.5 `Transaction.mapping` accumulation + `storedMarks`

**Today:** `tx_step` maps the selection one step at a time but keeps no cumulative
`Mapping`; `tx_map_pos` re-walks. There is no `tr.storedMarks`.

**PM:** `Transaction` carries a `Mapping` (`tr.mapping.appendMap`,
`tr.mapping.maps`) and `storedMarks` so typing at a *collapsed* cursor with an
active-but-not-yet-applied mark (toggle Bold then type) behaves correctly.

**Recommendation:** add `mapping` and `stored_marks` fields to the Transaction
record; `tx_step` appends to the mapping; `mod_commands` toggle-mark-at-cursor
writes `stored_marks`; `mod_input_intent` honours them on the next `insertText`.

### 3.6 Collab — *not built here*; pointer to the deferred design

**Today:** `mod_collab.ls` has a `mapping`/`rebase` skeleton. It is **not extended
in Stage 3.**

**Recommendation:** do **not** port PM's integer `rebaseSteps` and do **not**
build any sync engine now. The eventual direction is **decentralized,
offline-first, ID-anchored δ-state tree merge** — designed in full in **Part F**,
implementation entirely deferred. Stage 3's *only* collab-related work is the
§3.8 local-model constraint (stable `NodeId` + ID-anchored ops + the serialisable
delta encoding for the undo log / autosave). That constraint is what makes the
Part F engine purely additive later; nothing else collab-related belongs in this
stage.

### 3.7 Operations substrate + unified `Transforms` (Slate)

**Today:** selection mapping (`sel_map`) and undo (`tx_invert`) are bolted onto
steps separately; `mod_commands.ls` is 84 KB of individually-written commands.

**Slate model:** 9 normalized low-level operations (`insert_node`, `remove_node`,
`insert_text`, `remove_text`, `set_node`, `split_node`, `merge_node`, `move_node`,
**`set_selection`**) — selection changes are *operations in the same stream*, so
mapping and undo fall out of one mechanism. Above them, a uniform `Transforms.*`
surface (`wrapNodes/unwrapNodes/liftNodes/splitNodes/setNodes/moveNodes`) takes a
`{at, match, mode}` location spec, plus schema-derived overridable predicates
`is_inline` / `is_void` / `is_element`.

**Recommendation (incremental, not a rewrite):**

- Treat the existing step kinds as the operation layer; **add `set_selection` as a
  step kind** so selection transforms through `mod_mapping.ls` (§3.1) like any
  other op instead of via the separate `sel_map` path.
- Derive `is_inline`/`is_void`/`is_element` from the schema as overridable
  predicates (the Stage-1 doc already committed to Slate's plugin-as-function
  model — this is its concrete payoff).
- Refactor the list/blockquote/setBlockType commands in `mod_commands.ls` onto a
  small `Transforms`-style core over `NodeRange` (§3.4), shrinking the command
  surface. Guard each refactor with its ported PM/Slate command scenario (§4).
- Adopt **Lexical's command-priority dispatch** (Part E): commands register at a
  numeric priority and the first handler that returns `true` wins. PM's
  `(state, dispatch?) → bool` chain has *no* priority, so keymap vs. IME vs.
  plugin vs. default contend by registration order — fragile once §7.2 keymap,
  §7.3 composition, and decoration plugins coexist. Make `edit_dispatch` a
  priority-ordered bus (`EDITOR_HIGH/NORMAL/LOW`) while keeping each handler's
  dry-run `(state, dispatch?) → bool` signature (PM) intact.

### 3.8 Collab-readiness — the *one* constraint the local model must honour now

Everything about *syncing* is deferred (Part F). But a decentralized,
offline-first future is only reachable if the **local** model, built in Stage 3,
honours one invariant from day one — retrofitting it later is the "later rewrite"
we are explicitly avoiding.

**The invariant:** *every node has a stable, globally-unique identity assigned at
creation and never reused; operations and durable references address nodes by that
identity; nothing in the model assumes a central linearizer or a single
contiguous integer position space.*

Concretely, in the Stage-3 local model:

- **`NodeId` at construction.** `mod_doc.ls` node/leaf constructors stamp a
  `NodeId = {c: ClientId, n: u32}` (Lamport-style; locally `ClientId` is a fixed
  self-id, e.g. a random per-install constant — no coordination needed offline).
  IDs survive `step_apply` (steps preserve identity except `split` which mints one
  new id, `join` which retires one — both deterministic).
- **Steps stay ID-anchored.** §3.7's normalized ops already address *parent by
  position*; tighten that to *parent by `NodeId` + child index*. This is the
  collapse that makes §3.1 mapping robust **and** is exactly what a future
  tree-merge needs — one change, two payoffs.
- **Durable references are `Anchor`, not raw path.** Selections persisted across
  reload, comment ranges, decorations: `Anchor = {node: NodeId, offset, assoc}`,
  resolved to a live path on demand via §3.1. (Transient in-RAM positions stay
  paths — no churn there.)
- **Serialisable delta, no sync.** A committed transaction serialises to the
  ID-anchored **RTD** delta of Part F.3 (`"rtd/1"`; **RTD = *Radiant Tree
  Delta***, the name coined in this proposal for the path/tree-native op-list
  encoding), and `mod_history.ls` is backed by it. **No merge, no transform, no
  protocol, no version vectors are built in Stage 3** — only the *encoding* and
  the *identity* it presupposes. This is the whole forward-compat surface, and it
  is independently useful now (stable on-disk undo log, autosave, debuggable
  diffs). *(Distinct from the clipboard exchange format — §3.10 — which is Lambda
  **Mark notation**, not RTD: RTD encodes a sequence of operations for
  history/collab; the clipboard carries a static document slice.)*

What is **explicitly NOT** in the Stage-3 local model: peer discovery, version
vectors / causal metadata, the merge function, conflict policy, the move-conflict
resolver, any network. Those are Part F, deferred.

**Dependency order:** 3.1 → (3.2, 3.5, 3.6, 3.7); 3.3 and 3.4 are independent and
can land in parallel. 3.1 + 3.3 + 3.4 are the prerequisites for the §4 test port to
produce meaningful pass/fail signal. 3.7 depends on 3.1 (`set_selection` rides the
mapping) and 3.4 (`NodeRange`). **3.8 is a constraint *on* 3.1/3.7, not a separate
build step** — it is satisfied by making the §3.7 op set ID-anchored and adding
the `NodeId` stamp in `mod_doc.ls`; it adds the serialisable delta encoding but no
sync logic. **3.9 depends on 3.8** (the text-input store is `Anchor`-addressable).

### 3.9 Platform-neutral text-input / IME — `RdTextInputClient`

**Today (verified in-tree):** Radiant already has the *core* of a neutral IME
layer — `te_ime_begin/update/commit/cancel/is_composing`
([radiant/text_edit.hpp](../radiant/text_edit.hpp)), `RDT_EVENT_COMPOSITION_*` +
`CompositionEvent{text, preedit_caret}` ([radiant/event.hpp](../radiant/event.hpp)),
a structured-editor path `radiant_dispatch_rich_composition_event`, and an
`ime_compose` simulation event for tests. But the only real platform backend is
**macOS** ([radiant/ime_mac.mm](../radiant/ime_mac.mm), an `NSTextInputClient`
subclass); Windows and Linux have no IME, and the contract is `<textarea>`-shaped
(`DomElement* elem` + byte offsets), not editor-model-shaped.

**The problem with the §2 plan as written:** it makes WPT `editing/event.html` +
composition the *contract*. But Radiant is **not a browser** — it never receives
`beforeinput`/`compositionstart`. The real native contract is the OS text-input
protocol, and all three are *query-driven*: the IME asks the application for the
selected range, the substring around a range, and the **caret rectangle** (to
place the candidate window). A `compositionstart`-style fire-and-forget event
model cannot answer those.

**Design — one neutral interface, three backends, the editor as the store.**
Define `RdTextInputClient`: the platform-neutral subset common to Windows **TSF
`ITextStoreACP`**, macOS **`NSTextInputClient`**, and Linux **IBus/Fcitx**, expressed
over §3.8 `Anchor` positions (never raw byte offsets):

| `RdTextInputClient` method | TSF | NSTextInputClient | IBus | Backed by |
|---|---|---|---|---|
| `selected_range() → (Anchor, Anchor)` | `GetSelection` | `selectedRange` | preedit anchor | editor selection (§3.7) |
| `text_in_range(a,b) → str` | `GetText` | `attributedSubstringForProposedRange` | surrounding-text | `selection_to_string` (§3.6 src-pos) |
| `replace_range(a,b,str)` | `SetText`/`InsertTextAtSelection` | `insertText:replacementRange:` | commit | `cmd_insert_text` transaction |
| `set_marked(str, sel, repl)` | composition view | `setMarkedText:` | preedit update | transient overlay, **not** a step |
| `unmark()/commit()` | `OnEndComposition` | `unmarkText` | commit | one transaction (Stage-1 §7.3) |
| `caret_rect(a) → Rect` | `GetTextExt` | `firstRectForCharacterRange` | cursor-location | Radiant layout: `Anchor`→box |
| `composing?()` | — | `hasMarkedText` | — | `te_ime_is_composing` |

Key design points:

1. **The composing range is an overlay, never a document step.** Per Stage-1 §7.3
   the whole session is *one* transaction committed on `commit()`; `set_marked`
   only paints a transient decoration (Stage-1 §8) over the `Anchor` range —
   nothing enters history until commit. The existing `te_ime_*` "preedit buffer is
   NOT part of value" rule generalises verbatim from textarea to the structured
   editor.
2. **`caret_rect` is why §3.8 matters here too.** TSF/NSTextInputClient need a
   screen rectangle for an `Anchor`; that is `Anchor → SourcePos → render_map →
   layout box`, reusing the §3.1 bridge and `source_pos_bridge`. No new geometry
   code — it is the selection-rendering path already built for carets.
3. **Backends, not rewrites.** `ime_mac.mm` becomes the `NSTextInputClient`
   implementation of `RdTextInputClient`; add `ime_win.cpp` (TSF `ITextStoreACP`)
   and `ime_linux.cpp` (IBus). The neutral `te_ime_*` core and `ime_compose` test
   driver are unchanged — they become the in-process test backend.
4. **WPT is demoted to a behavioral cross-check, not the contract.** `editing/
   event.html` composition ordering is still run (Tier A) as an *oracle for
   observable behavior* (event order, one-transaction-per-session), but the
   *integration contract* is `RdTextInputClient` ↔ the three OS protocols, tested
   per-platform via the `ime_compose` driver scripted to mirror real TSF/Cocoa/IBus
   call sequences.

**Recommendation:** lift `te_ime_*` into the `RdTextInputClient` interface over
`Anchor`s; keep macOS as backend #1; specify (not necessarily build in Stage 3)
the TSF and IBus backends; rewrite the §2 / phased-plan IME line so the contract
is the platform protocols with WPT as cross-check.

### 3.10 Clipboard exchange — `text/html` + Mark-notation slice sentinel

**Today (verified in-tree):** the *transport* is done — `radiant/clipboard.cpp`'s
`ClipboardStore` is multi-MIME, sanitising (`<script>`/`<style>` stripped), and
WPT-conformant (19/19, [Radiant_Clipboard_WPT_Status.md](radiant/Radiant_Clipboard_WPT_Status.md)).
What is missing is the editor's *representation* strategy. Stage-2 §4.5 currently
copies via `output(slice, 'markdown)` — **lossy** (markdown cannot carry
mark-stacking, attrs, schema role, or block nesting fidelity).

**Design — two representations, one of them dual-purpose.** On copy, write
**exactly two** clipboard representations through the existing `ClipboardStore`
(no new transport code):

1. **`text/plain`** — the slice's text content. Universal degrade path.
2. **`text/html`** — a schema downcast of the slice for cross-app interop
   (browsers, Word, Google Docs), whose **root element carries a sentinel
   attribute `data-mark-slice="…"`** holding the *high-fidelity* internal payload.

The internal payload is **Lambda Mark notation** of the sliced document
subtree — *not* RTD JSON. Rationale: Mark notation is the doc model's own
canonical textual form, so it reuses the existing Mark parser/formatter
(`lambda/format/*`, `lambda/input/*`), is human-debuggable, and needs no separate
JSON schema. RTD encodes *operations* (history/collab); the clipboard carries a
*static document slice* — different problem, different format. (See §3.8 note.)

Slice open-depth (§3.2 `openStart`/`openEnd`, needed so a partial paragraph
pastes as inline content, not a new block) is carried *in* the Mark notation by
wrapping the content in a synthetic root:

```
<mark-slice open-start:1 open-end:1
  "world"                       // e.g. copying inside "Hello world", from offset 6
>
```

`NodeId`s are **omitted** from the serialised slice (the §3.8 fresh-identity rule,
below) — Mark notation simply does not emit them for clipboard.

**Attribute encoding.** Mark notation contains `<>"` etc.; embed it as
**base64url(UTF-8)** in `data-mark-slice` — robust against every HTML
re-serialiser the clipboard may pass through, at the cost of debuggability
(percent-encoding is the documented debuggable alternative). The `ClipboardStore`
HTML sanitiser must **preserve** `data-*` attributes (it already strips only
`<script>`/`<style>`); confirm in the §2 clipboard tests.

**Paste — read priority resolver:**

1. **`data-mark-slice` present and schema-compatible** → base64url-decode → parse
   Mark notation → it *is* the doc-model shape → run the §3.3 normalize/coerce
   pass → insert. Lossless within Radiant.
2. **else `text/html`** → Lambda HTML parser → the §3.3 / Part-E.1 CKEditor-style
   **upcast-over-a-fragment** + schema `normalize` (this *is* `mod_html_paste.ls`,
   not a separate parser).
3. **else `text/plain`** → split into schema default blocks.
4. (image/file → image node — Stage-2-deferred hook.)

**Security.** Clipboard content is untrusted *including a `data-mark-slice` that a
hostile app could forge*. Both the Mark-notation path and the HTML path therefore
run the **§3.3 schema normalize** (drop disallowed elements/attrs) — that, not a
blocklist, is the real filter. `ClipboardStore`'s `<script>`/`<style>` strip stays
as defense-in-depth. Never execute pasted HTML.

**Identity rule (collab-critical, explicit).** **Paste always mints fresh
`NodeId`s**, even when the source is the same document — pasted content is *new*
content. Cut = delete + (paste as new ids). The only identity-preserving relocation
is an internal drag-*move*, which is a `move` op (Part F.3), never a clipboard
round-trip. This prevents duplicate-id divergence in the §3.8 / Part F
decentralized model and is why the slice serialisation omits ids in the first
place.

**Recommendation:** add (a) a slice → `text/html` + `data-mark-slice`(Mark
notation, base64url) encoder, (b) the priority paste resolver above, (c) the
fresh-`NodeId` rule; **replace** the Stage-2 markdown-copy path (markdown becomes
save/export-only); reuse `ClipboardStore` and the existing WPT clipboard
conformance unchanged. The HTML upcast is the §3.3 converter scoped to a
fragment — no new parser.

### 3.11 Input rules — typed markdown shortcuts (Typora-grade UX)

**Today:** there is **no input-rule layer**. `mod_input_intent.ls` maps platform
keys to `InputIntent`s and §3.7 makes `edit_dispatch` a priority bus, but nothing
turns *typed text patterns* into structural commands. Typing `- ` stays literal
text instead of starting a bullet list.

**Why it is its own layer (not just commands):** an input rule fires on a *text
pattern reaching a trigger* (usually a space or newline), must be **atomic and
undoable as one transaction**, and a single Ctrl/Cmd+Z must restore the literal
text (so a user who *wanted* `- ` keeps it). That lifecycle is distinct from a
keymap command; ProseMirror isolates it as `inputRules` for exactly this reason,
and Typora is the UX bar (no mode switch — markup becomes formatting as you type).

**Design — an input-rule stage in the §3.7 pipeline:**

```
platform key → mod_input_intent → [INPUT-RULE STAGE] → command (priority bus) → transaction
```

An input rule is `{ pattern: regex, on_match: (state, captures, range) → Transaction? }`.
On every `insertText` the stage tests the *current block's text up to the caret*
against registered rules in priority order; the first match returns one
transaction (which **replaces the matched literal text** with the structural
edit) and stores an "undo-to-literal" marker so the next backspace/Ctrl-Z reverts
the rule rather than the structure. Rules are **schema-gated**: a rule only fires
if its result is valid at the caret per §3.3 (e.g. `# ` does nothing inside a
`code_block`).

**Default rule set (ships in `mod_input_rules.ls`):**

| Typed (at block start unless noted) | Becomes | Command |
|---|---|---|
| `- ` / `* ` / `+ ` | unordered list item | `cmd_wrap_list('bullet)` |
| `1. ` / `1) ` | ordered list item (start = typed number) | `cmd_wrap_list('ordered)` |
| `[ ] ` / `[x] ` | task-list item (unchecked/checked) | `cmd_wrap_list('task)` |
| `# `…`###### ` | heading level 1–6 | `cmd_set_block_type('heading, {level})` |
| `> ` | blockquote | `cmd_wrap_blockquote` |
| ` ``` ` / ` ```lang ` | fenced code block (lang attr) | `cmd_set_block_type('code_block)` |
| `---` / `***` / `___` (whole line) | thematic break (`hr`) | `cmd_insert_hr` |
| `**x**` / `__x__` (inline, on close) | bold | `cmd_toggle_mark('strong)` over `x` |
| `*x*` / `_x_` (inline, on close) | italic | `cmd_toggle_mark('em)` |
| `` `x` `` (inline, on close) | inline code | `cmd_toggle_mark('code)` |
| `~~x~~` (inline, on close) | strikethrough | `cmd_toggle_mark('strike)` |
| `[text](url)` (inline, on close) | link | `cmd_insert_link(url, null, text)` |

Block rules trigger on the trailing space/newline; inline rules trigger on the
closing delimiter. All are user-overridable Lambda (same plugin-as-function model
as §3.7) and all are **off inside `code_block`** by schema gate.

**Conformance:** PM's `prosemirror-inputrules` cases transcribe directly (§4.4
method) into `test/lambda/editor/input_rules.ls` (+ `.txt`); each asserts (1)
typed sequence → expected doc, (2) immediate Ctrl-Z → literal text restored, (3)
rule suppressed inside `code_block`. Plus a `test/ui/rte_inputrules.json`
UI-automation driving real keystrokes.

**Recommendation:** add `mod_input_rules.ls` + the pipeline stage between
`mod_input_intent` and the §3.7 command bus; ship the table above; gate by §3.3
schema; make undo-to-literal a single transaction.

---

## 4. Part C — Adapting the ProseMirror Test Suite

### 4.1 PM test module → Radiant target mapping

ProseMirror's tests are layered; portability varies sharply.

| PM module / test file | DOM-bound? | Portability | Radiant target |
|---|---|---|---|
| `prosemirror-model` — `test-node`, `test-slice`, `test-fragment`, `test-mark`, `test-content`, `test-resolvedpos` | No | **High** | `mod_doc.ls`, `mod_source_pos.ls`, schema matcher (§3.3/3.4) |
| `prosemirror-transform` — `test-step`, `test-mapping`, `test-replace`, `test-structure`, `test-trans`, `test-mark` | No | **High** | `mod_step.ls`, `mod_mapping.ls` (§3.1), `mod_transaction.ls` |
| `prosemirror-state` — `test-selection`, `test-plugin` | Mostly no | **Medium** | `mod_source_pos.ls` selection, `mod_editor.ls` |
| `prosemirror-commands` — `test-commands` | Partly | **Medium (scenarios)** | `mod_commands.ls` |
| `prosemirror-view` — `test-draw`, `test-domchange`, `test-selection`, `test-paste` | **Yes (jsdom)** | **Low (scenarios only)** | `test/ui/*.json` UI-automation; Tier-A WPT (§2) |
| **Slate** `slate/test/transforms/*`, `interfaces/path`, `interfaces/point`, `normalization/*` | No | **High — and unique** | `path_transform`/`pos_transform` (§3.1), `normalize_doc` (§3.3), `Transforms` core (§3.7) |
| **CodeMirror 6** `@codemirror/state` — `ChangeSet`/`mapPos`/changeset-composition tests | No | **High (mapping only)** | `mod_mapping.ls` (§3.1) — third independent mapping oracle |
| **Yjs / Automerge** randomized convergence harness | No | **Methodology only** | Part F.7 convergence property (deferred) |

The **model + transform** tier (PM) plus the **path-transform + normalization**
tier (Slate) together are the high-ROI target: both are DOM-free executable
specifications of exactly the correctness properties §3 is about. They are
**complementary, not redundant** — PM tests the step/integer-position algebra;
Slate tests the path algebra and the normalization repair pass our model actually
runs. A position-mapping bug that PM's integer cases miss, Slate's path cases catch,
and vice versa.

### 4.2 The keystone: a Lambda tagged-document builder

PM's tests are readable only because of `prosemirror-test-builder`: `doc(p("foo<a>bar"))`
builds a document *and* records tagged positions (`<a>`, `<b>`, `<0>`) that
assertions reference. We replicate this once, in Lambda, emitting the exact
`{kind:'node'…}` / `{kind:'text'…}` shape `mod_doc.ls` uses.

`test/lambda/editor/tb.ls` (new, ~150 LoC):

```lambda
// tb.ls — ProseMirror-style tagged-document builder for editor tests.
// doc(p("foo<a>bar"))  ->  { tree: <doc>, tags: { a: pos([0,0], 3) } }
pub fn doc(...kids)  => build('doc, {}, kids)
pub fn p(...kids)    => build('paragraph, {}, kids)
pub fn h(level, ...kids) => build('heading, {level: level}, kids)
pub fn ul(...kids)   => build('list', {ordered: false}, kids)
pub fn li(...kids)   => build('list_item, {}, kids)
pub fn strong(...k)  => mark('strong, k)
pub fn em(...k)      => mark('em, k)
// "foo<a>bar"  ->  text leaf "foobar" + tag a at offset 3 (UTF-8 byte offset,
// matching mod_source_pos / source_pos_bridge semantics)
```

`<a>`/`<b>` resolve to `mod_source_pos.pos(path, offset)` so PM position
assertions translate to our path+offset model mechanically (PM integer positions
→ our `SourcePath`; the builder does the conversion as it walks). The same builder
also accepts Slate's `<anchor/>`/`<focus/>`/`<cursor/>` markers (Slate-hyperscript
style) — since Slate fixtures are *already* path+offset, they map to our model
with **no conversion at all**, which is exactly why the Slate corpus is the
more direct fit (§3.1, Part D).

### 4.3 The assertion helpers

Port PM's three test idioms:

- `eq(a, b)` → structural doc equality (already available via Lambda `==` on the
  map shape; wrap as `tb_eq` for nice diffs).
- `ist(value)` → boolean assert.
- `testTransform(tr, expected)` — PM's core transform invariant, the most valuable
  thing to port. For every ported step/transform case:
  1. `step_apply(step, doc)` **eq** `expected`.
  2. `step_invert(step, doc)` applied to the result **eq** original `doc`
     (round-trip).
  3. every tagged position maps through `pos_transform`/`Mapping` to the position
     the case asserts, **including** the `deleted` signal — PM cases assert it as
     `!`, Slate cases assert it as a `null` return; the harness checks both
     spellings against the one `{pos, deleted}` result (this is what forces §3.1).

### 4.4 Where the cases come from

PM's `prosemirror-transform/test/*.js` and `prosemirror-model/test/*.js` encode
hundreds of `(input-tagged-doc, step, expected-tagged-doc, mapped-positions)`
tuples. Slate's `slate/test/` (and `slate-hyperscript` JSX fixtures) encode
`(input-doc, operation, expected-doc)` and `Path.transform`/`Point.transform`
tuples plus `normalization/*` repair fixtures — a corpus PM **cannot** provide
because PM has no path model. Neither Slate nor PM source is vendored. We
**transcribe the tuples** from both into Lambda data, because the JS is the spec
and the tuples are the corpus. Two delivery shapes,
per the project's testing conventions:

1. **`.ls` integration fixtures** under `test/lambda/editor/` (e.g.
   `step_replace.ls`, `step_mapping.ls`, `structure_lift_wrap.ls`,
   `resolvedpos.ls`), **each with a matching `.txt` expected file** (CLAUDE.md
   rule 8). Auto-discovered by the existing Lambda integration runner.
2. **A GTest driver** `test/test_editor_transform_gtest.cpp` that executes the
   `.ls` corpus headlessly and reports per-case pass/fail, added to
   `make test-radiant-baseline` once §3.1/3.3/3.4 land.

### 4.5 What explicitly NOT to port

- `prosemirror-view` code: jsdom-bound, no analogue in Radiant. Instead, take its
  *scenario list* ("type at boundary X → expect model Y", "paste HTML Z →
  expect coerced doc", "DOM mutation observed → expected transaction") and express
  each as a `test/ui/rte_*.json` UI-automation script (Stage-2 already established
  this vocabulary) plus a Tier-A WPT file where one exists.
- `editing/run/*` execCommand expectations (Tier B, §2) — never an oracle for our
  model.

### 4.6 Coverage goal

| Layer | Source | Target pass bar |
|---|---|---|
| Model (node/fragment/slice/mark/content/resolvedpos) | PM `prosemirror-model` cases | 100 % of ported subset |
| Transform (step/mapping/replace/structure/trans/mark) | PM `prosemirror-transform` cases | 100 % of ported subset |
| Path/Point transform + deletion (`null`) | Slate `interfaces/path`, `interfaces/point`, `transforms/*` | 100 % of ported subset |
| Normalization / schema repair | Slate `normalization/*` fixtures | 100 % of ported subset |
| Commands | PM `prosemirror-commands` + Slate `Transforms` + `editing/other` (Tier C) scenarios | 100 % as UI-automation |
| Selection / input contract | Tier-A WPT (§2) | 100 % of curated subset |
| Legacy execCommand | Tier-B WPT gauge | tracked, no bar |

---

## Part D — ProseMirror vs. Slate.js: what to take from each

Both are mature; they make opposite bets on three axes. Radiant's existing
constraints (path-based positions chosen in Stage-1 §4.3; documents sourced from
arbitrary parsers; an immutable Mark tree; a `render_map` reconciler that is
*not* a DOM observer) determine which bet fits where. This section is the
rationale behind the §3.1 / §3.3 / §3.7 decisions.

| Axis | ProseMirror | Slate.js | Radiant fit |
|---|---|---|---|
| **Position model** | Flat integer offsets over an open/close token stream | `Path` (child-index list) + offset | **Slate.** Our `SourcePos` is already `{path, offset}`; PM's integer `StepMap` would reintroduce the impedance mismatch Stage-1 §4.3 explicitly rejected. |
| **Position mapping** | `Mapping`/`StepMap`/`MapResult.deleted`/`recover` | `Path.transform`/`Point.transform` → `null` on delete | **Slate algebra, PM API shape.** Implement path-native (Slate); expose the `Mapping`-style accumulating API (PM) so undo and `tx_map_pos` have a clean seam. (§3.1) |
| **Schema** | Invalid states impossible at construction (`StepResult` rejection) | Construct freely, `normalizeNode` repairs | **Both, split by origin.** Normalize on *load* (parser output is dirty) — Slate; reject on *interactive step* (don't let edits re-break it) — PM. (§3.3) |
| **Edit primitive** | Steps; selection mapped separately | 9 operations incl. `set_selection` in the same stream | **Slate.** Add `set_selection` as a step kind so selection rides `mod_mapping.ls` instead of the separate `sel_map`. (§3.7) |
| **Command surface** | Per-command functions `(state, dispatch?) → bool` | Uniform `Transforms.*` with `{at, match, mode}` | **Both.** Keep PM's dry-run `(state, dispatch?) → bool` probe (drives menu/keymap active state — already used in Stage-2 toolbar `edit_can_exec`); implement them *over* a Slate-style `Transforms` core to shrink the 84 KB `mod_commands.ls`. (§3.7) |
| **Plugins** | `PluginSpec` (state fields, props, view) | Editor-as-function: override `normalizeNode`/`isInline`/`isVoid` by composition | **Slate** for predicates/normalization (Stage-1 already chose this); **PM** decoration sets for non-document overlays (`mod_decorations.ls` already PM-shaped). |
| **Collab** | First-class but central-server OT (TP1) | Op-CRDT possible, less canonical | **Neither — and deferred.** Decentralized requirement rules out PM's central OT; we take only the *identity* idea and design a separate decentralized **ID-anchored δ-state tree merge** (Part F), built later, not here. |
| **View/reconcile** | `EditorView` + `DOMObserver` reads DOM mutations back | Controlled React render; DOM is pure projection | **Lexical's reconciler model fits best** (immutable state → diff → minimal mutation), which is what Radiant's `render_map` already is. Do *not* port any view layer; take Lexical's command-priority bus (Part E) and reuse PM/Slate view *scenarios* as UI-automation (§4.5). |
| **Test corpus** | `model` + `transform` (integer/step algebra) | `path`/`point` transform + `normalization` (path algebra, repair) | **Both — complementary.** PM can't test paths; Slate can't test the integer step algebra. Port both (§4). |

**One-line synthesis:** *PM contributes the transaction/step rigor, the dry-run
command protocol, and decorations; Slate contributes the path-native position
math, the load-then-normalize schema model, the selection-as-operation substrate,
and the unified `Transforms` surface; collab is neither's — a deferred,
decentralized ID-anchored tree merge (Part F). Stage 3 is PM's discipline
expressed over Slate's path model, built collab-ready but single-user.*

---

## Part E — Wider landscape: what CKEditor 5 and Lexical contribute

PM and Slate remain the primary models. Two further systems each contribute **one**
architectural idea that neither PM nor Slate isolates as cleanly, and that maps
directly onto an existing Radiant seam.

### E.1 CKEditor 5 — the model↔view *conversion pipeline*

CKEditor separates a model document from its view via explicit, registered
**converters**: *upcast* (external/DOM → model) and *downcast* (model → view),
with HTML paste handled as upcast over a fragment.

Radiant already has this boundary, just not named or factored as one layer:

| CKEditor concept | Radiant equivalent |
|---|---|
| Upcast (DOM/data → model) | `lambda/input/*` parser → Mark tree |
| Downcast (model → view) | Mark tree → `render_map` → Radiant DOM (`view`/`edit` templates) |
| Paste/upcast over a fragment + schema filtering | Stage-1 §6.3 HTML-paste schema coercion |
| Data downcast vs. editing downcast | `view <x>` vs. `edit <x>` templates (Stage-1 §6.2) |

**Takeaway (feeds §3.3):** treat conversion as a *first-class, testable layer* with
a uniform converter registry keyed by tag/role, instead of per-format ad-hoc code.
`normalize_doc` (Slate, §3.3) is then "upcast + repair," and paste coercion is the
same code path scoped to a fragment. One conversion layer, one test surface.

### E.2 Lexical — immutable state + keyed reconciler + command priority

Lexical's architecture: an **immutable `EditorState`**, nodes addressed by a stable
**`NodeKey`** (neither integers nor paths), a **reconciler** that diffs successive
states to minimal DOM mutations, and a **command bus where handlers register at a
numeric priority** and the first to return `true` wins.

Two of these are already Radiant:

- Immutable state → reconciler → minimal mutation **is** the immutable Mark tree +
  `render_map` two-phase reconcile. Validation: Lexical's reconciler model is the
  closest external precedent for `render_map`; cite it, don't port it.
- Stable `NodeKey` is the seed for **Part F's node identity** (RTD `NodeId`).

The genuinely new idea worth adopting is **command priority** — see §3.7. PM's
command chain has no priority; once keymap, IME composition, decoration plugins,
and defaults all want the same key, registration order is too fragile. Lexical's
`COMMAND_PRIORITY_{LOW,NORMAL,HIGH,EDITOR}` bus solves exactly this and is a small,
local change to `edit_dispatch`.

### E.3 Native desktop precedents (prior art — validation, not new design)

Radiant is a *native* engine, so the mature **desktop** editor frameworks are
direct prior art. None changes the design; each independently confirms a primitive
the proposal already commits to, and two imply a future conformance tier.

**Document-model frameworks — consensus validation of the model split.**

| Framework | What it independently confirms |
|---|---|
| **Cocoa TextKit 2** — `NSTextContentStorage` / `NSTextLayoutManager` / `NSTextElement` over `NSAttributedString` | Apple's own re-derivation of the *content / layout / view* separation = Radiant's source-tree / layout / `render_map` split. Cite as precedent the way §E.2 cites Lexical's reconciler. |
| **GTK `GtkTextBuffer`** — `GtkTextMark` with **left/right gravity** | `GtkTextMark` is a position that *survives edits with a bias* — exactly §3.1 `Anchor.assoc` / PM association. Battle-tested confirmation that the §3.1/§3.8 anchor primitive is correct, not novel. |
| **Qt `QTextDocument` / `QTextCursor`** — cursor = position+anchor, methods, built-in undo stack | API-shape reference for the Stage-1 §9 public surface; validates "edits expressed through a cursor/selection object over an undoable document." |
| **Scintilla / piece table** (orig. MS Word data structure) | A data-structure option *iff* large-document edit perf becomes a measured problem. Secondary — Radiant's model is a Mark tree, not a text buffer — but the reference is logged so the option isn't re-discovered later. |

Takeaway: the model/layout/view split, the stable-anchor primitive, and the
cursor-over-undoable-document API are the **consensus of every mature native
editor**. The proposal is on the well-trodden path; these are citations, not
action items.

**Formats & accessibility — prior art + one future tier.**

- **Interchange formats:** RTF, Microsoft **TOM** (`ITextDocument`/`ITextRange`),
  and the ISO document standards **ODF** / **OOXML** are the desktop rich-text
  *interchange* standards. Radiant already round-trips formats via
  `lambda/format/*`; these are export-target prior art, **low marginal value** —
  noted so "should we invent a format?" is answered (no: the model is the Mark
  tree, interchange is the existing formatters, and the collab/undo wire form is
  the §3.8 `"rtd/1"` delta).
- **OpenOffice / LibreOffice — *not* an editing-model source; a conditional
  format-corpus.** LibreOffice's editing tests (`sw/qa/`, `qadevOOo`) are bound to
  the `SwDoc`/`SwPaM` cursor model and UNO API — **not portable** as
  model/transform fixtures (PM/Slate/CM6 already cover that, DOM-free and
  transform-shaped). Its genuine strength is the **ODF / DOCX / RTF round-trip
  corpus** (`sw/qa/extras/{ooxmlexport,ooxmlimport,rtfexport,…}`, thousands of
  real-world documents with import/export assertions). **Conditional value:** *if
  Radiant ever adds ODF or DOCX import/export*, that corpus is the best external
  conformance set for **those specific formatters** (transcribe scenarios, not
  vendor code — same method as the PM/Slate ports; MPL/LGPL makes fixture reuse
  fine). It is **out of Stage-3 scope** and is **not** an editing-model corpus —
  logged here only so the option is not re-discovered or mistaken for model-test
  work later. *Change-tracking contrast (future note):* LibreOffice weaves
  redlines invasively into the `SwDoc` core; Radiant should instead derive
  tracked-changes from the RTD op-log + decorations (Stage-1 §8) — no core-model
  change — if/when change-tracking is requested.
- **Native accessible-text APIs — a legitimate future conformance tier.**
  `IAccessible2`/`IAccessibleText` (Windows), `ATK`/`AT-SPI` (Linux),
  `NSAccessibility` (macOS), unified by **AccessKit**. The §2 WPT tiers pin the
  *web* a11y contract; a native editor *also* owes the *desktop* a11y text
  contract (expose caret, ranges, text attributes, selection changes to assistive
  tech). This is **out of Stage-3 scope** but is recorded here as a **"Tier D —
  native accessible-text conformance"** that parallels the WPT tiers, to be
  designed alongside the §3.9 platform backends (the same `Anchor`-addressable
  store that feeds TSF/NSTextInputClient also feeds `IAccessibleText`/AccessKit —
  one store, two consumers).

**Modern WYSIWYG / markdown editors — design lessons (not test suites).** None is
open enough to import a corpus from; each contributes one design validation or
borrowed UX.

| App | Editing model | Lesson for Radiant |
|---|---|---|
| **Notion** | Every block has a stable, *user-visible* ID; document = blocks with `content:[childId]` (adjacency, not deep nesting); sync ships **per-block records, not a tree diff** | Strongly validates §3.8 (`NodeId` is not just a merge key — it is a product feature: block links, "copy link to block", comment/backlink anchors) **and** Part F.4 (per-node keyed δ-merge over git-style tree-diff is what a shipping decentralized block editor actually does). Also: block-with-childIds is the natural answer to Stage-1's open *embeds / transclusion* question. |
| **Typora** | Seamless markdown WYSIWYG: typing markdown syntax transforms to formatted output inline, **no mode switch**, document stays markdown | The gold-standard **input-rule** UX. Directly motivates **§3.11** (input rules). PM's `inputRules` is the implementation precedent; Typora is the UX bar. |
| **Obsidian** | Markdown text *is* the model; "live preview" is a **CodeMirror 6 decoration layer** that hides syntax except on the caret's line; no separate rich model | Radiant chose model-first (opposite), but the *technique* is valuable because Radiant has *both* source format and model: an optional "show raw markup at caret" mode implemented purely via decorations (Stage-1 §8), **no model change**. Also why CM6's changeset suite is a §3.1 oracle (Part C). |
| **Typst** | Not WYSIWYG — compile-based; source-text editing + **memoized incremental recompilation** (`comemo`): re-evaluate only content whose inputs changed | *Not* an editing-model source. The single transferable idea is the memoized incremental-recompile model as a **validating precedent for `render_map`'s dirty-tracking** (and a reference if finer-grained incrementalism is ever needed). Logged so it is not mistaken for an editing reference. |

Takeaway: Notion validates the identity decisions (§3.8, F.4); Typora supplies
the only genuinely missing *design* element → §3.11; Obsidian gives a future
decoration-only "raw-at-caret" mode and motivates the CM6 test oracle; Typst is a
`render_map` precedent only.

---

## Part F — Decentralized offline tree-merge collaboration (design only; **deferred, not built in Stage 3**)

**Status:** DESIGN SKETCH. **Nothing in this part is implemented in Stage 3.** The
*only* Stage-3 obligation is §3.8 (stable `NodeId` + ID-anchored ops + the
serialisable delta *encoding*, with **no** merge/sync logic). This part records
*why* that single constraint is sufficient and what the eventual engine looks
like, so the local model is built collab-ready and the future work is additive.

### F.1 Requirements (from the product direction)

1. **Decentralized — no central server.** No authority linearises edits. Rules out
   classic OT (OT's tractable form, TP1-only, *requires* a central linearizer;
   the server-free form needs TP2, which is notoriously fragile).
2. **Offline-first.** A peer edits arbitrarily long while disconnected; its local
   doc is always immediately usable.
3. **Sync on reconnect.** When peers meet (pairwise, intermittently, any topology)
   they exchange deltas-since-last-common-state and **converge** — same final
   document on every replica, regardless of sync order or which pairs met when.
4. **Tree/path-native.** Operate on the Mark tree, not a flattened buffer.

### F.2 Why "tree diff/merge" — and the honest caveat

The chosen mental model is **git-for-trees**: keep a common ancestor, diff each
divergent branch, 3-way merge. Two facts must be stated plainly:

- **Anonymous-tree diff/merge does not work for a live editor.** Tree-edit-distance
  diffing (Zhang–Shasha, O(n³)) is *heuristic* about node identity — a retyped
  paragraph reads as delete+insert, mis-merging silently. And a plain 3-way tree
  merge is **not convergent** across N peers syncing pairwise: merge-of-merges is
  order-dependent unless the merge is associative + commutative.
- **Stable node identity removes both problems.** With §3.8's `NodeId`, "diff"
  degenerates to a trivial *keyed* comparison (per id: present? parent? index?
  attrs? text?), and the merge can be made a deterministic per-id rule.

The honest consequence: *a tree merge that provably converges for N offline peers
is, mathematically, a **state-based (δ) CRDT** — a join on a lattice — just
expressed as state-merge rather than op-integration.* We embrace that. We are not
adopting Yjs/YATA op-CRDT (heavier per-character causal metadata) and not adopting
OT (needs a server). We are adopting the **simplest CRDT family, presented as the
tree diff/merge the product wants**: ID-anchored δ-state merge with an explicit,
small conflict-policy table.

### F.3 Identity, causal metadata, and the delta

- **`NodeId`** (from §3.8, already in the local model): `{c: ClientId, n: u32}`,
  `ClientId` = random per-install constant (no coordination, works offline).
- **Causal metadata (added only when the sync engine is built, *not* in Stage 3):**
  each replica keeps a **version vector** `VV: ClientId → u32`. Every op carries
  its origin `{ClientId, lamport}`. This is what makes "what changed since we last
  met" exact and order-independent.
- **The delta is the §3.8 / Part-F.4 ID-anchored op list** — the *same* `"rtd/1"`
  encoding the local undo log already uses. A sync payload is
  `{ from_vv, to_vv, ops:[Op…] }` = exactly the ops the receiving peer has not
  seen, by version-vector difference. No snapshots shipped in the common case.

Op set (unchanged from the local model — this is the point of §3.8):

| Op | JSON | Notes for merge |
|---|---|---|
| insert node | `{t:"in", p:NodeId, ref:NodeId?, side, node:NodeJSON}` | ordered by RGA-style `ref` sibling, not raw index |
| delete node | `{t:"dn", id:NodeId}` | id-addressed; tombstone, not index |
| insert text | `{t:"it", leaf:NodeId, ref:CharId?, s:str}` | per-leaf sequence CRDT (the *only* sequence point) |
| delete text | `{t:"dt", leaf:NodeId, ids:[CharId…]}` | id-addressed chars |
| set attr | `{t:"sa", id:NodeId, k, v, lamport}` | **LWW per (id,k)** by lamport, ClientId tiebreak |
| set mark | `{t:"mk", leaf:NodeId, range, mark, add, lamport}` | LWW per (leaf,mark,char) |
| move node | `{t:"mv", id:NodeId, newParent:NodeId, ref:NodeId?, lamport}` | **Kleppmann move rule** (F.5) |

Note the shift from raw `index` to `ref:NodeId/CharId` ordering: that is the one
representational refinement the *eventual* engine needs. **Stage 3 may keep the
index form locally**; the converter to the `ref` form is mechanical and additive,
*provided* identity exists — which §3.8 guarantees. (This is the single thing to
keep honest: §3.8 must stamp ids; index↔ref is then a later, safe transform.)

### F.4 Merge = deterministic join, by op class

Sync applies the peer's unseen ops; convergence is guaranteed because every op
class has a **commutative, associative, idempotent** resolution:

- **Structure (insert/delete node):** id-addressed; siblings ordered by an
  RGA/Logoot sequence keyed on `NodeId` → concurrent inserts at the "same place"
  get a total order from `(lamport, ClientId)`; delete is a tombstone (GC only
  when causally stable across all known peers).
- **Text within a leaf:** a small per-leaf sequence CRDT (RGA) on `CharId`s. This
  is the *only* place a sequence CRDT is needed, and it is contained to one text
  leaf — not the whole document.
- **Attributes / marks:** last-writer-wins per `(id, key)` by Lamport timestamp,
  `ClientId` as deterministic tiebreak. (Marks may instead use add-wins/remove-wins
  sets if "both bolded" should survive — a policy choice, tabulated, not hardcoded.)
- **The conflict-policy table is the product surface.** Everything resolvable by
  LWW/tombstone is automatic; anything the product wants surfaced (e.g. concurrent
  edits to the same sentence) is raised as a *decoration* (Stage-1 §8), never a
  blocking modal.

### F.5 The one genuinely hard case: concurrent moves

Concurrent `move` can create cycles (A→under B on peer 1; B→under A on peer 2) or
duplicate a subtree. This is *the* known-hard decentralized-tree problem. **Do not
invent a rule** — adopt Kleppmann, Gomes, Mulligan & Beresford,
*"A highly-available move operation for replicated trees"* (2021): each move
records the old parent; on merge, moves are applied in `(lamport, ClientId)` order
and any move that would introduce a cycle is **skipped** (its effect is null,
deterministically, on every replica). Provably convergent, no coordination. This
single citation closes the only open structural-merge question.

### F.6 Sync protocol (peer-to-peer, gossip-friendly)

No server, no global version. Pairwise exchange, any transport (libuv socket,
file, USB — transport-agnostic by design):

| Step | A → B |
|---|---|
| 1 | `hello { vv_A }` |
| 2 | B computes `missing = ops B has that A's vv doesn't cover`; replies `delta { from: vv_A, ops, vv_B }` |
| 3 | A merges (F.4/F.5), updates `vv_A ⊔ vv_B`; symmetrically sends A's `missing` to B |
| 4 | both replicas now equal for the union of seen history; **converged** |

Idempotent and order-independent: re-running, or syncing through a third peer
first, yields the same state (the lattice-join property). Offline = unbounded
local op log + growing local `vv`; reconnect = one delta exchange. No "rebase
loop", no central `V`, no in-flight/pending state machine (those were the *OT*
model — explicitly dropped).

### F.7 Convergence testing — adopt the Yjs / Automerge methodology

When this engine is eventually built, its correctness test is **not** an
example-fixture corpus — it is the **randomized convergence property** that the
mature CRDT libraries pioneered:

> Generate a random schema-valid document; fork it to *N* replicas; apply
> independent random op sequences to each; sync them pairwise in a **random
> order / random topology** (including syncing through a third replica first);
> assert **every replica ends byte-identical and schema-valid**, and that the
> result is invariant under sync order.

This is exactly **Yjs's and Automerge's own test harness design** (randomized
concurrent ops + all-pairs/all-orders convergence), and it is the named precedent
for this section — not a vague "fuzz it." Adopt their methodology directly:
property-based generator + a frozen seed corpus for regressions, run by the §4
GTest driver. The Kleppmann move rule (F.5) and the per-op-class joins (F.4) are
*defined* to make this property hold; the test is what proves they do. (Deferred
with the rest of Part F — listed here so the eventual test plan is not
re-invented.)

### F.8 Awareness (presence)

Unchanged from good practice: cursors/selections travel on a **separate ephemeral
channel**, never in the op log, never merged, never undoable — `{ClientId,
anchor:Anchor, head:Anchor, ttl}` where `Anchor = {node:NodeId, offset, assoc}`,
re-resolved locally via §3.1 mapping. Presence loss on disconnect is expected and
fine.

### F.9 What this means for Stage 3 (the only actionable part)

**Build now (it is §3.8, nothing more):** `NodeId` stamped at node creation;
ops/anchors addressed by `NodeId`; the `"rtd/1"` JSON delta encoding as the undo
log / autosave format; no central, no integer-global-position assumption.

**Explicitly deferred (this entire Part F engine):** version vectors, the
index→`ref` ordering refinement, the per-leaf text RGA, the LWW/tombstone merge,
the Kleppmann move resolver, the gossip protocol, awareness transport.

**The guarantee:** because identity exists from day one, every deferred item above
is *additive* — no change to the local editing model, the step set, the schema, or
the test corpus is required to add collaboration later. That is the entire purpose
of admitting §3.8 now and nothing else.

---

## 5. Phased Plan

| Phase | Scope | Exit criterion |
|---|---|---|
| **S3.1 — Test builder + harness** | `test/lambda/editor/tb.ls` + `tb_eq`/`ist`/`testTransform`; `test_editor_transform_gtest.cpp` skeleton. | A hand-written `step_replace_text` round-trip case passes end-to-end. |
| **S3.2 — Model port** | Port `prosemirror-model` `test-node/fragment/slice/mark/resolvedpos` cases; land §3.4 (`ResolvedPos` surface, `NodeRange`). | Ported model corpus 100 %. |
| **S3.3 — Path-native mapping** | Implement §3.1 the **Slate** way (`path_transform`/`pos_transform` → `deleted`, `mod_mapping.ls` with inverse mirrors); route `tx_map_pos`/`sel_map`/collab through it. Port **both** PM `test-mapping.js` and Slate `path`/`point` transform fixtures. | Both corpora 100 % incl. PM `!`-deleted and Slate `null`-deleted. |
| **S3.4 — Slice + schema (normalize + reject)** | §3.2 (`Slice` + fitting); §3.3 `ContentMatch` + Slate `normalize_doc` on load + PM rejection on step. Port `test-replace`/`test-structure`/`test-content`/`test-trans` **and** Slate `normalization/*`. Retires the §3a `build_dom_tree` inline-scalar blocker. | Transform corpus + Slate normalization 100 %; dirty parser input loads valid; invalid live steps rejected. |
| **S3.5 — WPT Tier A: selection** | `wpt_testharness_shim.js` editing extensions; runner over `selection/*`, `selection/contenteditable/*`, `selection/textcontrols/*`, `dom/ranges` subset; `Radiant_Editor_WPT_Status.md`. | Curated selection subset 100 %; status doc published. |
| **S3.6 — Input contract: WPT Tier A + `RdTextInputClient`** | Runner over `input-events/*` (non-tentative), `uievents/keyboard` subset, `editing/event.html` composition (behavioral cross-check). Land §3.5 (`storedMarks`) so typing-after-toggle matches `input-events-typing`. Land §3.9: lift `te_ime_*` into the `RdTextInputClient` interface over `Anchor`s; macOS (`ime_mac.mm`) as backend #1; TSF + IBus backends *specified*; `ime_compose` driver scripted to mirror real TSF/Cocoa/IBus call sequences. | WPT input/keyboard/composition subset 100 %; `RdTextInputClient` neutral interface landed with macOS backend + `ime_compose` per-protocol sequence tests green; TSF/IBus backends specified. |
| **S3.7 — Operations substrate + commands** | §3.7: add `set_selection` step kind so selection rides `mod_mapping.ls`; derive `is_inline`/`is_void`/`is_element` from schema; refactor list/blockquote/setBlockType onto a Slate-style `Transforms` core over `NodeRange`; make `edit_dispatch` a **Lexical-style priority bus** (Part E.2). **§3.11:** add `mod_input_rules.ls` + the input-rule pipeline stage with the default markdown-shortcut table (`- `→bullet, `# `→heading, `**x**`→bold, …), schema-gated, undo-to-literal in one transaction. Port `prosemirror-commands` + `prosemirror-inputrules` + Slate `Transforms` + `editing/other` (Tier C) scenarios as `test/lambda/editor/*.ls` + `test/ui/rte_*.json`. | Command + input-rule UI-automation green; `mod_commands.ls` surface shrunk; keymap/IME/plugin contention resolved by priority; `make test-radiant-baseline` green with Tier A added. |
| **S3.8 — Conversion layer + clipboard + collab-ready local model** | Refactor parser↔tree↔render_map into a **CKEditor-style upcast/downcast converter registry** (Part E.1); land **§3.8 only**: `NodeId` stamped in `mod_doc.ls`, §3.7 ops + anchors made ID-addressed, the `"rtd/1"` ID-anchored RTD delta *encoding*, `mod_history.ls` re-based onto it. **§3.10 clipboard:** slice → `text/html` + `data-mark-slice`(Mark notation, base64url) encoder + priority paste resolver + fresh-`NodeId` rule; **delete the Stage-2 markdown-copy path**; extend `rte_clipboard.json`. Non-gating `make wpt-editing-gauge` over `editing/run/*`. | History/undo is RTD-delta-backed and round-trips 100 %; one conversion layer with its own test surface; copy/paste round-trips losslessly within Radiant and degrades to clean HTML/plain-text cross-app; WPT clipboard still 19/19; **no merge/sync code** — Part F engine is explicitly *not* started. |

Dependency: S3.1→S3.2→S3.3→S3.4 (model/transform spine); S3.5/S3.6 (WPT) depend
only on S3.1's harness conventions and can run parallel to S3.2–S3.4; S3.7 needs
S3.4 + S3.5/3.6; **S3.8 needs S3.1 (invertible mapping) + S3.3 (schema check) +
S3.7 (op set)**. The **decentralized tree-merge engine (Part F.3–F.7) is out of
Stage-3 scope entirely** — design-documented only; Stage 3 ends at the
collab-*ready* local model (§3.8), not collab itself.

---

## 6. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| PM uses flat integer positions; we use path+offset. Cases may not transcribe 1:1. | The tagged-builder (§4.2) does the integer→path conversion *once*, at build time, exactly where `source_pos_bridge.hpp` already says the boundary is. Assertions then compare in our model. |
| Enforcing the wrong WPT subset locks us to execCommand quirks. | Hard tier split (§2.3): `editing/run/*` is Tier B, **never** in `test-radiant-baseline`. Each Tier-A skip carries a documented rationale, per the Clipboard-status precedent. |
| §3.2 `Slice`/fitting is a large, correctness-critical refactor. | It *reduces* step-kind surface and is the same primitive backspace-join and paste need anyway. Gate it behind the ported `test-replace`/`test-structure` corpus — the refactor is "done" iff the corpus is green. |
| `mod_commands.ls` (84 KB) may have re-derived range logic that resists `NodeRange` refactor. | S3.7 refactors incrementally per command, each guarded by its ported PM command scenario; no big-bang rewrite. |
| WPT shim drift between clipboard and editor areas. | Extend the single existing `test/wpt/wpt_testharness_shim.js`; do not fork. Composition/selection helpers are additive. |
| Headless IME/composition has no real platform events. | Drive composition via the `event_sim` vocabulary against `editing/event.html`'s recorded ordering as the oracle (Tier A), exactly as clipboard synthesises Cmd+C/V. |

---

## 7. Acceptance Criteria

- `make test-radiant-baseline` green **with** the Tier-A WPT editor binary and the
  ported PM model+transform **and** Slate path/normalization corpora added.
- `vibe/radiant/Radiant_Editor_WPT_Status.md` exists with: headline numbers,
  per-file Tier-A matrix, documented `SKIP_SUBSTRINGS` rationale, Tier-B gauge
  number.
- The ported `prosemirror-model` + `prosemirror-transform` **and** Slate
  `path`/`point`-transform case subsets pass 100 %, including both PM `!`-deleted
  and Slate `null`-deleted expectations (proves §3.1 path-native mapping).
- A schema-dirty parser document (e.g. the §3a markdown inline-scalar case) loads
  through `edit_open` into a schema-valid editing doc via `normalize_doc`, **and** a
  schema-invalid *interactive* step is rejected rather than producing a malformed
  doc (proves §3.3 normalize-on-load + reject-on-step).
- The parser↔tree↔`render_map` path is a single CKEditor-style upcast/downcast
  converter registry with its own test surface (proves Part E.1), and command
  dispatch resolves keymap/IME/plugin contention by Lexical-style priority
  (proves Part E.2).
- **Collab-readiness (§3.8) — Stage-3 mandatory, but bounded:** every node carries
  a stable `NodeId` from creation; the §3.7 op set and durable `Anchor`s are
  ID-addressed; a committed transaction serialises to the `"rtd/1"` ID-anchored
  JSON delta and `mod_history.ls` round-trips through it 100 %. **No merge,
  transform, version vector, or protocol exists** — the Part F engine is
  design-documented and explicitly *not* implemented. The acceptance test is:
  *can the entire Part F decentralized engine be added later with zero changes to
  the step set, schema, position model, or test corpus?* — demonstrated by the
  ID-anchored delta encoding alone.
- **Platform-neutral IME (§3.9):** `te_ime_*` is lifted into a single
  `RdTextInputClient` interface expressed over §3.8 `Anchor`s; the macOS
  `NSTextInputClient` backend (`ime_mac.mm`) drives it; the `ime_compose` test
  driver, scripted to mirror real TSF / Cocoa / IBus query+commit sequences,
  passes per-protocol; WPT `editing/event.html` composition passes as the
  behavioral cross-check; TSF and IBus backends are *specified* (not necessarily
  built). `caret_rect` resolves `Anchor → render_map → layout box` with no new
  geometry code.
- **Prior-art & future tiers recorded (Part E.3):** the design doc names TextKit 2
  / GtkTextMark / QTextCursor as validating precedents and logs a future **Tier D —
  native accessible-text conformance** (IAccessible2/ATK/NSAccessibility via
  AccessKit) parallel to the WPT tiers, fed by the same `Anchor`-addressable store
  as §3.9. (Documentation criterion, no Stage-3 implementation.)
- **Clipboard exchange (§3.10):** copy writes exactly `text/plain` + `text/html`,
  the latter carrying `data-mark-slice` = base64url Lambda **Mark notation** of the
  slice (with `open-start`/`open-end`, no `NodeId`s). Paste resolves
  slice → sanitised HTML upcast → plain text; pasted nodes get **fresh
  `NodeId`s**. Radiant→Radiant copy/paste is lossless; paste into a browser/Word
  yields clean HTML; the Stage-2 `output(slice,'markdown)` copy path is **removed**
  (markdown is save/export-only); `ClipboardStore` and WPT clipboard (19/19) are
  unchanged and `data-*` survives the sanitiser.
- **Input rules (§3.11):** `mod_input_rules.ls` + pipeline stage shipped with the
  default table; ported `prosemirror-inputrules` cases pass, each asserting
  typed-sequence→doc, immediate-undo→literal-text-restored, and
  suppressed-inside-`code_block`; `test/ui/rte_inputrules.json` drives real
  keystrokes (`- `→bullet, `# `→heading, `**x**`→bold, fenced code, `---`→hr,
  `[text](url)`→link) green.
- **Mapping is triple-sourced (§3.1):** PM `test-mapping` + Slate
  `Path/Point.transform` + **CodeMirror 6 `ChangeSet`** corpora all pass; Part F.7
  records the Yjs/Automerge randomized-convergence methodology (deferred).
- `make wpt-editing-gauge` runs `editing/run/*` and reports a tracked, non-gating
  pass count.

When met, the Radiant editor's *document-model correctness* is pinned by both
ProseMirror's step/transform corpus and Slate's path-transform + normalization
corpus (PM's discipline expressed over Slate's path model); its *conversion
pipeline* is a single CKEditor-style upcast/downcast layer and its *dispatch* a
Lexical-style priority bus; its *collaboration future* is held open — not built —
by a single local-model constraint (§3.8: stable identity + ID-anchored ops),
with the decentralized offline tree-merge engine designed in Part F and deferred
in full; its *text-input contract* is a platform-neutral `RdTextInputClient`
(§3.9) over the OS protocols (TSF / `NSTextInputClient` / IBus), with WPT
composition demoted to a behavioral cross-check; its *model* is validated by the
native-editor consensus (TextKit 2 / GtkTextMark / QTextCursor, Part E.3) with a
future native-accessibility tier logged; its *clipboard exchange* is `text/html` +
a `data-mark-slice` Mark-notation sentinel (§3.10) — lossless within Radiant,
clean HTML outward; and its *web-platform contract* (selection, beforeinput,
keyboard, clipboard) is pinned by the curated WPT subset — while the legacy
`execCommand` quirk corpus is consciously held at arm's length as an
informational gauge, not a target.
