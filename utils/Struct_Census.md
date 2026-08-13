# Data-Structure Census

A libclang-based census of every `struct`, `class`, `union` and `enum` *definition* declared under `lambda/`, `lib/` and `radiant/`, with the compiler's real record layout attached to each one and each record tagged with the static module that owns it.

The point of using clang rather than a regex sweep or `ctags` is that clang answers questions a text scan cannot: it sees through macros and typedefs, resolves the include closure, dedupes a header included 300 times down to one record, and reports the actual **size and alignment** the compiler assigns. `ctags` tells you what is declared and where; clang tells you what it costs.

## Quick start

```bash
make struct-census
```

Incremental by default — only translation units whose dependency set changed since the last run are re-parsed. A no-op run takes about 0.2 s; a cold run over all 526 TUs takes about 40 s on 10 cores.

To ignore the cache and re-parse everything:

```bash
make struct-census ARGS=--full
```

The script is also callable directly, which is how you get the less common flags:

```bash
python3 utils/struct_census.py --config utils/struct_census.config.json -j 10 --top 30
```

| flag | effect |
|---|---|
| `--full` | ignore the cache, re-parse every TU |
| `-j N` | worker processes (default: CPU count) |
| `--top N` | rows in the stdout summary (default 15) |
| `--quiet` | write the reports, print nothing |
| `--config PATH` | use a different config file |

## Modules

Records are grouped by the static-module split, which is normative in `doc/Lambda_Formal_Design.md` **D7.1.1**: four archives plus the executable, layered strictly leftward.

```
lib  →  lambda-data  →  lambda-rt  →  radiant  →  lambda.exe
```

The census reports **six** buckets rather than five, because `lambda-rt` is subdivided into `lmd-rt` and `js-rt`. That subdivision is a reporting refinement only — it does not change D7.1.1, which still packages both as one `liblambda-rt.a`. It exists because the two runtimes have very different data-structure profiles and the split is what makes that visible.

| module | source prefixes |
|---|---|
| `lib` | `lib/**` |
| `lambda-data` | `lambda/core/`, `lambda/io/`, `lambda/input/`, `lambda/format/`, `lambda/network/`, and the mixed-layer roots `lambda/lambda*.h/.hpp` |
| `lmd-rt` | `lambda/runtime/`, `lambda/validator/`, `lambda/jube/`, `lambda/serve/` |
| `js-rt` | `lambda/js/`, `lambda/ts/` |
| `radiant` | `radiant/**` |
| `lambda.exe` | `lambda/main.cpp`, `lambda/main-repl.cpp` |

Mapping is by longest matching path prefix, so a rule can name a directory (`lambda/js`) or a file stem (`lambda/main`). The `*` fallback is deliberately set to `unmapped`: a new source directory shows up as its own bucket in the report rather than being silently absorbed into a neighbour.

Two mappings are worth knowing about because the specification does not settle them:

- **`lambda/lambda-data.hpp`, `lambda.h`, `lambda.hpp`** are today mixed-layer headers, not core headers — `Lambda_Design_Static_Modules.md` §4 evidence item 11 and SM13 both say so. They are counted under `lambda-data` because that is where the split is headed, not because they are clean today.
- **`lambda/serve/`** has no ruling in either `Lambda_Formal_Design.md` or `Lambda_Design_Static_Modules.md`. It embeds every language backend (`backend_js.cpp`, `backend_python.cpp`, `backend_lambda.cpp`, …), which puts it somewhere between `lambda-rt` and the shell. It is counted under `lmd-rt` as a working assumption; change `module_rules` if the split rules it the other way.

## Outputs

Both land in `vibe/meta/ds/` (configurable via `output_dir` / `output_stem`).

### `struct_census.json` — canonical metadata *and* the cache

One record per definition plus the incremental state. This is the file to read programmatically, and the one to delete if you ever want a guaranteed-clean rebuild.

```
version      state-format version
generated    ISO-8601 UTC timestamp of the run
config_sig   cache key over the config file + this script's own source
totals       record count, TU count, counts by kind and by module
records[]    usr, name, kind, module, file, line, size, align, members
files[]      dependency path table (indices used below)
tus{}        per-TU { sig, deps: [file idx], recs: [record idx] }
```

`size` and `align` are `null` for anything clang never laid out (templates, dependent types). `members` is the field count for records and the enumerator count for enums.

### `struct_census.csv` — the flat table

Eight columns, one row per record, sorted by module then file then line so the diff between two runs is readable:

```
module,kind,name,size,align,members,file,line
```

This is also the feed for the DevTool viewer. If only one of the two files is to be committed, commit this one: it is ~200 KB with reviewable diffs, where the JSON is ~3 MB of machine state that churns on every run.

## The viewer: `make devtool`

```bash
make devtool
```

**Struct Census** is the first entry in the DevTool left panel, above the test sections. Selecting it opens the report in the right-hand panel:

- a module rollup strip — one chip per module with its record count, click to pin the table to that module (hover for the module's total record bytes)
- free-text filter across name and file, plus module, kind and minimum-size dropdowns
- click-to-sort on every column; numeric columns sort descending first
- a running total of the bytes in the current selection, which is the fastest way to ask "how much does module X actually weigh"

The **Regen** button re-runs `utils/struct_census.py` incrementally and reloads the table when it finishes. Script output streams to the DevTool terminal panel at the bottom, so a long first run is not a silent one. If no report exists yet, the panel says so and offers the same button.

Implementation: `src/components/StructCensusPanel.jsx` plus the `load-struct-census` / `regen-struct-census` IPC handlers in `main.js`. The panel reads the CSV, never the JSON — the JSON is fifteen times larger and is the census's own cache, not a view model.

## Configuration

`utils/struct_census.config.json`.

| key | meaning |
|---|---|
| `source_dirs` | roots to walk, and the boundary of "ours" — a record declared outside these is ignored even when a scanned TU pulls it in |
| `exclude_dirs` | **plain path prefixes**, not directory names. `lambda/tree-sitter` deliberately covers every `tree-sitter-*` sibling. Add a trailing `/` to pin an entry to exactly one directory |
| `exclude_files` | exact repo-relative paths to skip entirely |
| `objcxx_files` | `.cpp` files that must be parsed as Objective-C++ (the `radiant/*_mac_static.cpp` wrappers `#include` a `.mm`) |
| `tu_extensions` | which files are treated as translation units |
| `kinds` | any subset of `struct`, `class`, `union`, `enum` |
| `module_rules` | path prefix → module name, longest match wins; `"*"` is the fallback |
| `clang.*` | language standards, defines, extra C flags, include dirs, and prefixes to search for libclang |
| `output_dir` / `output_stem` | where the reports are written |

The include dirs mirror the `includedirs` block in `premake5.mac.lua`. There is no `compile_commands.json` in this repo, so they are maintained by hand; if a new third-party include root is added to the build and the census starts reporting parse errors, that is the list to update.

## How incremental works

After parsing a TU, clang hands back the full list of headers it actually opened. The run keeps the repo-local ones and stores a SHA-1 over `(path, size, mtime_ns)` for the TU plus every one of those headers. On the next run each TU's signature is recomputed from its stored dependency list; a mismatch means re-parse, a match means reuse the cached records.

This gets the transitive case right without any extra bookkeeping. Touch `lambda/lambda-data.hpp` and most of the tree goes stale. Touch one leaf `.cpp` and exactly one TU does.

Two things force a full rebuild on their own: editing the config file, and editing `struct_census.py` itself. Both feed `config_sig`, because a change to either can alter what a cached record *means*.

Adding a new `#include` to a file also changes that file's own mtime, so a newly-pulled-in header can never be missed.

## Requirements

libclang plus its Python bindings. Homebrew and apt LLVM both ship them together, and the script prefers a matched pair from `clang.libclang_search` over anything on `sys.path` — a bindings/library version skew fails at the first parse.

```bash
brew install llvm            # macOS
sudo apt install libclang-dev python3-clang   # Debian/Ubuntu
```

libclang is driven through the C API, so nothing computes the builtin-header path the way the `clang` driver does from `argv[0]`. The script passes an explicit `-resource-dir` derived from the LLVM prefix it loaded. Without it every single TU fails on `<stdarg.h>`, and the census silently under-reports by roughly a fifth.

## What the census cannot see

- **Hosted language modules.** `lambda/module/**` (Python, Ruby, Node) and `lambda/bash/**` are excluded by configuration. They are Jube-hosted guests rather than part of the D7.1.1 archive set, and their data structures would swamp the layers the split is actually about.
- **Function-local types.** Parsing runs with `PARSE_SKIP_FUNCTION_BODIES`, which is what makes a cold run 40 s instead of several minutes. A struct declared inside a function body is not counted.
- **Templates.** A class template has no layout until it is instantiated, so `size` and `align` come back `null`.
- **Unreferenced headers.** A header that no `.cpp` or `.c` in `source_dirs` includes is never parsed. In practice this is a small set, but it is not zero.
- **Platform-excluded code.** The census reflects one platform's preprocessor state — the macOS debug configuration. Windows-only and Linux-only branches are invisible from a Mac, which is why `lib/clock_compat.c` sits in `exclude_files`.
- **Vendored trees.** MIR, tree-sitter, SQLite, ThorVG, woff2 and friends are excluded by design; they are not ours to audit.

## Parse errors

The run reports how many TUs produced hard errors, and their records are flagged as possibly incomplete rather than silently dropped. The tree currently reports **zero** across all 526 TUs. A number appearing here is a signal about the tree or the config, not noise to tune out.

One known break sits outside the census's scope: `lambda/bash/bash_runtime.cpp` redefines the enumerators of `BashTestOp` (declared in the `bash_ast.hpp` it includes) under a second name, `BashTestComparison`, so the file does not compile. It stopped showing up here when `lambda/bash` was excluded, and nothing in `lambda/bash/` is currently built, so the breakage is latent rather than live.
