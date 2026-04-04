# Bash Transpiler — Phase 5: Closing the GNU Test Gap

## Executive Summary

Phase 4 built four new C modules (Exec Engine, Extended Builtins, Conditional Engine, Heredoc Engine) and raised passing GNU tests from 7 to 12. Phase 5 continues the proven methodology — **modular C APIs first, then wire the transpiler** — targeting the root causes that block the highest number of remaining tests.

**Current status:**
- Integration tests: 37/37 passing
- GNU official: 12/82 passing (dbg_support, dbg_support2, dstack2, dynvar, extglob3, getopts, ifs, invert, nquote4, strip, tilde, tilde2)
- GNU official: 9 tests have missing `.tests`/`.right` files (not real failures)
- Real failing tests: 61 out of 73 valid tests

**Analysis of 61 failing tests reveals 8 root cause clusters.** The top 4 clusters account for ~45 tests. Phase 5 targets these with 5 new/extended modules.

---

## 1. Design Principles (Unchanged from Phase 3–4)

1. **C functions are the source of truth.** Fix the C function, not the code generator.
2. **Unit test before integration.**
3. **Match GNU Bash output exactly.** Byte-for-byte against `*.right` files.
4. **Progressive enhancement.** Each module independently valuable.
5. **Stay within Lambda's C+ convention.**
6. **Keep the transpiler simple.** Emit `call runtime_func(args)`.

---

## 2. Root Cause Analysis

### 2.1 Failure Classification by Diff Size

Tests sorted by diff line count (smaller = closer to passing):

| Bucket | Tests | Count | Approx. Fix Effort |
|--------|-------|-------|---------------------|
| Small (≤50 diff) | exportfunc, posix2, lastpipe, parser, appendop, posixpat, rsh, procsub, herestr, attr, posixexp2, posixpipe | 12 | Low–Medium |
| Medium (51–150) | case, casemod, nquote3, alias, cprint, nquote2, dstack, nquote5, nquote, iquote, nquote1, read, braces, vredir, trap, type | 16 | Medium |
| Large (151–400) | quote, extglob, heredoc, mapfile, quotearray, redir, comsub2, cond, func, glob, posixexp, shopt, test, arith, varenv, printf, errors, assoc | 18 | Medium–High |
| Very Large (>400) | exp, builtins, nameref, globstar, comsub | 5 | High |
| Missing files | comsub_eof, comsub_posix, glob_bracket, ifs_posix, more_exp, new_exp, rhs_exp, set_e, set_x | 9 | N/A |
| Deferred (interactive/infeasible) | arith_for, complete, coproc, histexp, history, intl, invocation, jobs | 8 | Deferred |

### 2.2 Root Cause Clusters

| ID | Root Cause | Tests Affected | Diff Impact |
|----|-----------|----------------|-------------|
| **RC1** | `$"..."` locale string not unquoted | nquote, nquote1, nquote2, nquote3, nquote5, iquote, quote, exp, posixexp | ~9 tests, ~700+ diff lines |
| **RC2** | `${var~}` / `${var~~}` case toggle missing | casemod, exp | ~2 tests, ~120 diff lines |
| **RC3** | Process substitution `<(...)` / `>(...)` not implemented | procsub, comsub2, posixpipe | ~3 tests, ~280 diff lines |
| **RC4** | `declare -p` / `declare -A` display format broken | assoc, quotearray, attr, appendop, nameref, cprint, func | ~7 tests, ~1800 diff lines |
| **RC5** | `${!var}` indirect expansion wrong (returns name, not value) | exp, posixexp, posix2, varenv | ~4 tests, ~500 diff lines |
| **RC6** | Sub-script `.sub` file execution gaps (THIS_SH -c, error propagation) | parser, exportfunc, rsh, heredoc, trap, cond, func, errors | ~8 tests, ~1200 diff lines |
| **RC7** | `shopt` option listing / `set` option display incomplete | shopt, builtins, set_x | ~3 tests, ~900 diff lines |
| **RC8** | Quoting/escaping edge cases (backslash-newline, brace escaping, `$'...'` in IFS) | braces, quote, read, nquote2, nquote3, nquote5 | ~6 tests, ~400 diff lines |
| **RC9** | `time` keyword missing | posixpipe | ~1 test, ~48 diff lines |
| **RC10** | Alias expansion incomplete | alias | ~1 test, ~63 diff lines |
| **RC11** | `readonly` enforcement across scopes | attr, case, appendop | ~3 tests, ~120 diff lines |
| **RC12** | Variable-name fd redirection `{var}>file` | vredir | ~1 test, ~124 diff lines |

