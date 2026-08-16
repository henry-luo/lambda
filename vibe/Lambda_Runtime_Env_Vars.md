# Lambda Runtime Environment Variables Audit

Audited: 2026-07-26; per-variable tables and classification sets
re-verified against source 2026-08-16.  Scope is production Lambda, LambdaJS, Jube, the
embedded Bash/Python hosts, and Radiant.  The inventory comes from direct
`getenv()` / `shell_getenv()` reads in `lambda/`, `radiant/`, and the
supporting `lib/` code.  Generated parsers, vendored dependencies, and
test-only switches are deliberately excluded from the main tables.

## Profile meaning

| Profile | Build configuration | Meaning |
|---|---|---|
| debug | `debug_native` | `DEBUG`, symbols, `-Og`, frame pointers, and AddressSanitizer for the main executable. |
| debug_profile | `debug_profile_native` | `DEBUG`, symbols, optimized code, frame pointers, and `LAMBDA_JS_EXEC_PROFILE`. |
| release | `release_native` | `NDEBUG`, `LAMBDA_HOME_RELEASE`, `-O3`, ThinLTO, section stripping, and native CPU tuning. |
| release_profile | `release_profile_native` | The release profile plus `LAMBDA_JS_EXEC_PROFILE`. |

The generator is authoritative: `utils/generate_premake.py`; `make debug`,
`make build-debug-profile`, `make build-release`, and
`make build-release-profile` select the four configurations.  Release builds
compile `log_debug()` and `log_info()` out, so log-only diagnostics are not
functional there.

In the tables, **✓** means the variable can affect that build when the relevant
runtime path is exercised; **✗** means the code is compiled out.  A check is
not a statement that the feature is enabled by default.  Unless a row says
otherwise, boolean diagnostics require a non-empty value other than `0`;
several optimization controls are enabled by default and use `=0` to disable.

## Core runtime, logging, and MIR

| Variable | Effect / accepted value | Debug | Debug profile | Release | Release profile |
|---|---|:---:|:---:|:---:|:---:|
| `LAMBDA_HOME` | Overrides runtime asset directory. Defaults are `./lambda` for debug and `./lmd` for release, with fallback to the other layout. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_PROFILE` | `1` or `true`: write Lambda compilation-phase timing to `temp/phase_profile.txt`. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_DISABLE_MIR_CACHE` | `1` or `true`: disable retained Lambda MIR-import cache. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_MIR_DUMP_PATH` | Write finalized MIR to this path. `--no-log` and disabled default log category suppress it. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_MIR_LOG_FRAME_SLOTS` | Emit MIR frame-slot telemetry; also gated by normal logging / `--no-log`. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_C2MIR_DEBUG` | `1` or `true`: capture legacy C2MIR debug messages. It is behind the separate `LAMBDA_C2MIR` compile flag, which none of these four profiles defines. | ✗ | ✗ | ✗ | ✗ |
| `LAMBDA_LOG_LEVEL` | Overrides configured log severity. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_LOG_FILE` | Overrides `log.txt` destination for file logging. | ✓ | ✓ | ✓ | ✓ |

## GC, allocation, and execution diagnostics

