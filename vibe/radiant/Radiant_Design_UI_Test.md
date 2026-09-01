# Radiant UI Test Runner Unification

- **Status:** Phase 1/2 implementation in progress
- **Date:** 2026-09-01
- **Scope:** event-driven Radiant UI fixtures executed through `lambda.exe view`
- **Formal linkage:** no new language or runtime ruling. The runner's use and teardown of Lambda's strict JSON parser must follow **D4.2.1v3**: allocator parent/backing edges are established at creation and released in reverse-dependency order.

## Executive decision

All event-driven Radiant UI fixtures shall have one owner, one manifest, and one executable runner:

```text
test/ui/ui_test_manifest.json
                |
                v
test/test_ui_automation_gtest.exe --suite <name>
                |
                v
./lambda.exe view <page> --event-file <fixture> ...
```

`test_ui_automation_gtest.exe` becomes the only runner for JSON event fixtures. It owns discovery, strict parsing, preflight, filtering, process launch, resource limits, result parsing, and reporting. Make targets become thin suite selectors.

### Implementation checkpoint (2026-09-01)

The first cut is now landed in the working tree: `test/ui/ui_test_manifest.json` owns 466 fixtures across the six suites; the C++ runner uses Lambda's strict JSON parser, validates ownership/pages/assertions, supports `--suite`, `--tag`, `--test`, `--preflight`, bounded scheduling, and machine-result assertions; and `lambda.exe view` emits the versioned result file. The DOM Node runner, hit-test/editor shell loops, hard-coded editor command list, and JSON-fixture launch table in `test_radiant_view_gtest.cpp` have been removed. Native fixtures have an explicit `--native-gui` execution path and are excluded from ordinary headless selections. Remaining follow-up work is modularizing the runner, aggregate JSON reporting, and dedicated preflight/error-path unit tests.

The following parallel runner implementations are retired:

- the hand-written JSON string scanning in `test/test_ui_automation_gtest.cpp`;
- `test/ui/dom/run-dom-ui.mjs`;
- the hit-test and editor shell loops in the Makefile;
- the 32 hand-written `lambda.exe view ... --event-file ...` commands in `editable-editor-e2e`;
- the JSON-fixture table and launch path in `test/test_radiant_view_gtest.cpp`.

`test_radiant_view_gtest.cpp` may retain genuinely native tests that contain C++ assertions or exercise non-fixture APIs. It must not remain a second JSON event-fixture runner.

The central invariant is:

> Adding a UI fixture changes data only: its JSON file and, when needed, one manifest ownership rule. It never requires a new loop, command list, or runner.

## 1. Current problem

The current tree has several implementations of the same operation: select a fixture, determine its page, invoke `lambda.exe view`, interpret the result, and report pass or failure.

| Implementation | Discovery and metadata | Scheduling | Result handling |
| --- | --- | --- | --- |
| `test/test_ui_automation_gtest.cpp` | scans only `test/ui/*.json`; extracts `html`, `skip_headless`, event types, and waits with `strstr`/`strchr`; silently ignores missing pages | CPU-derived jobs clamped by a memory-derived limit | parses the human `Assertions:` line with `sscanf` |
| `test/ui/dom/run-dom-ui.mjs` | parses `test/ui/dom/*.json` with Node `JSON.parse`; uses a sibling page by default | separate CPU-minus-one worker pool | trusts process exit and prints its own summary |
| `hit-test-ui` Make target | shell glob plus `sed` extraction of `html` | sequential shell loop | trusts process exit |
| `editor-4c-view` Make target | three shell globs, `sed`, and a hard-coded default page | sequential shell loop | suppresses output and trusts process exit |
| `editable-editor-e2e` Make target | 32 explicit page/fixture command pairs | sequential Make recipe | stops on command failure |
| `test/test_radiant_view_gtest.cpp` | hard-coded C++ case table, including five `test/view` event fixtures | its own parallel pre-run | independently checks exit and selected output strings |

