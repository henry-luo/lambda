# Bash Transpiler — GNU Official Test Suite Integration

## Overview

This document covers the integration of the **GNU Bash official test suite** into Lambda's CI as a GTest-based regression harness. The official test suite (`ref/bash/tests/`) provides 82 paired test cases (`*.tests` + `*.right`) covering virtually every Bash feature area, serving as a comprehensive conformance target for Lambda's Bash transpiler.

### Motivation

The existing baseline tests (`test/bash/*.sh`, 31 pairs) are hand-written integration tests targeting specific features implemented in Phases 1–9a. They verify that what Lambda implements works correctly but don't reveal **what's missing**. The official Bash test suite fills this gap — it defines what a full Bash implementation must handle, making it a roadmap for remaining work.

### Summary

| Metric | Value |
|--------|-------|
| Test source | GNU Bash 5.x (`git.savannah.gnu.org/git/bash.git`) |
| Location | `ref/bash/tests/` (shallow clone, `.gitignore`'d) |
| Test pairs (`.tests` + `.right`) | 82 |
| Sub-scripts (`.sub` files) | 451 |
| GTest harness | `test/test_bash_official_gtest.cpp` (428 LOC) |
| Binary | `test/test_bash_official_gtest.exe` |
| Build category | Extended tests |
| Per-test timeout | 10 seconds |
| Output cap | 1 MB |
| Total runtime | ~21 seconds (debug build) |
| **Current pass rate** | **1 / 82 (1.2%)** |
| Passing test | `strip` |

---

## Architecture

### Test Suite Structure

The official Bash test suite follows a consistent pattern:

```
ref/bash/tests/
├── arith.tests          # Test script: runs arithmetic test cases
├── arith.right          # Expected output (stdout + stderr combined)
├── arith-for.tests      # C-style for loop tests
├── arith-for.right
├── array.sub            # Sub-script invoked via ${THIS_SH} from array.tests
├── array1.sub
├── array2.sub
├── ...
└── run-all              # Original Bash runner (not used by Lambda)
```

**Key convention:** Tests invoke sub-scripts via `${THIS_SH} ./some.sub`, where `THIS_SH` is set to the shell being tested. Lambda's harness sets `THIS_SH="<abs_path>/lambda.exe bash"` so sub-scripts are also run through Lambda's transpiler.

### GTest Harness Design

The harness (`test/test_bash_official_gtest.cpp`) uses GTest parameterized tests with auto-discovery:

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│  discover_bash_  │     │  execute_bash_   │     │   TEST_P:        │
│  tests()         │────▶│  test()          │────▶│   ExecuteAnd     │
│                  │     │                  │     │   Compare        │
│  Scans for       │     │  fork/exec with  │     │                  │
│  *.tests +       │     │  process group,  │     │  Filter noise,   │
│  *.right pairs   │     │  timeout, output │     │  trim, compare   │
│                  │     │  cap             │     │  EXPECT_STREQ    │
└──────────────────┘     └──────────────────┘     └──────────────────┘
```

**Key implementation details:**

1. **Auto-discovery**: `discover_bash_tests()` scans `ref/bash/tests/` for every `*.tests` file that has a matching `*.right` file. Tests are sorted alphabetically and registered as parameterized GTest cases.

2. **Process isolation**: Each test runs in a forked child process with its own process group (`setpgid(0,0)`). This allows killing the entire process tree (test + any spawned sub-scripts) on timeout.

3. **Timeout enforcement**: A `select()` loop with 1-second polling checks elapsed wall time against the 10-second timeout. On timeout, `kill(-pid, SIGKILL)` terminates the entire process group.

4. **Output cap**: If a test produces more than 1 MB of output (e.g., infinite loops), the process group is killed immediately.

5. **Noise filtering**: Lambda's debug build emits timestamp-prefixed log lines (`HH:MM:SS [NOTE] ...`) to stderr. `filter_lambda_noise()` strips these before comparison, matching the regex pattern `^\d{2}:\d{2}:\d{2} \[`.

6. **Environment setup**: The child process `chdir`s into `ref/bash/tests/` and sets `THIS_SH=<abs>/lambda.exe bash` so sub-script invocations resolve correctly.

7. **Cross-platform**: Windows uses `popen` with a command string (no process group timeout), Unix uses `fork`/`exec`/`select`.

### Running the Tests

```bash
# Build (included in extended test suite)
make build-test

# Run all 82 tests
./test/test_bash_official_gtest.exe

# Run a single test
./test/test_bash_official_gtest.exe --gtest_filter=Official/BashOfficialTest.ExecuteAndCompare/arith

# Run tests matching a pattern
./test/test_bash_official_gtest.exe --gtest_filter=Official/BashOfficialTest.ExecuteAndCompare/*quote*
```

---

## Setup & Dependencies

### Clone Command (all platforms)

```bash
git clone --depth 1 https://git.savannah.gnu.org/git/bash.git ref/bash
```

This is integrated into all three setup scripts:

| Script | Line |
|--------|------|
| `setup-mac-deps.sh` | ~1444 |
| `setup-linux-deps.sh` | ~1435 |
| `setup-windows-deps.sh` | ~1313 |

The `ref/` directory is listed in `.gitignore` — the clone is local only and not checked into version control.

### Build Configuration

Added to `build_lambda_config.json` (line ~2678) under the **extended** test category:

```json
{
    "source": "test/test_bash_official_gtest.cpp",
    "binary": "test_bash_official_gtest.exe",
    "category": "extended"
}
```

### Premake Integration

The generated `premake5.mac.lua` includes the test project automatically via `make` (which runs `utils/generate_premake.py` from `build_lambda_config.json`).

---

## Current Results (March 2026)

### Summary

```
[==========] Running 82 tests from 1 test suite.
[==========] 82 tests from 1 test suite ran. (20734 ms total)
[  PASSED  ] 1 test.
81 FAILED TESTS
```

### Passing: 1 test

| Test | Feature | Time |
|------|---------|------|
| `strip` | Parameter expansion `${var#pat}`, `${var%pat}`, `${var##pat}`, `${var%%pat}` | 28 ms |

This passes because Lambda already implements all 16 forms of parameter expansion (Phase 5 in Transpile_Bash.md).

### Complete Test Inventory (82 tests)

Grouped by feature area with current status:

#### Arithmetic & Numeric (3 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `arith` | Arithmetic expansion `$(( ))` | FAIL | Sub-script invocations, edge cases |
| `arith-for` | C-style `for((;;))` loops | FAIL | Timeout-prone (1.2s with cap) |
| `appendop` | `+=` operator for strings/arrays | FAIL | |

#### Arrays & Associative Arrays (3 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `array` | Indexed arrays | FAIL | Complex sub-scripts |
| `assoc` | Associative arrays `declare -A` | FAIL | |
| `quotearray` | Quoting in array contexts | FAIL | |

#### Brace & Glob Expansion (5 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `braces` | Brace expansion `{a,b}`, `{1..5}` | FAIL | |
| `glob` | Glob patterns `*`, `?`, `[...]` | FAIL | |
| `glob-bracket` | Extended bracket patterns | FAIL | |
| `globstar` | `**` recursive glob | FAIL | |
| `extglob` / `extglob2` / `extglob3` | Extended globs `?(pat)`, `+(pat)`, etc. | FAIL | Not yet implemented |

#### Case & Pattern Matching (3 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `case` | `case` statement | FAIL | |
| `casemod` | Case modification `${var^^}`, `${var,,}` | FAIL | |
| `posixpat` | POSIX pattern matching | FAIL | |

#### Command Substitution (4 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `comsub` | `$(command)` substitution | FAIL | |
| `comsub2` | Additional comsub cases | FAIL | |
| `comsub-eof` | Comsub with heredoc EOF | FAIL | |
| `comsub-posix` | POSIX-mode comsub | FAIL | |

#### Conditionals & Tests (3 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `cond` | `[[ ]]` conditional expressions | FAIL | |
| `test` | `[ ]` / `test` builtin | FAIL | |
| `invert` | `!` command negation | FAIL | |

#### Builtins & Shell Options (5 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `builtins` | General builtins | FAIL | |
| `shopt` | `shopt` shell options | FAIL | |
| `type` | `type` builtin | FAIL | |
| `getopts` | `getopts` builtin | FAIL | Not yet implemented |
| `complete` | Programmable completion | FAIL | Not targeted for Lambda |

#### Expansion & Quoting (15 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `exp` | General expansions | FAIL | |
| `more-exp` | Additional expansion cases | FAIL | |
| `new-exp` | Bash 4+ expansion features | FAIL | |
| `posixexp` / `posixexp2` | POSIX-compliant expansion | FAIL | |
| `rhs-exp` | Right-hand-side expansion | FAIL | |
| `strip` | `${var#}`, `${var%}`, etc. | **PASS** | ✅ |
| `quote` | Quoting rules | FAIL | |
| `iquote` | `$'...'` and `$"..."` quoting | FAIL | |
| `nquote` | ANSI-C quoting | FAIL | |
| `nquote1`–`nquote5` | Extended quoting tests | FAIL | |

#### Functions (2 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `func` | Function declaration and calling | FAIL | |
| `exportfunc` | `export -f` function export | FAIL | Not yet implemented |

#### Here-Documents & Here-Strings (2 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `heredoc` | Here-documents `<<EOF` | FAIL | |
| `herestr` | Here-strings `<<<` | FAIL | |

#### History & Interaction (2 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `histexp` | History expansion `!` | FAIL | Interactive-only feature |
| `history` | History list management | FAIL | Interactive-only feature |

#### IFS & Word Splitting (2 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `ifs` | IFS word splitting | FAIL | Not yet implemented |
| `ifs-posix` | POSIX IFS semantics | FAIL | Not yet implemented |

#### Job Control & Coproc (3 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `jobs` | Job control (`&`, `bg`, `fg`, `wait`) | FAIL | Not yet implemented |
| `coproc` | Coprocesses | FAIL | Not yet implemented |
| `lastpipe` | `shopt -s lastpipe` | FAIL | |

#### Variables & Environment (5 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `varenv` | Variable/environment manipulation | FAIL | |
| `nameref` | `declare -n` name references | FAIL | Not yet implemented |
| `dynvar` | Dynamic/special variables | FAIL | |
| `attr` | Variable attributes | FAIL | |
| `dstack` / `dstack2` | Directory stack `pushd`/`popd` | FAIL | |

#### Parser & Invocation (3 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `parser` | Parser edge cases | FAIL | |
| `invocation` | Shell invocation modes | FAIL | |
| `rsh` | Restricted shell mode | FAIL | |

#### Printf & Printing (2 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `printf` | `printf` builtin | FAIL | |
| `cprint` | C-style print escapes | FAIL | |

#### Process Substitution (1 test)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `procsub` | `<(cmd)` / `>(cmd)` | FAIL | Not yet implemented |

#### Redirections (2 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `redir` | File redirections `>`, `>>`, `<` | FAIL | |
| `vredir` | Variable-target redirections `{fd}>` | FAIL | |

#### Set & Options (3 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `set-e` | `set -e` (errexit) | FAIL | |
| `set-x` | `set -x` (xtrace) | FAIL | |
| `posix2` | POSIX mode behavior | FAIL | |

#### Tilde & Path (3 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `tilde` | Tilde expansion `~` | FAIL | |
| `tilde2` | Extended tilde tests | FAIL | |
| `posixpipe` | POSIX pipe semantics | FAIL | |

#### Trap & Signals (1 test)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `trap` | `trap` builtin, signal handling | FAIL | |

#### Error Handling (1 test)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `errors` | Error conditions and messages | FAIL | |

#### Internationalization (1 test)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `intl` | `$"..."` localization | FAIL | |

#### Debugging (2 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `dbg-support` | DEBUG trap, `BASH_SOURCE`, etc. | FAIL | |
| `dbg-support2` | Extended debug support | FAIL | |

#### Mapfile / Read (2 tests)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `mapfile` | `mapfile` / `readarray` builtin | FAIL | Not yet implemented |
| `read` | `read` builtin edge cases | FAIL | |

#### Alias (1 test)

| Test | Feature | Status | Notes |
|------|---------|--------|-------|
| `alias` | Alias expansion | FAIL | |

---

## Feature Coverage Gap Analysis

Comparing current Lambda Bash implementation (Transpile_Bash.md Phase 1–9a) against the official test suite reveals these priority areas:

### Already Implemented but Tests Still Fail

These features are implemented in Lambda but the official tests exercise edge cases, sub-script patterns, or interactions not yet covered:

| Feature | Lambda Status | Official Tests | Gap |
|---------|--------------|----------------|-----|
| Arithmetic | ✅ All operators | `arith`, `arith-for` | Sub-script invocation via `${THIS_SH}`, edge cases |
| Arrays | ✅ Full indexed + assoc | `array`, `assoc`, `quotearray` | Complex sub-script patterns |
| Parameter expansion | ✅ All 16 forms | `strip` ✅, `exp`, `more-exp`, `new-exp` | Only `strip` passes — others test combinations |
| Control flow | ✅ All constructs | `case`, `cond`, `test`, `invert` | Edge cases, sub-scripts |
| Functions | ✅ Both syntaxes | `func` | Sub-script patterns |
| Brace expansion | ✅ All forms | `braces` | Complex nesting |
| Tilde expansion | ✅ `~`, `~/path` | `tilde`, `tilde2` | Sub-script interaction |
| `set -e/-x` | ✅ Implemented | `set-e`, `set-x` | Edge-case interactions |
| Trap/signals | ✅ EXIT/ERR/DEBUG/signals | `trap` | Complex handler patterns |
| Here-documents | ✅ Implemented | `heredoc`, `herestr` | Edge cases |
| Redirections | ✅ `>`, `>>`, `<` | `redir`, `vredir` | Variable-target `{fd}>` not impl. |

### Not Yet Implemented

| Feature | Official Tests | Priority |
|---------|---------------|----------|
| IFS word splitting | `ifs`, `ifs-posix` | High — affects variable expansion semantics |
| Process substitution `<()` | `procsub` | Medium |
| Job control (`&`, `bg`, `fg`) | `jobs`, `coproc` | Medium |
| `getopts` builtin | `getopts` | Medium |
| `export -f` | `exportfunc` | Low |
| `declare -n` (namerefs) | `nameref` | Low |
| `mapfile`/`readarray` | `mapfile` | Low |
| Programmable completion | `complete` | Not targeted |
| History expansion | `histexp`, `history` | Not targeted (interactive) |

---

## Relationship to Existing Tests

Lambda has two layers of Bash testing:

| Layer | Location | Count | Purpose |
|-------|----------|-------|---------|
| **Baseline** | `test/bash/*.sh` + `*.txt` | 31 pairs | Hand-written, feature-focused, must pass 100% |
| **Official** | `ref/bash/tests/*.tests` + `*.right` | 82 pairs | Conformance target, aspirational, tracks progress |

The baseline tests are run via `make test-bash-baseline` and must all pass before any commit. The official tests are in the extended category — they track how close Lambda is to full Bash conformance but are not blocking.

### Workflow

1. Implement a feature (e.g., IFS word splitting)
2. Write/update baseline test in `test/bash/` → must pass
3. Run official suite → check if `ifs.tests` and `ifs-posix.tests` now pass
4. As official tests flip from FAIL to PASS, update this document

---

## Timeout & Safety Design

Some official tests exercise interactive features, job control, or infinite loops that would hang a naive test runner. The harness addresses this with:

1. **Process groups**: Child process calls `setpgid(0,0)`, parent calls `setpgid(pid,pid)`. On timeout, `kill(-pid, SIGKILL)` kills the entire tree.

2. **10-second timeout**: Checked every 1 second via `select()` polling. Tests like `arith-for` (which spawns many sub-processes) complete at ~1.2s; `jobs` hits the 10s cap and is killed.

3. **1 MB output cap**: Prevents memory exhaustion from tests that produce unbounded output.

4. **stdin from `/dev/null`**: Prevents tests from blocking on terminal input.

5. **Noise filtering**: Removes Lambda's internal log lines (`HH:MM:SS [LEVEL] ...`) from captured output before comparison.

These measures keep the full 82-test suite running in ~21 seconds on a debug build.

---

## License

The GNU Bash test suite is licensed under **GPLv3**. The test files are not modified or redistributed — they are cloned locally at setup time via `setup-*-deps.sh` and excluded from version control via `.gitignore`.
