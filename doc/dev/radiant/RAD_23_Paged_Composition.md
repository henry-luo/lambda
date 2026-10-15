# Radiant paged composition

> **Last verified against tree:** 2026-10-15

This document describes the browsing-layout page flow and its publication path. The design authority is [Radiant_Layout_Paged_Media §1.3](../../../vibe/radiant/Radiant_Layout_Paged_Media.md#13-design-principle-2026-10-14-paged-layout-is-normal-layout-with-breaks); progress and completion gates belong to [Radiant_Impl_Paged_Integration](../../../vibe/impl/Radiant_Impl_Paged_Integration.md). **D4.5.1v4 / D4.1.4v5** govern published-edition and retained-page ownership. No formal ruling defines Radiant's page-breaking algorithm.

## Flow and composition

`paged_flow_pass` (`radiant/layout_paged.cpp:4509`) resolves the opening page's body rectangle, applies the print environment, and calls `layout_html_doc` with a `PagedFlowState`. Normal block, inline, table, list, flex/grid and replaced-element layout produce the DOM-embedded views. A browsing pass has no paged state and skips the hooks.

`PagedFlowState` (`layout_paged.cpp:1018`) holds the lazily grown fragmentainer table, paragraph lines, struts, table continuation records, note/float anchors and publication state. Body columns are consecutive fragmentainers on one physical page. `paged_block_before`, `paged_line_commit` and `paged_block_after` (`:3162`, `:3182`, `:3202`) apply forced breaks, keeps, line minima and reservations. Struts move completed geometry forward; table fragmentation fits finished rows and cell content and publishes repeated groups. Monolithic content moves whole when possible and otherwise slices.

`paged_flow_compose_pages` runs after the flow settles. It publishes body fragments and note/float areas, closes sequences, checks terminal-master selection, evaluates page counters and composes margin/static regions. References and terminal-master changes use bounded whole-document convergence passes. Width can restart at a supported top-level forced boundary; a changed width inside an open run produces a diagnostic.

## Publication and ownership

The DOM view is a workspace, reused by the next edition or browsing layout. `paged_publish_children`, `paged_publish_text` and `paged_publish_boundary` (`layout_paged.cpp:3655`, `:3548`, `:3388`) copy page-local box slices, source ranges, shaped text runs and supported boundary paint into the edition. Body fragments do not depend on the old per-element secondary style representation. Page contexts still use `ViewCssStyle`.

`paged_flow_font` pins copies of fonts used by published glyph runs and copies their strings. `paged_publish_image` (`:3440`) loads the layout-selected image source into `ViewTreeModel::image_resources`, which belongs to the edition's generation. Retiring the host image cache or rebuilding the DOM view therefore cannot retire those published images (**D4.5.1v4 / D4.1.4v5**). `load_document_image` in `surface.cpp` scopes resource decoding and animation registration to its explicit document and restores the host's current document afterwards.

Synthetic margin boxes reuse DOM nodes across pages and convergence passes. `PagedMarginBorrows` resets their view properties before releasing borrowed declarations and invalidates the corresponding source-pointer font lookup entries. Published copies remain pinned; the next page can resolve a different font size without changing an earlier page's glyph runs (**D4.5.1v4**).

The page model, preview arrangement, retained generations, snapshot cache and raster/PDF traversal consume these published fragments. `layout_secondary_visit_page` is the shared paint traversal. Static furniture can paint on a blank page even though body content does not.

## Fixed content and counters

`paged_publish_fixed` (`layout_paged.cpp:4077`) repeats viewport-fixed boxes once on each physical sheet after page composition. Collection removes the source parent's flow-page offset, retains local positioned geometry and deduplicates sources encountered through repeated furniture. The copies use the static role and do not advance body mark assignment. A transformed or containing ancestor that establishes the fixed containing block keeps the box in its ordinary source occurrence. Percentage-sized fixed furniture across different page widths still needs qualification.

`LayoutCounterScope::close` (`layout_list.cpp:824`) and `CounterContext::pop_scope_propagate` (`layout_counters.cpp:230`) retain the existing browsing counter rules. An ancestor's same-name counter takes precedence over a preceding sibling's nested reset; the parent frame bounds child resets, and pseudo-elements keep their separate retention rules. This follows the [published CSS Lists 3 §4.4.1](https://www.w3.org/TR/css-lists-3/#inheriting-counters). The [current editor's draft](https://drafts.csswg.org/css-lists-3/#inheriting-counters) uses preceding-sibling inheritance instead; adopting it conflicts with existing browser-reference baselines and remains open in the integration record. Heading `counter-set` declarations update the existing `footnote` counter under both rules.

## Validation pointers

`SecondaryViewTest.PagedFlow*` in `test/test_view_tree_model_gtest.cpp` covers continuous-layout parity, struts, tables, print cascade, marks/references, width restarts, notes/floats, repeated fixed content, independent per-page margin fonts and heading-based footnote counter sets. The repeated-image fixture retires the host cache before raster/PDF painting. These checks are focused evidence; test migration, native/FO provider integration, full publication paint and the aggregate/release gates remain tracked in the integration record.