The working-tree inventory on 2026-08-31 contains 460 JSON fixtures below `test/ui` and five below `test/view`. Notable subsets are 65 DOM fixtures, 53 Stage 4B/4C editor fixtures, six hit-test fixtures, five view fixtures, and two `skip_headless` fixtures. These counts describe the migration input; they are not intended as permanent baselines.

This split causes concrete defects:

- JSON semantics depend on the runner. The C++ and shell implementations are not escape-aware and can match a nested or quoted `"html"` token as metadata.
- A malformed fixture can be rejected by Node, silently skipped by the C++ runner, or misread by `sed`.
- Missing pages and unowned subdirectory fixtures can disappear from coverage without failing a gate.
- Filtering, concurrency, memory budgeting, font setup, output capture, and summaries differ by target.
- `skip_headless: true` currently means the main C++ runner omits the test; it does not provide a target that actually runs it in a native window.
- The 32-command editor list duplicates fixture-to-page metadata that belongs in data.
- Human-readable event output is serving as an unstable machine protocol.

## 2. Goals and non-goals

### Goals

1. Make the C++ GTest executable the sole JSON event-fixture runner.
2. Give every managed fixture exactly one suite owner.
3. Fail closed before launching children when configuration or fixture data is invalid.
4. Put suite selection, page defaults, tags, fonts, GUI requirements, timeouts, and resource budgets in one manifest.
5. Keep fixture-specific behavioral checks in JSON.
6. Give every Make target the same filtering, concurrency, output, and reporting behavior.
7. Preserve one GTest case per fixture so CI and local focused runs remain easy to identify.

### Non-goals

- This proposal does not replace `radiant/event_sim.cpp`; it remains the in-process event and DOM assertion engine.
- It does not absorb layout comparison, render-visual, WPT, page-load, or non-UI JavaScript test runners.
- It does not force native C++ invariants into JSON. A test that directly calls a C/C++ API remains a native GTest.
- It does not make a fixture belong to multiple suites. Cross-cutting selection uses tags; primary ownership remains unique.
- It does not allow arbitrary shell commands in fixtures or the manifest.

## 3. Repository layout and suite ownership

Event JSON files should be physically grouped so a reviewer can understand the test inventory without reading Makefile recipes:

```text
test/
  ui/
    ui_test_manifest.json
    baseline/                 generic headless UI event fixtures
    dom/                      browser-library and DOM integration fixtures
    editor/
      core/                   focused native/contenteditable behavior
      upstream/               CodeMirror, ProseMirror, Editor.js, drawing packages
      stage4b/
      stage4c/
      stage4c-phase-c/
    hit-test/                 elementFromPoint and SVG geometry fixtures
    native-gui/               WKWebView and other real-window fixtures
    state/                    referenced `.mark` files; not fixture JSON
    snapshots/                referenced images; not fixture JSON
  view/                       `lambda.exe view` navigation/load fixtures
```

Page files do not need to move merely to mirror the fixture tree. Shared harnesses may remain under `test/html`, `test/editable-editors`, `test/layout/data`, `test/lambda`, or next to a fixture. The manifest resolves those relationships. When a fixture currently relies on a sibling-page fallback, either the pair moves together or the fixture receives an explicit `html` path before the JSON moves.

The initial primary suites are:

| Suite | Purpose | Default execution |
| --- | --- | --- |
| `baseline` | generic UI event behavior used by the Radiant baseline | headless |
| `dom` | DOM integration and checked-in browser-library fixtures | headless |
| `editor` | contenteditable, editor, clipboard, selection, and drawing-editor fixtures | headless |
| `hit-test` | coordinate, stacking, SVG, and `elementFromPoint` assertions | headless |
| `view` | document-load, iframe navigation, and view-command fixtures | headless |
| `native-gui` | tests requiring a real native window or embedded platform view | native GUI |