### 2.3 Verified Working Features

Features confirmed working that were initially suspected as broken:
- `trap` builtin (trap set, trap -p, EXIT trap)
- `[[ ]]` conditional command (string/numeric/file tests)
- `(( ))` arithmetic evaluation
- `test` / `[` builtin
- `recho` external command (found via PATH when cwd = tests dir)
- `shopt -s lastpipe` (pipes last command in current shell)
- `;&` case fall-through
- `declare -n` nameref resolution
- Brace expansion `{a,b,c}`, `{1..5}`
- `+=` string/array append
- `${THIS_SH} -c 'command'` (sub-script execution)
- ANSI-C quoting `$'\n'`, `$'\x41'`

---

## 3. New & Extended C Modules

### Module 12: Process Substitution Engine (`bash_procsub.h` / `bash_procsub.cpp`)

**Purpose:** Implement `<(command)` and `>(command)` process substitution using `/dev/fd/N` or named pipes.

**Tests unblocked:** procsub, comsub2, posixpipe (partial)

Process substitution creates a file descriptor connected to a command's stdin or stdout, and substitutes it as a filename (e.g., `/dev/fd/63`). This is fundamentally different from command substitution `$(...)` — it produces a **filename**, not a string.

```c
// === bash_procsub.h ===

// Process substitution: create pipe + fork, return /dev/fd/N path
// <(command): child writes to pipe, parent reads via /dev/fd/N
// >(command): child reads from pipe, parent writes via /dev/fd/N
//
// Returns: Item string containing the path (e.g., "/dev/fd/63")
// The caller uses this path as a filename argument to the outer command.
//
// cmd_item: an Item representing the command to execute (string or closure)
// The command is executed in a forked child process.
Item bash_procsub_in(Item cmd_item);    // <(command) — read from process
Item bash_procsub_out(Item cmd_item);   // >(command) — write to process

// Wait for all process substitution children to complete
// Called after the parent command finishes
void bash_procsub_wait_all(void);

// Cleanup: close any open fds from process substitution
void bash_procsub_cleanup(void);

// Init/shutdown
void bash_procsub_init(void);
void bash_procsub_shutdown(void);
```

**Key design decisions:**
- On macOS/Linux, use `pipe()` + `fork()`. The child inherits one end of the pipe and runs the command. The parent uses `/dev/fd/N` to reference the other end.
- Track all child PIDs in a static array for `bash_procsub_wait_all()`.
- The transpiler detects `<(...)` / `>(...)` in the AST and emits a single `bash_procsub_in(cmd)` or `bash_procsub_out(cmd)` call. The result is used as a string argument in the outer command.

**Implementation notes:**
- `<(cmd)` creates a pipe, forks. Child: dup2 pipe-write to stdout, exec command. Parent: returns `/dev/fd/<pipe-read-fd>`.
- `>(cmd)` creates a pipe, forks. Child: dup2 pipe-read to stdin, exec command. Parent: returns `/dev/fd/<pipe-write-fd>`.
- On systems without `/dev/fd`, fall back to `mkfifo()` in `./temp/`.

---

### Module 13: Locale & Quoting Engine (`bash_locale.h` / `bash_locale.cpp`)

**Purpose:** Handle `$"..."` locale-translated strings and complete quoting edge cases.

**Tests unblocked:** nquote, nquote1, nquote2, nquote3, nquote5, iquote, quote (partial), exp (partial), posixexp (partial)

This is the **highest-impact** module by test count. The `$"..."` syntax in bash performs locale translation (via `gettext`). In practice, without locale catalogs the string is simply **unquoted** — the `$"` prefix and trailing `"` are stripped, leaving the content as a regular double-quoted string.