| Variable | Effect / accepted value | Debug | Debug profile | Release | Release profile |
|---|---|:---:|:---:|:---:|:---:|
| `LAMBDA_GC_FORCE_EVERY` | Positive allocation interval: force a collection every N public GC allocations. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_GC_FORCE_SEED` | Seed for deterministic randomized forced collection; must accompany `LAMBDA_GC_FORCE_ONE_IN`. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_GC_FORCE_ONE_IN` | Positive denominator for randomized forced collection; must accompany the seed. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_GC_POISON_FREED` | `1`: poison freed GC memory; `0`: off. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_GC_STATS` | Dump GC tuning counters at heap teardown. | ✓ | ✓ | ✓ | ✓ |
| `MEMTRACK_MODE` | `DEBUG` enables debug memory tracking; other/unset values use stats mode. | ✓ | ✓ | ✓ | ✓ |
| `VIEW_MEM_STAGES` | Emit process-memory checkpoints. | ✓ | ✓ | ✓ | ✓ |
| `VIEW_MEM_STATS` | `1`: print memory categories before CLI exit. | ✓ | ✓ | ✓ | ✓ |
| `VIEW_PAUSE_BEFORE_EXIT` | Positive integer: wait that many seconds before exit for external inspection. | ✓ | ✓ | ✓ | ✓ |
| `POOL_TRACE` | Enable memory-pool trace diagnostics. | ✓ | ✓ | ✗ | ✗ |
| `POOL_STATS` | Enable memory-pool statistics diagnostics. | ✓ | ✓ | ✓ | ✓ |
| `COW_EXEC_PROFILE` | Enable copy-on-write execution counter profile. | ✓ | ✓ | ✓ | ✓ |
| `COW_EXEC_PROFILE_OUT` | Destination for the COW TSV; default `temp/cow_exec_profile.tsv`. | ✓ | ✓ | ✓ | ✓ |

`POOL_TRACE` is marked unavailable in release because it only produces
`log_debug()` output.  `LAMBDA_GC_STATS` deliberately uses a notice-level log,
so it remains useful under `NDEBUG`.

## Lambda/LambdaJS compilation, optimization, and diagnostics

| Variable | Effect / accepted value | Debug | Debug profile | Release | Release profile |
|---|---|:---:|:---:|:---:|:---:|
| `LAMBDA_JS_LARGE_INTERP` | Default on; `0`/`false` disables automatic large-module/document MIR interpretation for Lambda and LambdaJS. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_JS_LARGE_INTERP_BYTES` | Positive source-size threshold for automatic O0 interpretation; default 15000 bytes. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_DISABLE_JS_MIR_CACHE` | Presence disables Radiant batch JS-MIR cache. | ✓ | ✓ | ✓ | ✓ |
| `JS_MIR_INTERP` | `1`/`true`: force the MIR interpreter path for Lambda and LambdaJS. | ✓ | ✓ | ✓ | ✓ |
| `JS_LAZY_MIR` | Non-zero: select per-function lazy MIR code generation. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_JS_CONST_FOLD` | Default on; `0` disables JS MIR constant folding. | ✓ | ✓ | ✓ | ✓ |
| `JS_TRANSPILE_TIMING` | Non-zero: print parse/AST/import/MIR/link/execute timing and AST counters. | ✓ | ✓ | ✓ | ✓ |

## LambdaJS counters and executable profiling

| Variable | Effect / accepted value | Debug | Debug profile | Release | Release profile |
|---|---|:---:|:---:|:---:|:---:|
| `JS_OPT_TRACE` | Truthy: record optimization hit/miss/fallback counters and dump them at exit. Requires `LAMBDA_JS_EXEC_PROFILE` at compile time. | ✓ | ✓ | ✗ | ✓ |
| `JS_OPT_TRACE_OUT` | Output TSV for `JS_OPT_TRACE`; defaults to `temp/js_opt_trace_<pid>.tsv`. | ✓ | ✓ | ✗ | ✓ |

`JS_OPT_TRACE` is the only surviving member of this group; the ten
`JS_EXEC_PROFILE*` / `LAMBDA_JS_*_STATS*` counters that this audit previously
listed were removed from the runtime along with the per-helper profiler.
`test/benchmark/run_standard_benchmarks.py` detects an instrumented binary by
scanning `strings` for opt-trace event names, which exist only under
`LAMBDA_JS_EXEC_PROFILE` — not for `JS_OPT_TRACE` itself, whose literal is
present in every build because `lambda/main.cpp` reads it with an ungated
`getenv()`.

## Node-compatible runtime and process integration

| Variable | Effect / accepted value | Debug | Debug profile | Release | Release profile |
|---|---|:---:|:---:|:---:|:---:|
| `NODE_COMPILE_CACHE` | Enables Node-compatible compile-cache directory. | ✓ | ✓ | ✓ | ✓ |
| `NODE_DISABLE_COMPILE_CACHE` | Non-zero disables Node-compatible compile cache. | ✓ | ✓ | ✓ | ✓ |
| `NODE_DEBUG_NATIVE` | Include `COMPILE_CACHE` to report compile-cache activity to stderr. | ✓ | ✓ | ✓ | ✓ |
| `NODE_DEBUG` | Controls Node-compatible `util.debuglog()` namespaces. | ✓ | ✓ | ✓ | ✓ |
| `NODE_EXTRA_CA_CERTS` | File containing additional TLS CA certificates. | ✓ | ✓ | ✓ | ✓ |
| `NODE_UNIQUE_ID` | Marks a cluster worker and supplies its worker id. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_JS_IPC` | Internal child-process IPC marker; normally set/cleared by the runtime. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_JS_IPC_FD` | Internal child-process IPC file descriptor. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_JS_IPC_REF` | Internal IPC reference state for cluster/child processes. | ✓ | ✓ | ✓ | ✓ |
| `PATH` | Used for Node/Bash command lookup and temporary `node_modules/.bin` augmentation. | ✓ | ✓ | ✓ | ✓ |

