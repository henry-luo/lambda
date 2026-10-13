# Radiant UI Test Tuning (2026-10-13)

- **Status:** implemented in the working tree, uncommitted. `make test-radiant-baseline` green (5,750 tests, 351 layout partials unchanged; wall 158 s → 134 s, UI alias 127 s → 94 s, measured while another session was also loading the host). `make test-lambda-baseline` green except three REPL prompt tests that need a UTF-8 `LANG` (they pass with `LANG=en_US.UTF-8`).
- **Scope:** the event-driven UI fixtures run by `test/test_ui_automation_gtest.exe` (manifest `test/ui/ui_test_manifest.json`), their Make/runner wiring, and the engine hot paths those fixtures exposed.
- **Formal linkage:** no language or runtime ruling changes. GC changes keep precise rooting (AGENTS rule 15) and only speed ownership lookup.
- **Related:** [Radiant_Design_UI_Test.md](../radiant/Radiant_Design_UI_Test.md) (runner unification), [Radiant_UI_Automation2.md](../radiant/Radiant_UI_Automation2.md) / [3](../radiant/Radiant_UI_Automation3.md) (phase fixtures).

## 1. Where the time went

Measured from the 2026-10-12 gate run (`baseline,dtna,timeline` + `view` + `dom`, 723 fixtures, ~970 s child CPU, debug `-O3` host):

- 85% of child time was the event loop of a few app families; process startup was ~11%.
- 266 fixtures under 150 ms cost ~27 s together, so merging small fixtures saves little time.
- Gate timings are inflated ~2.2× by contention: the 129 fixtures over 1.5 s took 870 s in the gate and 395 s run alone.
- Headless time is virtual: `wait` and the 50 ms default event interval cost no wall time. Slow fixtures are CPU-bound.

## 2. Runner and Make fixes

| Problem | Fix |
|---|---|
| `make test` / `make test-all` ran all ~723 gate fixtures twice: the extended runner used `--suite all` | Extended runner now runs `--suite bold,editor,edit,hit-test` (`build_lambda_config.json`) |
| `--test` patterns with `-` matched nothing when they had a wildcard; `drawing-editor-e2e` ran 1 of 6 fixtures | Test patterns are normalized like fixture ids (`ui_normalize_id_chars`) |
| A misspelled or stale `--test`/`--tag` selector silently ran zero fixtures | Each selector must match a fixture or the runner exits 2 (`ui_unmatched_selector`) |
| Editable targets ran shared fixtures twice and launched the runner up to 5 times | Selections are Make variables; composite targets pass their union to one runner call |
| `editable-package-disabled` hand-ran a fixture the manifest already owns | It runs through the runner |
| `dom_pkg_*.json` globs duplicated `dom_*.json` | Removed |

## 3. Fixture consolidation (1028 → 1016)

- **Exact subsets removed:** `rte_selection_release_visible` and `editor4c_phase_c/paste-multi-paragraph`. Their actions are a prefix of other fixtures and every assertion recurs at the same point.
- **Byte-identical pages:** five `test/ui/baseline_*.html` copies deleted; fixtures load `test/layout/data/baseline/`.
- **Same-page merges** (the union of assertions passes):

| Merged into | Absorbed |
|---|---|
| `ui_phase1b_text_align` | `ui_phase1c_click_positions`, `ui_phase3a_drag_select` |
| `ui_phase1d_interactive_elements` | `ui_phase1e_structural_elements` |
| `ui_phase5g_transform` | `ui_phase5c_auto_wait` (with its `default_timeout`) |
| `ui_phase3_form_controls` | `ui_phase3_viewport` (its 800×600 viewport) |
| `ui_phase5b_navigate` | `ui_phase5b_navigate_back` |
| `test_shift_down_extends_selection` | `test_shift_right_extends_selection` |
| `svg_text_path` | `svg_text_path_interaction` |
| `dtna/gallery_elements` | `dtna/gallery_spacing`, `dtna/gallery_description_spacing` |

- **`doc_editor copy.html`** (stray file) deleted.
- **Not merged:**
  - Doc fixtures: the fork's filter only inserts at the caret, so switching files needs a filter clear that re-renders the whole expanded repo tree. That costs more than a new process until directory navigation is faster.
  - Tom Select trio: see §6.