```c
// === bash_locale.h ===

// Process $"..." locale string: strip $" prefix, perform variable expansion
// within the string body (same as double-quote), return the result.
// In non-locale mode (no gettext catalogs), this is equivalent to
// stripping $" and " and treating as double-quoted string.
Item bash_locale_expand(Item body);

// Process ANSI-C quote escapes for $'...' strings
// Handles: \n, \t, \r, \a, \b, \e, \f, \v, \\, \', \", \?,
//          \xHH (hex), \uHHHH (unicode), \UHHHHHHHH, \nnn (octal),
//          \cX (control character)
// This extends the existing bash_process_ansi_escapes() with missing escapes.
Item bash_ansi_quote(Item body);

// Backslash-newline continuation: remove \<newline> sequences from input
// Used during word expansion when not inside single quotes
Item bash_remove_backslash_newline(Item text);

// Quote removal: strip syntactic quotes from a word
// Handles: 'single', "double", $'ansi', $"locale", \escape
// Already partially implemented in bash_expand.h as bash_quote_remove()
// This function handles the $" case that bash_quote_remove() misses.
Item bash_locale_quote_remove(Item word);
```

**Key design decisions:**
- `bash_locale_expand()` treats `$"..."` as `"..."` (no actual gettext lookup — matches bash behavior without locale catalogs).
- `bash_ansi_quote()` extends `bash_process_ansi_escapes()` to handle `\cX` (control chars) and `\uHHHH` / `\UHHHHHHHH` (Unicode) which are currently incomplete.
- `bash_remove_backslash_newline()` scans for `\` followed by `\n` and removes both, which is needed for multi-line command continuation inside strings.

**Transpiler integration:** When the AST contains a `$"..."` node, emit `bash_locale_expand(body)` instead of passing the raw `$"..."` literal. This is a single call replacement in the string expansion handler.

---

### Module 14: Declare Formatter (`bash_declare.h` / `bash_declare.cpp`)

**Purpose:** Correct `declare -p` / `declare -A` / `declare -a` output formatting to match GNU Bash exactly.

**Tests unblocked:** assoc, quotearray, attr, appendop, nameref, cprint, func (partial), builtins (partial)

The `declare -p var` output format must match GNU Bash byte-for-byte. Current issues:
- Associative arrays print without keys: `declare -A aa=quux` instead of `declare -A aa=([foo]="bar" [baz]="quux")`
- Array attributes (`-r`, `-i`, `-x`) not shown in declaration
- Function body display (`declare -f func`) incomplete

```c
// === bash_declare.h ===

// Print a single variable declaration in GNU Bash format
// Format: declare [-aAilnrtux] name=value
// For arrays: declare -a name=([0]="val0" [1]="val1")
// For assoc:  declare -A name=([key1]="val1" [key2]="val2")
// For nameref: declare -n name="target"
// For integer: declare -i name="42"
// Output goes to stdout (or capture buffer if active)
void bash_declare_print(Item var_name);

// Print all variables matching a set of attribute flags
// Used by: declare -p (no args), declare -a, declare -A, declare -r, etc.
// flags: combination of BASH_ATTR_* flags to filter by
void bash_declare_print_by_attrs(int flags);

// Print a function definition in GNU Bash format
// Format:
//   name ()
//   {
//       body
//   }
// Used by: declare -f name, type name
void bash_declare_print_func(Item func_name);

// Print all function definitions
// Used by: declare -f (no args), declare -F
void bash_declare_print_all_funcs(bool names_only);

// Format a value for declare output (handles quoting, escaping)
// Produces the RHS of: declare -x VAR="value with \"quotes\""
Item bash_declare_format_value(Item value);

// Format an array for declare output
// Produces: ([0]="val0" [1]="val1" ...)
Item bash_declare_format_array(Item array);

// Format an associative array for declare output
// Produces: ([key1]="val1" [key2]="val2" ...)
Item bash_declare_format_assoc(Item assoc);
```

**Key design decisions:**
- This module replaces the current `bash_declare_print_var()` in `bash_runtime.h` which has incorrect formatting.
- Associative array key ordering: bash uses internal hash order. We iterate keys from the Lambda map structure.
- Value quoting: special characters (`"`, `$`, `` ` ``, `\`, newline) must be escaped with backslash inside double quotes.
- Function body display: requires reconstructing bash syntax from the AST or stored source. The test `cprint` specifically checks complex function body formatting (while, for, if/elif/else, case, `[[ ]]`, `(( ))`). This is a separate serialization concern — the stored function IR needs a "pretty-print to bash" path.

