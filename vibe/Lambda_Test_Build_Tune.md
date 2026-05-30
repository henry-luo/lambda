# Lambda Test Build Tune

Proposal for reducing `make build-test` wall-clock and trimming dead weight from `./test/`.

Snapshot of the current state:

- ~120 `*.cpp` test sources, 147 test executables defined in [build_lambda_config.json](../build_lambda_config.json).
- `make build-test` runs `make -C build/premake config=debug_native -j$(TEST_JOBS)` where `TEST_JOBS = (NPROCS+1)/2` ([Makefile:55](../Makefile#L55)) — intentionally halved for Debug+ASan memory pressure.
- `lambda-input-full` is already a dynamic library (`"link": "dynamic"`), shared by 69 test entries. `lambda-lib` is still **static**, with 31 sources.

The proposal has three independent tracks. Each can land on its own; they compound.

---

## Track 1 — Shared test utilities (`lib/test_utils.{h,cpp}`)

A survey of the ~120 test `*.cpp` files found heavily duplicated scaffolding. Two helper files already exist and should be reused, not duplicated:

- [test/lib/test_lambda_helpers.hpp](../test/lib/test_lambda_helpers.hpp) — Lambda-specific harness (used by ~5 tests).
- [test/input_roundtrip_helpers.{cpp,hpp}](../test/input_roundtrip_helpers.cpp) — roundtrip comparison helpers (used only by roundtrip tests).

Anything not covered there should land in a new `lib/test_utils.{h,cpp}`. The header exposes a C-style API; the implementation is C++ because several helpers need `String*`, `Pool*`, `Heap*` from the runtime.

### Helpers to extract, by priority

| P | Helper group | Proposed API | Today's situation | Files benefiting |
|---|---|---|---|---|
| P0 | Runtime fixture setup/teardown | `tu_init_runtime(...)` / `tu_cleanup_runtime(...)` | 50–100-line `SetUp`/`TearDown` duplicated in [test_dir_gtest.cpp:40](../test/test_dir_gtest.cpp), [test_http_gtest.cpp:15](../test/test_http_gtest.cpp), [test_enhanced_errors.cpp:17](../test/test_enhanced_errors.cpp), … | ~90 |
| P0 | Temp dir/file under `./temp/` | `tu_mkdtemp(name)`, `tu_write_temp(content, ext)`, `tu_rmtree(path)` | Ad-hoc `system("mkdir -p")` + `rm -rf`; several callers still use `/tmp` (violates CLAUDE.md rule 2) | ~30 |
| P0 | Cross-platform process spawn | `tu_run(cmd, &exit_code)`, `tu_run_lambda(args)` | Hand-rolled popen + fgets/realloc + `#ifdef WIN32` in [test_js_gtest.cpp:41](../test/test_js_gtest.cpp), [test_bash_official_gtest.cpp:501](../test/test_bash_official_gtest.cpp), [test_html_roundtrip_gtest.cpp:65](../test/test_html_roundtrip_gtest.cpp) | ~20 |
| P1 | Golden-file comparison | `tu_read_golden(path)`, `tu_assert_matches_golden(actual, path, opts)` | Each file rolls its own with subtly different normalization | ~25 |
| P1 | String normalization | `tu_trim_trailing(s)`, `tu_collapse_ws(s)`, `tu_strip_timing_lines(s)` | Duplicated in [test_input_roundtrip_gtest.cpp:119](../test/test_input_roundtrip_gtest.cpp), [test_bash_official_gtest.cpp:110](../test/test_bash_official_gtest.cpp), [test_js_gtest.cpp:100](../test/test_js_gtest.cpp); partial copy in `test_lambda_helpers.hpp` | ~15 |
| P2 | File I/O | `tu_slurp(path)`, `tu_exists(path)` | `fseek/ftell/fread` boilerplate scattered everywhere | ~20 |
| P2 | Lambda string ctor | `tu_lambda_str(const char*) -> String*` | format/input tests reimplement this | ~10 |

### Rollout

1. Land `lib/test_utils.{h,cpp}` with the P0 group only.
2. Migrate 5–10 of the highest-LOC fixtures (e.g. [test_dir_gtest.cpp](../test/test_dir_gtest.cpp), [test_http_gtest.cpp](../test/test_http_gtest.cpp), [test_enhanced_errors.cpp](../test/test_enhanced_errors.cpp), [test_bash_official_gtest.cpp](../test/test_bash_official_gtest.cpp), [test_html_roundtrip_gtest.cpp](../test/test_html_roundtrip_gtest.cpp)) to validate the API.
3. Mass-migrate the rest in batches; each batch is one PR keeping CI green.
4. Add P1/P2 helpers once P0 has stabilized.

### Estimated impact

~500–700 LoC removed across 90+ test files; enforces `./temp/` rule by construction; reduces per-test SetUp from ~100 lines to ~5.

---

## Track 2 — Build pipeline optimization

The biggest already-implemented win is `lambda-input-full` as a shared DLL, which 69 tests already link against. The remaining work, in order of impact:

### P0 — Make `lambda-lib` dynamic

- **Today:** [build_lambda_config.json](../build_lambda_config.json) `targets[0]` declares `lambda-lib` with `"link": "static"` and 31 `lib/*.c` sources (`mempool.c`, `memtrack.c`, `hashmap.c`, `str.c`, …). 31 test entries declare `dependencies: ["lambda-lib"]`, but lambda-input-full also rolls in lib code, so the effective recompile surface is larger.
- **Change:** Flip to `"link": "dynamic"`. The lib is built once as `liblambda-lib.{dylib,so,dll}` and linked into every test exe in milliseconds.
- **Risk:** Static-init order. `rpmalloc`, `memtrack`, `log` all hold global state. Before mass-rolling, build one small test (e.g. `test_hash_gtest`), run it, and confirm clean startup. If any test SEGVs in init, the fix is usually to call the corresponding `_init()` from a `__attribute__((constructor))` in the shared lib itself.
- **Expected savings:** Largest single contributor remaining. Final number depends on ccache hit rate (see below) but is the biggest lever on the table.

### P1 — Precompiled header for `<gtest/gtest.h>`

- Every one of the 147 TUs reparses gtest + ~100 STL headers. A single PCH typically saves 50–150 ms per TU.
- **Change:** Add `pchheader` / `pchsource` in [utils/generate_premake.py](../utils/generate_premake.py) so the Premake gmake action emits a shared `gtest.pch` and `-include`s it into each test TU.
- **Caveat:** Premake5 + clang PCH support on macOS works but is finicky; gate behind a feature flag for the first PR.

### P1 — Confirm ccache is actually hitting

- ccache is wired in at [Makefile:58](../Makefile#L58), but no one has measured hit rate.
- **Change:** Run `ccache -s` before and after a full `make clean-all && make build-test`. If hit rate is < 70 %:
  - Bump `CCACHE_MAXSIZE` (likely 5 GB is small for 147 Debug+ASan TUs).
  - Set `CCACHE_SLOPPINESS=time_macros,include_file_mtime`.
  - Verify `-Werror=date-time` is not set (defeats caching).
- Order this **before** Track 2 P0 so the baseline measurement is honest.

### P2 — Re-evaluate `TEST_JOBS` cap

- Cap exists for ASan RSS headroom ([Makefile:55](../Makefile#L55)). Once `lambda-lib` is shared and PCH is in, per-job RSS drops noticeably.
- **Change:** Measure peak RSS with `/usr/bin/time -l` on a representative build. If headroom is comfortable, raise to `3*NPROCS/4`.

### P2 — Lazy linking

- Build `.o` always, link `.exe` only when the user runs a specific test.
- Saves ~5–10 % of total wall-clock spent in the link stage when iterating on headers.
- Adds runner complexity (test discovery must handle missing exes). Only worth it after P0/P1 are in.

### Explicitly NOT recommended

- **Merging all 147 tests into one exe.** Kills `--gtest_filter` selectivity, breaks isolation across ASan-instrumented suites, and a single crash poisons the whole run.

### Suggested order

1. `ccache -s` baseline.
2. Track 2 P0 (`lambda-lib` → dynamic). Re-measure.
3. Track 2 P1 (PCH for gtest). Re-measure.
4. Decide on `TEST_JOBS` raise and lazy linking based on what's left.

---

## Track 3 — Dead-code removal in `./test/`

Four files are safe to remove. They are not referenced in [build_lambda_config.json](../build_lambda_config.json), are not gtest-shaped, and have been stale for 6–10 months:

- [test/csv_generator.cpp](../test/csv_generator.cpp) — standalone CSV-generation utility, not a test.
- [test/debug_types.cpp](../test/debug_types.cpp) — 29-line debug helper.
- [test/example_hashmap.cpp](../test/example_hashmap.cpp) — example/reference code.
- [test/test_ascii_formatter_standalone.cpp](../test/test_ascii_formatter_standalone.cpp) — standalone tool, not gtest-based.

### Pending user review

These three look like disabled experiments but warrant a closer look before deletion:

- [test/test_enhanced_errors.cpp](../test/test_enhanced_errors.cpp)
- [test/test_error_tracking.cpp](../test/test_error_tracking.cpp)
- [test/test_format_validation.cpp](../test/test_format_validation.cpp)

### Out of scope

~83 other sources are not in the build config but have been touched recently. These are likely WIP and should be left alone.