## Jube and hosted-language runtime

| Variable | Effect / accepted value | Debug | Debug profile | Release | Release profile |
|---|---|:---:|:---:|:---:|:---:|
| `JUBE_MODULE_PATH` | Path-list of module roots searched before/alongside installed module roots. | ✓ | ✓ | ✓ | ✓ |
| `JUBE_DYNAMIC_MODULE` | Dynamic module library path to load at startup. | ✓ | ✓ | ✓ | ✓ |
| `JUBE_DYNAMIC_ENTRY` | Optional dynamic-module entry symbol accompanying `JUBE_DYNAMIC_MODULE`. | ✓ | ✓ | ✓ | ✓ |
| `JUBE_HOSTOBJ_DEMO_DYNAMIC_ONLY` | Non-zero: suppress static host-object demo so its dynamic version can register. Development/test integration switch. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_PYTHON` | Python executable for the serve backend. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_SHELL` | Shell executable for the serve Bash backend. | ✓ | ✓ | ✓ | ✓ |
| `HOME` | Lambda npm cache, fonts, Bash and JS home-directory support. | ✓ | ✓ | ✓ | ✓ |
| `XDG_DATA_HOME` | Linux font-discovery location. | ✓ | ✓ | ✓ | ✓ |
| `PWD` | Embedded Bash current-directory state. | ✓ | ✓ | ✓ | ✓ |
| `OLDPWD` | Embedded Bash previous-directory state. | ✓ | ✓ | ✓ | ✓ |
| `BASH_COMMAND` | Set internally when Lambda dispatches inline Bash. | ✓ | ✓ | ✓ | ✓ |

## Radiant, rendering, and document JavaScript