**Implementation notes:**
- Function display is the hardest part. Start with `declare -p` for variables (fixes 5+ tests), defer complex function body display to a later step if needed.
- The existing `bash_declare_print_var()` API in `bash_runtime.h` should delegate to this module.

---

### Extension to Existing Modules

#### 14A: Case Toggle Expansion (extend `bash_runtime.h`)

**Tests unblocked:** casemod, exp (partial)

```c
// ${var~} — toggle case of first character
Item bash_expand_toggle_first(Item value);

// ${var~~} — toggle case of all characters
Item bash_expand_toggle_all(Item value);

// ${var~pattern} — toggle case of first character matching pattern
Item bash_expand_toggle_first_pat(Item value, Item pattern);

// ${var~~pattern} — toggle case of all characters matching pattern
Item bash_expand_toggle_all_pat(Item value, Item pattern);
```

**Implementation:** For each character, if uppercase → lowercase, if lowercase → uppercase. Pattern variant: only toggle characters matching the glob pattern.

#### 14B: Indirect Expansion Fix (extend `bash_runtime.h`)

**Tests unblocked:** exp, posixexp, posix2, varenv (partial)

```c
// ${!var} — indirect expansion: get value of variable whose name is stored in var
// If var="PATH", returns the value of $PATH
// Current bug: returns the variable name instead of dereferencing it
Item bash_expand_indirect(Item var_name);

// ${!prefix@} — list variable names matching prefix
// Returns the names (not values) of all variables starting with prefix
Item bash_expand_prefix_names(Item prefix, bool at_form);

// ${!array[@]} — list array indices/keys
Item bash_expand_array_keys(Item array_name);
```

**Implementation:** `bash_expand_indirect()` is a two-step lookup: `name = bash_get_var(var_name)` → `value = bash_get_var(name)`. The current implementation appears to only do the first step. `bash_expand_prefix_names()` iterates the variable scope chain looking for matching prefixes.

#### 14C: Shopt/Set Display (extend `bash_runtime.h`)

**Tests unblocked:** shopt, builtins (partial)

```c
// Print all shopt options with their on/off status
// Format: optname       on/off
void bash_shopt_print_all(void);

// Print specific shopt option status
void bash_shopt_print(Item name);

// Print all set -o options with their on/off status  
// Format: optname       on/off
void bash_set_print_all(void);
```

**Implementation:** Iterate the internal option arrays and print each in the exact GNU Bash format. Current `shopt` command doesn't list options — it only sets them.

#### 14D: Time Keyword (extend `bash_runtime.h`)

**Tests unblocked:** posixpipe (partial)

```c
// Execute a command and print timing information
// Format (POSIX): real Xm Y.YYYs\nuser Xm Y.YYYs\nsys Xm Y.YYYs
// Format (bash default): \nreal\tXmY.YYYs\nuser\tXmY.YYYs\nsys\tXmY.YYYs
Item bash_time_command(Item command);

// Format a time value in bash's format
// Uses TIMEFORMAT variable if set, else default format
void bash_time_format(double real_sec, double user_sec, double sys_sec);
```

**Implementation:** Wrap command execution with `getrusage(RUSAGE_CHILDREN)` before and after. Print in POSIX or TIMEFORMAT format.

---

## 4. Implementation Plan

### Phase J: Locale & Quoting (Highest Test Count Impact)

**Estimated tests unblocked:** 9 tests (nquote, nquote1, nquote2, nquote3, nquote5, iquote, quote, exp, posixexp — partial)

| Step | Action | Files |
|------|--------|-------|
| J1 | Create `bash_locale.h` / `bash_locale.cpp` | new |
| J2 | Implement `bash_locale_expand()` — strip `$"` / `"` wrapper, treat as double-quoted | bash_locale.cpp |
| J3 | Implement `bash_ansi_quote()` — extend `\cX`, `\uHHHH`, `\UHHHHHHHH` | bash_locale.cpp |
| J4 | Implement `bash_remove_backslash_newline()` | bash_locale.cpp |
| J5 | Wire transpiler: detect `$"..."` AST node → emit `bash_locale_expand()` | transpile_bash_mir.cpp |
| J6 | Register all in `sys_func_registry.c` | sys_func_registry.c |
| J7 | Create integration tests: `test/bash/locale_string.sh` + `.txt` | new |
| J8 | Run GNU suite, update `gnu_baseline.json` | — |