A fixture has exactly one primary suite. More detailed groupings such as `reactive`, `stage4c`, `codemirror`, `drawing`, `webview`, or `slow` are tags, not additional owners.

## 4. Manifest is the authority

The proposed manifest path is `test/ui/ui_test_manifest.json`. Directory names are explanatory; the manifest is authoritative.

An illustrative schema is:

```json
{
  "schema_version": 1,
  "fixture_roots": [
    "test/ui/**/*.json",
    "test/view/**/*.json"
  ],
  "non_fixture_files": [
    {"path": "test/ui/ui_test_manifest.json", "reason": "suite manifest"}
  ],
  "runner": {
    "lambda": "./lambda.exe",
    "temp_root": "./temp/ui-test",
    "jobs": "auto",
    "memory_budget_fraction": 0.5,
    "default_memory_mib_per_job": 1024,
    "default_timeout_ms": 30000,
    "max_output_bytes": 4194304,
    "default_assertions": [
      {"type": "process_exit", "equals": 0},
      {"type": "event_summary", "failed": 0}
    ]
  },
  "suites": {
    "baseline": {
      "owner": "radiant-ui",
      "include": ["test/ui/baseline/**/*.json"],
      "page": {"fixture_field": "html", "fallback": "sibling"},
      "tags": ["baseline", "headless"],
      "font_dirs": ["test/layout/data/font"],
      "gui": "headless",
      "memory_mib_per_job": 1024
    },
    "dom": {
      "owner": "radiant-dom",
      "include": ["test/ui/dom/**/*.json"],
      "page": {"fixture_field": "html", "fallback": "sibling"},
      "tags": ["dom", "headless"],
      "font_dirs": ["test/layout/data/font"],
      "gui": "headless"
    },
    "editor": {
      "owner": "radiant-editing",
      "include": ["test/ui/editor/**/*.json"],
      "page": {"fixture_field": "html", "default": "test/html/editor-dom.html"},
      "tags": ["editor", "headless"],
      "font_dirs": ["test/layout/data/font"],
      "gui": "headless",
      "timeout_ms": 60000,
      "memory_mib_per_job": 1024
    },
    "hit-test": {
      "owner": "radiant-events",
      "include": ["test/ui/hit-test/**/*.json"],
      "page": {"fixture_field": "html", "required": true},
      "tags": ["hit-test", "headless"],
      "font_dirs": ["test/layout/data/font"],
      "gui": "headless"
    },
    "view": {
      "owner": "radiant-view",
      "include": ["test/view/**/*.json"],
      "page": {"fixture_field": "html", "default": "test/html/index.html"},
      "tags": ["view", "headless"],
      "gui": "headless"
    },
    "native-gui": {
      "owner": "radiant-platform",
      "include": ["test/ui/native-gui/**/*.json"],
      "page": {"fixture_field": "html", "fallback": "sibling"},
      "tags": ["native-gui"],
      "gui": "native",
      "jobs": 1
    }
  },
  "overrides": [
    {
      "include": ["test/ui/editor/upstream/codemirror-*.json"],
      "tags": ["codemirror", "upstream-editor"],
      "page": {"default": "test/editable-editors/fixtures/codemirror/typing.html"}
    }
  ]
}
```

The checked-in manifest is the executable ownership contract. It must enumerate every managed fixture and pass preflight before a suite is allowed to launch children.

### Manifest rules

- Paths and globs are repository-root relative and normalized before comparison.
- A fixture root finds the universe of files that require ownership.
- `non_fixture_files` is explicit and narrow. It cannot be a broad ignore glob, and each entry carries a reason in the final schema.
- Each fixture must match exactly one suite `include` set. Zero matches is unowned; more than one is duplicate ownership. Both are fatal.
- Overrides may add tags or replace execution metadata, but may not change primary ownership.
- A suite declares one `owner`. Ownership is a maintenance responsibility, not merely a directory label.
- `gui` is `headless` or `native`; the deprecated fixture field `skip_headless` is removed after migration.
- `font_dirs` is an array and becomes repeated `--font-dir` arguments in declared order.
- A page is resolved from the fixture's `html` field first, then the suite's declared fallback. No undeclared implicit path search is allowed.
- Absolute paths and paths escaping the repository are rejected.

