# Element Type Sharing — Implementation Plan

**Date:** 2026-09-25
**Status:** PLANNED, not started.
**Design:** [Lambda_Design_Shape_Transitions.md](../Lambda_Design_Shape_Transitions.md) §7 (E1–E7). **Rulings:** D3.4.3v2 (elements share through the transition tree; the shape pool retires), D2.6.6v3 (`content_list` holds a declared type's content pattern; a nominal name is not a tag), S11.1.6v3 (element content is a sequence-pattern slot). **Closes:** [LR13-9](../Lambda_Issue_Ledger.md#lr13-9). **Open, not blocking:** SO46 (the *must be empty* spelling), DO31 (`type <B>`).
**Goal:** one `TypeElmt` per distinct tag, namespace and attribute sequence instead of one per element (about 64 MB of the 13 MiB HTML benchmark's 233 MB peak), declared content validated, and the shape pool gone.

## Phases

Each phase lands on its own, green on the gates below, before the next starts.

### P0 — `content_list` replaces `content_length` (E1, E4, E5, E6)

No sharing yet; this phase removes the one per-instance field and fixes validation.

- `lambda/lambda-data.hpp`: `TypeElmt::content_length` → `TypeList* content_list`; `TypeNominal::content_length` removed.
- `lambda/runtime/parse_type_pattern.cpp`: extract `resolve_array_type`'s slot filling into a helper that fills a `TypeList`'s `item_patterns`/`item_is_type_pattern`; `LSF_TP_CONTENT` uses it, and `LSF_TP_ELEMENT` sets `content_list` (NULL when the section is absent or empty — SO46).
- `lambda/runtime/build_ast.cpp`: the element literal stops writing the count; the object-type resolver sets `content_list` from the resolved content section and selects the kind by `content_list != NULL`.
- `lambda/runtime/transpile-mir.cpp`: the content count comes from `AstElementNode::content`'s list type; the "count but no content node" branch goes.
- `lambda/validator/validate.cpp`: both the fast verdict and the reporting path match the element's content against `content_list` with `array_pattern_runs_match` (reuse the array path's slot and run helpers over an element's children); NULL skips.
- Writers of the per-instance count, all removed: `mark_builder.cpp` `ElementBuilder::final`, `mark_editor.cpp` (its content edits no longer touch the type; `elmt_copy_with_new_children` no longer needs a new type), `input.cpp` clone (copy the pointer), `input-mark.cpp`, `input-graph.cpp`, `lambda-data-runtime.cpp` (group type), `lambda-eval.cpp` rebuilds, and the markup parsers: both `increment_element_content_length` definitions and their 122 calls; `block_table.cpp` reads the list length instead.
- Tests: `test_html_gtest` (content starts at 0), the validator GTests (content patterns of N `any` slots or runs, instead of counts), and a new fixture `test/lambda/element_content_pattern.ls` + `.txt`: runs (`<li>*`, `<li>+`, `<li>?`), literal items, open content (`any*`), a bare `<div>`, nominal content, and the merged-string case.
- Docs at landing: S11.1.6 and S2.1.3 footnote rows, the D2.6.6 rows, LR13-9 to the fixed archive, LR_03 §4 and LR_11 §2.1.

### P1 — Elements on the tree (E2)

- Tag roots: a tag edge from the per-`Input` root with a reserved key kind (one graph, one budget, the existing lookup), or a per-`Input` tag table if measurement says the root's fan-out bound gets in the way.
- `ElementBuilder` starts an element on its tag root instead of allocating a `TypeElmt`; `elmt_put` follows or mints edges as `map_put_with_data_growth` does, with the same detach-on-no-edge rule; `elmt_finalize_shape` and the per-element type-list registration go.
- Measure: HTML 13 MiB, XML 19 MiB and Markdown peaks and parse times (release A/B, interleaved medians); tree occupancy against the 1,024 budget on the HTML benchmark and the test262 batch peak.

### P2 — Attribute changes follow map rebuilds (E3)

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
