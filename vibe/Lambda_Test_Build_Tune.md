# Lambda Test Build Tune

Proposal for reducing `make build-test` wall-clock and trimming dead weight from `./test/`.

**Status as of 2026-05-30:** Tracks 1, 2, and 3 are landed. PCH was measured and explicitly skipped (see Track 2 P1 below). Section banners (✅ / ❌ / ⏭) reflect outcomes.

Snapshot at proposal time:

- ~120 `*.cpp` test sources, 147 test executables defined in [build_lambda_config.json](../build_lambda_config.json).
- `make build-test` runs `make -C build/premake config=debug_native -j$(TEST_JOBS)` where `TEST_JOBS = (NPROCS+1)/2` ([Makefile:55](../Makefile#L55)) — intentionally halved for Debug+ASan memory pressure.
- `lambda-input-full` is already a dynamic library (`"link": "dynamic"`), shared by 69 test entries. `lambda-lib` is still **static**, with 31 sources.

The proposal has three independent tracks. Each can land on its own; they compound.

---

## Track 1 — Shared test utilities (`lib/test_utils.{h,c,_runtime.cpp}`) ✅

A survey of the ~120 test `*.cpp` files found heavily duplicated scaffolding. Two helper files already exist and should be reused, not duplicated:

- [test/lib/test_lambda_helpers.hpp](../test/lib/test_lambda_helpers.hpp) — Lambda-specific harness (used by ~5 tests).
- [test/input_roundtrip_helpers.{cpp,hpp}](../test/input_roundtrip_helpers.cpp) — roundtrip comparison helpers (used only by roundtrip tests).

Anything not covered there should land in a new `lib/test_utils.{h,cpp}`. The header exposes a C-style API; the implementation is C++ because several helpers need `String*`, `Pool*`, `Heap*` from the runtime.

### Implementation outcome

Split into three files instead of one, so tests that don't need the Lambda runtime don't pull in `transpiler.hpp` + MIR:

- **[lib/test_utils.h](../lib/test_utils.h)** — public API: temp dir/file, process spawn, file I/O, string normalization, `tu_setup_pool`, `tu_setup_runtime`.
- **[lib/test_utils.c](../lib/test_utils.c)** — pure-C implementation of the C helpers and `tu_setup_pool` (no Lambda runtime deps).
- **[lib/test_utils_runtime.cpp](../lib/test_utils_runtime.cpp)** — `tu_setup_runtime` + Heap/EvalContext fixture, isolated to its own TU so only tests that need the full runtime pay the include cost.

Tests opt in via `additional_sources` in `build_lambda_config.json`: just `lib/test_utils.c` for the lightweight case, or both files for full-runtime tests.

### Helpers shipped

| P | Helper group | API | Status |
|---|---|---|---|
| P0 | Runtime fixture setup/teardown | `tu_setup_runtime(...)` / `tu_teardown_runtime(...)` | ✅ shipped |
| P0 | Pool-only fixture | `tu_setup_pool()` / `tu_teardown_pool(pool)` | ✅ shipped (added during migration — survey showed most tests didn't need full runtime) |
| P0 | Temp dir/file under `./temp/` | `tu_mkdtemp(name)`, `tu_write_temp(content, ext)`, `tu_rmtree(path)` | ✅ shipped |
| P0 | Cross-platform process spawn | `tu_run(cmd, &exit_code)` | ✅ shipped |
| P1 | Golden-file comparison | `tu_read_golden(path)`, `tu_assert_matches_golden(...)` | ⏭ deferred — only ~25 tests benefit, lower value than expected |
| P1 | String normalization | `tu_trim_trailing(s)`, `tu_strip_lines(s, prefix)` | ✅ shipped |
| P2 | File I/O | `tu_slurp(path)`, `tu_exists(path)` | ✅ shipped |
| P2 | Lambda string ctor | `tu_lambda_str(const char*) -> String*` | ⏭ deferred — ~10 tests benefit, trivial inline cost |

### Migrations landed

| Tier | Tests migrated | Total tests now running |
|---|---|---|
| Canary (validates both API modes) | 2: test_dir_gtest, test_bash_official_gtest | 9 + N (bash suite is parameterized) |
| Mass migration round 1 (pool-only fixture) | 10: test_edit_bridge, test_mark_editor, test_name_pool, test_sysinfo, test_html_css, test_ast_validator, test_validator_input, test_validator_features, test_namespace, test_format_markup | 287 |
| Mass migration round 2 (extract leading log_init+pool from custom fixtures) | 5: test_dom_range, test_source_pos_bridge, test_mark_builder_deepcopy, test_null_vs_missing, test_html_gtest | 179 |
| **Total migrated** | **17** | **475+ tests, 0 regressions** |

### Lessons / bugs found during migration

- **`extern "C"` placement bug.** The pool helpers were initially declared outside the `extern "C"` block in [test_utils.h](../lib/test_utils.h) — C++ callers expected mangled names but the C source produced unmangled symbols. Caught at link time, fixed.
- **Right-sized helpers matter.** The proposal expected ~90 tests would use `tu_setup_runtime`. In practice the survey showed most candidate fixtures only did `log_init + pool_create + ASSERT_NE`, not the full Pool+Heap+EvalContext+`context`+`path_init` dance. `tu_setup_pool` (added during the work) is the right primitive for those.

### Future work (not in scope of this PR)

- The remaining ~70 tests with `log_init`-only or trivial fixtures are not worth migrating: savings are 1–2 lines per file.
- Golden-file helpers (P1) and `tu_lambda_str` (P2) are deferred — let demand pull them in.

---

## Track 2 — Build pipeline optimization

The biggest already-implemented win at proposal time was `lambda-input-full` as a shared DLL (69 tests). Status of the remaining items below.

### P0 — Make `lambda-lib` dynamic ✅

- **Before:** [build_lambda_config.json](../build_lambda_config.json) `targets[0]` declared `lambda-lib` with `"link": "static"` and 31 `lib/*.c` sources.
- **First attempt — failed cleanly.** Flipping `"link": "dynamic"` produced `kind "SharedLib"` correctly in Premake but `ld: library 'rpmalloc' not found` at link time. Root cause: `_generate_meta_library` in [utils/generate_premake.py](../utils/generate_premake.py) only emitted `libdirs { /opt/homebrew/lib, /usr/local/lib }` and `links { "rpmalloc", "utf8proc" }` — but on macOS rpmalloc lives at `mac-deps/rpmalloc-install/lib/librpmalloc_no_override.a` and utf8proc at `build_temp/utf8proc/build/libutf8proc.a`, neither in those libdirs.
- **Fix landed.** Patched `_generate_meta_library` to emit `linkoptions{}` with resolved `.a` paths from `self.external_libraries[dep]['lib']` when `link == 'dynamic'`. Static builds keep the old `links{}` form because their symbols are resolved at the final exe link.
- **Also added [lib/file.c](../lib/file.c) to lambda-lib sources** — was missing; only static linking masked the unresolved `create_dir` / `file_copy` references from `file_utils.c`.
- **Outcome.** `liblambda-lib.dylib` (2.1 MB) replaces `liblambda-lib.a` (3.5 MB). 250+ tests across 12 suites verified — all pass.
- **Static-init risk did not materialize.** `rpmalloc`, `memtrack`, `log` all initialize lazily on first use; no test SEGV'd at startup.

### P1 — ccache ✅

- **Finding at start:** ccache was not installed on the development machine; the Makefile gating at [Makefile:58](../Makefile#L58) was inert. Also found a sequencing bug — the ccache wrap was overwritten by the later platform CC/CXX detection block (lines 94-109), so even if ccache had been installed, the wrap would not have applied.
- **Fixed:**
  - Installed ccache 4.13.6 (mac) via `brew install ccache`; added ccache installs to [setup-mac-deps.sh](../setup-mac-deps.sh) and [setup-linux-deps.sh](../setup-linux-deps.sh) ([setup-windows-deps.sh](../setup-windows-deps.sh) already had it).
  - Moved the ccache wrap to AFTER the CC/CXX detection block.
  - Bumped `CCACHE_MAXSIZE` from `500M` → `5G` (matches ccache's own default; 500M was undersized for 147 Debug+ASan TUs).
  - Added `CCACHE_SLOPPINESS=time_macros,include_file_mtime` to avoid false misses on `__DATE__` / `__TIME__` and regenerated files like `parser.c` / `ts-enum.h`.
- **Measured.** Single-TU compile of `test_dir_gtest.cpp`: cold ccache **0.707s**, warm ccache **0.012s** = **60× speedup** on cached builds.

### P1 — Precompiled header for `<gtest/gtest.h>` ❌ SKIPPED

- **Decision basis.** With ccache delivering 60× on warm hits, PCH would only help on first-ever builds (cold ccache) — and even there the parser cost is a fraction of cold compile time.
- **Trade-off.** Premake5+clang PCH on macOS is documented as finicky; the implementation complexity and ongoing maintenance burden weren't justified by the marginal savings ccache doesn't already cover.
- **Revisit trigger.** Only if cold-build wall-clock becomes a CI bottleneck. ccache's own cache eviction would have to consistently happen before the cache pays off.

### P2 — Re-evaluate `TEST_JOBS` cap ⏸ deferred

- Cap exists for ASan RSS headroom ([Makefile:55](../Makefile#L55)). With `lambda-lib` now shared and ccache active, per-job RSS likely drops. Not measured yet.
- **Recommendation:** measure peak RSS with `/usr/bin/time -l` on a full `make build-test` after this PR lands, then decide whether to raise to `3*NPROCS/4`.

### P2 — Lazy linking ⏸ deferred

- Build `.o` always, link `.exe` only when the user runs a specific test. Adds runner complexity. Only worth it after measuring how much link time dominates the rebuild inner loop now that `lambda-lib` is dynamic.

### Explicitly NOT recommended

- **Merging all 147 tests into one exe.** Kills `--gtest_filter` selectivity, breaks isolation across ASan-instrumented suites, and a single crash poisons the whole run.

---

## Track 3 — Dead-code removal in `./test/` ✅

Four files removed (not in build config, not gtest-shaped, stale 6–10 months):

- ~~test/csv_generator.cpp~~ — standalone CSV-generation utility, not a test.
- ~~test/debug_types.cpp~~ — 29-line debug helper.
- ~~test/example_hashmap.cpp~~ — example/reference code.
- ~~test/test_ascii_formatter_standalone.cpp~~ — standalone tool, not gtest-based.

Also removed 28 orphan `.exe` binaries from `./test/` whose `.cpp` sources had already been deleted (cosmetic — gitignored anyway).

### Still pending user review

These three look like disabled experiments but warrant a closer look before deletion:

- [test/test_enhanced_errors.cpp](../test/test_enhanced_errors.cpp)
- [test/test_error_tracking.cpp](../test/test_error_tracking.cpp)
- [test/test_format_validation.cpp](../test/test_format_validation.cpp)

### Out of scope

~83 other sources are not in the build config but have been touched recently. These are likely WIP and should be left alone.

---

## Cumulative outcome

| Metric | Before | After |
|---|---|---|
| Dead test files | 4 dead + 28 orphan exes | All removed |
| Test fixture boilerplate (15 migrated tests) | ~50–100 lines per fixture | 1–5 lines via `tu_setup_pool` / `tu_setup_runtime` |
| ccache | Not installed; wrap shadowed by later CC override | Installed, wrap works, 5 GiB cache, 60× speedup verified |
| Setup scripts | macOS and Linux had no ccache install step | All 3 platforms install ccache |
| `lambda-lib` | Static `.a`, recompiled symbols linked into 147 exes | Shared `.dylib` (2.1 MB), one copy at runtime |
| Migrated tests | 0 | 17 (covering ~475 individual gtest cases) |

### Suggested follow-up order

1. Run `make clean-all && make build-test` once, measure wall-clock + peak RSS. This sets the new baseline.
2. Based on that measurement, decide whether to raise `TEST_JOBS` (Track 2 P2).
3. User reviews and deletes the 3 pending files in Track 3.
4. If a developer hits a duplicated pattern not covered by `test_utils`, extend it then — don't speculate.

Items explicitly closed without action: PCH (Track 2 P1).
