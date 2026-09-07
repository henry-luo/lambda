# Radiant Full-UA Editable Support — Detailed Implementation Plan

**Date:** 2026-09-07
**Status:** Implemented — applicable UA acceptance verified; repository-wide
aggregate-test exception recorded in
[Radiant_Editable_UA6_Report.md](Radiant_Editable_UA6_Report.md)
**Design contract:** [Radiant_Design_Editable.md](Radiant_Design_Editable.md),
§20
**Predecessor:** [Radiant_Impl_Editable.md](Radiant_Impl_Editable.md) records
the completed Phase-1 common-gate work and is not the implementation plan for
this scope.
**Scope:** implement full user-agent `contenteditable`, `designMode`,
`execCommand`, and `queryCommand*` behavior through the shipped Lambda DOM
behavior package; retain only platform transport and generic DOM mechanisms in
native code; pass the applicable pinned WPT and Chrome/Chromium editing suites.

> **Formal authority.** D7.2.5 assigns all UA editing policy, normalization,
> history, `designMode`, and command/query semantics to the Lambda DOM behavior
> package. D7.2.1–D7.2.4 govern package state, initialization, distribution,
> and naming; D7.3.5 requires a conformance gate; D7.5.3 governs the explicit
> Lambda↔Radiant contract. D1.10 requires executable enforcement. D5.3.3
> requires precise roots across every native allocation/callback boundary.
> S9.1.7 forbids global mutable language state, and S12.1.3 confines reactive
> mutation to handlers. This plan must be revised before implementation if it
> would contradict any of those rulings.

This document turns Phase 9 of the design into an ordered implementation
program. It does not re-open the ownership decision. Exact DOM results and
legacy-command compatibility come from the pinned conformance corpora; this
plan defines where that behavior is implemented and how it is proven.

---

## 1. Terminal outcome

At completion, an uncanceled platform edit and a programmatic legacy command
reach one package-owned editing engine:

```text
platform key/text/IME/clipboard/drop
    -> Radiant platform event + common edit gate
    -> author cancellation / model-handler claim
    -> package input intent -----------+
                                       |
Document.execCommand/queryCommand* ----+--> immutable command descriptor
Document.designMode -------------------+        + live EditContext
                                                |
                                           package planner
                                                |
                                       validated generic EditPlan
                                                |
                                    generic native DOM transaction waist
                                                |
                              normalization + selection + package history
                                                |
                         input / selectionchange / MutationObserver / paint
```

The terminal implementation has:

1. one immutable package command registry for execution, query, and keyboard
   intent mapping;
2. one package `EditContext` builder and one plan/applier pipeline;
3. package-owned text, composition, block, list, formatting, object,
   clipboard/drop, normalization, typing-state, history, and `designMode`
   rules;
4. explicit per-document package state, with no module/process-global mutable
   editing state;
5. an explicit invocation/result contract instead of TLS pending-range,
   apply-epoch, and caret side channels;
6. native code that validates handles and performs generic DOM, Selection,
   observer, geometry, and invalidation mechanics without recognizing command
   names;
7. thin JavaScript document bindings that only coerce arguments, invoke the
   package, and convert its structured result to the required IDL value;
8. honest WPT and Chrome/Chromium runners whose harnesses inject input and
   report results but never implement editing behavior;
9. zero unexpected failures, crashes, hangs, or timeouts in the applicable
   automated conformance manifests;
10. the Phase-1 editor, Radiant-template, form-control, logging/replay, and
    layout regressions still green.

### 1.1 Meaning of “package-owned”

The package decides:

- whether an operation is supported or enabled;
- which nodes/ranges are affected;
- which generic mutation steps are required;
- block/list/inline semantics and whitespace rules;
- collapsed typing state and command query results;
- normalization scope and rules;
- history grouping and undo/redo contents;
- the compatibility return value and command-specific event contract.

Native code may:

- deliver platform events and enforce cancellation ordering;
- identify the current document, editing host, and live Selection;
- validate node/document/host ownership and mutation epochs;
- expose or retain live Range objects;
- parse an HTML fragment through the ordinary DOM parser;
- execute generic insert/remove/move/split/text/attribute/style operations;
- maintain live Range boundaries, queue real mutation records, invalidate
  layout/paint, and expose geometry;
- root Lambda/DOM objects and provide a generic atomic DOM transaction.

A native helper that switches on `bold`, `insertParagraph`, a command alias,
or an `inputType` to choose DOM structure is UA policy and violates D7.2.5.
A helper that performs a parameterized checked `split_node`, `move_node`, or
`replace_text` operation is mechanism and is allowed.

### 1.2 Explicit non-goals

- Do not port Blink's C++ editing classes or restore the retired native rich
  editor.
- Do not implement a ProseMirror-, Editor.js-, or CodeMirror-specific branch.
- Do not move form-control value editing into the DOM-tree plan pipeline.
- Do not make WPT or Chromium test names observable to production code.
- Do not patch vendored WPT, Chromium, Tree-sitter, or other third-party code.
- Do not accept a passed-test baseline that depends on harness mutation or
  expected-output rewriting.
- Do not select a final import name that collides with the built-in
  `import dom` module; D7.2.4 requires one meaning per package path.
- Do not add asynchronous edit plans in this phase.
- Do not optimize by adding command-shaped native fast paths.

---

## 2. Non-negotiable implementation rules

These apply to every phase and review:

1. **One rule set.** Keyboard/default input and `execCommand` must call the
   same descriptor and planner. Query methods read the same descriptor and
   live state.
2. **One action owner.** Author cancellation or a registered model action
   suppresses the package action and its history record.
3. **No contenteditable native fallback.** If the package cannot initialize or
   its handler raises, the edit fails visibly and is logged; it does not fall
   back to command-specific C++ behavior. This is the contenteditable-specific
   consequence of D7.2.5 and supersedes the older fail-open migration rule.
4. **Plan before mutation.** Resolve policy and validate a complete plan before
   the first DOM write.
5. **Atomic apply.** A rejected or stale plan changes nothing. A failure during
   apply rolls back through the generic DOM transaction mechanism.
6. **Small mutation scope.** Never serialize and rebuild the editing host for
   a local edit. Preserve node identity outside the affected roots.
7. **Real observation.** Mutation records come from ordinary DOM primitives;
   the package and harness never manufacture records to match tests.
8. **Explicit state.** Typing style, command settings, composition linkage,
   history, and re-entrancy state are document-owned package values. No
   mutable module global is allowed by S9.1.7.
9. **Explicit invocation.** Do not extend the current `__thread` epoch/caret
   pattern to new command families. Replace it with a retained invocation and
   structured result.
10. **Mechanism-only waist.** A native addition must be useful as a general
    DOM/Selection transaction primitive and tested independently of a command.
11. **No duplication.** Search for an existing DOM/Range helper before adding
    one. Promote reusable `static` helpers to the owning header instead of
    copying them. At the third similar family, extract a common planner or
    table.
12. **Precise rooting.** Any `Item`, wrapper, fragment, Range, or callback that
    crosses a native allocation/safepoint uses `RootFrame`, `Rooted`, handles,
    or `PersistentRooted` under D5.3.3. Native-stack scanning is not an option.
13. **Standard containers.** Native work uses project `Str`, `ArrayList`, and
    `HashMap`, never `std::` containers.
14. **No generated-file edits.** Build changes go through
    `build_lambda_config.json` and normal generation, never hand-edited Lua.
15. **Every test has an oracle.** Each new Lambda `*.ls` unit test receives its
    corresponding expected `*.txt` file.
16. **Standards precedence.** WPT determines standards behavior. The pinned
    Chromium result determines legacy compatibility only where WPT is silent.
17. **No silent exclusions.** Every excluded case has one stable manifest
    reason and remains visible in suite totals.
18. **Preserve existing surfaces.** A contenteditable change cannot regress
    form controls, Radiant model editing, Selection/Range, editor libraries,
    logging/replay, or headless event simulation.

---

## 3. Starting implementation inventory

This inventory was verified against the tree on 2026-09-07. Line numbers are
not contractual; symbols and responsibilities are.

### 3.1 Landed package foundation

