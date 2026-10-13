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

## 5. What remains heavy

- `view/radiant_view_math_intensive_scroll` (~21 s):
  - About 9 s is the LaTeX document transform.
  - Hit-testing still re-cascades SVG presentation properties on every pointer event. A per-document presentation-value cache would need invalidation for stylesheet, attribute, inline-style and hover-state changes and for SMIL sampling. It is not done.
- `lambda_handler_allocation` and the slide GC-stress fixtures:
  - Each forced collection re-marks the whole live runtime (loaded `lmd/package/dom` modules).
  - Further gains need an old generation with write barriers.
- Doc fixtures:
  - Each click still runs one full relayout of the ~650-row repo tree. Styles are re-resolved every layout by design, and the intrinsic-size cache keys on exact containing dimensions (5 hits per 2,348 requests).
  - Loosening that key is a layout-correctness change and needs the full layout suites.
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
