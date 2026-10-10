# Lambda Test Map

Where every test lives, which gate covers which change, and how to run or add one test.
Written for coding agents; build and Make details live in [`doc/dev/Make_Guide.md`](../doc/dev/Make_Guide.md).

Last verified against tree: 2026-10-07

## Ground rules

- Run everything from the repo root. Harnesses use repo-relative paths (`./lambda.exe`, `test/...`).
- `make build-test` builds every `test/*.exe` listed in `build_lambda_config.json` → `test.test_suites`.
  To add a gtest binary, add an entry there (`binary`, `source`, `libraries`, optional `category`/`parallel`) and rebuild; never edit the generated premake `.lua` files.
- `make test-library`, `test-input`, `test-validator`, `test-lambda` and `test-std` depend only on `build`. Run `make build-test` first.
- `test`, `test-all`, `test-all-baseline` and `test-lambda-baseline` switch `lambda.exe` to the debug build (`make debug`).
- Per-binary results go to `test_output/<name>_results.json`; Radiant baseline logs go to `temp/_radiant_*.log`.
- Never update a golden to make a failure go away (AGENTS.md rule 1). Never weaken `test_js_test262_gtest` (rule 18).
- Every `.ls` in a golden-driven directory is a test and needs its golden (rule 8); a script without one fails the run. Name helpers, imported modules and manual playground scripts `_*` or `mod_*` instead.
- A test blocked by an open bug or a missing ruling is **parked**: renamed `_<name>.ls` with a `// PARKED <date> — <ledger ID or S#>` header, and its spec-correct golden kept as `_<name>.expected.pending`. Un-park it in the change that fixes the issue. Parked tests are listed in `vibe/Lambda_Issue_Ledger.md` ("Std test triage — 2026-10-07").
- Worktree, flaky-baseline and debug-vs-release gotchas: [`doc/dev/Developer_Guide.md` §7](../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas).

## 1. Which gate do I run?

**Bold** gates must pass 100% (AGENTS.md). `make test-all-baseline` is the widest baseline gate. Run it before a merge that touches more than one area.