## 5. One strict JSON parser and one preflight

The runner, manifest loader, fixture preflight, event simulator, and machine-result reader shall use Lambda's strict JSON parser through one small test-harness adapter. The runner must not add another JSON lexer or copy parser logic.

The adapter should wrap `parse_json_to_item_strict()` and expose typed field readers with path-aware diagnostics. It must use Lambda `Str`, `ArrayList`, and `HashMap` rather than introducing new `std::` containers. Parser Inputs and their allocator graph are released in reverse-dependency order as required by **D4.2.1v3**.

Preflight runs before any child process. Every normal suite invocation preflights the complete managed fixture universe; selecting `--suite dom` must not hide a duplicate owner in another suite.

Preflight fails on:

1. invalid manifest JSON or unsupported `schema_version`;
2. invalid fixture JSON;
3. wrong field types, missing `events`, or an event without a valid `type`;
4. an unresolved or unreadable page;
5. an unowned fixture;
6. a fixture matched by more than one suite;
7. a zero-assertion fixture;
8. duplicate stable test IDs or GTest names after sanitization;
9. an invalid glob, tag, font directory, timeout, resource limit, or GUI mode;
10. a fixed `wait` immediately before an auto-waiting assertion, preserving the current anti-sleep gate;
11. deprecated execution metadata such as `skip_headless` after its migration window;
12. a runner assertion referring to a path outside its per-test output directory, except for explicitly read-only reference artifacts.

Zero assertions means zero author-declared behavioral checks. The count includes event entries whose type begins with `assert_` and top-level runner assertions described below. Implicit process-success policy and suite defaults do not satisfy this rule.

The standalone command is:

```sh
./test/test_ui_automation_gtest.exe --preflight --suite all
```

It prints the ownership count per suite and exits non-zero on any diagnostic. `--gtest_list_tests` remains metadata-only and must never launch `lambda.exe`.

## 6. Fixture assertions

There are two assertion locations because they observe different boundaries.

### 6.1 In-document assertions

Existing `assert_*` entries in `events` remain the preferred way to verify DOM, layout, state, pixels, selection, focus, clipboard, and event behavior. They execute inside Radiant and participate in event-simulator retries.

```json
{
  "name": "iframe XML navigation",
  "html": "test/html/index.html",
  "events": [
    {"type": "click", "target": {"selector": "a[href=\"../input/test.xml\"]"}},
    {"type": "switch_frame", "selector": "iframe[name=\"main_frame\"]"},
    {"type": "assert_text", "target": {"selector": "header title"}, "contains": "Bookstore"}
  ]
}
```

### 6.2 Runner assertions

An optional top-level `assertions` array verifies the process and artifacts outside the document. This replaces specialized runner branches and output checks.

```json
{
  "name": "view command remains quiet",
  "html": "test/html/index.html",
  "events": [
    {"type": "assert_count", "target": {"selector": "iframe"}, "min": 1}
  ],
  "assertions": [
    {"type": "process_exit", "equals": 0},
    {"type": "event_summary", "failed": 0, "min_total": 1},
    {"type": "output", "stream": "combined", "not_contains": "[LAYOUT_PROF]"}
  ]
}
```

Initial runner assertion types are:

| Type | Purpose |
| --- | --- |
| `process_exit` | exact allowed process exit code |
| `event_summary` | events executed and event assertion pass/fail counts |
| `output` | contains/not-contains checks over stdout, stderr, or combined output |
| `artifact_text` | contains/not-contains checks in a declared per-test output file |
| `artifact_exists` | require or forbid a declared output artifact |
| `peak_memory` | compare measured peak child memory to a declared ceiling |