## 4. Engine fixes the fixtures exposed

Each fix keeps behaviour identical. All `assert_*` results are unchanged, and the Radiant baseline layout-partial count stays at 351.

| Hot path | Root cause | Fix | Effect |
|---|---|---|---|
| `dom_flat_tree_parent` → `dom_element_html_tag` → `dom_element_namespace_uri` (24% of a doc click) | Tag predicates derived the namespace (two ancestor walks plus case-insensitive compares) before the cheap local-name test, once per ancestor, so the cost was quadratic in depth | `dom_element_is_html_tag(element, tag)` tests the local-name id first. `inert` and `dir` check their attribute before the namespace | Doc fixtures −25–30% |
| `css_is_mathml_element` (11%) | 42 `strcmp`s per element per display resolution | First-byte gate (`m`, or `semantics`/`annotation*`) | — |
| `dom_parent_element` | `strcmp("#document-fragment")` on every parent | `#` first-byte gate | — |
| Harness clicks on clipped targets (34% of `doc_viewer_toml`) | `scrollIntoView` requested a full relayout just to move a scrollport | `layout_scroll_into_view_without_relayout`: when layout is otherwise clean, resolve the scroll against committed geometry. The viewport step is shared with layout's finish phase | `doc_viewer_toml` 4.6 → 3.1 s |
| `fn_radiant_measure_text` | `radiant_query_font_key` rejected a query with no faces, so the system-fonts-only case was never cached. Each call built a new `FontContext` and rescanned every font directory (2,585 scans in `timeline/editing`) | The key accepts missing and empty face lists, exactly as `radiant_build_query_fonts` does | `timeline/editing` 12.4 → 2.7 s; timeline suite −61%, charts −43% |
| `dom_node_schedule_detached` (34% of a superlambda frame) | Every replaced node scanned every DOM record looking for detach candidates | Candidates are kept on their own intrusive list (`registry->candidates`). A root inside an existing candidate now returns before ending nested candidacies, which removes a dependence on record order | superlambda −47%, tetris −43% |
| GC mark ownership (`gc_object_zone_owns` 21% + bump-list walk 6% under `LAMBDA_GC_FORCE_EVERY=1`) | A binary search over thousands of slab ranges per traced word, then a linked-list walk for bump blocks | An exact page index (pages → range owner; a page shared by two ranges falls back to the search; a failed grow retires the index). Ranges carry their bump block as foreign owner. A zone-backed heap skips the dead bump-list walk | `lambda_handler_allocation` 13.2 → 11.0 s |
| SVG hit-testing (53% of the math-scroll fixture) | `dom_svg_presentation_value` re-runs the cascade per property, matching every rule's selectors even when the rule cannot set the property | `css_query_declaration_relevant` filters each rule's declarations, by property code first, before any selector match | Event loop −10% under load |

Heavy fixtures (the 127 still present that ran over 1.5 s in the gate), run alone: **393 s → 276 s (−30%)** before the CSS-query fix. By family: timeline −61%, chart −43%, superlambda −43%, tetris −40%, doc −30%, lambda −17%, slide −14%, dtna −12%.

### 4.1 Follow-up round (2026-10-13, commits a9e84b635, 97c8e2102)

| Hot path | Root cause | Fix | Effect |
|---|---|---|---|
| Full-document recascade on `:hover`/`:active` (33% of dtna `rate_modes`) and every page load | `apply_active_rules_to_tree` matched the whole flat rule program against every element | Rule program index: rules bucketed by one id/class/tag their subject compound requires (lowercased, so buckets only widen); an element tests its own buckets plus unkeyed rules in program order. `::slotted`, generic pseudo-classes/elements (`:host`, `::part`), `:scope` and scope/container/nested-declaration rules stay unkeyed | `rate_modes` 6.5 → 5.1 s; recascade left the top of the profile |
| `view_focus_is_hidden` (18% of `rate_modes`) | Each flat-tree ancestor called `dom_ensure_computed`, which recascades that node's whole style chain while anything is dirty, so the chain was recascaded once per level | Ancestors already on a refreshed chain are skipped; slot-assigned parents off the chain are still ensured | `button_modes` 5.7 → 4.6 s, `gallery_elements` 4.5 → 3.8 s |