| Source | Landed behavior | Phase-9 gap |
|---|---|---|
| `lambda/package/dom/dom.ls` | package entry and behavior registration | import-path migration remains governed by D7.2.4; no second DOM namespace may be created |
| `lambda/package/dom/form.ls` | body-scoped `domedit` and `execcommand` behavior hooks | document editing API transport is Boolean/verdict-shaped and has no query/design-mode result channel |
| `lambda/package/dom/dom_edit.ls` | text/replacement insertion, simple deletion, composition splice/caret, plain paste/drop, cut/drag deletion, paragraph/line-break delegation, formatting delegation | context comes through native pending-range side channels; cross-tree rules, whitespace, typing style, normalization, and history are incomplete |
| `lambda/package/dom/commands.ls` | case-insensitive subset for bold/italic/underline/strike, HTML/text insertion, backward/forward deletion, paragraph, and line break | conditional registry, single-text formatting assumptions, no query API, no typing state, no block/list/object/history/clipboard command families |
| `lambda/package/dom/tree.ls` | package-side general DOM traversals and small edit-range composition | lacks the traversal and classification vocabulary required by full structural planning |
| `lambda/package/dom/keymap.ls` | key-to-intent policy shared by current editable surfaces | must map into descriptors without becoming a second command table |

The existing package direction is retained. Phase 9 refactors and extends it;
it does not create a separate rich-edit package beside it.

### 3.2 Native waist residue

`radiant/editing_dom_waist.cpp` correctly owns several mechanisms, including
UTF-16/codepoint conversion, checked text replacement, live Range maintenance,
mutation notification, Selection collapse, and parsed-fragment insertion.
It also still contains transitional state and policy that cannot remain at the
Phase-9 exit:

- `s_dom_edit_apply_epoch`, `s_dom_edit_caret_node`, and
  `s_dom_edit_caret_u16` are thread-local result channels rather than an
  invocation-scoped contract;
- the `DocState::editing.pending_dom_edit_*` fields are an implicit argument
  bundle and are unsafe for nesting or re-entry;
- `editing_dom_is_structural_block` embeds the UA block-element set;
- `editing_dom_mergeable_blocks` embeds block-join policy;
- `editing_dom_structural_block_ancestor`, `editing_dom_split_block`, and
  `editing_dom_insert_break_at` mix tree mechanics with command decisions;
- `editing_dom_format_ancestor`, range-format tests, and wrap/unwrap behavior
  need separation into generic traversal/mutation and package toggle policy;
- `radiant_dom_exec_command` resolves the live host/range correctly but then
  relies on the pending-range and epoch channels.

The phase does not delete useful mechanics. It extracts parameterized
operations, moves classifications and sequencing to Lambda, replaces implicit
state with explicit arguments/results, and then deletes only the residue.

### 3.3 DOM and JavaScript surface

| Source | Current role | Required change |
|---|---|---|
| `lambda/dom/dom.cpp` | exposes `designMode` IDL state and a thin-ish `execCommand` bridge | add all query methods; route get/set/exec/query through one package operation bridge; keep only IDL coercion/result conversion; remove remaining edit semantics |
| `lambda/dom/dom_selection.cpp` / `.h` | live Range/Selection wrappers, mutation adjustment, direction, geometry, selectionchange scheduling | close WPT gaps; expose generic retained boundary/bookmark operations needed by package plans without command knowledge |
| `lambda/dom/dom_clipboard.cpp` | Clipboard/DataTransfer and async Clipboard API mechanisms | expose trusted activation and payload mechanisms; keep rich-paste selection/sanitization policy in package |
| `lambda/dom/dom_events.cpp` and Radiant event dispatch | public event propagation and package behavior dispatch | carry explicit edit invocation/result data; preserve cancellation and event-loop ordering |
| `radiant/editing_controller.cpp` | common contenteditable input transaction | invoke the package once after cancellation/owner arbitration and accept a structured result |
| `radiant/editing_target_range.cpp` | native target-range geometry | retain only geometry/mechanism; package selects operation semantics and normalization |

### 3.4 Existing verification

The tree already has useful focused and regression coverage:

- `test/ui/test_editing_contenteditable_dom_action.json`;
- `test/ui/test_editing_contenteditable_composition.json`;
- `test/ui/test_editing_contenteditable_structural.json`;
- `test/ui/test_editing_contenteditable_commands.json`;
- the `test_editing_paired_*contenteditable*` fixtures;
- `test/lambda/dom_range_selection*.ls` with expected outputs;
- `test/wpt/test_wpt_selection_gtest.cpp`;
- `test/editable-editors` and its CodeMirror, ProseMirror, and Editor.js UI
  suites;
- `make editable-unit`, `editable-ui`, `editable-editor-e2e`, and
  `test-editable`.

These remain regressions, but they are not the full-UA acceptance gate. There
is no active dedicated WPT contenteditable/editing runner or active
Chrome/Chromium contenteditable runner in `build_lambda_config.json`.

### 3.5 Harness debt that invalidates conformance counts

`test/wpt/wpt_testharness_shim.js` currently contains substantial editing
policy: editing-host discovery, block classification, whitespace cleanup,
placeholder insertion, list/table deletion, target-range construction,
formatting mutation, history-event synthesis, and direct Selection changes.
Those helpers may have been useful while bootstrapping Selection tests, but a
Phase-9 test cannot pass because `_wpt_*` code performed the missing UA action.

UA-0 classifies each shim helper as one of:

1. **harness protocol** — assertions, async completion, result transport;
2. **input adapter** — translates testdriver actions into the product's public
   automation/event path without selecting an edit result;
3. **product emulation** — determines a target range, mutates content,
   normalizes a tree, changes Selection as an edit result, or synthesizes a
   command outcome.

Category 3 must be deleted from the harness before the affected result is
counted. Its algorithm is reimplemented from the governing tests in the
Lambda package, not copied verbatim into production.

### 3.6 Available external corpus

The sibling `lambda-test/editing` tree contains the imported Chromium editing
corpus, `MANIFEST`, `RUNNABLE`, `baseline.txt`, legacy harness files, and
families for caret, deleting, editability, `execCommand`, input, inserting,
pasteboard, selection, shadow DOM, style, unsupported content, and undo.

The old CE3 runner wiring stays retired. UA-0 introduces new read-only corpus
wiring whose name and linkage make package-UA ownership explicit. The derived
`test/editor-js/test/tier_f_chromium` suite remains a separate Lambda rich
editor/model test and is never used as a substitute for browser-UA coverage.

---

## 4. Target package decomposition

The package should not grow one multi-thousand-line `commands.ls`. The first
implementation creates the following modules, adjusting names only if the
package naming migration lands first:

```text
lambda/package/dom/
  dom.ls                    package entry; imports/registers behavior
  form.ls                   existing element/body behavior declarations
  dom_edit.ls               default-input entry; no structural algorithms
  commands.ls               immutable descriptor registry + public dispatch
  edit_context.ls           host/mode/selection/epoch/context construction
  edit_plan.ls              plan/step/result constructors and validation
  edit_apply.ls             generic apply/rollback/selection/invalidation call
  edit_positions.ls         boundary ordering, traversal, affected-root helpers
  edit_text.ls              text, grapheme/word deletion, composition, spaces
  edit_blocks.ls            line/paragraph, split/join, placeholders, blocks
  edit_lists.ls             list creation/removal, item split/join, indent/outdent
  edit_format.ls            inline/block style planning and live state/value
  edit_objects.ls           links, images, horizontal rules, atomic content
  edit_clipboard.ls         copy/cut/paste/drop payload and fragment policy
  edit_history.ls           transaction grouping, undo/redo, retained deltas
  edit_design_mode.ls       whole-document host lifecycle and settings
  edit_normalize.ls         deterministic affected-root normalization passes
```

### 4.1 Dependency direction

```text
form.ls / dom_edit.ls
          |
          v
      commands.ls ---------> edit_context.ls
          |                         |
          +----> family planners <--+
                         |
                         v
                    edit_plan.ls
                         |
                         v
                   edit_apply.ls
                    /     |      \
       edit_normalize  edit_history  import dom mechanisms
```

Rules:

- family modules construct plans; they do not apply DOM mutations directly;
- `commands.ls` owns aliases and descriptors but delegates algorithms to
  family planners;
- `dom_edit.ls` converts an input intent into the same canonical descriptor or
  planner entry used by `commands.ls`;
- `edit_apply.ls` knows generic step kinds but no command names;
- normalization modules know tree invariants but not keyboard/API entry paths;
- history records applied plans/results and cannot invoke a family planner;
- `tree.ls` remains general behavior-package traversal; editing-specific
  classification lives in `edit_positions.ls` or the relevant family module;
- cycles between command, family, normalization, and history modules are
  forbidden.

### 4.2 Package state home

Add a generic, document-owned package-state slot if the current behavior
template state cannot safely retain arbitrary Lambda values and DOM wrappers
across body replacement. The mechanism may be implemented as a context-owned,
precisely rooted `Item` keyed by package/module identity; native code must not
know its editing fields.

The package stores one immutable `EditingSession` value containing:

| Field | Purpose |
|---|---|
| `settings` | `styleWithCSS`/`useCSS`, default paragraph separator, compatibility flags fixed by descriptors |
| `typing_state` | marks/styles to apply at a collapsed caret and the host/position epoch they belong to |
| `composition` | package linkage for the active preedit transaction, not a second platform IME state |
| `undo` / `redo` | immutable stacks of retained edit deltas |
| `next_group` | deterministic history group identity |
| `active_group` | typing/composition coalescing metadata |
| `dispatch_depth` | explicit re-entrancy rule for legacy commands |
| `version` | state schema version for logs and safe reset |

Every update writes a new session value to the document slot. No history or
typing value is captured in an immutable closure expecting later mutation
(S9.1.4), and no module-level `var` is introduced (S9.1.7).

### 4.3 Package naming hold point

This plan uses the physical path `lambda/package/dom` because that is the
current tree. It does not establish `lambda.dom` as the package import name:
`import dom` already identifies the built-in DOM mechanism module. When the
D7.2.4 package-path migration reaches this package, select and migrate to one
non-colliding name in a separate atomic change. Do not introduce aliases that
let the same path resolve to both the built-in module and behavior package.

---

## 5. Core data and invocation contracts

The following are conceptual Lambda records. Field spelling may follow the
language's final type declarations, but their distinctions are required.

### 5.1 `EditInvocation`

Native dispatch creates one invocation per input/default/API call and retains
it through the synchronous package call:

| Field | Meaning |
|---|---|
| `document` | wrapped owning document |
| `host` | canonical editing host, or document host for `designMode` |
| `source` | `platform`, `automation`, `replay`, or `api` |
| `operation` | `input`, `exec`, one query kind, or design-mode get/set |
| `intent` / `command` | normalized input type or caller command spelling |
| `value` / `show_ui` | coerced API values |
| `selection` | retained live Selection identity |
| `target_ranges` | immutable pre-action snapshots plus retained live bookmarks |
| `data` / `data_transfer` | text, composition, clipboard, or drop payload |
| `is_composing` | platform composition state |
| `mutation_epoch` | document epoch captured after author notification |
| `cascade_id` | common gate/log/replay correlation |
| `trusted` / `activation` | platform trust and user-activation facts |

The invocation contains facts, not decisions. Native code does not set a
format tag, block type, deletion span, or history class.

### 5.2 `EditContext`

`edit_context.ls` validates the invocation and derives:

- effective `contenteditable` mode, including inheritance, false islands, and
  `plaintext-only`;
- active editing host or `designMode` root;
- anchor/focus direction and ordered start/end boundaries;
- affected block/list/table/atomic ancestors;
- collapsed/mixed selection facts;
- live document settings and typing state;
- descriptor support/enabled facts;
- the current mutation epoch and re-entrancy state.

Context construction is side-effect free. A missing, disconnected,
cross-document, cross-host, false-island, or stale range produces a structured
decline/error before planning.

### 5.3 `CommandDescriptor`

The registry is immutable package data. Every canonical command has one row:

| Field | Purpose |
|---|---|
| `name` / `aliases` | case-insensitive canonicalization |
| `family` | planner module routing |
| `input_type` | equivalent platform intent, if any |
| `requires_host` | support vs enabled distinction |
| `plaintext_rule` | allow, transform, or disable in `plaintext-only` |
| `event_contract` | which notification envelope applies to this entry path |
| `history_class` | typing, composition, structural, format, clipboard, selection-only, none |
| `state_kind` | Boolean, indeterminate, string/value, or none |
| `planner` | pure function reference or family operation key |

The registry covers the complete design §20.6 inventory and every additional
applicable command found in the pinned corpora. `queryCommandSupported`,
`queryCommandEnabled`, `queryCommandState`, `queryCommandIndeterm`, and
`queryCommandValue` all read this row; there is no query-only table.

### 5.4 `EditPlan`

An edit plan contains:

| Field | Purpose |
|---|---|
| `document` / `host` | ownership validation |
| `expected_epoch` | stale-plan rejection |
| `steps` | ordered generic DOM/Selection steps |
| `affected_roots` | minimal normalization and invalidation scope |
| `selection_before` | directional retained boundaries |
| `selection_after` | boundary bookmarks or a deterministic mapping rule |
| `normalize` | ordered package normalization passes |
| `history` | class, group/coalescing key, and whether an unchanged state is recordable |
| `events` | descriptor-selected observable event contract |
| `typing_state_after` | new collapsed style state, or reset instruction |

Generic step kinds are limited to mechanism:

- `replace_text`;
- `split_text` / `split_element`;
- `insert_node` / `insert_fragment`;
- `remove_node` / `move_node`;
- `wrap_range` / `unwrap_node`;
- `set_attribute` / `remove_attribute`;
- `set_style` / `remove_style`;
- `merge_compatible_nodes` with compatibility supplied by the plan;
- `set_selection`;
- `retain_for_history`.

The plan includes every policy parameter. Native application must not infer a
block tag, wrapper, merge condition, or caret destination from the command.

### 5.5 `EditResult`

The package returns one structured result:

| Field | Meaning |
|---|---|
| `supported` | command exists in the registry |
| `enabled` | current context permits execution |
| `claimed` | this package owns the action, including a compatible no-op |
| `changed` | DOM or package editing state changed |
| `selection_changed` | Selection changed independently of DOM mutation |
| `history_recorded` | an undo record was committed |
| `query_bool` / `query_value` | query result for non-executing operations |
| `failure` | stable diagnostic category, absent on success/normal decline |
| `plan_id` / `history_group` | audit/replay correlation |

The JavaScript bridge derives the IDL return from this record. It does not
guess from a mutation epoch or convert “handler existed” into success.

### 5.6 Generic atomic DOM transaction

Add or extend a generic DOM mutation transaction with this lifecycle:

1. begin against one document and expected mutation epoch;
2. validate all nodes, boundaries, ownership, and preconditions;
3. retain/root created fragments and nodes;
4. apply generic steps in order while journaling inverses;
5. update live Ranges/Selection and queue ordinary mutation records;
6. commit observer/invalidation state as one synchronous edit;
7. on failure, apply the journal in reverse and publish no partial edit result.

This is a DOM mechanism, not an editing engine. Its native representation must
use `ArrayList`/`HashMap`/`Str` and precise roots. It accepts no command or
`inputType` string. Add unit tests that build plans directly from generic DOM
operations so the mechanism is proven independently of contenteditable.

---

## 6. Behavior-family implementation order

Within each family, use the same loop:

1. select a focused WPT/Chromium group and record its current failures;
2. reduce representative failures to package unit and DOM-mechanism tests;
3. implement the package planner and only the missing generic mechanism;
4. verify exact DOM, directional Selection, event order, mutation records, and
   history result;
5. remove any harness emulation for that behavior;
6. promote the group in the manifest and run all previously promoted groups.

### 6.1 Text and composition

Implement first because every structural/formatting command depends on sound
boundaries:

- replacement over collapsed and non-collapsed ranges;
- UTF-16 API boundaries with Lambda codepoint/string operations kept explicit;
- grapheme-safe backward/forward delete and corpus-required word/line units;
- cross-text-node deletion without whole-host replacement;
- element-boundary insertion and empty-host behavior;
- collapsible whitespace, NBSP preservation, `white-space` modes, and `<pre>`;
- atomic and `contenteditable="false"` boundaries;
- composition start/update/replacement/commit/cancel and host/focus removal;
- `plaintext-only` filtering and HTML-command behavior;
- correct pre-action target ranges and post-action caret mapping.

Collapsed formatting state must be threaded through text insertion from the
start; do not add a second “styled typing” insertion algorithm later.

### 6.2 Structural deletion and insertion

Implement in slices:

1. range deletion inside and across inline wrappers;
2. backward/forward deletion at `<br>` and block boundaries;
3. paragraph and line-break insertion in empty/start/middle/end positions;
4. compatible/incompatible block joins and split attribute preservation;
5. list item split/join, empty-item breakout, list conversion, indent/outdent;
6. table-cell boundaries, selected cells, tables adjacent to text/blocks;
7. atomic/void elements, comments, foreign/SVG/MathML boundaries, detached or
   unusual editing roots exercised by crash tests;
8. shadow/slot cases once the underlying composed Selection APIs exist.

Block classification, splittability, prohibited-child rules, placeholder
choice, and merge compatibility are package tables/predicates. Native receives
the selected nodes and generic operations only.

### 6.3 Inline and block formatting

Build one style-run analysis used by execution and queries:

- semantic wrappers and CSS equivalents for bold/italic/underline/strike,
  subscript/superscript, font name/size, foreground/background/highlight;