Suite-level default assertions express common policy once, for example process exit zero and no event assertion failures. Fixture-level clauses extend those defaults. Adding a new external verification requires one evaluator in the unified C++ runner and JSON clauses in fixtures; it never justifies another runner.

Arbitrary regular expressions, shell expressions, and executable callbacks are intentionally excluded. String, numeric, boolean, count, and bounded-tolerance operators keep the format deterministic and portable.

## 7. Machine-readable child result

The runner must stop parsing the human `Assertions:` line. `lambda.exe view` shall accept a unique result path, for example:

```sh
./lambda.exe view page.html \
  --event-file fixture.json \
  --event-result ./temp/ui-test/view/iframe_xml/result.json \
  --headless --no-log
```

The event simulator writes a versioned result atomically after simulation:

```json
{
  "schema_version": 1,
  "events_executed": 5,
  "assertions": {"passed": 2, "failed": 0},
  "result": "pass"
}
```

Human output remains available for local diagnosis. The JSON result is the machine protocol and is parsed by the same strict parser as the manifest and fixture. A missing or malformed result is a harness failure, distinct from an assertion failure. A crash is reported from process status even when no result exists.

Each fixture gets an isolated directory below `./temp/ui-test/<suite>/<stable-id>/`. Logs, result JSON, state dumps, screenshots, and other generated artifacts are rooted there, eliminating parallel collisions on `log.txt` or shared filenames.

## 8. Unified execution model

The executable performs these phases:

```text
parse runner flags
      |
strictly parse manifest and all managed fixtures
      |
resolve unique ownership, pages, tags, requirements, and assertions
      |
apply suite/tag/test/GTest filters once
      |
schedule selected child processes in one bounded worker pool
      |
parse machine results and evaluate runner assertions
      |
publish one GTest case per fixture plus one aggregate report
```

### Filtering

The runner supports:

```text
--suite <name>       required for normal execution; repeatable; `all` is explicit
--tag <tag>          include fixtures carrying the tag; repeatable
--exclude-tag <tag>  exclude fixtures carrying the tag; repeatable
--test <glob>        filter stable fixture IDs; repeatable
--gtest_filter=...   standard final GTest filtering, normalized through the same selector
```

There is one filter implementation. Filtering determines both which children launch and which GTest cases report; no selected test may run invisibly before being filtered out.

Stable IDs use `<suite>/<repository-relative-fixture-stem>`, independent of enumeration order. GTest display names are sanitized from the stable ID, and collisions fail preflight.

### Bounded parallelism and memory

The scheduler computes its job count as the minimum of:

- the explicit `--jobs` value or suite override;
- the CPU-derived default;
- the number of selected fixtures;
- the aggregate memory budget divided by the largest selected fixture's declared `memory_mib_per_job`.

The default aggregate budget is half of physical memory, matching the intent of the current C++ guard without encoding a machine-specific job count. `--memory-budget-mib` may lower it. Increasing it beyond physical-memory-derived safety requires an explicit `--allow-oversubscribe` flag.

Each child also receives manifest-controlled hard limits for elapsed time, captured output, and peak memory. Resource-limit termination is reported separately from assertion failure. Platform adapters may use process monitoring or native facilities, but unsupported hard-limit enforcement must be reported and must fail suites that declare it required; it may not silently disappear.

Longest estimated fixtures run first. Estimated duration comes from the parsed event data and configured timeout, not string scanning.

`native-gui` defaults to one job. Selecting it on a host without the declared GUI capability fails clearly unless the caller explicitly requests `--allow-unavailable`, in which case each unavailable case is a GTest skip. A general headless suite never silently consumes native-GUI fixtures.

### Output and reporting

The runner captures stdout and stderr separately, subject to the output limit. Passing output stays quiet; failure output is printed once under the fixture's GTest case.

Terminal reporting is deterministic and includes:

- suite and stable fixture ID;
- page and event fixture paths;
- duration and peak memory;
- process exit or signal;
- in-document assertion totals;
- runner assertion totals;
- output/result/artifact paths on failure.

An optional `--report-json <path>` writes a versioned aggregate report for the Radiant baseline target. The Makefile must consume this report rather than scrape colored or human GTest lines.

## 9. C++ structure

The current single large test file should be split by responsibility rather than replaced with another monolith:

| Proposed file | Responsibility |
| --- | --- |
| `test/ui_test_manifest.hpp/.cpp` | strict manifest/fixture parsing, typed access, path normalization, ownership resolution, preflight diagnostics |
| `test/ui_test_filter.hpp/.cpp` | suite, tag, test, and GTest filter normalization |
| `test/ui_test_process.hpp/.cpp` | isolated temp directory, `lambda.exe` argv, GUI mode, time/output/memory limits, result capture |
| `test/ui_test_assert.hpp/.cpp` | machine-result and runner-assertion evaluation |
| `test/ui_test_scheduler.hpp/.cpp` | bounded longest-first worker queue and resource budget |
| `test/ui_test_report.hpp/.cpp` | GTest-facing result objects and aggregate JSON report |
| `test/test_ui_automation_gtest.cpp` | CLI, dynamic GTest registration, orchestration only |

Shared helpers are promoted to these modules; no static helper is copied between runners. New code follows the repository C+ conventions and uses `Str`, `ArrayList`, `HashMap`, and the existing shell/process abstraction rather than `std::string`, `std::vector`, or ad-hoc process launch code.

Dynamic GTest registration should replace the current static shallow scan and index-based parameterization. The manifest is loaded before `RUN_ALL_TESTS()`, selected fixtures are registered by stable ID, and the scheduler populates their results without changing discovery order.

## 10. Make targets

Every event-fixture target invokes the same executable with `--suite`. Make retains build dependencies and friendly aliases, not test inventories.

Illustrative recipes are:

```make
UI_TEST_RUNNER := ./test/test_ui_automation_gtest.exe

test-ui-automation: build-test
	$(UI_TEST_RUNNER) --suite all $(ARGS)

dom-ui-run: build-test
	$(UI_TEST_RUNNER) --suite dom $(ARGS)

hit-test-ui: build-test
	$(UI_TEST_RUNNER) --suite hit-test $(ARGS)

editor-4c-view: build-test
	$(UI_TEST_RUNNER) --suite editor --tag stage4c-view $(ARGS)

editable-editor-e2e: build-test
	$(UI_TEST_RUNNER) --suite editor --tag upstream-editor $(ARGS)

view-ui: build-test
	$(UI_TEST_RUNNER) --suite view $(ARGS)

native-gui-ui: build-test
	$(UI_TEST_RUNNER) --suite native-gui --native-gui $(ARGS)

```

Other existing aliases map to suite plus tags:

| Existing target | Unified selection |
| --- | --- |
| `test-reactive-ui` | `--suite baseline --tag reactive` |
| `editable-unit` | `--suite editor --tag unit` |
| `editable-ui` | `--suite editor --tag contenteditable` |
| `editable-editor-e2e` | `--suite editor --tag upstream-editor` |
| `drawing-editor-e2e` | `--suite editor --tag drawing` |
| `editor-4c-view` | `--suite editor --tag stage4c-view` |
| `dom-ui-run` | `--suite dom` |
| `hit-test-ui` | `--suite hit-test` |

Composite targets such as `test-editable` and `test-drawing` may retain dependencies on those thin aliases. They must not contain fixture paths.

The Radiant baseline should eventually invoke the unified runner once with `--suite baseline,dom,editor,hit-test,view` and consume the aggregate report, rather than separately scraping UI, DOM UI, and view-fixture summaries. Native C++ view tests remain a separate native-test line only if they no longer launch JSON fixtures. The native-GUI suite runs in a GUI-capable job with `--native-gui`, not as an implied part of a headless baseline.