Tried and reverted: a thread-local last-range cache in front of the GC page index. Its hit rate was low, and macOS TLV access (`_tlv_get_addr`) cost about as much as it saved.

After this round the 127 heavy fixtures run alone in 248 s (393 s at the start, −37%). By family: doc −39%, dtna −42%, timeline −61%, superlambda −41%, tetris −40%, chart −34%.

Unit test for the font-key fix: `test_radiant_text_query_gtest` (commit ee56c3536). It fails with 24 context builds against the old key and passes with at most 1.

### 4.2 Doc tree round (2026-10-13)

The doc fixtures spend most of their time in pane clicks: a property toggle, tab switch or filter edit in the document pane. A profile of `doc_viewer_toml` showed where one such click went (about 160 ms of layout):
- The whole app template re-rendered, because all pane state lived on `doc_editor_app`.
- `.doc-editor`, a row flex, re-measured its clean `.file-panel` three times: item measurement, hypothetical cross size, and the panel's nested column measuring `.project-tree`.
- 37% went to re-resolving styles during those measurements.

| Change | Where | Effect |
|---|---|---|
| Pane-local state: the preview tab, property filter and property open/closed/more paths are state on a stateful `document_pane` model. The app rebuilds the model only for a new file or a CSV change; CSV state stays on the app because a column release can land over the file panel | `lmd/package/doc/doc_viewer.ls`, ported to the `test/ui/doc_editor.ls` fork | Pane click dom_patch 10 → ~1.5 ms |
| Retained flex items (`FlexRetainedPass`): a clean item of an incremental pass replays its last measurement, hypothetical cross size and content pass under the same keys | `layout_flex_measurement.cpp`, hooks in `layout_flex.cpp` and `layout_flex_multipass.cpp`; see RAD_01 §6 | Intrinsic requests per pane click 2,353 → 15 |

Pane-click layout, `doc_viewer_toml`: **~160 ms → ~18 ms**. This is the debug-build `rebuild_incr` layout time of a pane click, measured at low machine load; without replay the same click takes ~65 ms.

Three pre-existing incremental-layout bugs turned up while checking replay against a full recompute. All three are fixed:
- **Measurement leak.** An intrinsic measurement re-resolves the styles of the elements it walks against the measured width, and the committed pass early-returned and kept those values. The doc file filter's `width: 100%` became 299 or 272 px instead of 271 after any relayout.
  - Now every walked descendant is flagged `ELMT_FLAG_MEASURE_RESTYLED`, and the committed pass that next lays it out resolves it again. Shift reuse and replay keep a flagged node's committed geometry, so the flag simply persists until that layout. (A first version made the stability walk reject flagged nodes; that disabled block shift reuse for every tree a measurement had walked, and `ReusesCleanRowsAfterDirectoryClose` caught it.)
  - The query's root is exempt (`LayoutMeasurementRootScope`): it resolves in its caller's own context, which grid items rely on to resolve `height: 100%` against their grid area.
  - Flow children also recurse with their parent's content box as the percentage basis.
  - Tried and reverted: skipping re-resolution of styles-current elements in `calculate_max_content_height` regressed 8 `wpt-css-sizing` tests for exactly that grid reason.
- **Double margin collapse.** Normal-flow collapse consumes `BoundaryProp` margins in place. A relayout of a clean block kept its style and collapsed the consumed margin again: a LaTeX `p`→`h3` gap of 21.12 px became 13.12 px. The committed pass now re-resolves an element whose used boundary was consumed (`layout_boundary_consumed_by_flow`).
- **Unmarked rebuilt nodes.** `set_layout_dirty_subtree` used a 256-entry stack and silently skipped the tail of a wide rebuilt subtree. It now uses `view_geometry_walk_dom_tree`.

A replay bug found by the same check, before landing: a committed style pass resets an item's `PositionProp`, including its abspos list. If that happened before a replay (an outer measurement had flagged the item), the absolutely positioned source pane was dropped from paint. A committed style pass now drops the item's content memo (`flex_retained_forget_content`).