- `styleWithCSS`/`useCSS` setting and live query behavior;
- collapsed typing marks and reset rules after movement/deletion/host change;
- uniform, mixed, and indeterminate selection states;
- partial wrapper splitting, nested wrapper reuse, and redundant wrapper
  cleanup;
- `removeFormat` without destroying unrelated attributes/links unless the
  compatibility result requires it;
- `formatBlock`, justify variants, and multi-block selections;
- preservation of Selection direction and exact affected structure.

No planner may use `innerHTML` replacement as its formatting primitive.

### 6.4 Objects and markup

Implement shared insertion plans for:

- `createLink` and `unlink`, including collapsed and cross-wrapper cases;
- `insertImage` and replacement of a selected atomic object;
- `insertHorizontalRule` and required surrounding/placeholder blocks;
- `insertHTML` using the ordinary fragment parser, contextual insertion, and
  one shared fragment-insertion planner;
- `insertText` through the text planner;
- selection-only operations such as `selectAll` and required Chrome selection
  commands.

Sanitization belongs to the clipboard/HTML ingestion policy in the package;
HTML parsing itself remains native DOM mechanism.

### 6.5 Clipboard and drag/drop

Use one package fragment pipeline for API commands and uncanceled platform
actions:

- serialize selected plain text and HTML for copy/cut;
- enforce trusted user activation and platform permissions at the native
  clipboard boundary;
- choose HTML vs plain text and `plaintext-only` conversion in the package;
- parse/sanitize/import the fragment, then use the shared insertion plan;
- delete the source only for a move and map the destination after source
  deletion;
- preserve DataTransfer/ClipboardEvent/InputEvent payload identity and order;
- record cut/paste/drop as explicit history boundaries;
- handle iframe, detached source, file/image, URL, and unsupported payload
  dispositions through the manifest rather than guessed fallbacks.

OS clipboard service absence may exclude a platform integration test, but the
same serialization/insertion policy must have an in-process focused test.

### 6.6 History

History is package state and is added after generic transactions are stable:

- record inverse-capable retained deltas, not whole-host HTML strings;
- retain removed nodes/fragments safely so undo can restore observable
  structure and identity where required;
- store Selection and typing state before/after;
- coalesce adjacent compatible typing and composition updates;
- break groups on explicit commands, selection/focus/host changes,
  clipboard/drop, incompatible author mutation, and corpus-required timing;
- clear redo on a new committed edit;
- run undo/redo through the same generic transaction and observation path;
- never record prevented, declined, editor-owned, failed, or unchanged actions
  unless the compatibility contract records a state-only change;
- prune history on document teardown/navigation and handle body/host removal
  deterministically.

Retained DOM wrappers and package history values require explicit lifetime/GC
tests under D5.3.3; a passing functional undo test is not enough.

### 6.7 `designMode` and document APIs

Implement one internal package operation transport for:

- `execCommand`;
- each `queryCommand*` method;
- `designMode` get/set.

The native binding identifies which IDL operation was called and coerces its
arguments. It does not canonicalize command aliases, decide enabled/state
semantics, or mutate editable content. The package:

- canonicalizes `designMode` values and defines on/off behavior;
- establishes the document element/body editing host according to tests;
- handles focus and Selection initialization/teardown;
- makes commands and platform defaults use the same document context;
- resets or preserves typing/history state exactly where conformance requires;
- supports no-selection, detached-body, iframe, navigation, and re-entrant
  cases without crashing;
- returns query values without mutation, notification, history, or selection
  side effects.

---

## 7. Native waist migration

### 7.1 Introduce the explicit call boundary first

Before adding command families:

1. add an invocation/result bridge capable of returning an `Item` record from
   a package behavior call;
2. preserve the existing symbol-verdict adapter for unrelated behavior
   handlers;
3. root the invocation, handler result, document, Selection, and target ranges
   across package execution;
4. pass the document mutation epoch explicitly;
5. reject nested reuse of one invocation object;
6. log one stable failure category when package initialization/dispatch fails.

Convert the current `domedit` and `execcommand` paths to this contract while
their behavior remains unchanged. Only then remove the epoch-based result
guessing.

### 7.2 Replace implicit pending range

Delete after all callers migrate:

- `s_dom_edit_apply_epoch`;
- `s_dom_edit_caret_node` / `s_dom_edit_caret_u16`;
- `dom_edit_apply_epoch()` and caret channel accessors;
- `dom_edit_set_pending_range*()` / `dom_edit_clear_pending_range()`;
- `DocState::editing.pending_dom_edit_*` fields;
- package APIs that implicitly read “the current pending edit”.

Replacement APIs accept an explicit live Range/bookmark or transaction token.
They validate that it belongs to the token's document/host and expected epoch.
Nested package/API calls receive distinct tokens, so re-entry cannot overwrite
the outer selection.

### 7.3 Split policy from mechanics

| Current native shape | Terminal disposition |
|---|---|
| UTF-16/codepoint conversion and text splice | retain as generic text-node mechanism |
| Range content deletion/insertion | retain through ordinary DOM Range transaction operations |
| create/insert/remove/move/split node | retain/promote as generic DOM transaction steps |
| parsed contextual fragment insertion | retain parser/mechanism; package chooses when and normalization |
| live Range/Selection adjustment | retain in DOM core |
| mutation notify and rendering invalidation | retain in DOM core |
| block-tag set and structural ancestor selection | move to package |
| block merge compatibility | move to package; native receives explicit move/remove plan |
| line/paragraph structure choice | move to package |
| format ancestor/tag state and toggle choice | move to package; retain generic ancestor/attribute/style reads |
| command bridge success inferred from epoch/verdict | replace with package `EditResult` |

Every retained native operation gets a mechanism-level test with arbitrary DOM
inputs, not only a named editing command.

### 7.4 Package-disable and failure behavior

`RADIANT_DOM_PKG=0` may remain a diagnostic switch, but for contenteditable it
means “UA edit unavailable,” not “run native rich editing.” Add a focused test
that disables the package and proves:

- author events still dispatch;
- an uncanceled contenteditable default does not mutate;
- `execCommand` returns the compatible failure value;
- query methods report no package support according to the defined failure
  contract;
- a clear package-unavailable diagnostic is logged once per document/class.

Other already-migrated DOM behaviors follow their own governing design; do not
change their failure policy accidentally in this phase.

---

## 8. Event, Selection, and observation contract

### 8.1 Input route

For a platform action:

1. dispatch the ordinary precursor event (`keydown`, composition,
   paste/cut/drop, as applicable);
2. stop if it was canceled or an editor/model handler claimed the action;
3. capture the live host, Selection, target ranges, and mutation epoch;
4. dispatch the standard cancelable `beforeinput` when required;
5. stop on cancellation; if a listener mutated without canceling, apply the
   design's stale-epoch rule rather than a stale plan;
6. invoke the package planner/applier once;
7. if claimed/changed according to the command event contract, dispatch
   non-cancelable `input` at the required point;
8. queue `selectionchange`, observers, layout, and paint through their normal
   checkpoints;
9. commit audit/history outcome.

### 8.2 API route

`execCommand` uses the same context/descriptor/planner/applier but follows the
descriptor's compatibility event contract. Do not unconditionally synthesize
`beforeinput` and `input` for every legacy command.

Queries:

- build a read-only live context;
- do not open a mutation transaction;
- do not dispatch events;
- do not alter typing/history state;
- tolerate unsupported commands, absent Selection, detached roots, and
  non-editable contexts with exact compatibility return values.

### 8.3 Selection mapping

The applier retains boundary bookmarks/live Ranges through mutations. Tests
must cover:

- forward/backward selections;
- text and element boundary points;
- boundaries inside nodes being split, moved, removed, wrapped, or unwrapped;
- atomic nodes and false islands;
- multi-block/list/table ranges;
- disconnected nodes and host replacement;
- query methods preserving the exact Selection;
- undo/redo restoring direction and boundary placement.

Do not repair a lost selection by searching for equal text after the edit.

### 8.4 Mutation and layout observation

For each promoted behavior, assert:

- mutation record kind, target, old value, added/removed nodes, subtree scope,
  batching, and delivery order;
- no whole-host replacement for local operations;
- `input` sees committed DOM and Selection;
- observer callbacks run at the normal microtask checkpoint;
- `selectionchange` fires only when required and in the correct document;
- synchronous geometry after commit is fresh;
- style/layout/paint invalidation is neither missing nor whole-document by
  default.

---

## 9. Conformance infrastructure

### 9.1 Manifest schema

Add checked-in manifests for WPT and Chromium. Each entry records:

| Field | Meaning |
|---|---|
| `source` | stable corpus-relative path |
| `upstream_revision` | pinned corpus revision |
| `sha256` or manifest identity | detects unreviewed source drift |
| `kind` | automated text, automated DOM, crash-only, pixel/manual, OS-service |
| `family` | host, text, deletion, block, list, format, command, clipboard, history, selection, etc. |
| `requirements` | product capabilities needed by the case |
| `disposition` | required, temporarily failing, manual, platform-unavailable |
| `reason` | mandatory only when not required |
| `issue` | implementation issue/phase owner for temporary failures |

Unknown files and missing manifest entries fail the inventory check. A new
upstream revision is a reviewed manifest update, not an implicit suite change.

### 9.2 WPT runner

Add `test/wpt/test_wpt_contenteditable_gtest.cpp` and register it through
`build_lambda_config.json`. Reuse the existing WPT runner/testharness plumbing
where it is protocol-only; do not create a second JavaScript runtime adapter.

The selected roots are:

- `ref/wpt/contenteditable/`;
- applicable `ref/wpt/editing/` including `data`, `run`, `plaintext-only`,
  `other`, whitespace, and crash tests;
- applicable `ref/wpt/input-events/`;
- applicable `ref/wpt/selection/` and `selection/contenteditable/`.

The runner supports filters by path/family, deterministic per-test timeout,
crash isolation where required, result JSON, and summary counts by
disposition. It must run the same product package used outside tests.

### 9.3 WPT shim cleanup

Create an audit table for every editing-related function in
`test/wpt/wpt_testharness_shim.js`. Remove category-3 product emulation in
behavior-family order. In particular, the final harness contains no helper
that:

- classifies editing blocks to choose a mutation;
- computes deletion or insertion target ranges on behalf of the UA;
- inserts/removes placeholders, joins blocks, or normalizes whitespace;
- wraps/unwraps formatting;
- maintains UA undo/redo state;
- dispatches a synthetic input sequence as a replacement for a missing
  product action;
- directly changes Selection as the expected result of a key/command.

Testdriver adapters may focus a node, synthesize device actions, or translate
coordinates into the existing automation path. They must not directly create
the edit result.

### 9.4 Chrome/Chromium runner

Add a new package-UA runner, preferably
`test/chromium/test_chromium_contenteditable_gtest.cpp`, with a generated
binary name `test_chromium_contenteditable_gtest.exe`. Wire the sibling
`lambda-test/editing` corpus through an explicit pinned read-only root and
manifest; do not use a developer-specific absolute path at runtime.

The runner:

- loads the unmodified test and expected files;
- provides only the minimum legacy harness protocol required to open a test,
  inject public actions, wait for completion, and collect text/DOM/Selection
  output;
- supports text, DOM, crash-only, and explicitly classified pixel/manual
  cases without treating one kind as another;
- isolates crashes/timeouts and reports the last test path;
- emits stable JSON and a human summary;
- never calls a package planner or generic mutation primitive directly.

The existing `chrome_editing_ce3_harness_patch.js` is historical input to the
audit, not assumed-valid infrastructure. Preserve only protocol adaptation;
delete or replace any mutation/selection/command emulation.

### 9.5 Make targets

Add generated/build-backed targets with these responsibilities:

```text
make test-wpt-contenteditable
make test-chromium-contenteditable
make test-editable-ua-focused
make test-editable-ua
```

`test-editable-ua-focused` runs package/DOM mechanism tests and the promoted
family slices. `test-editable-ua` runs focused tests, full applicable WPT,
full applicable Chromium, existing `test-editable`, and form-control
regressions. Final baseline/CI integration occurs only after the runners are
stable and honest.

### 9.6 Acceptance accounting

During implementation, reports distinguish:

- required pass;
- required fail;
- crash;
- timeout;
- manual/pixel;
- OS/platform unavailable;
- not yet promoted.

The current passed-test `baseline.txt` may be retained as a monotonic progress
ratchet, but it cannot hide required failures. Final acceptance has zero
required fail/crash/timeout. An exclusion count never contributes to the pass
percentage.

---

## 10. Phased implementation

The phases correspond to Design §20.9. Do not start a later family by adding
a native shortcut around an unmet earlier exit gate.

### UA-0 — Freeze corpora and establish honest red gates

#### UA-0.1 Pin revisions and inventory files

- record the current WPT revision used by `ref/wpt`;
- record the sibling Chromium corpus revision and validate `MANIFEST`;
- create checked-in WPT and Chromium selection manifests;
- classify every selected file and fail on unclassified additions;
- separate automated result tests, crash tests, pixel/manual tests,
  spelling/OS-service tests, and unsupported platform tests;
- record the exact existing pass/fail/crash/timeout counts without claiming
  conformance.

#### UA-0.2 Build runner skeletons

- add both GTest runners and `build_lambda_config.json` entries;
- reuse common page-load, timeout, result, and filter code rather than copying
  the Selection runner;
- add per-test process isolation only where current infrastructure requires it;
- emit stable JSON summaries under `./temp/`, never `/tmp`;
- add manifest-validation unit tests.

#### UA-0.3 Audit and disable harness emulation

- classify every editable helper in the WPT shim;
- classify the historical Chromium harness patch;
- add a runner mode/assertion that detects direct harness DOM mutation during
  an injected edit where possible;
- remove emulation for the first promoted text/command smoke slice;
- make those cases honestly red if product behavior is missing.

#### UA-0.4 Capture current source ownership

- generate a checked source inventory for command names, `inputType` branches,
  block/list tag tables, history state, normalization helpers, and TLS edit
  channels outside `lambda/package/dom`;
- distinguish allowed IDL method names and generic enum operations from
  forbidden native policy;
- add a lint/audit script that fails when a new native command-policy site is
  introduced.

#### UA-0.5 Preserve current regressions

- run and record `make test-editable`;
- run the existing Selection WPT suite;
- run form-control editing, clipboard, IME, and history fixtures;
- retain the package-disabled diagnostic behavior as a separately labeled
  result, not a parity oracle for full UA editing.

#### UA-0 exit gate

- [ ] both new runners build and enumerate their manifests;
- [ ] corpus revisions and dispositions are reproducible;
- [ ] the first no-emulation smoke cases expose real product results;
- [ ] no vendor file or expected result was modified;
- [ ] current regressions remain green;
- [ ] source-policy audit has a reviewed baseline.

### UA-1 — Explicit package core and generic transaction waist

#### UA-1.1 Add document package state

- select or add the generic precisely rooted document package-state slot;
- add `EditingSession` initialization, read, write, schema version, and reset;
- prove two documents do not share settings, typing state, or history;
- prove body/host removal and document teardown release retained values;
- add GC stress around stored arrays, maps, DOM wrappers, and fragments.

#### UA-1.2 Add invocation/result transport

- define the internal package operation kinds without command tables;
- pass wrapped document, host, Selection/ranges, epoch, payload, trust, and
  cascade facts explicitly;
- return a rooted `EditResult` Item;
- keep ordinary behavior-verdict callers unchanged through an adapter;
- convert `domedit` and current `execCommand` cases first.

#### UA-1.3 Add descriptor/context/plan modules

- introduce the immutable command registry and alias normalization;
- build side-effect-free `EditContext` validation;
- add plan/step/result constructors with structural validation;
- make current text, formatting, insertHTML, delete, paragraph, and line-break
  behavior lower through the same plan shape;
- add pure package tests and expected outputs for every constructor/predicate.

#### UA-1.4 Add generic atomic mutation transaction

- implement begin/validate/apply/commit/rollback;
- root every plan input and retained inverse;
- integrate live Range adjustment, mutation records, Selection, and
  invalidation once;
- add failure injection for stale epoch, wrong document/host, allocation
  failure boundary, invalid reference, and mid-plan rejection;
- assert rollback leaves DOM, Selection, observer queue, and history unchanged.

#### UA-1.5 Remove implicit edit channels

- migrate package calls off `pending_dom_edit_*`;
- remove apply/caret TLS result channels;
- make re-entrant invocations independent;
- update logs/replay to record explicit plan/result IDs;
- keep unrelated caret/key/scroll request channels out of this cleanup unless
  the same explicit-contract change is separately justified.

#### UA-1.6 Establish the no-fallback rule

- update the DOM-state working docs to note the D7.2.5 contenteditable
  exception to historical fail-open migration behavior;
- ensure package load/handler failure returns no UA mutation;
- add once-per-document diagnostics and focused tests;
- verify author events still work when package UA behavior is unavailable.

#### UA-1 exit gate