## 11. Migration plan

### Phase 1 — parser, manifest, and preflight

1. Add the strict JSON adapter and typed manifest model.
2. Check in `test/ui/ui_test_manifest.json` covering the current fixture universe without moving files yet.
3. Add preflight unit tests for malformed JSON, missing pages, zero assertions, duplicate ownership, unowned files, bad defaults, path escapes, and sanitized-name collisions.
4. Make `--preflight --suite all` pass with current fixtures.

The legacy runners have now been removed as part of the initial cutover; the remaining work in this phase is to split the large runner and add its dedicated error-path tests.

### Phase 2 — unified execution and result protocol

1. Add the event-result JSON output to `lambda.exe view`.
2. Implement the shared process, scheduler, filter, assertion, and report modules.
3. Run each suite in shadow mode and compare selected fixture IDs, page resolution, exit status, assertion totals, and wall time with its legacy runner.
4. Add scheduler tests for CPU and memory clamps, filtering-before-launch, list-only behavior, timeouts, output truncation, and deterministic reporting.

### Phase 3 — physical suite migration

1. Move generic top-level event JSON to `test/ui/baseline`.
2. Consolidate editor JSON below `test/ui/editor`, retaining useful subgroups.
3. Keep `test/ui/dom` and `test/ui/hit-test` as their suite roots.
4. Keep the view fixtures under `test/view` and move the remaining markdown iframe fixture there.
5. Move the two native-window fixtures to `test/ui/native-gui` and replace `skip_headless` with manifest requirements.
6. Update fixture reference paths and prove that every managed JSON has exactly one owner.

Moves occur only after manifest-based execution works, so a move changes an ownership glob rather than a runner implementation.

### Phase 4 — cutover and deletion (initial cut completed)

1. Switch event-fixture Make targets to `test_ui_automation_gtest.exe --suite ...` (completed for the current aliases).
2. Remove `test/ui/dom/run-dom-ui.mjs` (completed).
3. Delete the hit-test and editor shell loops (completed).
4. Delete the 32-command `editable-editor-e2e` list (completed).
5. Remove JSON fixture execution from `test_radiant_view_gtest.cpp` (completed; native format smoke tests remain).
6. Remove hand-written JSON scanners and human-summary parsing from `test_ui_automation_gtest.cpp` (completed; machine-result JSON is consumed).
7. Make full preflight a prerequisite of the Radiant baseline and replace its remaining text scraping with the aggregate report (in progress).

No legacy runner remains as a fallback after cutover; otherwise coverage will diverge again.

## 12. Acceptance gates

The migration is complete only when all of the following hold:

1. `--preflight --suite all` reports zero invalid, missing-page, unowned, duplicate-owned, zero-assertion, or invalid-requirement fixtures.
2. Manifest ownership totals equal the filesystem fixture universe after declared non-fixture files are removed.
3. Each retired runner's fixture set is exactly reproduced by a suite/tag query before deletion.
4. Focused filters launch only selected children; `--gtest_list_tests` launches none.
5. Each suite passes independently through the unified runner.
6. Native-GUI fixtures run in the native-GUI job instead of being counted as headless coverage.
7. Timeout, memory, output-limit, crash, missing-result, assertion-failure, and malformed-result paths have unit tests and distinct diagnostics.
8. Linux, macOS, and Windows path/glob normalization and process launch are covered.
9. `make test-radiant-baseline` passes on the clean migration tree and reports unified suite totals without scraping human output.
10. Repository search finds no event-fixture loop, Node event runner, hard-coded editor invocation list, or second JSON fixture-launch table.

## 13. Expected result

The fixture becomes the test, the manifest becomes the ownership map, and the C++ executable becomes the only execution policy. Suite growth is then linear and reviewable: add a JSON file with at least one assertion, ensure one manifest rule owns it, and run the same runner locally and in CI.