### Phase K: Case Toggle & Indirect Expansion

**Estimated tests unblocked:** 4+ tests (casemod, exp, posix2, varenv — partial)

| Step | Action | Files |
|------|--------|-------|
| K1 | Implement `bash_expand_toggle_first()` / `bash_expand_toggle_all()` | bash_runtime.cpp |
| K2 | Implement pattern-restricted variants `bash_expand_toggle_first_pat()` / `_all_pat()` | bash_runtime.cpp |
| K3 | Fix `bash_expand_indirect()` — two-step dereference | bash_runtime.cpp |
| K4 | Implement `bash_expand_prefix_names()` / `bash_expand_array_keys()` | bash_runtime.cpp |
| K5 | Wire transpiler: `${var~}` / `${var~~}` → toggle functions | transpile_bash_mir.cpp |
| K6 | Wire transpiler: fix `${!var}` → `bash_expand_indirect()` | transpile_bash_mir.cpp |
| K7 | Create integration tests: `test/bash/case_toggle.sh` + `.txt`, `test/bash/indirect.sh` + `.txt` | new |
| K8 | Run GNU suite, update baseline | — |

### Phase L: Declare Formatter

**Estimated tests unblocked:** 7+ tests (assoc, quotearray, attr, appendop, nameref, cprint, func — partial)

| Step | Action | Files |
|------|--------|-------|
| L1 | Create `bash_declare.h` / `bash_declare.cpp` | new |
| L2 | Implement `bash_declare_format_value()` — proper escaping | bash_declare.cpp |
| L3 | Implement `bash_declare_format_array()` — `([0]="val" ...)` format | bash_declare.cpp |
| L4 | Implement `bash_declare_format_assoc()` — `([key]="val" ...)` format | bash_declare.cpp |
| L5 | Implement `bash_declare_print()` — full `declare [-flags] name=value` | bash_declare.cpp |
| L6 | Implement `bash_declare_print_by_attrs()` — filtered listing | bash_declare.cpp |
| L7 | Delegate existing `bash_declare_print_var()` to new module | bash_runtime.cpp |
| L8 | Create integration tests: `test/bash/declare_format.sh` + `.txt` | new |
| L9 | Run GNU suite, update baseline | — |

### Phase M: Process Substitution

**Estimated tests unblocked:** 3 tests (procsub, comsub2, posixpipe — partial)

| Step | Action | Files |
|------|--------|-------|
| M1 | Create `bash_procsub.h` / `bash_procsub.cpp` | new |
| M2 | Implement `bash_procsub_in()` — `<(cmd)` via pipe+fork | bash_procsub.cpp |
| M3 | Implement `bash_procsub_out()` — `>(cmd)` via pipe+fork | bash_procsub.cpp |
| M4 | Implement `bash_procsub_wait_all()` / cleanup | bash_procsub.cpp |
| M5 | Wire transpiler: detect `<(...)` / `>(...)` AST nodes → emit calls | transpile_bash_mir.cpp |
| M6 | Register in `sys_func_registry.c` | sys_func_registry.c |
| M7 | Create integration tests: `test/bash/procsub.sh` + `.txt` | new |
| M8 | Run GNU suite, update baseline | — |

### Phase N: Shopt/Set Display & Time Keyword

**Estimated tests unblocked:** 3+ tests (shopt, builtins, posixpipe — partial)

| Step | Action | Files |
|------|--------|-------|
| N1 | Implement `bash_shopt_print_all()` / `bash_shopt_print()` | bash_runtime.cpp |
| N2 | Implement `bash_set_print_all()` | bash_runtime.cpp |
| N3 | Implement `bash_time_format()` / `bash_time_command()` | bash_runtime.cpp |
| N4 | Wire transpiler: `shopt` with no args → `bash_shopt_print_all()` | transpile_bash_mir.cpp |
| N5 | Wire transpiler: `time` keyword → `bash_time_command()` | transpile_bash_mir.cpp |
| N6 | Create integration tests: `test/bash/shopt_display.sh` + `.txt` | new |
| N7 | Run GNU suite, update baseline | — |

### Phase O: Bug Fixes & Edge Cases