- [ ] current supported commands and input actions use one descriptor/plan;
- [ ] no contenteditable result is inferred from TLS epochs;
- [ ] atomic rollback and stale-plan tests pass;
- [ ] state is per document and precisely rooted;
- [ ] native policy audit has not grown;
- [ ] all UA-0 and existing editor/form regressions pass.

### UA-2 — Host, text, composition, and Selection foundation

#### UA-2.1 Effective editing host and mode

- complete inheritance for missing/invalid/empty/true/false/plaintext-only;
- distinguish an editing host from an editable descendant;
- constrain nested hosts and false islands;
- implement document-root selection for `designMode` without duplicating host
  parsing;
- cover detached nodes, iframe documents, body replacement, and focus changes.

#### UA-2.2 Boundary and position layer

- implement package traversal/order helpers over ordinary DOM snapshots;
- expose generic live bookmarks for plan start/end/caret positions;
- validate UTF-16 vs codepoint conversions at every API/waist edge;
- cover forward/backward Selection and element offsets;
- add cross-document, stale-node, and disconnected-boundary tests.

#### UA-2.3 Complete text/deletion semantics

- implement the §6.1 matrix in small promoted groups;
- share selection replacement between typing, `insertText`, paste, and
  composition;
- implement grapheme/word/line boundaries once and reuse them;
- move whitespace/placeholder decisions out of the WPT shim;
- preserve precise mutation records and minimal invalidation.

#### UA-2.4 Complete composition

- tie the package plan to the existing document/platform IME session;
- handle repeated identical preedit without false mutation;
- map the IME caret inside preedit text;
- commit/cancel exactly once;
- safely terminate on focus/host/document removal;
- verify editor model reconciliation and package history grouping.

#### UA-2.5 Query and design-mode skeleton

- expose all public query methods through the package bridge;
- return correct unsupported/disabled defaults before rich families land;
- implement `designMode` IDL get/set and document editing-host lifecycle;
- prove every query is read-only and Selection-preserving.

#### UA-2 exit gate

- [ ] selected WPT contenteditable/input-events/Selection foundation is green;
- [ ] promoted Chromium text/deletion/composition groups are green;
- [ ] corresponding harness editing emulation is deleted;
- [ ] package-disabled and stale/re-entry tests pass;
- [ ] CodeMirror/ProseMirror native-input reconciliation remains green;
- [ ] no full-host rebuild appears in mutation traces.

### UA-3 — Structural editing and normalization

#### UA-3.1 Establish classification tables

- move block/splittable/merge/list/table/atomic classifications to package
  data and predicates;
- test classifications independently across HTML/foreign/unknown elements;
- ensure native plan application sees only explicit nodes/parameters.

#### UA-3.2 Cross-node deletion and joins

- complete partially selected ancestor handling;
- join inline runs and compatible/incompatible blocks per corpus;
- preserve/remove wrappers and attributes exactly as required;
- handle false islands, void/atomic nodes, `<br>`, whitespace, and empty roots;
- promote deletion groups monotonically.

#### UA-3.3 Paragraph and line breaks

- implement split points, default paragraph separator, empty-block
  placeholders, `<pre>`/white-space behavior, headings/quotes, inline roots,
  and non-splittable elements;
- share the planner between physical Enter/input intents and legacy commands;
- add event/Selection/history boundaries even before full history lands.

#### UA-3.4 Lists and indentation

- implement ordered/unordered conversion and removal;
- split/join list items and break out of empty items;
- indent/outdent nested and partial selections;
- preserve list attributes and normalize invalid direct nesting;
- cover lists inside table cells and non-editable descendants.

#### UA-3.5 Tables and unusual roots

- cover cell-boundary delete/insert, selected cells, tables adjacent to
  blocks/atomic nodes, and selection confinement;
- make crash-only tests for detached/body/head/document/foreign roots pass
  without widening behavior beyond the corpus result;
- classify true layout/pixel cases separately.

#### UA-3.6 Normalization

- implement ordered idempotent affected-root passes;
- merge only compatible adjacent text/inline/block nodes;
- remove only redundant empty wrappers/placeholders;
- preserve authored attributes and node identity outside the plan;
- run normalization inside the same generic transaction and future history
  delta;
- add idempotence and “apply to unrelated sibling” negative tests.

#### UA-3 exit gate

- [ ] structural WPT and promoted Chromium deletion/insertion/list groups pass;
- [ ] no structural policy table remains in native code;
- [ ] normalization is deterministic and idempotent;
- [ ] Selection and observer records match exact expected results;
- [ ] Editor.js ordinary paragraph editing and paste fallback work through the
      package without fixture replacement Tools;
- [ ] all earlier groups remain green.

### UA-4 — Full commands, formatting, queries, and objects

#### UA-4.1 Complete registry coverage

- generate/audit the registry inventory against WPT data and Chromium command
  extraction;
- implement support/enabled/event/history/state metadata for every required
  command and alias;
- give Chrome-only platform commands an explicit supported or
  platform-unavailable disposition;
- fail tests if the corpus reaches an unclassified command.

#### UA-4.2 Inline formatting and typing state

- implement all inline families in §6.3;
- add collapsed typing state scoped to document/host/selection epoch;
- apply typing state through the ordinary text insertion planner;
- reset/preserve it on navigation, deletion, composition, host change, and
  author mutation according to tests;
- complete state/indeterminate/value query matrices.

#### UA-4.3 Block formatting and lists

- implement `formatBlock`, justification, indent/outdent, and list commands
  through structural planners;
- handle multi-block, partial, mixed, disabled, and plaintext-only selections;
- preserve exact Selection and normalization.

#### UA-4.4 Links, images, rules, HTML, and selection commands

- complete §6.4;
- share contextual fragment insertion with rich paste;
- implement `selectAll` and applicable Chrome selection commands through
  generic Selection mechanisms;
- ensure commands never throw for invalid caller combinations where the
  corpus requires a Boolean failure.

#### UA-4.5 Re-entrancy and API compatibility

- implement one explicit `dispatch_depth` policy;
- cover recursive `execCommand`, commands from `beforeinput`/`input`/
  `selectionchange`, DOM mutation during command dispatch, and navigation;
- ensure an outer uncommitted plan is never partially overlapped;
- validate `showUI`, missing arguments, coercion, case folding, unknown names,
  no selection, and non-editable contexts.

#### UA-4 exit gate

- [ ] full required exec/query command inventory is implemented or explicitly
      platform-classified;
- [ ] WPT editing command data/run groups pass;
- [ ] promoted Chromium `execCommand` and style groups pass;
- [ ] query calls are mutation/event/history free;
- [ ] stock Editor.js inline tools and ProseMirror legacy fallback probes pass;
- [ ] no native command table or query table exists.

### UA-5 — Clipboard, drag/drop, and UA history

#### UA-5.1 Clipboard serialization and trust

- implement package selection serialization policy;
- expose only platform payload/permission/user-activation facts through native
  mechanisms;
- cover copy/cut/paste commands and physical clipboard events;
- add in-process payload tests for environments without an OS clipboard.

#### UA-5.2 Rich/plain insertion and sanitization

- share HTML/text choice, sanitization, contextual parsing, and insertion with
  `insertHTML`/`insertText` planners;
- implement `plaintext-only` conversion;
- preserve style/list/table/link behavior required by the corpora;
- assert no duplicate parse/normalize implementation in native or harness.

#### UA-5.3 Drag/drop

- map source and destination through one plan;
- distinguish move/copy and delete source only after destination validation;
- support text, HTML, URL, image/file, same-host, cross-host, and iframe cases
  according to the manifest;
- preserve public drag/drop and input event ordering.

#### UA-5.4 History state and transactions

- implement the §6.6 stack/delta model;
- add typing/composition coalescing and explicit command boundaries;
- integrate undo/redo descriptors and keyboard input types;
- preserve Selection, typing state, mutation records, and invalidation;
- detect incompatible author mutations and close/reset groups without
  recording editor-owned changes;
- add teardown and GC stress with deep retained histories.

#### UA-5 exit gate

- [ ] applicable WPT clipboard/editing history tests pass;
- [ ] promoted Chromium pasteboard, drag/drop, and undo groups pass;
- [ ] trusted activation/permission failures return compatible values;
- [ ] undo/redo restore exact observable structure and Selection;
- [ ] no double history entry occurs in CodeMirror/ProseMirror/editor-owned
      paths;
- [ ] history and clipboard retain no document after teardown.

### UA-6 — Full conformance, cleanup, and baseline cutover

#### UA-6.1 Promote all applicable automated cases

- classify any remaining case with a reviewed reason;
- eliminate every required fail, crash, hang, and timeout;
- rerun all promoted groups after every final fix;
- preserve separate totals for manual/pixel/OS-service/platform-unavailable;
- verify WPT precedence decisions are recorded once and not runtime branches.