| Variable | Effect / accepted value | Debug | Debug profile | Release | Release profile |
|---|---|:---:|:---:|:---:|:---:|
| `LAMBDA_JS_EXEC_TIMEOUT_SECONDS` | Positive document-script timeout override. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_JS_SOURCE_CACHE` | Default on; `0` disables external script-source cache. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_JS_SOURCE_CACHE_BYTES` | Positive source-cache byte limit. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_JS_PRELAYOUT_DEFER_BYTES` | Positive pre-layout script deferral threshold. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_JS_EXTERNAL_SCRIPT_BYTES` | Positive external-script byte limit. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_JS_TOTAL_SCRIPT_BYTES` | Positive total document-script byte limit. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_JS_LOAD_BLOCK_TIMEOUT_MS` | Positive block-load timeout. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_JS_TASK_DIAGNOSTICS` | Non-zero task diagnostic logging. | ✓ | ✓ | ✗ | ✗ |
| `RADIANT_JS_TASK_TIMING` | Non-zero task timing logging. | ✓ | ✓ | ✗ | ✗ |
| `RADIANT_SCRIPT_BEFORE_CASCADE` | Non-zero: legacy script-before-CSS-cascade order. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_LAYOUT_RESOURCE_TIMEOUT_MS` | Positive layout resource wait timeout. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_RENDER_THREADS` | Positive renderer worker count. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_TILE_STRIP_H` | Positive raster tile-strip height. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_TILE_THRESHOLD` | Positive PNG tile threshold in bytes. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_VERIFY_INCREMENTAL_LAYOUT` | `1`: run incremental-layout verification. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_UPDATE_STATE_DUMPS` | `1`/`true`/`yes`: update event-simulation state dumps. | ✓ | ✓ | ✓ | ✓ |
| `LAYOUT_DEBUG` | Comma/semicolon/space-separated layout debug categories. | ✓ | ✓ | ✓ | ✓ |
| `LAYOUT_PROFILE` | Non-zero layout profiling. | ✓ | ✓ | ✓ | ✓ |
| `RADIANT_TRACE_RENDER` | Render trace logging. | ✓ | ✓ | ✗ | ✗ |
| `RADIANT_TRACE_TEXT` | Text-render trace logging. | ✓ | ✓ | ✗ | ✗ |
| `RADIANT_TRACE_FONT` | Font metric trace logging. | ✓ | ✓ | ✗ | ✗ |
| `RENDER_BATCH_MEM_REPORT` | Positive batch interval for memory reports. | ✓ | ✓ | ✓ | ✓ |
| `RENDER_BATCH_RSS_REPORT` | Non-zero: per-job RSS reports. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_IMAGE_DECODE_TRACE` | Non-empty image-decode trace logging (notice level, retained in release). | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_AUTO_CLOSE` | Presence auto-closes Radiant window after first frame / enables batch close. | ✓ | ✓ | ✓ | ✓ |
| `LAMBDA_HEADLESS_GLFW_WINDOW` | Presence restores a hidden GLFW window in headless mode. | ✓ | ✓ | ✓ | ✓ |

## OS compatibility variables

| Variable | Effect / accepted value | Debug | Debug profile | Release | Release profile |
|---|---|:---:|:---:|:---:|:---:|
| `TMPDIR` | Native/JS temporary-directory selection and Node compile-cache default. | ✓ | ✓ | ✓ | ✓ |
| `TMP` | Fallback temporary-directory selection. | ✓ | ✓ | ✓ | ✓ |
| `TEMP` | Fallback temporary-directory selection, primarily Windows. | ✓ | ✓ | ✓ | ✓ |
| `USERPROFILE` | Windows home directory for shell/JS OS support. | ✓ | ✓ | ✓ | ✓ |
| `HOMEDRIVE` | Windows fallback home directory for shell support. | ✓ | ✓ | ✓ | ✓ |
| `USERNAME` | Windows JS OS user information. | ✓ | ✓ | ✓ | ✓ |
| `LANG` | REPL locale selection. | ✓ | ✓ | ✓ | ✓ |
| `LC_ALL` | Overrides `LANG` for REPL locale selection. | ✓ | ✓ | ✓ | ✓ |

## Deliberately excluded names

LambdaJS exposes `process.env`; the embedded Python `os.getenv()` and Lambda
system-information accessors can also read arbitrary process environment names.
Those APIs do not define named host configuration switches, so a finite table
cannot enumerate them.  The variables above are the named reads that alter
native runtime behavior.

Test harnesses have additional, non-product switches such as
`LAMBDA_USE_C2MIR`, `LAMBDA_NODE_BASELINE_ONLY`, `LAMBDA_JS_PHASE_TIMING`,
`NODE_TEST_WORKER_ID`, `LAMBDA_RADIANT_VIEW_TEST_JOBS`,
`LAMBDA_UI_TEST_JOBS`, `LAMBDA_RADIANT_ONLINE_VIEW_TIMEOUT`,
`WPT_FORM_UPDATE_BASELINE`, and `LAMBDA_WPT_JOBS`.  They should not be used as
runtime configuration.  The standalone `lib/hashmap.c` benchmark also accepts
`SEED` and `N`; it is not part of the Lambda or LambdaJS runtime.

## Cleanup review and proposed profile policy

The matrix above describes the current implementation. It is too permissive:
the normal release binary currently recognizes 94 of the 109 audited names.
Many of those names are developer escape hatches, differential-test controls,
or counters whose branches and string literals should not exist in a production
binary.

The recommended policy has four classes:

1. **Runtime (30):** documented public configuration or environment values
   required to implement a public runtime feature. These remain in all builds.
2. **Debug/test (34):** diagnostics, fault injection, validation, trace output,
   legacy ordering, and test automation. Compile only in `debug` and
   `debug_profile`.
3. **Profiling (15):** timers, counters, reports, and output destinations.
   Compile only in `debug_profile` and `release_profile`.
4. **Experimental/A-B (11):** temporary optimization and resource-strategy
   controls used for differential correctness tests and performance A/B runs.
   Keep in `debug`, `debug_profile`, and `release_profile` during migration,
   but compile them out of `release`. Retire each switch after its optimized
   path becomes unconditional.

`LAMBDA_C2MIR_DEBUG` is outside all four classes: none of the four builds
defines `LAMBDA_C2MIR`, and the legacy C2MIR path is frozen. Remove the
environment hook rather than carrying it into a new profile.

### Recommended counts

| Profile | Truly needed after cleanup | Composition |
|---|---:|---|
| debug | **69** | 30 runtime + 28 debug/test + 11 experimental |
| debug_profile | **84** | 30 runtime + 28 debug/test + 15 profiling + 11 experimental |
| release | **30** | 30 runtime only |
| release_profile | **56** | 30 runtime + 15 profiling + 11 experimental |

The "Truly needed" column is derived from the four classification sets below,
each of which has been re-verified against the current source. The original
"Current recognized" per-profile column has been dropped: it came from the
2026-07-26 sweep and cannot be re-derived without repeating the per-site
compiled-out analysis, so leaving stale numbers beside verified ones would be
worse than omitting them. Re-add it with a fresh audit.

The profiling-only rule removes 15 profiler variables from both non-profile
builds; `JS_OPT_TRACE*` already obey it, the other 13 do not yet.

### Release/runtime allowlist (30)

These variables are needed by public runtime behavior or an internal
cross-process protocol that implements public behavior:

| Group | Variables |
|---|---|
| Lambda configuration and logging (3) | `LAMBDA_HOME`, `LAMBDA_LOG_LEVEL`, `LAMBDA_LOG_FILE` |
| Node-compatible runtime and IPC (9) | `NODE_COMPILE_CACHE`, `NODE_DISABLE_COMPILE_CACHE`, `NODE_DEBUG`, `NODE_EXTRA_CA_CERTS`, `NODE_UNIQUE_ID`, `LAMBDA_JS_IPC`, `LAMBDA_JS_IPC_FD`, `LAMBDA_JS_IPC_REF`, `PATH` |
| Hosted runtimes and module lookup (8) | `JUBE_MODULE_PATH`, `LAMBDA_PYTHON`, `LAMBDA_SHELL`, `HOME`, `XDG_DATA_HOME`, `PWD`, `OLDPWD`, `BASH_COMMAND` |
| Document/runtime controls (2) | `LAMBDA_JS_EXEC_TIMEOUT_SECONDS`, `RADIANT_RENDER_THREADS` |
| OS compatibility (8) | `TMPDIR`, `TMP`, `TEMP`, `USERPROFILE`, `HOMEDRIVE`, `USERNAME`, `LANG`, `LC_ALL` |

Two allowlist entries need public-contract follow-up:
`LAMBDA_JS_EXEC_TIMEOUT_SECONDS` and `RADIANT_RENDER_THREADS` should be
documented in user-facing CLI/runtime documentation. If they are not intended
as supported configuration, replace them with CLI/config-file options or fixed
policy and remove their environment reads from release too.

`NODE_UNIQUE_ID` and the three `LAMBDA_JS_IPC*` names are not user knobs. They
remain because parent/child process and Node cluster behavior requires the
environment as a transport. They should be described as reserved internal
protocol variables.

### Debug/test-only set (28)

| Group | Variables |
|---|---|
| MIR diagnostics (2) | `LAMBDA_MIR_DUMP_PATH`, `LAMBDA_MIR_LOG_FRAME_SLOTS` |
| GC and allocation fault diagnosis (6) | `LAMBDA_GC_FORCE_EVERY`, `LAMBDA_GC_FORCE_SEED`, `LAMBDA_GC_FORCE_ONE_IN`, `LAMBDA_GC_POISON_FREED`, `VIEW_PAUSE_BEFORE_EXIT`, `POOL_TRACE` |
| Node/Jube development (4) | `NODE_DEBUG_NATIVE`, `JUBE_DYNAMIC_MODULE`, `JUBE_DYNAMIC_ENTRY`, `JUBE_HOSTOBJ_DEMO_DYNAMIC_ONLY` |
| Radiant diagnostics and test control (16) | `RADIANT_JS_PRELAYOUT_DEFER_BYTES`, `RADIANT_JS_EXTERNAL_SCRIPT_BYTES`, `RADIANT_JS_TOTAL_SCRIPT_BYTES`, `RADIANT_JS_LOAD_BLOCK_TIMEOUT_MS`, `RADIANT_JS_TASK_DIAGNOSTICS`, `RADIANT_SCRIPT_BEFORE_CASCADE`, `RADIANT_LAYOUT_RESOURCE_TIMEOUT_MS`, `RADIANT_VERIFY_INCREMENTAL_LAYOUT`, `RADIANT_UPDATE_STATE_DUMPS`, `LAYOUT_DEBUG`, `RADIANT_TRACE_RENDER`, `RADIANT_TRACE_TEXT`, `RADIANT_TRACE_FONT`, `LAMBDA_IMAGE_DECODE_TRACE`, `LAMBDA_AUTO_CLOSE`, `LAMBDA_HEADLESS_GLFW_WINDOW` |

The direct `JUBE_DYNAMIC_MODULE` injection path is classified as development
because production dynamic modules should be resolved through the manifest and
`JUBE_MODULE_PATH` contract. This also removes an unmanifested library-loading
surface from release.

### Profiling-only set (15)

| Group | Variables |
|---|---|
| Lambda/MIR phase measurement (2) | `LAMBDA_PROFILE`, `LAMBDA_DISABLE_MIR_CACHE` |
| Memory and execution measurement (6) | `LAMBDA_GC_STATS`, `VIEW_MEM_STAGES`, `VIEW_MEM_STATS`, `POOL_STATS`, `COW_EXEC_PROFILE`, `COW_EXEC_PROFILE_OUT` |
| JS phase/call measurement (1) | `JS_TRANSPILE_TIMING` |
| JS counter reports (2) | `JS_OPT_TRACE`, `JS_OPT_TRACE_OUT` |
| Radiant measurement (4) | `RADIANT_JS_TASK_TIMING`, `LAYOUT_PROFILE`, `RENDER_BATCH_MEM_REPORT`, `RENDER_BATCH_RSS_REPORT` |

`LAMBDA_DISABLE_MIR_CACHE` is included here because its documented purpose is
controlled timing comparison and emergency performance bisection, not
application semantics.

### Experimental/A-B set (11)

| Group | Variables |
|---|---|
| Memory instrumentation mode (1) | `MEMTRACK_MODE` |
| JS execution and optimization gates (6) | `LAMBDA_JS_LARGE_INTERP`, `LAMBDA_JS_LARGE_INTERP_BYTES`, `LAMBDA_DISABLE_JS_MIR_CACHE`, `JS_MIR_INTERP`, `JS_LAZY_MIR`, `LAMBDA_JS_CONST_FOLD` |
| Radiant cache/render tuning (4) | `RADIANT_JS_SOURCE_CACHE`, `RADIANT_JS_SOURCE_CACHE_BYTES`, `RADIANT_TILE_STRIP_H`, `RADIANT_TILE_THRESHOLD` |

These should not become a permanent configuration API. After differential
tests establish the invariant, delete the variable and generic fallback rather
than leaving a dormant branch. If an execution strategy must remain
user-selectable, promote it to a documented CLI/config option and move it to
the runtime allowlist.

### Build-system enforcement

Add two explicit generated-build defines rather than scattering assumptions
about `DEBUG` and `NDEBUG`:

```c
// debug and debug_profile
#define LAMBDA_DEBUG_TOOLS 1

