# Lambda Test Map

Where every test lives, which gate covers which change, and how to run or add one test.
Written for coding agents; build and Make details live in [`doc/dev/Make_Guide.md`](../doc/dev/Make_Guide.md).

Last verified against tree: 2026-10-11; Radiant baseline runner: 2026-10-13

## Ground rules

- Run everything from the repo root. Harnesses use repo-relative paths (`./lambda.exe`, `test/...`).
- `make build-test` builds every `test/*.exe` listed in `build_lambda_config.json` → `test.test_suites`.
  To add a gtest binary, add an entry there (`binary`, `source`, `libraries`, optional `category`/`parallel`) and rebuild; never edit the generated premake `.lua` files.
- `make test-library`, `test-input`, `test-validator`, `test-lambda` and `test-std` depend only on `build`. Run `make build-test` first.
- `test`, `test-all`, `test-all-baseline` and `test-lambda-baseline` switch `lambda.exe` to the debug build (`make debug`).
- Per-run results go to `test_output/<runner_name>_results.json`, logs to `test_output/<runner_name>.log`, and the combined report to `test_output/test_summary.json`. Runner aliases have separate artifacts even when they share a binary.
- Script checks are registered in `build_lambda_config.json` → `test.script_tests` with `suite`, `category`, `script`, and optional `args`/`parallel`. They receive `--report <path>` and write `{summary: {passed, failed, total}, results: [...]}`. Use `runner_only`, `runner_name`, and `runner_args` in native entries to reuse a binary without generating another build project.
- Never update a golden to make a failure go away (AGENTS.md rule 1). Never weaken `test_js_test262_gtest` (rule 18).
- Every `.ls` in a golden-driven directory is a test and needs its golden (rule 8); a script without one fails the run. Name helpers, imported modules and manual playground scripts `_*` or `mod_*` instead.
- A test blocked by an open bug or a missing ruling is **parked**: renamed `_<name>.ls` with a `// PARKED <date> — <ledger ID or S#>` header, and its spec-correct golden kept as `_<name>.expected.pending`. Un-park it in the change that fixes the issue. Parked tests are listed in `vibe/Lambda_Issue_Ledger.md` ("Std test triage — 2026-10-07").
- Worktree, flaky-baseline and debug-vs-release gotchas: [`doc/dev/Developer_Guide.md` §7](../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas).

## 1. Which gate do I run?

**Bold** gates must pass 100% (AGENTS.md). `make test-all-baseline` is the widest baseline gate. Run it before a merge that touches more than one area.