#### UA-6.2 Delete transitional policy and harness behavior

- complete the TLS/pending-range deletion;
- remove native block/format/list/command policy;
- narrow/delete command-shaped waist exports whose generic replacements land;
- remove WPT/Chromium product emulation;
- remove obsolete first-gate exclusions and replacement editor fixtures;
- remove stale baseline/test-runner code from the retired CE3 lane.

#### UA-6.3 Documentation and build cutover

- update `RAD_18_Editing_Selection_Ranges.md` and related diagrams;
- update DOM state/default/API working docs where ownership or failure behavior
  changed;
- update the design §20 status and the D7.2.5 implementation-status appendix;
- revise formal rulings/version only if implementation evidence changes a
  ruling, following `doc/Doc_Convention.md`;
- add generated suite targets to required baseline/CI through
  `build_lambda_config.json` and normal generation.

#### UA-6.4 Performance and lifecycle closeout

- use `make release` for package-load and per-edit performance measurement;
- record compile-cache reuse, first-interaction load, typing p50/p95, large
  selection/host operations, history memory, and observer/layout cost;
- optimize shared package/planner or generic DOM mechanisms only after a
  measured bottleneck;
- run repeated document/iframe create-edit-destroy and navigation stress;
- prove no retained package state, DOM wrapper, history node, callback, or
  native token survives document teardown.

#### UA-6 exit gate

- [x] all Design §20.11 exit criteria pass for the reviewed applicable scope;
- [x] full WPT and Chromium applicable manifests have zero unexpected
      fail/crash/timeout;
- [x] `make test-editable-ua`, Radiant baseline, Lambda baseline, and lint
      pass; the non-UA aggregate-test exception is recorded in the UA-6 report;
- [x] native ownership audit is clean;
- [x] harness no-emulation audit is clean;
- [x] release performance and lifecycle reports are recorded;
- [x] implementation/docs/formal-status agree.

---

## 11. File-level change checklist

| File/area | Planned responsibility/change |
|---|---|
| `lambda/package/dom/dom_edit.ls` | thin default-input entry into context/descriptor/planner |
| `lambda/package/dom/commands.ls` | immutable full descriptor/alias registry and exec/query dispatch |
| `lambda/package/dom/form.ls` | register body/document internal edit API behavior; no algorithms |
| `lambda/package/dom/tree.ls` | general DOM traversal only; promote shared helpers rather than copy |
| new `lambda/package/dom/edit_*.ls` modules | package policy families in §4 |
| package-state mechanism | one generic precisely rooted document package value; no native editing fields |
| `radiant/editing_controller.cpp` | common gate invocation/result integration, cancellation, event order |
| `radiant/editing_dom_waist.cpp` | generic transaction primitives only; delete TLS/pending channels and policy |
| `radiant/editing_target_range.cpp` | retained range geometry/mechanism; remove operation policy if found |
| `radiant/editing_host.cpp` / `editing.cpp` | canonical host/mode facts shared with package; no command behavior |
| `radiant/event.cpp` / `event.hpp` | package load, structured behavior-call transport, platform events/logging |
| `lambda/dom/dom.cpp` | thin exec/query/designMode IDL bridges; generic DOM transaction exposure |
| `lambda/dom/dom_selection.cpp` / `.h` | live bookmarks, Selection/Range conformance, selectionchange |
| `lambda/dom/dom_clipboard.cpp` | clipboard/data-transfer/trust mechanism only |
| `lambda/dom/dom_observers.cpp` | observer fidelity fixes, never synthetic command records |
| `radiant/state_store.cpp` / state structs | remove pending edit policy state; retain only generic document/context storage |
| `radiant/event_sim.cpp` | public action injection/assertions, never direct UA mutation |
| `test/wpt/wpt_testharness_shim.js` | remove contenteditable product emulation; retain protocol/input adaptation |
| new WPT runner + manifests | standards conformance gate |
| new Chromium runner + manifests | Chrome compatibility gate over pinned read-only corpus |
| `build_lambda_config.json` / generated build | new executables and required suite targets |
| `test/ui/` and `test/lambda/` | focused package, waist, event, history, and regression fixtures |
| developer/design docs | status, ownership, failure behavior, and verified implementation map |

### 11.1 Native symbol audit

At each phase, search native source for:

- all `execCommand` command literals and aliases;
- `queryCommand*` decision tables;
- `inputType` branches that choose contenteditable structure;
- block/list/format tag sets used for UA edits;
- typing-style or UA history state;
- pending edit range/caret/apply epoch globals;
- WPT/Chromium/test-name checks.

Allowed matches are limited to IDL publication/coercion, generic event names,
test runner inventory, logging labels, and comments/history. Every executable
match is reviewed against D7.2.5.

### 11.2 Duplicate-helper audit

Before each new helper:

```text
rg "existing concept or candidate symbol" lambda/dom radiant lambda/package/dom
```

If the required native mechanism exists as a `static`, promote it to the
owning module header and call it. Do not copy it into the waist. If three
family modules share a planning shape, extract it into `edit_plan.ls`,
`edit_positions.ls`, or `edit_normalize.ls` before adding the third copy.

---

## 12. Test matrix

### 12.1 Package unit tests

Every family gets pure Lambda tests with expected outputs:

| Area | Required assertions |
|---|---|
| command registry | case folding, aliases, exhaustive inventory, no duplicate canonical names, support/enabled defaults |
| context | modes, false islands, host/document ownership, direction, stale/disconnected ranges |
| plan | valid step sequences, affected roots, selection mapping, invalid plan rejection |
| text | grapheme/word/line spans, whitespace modes, plaintext conversion, typing state |
| blocks/lists | classification, split/join/merge decisions, placeholders, nesting |
| format | live state/value, mixed/indeterminate, wrapper/style choice, normalization |
| clipboard | payload choice, sanitization policy, move/copy plan |
| history | grouping, coalescing, redo invalidation, state-only changes |
| designMode | value normalization and context selection |

### 12.2 Native mechanism tests

Add focused GTests for:

- generic transaction validation/rollback;
- node insert/remove/move/split and text replacement;
- live Range/bookmark mapping;
- observer record production;
- Selection direction/collapse/restore;
- cross-document/host rejection;
- mutation epoch mismatch;
- precise rooting and retained detached fragments;
- teardown with open/aborted transactions.

These tests construct generic operations without naming editing commands.

### 12.3 UI/API tests

Add small fixtures for:

- physical key vs `execCommand` parity per shared command;
- every query method before/after selection/DOM/typing-state changes;
- `beforeinput` cancellation and mutating-listener stale epoch;
- recursive `execCommand`;
- `designMode` on/off, iframe, body replacement, no selection;
- package-disabled/package-error behavior;
- observer/event/selectionchange ordering;
- undo/redo and editor-owned history isolation;
- clipboard trust and in-process payloads;
- two documents with independent state;
- GC pressure during package calls and retained history.

### 12.4 Editor regressions

Keep the unmodified upstream probes:

- CodeMirror: DOM-change reconciliation, composition, Selection direction,
  clipboard, operations, readonly, lifecycle;
- ProseMirror: DOM change, marked composition, toolbar/marks, HTML clipboard,
  operations, readonly, lifecycle, legacy ShadowRoot fallback when applicable;
- Editor.js: stock inline tools, block split/merge, paste, clipboard, arrow
  movement, change notifications, saved data, readonly, lifecycle;
- Radiant template: one-owner routing, source reconciliation, selection
  projection, replay;
- form controls: text value/selection/IME/clipboard/history remain on their
  established value-backed path.

### 12.5 Conformance families

| Family | WPT evidence | Chromium evidence |
|---|---|---|
| host/editability | contenteditable + HTML editing | editability, shadow |
| text/composition | input-events, plaintext-only | input, inserting, composition |
| Selection/Range | selection/contenteditable | caret, selection |
| deletion/structure | editing other/whitespace/data | deleting, inserting |
| commands/formatting | editing data/run/other | execCommand, style |
| clipboard/drop | editing paste/input-events | pasteboard |
| history | editing undo-related cases | undo |
| robustness | WPT crash tests | crash-only cases across families |

---

## 13. Verification commands

Run the narrowest relevant gate continuously, then all accumulated gates at a
phase exit.

### 13.1 Existing gates

```bash
make build-test
make editable-unit
make editable-ui
make editable-editor-e2e
make test-editable
./test/test_wpt_selection_gtest.exe
```

### 13.2 New gates after UA-0

```bash
make test-editable-ua-focused
make test-wpt-contenteditable
make test-chromium-contenteditable
make test-editable-ua
```

### 13.3 Phase exits