// debug_profile and release_profile
#define LAMBDA_PROFILE_TOOLS 1
```

`debug_profile` receives both. `release` receives neither. The existing
`LAMBDA_JS_EXEC_PROFILE` can become an implementation detail selected by
`LAMBDA_PROFILE_TOOLS`, or be retained as a JS-specific sub-feature.

Wrap the complete implementation, including the environment-name literal,
counter storage, branches, report writers, and registration hooks:

```c
#if defined(LAMBDA_PROFILE_TOOLS)
static bool profile_enabled(void) {
    return runtime_env_truthy("SOME_PROFILE_VARIABLE");
}
#endif
```

Do not leave an always-compiled `getenv()` followed by a no-op in release. The
goal is zero lookup overhead, zero counter/storage overhead, and no diagnostic
variable strings in `lambda_release.exe`.

Experimental controls use:

```c
#if defined(LAMBDA_DEBUG_TOOLS) || defined(LAMBDA_PROFILE_TOOLS)
// temporary differential/A-B gate
#endif
```

This preserves debug correctness bisection and optimized release profiling
without exposing the switches in the normal release binary.

### Cleanup sequence

1. Add `LAMBDA_DEBUG_TOOLS` and `LAMBDA_PROFILE_TOOLS` in
   `build_lambda_config.json` generation logic; regenerate Premake files with
   the normal generator.
2. Compile the 25 profiler variables and all associated state only under
   `LAMBDA_PROFILE_TOOLS`.
3. Compile the 34 debug/test variables only under `LAMBDA_DEBUG_TOOLS`.
4. Compile the 19 temporary A/B controls under either tools define, and open a
   retirement issue for every default-on optimization escape hatch.
5. Delete `LAMBDA_C2MIR_DEBUG` from the current runtime surface.
6. Publish the 30-name release allowlist, documenting the two provisional
   Radiant controls and reserving the IPC names.
7. Add a release-binary audit that fails if a non-allowlisted environment name
   is present.

Suggested release gate:

```sh
make build-release