**Estimated tests unblocked:** 8+ tests (braces, herestr, heredoc, read, dstack, posixpat, redir, trap — partial)

| Step | Action | Files |
|------|--------|-------|
| O1 | Fix brace expansion backslash handling: `\{`, `\,` should be unescaped after expansion | bash_runtime.cpp |
| O2 | Fix here-string expansion: ensure full `<<<word` expansion pipeline works | bash_heredoc.cpp |
| O3 | Fix `read -d delim` / `read -t timeout` edge cases | bash_runtime.cpp |
| O4 | Fix `pushd`/`popd`/`dirs` error messages and edge case handling | bash_runtime.cpp |
| O5 | Fix glob pattern bracket expression edge cases (dangling `\`, char classes) | bash_pattern.cpp |
| O6 | Fix `readonly` enforcement across function scope boundaries | bash_runtime.cpp |
| O7 | Fix variable-name fd redirection `{var}>file` in transpiler | transpile_bash_mir.cpp |
| O8 | Run GNU suite, update baseline | — |

---

## 5. Architecture Diagram (Updated)

```
                          ┌─────────────────────────────┐
                          │     Tree-sitter Parser       │
                          │   (grammar.js → parser.c)    │
                          └──────────┬──────────────────┘
                                     │ CST
                          ┌──────────▼──────────────────┐
                          │    build_bash_ast.cpp        │
                          │  (CST → typed Bash AST)      │
                          └──────────┬──────────────────┘
                                     │ BashAstNode tree
                          ┌──────────▼──────────────────┐
                          │  transpile_bash_mir.cpp      │
                          │  (thin MIR code generation)  │
                          └──────────┬──────────────────┘
                                     │ MIR IR → JIT
                                     │
    ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┐
    │      │      │      │      │      │      │      │
  Mod1   Mod2   Mod3   Mod4   Mod5   Mod6   Mod7   Mod8
  Word   Pat    Var    Error  Printf Arith  Redir  Exec
  Exp    Match  Attrs  Fmt    Engine Eval   & I/O  Engine
  (P3)   (P3)   (P3)   (P3)   (P3)   (P3)   (P3)   (P4)
    │      │      │      │
  Mod9   Mod10  Mod11  Mod12        Mod13      Mod14
  Built  Cond   Here   ProcSub     Locale     Declare
  Ext    Engine doc    Engine      & Quote    Formatter
  (P4)   (P4)   (P4)   (NEW-P5)   (NEW-P5)   (NEW-P5)
```

**Modules 1–7:** Phase 3 (existing).
**Modules 8–11:** Phase 4 (existing).
**Modules 12–14:** Phase 5 (this proposal). Plus extensions 14A–14D to existing modules.

---

## 6. Transpiler Changes Summary

| AST Node / Construct | Current Behavior | Phase 5 Change |
|----------------------|-----------------|----------------|
| `$"hello"` | Passed as literal `$"hello"` | Emit `bash_locale_expand(body)` |
| `${var~}` | Not handled (literal output) | Emit `bash_expand_toggle_first(var)` |
| `${var~~}` | Not handled | Emit `bash_expand_toggle_all(var)` |
| `${!var}` | Returns variable name, not value | Emit `bash_expand_indirect(var)` |
| `${!prefix@}` | Not handled | Emit `bash_expand_prefix_names(prefix)` |
| `${!arr[@]}` | Not handled | Emit `bash_expand_array_keys(arr)` |
| `<(command)` | "No such file or directory" | Emit `bash_procsub_in(cmd)` |
| `>(command)` | Not handled | Emit `bash_procsub_out(cmd)` |
| `declare -p var` | Wrong format | Emit `bash_declare_print(var)` |
| `declare -f func` | Incomplete | Emit `bash_declare_print_func(func)` |
| `shopt` (no args) | No output | Emit `bash_shopt_print_all()` |
| `time command` | macOS raw time format | Emit `bash_time_command(cmd)` |
| `\{`, `\,` in braces | Not unescaped | Fix in `bash_expand_brace()` |
| `{var}>file` | Not handled in redirections | Emit `bash_exec_redir_varfd()` |

**Transpiler LOC estimate:** ~120 lines of new dispatch code.

---

## 7. Integration Test Plan

| Script | Scenarios | Tests Validated |
|--------|-----------|----------------|
| `test/bash/locale_string.sh` + `.txt` | `$"hello"`, `$"with $var"`, nested, empty | Module 13 |
| `test/bash/case_toggle.sh` + `.txt` | `${x~}`, `${x~~}`, with patterns, empty string | Extension 14A |
| `test/bash/indirect.sh` + `.txt` | `${!var}`, `${!prefix@}`, `${!arr[@]}`, nested indirect | Extension 14B |
| `test/bash/declare_format.sh` + `.txt` | `declare -p` for string/int/array/assoc/nameref/readonly/export | Module 14 |
| `test/bash/procsub.sh` + `.txt` | `diff <(...) <(...)`, `cat <(echo hi)`, `>(tee file)` | Module 12 |
| `test/bash/shopt_display.sh` + `.txt` | `shopt`, `shopt -s`, `shopt -u`, `shopt optname` | Extension 14C |

---

## 8. Projected Impact

| Phase | Before | After (Est.) | Delta | Key Tests Progressed |
|-------|--------|--------------|-------|---------------------|
| J: Locale & Quoting | 12/82 | 17–19/82 | +5–7 | nquote, nquote1, nquote2, nquote3, nquote5, iquote, quote |
| K: Case Toggle & Indirect | 17/82 | 19–21/82 | +2–3 | casemod, posixpat, posix2 |
| L: Declare Formatter | 19/82 | 23–26/82 | +4–5 | assoc, appendop, attr, quotearray, nameref |
| M: Process Substitution | 23/82 | 25–27/82 | +2–3 | procsub, comsub2 |
| N: Shopt/Set/Time | 25/82 | 27–29/82 | +2 | shopt, posixpipe |
| O: Bug Fixes | 27/82 | 30–35/82 | +3–6 | braces, herestr, dstack, vredir, redir, heredoc |

**Conservative target:** 30/82 (37%)
**Optimistic target:** 35/82 (43%)

### Tests That Remain Hard (Deferred)

| Test | Reason |
|------|--------|
| arith_for | Infinite output (2.3M+ diff lines) |
| complete | Programmable completion — interactive |
| history, histexp | Interactive history features |
| jobs | Full job control (process groups, terminal control) |
| coproc | Bidirectional coprocess pipes |
| invocation | Exhaustive CLI option testing |
| intl | Requires locale support infrastructure |
| alias | Requires parse-stage alias expansion (pre-parse rewrite) |
| exportfunc | Requires function serialization to environment variables |

---

## 9. Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Process substitution fd leaks | Medium | Medium | Track all fds in static array, cleanup on signal/exit |
| `declare -p` format has many edge cases | High | Medium | Test against GNU bash interactively; implement core format first |
| `$"..."` fix doesn't fully fix nquote tests (other issues) | Medium | Low | nquote tests have multiple issues; locale fix is one layer |
| Function body display too complex | High | Medium | Defer function display; focus on variable display first |
| `time` format differs between GNU bash and macOS | Low | Low | Use `getrusage()` for portable timing |
| `${!var}` fix breaks existing nameref behavior | Low | High | Indirect and nameref are distinct; test both paths carefully |

---

## 10. Success Criteria

| Metric | Before | Target |
|--------|--------|--------|
| Integration tests | 37/37 | 37/37 + 6 new (no regressions) |
| GNU official tests | 12/82 | 30–35/82 |
| `gnu_baseline.json` entries | 12 | 30+ |
| New C module LOC | 0 | ~800–1200 |
| New module files | 0 | 6 (3 headers + 3 implementations) |
| Transpiler new LOC | 0 | ~120 (dispatch only) |

---

## 11. Priority Order

If time is limited, implement in this order for maximum impact:

1. **Phase J (Locale/Quoting)** — Highest test count (5–7 tests), low complexity
2. **Phase K (Case Toggle/Indirect)** — Medium test count (2–3 tests), low complexity
3. **Phase L (Declare Formatter)** — High test count (4–5 tests), medium complexity
4. **Phase O (Bug Fixes)** — Aggregate many small fixes (3–6 tests)
5. **Phase N (Shopt/Set/Time)** — Low test count but easy wins
6. **Phase M (Process Substitution)** — Important feature but complex (fork/pipe/fd management)

---

## 12. Progress Log

_(To be filled during implementation)_