| Change area | Must-pass gate | Quick targeted check |
|---|---|---|
| Lambda core: `lambda/runtime/`, `lambda/core/`, parser, MIR JIT | **`make test-lambda-baseline`** | `./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*<name>*'`; `./test/test_mir_emission_gtest.exe --gtest_filter='Fixtures/*<name>*'`; `./test/test_lambda_runtime_gtest.exe --gtest_filter='NegativeScriptTest.*'` |
| Lambda packages: `lmd/package/**/*.ls` | **`make test-lambda-baseline`** | `./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*scene3d_*'` (also `physics_`, `chart_`, `latex_`, `math_`, `mermaid_`, `graphviz_`, `structurizr_`, `slide_`, `editor_`, `edit_`); `make test-math-corpus` |
| EPUB reader: `lmd/package/doc/ebook*` | **`make test-lambda-baseline`** + **`make test-radiant-baseline`** | `./test/test_lambda_gtest.exe --gtest_filter='*ebook*'`; `./test/test_ui_automation_gtest.exe --suite baseline --test 'ebook_*,doc_viewer_epub_preview,iframe_navigation_delegate,browser_focus_caret,browser_inline_focus'`; `python3 test/epub_reader_check.py --report temp/epub-state.json` |
| Diff review: `lmd/package/doc/diff*`, byte grammar and process capture | **`make test-lambda-baseline`** + **`make test-radiant-baseline`** | `./test/test_lambda_gtest.exe --gtest_filter='*grammar_*:*diff_*'`; `./test/test_ui_automation_gtest.exe --test 'diff_*'`; `./test/test_lambda_concurrency_gtest.exe` |
| Native geographic maps: `radiant/geomap*`, `lmd/package/map*` | **`make test-lambda-baseline`** + **`make test-radiant-baseline`** | `make test-map` (frames, indexed picking, native pointer/control input), `make test-map-export`, `make test-map-reference` (first `npm ci --prefix test/map`); fixtures and pinned expression corpus in `test/map/`, functional goldens in `test/lambda/map/` |
| Input parsers / formatters: `lambda/input/`, `lambda/format/`, `lambda/io/` | `make test-input-baseline` (5 corpora, also run by test-lambda-baseline) + `make test-input` (70-binary input suite) | `./test/test_input_model_gtest.exe`; `./test/test_markdown_gtest.exe --baseline`; `./test/test_input_roundtrip_gtest.exe --gtest_filter='JsonTests.*'`; `./test/test_html_gtest.exe` |
| Script source writers: `lmd/package/doc/misc*` | `make test-input` + `make test-lambda-baseline` | `python3 test/script_format_roundtrip.py --report temp/script-format-roundtrip.json` (`--format` / `--route` select cases); `./test/test_lambda_gtest.exe --gtest_filter='*doc_misc_roundtrip*'`. EML/VCF/ICS pass; prose readback currently fails. Details: [`Script_Formats_Support.md`](../doc/Script_Formats_Support.md#verification) |
| CSS engine: `lambda/input/css/` | **`make test-radiant-baseline`** + `make test-input` (`test_css_*` binaries) | `./test/test_css_parser_gtest.exe`; `make layout test=<file>` |
| Radiant layout / render / events: `radiant/` | **`make test-radiant-baseline`** (the `radiant` suite's baseline native and script checks) | `make layout test=baseline_301_simple_margin`; `make layout suite=<dir>`; `./test/test_ui_automation_gtest.exe --suite baseline --test <id>`; `make test-render test=<name>` |
| LambdaJS: `lambda/js/` | **`make test-lambda-baseline`** + `make test262-baseline` | `./test/test_js_gtest.exe --gtest_filter='JavaScriptTests/*<name>*'`; `make test-js262-prelim`; `make test-js-parity` |
| DOM: `lambda/dom/` | **`make test-radiant-baseline`** (DOM UI fixtures, WPT input-events) + **`make test-lambda-baseline`** (JS DOM fixtures) | `make dom-ui test=<id>`; `./test/test_lambda_domnode_gtest.exe`; `./test/test_dom_range_gtest.exe`; `./test/test_wpt_dom_nodes_gtest.exe` (extended) |
| Validator: `lambda/validator/` | `make test-validator` (validator suite, part of test-all-baseline) | `./test/test_validator_gtest.exe --gtest_filter='ValidatorTest.*'`; `./test/test_validator_input_gtest.exe` |
| `lib/` utilities | `make test-library` (library suite, part of test-all-baseline) | `./test/test_str_gtest.exe`, `./test/test_arena_gtest.exe`, … (source `test/lib/test_<name>_gtest.cpp`) |
| HTTP server: `lambda/serve/` | `make test-lambda-baseline` | `./test/test_serve_gtest.exe --gtest_filter='SchemaValidationTest.*:ValidationResultJson.*'`; the consolidated runner also covers routing, middleware, requests/responses, cookies, MIME, static helpers and the backend registry |
| Mail and OAuth: `lambda/runtime/io_mail.cpp`, `io_secret.cpp`, `io_loopback.cpp`, `sys_crypto.cpp`, `lmd/package/oauth.ls`, `lib/mail_imap.*`, `lib/mail_mime.*`, `lib/secret_store.*`, the `eml` reader, `lmd/package/mail/`, `test/demo/mail/` | `make test-mail` (scripted loopback IMAP/POP3/SMTP servers, the Keychain round trip and the demo window's replay; needs socket permission, and on macOS writes test-only Keychain items it removes again) + `./test/test_mail_imap_gtest.exe` + `./test/test_mail_mime_gtest.exe` + the `test/lambda/mail` and `test/demo/mail/tests` goldens (in `test-lambda-baseline`) | `python3 test/mail/test_mail.py DemoWindowTests`; servers in [mail/fake_mail.py](mail/fake_mail.py), design in `vibe/Lambda_Demo_Mail.md` |
| MCP client/server: `lambda/io/mcp*`, `lambda/serve/mcp_server*`, `lambda/runtime/mcp_module.cpp` | `make test-mcp` (real stdio and loopback HTTP; needs socket permission) + `make test-lambda-baseline` for runtime changes | `python3 test/mcp/test_mcp.py ClientTests`; transport contract and current scope: [mcp/README.md](mcp/README.md) |
| Jube / Node modules: `lambda/jube/`, `lambda/module/node_*` | **`make test-lambda-baseline`** (builds node-core/node-fs) + `make node-regression-gate` | `make node-baseline`; `make test-jube-module-integrity`; `make check-host-exports` |
| Release-only checks (perf, JIT timing) | n/a | `make release` first (rule 10). Never run it in a worktree. |

## 2. Suites (`build_lambda_config.json` → `test.test_suites`)

`node test/test_run.js` runs these. Flags: `--target=<suite>`, `--exclude-target=<suite>`, `--category=baseline|extended`,
`--exclude-test=<name>[,<name>]`, `--raw`, `--sequential`, `--list` (JSON inventory), `--list-build-targets` (unique native projects). Env: `LAMBDA_TEST_BIN_DIR`, `LAMBDA_TEST_OUTPUT_DIR`, `LAMBDA_TEST_IDLE_TIMEOUT` (s), `LAMBDA_TEST_MAX_CONCURRENT`, `LAMBDA_TEST_HEAVY_LOAD=1`, `LAMBDA_UI_TEST_JOBS`.
Every Radiant baseline runner receives `LAMBDA_LOG_LEVEL=NOTICE`, inherited by its native and script subprocesses, including case workers. The View fixtures also use NOTICE for their explicit logging settings. Warnings, errors and notice-level profiles remain enabled without parser/layout debug traces.
The shared runner executes Radiant View tests in three isolated GTest case workers by default, configured by `case_workers`, `case_workers_env` and `case_worker_args` in the build config. It discovers cases using the configured GTest filter, starts previously slow cases first when the prior report is available, then merges every case into the usual JSON report. Split variants inherit an estimate from their retired bundled case until their own measured timings are available. Each worker caps nested viewer launches at one, so the slow document checks can overlap other cases within the same process ceiling. The twelve HTTP language variants and photo replays (automatic tier and JIT) are separate cases so a multi-launch case cannot hold one worker for a minute. The complete language matrix runs normally; `HttpDocumentLanguageSurvivesForcedGc_{redirect_single,root}` separately stress protocol-language and explicit-root handoffs with every-allocation, poisoning GC (D5.3.3). `PhotoCanvasCopiesRgbaAndReleasesItAcrossGc` checks retained pixels, clean shutdown and all three repaint-only frame profiles in one replay (D4.5.2). The math-intensive View case checks static loading and shutdown; the View UI fixture owns the full scrolling replay and checks its shutdown allocations and quiet output, avoiding a duplicate replay. GPU demos remain serial within their catalog case. `--sequential` keeps suites sequential while allowing these case workers. `LAMBDA_RADIANT_VIEW_TEST_JOBS=1` provides serial case execution; `LAMBDA_TEST_MAX_CONCURRENT` caps the worker count. Direct binary invocations use the same View setting for subprocess groups.

Independent Radiant suites run concurrently within one worker budget, defaulting to 125% of logical CPUs for their mix of compilation, rendering and event waits. `LAMBDA_TEST_MAX_CONCURRENT` lowers this ceiling, and heavy-load mode still defaults to at most four. Scheduling starts previously slow suites first using their measured wall time, which includes subprocess work before GTest assertions. `worker_slots` reserves capacity for native or script subprocess pools; `worker_slots_env` selects an override and `worker_args` passes the granted count to native runners with a jobs option. GTest case pools reserve their `case_workers` count. UI baseline workers reserve seven slots, the small View UI alias one, DOM UI two, and page-load/fuzzy runners two each. One math fixture dominates the View UI alias, so a second reserved worker mostly waits. Child pools inherit their allocation through `LAMBDA_TEST_MAX_CONCURRENT`. Layout and page snapshots have separate scratch directories under `temp/` and overlap other suites; WPT input events use their own fixture namespace with three workers. Render and native map checks remain exclusive. Scene3D integration cases share the View case workers and `LAMBDA_RADIANT_VIEW_TEST_JOBS` setting; the separate native unit target uses two case workers. The full baseline reserves three layout workers with 100-file batches across all categories and two snapshot workers; `LAYOUT_BASELINE_CONCURRENCY` overrides the layout allocation within the shared ceiling. The standalone layout baseline shares the same category pool with a four-worker default. The full baseline leaves capacity for page-load and DOM workers to overlap that pool. Category wall times include waiting for the shared layout pool. Reports retain both elapsed `duration_ms` and accumulated `work_duration_ms` at suite level.

Case logs and individual GTest reports are under `test_output/test_radiant_view_gtest_cases/`. Working directories are under `temp/`; queued viewer logs, event results and phase timings are under `test_output/radiant_view/<run>/<launch>/` (output paths honor `LAMBDA_TEST_OUTPUT_DIR`). The runner replaces the View suite's `radiant_view/` artifacts before starting its next run, as declared by `artifact_dirs` in the build config, so debug logs do not accumulate across baseline runs. Copy diagnostics elsewhere before rerunning if you need their history. The combined JSON report includes `launch_timings`; document checks report subprocess duration instead of their assertion-only time. Discovery errors, missing cases, crashes, timeouts and log write errors remain failures in the merged report.

A test that stops producing output is killed after the idle timeout; a log write error also terminates its process group. The runner pauses both output pipes while the log sink drains, keeping buffered log data bounded. With `--category=baseline` the runner passes `--baseline` to `test_js_gtest`, `test_wpt_html_parser_gtest` and `test_markdown_gtest`.

UI automation uses a memory-weighted queue. Manifest suites and individual fixtures can set `worker_mb`; baseline/DTNA/timeline reserve 1024 MiB, and the indexed-math editor fixture reserves 1536 MiB. Other suites retain the 2048 MiB default for large editor/JS bundles. These budgets follow a full release profile: the indexed-math case peaked at 1179 MiB, all other cases below 900 MiB. Reservations collectively use at most half of physical RAM, with at least one case admitted on smaller hosts. `LAMBDA_UI_TEST_JOBS` and `LAMBDA_TEST_MAX_CONCURRENT` cap workers; `LAMBDA_UI_TEST_WORKER_MB` overrides reservations. Subprocess durations and per-view phase timings are retained in the shared report. A separate `<gtest-json-path>.timings.json` file supplies scheduling hints for later runs; every fixture still executes and produces a fresh result. `RADIANT_TEST_EXECUTABLE` selects an alternate host executable for isolated release comparisons.

Headless assertion retries give pending native document and media work wall time to complete, including AVFoundation loading and playback transitions.

**XHTML (D2.10.1):** `test/html/xhtml_feature_gallery.xhtml` is a viewable
gallery with 27 live DOM/style checks and local CSS/JS/SVG assets.
`RadiantViewTest.LoadsXhtmlFeatureGallery` replays nine assertions, including
clicks, a text-field edit and a checkbox toggle. Run
`./test/test_radiant_view_gtest.exe --gtest_filter='RadiantViewTest.*Xhtml*'`
for gallery, HTML parity, HTTP MIME/redirect and Lambda-returned document
coverage. `HtmlParserTest.Xhtml*` checks parser edge cases;
`test/lambda/input_xhtml.ls` and its golden cover `input`/`parse`/HTML output
across execution tiers. Static rendering currently reports one startup
computed-border discrepancy in the gallery; interactive view checks pass.

For performance measurements, select a release host with `make build-release-compile`, then run `make test-radiant-baseline RADIANT_TEST_CONFIG=release_native`. The normal native runner build remains `debug_native`. `make run-radiant-baseline ARGS=--sequential` provides a serialized comparison without rebuilding.

| `--target` | Category | Binaries | Notable members |
|---|---|---|---|
| `library` | baseline, +1 script | 52 | `test/lib/test_*` (str, arena, gc_heap, mempool, url, font…), `test_rdb_gtest`, `test_avl_tree_perf`; runner contracts: `node test/test_test_run.js` |
| `radiant` | 24 baseline runs, 13 extended, +3 baseline scripts | 35 | baseline: native scene/map, text-query font cache (`test_radiant_text_query_gtest`), display lists, vector backend, paged media (`test_paged_media_gtest`), CSS cascade memory, animation/media, UI (`baseline,dtna,timeline`, `view`, `dom`), page/view/fuzzy, WPT CSS Syntax and input-events. Scripts: layout baselines, page snapshot, render visual. Extended: remaining WPT/editing runners |
| `input` | baseline, +1 script | 70 | mark builder/reader/editor, HTML, markdown, YAML, `test/css/*`, graph parsers, `test_input_model_gtest` (OBJ/MTL/glTF/A3D; fixtures in `test/input/model/`), `test_validator_input_gtest`, `test_lambda_domnode_gtest`, `test_dom_range_gtest`. Script: `test/run_script_format_roundtrip.js` (six `lambda.doc.misc` writers; prose reader gaps currently fail) |
| `validator` | baseline | 3 | `test_validator_gtest`, `test_ast_validator_gtest`, `test_validator_path_reporting` |
| `lambda` | 32 baseline, 2 extended, +2 scripts | 34 | `test_serve_gtest`, `test_zip_gtest` (ZIP32/ZIP64, Word/JAR packages, 7z/tar/RAR/RAR5, quotas and ZIP writing), `test_lambda_gtest`, `_std`, `_errors`, `_proc`, `_repl`, `_concurrency`, MIR emission/ratchet/gc-stress, `test_js_gtest`, `test_ts_gtest`, `test_js_test262_gtest --prelim` (runs as `test_js_test262_prelim_gtest`). Scripts: `test/lambda/math/run_corpus.mjs` (baseline), `test/run_js_parity.mjs` (extended) |
| `extended` | extended | 10 | `test_interp_gtest`, `test_lambda_extended_gtest`, math/HTTP/network/online-view tests; Radiant core unit tests (`test_radiant_core_gtest`); runner aliases retain Doom and the UI suites the `radiant` baseline does not run (`bold,editor,edit,hit-test`) |
| `jube` | 3 baseline, 2 extended | 5 | `test_rb_gtest`, `test_bash_official_gtest`, `test_bash_run_gtest` (no build compiles these front ends, see `doc/dev/Lambda_Jube_Runtime.md`), `test_js_test262_gtest` (full), `test_node_gtest` |

## 3. Aggregate make targets

Window fullscreen controller/key tests and the windowless Lambda API probe run
in `test_radiant_text_query_gtest.exe`:

```sh
./test/test_radiant_text_query_gtest.exe --gtest_filter='WindowFullscreen*.*:RadiantFrameDelivery.Fullscreen*'
```

Physical-window replays require a desktop session and deliberately live outside
the headless UI fixture manifest:

```sh
./lambda.exe view test/native/fullscreen/window.ls --event-file test/native/fullscreen/window.json
./lambda.exe view test/native/fullscreen/window.ls --fullscreen --event-file test/native/fullscreen/startup.json
```

They cover DOM requests, idempotence, F11, Escape, startup, and native change
notifications. Multi-monitor/DPI, display removal, and platform focus behavior
still require native checks. Design: [Radiant Fullscreen](../vibe/radiant/Radiant_Design_Fullscreen.md).

| Target | Runs |
|---|---|
| `make test` | every suite except `jube`, and minus `test_radiant_online_view_gtest`. This is not `test-all`, despite what `make help` says |
| `make test-all` / `test-all-baseline` / `test-extended` | all suites / `--category=baseline` / `--category=extended`. `test-extended` also runs `dom-ui-run`. |
| `make test-lambda-baseline` | `test-input-baseline`, then `lambda` suite baseline (excludes `test_node_prelim_gtest`, `test_lambda_concurrency_gtest`), one merged report |
| `make test-lambda-full` | the above plus concurrency, `test_lambda_extended_gtest`, `test_lambda_domnode_gtest`, `test_validator_input_gtest` |
| `make test-input-baseline` | `test_wpt_html_parser_gtest --baseline`, `test_markdown_gtest --baseline`, `test_yaml_suite_gtest`, `test_math_ascii_gtest`, `test_math_gtest` |
| `make test-radiant-baseline` | builds the native projects selected by `--target=radiant --category=baseline`, then runs independent checks concurrently through `test/test_run.js` within a shared worker budget. All native checks, layout suites, page snapshot, UI/DOM fixtures and render visual checks appear in the shared console/JSON report; layout partials, skips and baseline verdicts are retained |
| `make run-radiant-baseline` | the same Radiant checks with no rebuild |
| `make test-layout-baseline` | layout baseline suites only |

Focused targets agents commonly need (`make help` lists ~113; the Makefile has ~240):

- **Archive package:** `test/lambda/archive/` runs the archiver end to end over `file:` URLs into per-process folders under `temp/archive_test/`: pages, renderings (a child `lambda run` worker, PDF and thumbnails), search, the media and gallery engines over the fixture site `test/lambda/archive/site/` (regenerate its media with `python3 test/lambda/archive/site/make_media.py`), and the job service with cancellation. The demo's native replay is run by hand (`test/demo/archive/README.md`).
- **Readability conformance:** `./lambda.exe run test/lambda/readability/_corpus.ls` runs `lambda.doc.readability` over Mozilla's Readability.js test pages (default `ref/readability/source/test/test-pages`, git-ignored; `READABILITY_PAGES=<dir>`, `READABILITY_PAGE=<name>`) with the semantics of Mozilla's harness, and reports content, metadata and `readerable` per page. It must stay at 130/130; the golden `test/lambda/readability/pages.ls` covers synthetic pages whose expectations Readability.js produced. See [Lambda_Pkg_Readability.md §7](../vibe/Lambda_Pkg_Readability.md#7-verification).
- **Math source formatting:** `node --test test/lambda/math/source_format.test.mjs` checks LaTeX/AsciiMath compatibility goldens, package/runtime routes on all execution tiers, forced GC, CLI conversion and procedural output. Set `LAMBDA_MATH_FORMAT_EXE=./lambda-cli.exe` to check the runtime-only host. MathML conversion/layout integration has its separate `test/lambda/math/mathml.test.mjs` runner.

- **PDF export across input formats:** `python3 test/pdf_export_corpus.py --manifest test/pdf_export_formats.json` checks representative viewer routes. Use repeated `--root` arguments for corpus sweeps, `--extensions` to select suffixes, and `--resume` to retry failed or unfinished cases. The runner snapshots its executable/package tree, checks PDF offsets/stream lengths/page trees, and rasterizes every page with Poppler. Reports and failure diagnostics go under `temp/pdf_export_corpus/`; successful PDFs are discarded unless `--keep-pdfs` is set. `test/pdf_export_expectations.json` pins documented malformed-input errors and the seven-page RPD oracle. Structural/raster success does not assert browser pixel parity; `test_pdf_render_visual_gtest` provides focused visual checks.

- **PDF output roundtrip:** `GTEST_FILTER='PdfOutputRoundtrip.*' node test/test_run.js --include-test=test_pdf_render_visual_gtest` loads source PDFs recursively under `test/`, including linked `test/pdf` and LaTeX references, and regenerates every page through paged intake and the Lambda writer (S2.6.6v3). This bypasses the unpaged byte-copy route. Python 3 and Poppler (`pdfinfo`, `pdftoppm`) are required. Checks cover page count, every page's boxes/rotation, cross-reference offsets, stream lengths/decompression, and independent rasterization at 128 pixels. The malformed PDF fixture must leave a fresh destination absent and preserve an existing destination. Generated `output`/`results` directories are excluded; artifacts use each worker's `temp/pdf_roundtrip/`. Valid unsupported documents fail instead of being skipped. This includes the 183-page `b-format.pdf` and 408-page `essay.pdf`, so a full roundtrip sweep costs more than the four-page import visual sample. The five original failures now have parser/font/image fixes: linearized trailers, legacy embedding flags, packed/16-bit colour samples and predictors, Indexed/DeviceN/Lab colours, JPEG2000, CCITT and generic-region JBIG2. Symbol-dictionary/text/refinement JBIG2 and general PostScript calculator procedures remain unsupported; new unsupported fixtures still fail the sweep.

- **PDF visual/export gate:** build the host first, then use `make test-pdf-render`. The PDF executable belongs to `input / baseline`; it is separate from the Radiant baseline. The shared runner uses eight case workers, capped by the shared CPU budget, each with its own scratch directory under `temp/`; saved case results include `workspace`. `LAMBDA_PDF_TEST_JOBS=1` serializes execution. Cases are balanced into GTest batches of at most 16, reusing modules across cases; `LAMBDA_PDF_TEST_BATCH_SIZE=1` restores a separate process per case. Every assigned case must appear exactly once in the report; crashes and incomplete reports fail the affected batch. On POSIX, each batch lazily starts a persistent `render-batch --export` process: ordinary PNG/SVG/PDF comparisons, including explicit viewport dimensions, and imported-page renders reuse the UI host between render jobs, and PDF exports retain their package cache. Script pages still own a runtime per document. Document state is retired after each job. Paged-option and CLI rejection checks keep their ordinary command route. `LAMBDA_PDF_BATCH=0` selects ordinary commands for a control run; Windows retains that route. FO/native fixtures, gradient kinds and discovered PDF-import fixtures are separately scheduled while preserving their full inner matrices. Each import fixture rasterizes its reference pages in one Poppler invocation. Case subprocesses default their compiler-worker limit to the inherited CPU budget; an explicit `LAMBDA_MODULE_AST_THREADS` setting is preserved. `GTEST_FILTER='PdfImportVisual.*' node test/test_run.js --include-test=test_pdf_render_visual_gtest` runs the import corpus with scheduling. Baseline readers and partial updates use a file lock so workers retain other fixtures; `--update-baseline` keeps the original whole-corpus regression check and single replacement. `GTEST_FILTER='RenderOutputParity.Pdf*' node test/test_run.js --include-test=test_pdf_render_visual_gtest` selects cases while keeping worker scheduling; `make test-pdf-render ARGS='--gtest_filter=RenderOutputParity.Pdf*'` retains direct GTest execution. Export pixel checks require `pdftocairo` and `rsvg-convert`. When their compatible Poppler GLib/Cairo and librsvg libraries are available, independent readers run in process; otherwise the CLI route remains. Generated self-contained PDF exports reuse independent rasterizations only after full source-byte and density equality, within 32 entries / 32 MiB; `LAMBDA_PDF_RASTER_CACHE=0` disables this reuse. SVG resources are read afresh. Diagnostic PNGs use fast lossless compression. Separate controls compare library and CLI pixels exactly and exercise file replacement, density, missing and invalid sources. PDF import corpus references use `pdfinfo`/`pdftoppm`, and searchable text checks require `pdftotext`. Performance measurements require `make build-release-compile` followed by `make test-pdf-render PDF_TEST_CONFIG=release_native`. `RADIANT_TEST_EXECUTABLE` selects a frozen host for worker runs.

- **Scene3D:** `make test-scene3d` filters the Scene3D cases in `test_radiant_view_gtest.exe`, then runs the separate native unit target. View launches `lambda.exe view --headless --event-file ... --event-result ...` for the scene/page integration cases and negative event-sim controls listed in the case inventories. `test_scene3d_unit_gtest.exe` preserves 19 native math, shader, allocator, ownership and GC unit cases. The integration cases and controls run and report within Radiant View; native units remain a separate Radiant baseline target. Integration scenarios live in `test/scene3d/events/`, with an explicit case/event inventory in `test/scene3d/cases.inc`; negative controls live in `test/scene3d/contracts/`. Each CLI run uses the shared View launcher: logs, phase timings and structured assertion results go under `test_output/radiant_view/`, and scratch artifacts use the isolated launch workspace under `temp/` (reported by `scene3d_artifacts`). The harness checks process exit, the complete event inventory and every assertion result. Native graphics access is required; missing graphics fails rendered tests. `RADIANT_TEST_EXECUTABLE` can select another host executable. Imported OBJ/MTL/glTF/A3D models and fixed-time playback are covered by `test_input_model_gtest` and `Scene3dTest.ImportedAssetGalleryRendersTexturedObjAndPlaysGltfAndA3dClips`; the viewable fixture is `test/scene3d/imported-assets.ls` with resources under `test/demo/scene3d/assets/loading/`. The demo collection index and breadcrumb round trips have desktop and mobile replays in `Scene3dTest.Scene3dCollection*`. Package goldens live in `test/lambda/scene3d/`; page, sizing and texture fixtures live in `test/scene3d/`. Phases II/III add [selected WebGL2/Three.js and shared animation fixtures](webgl/README.md) in `test/webgl/` and the pinned gallery under `test/demo/scene3d/`.
- **Lambda DOOM:** `./test/test_lambda_gtest.exe --gtest_filter='DoomDemoTests.*'` compares the demo's rules/resources once on the selected default backend (AUTO unless overridden; D8.1.1v18). `./test/test_radiant_view_gtest.exe --gtest_filter='RadiantViewTest.Doom*'` runs the effects/input/audio probes and six native gameplay/episode/lifecycle replays with forced GC, freed-memory poisoning and tracked close checks. These Radiant View tests run through the `test_radiant_view_doom_gtest` alias in `make test-extended` (`--target=extended` in the Node runner), outside `make test-radiant-baseline`; shared CSS 3D regressions remain in the baseline's Radiant View target. Canonical fixtures and goldens live in [`test/demo/doom/`](demo/doom/README.md).
- **Layout and render:** `layout` / `test-layout` (`test=`, `suite=`, `pattern=`, `update=1`), `layout-snapshot-check suite=page`, `capture-layout test=`, `test-render` (`test=`, `suite=`, `pattern=`, `update=1`), `capture-render`.
- **Paged media and Radiant core:** `./test/test_paged_media_gtest.exe` owns the 500-case paged-media baseline and the native `radiant.pdf_walk` walker test (`RadiantPdfWalkTest.*`). `./test/test_radiant_core_gtest.exe` owns the separate extended Radiant core unit selection, including `FoExpressions.*`, `RadiantImageSizing.*`, `RadiantLabelBodySizing.*`, `LayoutTableTracks.*` and `RadiantFlowTraits.*`. The executables have disjoint case inventories; no filter is needed to select either whole suite.
- **Paged flow integration:** `./test/test_paged_media_gtest.exe --gtest_filter='TypesetTest.*:ResourceAdmissionTest.*:SecondaryViewTest.PagedFlow*'` checks shared-layout parity, struts, tables, print styles, references, notes/floats, fixed furniture and heading-based footnote counter sets. The image/running-content and rich-paint controls retire the host image cache before painting; rich paint also survives DOM relayout and source-edition release. `PaintListTest.*:PaintIrParityTest.*` checks repeated borrowed lowering and exactly-once owner release. `RenderOutputParity.PagedFlowRichBoundaryPaintMatchesContinuousOutputs` and `PagedCaptionsMatchBlockWrappersAcrossTableAndCaptionContinuations` compare rendered PNG/SVG/PDF or independent block-wrapper controls. Caption and `SizeContainer*` fixtures cover shared geometry/cascade and retained publication. The integration record lists retired item budgets, anonymous-style and secondary-container replay assertions; historical item-engine totals do not qualify this implementation. Published resource ownership follows D4.5.1v4/D4.1.4v5. Aggregate gates remain `make test-radiant-baseline` and the full PDF visual runner; migration of legacy assertions is tracked in `vibe/impl/Radiant_Impl_Paged_Integration.md`.
- **Native paged providers:** `SecondaryViewTest.NativeAdapter*` checks `PagedNativeFlowBinding` body/static/nested streams, producer line alternatives and signed break preferences, forced/forbidden boundaries, ordered vertical glue, mark/target reference replay, cursor/depth/budget failures and retained native paint. Bindings use `TypesetFlowProvider` in `radiant/typeset.hpp` and the common page-flow strut journal. Native insertion/float contributions and static mark/target events remain diagnosed outside this adapter profile; the legacy page policy, region trials and hold/reinsert engine stay removed. See the integration plan's P6/P7 receipt and `vibe/radiant/Radiant_Layout_Paged_Media.md` §3.4.
- **FO decoration arithmetic:** `./test/test_paged_media_gtest.exe --gtest_filter='SecondaryViewTest.FoDecorationArithmetic*'` checks typed shorthand components, arithmetic widths/padding, computed colors, property precedence and transactional rejection. `RenderOutputParity.ComputedBorderShorthandWidthsMatchLiteralWidthsAcrossOutputs` covers the shared continuous CSS projection path. `paged_media_decoration_expressions.fo` / `.rpd` joins the 44 exact FO/native preview/PDF pairs. The existing `radiant_vmap_projection` Lambda golden checks generated SVG fragment context.
- **Paged computed typography:** `./test/test_paged_media_gtest.exe --gtest_filter='SecondaryViewTest.ComputedTypography*:SecondaryViewTest.FoTypography*'` checks quoted font-family groups, normal versus zero spacing, declaring-font inheritance, original FO family ancestors, scalar spacing expressions, retained editions and atomic type/domain rejection. `paged_media_typography_bindings.fo` / `.rpd` adds a three-page exact preview/PDF pair to `RenderOutputParity.FoAndNativePageControlsSharePreviewAndPhysicalPdfPages`. Common selected-style ownership follows D4.5.1v4/D4.1.4v5. XSL spacing ranges remain pending.
- **Paged graphic constraints:** `./test/test_paged_media_gtest.exe --gtest_filter='SecondaryViewTest.NativeImageAllowedScales*:SecondaryViewTest.FoImageScale*:SecondaryViewTest.NativeComputedImageQueries*:SecondaryViewTest.FoGraphicPropertyQueries*'` checks inherited scale sets, typed expression lists, discrete/any selection, exact and fit sizing, failed-edition rollback and retained paint. `paged_media_graphics_scales.fo` and `paged_media_graphics_bindings.fo`, each paired with `.rpd`, join physical export parity; the binding pair also checks owner-font computation, keyword and whole-list queries. Generation ownership follows D4.5.1v4/D4.1.4v5. Apache FOP 2.11 rejects allowed-scale properties; retain that strict reference failure and use the specification-derived SVG/Poppler dimension audit.

- **UI and DOM:** `test-ui-automation` (`ARGS=`), `test-ui-bold` / `test-ui-wired` / `test-ui-clay` / `test-ui-blade` (theme contracts/native fixtures across tiers), `dom-ui test=`, `test-page-load`, `test-reactive-ui`, `test-editable`, `test-wpt-contenteditable`, `test-css-cascade-memory`, `test-pdf-render`, `test-svg-export`, `test-svg-paint`, `test-svg-smil`.
- **Lambda tiers and GC:** `test-lambda-interp`, `interp-sweep`, `test-gc-rooting`, `test-mir-gc-stress`, `check-error-recovery`, `test-grammar-s16`.
- **JS and Node:** `test-js262-prelim`, `test262-baseline` (`VERBOSE=1`), `test262-full`, `test262-update-baseline`, `test-js-parity` (`SUITE=js|test262`, `MODE=mir|ast`), `test-js-opt`, `test-js-parser-diff`, `node-baseline`, `node-regression-gate`, `node-full`, `node-update-baseline`.
- **Packages and data:** `test-math-baseline`, `test-math-corpus`, `test-mathcmp` (Lambda PNG versus pdfLaTeX; `ARGS='--case Functions'`, see [math comparison instructions](lambda/math/README.md)), `test-graph-mermaid`, `test-graph-graphviz`, `test-graph-structurizr`, `test-rdb-drivers-local`.
- **Spelling vocabulary POC:** `./lambda.exe run test/lambda/proc/spelling_vocab_array.ls`
  checks positional JSON rows against `test/input/spelling/vocab_array.json`.
  After `python3 utils/spelling_vocab_poc.py`, run
  `./lambda.exe run test/lambda/spelling/_vocab_file.ls` for all four full-size
  English packs; generated inputs and size reports live under `temp/spelling_vocab_poc/`.
- **Other:** `test-wasm`, `test-coverage`, `lint`, `check-tutorial`, `check-doc-code`, `semantics-verify` (the semantics model as a second oracle over `test/lambda`, see §4; also the last step of `test-lambda-full`).

The shared image codec suite is `./test/test_image_io_gtest.exe` (library
baseline). It covers PNG/JPEG/GIF/WebP/AVIF/BMP/TIFF/ICO, thumbnail planning,
structured errors/warnings, padded channel layouts and alpha conversion,
EXIF/AVIF pixel orientation, native ownership, and allocation-failure cleanup. The
AVIF backend is decoder-only; stored fixtures retain alpha, 10-bit and all 12
rotation/mirror checks, and encoding requests must report `codec_unavailable`.
TIFF coverage includes ordinary 8-bit JPEG strips/tiles and a stored JPEG12
fixture whose metadata remains readable while pixel decoding returns
`unsupported_feature` under the reduced TIFF build.
The `image_io_formats.ls`/`.txt` pair exercises the Lambda `load`/`save` options;
`test_gif_player_gtest.exe` retains the animation/disposal gate.

CSS Text atomic no-break fixtures use explicitly labelled `.standards.<platform>.json`
controls for the seven browser captures that disagree with CSS Text 3 §5.5.
The original Chromium captures remain available. Controls retain strict geometry
and text comparison, record their stylesheet/specification, and reject a changed
HTML source by SHA-256. Regenerate full and compact controls together with
`node utils/capture_layout_reference_controls.js test/css/atomic_no_break_reference_controls.json`;
run `node test/test_layout_reference_controls.js` to check the oracle contract.

## 4. Script-driven harnesses

| Harness | Fixture dirs | Discovery | Expected output | Adding a test |
|---|---|---|---|---|
| `test_radiant_view_gtest` (Scene3D cases) / `test_scene3d_unit_gtest` | `test/scene3d/events`, `test/scene3d/contracts`; authored scenes in `test/{scene3d,webgl,demo/scene3d}` | Explicit `cases.inc` / `contracts.inc`; native unit cases use GTest | CLI exit + versioned `--event-result` JSON; exact event count, positive assertions, zero failures (negative controls require failure) | Add an event JSON and its case inventory entry; keep low-level native probes in the unit executable. New Lambda documents need a sibling `.txt` golden, except asset-heavy fixtures whose converted numbers another test already checks (`imported-assets.ls`, `motion-kenney.ls`): their goldens would mostly print mesh arrays. |
| `test_lambda_gtest` | Functional (`lambda.exe <f>`): `test/lambda`, `test/lambda/{chart,map,latex,math,editor,editing,edit,slide,scene3d,ui_dtna,ui_bold,ui_wired,ui_clay,ui_blade,readability}`, `test/lambda/graph/{mermaid,graphviz,structurizr}`, `test/demo/{doom,photo,cellscope,word,readability,archive}/tests`. Procedural (`lambda.exe run <f>`): `test/lambda/{proc,conc,pdf,eps,archive}`, `test/benchmark/{awfy,r7rs,beng,kostya,larceny}` | Non-recursive. Every `*.ls` is a case; one without its golden **fails** ("No expected output") unless named `_*`, `mod_*` or `schema_*` (helper / module / playground, see `test/test_script_discovery.hpp`). Name is `<parentdir>_<stem>`, with no prefix in `test/lambda` itself. `SLOW_BENCHMARK_TESTS` in `test_lambda_helpers.hpp` are never instantiated. | Sibling `<stem>.txt`. `<stem>.mac.txt` / `.linux.txt` / `.win.txt` wins on that OS. Compared after the `##### Script` marker, with trailing whitespace and `__TIMING__:` lines stripped. | Add `foo.ls` + `foo.txt` in a listed dir (rule 8). A new dir goes into `FUNCTIONAL_`/`PROCEDURAL_TEST_DIRECTORIES` at the top of `test/test_lambda_gtest.cpp`. That file also holds hand-written `TEST`s: tier parity over interp/jit/auto, typed paths on `test/mir/lambda/*.ls` + `.txt`, and error-without-crash checks. |
| `test_lambda_extended_gtest` | `test/lambda/ext`, `test/lambda/proc-ext` (procedural) | Same helper. Names are `ext_<stem>` and `proc_ext_<stem>`. | `.txt` (+ platform override) | Same as above |
| `test_lambda_std_gtest` | `test/std/**` (recursive) | Every `*.ls`; one without its `.expected` fails unless named `_*`/`mod_*`/`schema_*`. `// Mode: procedural` in the first 5 lines means `run`. Name is the relative path with `/` → `_`. | `<stem>.expected` (+ `.mac/.linux/.win.expected`) | Write the `.expected` by hand. `bash test/std/generate_expected.sh` rewrites every passing file, so review the diff. |
| `test_lambda_runtime_gtest` (`test_lambda_errors_gtest.cpp` + `test_binary_schema_gtest.cpp` + `test_pdf_builder_gtest.cpp`; links `lambda-rt` and `radiant`, which the runtime's DOM objects require) | `test/lambda/negative/{syntax,semantic,runtime,io,fuzzy_crashes}`; native binary-schema and PDF builder units; `test/lambda/pdf/output_*.ls`, whose PDFs `test/pdf_output_check.py` compares with `output_*.golden.pdf` (streams inflated; paired 8-bit RGB/soft-mask images allow one level of premultiplied color/alpha rounding; other bytes exact; no external tool). `test/test_pdf_output_check.py` checks that larger pixel changes, dictionary changes and changed nonimage streams fail. | None. Each script has an explicit `TEST_F(NegativeScriptTest, …)`. | C++ assertions: non-zero exit plus a message substring (`ExpectErrorMessage`, `ExpectRuntimeErrorMessage`, `ExpectRejectedOnEveryTier`). No harness reads the `.txt` files in `negative/`. | Add the `.ls` plus a `TEST_F` |
| `test_mir_emission_gtest`, `test_js_mir_emission_gtest` | `test/mir/lambda/*.ls`, `test/mir/js/*.js` | Non-recursive; every script must have a sidecar | `<stem>.mir-check` JSON (`checks[]` with `in_func` + `expect`/`forbid`, optional `args`, `expect_exit_code`) | See `vibe/Lambda_Design_MIR_Emission_Test.md`. The same corpus feeds `test_mir_gc_stress_gtest` (stressed vs unstressed output, no goldens) and `test_mir_ratchet_gtest` (budgets in `test/mir/mir_budgets.json`, 0% slack). |
| `semantics_verify.py` (`make semantics-verify`) | `test/lambda` (`suite=<dir>` for another) | Every `*.ls` + `.txt` pair not named `_*`/`mod_*`/`schema_*`, minus the manifest's `excluded`. Each is dumped with `--emit-ast-dump`, lowered by `test/lambda/sem/mod_sem_poc_ingest.ls`, evaluated by `mod_sem_poc_kernel.ls` on the T0 interpreter (the oracle never inherits a JIT-only defect), and printed by the script printer; the fixture itself runs on the default tier. | The sibling `.txt`, diffed against the model's output and against `lambda.exe`'s, giving the §13.1 four buckets (verified / suspect golden / regression / both moved); a fixture the model cannot lower or evaluate is *pending* with its reason. `// expect: V` comments are checked per binding. | Nothing per fixture: the manifest `test/lambda/sem/semantics-manifest.json` lists the verified set (the gate: it must stay verified), the pending census and hand-written exclusions. `make semantics-verify ARGS='--update'` grows the verified set from one sweep; `test=<stem>` prints one fixture's diffs. Design: `vibe/Lambda_Semantics_DSL_POC.md` §13. |
| `test_interp_gtest` | Scripts listed in `test/lambda/interp_p0_subset.txt` | List-driven | Sibling `.txt`, with zero T0 fallbacks | `make interp-sweep` regenerates `interp_p0_subset.txt` and `interp_excluded.txt` together. Never hand-edit one. |
| `test_js_gtest` | `test/js`, `test/js/props` | Non-recursive `foo.js` + `foo.txt`. `foo.html` or `// @document <file>` makes it a DOM test. Header directives `// @test-permission` and `// @test-module-path`. | `.txt` | Mixed mode is the default: names in `test/js/mir_list.txt` run on MIR, the rest on AST. Override with `--full-mir`, `--full-ast`, or `JS_GTEST_MODE=ast` / `JS_GTEST_MODE=mir`. `--baseline` drops the `lib_codemirror`/`lib_tabulator`/`lib_tom_select` probes. |
| `test_js_mvp_lmd_gtest` | Inline sources in `test/test_js_mvp_lmd_gtest.cpp` | Formal MIR entry and MVP runtime fixtures | C++ assertions, JS results and MIR inspection | Retains ABI, Lambda storage, GC, realm/embedding and compiler regressions. Consolidate overlapping feature checks into their existing focused case; a mixed AST pass alone does not replace MIR coverage. [Consolidation map](../vibe/impl/JS_MVP_Test_Consolidation.md). |
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

The MIR forced-GC sweep checks executed rooting paths (D5.3.1–D5.3.4), with
one unstressed reference plus three GC schedules per Lambda script and two
per JS script. Reviewed scalar-only/`NO_GC` emission probes listed with reasons
in `test/mir/gc_backend_parity.tsv` instead retain native/MIR-interpreter output
parity, alongside their separate emission checks. New fixtures default to full
stress; a missing policy file or stale entry fails the gate. Removing an entry
returns that fixture to full stress. `make test-mir-gc-stress-full`
(or `LAMBDA_MIR_GC_FULL=1`) restores every GC schedule for those probes.

The DocBook reference gate exports all eight publications in one Lambda process
per output mode, sharing package compilation while rebuilding each publication.
Single-page and chunked output retain complete body comparisons, chunk filename
checks and local resource/link validation. Math SVG snapshots retain font
declarations and geometry, normalizing only the installation path and custom
font payloads. Bundled font URLs must name readable distributed files and
contain no base64 font data (D7.2.9); bytes-only custom fonts retain their
data-URL snapshots.

Lambda emission fixtures execute in batches of 32 through `test-batch`, which
resets the heap, roots, module state and Inputs between scripts (D5.4.3).
GTest filters and shards select the batch contents; every script retains its
own result; batch execution time is charged to its first requesting GTest case.
Missing, duplicate, malformed or truncated results and process
teardown failures fail the batch without retries. JS and additional lifetime
regressions keep separate processes. `LAMBDA_MIR_GC_BATCH_SIZE=1` restores
standalone execution; values up to 128 select a different batch size.
`make test-mir-gc-stress ARGS='--gtest_filter=*gc_parse_merged_text*'` runs a
focused check. A diagnostic line reports the actual process count.

Keep allocation churn and extra package fixtures small: collecting at every
allocation repeatedly scans retained objects. The music sweep uses
`music_gc_roots`; its full A4/reflow matrix remains in `music_layout`'s golden
test. Time the gate with a release host (`make build-release-compile`, then
`make test-mir-gc-stress`).

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
| Semantics manifest (verified set + pending census) | `make semantics-verify ARGS='--update'` |
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
./test/test_lambda_runtime_gtest.exe --gtest_filter='NegativeScriptTest.*Counted*'
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

**PDF output goldens.** `PdfFileOutput.*` requires each `test/lambda/pdf/output_*.ls` PDF to match its `output_*.golden.pdf` object for object, with Flate streams compared after inflating, so zlib builds that compress differently still match. When a change to the PDF writer is intended, regenerate the PDF and accept it. For `output_phase6` the `accept` step also renders the page with poppler (`pdftoppm`, `pdfinfo`) and checks 14 pixel colours, which is the independent visual check:

```bash
./lambda.exe run test/lambda/pdf/output_phase6.ls && python3 test/pdf_output_check.py temp/pdf_lambda_phase6.pdf accept test/lambda/pdf/output_phase6.golden.pdf phase6
```

The other two fixtures use `accept test/lambda/pdf/output_resources.golden.pdf` with no kind, and `accept test/lambda/pdf/output_pages.golden.pdf 3`.

Gotchas:
- In `test_lambda_gtest`, the gtest filter also chooses which scripts the batch runs, so a narrow filter is fast. `test_lambda_std_gtest` always batches all of `test/std`.
- The display-list, state-store, DOM-range and media-player tests link against `test/*_stubs.cpp`. A new `radiant/` symbol that code calls needs a stub there, and only `make build-test` catches a missing one.
- The `LAMBDA_BASELINE_TEST_PROJECTS`, `RADIANT_BASELINE_TEST_PROJECTS` and `INPUT_BASELINE_TEST_PROJECTS` lists at the top of the Makefile must match what the gates run. Update them when you add a baseline binary.

WebAssembly build and tests: `make build-wasm`, `make test-wasm`, and [`doc/dev/Lambda_WASM_Build.md`](../doc/dev/Lambda_WASM_Build.md).

### Lambda UI dtna

`make test-ui-dtna ARGS='--jobs 1'` checks the 73-entry feature manifest,
package goldens once on the selected default backend (**D8.1.1v18**), state-store
regressions, and the manifest-owned `dtna` native UI suite on forced
interpreter/JIT paths. The package goldens use the ordinary discovered batch,
sharing imported modules (**D7.2.3/D8.5.1v7**); no separate contract runner repeats
them. Document-error teardown still runs on forced interpreter/JIT with the
button module that owns the validation error, keeping its imports local.
The shared Radiant baseline also
includes this suite. The galleries are `test/ui/dtna_gallery.ls` and `test/ui/dtna_data_gallery.ls`; the JSON-formatted
coverage inventory uses `.manifest` because every `.json` under `test/ui/` must
be an executable fixture with exactly one owner.

Pure/API goldens: `test/lambda/ui_dtna/`. Native click/type/keyboard/style checks:
`test/ui/dtna/`. Coverage and remaining scope:
`test/ui/dtna_reference/catalog.manifest`. The gallery capture is a native
inspection artifact, not an AntD visual-equivalence baseline.

The galleries, native component pages and reference foundation author
`<dtna.*>` elements and apply the imported view templates (**S12.1.3**).
Stateful fixtures keep stable logical component bindings across parent redraws;
native HTML remains where it isolates engine behavior or exposes test outputs.
Constructor API/validation goldens and `dtna_error_document.ls` retain explicit
constructor calls to exercise that interface.

The URL clone ownership control (`./test/test_url_gtest.exe --gtest_filter='UrlCloneOwnershipTest.*'`) runs with allocation tracking and checks clone/free balance, retained components after source release and absent-component preservation. It covers the resource-base clones used by FO/native graphic exports under D4.5.1v4/D4.1.4v5 and D7.1.2v2.

### Lambda UI clay

`make test-ui-clay ARGS='--jobs 2'` checks the 59-component theme catalog,
source contracts and native interactions on interpreter, automatic and JIT paths.
Source goldens: `test/lambda/ui_clay/`. Native fixtures: `test/ui/clay/` and
`test/ui/clay_{gallery,components}.json`. The Radiant baseline includes `clay`.
See [the Clay guide](../doc/Lambda_UI_Clay.md) for the surface and current limits.

### Lambda UI blade

`make test-ui-blade ARGS='--jobs 2'` checks the 64-component fantasy theme,
18 vector illustrations, source contracts, and native RPG/control/collection
interactions on interpreter, automatic and JIT paths. Source goldens:
`test/lambda/ui_blade/`. Native fixtures: `test/ui/blade/` and
`test/ui/blade_{gallery,components}.json`. The Radiant baseline includes `blade`.
See [the Blade guide](../doc/Lambda_UI_Blade.md) for widget props and limits.
