# Element Type Sharing — Implementation Plan

**Date:** 2026-09-25
**Status:** P0–P2 DONE 2026-09-25; P3 not started.
**Design:** [Lambda_Design_Shape_Transitions.md](../Lambda_Design_Shape_Transitions.md) §7 (E1–E7). **Rulings:** D3.4.3v2 (elements share through the transition tree; the shape pool retires), D2.6.6v3 (`content_list` holds a declared type's content pattern; a nominal name is not a tag), S11.1.6v3 (element content is a sequence-pattern slot). **Closes:** [LR13-9](<../Lambda_Issue_Ledger (fixed).md#lr13-9>) (closed by P0). **Open, not blocking:** SO46 (the *must be empty* spelling), DO31 (`type <B>`).
**Goal:** one `TypeElmt` per distinct tag, namespace and attribute sequence instead of one per element (about 64 MB of the 13 MiB HTML benchmark's 233 MB peak), declared content validated, and the shape pool gone.

## Phases

Each phase lands on its own, green on the gates below, before the next starts.

### P0 — `content_list` replaces `content_length` (E1, E4, E5, E6)

**DONE 2026-09-25.** No sharing yet; this phase removes the one per-instance field and fixes validation.

**Results.**
- Lambda baseline 5,869/5,869 (the new fixture included), input suites green (HTML5 WPT 364, CommonMark 1,335, YAML 391); validator GTests 58/58 (features), 38/38 (AST), 44/44, 14/14 (input), 18/18 (path reporting); `test_html_gtest` 52/52.
- `test_validator_integration` fails `ValidateXMLDocumentWithUnwrapping` and `ValidateEmptyMap` exactly as the base binary does: their schema strings end in `;`, which the parser rejects as a trailing separator, so the schema never loads. Pre-existing, outside every gate.
- Every LR13-9 reproducer now answers correctly on both tiers, and the validator CLI accepts three `<li>` against `<li>*`/`<li>+` and reports a per-position tag mismatch against `<li>, <li>, <li>` given `<p>` children.

**Two defects found under the same symptom, fixed here.**
- **Structural slots reduced to a TypeId.** `array_pattern_simple_type_matches` answered a slot of kind `element`, `map` or `array` by TypeId alone, so `[<li "a">] is [<p>]`, `[{x: 1}] is [{y: int}]` and `[[1, 2]] is [[int, int, int]]` were all true — in bracket patterns before this work, and in element content once it used the same matcher. Only bare kinds (`element`, `map`, `array`, `function`) take the shortcut now; concrete shapes (`lambda_type_is_concrete_attr_shape`), array patterns other than the `TYPE_ARRAY`/`TYPE_LIST` singletons, and function signatures validate in full.
- **A derived object type took its kind from its own content section.** `resolver_object_end` recomputed the kind after `resolver_object_copy_base` had adopted the base's, so `type U : B { extra: int }` under an element-kinded `B` built maps (content dropped) and `type V : P { y: int, string* }` under a map-kinded `P` built elements — both against S2.1.3v2. A derived type now keeps its base's kind and inherits `content_list` unless it declares one (OB7); content on a map-kinded base is a compile error (OB17).

**Deviations from the plan.**
- The slot filler takes an explicit count: an object type's content node is also its member's reduction result, so its `next` link is not part of the pattern.
- `MarkEditor::elmt_copy_with_new_children` no longer allocates a type at all: a content-only copy keeps the old element's type.
- The validator tests share `test/test_validator_patterns.hpp` (`test_any_content_pattern`) for hand-built patterns of N `any` slots.
- An empty content section (`<div;>`) resolves to NULL, i.e. unconstrained, until SO46 rules the *must be empty* spelling.

**Found, not fixed (belongs to P2):** `container_rebuild_with_new_shape` gives an edited element a fresh `TypeElmt` that copies `name` and `content_list` but not `name_id` or `ns`, so editing an element's attributes drops its namespace.

**Planned work, as landed:**

- `lambda/lambda-data.hpp`: `TypeElmt::content_length` → `TypeList* content_list`; `TypeNominal::content_length` removed.
- `lambda/runtime/parse_type_pattern.cpp`: extract `resolve_array_type`'s slot filling into a helper that fills a `TypeList`'s `item_patterns`/`item_is_type_pattern`; `LSF_TP_CONTENT` uses it, and `LSF_TP_ELEMENT` sets `content_list` (NULL when the section is absent or empty — SO46).
- `lambda/runtime/build_ast.cpp`: the element literal stops writing the count; the object-type resolver sets `content_list` from the resolved content section and selects the kind by `content_list != NULL`.
- `lambda/runtime/transpile-mir.cpp`: the content count comes from `AstElementNode::content`'s list type; the "count but no content node" branch goes.
- `lambda/validator/validate.cpp`: both the fast verdict and the reporting path match the element's content against `content_list` with `array_pattern_runs_match` (reuse the array path's slot and run helpers over an element's children); NULL skips.
- Writers of the per-instance count, all removed: `mark_builder.cpp` `ElementBuilder::final`, `mark_editor.cpp` (its content edits no longer touch the type; `elmt_copy_with_new_children` no longer needs a new type), `input.cpp` clone (copy the pointer), `input-mark.cpp`, `input-graph.cpp`, `lambda-data-runtime.cpp` (group type), `lambda-eval.cpp` rebuilds, and the markup parsers: both `increment_element_content_length` definitions and their 122 calls; `block_table.cpp` reads the list length instead.
- Tests: `test_html_gtest` (content starts at 0), the validator GTests (content patterns of N `any` slots or runs, instead of counts), and a new fixture `test/lambda/element_content_pattern.ls` + `.txt`: runs (`<li>*`, `<li>+`, `<li>?`), literal items, open content (`any*`), a bare `<div>`, nominal content, and the merged-string case.
- Docs at landing: S11.1.6 and S2.1.3 footnote rows, the D2.6.6 rows, LR13-9 to the fixed archive, LR_03 §4 and LR_11 §2.1.

### P1 — Elements on the tree (E2)

**DONE 2026-09-25.**

**What landed.**
- **Tag roots: a per-`Input` table, not a tag edge.** A tag edge would have to mint a node without appending a field, which the transition lookup cannot express, so `elmt_tree_root` keeps a pool-owned open-addressing table keyed by the pooled tag name and namespace pointers. A root is an empty `TypeElmt` flagged transition-shared and registered in the type list. Unpooled tag names get no root and keep private types.
- **Attributes: `elmt_put_tree`.** `MarkBuilder::putToElement` now follows or mints the edge exactly as `map_put` does; `map_transition_target_for_add` mints a `TypeElmt`-sized child for an element parent, carrying its `name`, `name_id` and `ns`. Without a usable edge it falls back to `elmt_put`, which now detaches any shared type (a tree node or a pooled chain) before appending — the old guard missed a tree root, which has no chain yet.
- **A separate element budget, 16,384 per `Input`.** Measured: a 13 MiB corpus of 77 real sites needs 5,093 element types (179 roots), a 13 MiB layout-test corpus 1,775 (154 roots); on the 1,024 map budget both would saturate early. An `Input` that reaches it logs `element_tree_budget` once. The map budget and its behaviour are unchanged.
- `elmt_finalize_shape` returns early for a shared type; private fallback types still pool. `elmt_clone_type_for_mutation` now keeps `name_id`, which the HTML5 parser compares tags by — clones became common with detaching.

**Results** (release builds; the old 13 MiB benchmark went with its worktree, so two 13 MiB corpora were rebuilt: `temp/perf/big.html` from the Readability test pages, `temp/perf/wpt.html` from `test/layout/data`):

| Corpus | Elements | Types (P1) | Peak RSS P0 → P1 | Parse P0 → P1 |
|---|---:|---:|---:|---:|
| Readability pages | 58,197 | 5,093 | 109.6 → 86.3 MB | ~75 → ~75 ms |
| Layout tests | 203,136 | 1,775 | 184.0 → 120.4 MB | ~1,750 → ~1,225 ms |

- Differentials against P0: 17,230 HTML files, 99 test inputs, 84 formatter outputs (7 documents × 12 formats) — all identical.
- Lambda baseline 5,869/5,869; validator and HTML GTests green; Radiant baseline green. One Radiant run failed `page_facatology` in the page suite (text 94.5% → 9.6%) while running directly after the Lambda baseline; the standalone page and a rerun of the suite pass, and P0 and P1 release binaries produce byte-identical view trees for it, so the failure was load-induced, as `nojs` has been before.

**Not done here.** Runtime-built elements (`elmt_put` without an `Input`) keep private types; XML and Markdown peaks were not re-measured separately (their elements take the same `ElementBuilder` path the HTML corpora exercise).

**Planned work:**
- Tag roots: a tag edge from the per-`Input` root with a reserved key kind (one graph, one budget, the existing lookup), or a per-`Input` tag table if measurement says the root's fan-out bound gets in the way.
- `ElementBuilder` starts an element on its tag root instead of allocating a `TypeElmt`; `elmt_put` follows or mints edges as `map_put_with_data_growth` does, with the same detach-on-no-edge rule; `elmt_finalize_shape` and the per-element type-list registration go.
- Measure: HTML 13 MiB, XML 19 MiB and Markdown peaks and parse times (release A/B, interleaved medians); tree occupancy against the 1,024 budget on the HTML benchmark and the test262 batch peak.

### P2 — Attribute changes follow map rebuilds (E3)

**DONE 2026-09-25.**

**The bug, confirmed first.** `MarkEditorTest.MapUpdateInlineKeepsSharedTypeIntact` builds two maps by the same adds, so they share a tree type, and adds then retypes a field on one inline. Before the fix the sibling's type claimed two fields and its `name` and `age` read garbage: `container_rebuild_with_new_shape` rewrote any registered type in place, and a tree node or a literal's compile-time type is registered but shared. `ElementUpdateAttrKeepsSharedTypeAndTagId` showed the element rebuild dropping `name_id` (0 where `div` is 46).

**What landed.**
- **Tree rebuilds.** `container_rebuild_with_new_shape` turns the builder's field list into `TypeTreeStep`s — a kept field carries its old record (`like`), a new one a pooled key — and follows them from the container's root: `type_tree_root_like` (the plain-map root, or the element's tag root, found and never made) and `type_tree_follow`. The transition lookup was refactored around a `TransitionKey` so a step keyed by a record matches exactly the edge a string key made; a record-keyed mint copies the record (`shape_entry_copy_as`). All attribute writers that go through `MarkEditor` — the HTML5 parser's merges onto `<html>`/`<body>`, the DOM's `setAttribute`/`removeAttribute`, Radiant editing — follow.
- **Owned types only in place.** A declined tree leaves a fresh type flagged `is_private_clone` with a chain of its own (never pooled, so a later in-place append cannot reach another type); only such a type is edited in place.
- Element rebuilds keep `name_id` and `ns`; map rebuilds keep `js_meta` (D3.4.7).

**Results.** 17,230 HTML files, 99 test inputs and 84 formatter outputs identical to P1; peaks unchanged (86.2 and 120.3 MB). Lambda baseline 5,869/5,869; `test_mark_editor_gtest` 44/45, the failure being `LaneStorageResolverTests.TableAndProjectionsAgree`, which the base binary fails too; Radiant baseline green run alone. Run chained after the Lambda baseline, the page suite twice failed `page_facatology` with identical scores; the page fetches icons, a manifest and font CSS from the network, and alone it passes on every build, so the failure is environmental.

**Not done here.** Runtime-built elements (`elmt_put` without an `Input`) stay private. `elmt_rename` still keeps the old tag, as it always has; it has no callers.

**Planned work:**
- Audit and convert: the HTML5 tree builder's attribute merge onto `<html>`/`<body>`; runtime `elmt_put` callers (group-by, dynamic elements); DOM `setAttribute`/`removeAttribute` (`lambda/dom/dom.cpp`) and Radiant callers; `MarkEditor::container_rebuild_with_new_shape` for maps and elements, and the tag rename. An add follows one edge; a remove, retype or rename replays the sequence from the root. The in-place map rewrite retires.
- Gates: the Radiant DOM editing fixtures (`todo_*`), `test_ui_automation_gtest`, the editor GTests.

### P3 — Retire the shape pool (E7)

- Delete `lambda/core/shape_pool.cpp`/`.hpp`, the interning in `shape_builder.cpp` (the editor keeps a plain field-list builder), `Input::shape_pool`, the transpiler's unread `shape_pool` field, `mem_shape_pool_create` and its registry node.
- Tests: `test_shape_pool_gtest`, the pool cases in `test_namespace_gtest`.
- Docs: D3.4.2's signature clause, LR_08 §7, LR_11 §5, diagrams `d08_memory_regions` and `d11_mark_builder`, `Lambda_Shape_Pool.md` status.

## Gates, every phase

Lambda baseline 100%; Radiant baseline; test262 (P1 onward); the differentials: 108 formatter outputs, 99 test inputs, 21,602 HTML files, 10 corpora; release-build timing and peak RSS on the parse benchmarks. Scripts: `temp/string-func-tools/`.

## Risks

- **Budget exhaustion.** Maps and elements share the 1,024-shape budget; a corpus that exhausts it silently falls back to private types. Measure in P1 before choosing whether elements get their own budget.
- **Editor fixtures.** Sharing element types through the editor's in-place path broke eight DOM fixtures once; P2 must never write a shared type in place.
- **ui_mode.** `DomElement` embeds no `TypeElmt`, only a pointer, so sharing is layout-neutral there; confirm the DOM's attribute writers all go through P2's paths.
