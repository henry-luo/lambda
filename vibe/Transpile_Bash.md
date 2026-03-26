# Bash Transpiler — Enhancement Proposal

## Overview

Lambda's Bash transpiler compiles `.sh` scripts to native machine code through the standard Lambda JIT pipeline: Tree-sitter parsing → typed AST → MIR IR → native execution. The implementation covers core shell scripting, pipelines, file I/O, external commands, shell expansions, associative arrays, `declare`, sourcing, environment import, `set` options, signal handling, `trap`, dynamic scope stack for `local` variables, and `--posix` flag — with all 31 integration tests passing.

Phases 1–9a are complete (~93% feature coverage). This document tracks the remaining phases to bring Bash support toward a practical, self-hosting shell runtime capable of running real-world Bash scripts.

### Current State Summary

**Working (100%):**
- Variables, assignment, expansion, `local` (with dynamic scope stack), `export`, `unset`
- Arithmetic expansion (`$(( ))`) — all operators including bitwise, ternary, and short-circuit `&&`/`||`
- Parameter expansion — all 16 forms (`:-`, `:=`, `:+`, `:?`, `#`, `##`, `%`, `%%`, `/`, `//`, `^`, `^^`, `,`, `,,`, `:off:len`, `${!var}`)
- Control flow — `if`/`elif`/`else`, `for`-in, `for((;;))`, `while`, `until`, `case`, `break`, `continue`
- Functions — both syntaxes, positional params, recursion, `return`
- Indexed arrays — declaration, access, modify, append, slice, unset, iterate
- Test expressions — `[ ]` and `[[ ]]` with numeric, string, regex (`=~`), glob operators
- File test operators — `-f`, `-d`, `-e`, `-r`, `-w`, `-x`, `-s`, `-L` (via `stat()`/`access()`/`lstat()`)
- Command substitution — `$(command)` with nested capture (32 levels)
- Subshells — `( commands )` with scope isolation
- Here-documents and here-strings
- Special variables — `$?`, `$#`, `$@`, `$*`, `$$`, `$0`, `$1`–`$9`
- Builtins — `echo`, `printf`, `test`, `true`, `false`, `exit`, `return`, `read`, `shift`, `local`, `export`, `unset`, `cd`, `pwd`
- Pipeline builtins — `cat`, `wc`, `head`, `tail`, `grep`, `sort`, `tr`, `cut`
- Pipelines — builtin-to-builtin zero-copy item passing, hybrid builtin/external pipes, negated pipelines
- File redirections — `>`, `>>`, `<` with capture stack integration, `/dev/null` support
- External command execution — `posix_spawn` with PATH resolution, stdout capture, stdin forwarding, exit code propagation
- Tilde expansion — `~` → `$HOME`, `~/path`, `~user` via `getpwnam()`
- Brace expansion — `{a,b,c}`, `{1..5}`, `{a..z}` with step support
- Associative arrays — `declare -A`, key assignment, key access, `${!map[@]}` keys, `${map[@]}` values, `${#map[@]}` length, `unset` key, for-in iteration
- `declare` builtin — `-a` (indexed array), `-A` (associative array), `-i` (integer coercion), `-r` (readonly), `-x` (export), `-l` (auto-lowercase), `-u` (auto-uppercase), combined flags
- Glob expansion — `*.txt`, `?`, `[a-z]` via POSIX `glob()` (command-argument context only)
- `source`/`.` — dynamic script loading at runtime, shares caller's variable scope
- Environment variable import — `PATH`, `HOME`, `USER`, `PWD`, `SHELL`, `LANG`, `TERM` auto-imported at startup; `export` calls `setenv()` for child inheritance
- `set` builtin — `-e` (errexit), `-u` (nounset), `-x` (xtrace), `-o pipefail`
- `trap` builtin — EXIT/ERR/DEBUG/INT/TERM/HUP/QUIT; OS signals via `sigaction()`; `bash_trap_check()` at loop iterations
- `exit N` — early termination with EXIT trap execution and correct exit code propagation
- `eval` / `bash_eval_string()` — dynamic code compilation and execution at runtime
- Dynamic scope stack — `local` variables properly scoped per function call; dynamic scoping (callees see caller's `local` vars); recursion-safe (256-frame stack of per-call hashmaps)
- `--posix` flag — POSIX-compatible mode; `local` treated as global assignment (matches POSIX sh semantics)

**Not yet implemented:**
- `exec`, `getopts`
- Process substitution (`<(cmd)`, `>(cmd)`)
- Job control (`&`, `bg`, `fg`, `jobs`, `wait`)
- Word splitting on `$IFS`

### Architecture

```
lambda/bash/
├── bash_ast.hpp              567 LOC   AST node types, operator/expansion enums
├── build_bash_ast.cpp       2037 LOC   Tree-sitter CST → Bash AST (brace detection)
├── transpile_bash_mir.cpp   2824 LOC   Bash AST → MIR code generation
├── bash_runtime.h            310 LOC   C API for JIT-callable runtime functions
├── bash_runtime.cpp         2626 LOC   Runtime (exec, pipes, redirects, expansions, scope stack)
├── bash_builtins.cpp         833 LOC   Builtin commands (cat, wc, grep, sort, ...)
├── bash_scope.cpp            193 LOC   Compile-time scope/symbol management
├── bash_transpiler.hpp       111 LOC   Transpiler context struct
```

**CLI entry:** `./lambda.exe bash script.sh` → `main.cpp` reads source → `transpile_bash_to_mir()` → JIT → execute.

**Runtime model:** All values are Lambda `Item` (64-bit tagged). Bash string-first semantics are layered via `bash_to_int()` (atoi coercion) and `bash_to_string()`. Exit codes 0–255 mapped to Lambda booleans for control flow.

---

## Phase 1: Test Harness & Stability ✅ COMPLETED

**Goal:** Establish automated regression testing so all subsequent phases have a safety net.

> **Status:** Implemented `test/test_bash_run.sh` harness and `make test-bash-baseline` Makefile target. Iterates over `test/bash/*.sh`, diffs against `.txt` expected output, reports colored pass/fail with summary. Exit code propagation added to `main.cpp`. Currently 22 test scripts, all passing.

### 1.1 Makefile Integration

Add `make test-bash` and `make test-bash-baseline` targets that:
1. Iterate over `test/bash/*.sh` scripts
2. Run each via `./lambda.exe bash <script>`
3. Diff stdout against `test/bash/<name>.txt` expected output
4. Report pass/fail counts with summary

Pattern to follow: the existing `test/test_run.sh` harness for Lambda tests. Alternatively, a simpler standalone script since bash tests don't need GTest.

```makefile
test-bash-baseline: build
	@echo "Running Bash baseline tests..."
	@passed=0; failed=0; \
	for script in test/bash/*.sh; do \
		name=$$(basename "$$script" .sh); \
		expected="test/bash/$$name.txt"; \
		if [ ! -f "$$expected" ]; then continue; fi; \
		actual=$$(./lambda.exe bash "$$script" 2>/dev/null); \
		expected_content=$$(cat "$$expected"); \
		if [ "$$actual" = "$$expected_content" ]; then \
			passed=$$((passed + 1)); \
		else \
			echo "FAIL: $$name"; \
			failed=$$((failed + 1)); \
		fi; \
	done; \
	echo "Bash tests: $$passed passed, $$failed failed"; \
	[ "$$failed" -eq 0 ]
```

### 1.2 Extended Test Coverage

Write additional test scripts for edge cases not covered by the current 17 scripts:

| Test Script | Coverage |
|-------------|----------|
| `nested_substitution.sh` | Deep command substitution nesting, substitution in assignments |
| `compound_commands.sh` | `{ cmd1; cmd2; }` compound statements |
| `arithmetic_assign.sh` | `$(( i++ ))`, `$(( a += b ))`, ternary in arithmetic |
| `quoting.sh` | Escape sequences, mixed quoting, literal `$` in strings |
| `pipeline_basic.sh` | Multi-command pipelines (initially expected to fail — tracked) |
| `edge_cases.sh` | Empty function bodies, zero-iteration loops, deeply nested if/else |

### 1.3 Exit Code Propagation

Verify that `./lambda.exe bash script.sh` propagates the script's exit code to the process exit code. Currently `main.cpp` always returns 0 — it should return `bash_get_exit_code()` after execution.

**File:** `lambda/main.cpp` line ~1155
```cpp
// Current: always returns 0
runtime_cleanup(&runtime);
log_finish();
return 0;

// Proposed: return script's exit code
int exit_code = bash_exit_code(result);
runtime_cleanup(&runtime);
log_finish();
return exit_code;
```

---

## Phase 2: Pipelines (Builtin-to-Builtin) ✅ COMPLETED

**Goal:** Make `echo "hello" | cat` work by passing captured output between pipeline stages as Lambda `Item` values.

> **Status:** Implemented zero-copy item passing for builtin pipelines. Added `bash_set_stdin_item()`/`bash_get_stdin_item()` API. Pipeline stages use `bash_begin_capture()` / `bash_end_capture()` chain. Added 8 pipeline builtins: `cat`, `wc`, `head`, `tail`, `grep`, `sort`, `tr`, `cut`. Negated pipelines (`!`) supported via `bash_negate_exit_code()`. Hybrid builtin↔external pipelines work via stdin forwarding in `bash_exec_external()`. Test: `test/bash/pipeline.sh` (22 cases).

### 2.1 Design: Zero-Copy Item Passing

For pipelines where all commands are builtins, we can avoid OS pipes entirely. The output of each stage is captured as a string `Item` and injected as stdin for the next stage.

```
echo "hello" | cat | wc -c
    ↓ capture
  Item("hello\n")  →  cat reads Item  →  Item("hello\n")  →  wc reads Item  →  Item("6")
```

### 2.2 Implementation

**transpile_bash_mir.cpp** — Replace the current sequential pipeline execution:

```cpp
// Current (line ~331): just runs commands sequentially
case BASH_AST_NODE_PIPELINE: {
    BashPipelineNode* pipeline = (BashPipelineNode*)node;
    BashAstNode* cmd = pipeline->commands;
    while (cmd) {
        bm_transpile_statement(mt, cmd);
        cmd = cmd->next;
    }
    break;
}
```

With a capture-and-pass chain:

```
For each command in pipeline (left to right):
  1. If not first command: set_stdin_item(captured_output)
  2. bash_begin_capture()
  3. Execute command
  4. captured_output = bash_end_capture()
If last command: bash_write_stdout(captured_output)
```

**bash_runtime.h** — Add stdin item support:
```c
void bash_set_stdin_item(Item input);   // set pending stdin content
Item bash_get_stdin_item(void);         // read pending stdin (for builtins like cat, read)
void bash_clear_stdin_item(void);       // clear after consumption
```

**bash_builtins.cpp** — Update `read` and add `cat` builtin to consume stdin item.

### 2.3 New Builtins for Pipelines

| Builtin | Purpose |
|---------|---------|
| `cat` | Read stdin and echo to stdout (identity pipe stage) |
| `wc` | Word/line/char count (`-l`, `-w`, `-c`) |
| `head` | First N lines (`-n N`) |
| `tail` | Last N lines (`-n N`) |
| `grep` | Pattern matching (RE2 backed via existing `re2_wrapper.hpp`) |
| `sort` | Line sorting |
| `tr` | Character transliteration |
| `cut` | Field extraction (`-d`, `-f`) |

These are implemented as Lambda builtins, not by spawning external processes. They receive input via the stdin item mechanism.

### 2.4 Negated Pipelines

Support `! pipeline` to negate the exit code:
```bash
! grep -q "error" <<< "all good"
echo $?   # 0 (negated from 1)
```

The `BashPipelineNode.negated` flag already exists in the AST — just need to emit a `bash_negate_exit_code()` call after the pipeline.

---

## Phase 3: File Redirections ✅ COMPLETED

**Goal:** Make `echo "data" > file.txt`, `cat < input.txt`, and `cmd >> log.txt` work.

> **Status:** Implemented `bash_redirect_write()`, `bash_redirect_append()`, `bash_redirect_read()` runtime functions. Transpiler emits capture + redirect for `>`, `>>`, and `<` operators. File test operators (`-f`, `-d`, `-e`, `-r`, `-w`, `-x`, `-s`, `-L`) implemented via `stat()`/`access()`/`lstat()`. `/dev/null` redirection works. Tests: `test/bash/redirects.sh`, `test/bash/file_tests.sh`.

### 3.1 Design

Redirect nodes are already parsed into `BashRedirectNode` with fd, mode, and target. The transpiler needs to:

1. Before command execution: open target files and redirect fd
2. Execute command (output goes to file via capture mechanism)
3. After command: restore original fd state

### 3.2 Runtime API

```c
// File redirect management
int  bash_redirect_open(Item filename, int mode);  // returns fd or -1
void bash_redirect_apply(int source_fd, int target_fd);
void bash_redirect_restore(void);

// Redirect modes → open flags
// BASH_REDIR_READ   → O_RDONLY
// BASH_REDIR_WRITE  → O_WRONLY | O_CREAT | O_TRUNC
// BASH_REDIR_APPEND → O_WRONLY | O_CREAT | O_APPEND
// BASH_REDIR_DUP    → dup2()
```

### 3.3 Integration with Capture Stack

File redirections interact with command substitution capture. When a command has `> file` and is inside `$(...)`, the redirect takes precedence — output goes to the file, not the capture buffer. This requires redirect state to be checked in `bash_raw_write()`.

### 3.4 File Test Operators

With file I/O in place, implement the file test operators already defined in `BashTestOp`:

| Operator | Implementation |
|----------|---------------|
| `-f` | `stat()` + `S_ISREG()` |
| `-d` | `stat()` + `S_ISDIR()` |
| `-e` | `stat()` succeeds |
| `-r` | `access(path, R_OK)` |
| `-w` | `access(path, W_OK)` |
| `-x` | `access(path, X_OK)` |
| `-s` | `stat()` + `st_size > 0` |
| `-L` | `lstat()` + `S_ISLNK()` |

All AST infrastructure exists — only runtime functions need implementation.

---

## Phase 4: External Command Execution ✅ COMPLETED

**Goal:** Run system binaries when a command is not a recognized builtin.

> **Status:** Implemented `bash_exec_external()` using `posix_spawn` (Approach A). Features: PATH resolution by searching `$PATH` directories, stdout capture via `pipe()`, stdin forwarding for pipeline integration, exit code propagation to `$?`. Supports absolute paths and PATH-relative commands. Unresolved commands return exit code 127. Test: `test/bash/external_cmds.sh`.

### 4.1 Design Choices

| Approach | Pros | Cons |
|----------|------|------|
| **(A) `posix_spawn`** | Modern, efficient, single syscall | Less flexible than fork+exec |
| **(B) `fork` + `exec`** | Full control, handles redirects naturally | Forking large address space is slow |
| **(C) Hybrid** | `posix_spawn` for simple cases, fork+exec for complex redirects | More code paths |

**Recommendation:** Start with `posix_spawn` (approach A). It covers >90% of use cases and avoids the fork overhead. Fall back to `fork`+`exec` only if redirect complexity requires it.

### 4.2 Implementation

**bash_runtime.h:**
```c
Item bash_exec_external(Item* argv, int argc, Item stdin_input);
```

**Resolution order** (in transpile_bash_mir.cpp):
1. Check if command name matches a builtin → call builtin function
2. Otherwise → call `bash_exec_external()`

**`bash_exec_external` flow:**
1. Convert `Item` argv to `char*` argv (via `bash_to_string`)
2. Search `PATH` for executable
3. If stdin_input is set, create pipe and write input to child's stdin
4. `posix_spawn()` the process
5. Wait for exit, capture stdout via pipe
6. Return captured output as Item or set `$?` from exit code

### 4.3 PATH Resolution

```c
// Search PATH for executable
// Returns malloc'd absolute path or NULL
char* bash_resolve_command(const char* name);
```

Check `$PATH` directories in order. Cache results in a hashmap for the session to avoid repeated filesystem lookups.

### 4.4 Hybrid Pipelines

With external command support, pipelines become heterogeneous:

```bash
echo "hello world" | grep "hello" | wc -l
#      builtin         external      external
```

For builtin→external transitions: write captured output to child's stdin pipe.
For external→external transitions: use OS pipes (`pipe()` + `dup2()`).
For external→builtin transitions: capture child's stdout, pass as stdin Item.

---

## Phase 5: Expansions & Word Processing ✅ COMPLETED

**Goal:** Implement the missing expansion types and word processing rules that real Bash scripts rely on.

> **Status:** Implemented tilde expansion (`bash_expand_tilde()` — `getenv("HOME")` + `getpwnam()` for `~user`), brace expansion (`bash_expand_brace()` — comma lists `{a,b,c}`, numeric ranges `{1..5}` with step, character ranges `{a..z}`), glob expansion (`bash_glob_expand()` — POSIX `glob()` with `GLOB_NOCHECK`), and arithmetic short-circuit (`&&`/`||` in `$(( ))` using MIR conditional branches `BEQ`/`BNE`). Tree-sitter `concatenation` nodes starting with `{` are detected in `build_concatenation()` and converted to word nodes for brace expansion. Glob/brace expansion is context-aware — only applied in command arguments via `bm_transpile_cmd_arg()`, not in parameter expansion patterns like `${var#*/}`. Word splitting (`$IFS`) deferred. Test: `test/bash/expansions.sh`.

### 5.1 Tilde Expansion

```bash
echo ~           # /Users/jane
echo ~/docs      # /Users/jane/docs
echo ~user       # /Users/user (getpwnam)
```

**Implementation:** In `build_bash_ast.cpp`, detect leading `~` in unquoted words. In runtime, call `getenv("HOME")` for `~` and `getpwnam()` for `~user`.

### 5.2 Brace Expansion

```bash
echo {a,b,c}     # a b c
echo {1..5}      # 1 2 3 4 5
echo {01..10}    # 01 02 03 04 05 06 07 08 09 10  (zero-padded)
echo {a..z}      # a b c ... z
```

**Implementation:** Brace expansion happens before any other expansion. Parse brace patterns in the AST builder as a new `BASH_AST_NODE_BRACE_EXPAND` node type. At transpile time, expand into an argument list.

Note: Brace expansion is not performed inside double quotes (matching Bash behavior).

### 5.3 Glob Expansion

```bash
ls *.txt
for f in src/*.cpp; do echo "$f"; done
```

**Implementation:** The `BASH_AST_NODE_GLOB` AST node already exists but has no runtime support. Add:

```c
Item bash_glob_expand(Item pattern);  // returns array of matching paths
```

Use `glob()` from `<glob.h>` (POSIX) or manual directory iteration. Return matched paths as an array Item. If no matches, return the pattern literally (Bash default behavior).

### 5.4 Word Splitting

After variable expansion and command substitution in unquoted contexts, results should be split on `$IFS` (default: space, tab, newline). This is one of the most complex parts of Bash semantics.

**Implementation:**
```c
Item bash_word_split(Item value, Item ifs);  // split string on IFS chars, return array
```

Key rules:
- Double-quoted expansions (`"$var"`) are NOT word-split
- `$@` inside double quotes expands to separate words
- `$*` inside double quotes expands to single string joined by first char of `$IFS`

### 5.5 Arithmetic Short-Circuit

Fix the two TODO items in `transpile_bash_mir.cpp` (lines 874–875):

```bash
$(( 1 || side_effect() ))   # should NOT evaluate side_effect
$(( 0 && side_effect() ))   # should NOT evaluate side_effect
```

Currently both sides are always evaluated. Emit conditional MIR branches:
```
// For a || b:
eval a
if a != 0: result = 1
else: eval b; result = (b != 0)
```

---

## Phase 6: Associative Arrays & `declare` ✅ COMPLETED

**Goal:** Support `declare` builtin and associative arrays for scripts that use key-value data structures.

> **Status:** Implemented all associative array operations using Lambda's `Map` type. Added `bash_assoc_new()`, `bash_assoc_set()`, `bash_assoc_get()`, `bash_assoc_keys()`, `bash_assoc_values()`, `bash_assoc_unset()`, `bash_assoc_len()`. Implemented `declare` builtin with variable attribute flags (`-a`, `-A`, `-i`, `-r`, `-x`, `-l`, `-u`) stored in a parallel metadata table. Tests: `test/bash/assoc_arrays.sh`, `test/bash/declare.sh`.

### 6.1 Associative Arrays

```bash
declare -A colors
colors[red]="#ff0000"
colors[green]="#00ff00"
echo "${colors[red]}"
echo "${!colors[@]}"    # keys
echo "${colors[@]}"     # values
```

**Implementation:** Use Lambda's `Map` type (which already preserves insertion order) for associative arrays. Add a parallel tracking mechanism to distinguish indexed arrays from associative arrays at runtime.

**New runtime functions:**
```c
Item bash_assoc_new(void);
Item bash_assoc_set(Item map, Item key, Item value);
Item bash_assoc_get(Item map, Item key);
Item bash_assoc_keys(Item map);     // ${!map[@]}
Item bash_assoc_unset(Item map, Item key);
```

### 6.2 `declare` Builtin

| Flag | Meaning | Implementation |
|------|---------|---------------|
| `-a` | Indexed array | Mark variable as array type |
| `-A` | Associative array | Create map-backed variable |
| `-i` | Integer | Set integer attribute (arithmetic on every assignment) |
| `-r` | Readonly | Prevent reassignment |
| `-x` | Export | Same as `export` |
| `-l` | Lowercase | Auto-lowercase on assignment |
| `-u` | Uppercase | Auto-uppercase on assignment |
| `-p` | Print | Display variable attributes |

Store variable attributes in a parallel metadata table alongside the variable value table.

---

## Phase 7: Script Sourcing & Environment ✅ COMPLETED

**Goal:** Enable `source`/`.` for script composition and proper environment variable handling.

> **Status:** Implemented `source`/`.` by re-invoking `bash_eval_string()` at runtime, sharing the caller's variable scope. Environment import (`bash_env_import()`) runs at startup for `PATH`, `HOME`, `USER`, `PWD`, `SHELL`, `LANG`, `TERM`; `export` calls `setenv()` for child processes. Implemented `set` builtin with `-e` (errexit), `-u` (nounset), `-x` (xtrace), `-o pipefail` flags checked at appropriate evaluation points. Tests: `test/bash/source_cmd.sh`, `test/bash/env_vars.sh`, `test/bash/set_options.sh`.

### 7.1 `source` / `.` Command

```bash
source ./lib.sh
. ./helpers.sh
```

**Implementation:**
1. Read the target file
2. Parse with tree-sitter
3. Build AST
4. Transpile the sourced AST into the current MIR module (NOT as a separate JIT unit)
5. Functions and variables from the sourced script are available in the caller's scope

**Challenge:** Sourced scripts must share the same variable scope as the caller. This means the MIR transpiler needs to handle multiple ASTs merged into one compilation unit.

**Alternative approach:** Re-invoke `transpile_bash_to_mir()` for the sourced file at runtime, passing the current scope. Simpler but prevents compile-time optimization.

### 7.2 Environment Variable Integration

```bash
export PATH="/usr/local/bin:$PATH"
MY_VAR="hello" ./child_script.sh    # prefix assignment for child only
```

**Implementation:**
```c
void bash_env_import(void);         // import getenv → bash var table at startup
void bash_env_export(Item name);    // setenv when `export` is called
void bash_env_build_for_child(void); // build envp array for posix_spawn
```

At `bash_runtime_init()`, import select environment variables (`PATH`, `HOME`, `USER`, `PWD`, `SHELL`, `LANG`, `TERM`) into the Bash variable table. When `export` is called, also call `setenv()` so child processes inherit the change.

### 7.3 `set` Builtin

```bash
set -e          # exit on error
set -u          # error on undefined variable
set -o pipefail # pipeline fails if any stage fails
set -x          # trace execution
```

**Implementation:** Store shell option flags in a runtime struct. Check flags at relevant points:

| Option | Check Point |
|--------|------------|
| `-e` (errexit) | After every command — if exit code != 0, exit script |
| `-u` (nounset) | In `bash_get_var()` — error if variable is unset |
| `-x` (xtrace) | Before every command — print `+ command args` to stderr |
| `pipefail` | After pipeline — fail if any stage failed |

---

## Phase 8: Signal Handling & Job Control ✅ COMPLETED

**Goal:** Support `trap` and basic job control for scripts that need cleanup handlers.

> **Status:** Implemented `trap` builtin, EXIT trap (runs at script end and on `exit N`), signal traps (INT/TERM/HUP/QUIT via `sigaction()`), `bash_eval_string()` for dynamic code execution, and `bash_trap_check()` called at each loop iteration. `exit N` now correctly terminates the script early (via `BASH_AST_NODE_EXIT` case in the transpiler). Tests: `test/bash/trap.sh`, `test/bash/exit_trap.sh`.

### 8.1 `trap` Builtin

```bash
trap 'echo "Cleaning up..."; rm -f /tmp/lockfile' EXIT
trap 'echo "Interrupted"; exit 1' INT
trap '' SIGTERM    # ignore SIGTERM
```

**Implementation:**
```c
void bash_trap_set(int signal, Item handler);  // register handler
void bash_trap_clear(int signal);
void bash_trap_check(void);                    // called periodically to run pending handlers
```

Use `sigaction()` to install C signal handlers that set flags. Check flags at safe points (between statement executions) and invoke the Bash handler code.

Priority signals: `EXIT`, `INT`, `TERM`, `ERR`, `DEBUG`.

### 8.2 Basic Job Control (Low Priority)

Job control (`&`, `bg`, `fg`, `jobs`, `wait`) requires process group management and is complex. Defer to a later phase unless specific use cases emerge.

The `&` background operator (`BashListOp::BASH_LIST_BG`) is defined in the AST but would require fork/thread to implement. A simpler approach: `wait` builtin can be implemented as a synchronous no-op if background execution is not supported.

---

## Phase 8b: Scope Stack & POSIX Mode ✅ COMPLETED

**Goal:** Correct `local` variable scoping for nested/recursive functions, and add a `--posix` CLI flag for POSIX sh compatibility.

> **Status:** Implemented runtime dynamic scope stack (Option A) — per-call hashmaps on a 256-frame stack, walked top→bottom for dynamic scoping, one frame per active function invocation. `local` now correctly creates function-scoped variables. Added `--posix` flag that treats `local` as a global assignment (POSIX sh has no `local`). Fixed the `local` builtin handler in the transpiler. Tests: `test/bash/scope_stack.sh`, `test/bash/posix_mode.sh`.

### 8b.1 Dynamic Scope Stack

Bash uses **dynamic scoping** for functions — a callee can see variables declared `local` in its caller. The previous flat global table broke `local` in nested and recursive calls.

**Implementation:** `bash_func_scope_stack[]` — a 256-entry array of `hashmap*`, one frame per active function call.

```
caller() {             # bash_scope_push() → depth=1
  local x=10           # bash_set_local_var("x", 10) → writes to frame[0]
  callee()             # bash_scope_push() → depth=2
}                      #   callee can read x=10 via stack walk
                       # bash_scope_pop() → depth=0
```

Variable lookup in `bash_get_var()` walks the stack top→bottom before falling through to the global table. `bash_set_var()` updates in-place in the innermost frame where the variable exists, otherwise writes globally. `bash_set_local_var()` always writes to the top frame.

**Recursion safety:** Each call to a function pushes a new frame, so `local n` in `fib(7)` creates independent `n` per stack level.

### 8b.2 `--posix` CLI Flag

```bash
./lambda.exe bash --posix script.sh    # run in POSIX sh mode
```

In POSIX mode, `local` is not a standard builtin. The runtime flag `bash_posix_mode` (set from `--posix` in `main.cpp`) causes `bm_emit_set_local_var()` to emit `bash_set_var` instead of `bash_set_local_var` — making `local` a global assignment matching `/bin/sh` behavior.

### 8b.3 Key Changes

**`bash_runtime.cpp`:**
- Added `bash_func_scope_stack[256]` and `bash_func_scope_depth` state
- Added `bash_posix_mode` state flag
- Implemented `bash_scope_push()` / `bash_scope_pop()` with `hashmap_new()`/`hashmap_free()`
- Rewrote `bash_get_var()`, `bash_set_var()`, `bash_set_local_var()`, `bash_unset_var()` to walk scope stack
- Added `bash_set_posix_mode()` / `bash_get_posix_mode()`

**`transpile_bash_mir.cpp`:**
- Added `bm_emit_set_local_var()` helper — emits `bash_set_local_var` or `bash_set_var` based on POSIX mode
- Fixed `local` builtin handler — now emits `bm_emit_set_local_var()` per argument
- `bm_transpile_function_def()` — added `bash_scope_push` / `bash_scope_pop` around function body
- `BASH_AST_NODE_RETURN` — added `bash_scope_pop` before `bash_pop_positional` to avoid scope leak on early return
- `bm_transpile_assignment()` — honors `is_local` flag to route to `bm_emit_set_local_var()`

**`main.cpp`:**
- Replaced hardcoded `argv[2]` with a loop scanning for `--posix` flag and script file argument
- Calls `bash_set_posix_mode(true)` when `--posix` detected

---

## Phase 9: Lambda-Enhanced Builtins

### 9.1 Data Processing Builtins

| Builtin | Lambda Subsystem | Bash Usage |
|---------|-----------------|------------|
| `json_parse` | `input-json.cpp` | `data=$(json_parse file.json)` |
| `json_format` | `format-json.cpp` | `echo "$data" \| json_format` |
| `yaml_parse` | `input-yaml.cpp` | `config=$(yaml_parse config.yaml)` |
| `xml_parse` | `input-xml.cpp` | `doc=$(xml_parse page.html)` |
| `csv_parse` | `input-csv.cpp` | `table=$(csv_parse data.csv)` |
| `md_parse` | `input-markdown.cpp` | `ast=$(md_parse readme.md)` |
| `regex` | `re2_wrapper.hpp` | `matches=$(regex "pattern" "$text")` |

These builtins return Lambda `Item` values directly, enabling powerful data processing:

```bash
# Parse JSON, extract field, format as YAML
data=$(json_parse api_response.json)
echo "$data" | json_query ".users[0].name"
echo "$data" | yaml_format
```

### 9.2 Lambda Interop

Allow calling Lambda functions from Bash and vice versa:

```bash
# Call a Lambda function
result=$(lambda_eval 'map([1,2,3], fn(x) x * 2)')
echo "$result"    # [2, 4, 6]
```

This bridges Bash's procedural scripting with Lambda's functional data processing.

---

## Implementation Priorities

| Phase | Effort | Impact | Status |
|-------|--------|--------|--------|
| **Phase 1: Test Harness** | Small | High — enables safe iteration | ✅ Done |
| **Phase 2: Pipelines** | Medium | High — core shell feature | ✅ Done |
| **Phase 3: File Redirections** | Medium | High — scripts need file I/O | ✅ Done |
| **Phase 4: External Commands** | Large | Critical — scripts call external tools | ✅ Done |
| **Phase 5: Expansions** | Medium | Medium — needed for real scripts | ✅ Done |
| **Phase 6: Assoc Arrays** | Medium | Medium — used in complex scripts | ✅ Done |
| **Phase 7: Source & Env** | Medium | High — script composition | ✅ Done |
| **Phase 8: Signals** | Medium | Low — specialized use | ✅ Done |
| **Phase 8b: Scope Stack & POSIX** | Small | Medium — correctness for `local` | ✅ Done |
| **Phase 9: Lambda Builtins** | Medium | High — unique differentiator | Planned |

### Recommended Execution Order

```
Phase 1 (Test Harness)              ✅ Done — 31 tests, test_bash_run.sh harness
    │
    ▼
Phase 2 (Pipelines)                 ✅ Done — zero-copy item passing, 8 builtins
    │
    ▼
Phase 3 (File Redirections)         ✅ Done — >, >>, <, file tests
    │
    ▼
Phase 4 (External Commands)         ✅ Done — posix_spawn, PATH resolution
    │
    ▼
Phase 5 (Expansions)                ✅ Done — tilde, brace, glob, arith short-circuit
    │
    ▼
Phase 6 (Assoc Arrays & declare)    ✅ Done — Lambda Map type, declare flags
    │
    ▼
Phase 7 (Source & Environment)      ✅ Done — source/., env import, set options
    │
    ▼
Phase 8 (Signals)                   ✅ Done — trap, sigaction, bash_eval_string
    │
    ▼
Phase 8b (Scope Stack & POSIX)      ✅ Done — dynamic scope stack, --posix flag
    │
    ▼
Phase 9 (Lambda-Enhanced Builtins)  ← NEXT
```

Phase 5's word splitting (`$IFS`) may be revisited if needed by real-world scripts.

---

## Open Questions

1. **Should `./lambda.exe script.sh` auto-detect Bash via shebang/extension?** Currently requires the `bash` subcommand. Auto-detection from `.sh` extension or `#!/bin/bash` shebang would be more ergonomic.

2. **Security boundary for external commands.** Should there be a sandboxing mode that restricts which external commands can be executed? Lambda's security posture may warrant a `--allow-exec` flag.

3. ~~**POSIX sh compatibility.**~~ **Resolved (Phase 8b):** `--posix` flag implemented. Running with `./lambda.exe bash --posix script.sh` treats `local` as a global assignment, matching `/bin/sh` semantics.

4. **Performance target.** For scripts that only use builtins + Lambda-enhanced builtins (no external commands), the JIT pipeline should outperform native Bash by a significant margin for computation-heavy scripts. Worth benchmarking once pipelines work.

5. ~~**Runtime scope model.**~~ **Resolved (Phase 8b):** Implemented Option A — runtime dynamic scope stack. See Phase 8b section for details.

---

## Files to Create / Modify

### Files Created (Phases 1–8b)
| File | Purpose |
|------|--------|
| `test/test_bash_run.sh` | Bash test runner script (colored output, diff on failure) |
| `test/bash/pipeline.sh` + `.txt` | Pipeline integration tests |
| `test/bash/redirects.sh` + `.txt` | File redirection tests |
| `test/bash/file_tests.sh` + `.txt` | File test operator tests (`-f`, `-d`, `-e`, etc.) |
| `test/bash/external_cmds.sh` + `.txt` | External command execution tests |
| `test/bash/expansions.sh` + `.txt` | Tilde, brace, glob, arithmetic short-circuit tests |
| `test/bash/assoc_arrays.sh` + `.txt` | Associative array tests |
| `test/bash/declare.sh` + `.txt` | `declare` builtin tests |
| `test/bash/source_cmd.sh` + `.txt` | `source`/`.` tests |
| `test/bash/env_vars.sh` + `.txt` | Environment variable import/export tests |
| `test/bash/set_options.sh` + `.txt` | `set -e`, `-u`, `-x`, `pipefail` tests |
| `test/bash/trap.sh` + `.txt` | `trap` builtin, EXIT/signal handler tests |
| `test/bash/exit_trap.sh` + `.txt` | `exit N` early termination + EXIT trap tests |
| `test/bash/scope_stack.sh` + `.txt` | Dynamic scope stack tests (accumulation, dynamic scoping, recursion) |
| `test/bash/posix_mode.sh` + `.txt` | `--posix` flag smoke test (global assignment, arithmetic, strings) |

### Files Modified (Phases 1–8)
| File | Changes |
|------|--------|
| `Makefile` | Added `test-bash-baseline` target |
| `lambda/main.cpp` | Propagate bash exit code to process exit |
| `lambda/bash/transpile_bash_mir.cpp` | Pipeline capture chain; external command fallback; `bm_transpile_cmd_arg()` for context-aware glob/brace expansion; arithmetic `&&`/`||` short-circuit; `bash_eval_string()`; `BASH_AST_NODE_EXIT` case; `trap` builtin; `bash_trap_check()` in loops; `bash_trap_run_exit()` before main return |
| `lambda/bash/bash_runtime.h` | Stdin item API; file redirect API; expansion functions; external exec API; assoc array API; env import/export API; shell option flags; trap API |
| `lambda/bash/bash_runtime.cpp` | Pipeline data passing; file redirects; external exec via `posix_spawn`; tilde/brace/glob expansion; assoc arrays; env import (`bash_env_import()`); `bash_trap_set/run_exit/check`; OS signal handlers via `sigaction()` |
| `lambda/bash/bash_builtins.cpp` | Added `cat`, `wc`, `head`, `tail`, `grep`, `sort`, `tr`, `cut`; `source`/`.`; `declare`; `set` |
| `lambda/bash/build_bash_ast.cpp` | Brace pattern detection in `build_concatenation()`; redirect handling; `declare` command parsing |
| `lambda/bash/bash_ast.hpp` | Variable attribute flags for `declare` |
| `lambda/sys_func_registry.c` | Registered all new runtime functions for JIT linking |

### Files Modified (Phase 8b)
| File | Changes |
|------|--------|
| `lambda/main.cpp` | `--posix` flag parsing; calls `bash_set_posix_mode(true)` when detected |
| `lambda/bash/bash_runtime.h` | Added POSIX mode API: `bash_set_posix_mode()`, `bash_get_posix_mode()` |
| `lambda/bash/bash_runtime.cpp` | `bash_func_scope_stack[256]` state; implemented `bash_scope_push/pop`; rewrote `bash_get/set_var`, `bash_set_local_var`, `bash_unset_var` to walk scope stack; added `bash_posix_mode` flag and getter/setter |
| `lambda/bash/transpile_bash_mir.cpp` | `bm_emit_set_local_var()` helper; fixed `local` builtin handler; `bash_scope_push/pop` wired into `bm_transpile_function_def()`; `bash_scope_pop` before `bash_pop_positional` in `BASH_AST_NODE_RETURN`; `is_local` flag honored in `bm_transpile_assignment()` |

### Files Still to Modify (Phase 9)
| File | Changes |
|------|--------|
| `lambda/bash/bash_builtins.cpp` | `json_parse`, `yaml_parse`, `xml_parse`, `csv_parse`, `regex`, `lambda_eval` builtins |
| `lambda/bash/bash_runtime.cpp` | Lambda interop bridge for `lambda_eval` |
| `lambda/sys_func_registry.c` | Register new Phase 9 functions |