Regression tests:
- `test/ui/dom_mutation_margin_relayout`: a JS mutation re-lays out clean `p`/`h3` blocks. Without the consumed-boundary fix the heading moves from y = 47.1 to 31.1.
- `doc_viewer_tree_selection` now asserts the file filter stays 271 px after a folder collapse. Without the restyle flag it is 272.
- `test/ui/dom_mutation_flex_retained` with `RadiantViewTest.ReplaysCleanFlexSiblingOfMutatedPane`: a pane mutation replays the clean side panel (`flex_retained` > 0) and keeps its row geometry. The profile parser in `test_radiant_view_gtest.cpp` now takes the field name.

Verification: a differential run of every UI fixture with replay on and off, with an SVG snapshot after each interaction (`temp/ui-consolidation/abx.py`). Results:
- All 1,000 fixtures give identical snapshots and exit codes, except:
  - fixtures whose output is nondeterministic: `editorjs` random block ids and timestamps, and save/close editor fixtures that leave file state behind;
  - exits that flip between identical runs;
  - float noise under 0.0001 px in two `dtna` fixtures.
- UI gate: 720/720. Extended suites: 69 of 302 fail, the same count as 2026-10-12 master.
- Layout baseline: every suite passes except `markdown` `md_nuxt-readme` (100% → 99.3% elements). That regression reproduces with all of this round's full-layout changes disabled, so it comes from code outside this round.

## 5. What remains heavy

- `view/radiant_view_math_intensive_scroll` (~21 s):
  - About 9 s is the LaTeX document transform.
  - Hit-testing still re-cascades SVG presentation properties on every pointer event. A per-document presentation-value cache would need invalidation for stylesheet, attribute, inline-style and hover-state changes and for SMIL sampling. It is not done.
- `lambda_handler_allocation` and the slide GC-stress fixtures:
  - Each forced collection re-marks the whole live runtime (loaded `lmd/package/dom` modules).
  - Further gains need an old generation with write barriers.
- Doc fixtures:
  - Pane clicks replay the file panel (§4.2). A file selection still re-renders the whole tree, because the selection highlight is app state; moving it into the rows would let a selection re-render only the two affected rows.
  - The incremental rebuild still deep-copies every re-render into fresh DOM, so a re-rendered subtree is always fully restyled and relaid. That copy is an implementation choice, not a ruling: **D4.5.3v2** governs only DOM construction from Lambda values and embraces retention downstream. [Lambda_Proposal_DOM_Reconciliation](../Lambda_Proposal_DOM_Reconciliation.md) designs keeping and patching existing occurrences; its condition for reuse (Q2) and phasing (Q4) await review.
  - `test/ui/doc_editor.ls` is an 86% fork of the shipped `lmd/package/doc/doc_viewer.ls`. Retiring it is the owner's decision.
- dtna `rate_modes` and `button_modes` (6–7 s): not profiled yet.

## 6. Open issues found

- **Tom Select cross-instance input:**
  - With three Tom Select instances on one page, keyboard input to whichever instance is used second never filters it, while the third works. A virtual-time `wait` doesn't help.
  - This looks like a Radiant focus/blur sequencing bug.
  - Repro: `temp/ui-consolidation/tomselect_merged_repro.{html,json}`; copy it to `test/ui/dom/` so its script path resolves. The three original fixtures are kept.
- **Extended suites failing:** 69 of 302 `editor`/`edit`/`hit-test` fixtures fail on 2026-10-12 master (for example `rte_typing_at_caret` 2/4, `edit_md_type_save` 9/12). No gate runs these suites.
- **Pre-existing input-suite failures**, not caused by this work:
  - `test_css_dom_crud`: 2 `Ui*` DOM-build tests. They fail identically with HEAD `dom_lifecycle.cpp` and HEAD `dom_element.cpp`.
  - `test_css_parser_unit`: `Declaration_Display_CustomLayoutDeclarationParser`.
  - `test_css_roundtrip_unit`: 7 framework files.
  - `test_css_to_mark_gtest`: `RoundTripFormattingTest`.
  - `test_html_css_gtest`: the `AVLTreePerformance` timing test.