| Change area | Must-pass gate | Quick targeted check |
|---|---|---|
| Lambda core: `lambda/runtime/`, `lambda/core/`, parser, MIR JIT | **`make test-lambda-baseline`** | `./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*<name>*'`; `./test/test_mir_emission_gtest.exe --gtest_filter='Fixtures/*<name>*'`; `./test/test_lambda_errors_gtest.exe --gtest_filter='NegativeScriptTest.*'` |
| Lambda packages: `lmd/package/**/*.ls` | **`make test-lambda-baseline`** | `./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*scene3d_*'` (also `chart_`, `latex_`, `math_`, `mermaid_`, `graphviz_`, `structurizr_`, `slide_`, `editor_`, `edit_`); `make test-math-corpus` |
| Native geographic maps: `radiant/geomap*`, `lmd/package/map*` | **`make test-lambda-baseline`** + **`make test-radiant-baseline`** | `make test-map` (frames, indexed picking, native pointer/control input), `make test-map-export`, `make test-map-reference` (first `npm ci --prefix test/map`); fixtures and pinned expression corpus in `test/map/`, functional goldens in `test/lambda/map/` |
| Input parsers / formatters: `lambda/input/`, `lambda/format/`, `lambda/io/` | `make test-input-baseline` (5 corpora, also run by test-lambda-baseline) + `make test-input` (70-binary input suite) | `./test/test_input_model_gtest.exe`; `./test/test_markdown_gtest.exe --baseline`; `./test/test_input_roundtrip_gtest.exe --gtest_filter='JsonTests.*'`; `./test/test_html_gtest.exe` |
| CSS engine: `lambda/input/css/` | **`make test-radiant-baseline`** + `make test-input` (`test_css_*` binaries) | `./test/test_css_parser_gtest.exe`; `make layout test=<file>` |
| Radiant layout / render / events: `radiant/` | **`make test-radiant-baseline`** + `node test/test_run.js --target=radiant --category=baseline` (the gate does not run the radiant suite's own gtests) | `make layout test=baseline_301_simple_margin`; `make layout suite=<dir>`; `./test/test_ui_automation_gtest.exe --suite baseline --test <id>`; `make test-render test=<name>` |
| LambdaJS: `lambda/js/` | **`make test-lambda-baseline`** + `make test262-baseline` | `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/*<name>*'`; `make test-js262-prelim`; `make test-js-parity` |
| DOM: `lambda/dom/` | **`make test-radiant-baseline`** (DOM UI fixtures, WPT input-events) + **`make test-lambda-baseline`** (JS DOM fixtures) | `make dom-ui test=<id>`; `./test/test_lambda_domnode_gtest.exe`; `./test/test_dom_range_gtest.exe`; `./test/test_wpt_dom_nodes_gtest.exe` (extended) |
| Validator: `lambda/validator/` | `make test-validator` (validator suite, part of test-all-baseline) | `./test/test_validator_gtest.exe --gtest_filter='ValidatorTest.*'`; `./test/test_validator_input_gtest.exe` |
| `lib/` utilities | `make test-library` (library suite, part of test-all-baseline) | `./test/test_str_gtest.exe`, `./test/test_arena_gtest.exe`, … (source `test/lib/test_<name>_gtest.cpp`) |
| Jube / Node modules: `lambda/jube/`, `lambda/module/node_*` | **`make test-lambda-baseline`** (builds node-core/node-fs) + `make node-regression-gate` | `make node-baseline`; `make test-jube-module-integrity`; `make check-host-exports` |
| Release-only checks (perf, JIT timing) | n/a | `make release` first (rule 10). Never run it in a worktree. |

## 2. Suites (`build_lambda_config.json` → `test.test_suites`)

`node test/test_run.js` runs these. Flags: `--target=<suite>`, `--exclude-target=<suite>`, `--category=baseline|extended`,
`--exclude-test=<name>[,<name>]`, `--raw`, `--sequential`. Env: `LAMBDA_TEST_BIN_DIR`, `LAMBDA_TEST_IDLE_TIMEOUT` (s), `LAMBDA_TEST_MAX_CONCURRENT`, `LAMBDA_TEST_HEAVY_LOAD=1`, `LAMBDA_UI_TEST_JOBS`.
A test is killed only after it produces no output for the idle timeout. With `--category=baseline` the runner passes `--baseline` to `test_js_gtest`, `test_wpt_html_parser_gtest` and `test_markdown_gtest`.

| `--target` | Category | Binaries | Notable members |
|---|---|---|---|
| `library` | baseline | 52 | `test/lib/test_*` (str, arena, gc_heap, mempool, url, font…), `test_rdb_gtest`, `test_serve_gtest`, `test_avl_tree_perf` |
| `radiant` | 14 baseline, 15 extended | 29 | baseline: `test_display_list_gtest`, `test_retained_display_list_gtest`, `test_rdt_vector_gtest`, `test_view_reuse_gtest`, `test_layout_custom_gtest`, `test_css_cascade_memory_gtest`, animation/media players. Extended: WPT runners (sources `test/wpt/test_wpt_*_gtest.cpp`), `test_chromium_contenteditable_gtest` |
| `input` | baseline | 70 | mark builder/reader/editor, HTML, markdown, YAML, `test/css/*`, graph parsers, `test_input_model_gtest` (OBJ/MTL/glTF/A3D; fixtures in `test/input/model/`), `test_validator_input_gtest`, `test_lambda_domnode_gtest`, `test_dom_range_gtest` |
| `validator` | baseline | 3 | `test_validator_gtest`, `test_ast_validator_gtest`, `test_validator_path_reporting` |
| `lambda` | 31 baseline, 2 extended, +2 scripts | 33 | `test_zip_gtest` (ZIP32/ZIP64, Word/JAR packages, lazy decoding and ZIP writing), `test_lambda_gtest`, `_std`, `_errors`, `_proc`, `_repl`, `_concurrency`, MIR emission/ratchet/gc-stress, `test_js_gtest`, `test_ts_gtest`, `test_js_test262_gtest --prelim` (runs as `test_js_test262_prelim_gtest`). Scripts: `test/lambda/math/run_corpus.mjs` (baseline), `test/run_js_parity.mjs` (extended) |
| `extended` | extended | 11 | `test_interp_gtest`, `test_lambda_extended_gtest`, `test_ui_automation_gtest`, `test_page_load_gtest`, `test_radiant_view_gtest`, `test_layout_fuzzy_gtest`, `test_math_gtest`, `test_http_gtest`, `test_radiant_online_view_gtest` |
| `jube` | 3 baseline, 2 extended | 5 | `test_rb_gtest`, `test_bash_official_gtest`, `test_bash_run_gtest` (no build compiles these front ends, see `doc/dev/Lambda_Jube_Runtime.md`), `test_js_test262_gtest` (full), `test_node_gtest` |

## 3. Aggregate make targets

| Target | Runs |
|---|---|
| `make test` | every suite except `jube`, and minus `test_radiant_online_view_gtest`. This is not `test-all`, despite what `make help` says |
| `make test-all` / `test-all-baseline` / `test-extended` | all suites / `--category=baseline` / `--category=extended`. `test-extended` also runs `dom-ui-run`. |
| `make test-lambda-baseline` | `test-input-baseline`, then `lambda` suite baseline (excludes `test_node_prelim_gtest`, `test_lambda_concurrency_gtest`), one merged report |
| `make test-lambda-full` | the above plus concurrency, `test_lambda_extended_gtest`, `test_lambda_domnode_gtest`, `test_validator_input_gtest` |
| `make test-input-baseline` | `test_wpt_html_parser_gtest --baseline`, `test_markdown_gtest --baseline`, `test_yaml_suite_gtest`, `test_math_ascii_gtest`, `test_math_gtest` |
| `make test-radiant-baseline` | native `test_scene3d_gtest`, filtered `test_view_reuse_gtest`, then `run-radiant-baseline`: layout baselines (`LAYOUT_BASELINE_SUITES` in the Makefile), page-suite snapshot, UI automation `--suite baseline,dtna` and `--suite view`, `test_radiant_view_gtest` excluding `RadiantViewTest.Doom*` (extended), `test_rdt_vector_gtest`, `test_page_load_gtest`, `test_css_cascade_memory_gtest`, `test_layout_fuzzy_gtest`, render visual `--baseline`, `dom-ui-run`, WPT css-syntax and input-events |
| `make run-radiant-baseline` | the same Radiant checks with no rebuild |
| `make test-layout-baseline` | layout baseline suites only |

Focused targets agents commonly need (`make help` lists ~113; the Makefile has ~240):

- **Native scenes:** `make test-scene3d` or `./test/test_scene3d_gtest.exe` (full macOS host with desktop graphics access). The runner explicitly enables hidden GLFW contexts; missing graphics fails rendered tests. Imported OBJ/MTL/glTF/A3D models and fixed-time playback are covered by `test_input_model_gtest` and `Scene3dTest.ImportedAssetGalleryRendersTexturedObjAndPlaysGltfAndA3dClips`; the viewable fixture is `test/demo/scene3d/asset-gallery.ls` with resources under `assets/loading/`. Package goldens live in `test/lambda/scene3d/`; page, sizing and texture fixtures live in `test/scene3d/`. Phases II/III add [selected WebGL2/Three.js and shared animation fixtures](webgl/README.md) in `test/webgl/` and the pinned gallery under `test/demo/scene3d/`.
- **Lambda DOOM:** `./test/test_lambda_gtest.exe --gtest_filter='DoomDemoTests.*'` compares the demo's rules/resources across interpreter, automatic tiering and MIR JIT. `./test/test_radiant_view_gtest.exe --gtest_filter='RadiantViewTest.Doom*'` runs the effects/input/audio probes and six native gameplay/episode/lifecycle replays with forced GC, freed-memory poisoning and tracked close checks. These two Radiant View tests run through `make test-extended` (`--target=extended` in the Node runner), outside `make test-radiant-baseline`; shared CSS 3D regressions remain in the baseline's `test_scene3d_gtest`. Canonical fixtures and goldens live in [`test/demo/doom/`](demo/doom/README.md).
- **Layout and render:** `layout` / `test-layout` (`test=`, `suite=`, `pattern=`, `update=1`), `layout-snapshot-check suite=page`, `capture-layout test=`, `test-render` (`test=`, `suite=`, `pattern=`, `update=1`), `capture-render`.
- **Native paged providers:** `./test/test_view_reuse_gtest.exe --gtest_filter='TypesetTest.*:SecondaryViewTest.Native*'` exercises source-neutral boxes/paragraphs, exact metrics, ordered glue, selectable page policies, publishing stores, native region slices, deferred-float flushing and journaled hold/reinsert transitions. The combined publishing fixture also covers reference convergence, master-width changes and rollback while an earlier edition remains retained. Physical-page controls cover queue draining, leading/sidedness/end-padding blanks, fixed pages, callback order and a shared transition budget. Native flow and note controls write retained glyph/image PNG/SVG/physical PDF artifacts under `temp/paged-media-impl/`; ownership follows D4.5.1v4/D4.1.4v5. The complete synthetic TeX architecture gate is tracked in `vibe/radiant/Radiant_Layout_Paged_Media.md` §5.10; these fixtures do not provide a TeX frontend.
- **Nested native streams:** `./test/test_view_reuse_gtest.exe --gtest_filter='SecondaryViewTest.NativeNested*'` checks registered child providers, parent return boundaries, repeated paragraph invocation, changed widths, exact records, all-provider rollback, cycles and bounded depth/cursor failures. The nested combined publishing fixture retains marks, references, notes, floats and held/reinserted plans across failed editions and source-view release. Independently sized columns and margin regions remain separate common-geometry work.
- **FO decoration arithmetic:** `./test/test_view_reuse_gtest.exe --gtest_filter='FoExpressions.DecorationComponents*:SecondaryViewTest.FoDecorationArithmetic*'` checks typed shorthand components, arithmetic widths/padding, computed colors, property precedence and transactional rejection. `RenderOutputParity.ComputedBorderShorthandWidthsMatchLiteralWidthsAcrossOutputs` covers the shared continuous CSS projection path. `paged_media_decoration_expressions.fo` / `.rpd` joins the 44 exact FO/native preview/PDF pairs. The existing `radiant_vmap_projection` Lambda golden checks generated SVG fragment context.
- **Paged computed typography:** `./test/test_view_reuse_gtest.exe --gtest_filter='SecondaryViewTest.ComputedTypography*:SecondaryViewTest.FoTypography*'` checks quoted font-family groups, normal versus zero spacing, declaring-font inheritance, original FO family ancestors, scalar spacing expressions, retained editions and atomic type/domain rejection. `paged_media_typography_bindings.fo` / `.rpd` adds a three-page exact preview/PDF pair to `RenderOutputParity.FoAndNativePageControlsSharePreviewAndPhysicalPdfPages`. Common selected-style ownership follows D4.5.1v4/D4.1.4v5. XSL spacing ranges remain pending.
- **Paged graphic constraints:** `./test/test_view_reuse_gtest.exe --gtest_filter='RadiantImageSizing.*:SecondaryViewTest.NativeImageAllowedScales*:SecondaryViewTest.FoImageScale*:SecondaryViewTest.NativeComputedImageQueries*:SecondaryViewTest.FoGraphicPropertyQueries*:FoExpressions.ScaleLists*'` checks inherited scale sets, typed expression lists, discrete/any selection, exact and fit sizing, failed-edition rollback and retained paint. `paged_media_graphics_scales.fo` and `paged_media_graphics_bindings.fo`, each paired with `.rpd`, join physical export parity; the binding pair also checks owner-font computation, keyword and whole-list queries. Generation ownership follows D4.5.1v4/D4.1.4v5. Apache FOP 2.11 rejects allowed-scale properties; retain that strict reference failure and use the specification-derived SVG/Poppler dimension audit.

- **UI and DOM:** `test-ui-automation` (`ARGS=`), `test-ui-bold` (bold contracts/native fixtures across tiers), `dom-ui test=`, `test-page-load`, `test-reactive-ui`, `test-editable`, `test-wpt-contenteditable`, `test-css-cascade-memory`, `test-pdf-render`, `test-svg-export`, `test-svg-paint`, `test-svg-smil`.
- **Lambda tiers and GC:** `test-lambda-interp`, `interp-sweep`, `test-gc-rooting`, `test-mir-gc-stress`, `check-error-recovery`, `test-grammar-s16`.
- **JS and Node:** `test-js262-prelim`, `test262-baseline` (`VERBOSE=1`), `test262-full`, `test262-update-baseline`, `test-js-parity` (`SUITE=js|test262`, `MODE=mir|ast`), `test-js-opt`, `test-js-parser-diff`, `node-baseline`, `node-regression-gate`, `node-full`, `node-update-baseline`.
- **Packages and data:** `test-math-baseline`, `test-math-corpus`, `test-mathcmp` (Lambda PNG versus pdfLaTeX; `ARGS='--case Functions'`, see [math comparison instructions](lambda/math/README.md)), `test-graph-mermaid`, `test-graph-graphviz`, `test-graph-structurizr`, `test-rdb-drivers-local`.
- **Other:** `test-wasm`, `test-coverage`, `lint`, `check-tutorial`, `check-doc-code`.

## 4. Script-driven harnesses

| Harness | Fixture dirs | Discovery | Expected output | Adding a test |
|---|---|---|---|---|
| `test_lambda_gtest` | Functional (`lambda.exe <f>`): `test/lambda`, `test/lambda/{chart,map,latex,math,editor,editing,edit,slide,scene3d,ui_dtna,ui_bold}`, `test/lambda/graph/{mermaid,graphviz,structurizr}`, `test/demo/doom/tests`. Procedural (`lambda.exe run <f>`): `test/lambda/{proc,conc,pdf}`, `test/benchmark/{awfy,r7rs,beng,kostya,larceny}` | Non-recursive. Every `*.ls` is a case; one without its golden **fails** ("No expected output") unless named `_*`, `mod_*` or `schema_*` (helper / module / playground, see `test/test_script_discovery.hpp`). Name is `<parentdir>_<stem>`, with no prefix in `test/lambda` itself. `SLOW_BENCHMARK_TESTS` in `test_lambda_helpers.hpp` are never instantiated. | Sibling `<stem>.txt`. `<stem>.mac.txt` / `.linux.txt` / `.win.txt` wins on that OS. Compared after the `##### Script` marker, with trailing whitespace and `__TIMING__:` lines stripped. | Add `foo.ls` + `foo.txt` in a listed dir (rule 8). A new dir goes into `FUNCTIONAL_`/`PROCEDURAL_TEST_DIRECTORIES` at the top of `test/test_lambda_gtest.cpp`. That file also holds hand-written `TEST`s: tier parity over interp/jit/auto, typed paths on `test/mir/lambda/*.ls` + `.txt`, and error-without-crash checks. |
| `test_lambda_extended_gtest` | `test/lambda/ext`, `test/lambda/proc-ext` (procedural) | Same helper. Names are `ext_<stem>` and `proc_ext_<stem>`. | `.txt` (+ platform override) | Same as above |
| `test_lambda_std_gtest` | `test/std/**` (recursive) | Every `*.ls`; one without its `.expected` fails unless named `_*`/`mod_*`/`schema_*`. `// Mode: procedural` in the first 5 lines means `run`. Name is the relative path with `/` → `_`. | `<stem>.expected` (+ `.mac/.linux/.win.expected`) | Write the `.expected` by hand. `bash test/std/generate_expected.sh` rewrites every passing file, so review the diff. |
| `test_lambda_errors_gtest` | `test/lambda/negative/{syntax,semantic,runtime,io,fuzzy_crashes}` | None. Each script has an explicit `TEST_F(NegativeScriptTest, …)`. | C++ assertions: non-zero exit plus a message substring (`ExpectErrorMessage`, `ExpectRuntimeErrorMessage`, `ExpectRejectedOnEveryTier`). No harness reads the `.txt` files in `negative/`. | Add the `.ls` plus a `TEST_F` |
| `test_mir_emission_gtest`, `test_js_mir_emission_gtest` | `test/mir/lambda/*.ls`, `test/mir/js/*.js` | Non-recursive; every script must have a sidecar | `<stem>.mir-check` JSON (`checks[]` with `in_func` + `expect`/`forbid`, optional `args`, `expect_exit_code`) | See `vibe/Lambda_Design_MIR_Emission_Test.md`. The same corpus feeds `test_mir_gc_stress_gtest` (stressed vs unstressed output, no goldens) and `test_mir_ratchet_gtest` (budgets in `test/mir/mir_budgets.json`, 0% slack). |
| `test_interp_gtest` | Scripts listed in `test/lambda/interp_p0_subset.txt` | List-driven | Sibling `.txt`, with zero T0 fallbacks | `make interp-sweep` regenerates `interp_p0_subset.txt` and `interp_excluded.txt` together. Never hand-edit one. |
| `test_js_gtest` | `test/js`, `test/js/props` | Non-recursive `foo.js` + `foo.txt`. `foo.html` or `// @document <file>` makes it a DOM test. Header directives `// @test-permission` and `// @test-module-path`. | `.txt` | Mixed mode is the default: names in `test/js/mir_list.txt` run on MIR, the rest on AST. Override with `--full-mir`, `--full-ast`, or `JS_GTEST_MODE=ast` / `JS_GTEST_MODE=mir`. `--baseline` drops the `lib_codemirror`/`lib_tabulator`/`lib_tom_select` probes. |
| `test_ts_gtest` | `test/ts` | `*.ts` + `.txt` | `.txt` | Add the pair |
| `test_js_test262_gtest` | `test/js262` (stripped copies) + `ref/test262/harness` | `test/js262/test262_baseline.txt`, `skip_list.txt`, `t262_slow.txt`, `mir_list.txt` | Test262 metadata | `make test262-update-baseline` (rule 18) |
| `test_node_gtest` | `ref/node/test/parallel` | `test/node/official_{baseline,skip_list,serial_list,slow_list}.txt` | Exit 0 and no `Uncaught` | `make node-update-baseline` |
| `test_wpt_*_gtest` (sources in `test/wpt/`) | `ref/wpt/<area>`; `test_wpt_html_parser_gtest` reads `test/html/wpt/` | Per-runner | Most runners use a `test/wpt/wpt_<area>_baseline.txt` passing list | `WPT_<AREA>_UPDATE_BASELINE=1` (env name defined at the top of each runner) |
| `test_validator_gtest` | `test/lambda/validator` (`schema_*.ls` + `.json/.xml/.yaml/.md/.html/...` data) | None. Explicit `TEST_F(ValidatorTest, …)` per (data, schema) pair. | Assertions on `ValidationResult` | Add files plus a `TEST_F` |
| `test_ui_automation_gtest` | `test/ui/**`, `test/view/*.json`. Suites `baseline`, `dom`, `editor`, `hit-test`, `edit`, `view`, `native-gui` are defined by globs in `test/ui/ui_test_manifest.json`. | Manifest globs. The id is the path under `test/ui/` or `test/view/` with `/ - .` → `_`. | JSON `{name, html, events:[click, assert_text, wait, …]}` | Add the `.json` (+ `.html`). Check that the baseline suite's `exclude` list doesn't drop it. |
| `test_page_load_gtest`, `test_layout_fuzzy_gtest` | `test/layout/data/{page,markdown}`, `test/layout/data/fuzzy` | Every `*.html` (non-recursive) | Loads without crash | Drop in an `.html` file |
| `test/layout/test_radiant_layout.js` (`make layout`) | `test/layout/data/<suite>/` | Per-suite `baseline.txt` lists the files that must pass | Browser reference JSON in `test/layout/reference/` (flat; nested fixtures are keyed `dir__name`) | `make capture-layout test=<name>` |
| `test/render/test_radiant_render.js` (`make test-render`) | `test/render` | Script-defined | Reference PNGs | `make capture-render test=<name>` |
| `test/fo_table_reference.py` | `test/html/paged_media_{tables,display_alignment,alignment,graphics,graphics_namespaces,lists,lists_context,cell_flow,indents,indents_auto,corresponding,conditional,conditional_components,conditional_visible,proportions,proportions_columns,numbered_columns,table_furniture,expressions,parent_values,property_bindings,nearest_values,percentage_indents,percentage_indents_fractional,proportions_mixed,proportions_affine,decoration_bindings,direct_cells,dimensional_arithmetic,space_bindings,rgb_expressions,rgb_percentages,policy_bindings,decoration_expressions,typography_bindings}.fo` | Explicit three-page table, alignment, graphics, list, sibling-block cell, indent, corresponding-decoration and conditional-decoration profiles | Apache FOP physical page geometry and exact colored-region bounds after Poppler rasterization; glyph raster equality is not asserted; the PDF-font-configured FOP area tree is retained | Requires Java, binary FOP, Pillow, Poppler and a FOP font configuration using the same Arial files as Radiant. Run with `--lambda-exe ./lambda.exe --java <java> --fop-home <fop/build-parent> --fop-config <config> --fixture tables` (or `display_alignment` / `graphics` / `graphics_namespaces` / `lists` / `cell_flow` / `indents`); artifacts default to `temp/fo_table_reference/`. `graphics_namespaces` adds qualified external and instream SVG with ancestor namespace declarations, stylesheets and referenced shapes, requiring the same exact graphic masks. The strict `alignment` comparison records FOP 2.11's unsupported `relative-align` differences. `lists_context` retains its provisional-property function binding difference; neither profile asserts a reference pass. `indents_auto` retains the user-agent-dependent region auto-overflow difference; `indents` selects visible overflow explicitly and verifies negative outdents. `corresponding` covers relative/physical precedence and multicolor miters; its strict report retains a one-pixel corner difference between FOP clipped strokes and filled polygons. `conditional` retains sparse compound components and user-agent medium-width differences; `conditional_components` supplies all lengths but retains auto-overflow differences. `conditional_visible` supplies every length and visible overflow and requires matching border/background bounds on all three pages. `proportions_columns` verifies fixed and weighted tracks with each column visible on all three pages; `proportions` retains the strict repeated-color visibility failure where a spanning cell covers a column background identically in both hosts. `numbered_columns` checks decreasing column/cell numbers and implicit endpoints across spanning/continuing rows. `table_furniture` requires first-page-only headers and terminal-page-only footers, including their absence on every other page. `expressions` checks FO arithmetic in sheet/style dimensions, proportional weights and rounded spans against the same visible three-page table geometry. `parent_values` checks whole-property computed parent/inherited queries, root FO initials and nested inherited indents across three pages. `property_bindings` exercises cross-property color and arithmetic font/indent queries through native CSS-variable bindings. `nearest_values` also selects a distant authored font owner and a shorthand padding owner through common ancestor queries. `percentage_indents` checks inherited and mixed percentage indents in nested cell continuations; `percentage_indents_fractional` retains the strict fractional-edge clipping/fill difference, with the same vector geometry. `proportions_affine` checks mixed fixed/proportional expressions with every column visible. The equivalent scaled `proportions_mixed` profile retains a strict failure: FOP 2.11 does not scale its accumulated proportional factor for a scalar product, so its table exceeds the declared width. Both profiles require exact FO/native output parity. `decoration_bindings` checks logical border-color and compound-length queries, computed font style/alignment and paragraph limits through common bindings; its strict FOP report retains the one-pixel clipped-stroke/filled-polygon corner difference. `direct_cells` checks grouped direct cells, row/column spans, repeated furniture and a continued cell against equivalent native transparent rows. `dimensional_arithmetic` checks intermediate unit powers and cancellation in sheet dimensions, columns, computed parent-font queries and continued block indents against literal native output. `space_bindings` checks computed space-range components and nearest compound assignments across continued table content; its strict FOP 2.11 report retains unknown-component-property diagnostics and resulting geometry differences. `rgb_expressions` checks channel arithmetic, rounding and computed numeric ancestor bindings through common CSS colors; exact FO/native parity is required, while its strict FOP report retains rejected numeric-object string conversions and fallback colors. `rgb_percentages` covers function-specific percentage conversion, nested numeric functions and computed bindings; it retains FOP's arithmetic color-string/context failures, with a separate literal-percentage three-page reference recorded in the implementation ledger. `policy_bindings` checks numeric keep and keyword space-policy references against independently authored native values, including observable colored blocks around the chosen page boundary. Its strict FOP report retains dotted-component lookup failures and the resulting different break. `decoration_expressions` adds arithmetic in padding and border shorthands with physical-longhand precedence; its strict FOP report retains arithmetic RGB conversion failures. |

## 5. External test data

Run `./setup-test.sh` once per clone (`LAMBDA_TEST_DIR=<dir>` overrides `../lambda-test`). It clones `ref/wpt` at a pinned commit and links each top-level dir of `../lambda-test` as `test/<name>`.

| Path | Source | In git? | Used by |
|---|---|---|---|
| `test/js262` | `../lambda-test/js262` | ignored link | `test_js_test262_gtest` |
| `test/layout` | `../lambda-test/layout` | ignored link | `make layout`, page-load, fuzzy, radiant-view, UI fonts (`test/layout/data/font`) |
| `test/render`, `test/pdf` | `../lambda-test/{render,pdf}` | ignored links | `make test-render`, `test_pdf_render_visual_gtest` |
| `test/markdown`, `test/media`, `test/jquery-ui` | `../lambda-test/*` | **tracked** links | source-pos tests, `test_video_gtest`, UI `dom` fixtures |
| `../lambda-test/editing` | read directly | not linked (`test/editing` is a tracked dir) | `test_chromium_contenteditable_gtest` |
| `ref/wpt` | WPT clone at the commit pinned in `setup-test.sh` | ignored | WPT runners |
| `ref/test262`, `ref/node` | manual checkout (`setup-test.sh` does not fetch them) | ignored | Test262, Node official tests (`make node-shim`) |
| `test/yaml` | git submodule | submodule | `test_yaml_suite_gtest` (`ensure-yaml-submodule` inits it) |

## 6. Updating goldens and baselines

Update one only when the behavior change is intended, and cite the `S#`/`D#` ruling in the commit (AGENTS.md rule 17).

| Artifact | How |
|---|---|
| Lambda `.txt` / std `.expected` | Write it from `./lambda.exe [run] <file>` output (the text after `##### Script`). Add a `.mac.txt` (or `.linux.txt`/`.win.txt`) only for real platform differences. |
| `.mir-check` sidecars | Edit by hand. Assert names and instruction shapes, never raw immediates. |
| `test/mir/mir_budgets.json` | Raise a threshold by hand in the same commit as the reviewed growth. The ratchet never edits it. |
| Interp subset lists | `make interp-sweep` |
| Layout suite `baseline.txt` | `make layout suite=<dir> update=1` |
| Layout page snapshot | `make layout-snapshot suite=page` (check with `layout-snapshot-check`) |
| Render baseline | `make test-render suite=<suite> update=1` |
| Test262 / Node official | `make test262-update-baseline` / `make node-update-baseline` |
| WPT runners | `WPT_<AREA>_UPDATE_BASELINE=1 ./test/test_wpt_<area>_gtest.exe` |

Debugging: `./log.txt` holds the runtime trace, debug builds dump JIT'd MIR to `temp/mir_dump.txt`, and `node test/test_run.js --raw` shows unformatted output.

## 7. Common single-test commands

```bash
# Lambda script golden (full name: AutoDiscovered/LambdaScriptTest.ExecuteAndCompare/<name>)
./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*proc_array_view_admission'
LAMBDA_EXEC_BACKEND=jit ./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*'   # pin a tier
./lambda.exe run test/lambda/proc/array_view_admission.ls      # reproduce by hand; diff against the .txt

./test/test_lambda_extended_gtest.exe --gtest_filter='AutoDiscovered/*ext_*'
./test/test_lambda_std_gtest.exe --gtest_filter='Std/*core_datatypes_integer_basic'
./test/test_lambda_errors_gtest.exe --gtest_filter='NegativeScriptTest.*Counted*'
./test/test_mir_emission_gtest.exe --gtest_filter='Fixtures/*action_c_async_boundary'
./test/test_interp_gtest.exe --gtest_filter='P0Subset/*'
./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/*advanced_features'
./test/test_ts_gtest.exe --gtest_filter='TypeScript/*arrays'
./test/test_js_test262_gtest.exe --prelim                      # bounded preflight
./test/test_ui_automation_gtest.exe --suite baseline --test test_click_elements
make layout test=baseline_301_simple_margin                    # one layout file vs browser reference
make layout suite=wpt-css-box                                  # one layout suite
node test/test_run.js --target=lambda --category=baseline --exclude-test=test_js_gtest
```

Gotchas:
- In `test_lambda_gtest`, the gtest filter also chooses which scripts the batch runs, so a narrow filter is fast. `test_lambda_std_gtest` always batches all of `test/std`.
- The display-list, state-store, DOM-range and media-player tests link against `test/*_stubs.cpp`. A new `radiant/` symbol that code calls needs a stub there, and only `make build-test` catches a missing one.
- The `LAMBDA_BASELINE_TEST_PROJECTS`, `RADIANT_BASELINE_TEST_PROJECTS` and `INPUT_BASELINE_TEST_PROJECTS` lists at the top of the Makefile must match what the gates run. Update them when you add a baseline binary.

WebAssembly build and tests: `make build-wasm`, `make test-wasm`, and [`doc/dev/Lambda_WASM_Build.md`](../doc/dev/Lambda_WASM_Build.md).

### Lambda UI dtna

`make test-ui-dtna ARGS='--jobs 1'` checks the 73-entry feature manifest,
package goldens on T0/automatic/MIR tiers (**D8.1.1v17**), state-store regressions,
and the manifest-owned `dtna` native UI suite on forced interpreter/JIT paths.
The shared Radiant baseline also
includes this suite. The galleries are `test/ui/dtna_gallery.ls` and `test/ui/dtna_data_gallery.ls`; the JSON-formatted
coverage inventory uses `.manifest` because every `.json` under `test/ui/` must
be an executable fixture with exactly one owner.

Pure/API goldens: `test/lambda/ui_dtna/`. Native click/type/keyboard/style checks:
`test/ui/dtna/`. Coverage and remaining scope:
`test/ui/dtna_reference/catalog.manifest`. The gallery capture is a native
inspection artifact, not an AntD visual-equivalence baseline.

The URL clone ownership control (`./test/test_url_gtest.exe --gtest_filter='UrlCloneOwnershipTest.*'`) runs with allocation tracking and checks clone/free balance, retained components after source release and absent-component preservation. It covers the resource-base clones used by FO/native graphic exports under D4.5.1v4/D4.1.4v5 and D7.1.2v2.