# Must print no debug/test/profile variable names.
strings lambda_release.exe |
  rg 'LAMBDA_GC_|JS_MIR_|JS_EXEC_PROFILE|COW_EXEC_PROFILE|VIEW_MEM_|POOL_(TRACE|STATS)|LAYOUT_(DEBUG|PROFILE)' &&
  exit 1 || true
```

The production gate should use a checked-in exact denylist generated from the
classification above, not only the illustrative regular expression. Also
compile and smoke-test all four profiles so an accidentally over-broad guard
cannot leave profile-only code unbuilt.

## Audit commands

```sh
# Regenerate the derived Premake file, then inspect the resolved build flags.
python3 utils/generate_premake.py --output premake5.mac.lua
rg -n 'configurations:|LAMBDA_JS_EXEC_PROFILE|LAMBDA_HOME_RELEASE|sanitize' premake5.mac.lua

# Rebuild each behaviorally distinct profile.
make debug
make build-debug-profile
make build-release
make build-release-profile

# Re-inventory named reads after a runtime change.
rg -n '\\b(getenv|shell_getenv)\\s*\\(' lambda radiant lib \
  --glob '*.{c,cc,cpp,cxx,h,hpp}' --glob '!**/tree-sitter*/**' --glob '!lib/sqlite/**'
```

Do not use a debug/ASan binary for performance work.  Use `release` for
throughput/latency and `release_profile` only when JS execution instrumentation
is required.