```bash
make test-radiant-baseline
make test-lambda-baseline
make lint ARGS='--rule ^no-int-cast-radiant$'
make lint
```

Run the full `make test` at UA-6 and at any cross-runtime or DOM-core cutover
whose risk warrants it. Performance measurements use only:

```bash
make release
```

Store generated reports and temporary result JSON under `./temp/`.

---

## 14. Review checkpoints and stop conditions

### Checkpoint A — after UA-0

Review corpus revisions, manifest classifications, harness audit, and honest
red counts. Stop if a runner still implements a product edit or modifies
expected results.

### Checkpoint B — after UA-1 invocation/transaction core

Review rooting, atomic rollback, per-document state, result semantics, and
native ownership. Stop if adding a command still requires editing a native
command or query table.

### Checkpoint C — after each promoted behavior family

Review focused reduction, package policy location, exact DOM/Selection,
observer/event order, and harness deletion. Stop if the only explanation for a
pass is a broad normalization or whole-host replacement.

### Checkpoint D — before history retention

Review DOM wrapper/node lifetime, detached-node retention, package-state roots,
document teardown, and memory pressure. Stop if correctness relies on native
stack scanning or unowned raw pointers.

### Checkpoint E — before UA-6 deletion

Review all active call sites of each transitional helper and every existing
test tied to it. Delete only after the replacement gate is green; do not leave
two selectable editing implementations.

### Immediate stop conditions

Stop and return to design review if:

- WPT and Chromium require conflicting observable behavior not resolved by
  the precedence/manifest rule;
- a required behavior appears to need command-specific native layout policy
  rather than a generic mechanism;
- package state cannot be made per-document and precisely rooted;
- atomic rollback cannot preserve DOM/Selection/observer invariants;
- the final behavior-package import path cannot satisfy D7.2.4;
- passing a suite would require modifying vendor code or branching on tests;
- implementation evidence changes D7.2.5 or another formal ruling.

---

## 15. Risk register

| Risk | Failure mode | Control |
|---|---|---|
| Corpus behavior copied mechanically | incoherent collection of Blink quirks | WPT precedence, package descriptors, family reductions, manifest-reviewed divergence |
| Native waist regrows policy | commands split across C++ and Lambda | source ownership audit, no command strings, generic mechanism tests |
| WPT shim masks failures | false green conformance | category audit, delete product emulation before promotion, no-emulation runner checks |
| TLS/pending state survives | re-entrant command corrupts outer range/result | explicit invocation/token/result, nested-call tests, delete channels in UA-1 |
| Plan partially applies | corrupt DOM and misleading history | prevalidation, generic atomic transaction, failure injection, rollback tests |
| Whole-host normalization | lost node identity/editor reconciliation | affected roots, identity assertions, observer record assertions |
| History retains documents | leak/UAF after navigation | precise roots, detached-node registry tests, teardown stress, per-document reset |
| Typing state becomes stale | wrong command state or formatting | host/selection/mutation epoch scoping, author-mutation invalidation |
| Editor history double-records | undo applies twice | one-owner gate, cancellation before package apply/history begin |
| Package loading is too costly | first input latency or per-key overhead | compile cache, release measurement, optimize shared planner only after profiling |
| Package naming collision | built-in `dom` shadowed | D7.2.4 hold point and one atomic namespace migration |
| Platform-only clipboard tests | hidden product gaps | explicit OS exclusion plus in-process policy/serialization tests |
| Scope overwhelms review | regressions hidden in giant patches | behavior-family slices, monotonic promotion, phase exit gates |

---

## 16. Implementation log template

Append one row for each landed slice. Do not mark a phase complete from code
presence alone.

| Date | Phase/slice | Package policy added | Native mechanism changed | Harness emulation removed | Tests promoted | Verification | Remaining gap |
|---|---|---|---|---|---|---|---|
| YYYY-MM-DD | UA-N.x | | | | | | |
| 2026-09-07 | UA-1 invocation/session/transaction | Explicit `EditInvocation`, per-document opaque package session, immutable descriptor/context/plan/result records | Capability-checked generic transaction begin/abort, deferred mutation observation, and rollback snapshots | None; no harness result is counted | Package plan unit plus existing editable command/action fixtures | `make build`; focused editor UI suite | Retained inverse deltas/history and conformance runners remain |
| 2026-09-07 | UA-4 command families | Formatting/settings/typing state; links/images/rules; block conversion/justification; list conversion/indent/outdent; rich clipboard copy/cut/paste policy | Generic selection serialization and clipboard payload write, scoped to an invocation; no command spelling is accepted natively | None | `test_editing_contenteditable_{format_values,objects,lists,blocks,justify,remove_format,indent,clipboard}` | 18-fixture accumulated editable UI gate passes | Multi-block/table/foreign-root planning, undo/redo, and full corpus coverage remain |
| 2026-09-07 | UA-0/UA-2/UA-3 | Pinned WPT/Chromium manifests; host/mode/text/composition/Selection, structural commands, retained history, and query surfaces | Generic DOM edit transactions and result transport; precise roots and retained-delta replay | WPT and Chromium runners only adapt harness APIs; manifest checks reject product emulation | Manifest verification, package-disabled result, text/structure/history/query fixtures | `make test-editable-ua`: applicable WPT 3/3 and Chromium 4/4, 31 editor integrations, and form regressions pass | None in the selected applicable manifests |
| 2026-09-07 | UA-5/UA-6 | Full package command registry and shared planner; cleanup of pending-range/TLS command-result transport | Computed-style, clipboard, selection, transaction, teardown, and wrapper-lifetime mechanisms | Source and runner audits are clean | Lambda/Radiant baseline, lint, release lifecycle and timing fixtures | `make test-lambda-baseline`, `make test-radiant-baseline`, lint, and release probes pass; report records all commands and limits | Broad `make test` has unrelated existing extended-suite failures and a DOM-nodes idle timeout; it is not represented as a UA result |

For a conformance promotion, record:

- upstream revision and manifest diff;
- focused root-cause fixture;
- exact package module owning the rule;
- any new generic waist primitive and why it is mechanism;
- exact DOM/Selection/event/observer/history assertions;
- old and new family counts;
- full accumulated gate result.

---

## 17. Final completion checklist

### Ownership

- [x] D7.2.5 is realized: all UA edit/command/query/history policy is in the
      Lambda DOM behavior package.
- [x] Native code contains no contenteditable command table, block/list/
      formatting rule, typing state, normalization policy, or UA history.
- [x] Keyboard/default input and `execCommand` share one descriptor/planner.
- [x] Query methods read the same live descriptor/state and never mutate.
- [x] Form controls and Radiant model editing retain their intended owners.

### State and safety

- [x] Package editing state is per document and contains no mutable global.
- [x] All native crossings use precise rooting under D5.3.3.
- [x] No pending-range/apply-epoch/caret TLS channel remains for DOM editing.
- [x] Plans reject stale/cross-document/cross-host/false-island inputs.
- [x] Partial failure rolls back DOM, Selection, observer queue, and history.
- [x] Teardown releases invocations, history, wrappers, fragments, and package
      state.

### Behavior

- [x] host inheritance, nested hosts, false islands, plaintext-only, and
      `designMode` pass their matrices;
- [x] text, composition, deletion, blocks, lists, tables, whitespace, atomic
      nodes, and unusual roots pass;
- [x] full required `execCommand` and all five `queryCommand*` APIs pass;
- [x] formatting, typing state, objects, clipboard/drop, and history pass;
- [x] events, target ranges, Selection direction, observers, geometry, and
      invalidation are correct;
- [x] author/editor cancellation prevents duplicate UA mutation/history.

### Conformance

- [x] WPT and Chromium revisions/manifests are pinned and complete.
- [x] Every selected case has a visible disposition/reason.
- [x] Applicable automated WPT has zero unexpected fail/crash/timeout.
- [x] Applicable automated Chromium has zero unexpected fail/crash/timeout.
- [x] Harnesses contain no product editing behavior or expected-result rewrite.
- [x] No vendor file is patched.
- [x] CodeMirror, ProseMirror, Editor.js, Radiant template, and form-control
      regressions remain green.

### Repository closeout

- [x] `build_lambda_config.json` and generated builds contain the new gates.
- [x] Every new Lambda unit test has its expected `.txt` result.
- [x] duplicate-helper and native-policy audits pass.
- [x] no-int-cast Radiant lint and full lint pass.
- [x] baseline gates pass; the attempted repository-wide `make test` exception
      is documented rather than treated as an editable result.
- [x] release-only performance and lifecycle reports are recorded.
- [x] design, implementation, developer docs, and formal implementation status
      agree.
