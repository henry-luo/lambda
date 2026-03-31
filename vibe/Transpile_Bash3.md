# Bash Transpiler — Structural Enhancement Proposal

## Executive Summary

Lambda's bash transpiler currently passes **4 of 82** GNU official tests (dbg-support2, dstack2, invert, strip). The 137+ runtime C functions and tree-sitter-to-MIR pipeline are architecturally sound, but the failure patterns reveal that many features are implemented only to the depth of the hand-written baseline tests — not to the depth GNU Bash requires for conformance.

This proposal takes a **bottom-up approach**: survey what the GNU tests demand, design robust C modules to handle each feature area, unit-test those modules for correctness, then wire the transpiler to call them. Instead of chasing tests one at a time, we build solid foundations that make many tests pass simultaneously.

**Baseline status:** 31/31 passing (32 total, 1 skipped).

---

## 1. Analysis: What the GNU Tests Actually Demand

### 1.1 Test Survey Results (82 tests)

| Category | Tests | Count |
|----------|-------|-------|
| **PASS** | dbg-support2, dstack2, invert, strip | 4 |
| **Near-pass (≤10 diff lines)** | ifs-posix(2), tilde(2), tilde2(6), ifs(9), dynvar(10) | 5 |
| **Low diff (11–30)** | coproc(11), printf(11), exportfunc(15), case(18), comsub-eof(19), nquote4(19), posix2(19), appendop(20), parser(21), lastpipe(22), rsh(24), extglob3(25) | 12 |
| **Medium diff (31–100)** | posixpat(31), procsub(32), herestr(35), posixpipe(35), attr(38), posixexp2(41), casemod(53), nquote3(61), alias(62), intl(63), cprint(68), getopts(69), set-x(74), set-e(75), dstack(77), nquote2(77), nquote5(87), comsub-posix(91), braces(92), iquote(92), comsub(93), nquote(93) | 22 |
| **High diff (100–500)** | glob-bracket(104), rhs-exp(105), invocation(106), read(106), vredir(120), type(128), jobs(130), nquote1(134), trap(140), quote(147), quotearray(149), mapfile(155), heredoc(163), redir(176), cond(190), comsub2(198), extglob(216), func(249), glob(261), dbg-support(262), histexp(265), posixexp(308), shopt(315), more-exp(335), test(339), errors(345), arith(368), varenv(371), history(387), complete(388), builtins(485), exp(547), nameref(582), globstar(589), new-exp(811) | 35 |
| **Crash/timeout** | array(0), assoc(0), extglob2(7.4M), arith-for(18.7M) | 4 |

### 1.2 Root Cause Patterns

Examining the failing tests reveals **seven recurring root causes** that account for the vast majority of failures:

#### RC1: Word Expansion Pipeline Gaps
Bash defines a strict word expansion order: brace expansion → tilde expansion → parameter/variable expansion → command substitution → arithmetic expansion → **word splitting** → pathname expansion → quote removal. Lambda's transpiler handles most of these individually but **does not implement a unified expansion pipeline**. This means:
- Word splitting on `$IFS` is missing entirely
- Quote removal doesn't propagate correctly through nested expansions
- Expansion results aren't re-split when unquoted

**Tests affected:** ifs, ifs-posix, quote, iquote, nquote, nquote1–5, exp, more-exp, new-exp, posixexp, posixexp2, rhs-exp, quotearray (~25 tests)

#### RC2: Pattern Matching Incompleteness
Case statements, `[[ x == pattern ]]`, and parameter expansion patterns (`${var/pattern/}`) all depend on a common glob/extglob matching engine. Current issues:
- `;&` and `;;&` case fallthrough operators not implemented
- Extended globs `?(pat)`, `*(pat)`, `+(pat)`, `@(pat)`, `!(pat)` not supported
- Bracket expression edge cases (`[:class:]`, `[=equiv=]`) incomplete
- Pattern matching in `[[ ]]` doesn't handle all quoting correctly

**Tests affected:** case, casemod, posixpat, extglob, extglob2, extglob3, glob, glob-bracket, globstar, cond (~10 tests)

#### RC3: Arithmetic Context Edge Cases
Core arithmetic works, but the GNU tests stress:
- Array subscript arithmetic (`a[i+1]`, `a[$((i+1))]`)
- Arithmetic in assignment context (`typeset -i x=4+5` → evaluates as `9`)
- Nested arithmetic in expansions (`${a[$(($i+1))]}`)
- Error messages that must exactly match bash format
- `arith-for.tests` exercise deeply nested loops that may cause infinite output

**Tests affected:** arith, arith-for, appendop, attr (~4 tests)

#### RC4: Declare/Typeset Attribute Semantics
`declare`/`typeset` with flags `-i`, `-l`, `-u`, `-r`, `-n`, `-a`, `-A` modify how variables behave on every subsequent assignment. Current implementation applies attributes at declaration but doesn't enforce them on later operations:
- `-i` should make every assignment arithmetic-evaluate the RHS
- `-l`/`-u` should case-convert on every assignment, not just at declaration
- `-n` (nameref) is unimplemented
- `-r` (readonly) enforcement is incomplete

**Tests affected:** attr, nameref, varenv, appendop, casemod (~5 tests)

#### RC5: Error Message Format Mismatch
Many GNU tests check exact error messages. Bash produces messages like:
```
bash: line 42: foo: readonly variable
bash: ${#: bad substitution
```
Lambda either doesn't produce errors where bash does, or produces differently-formatted messages.

**Tests affected:** errors, arith, array, assoc, redir, varenv (~6 tests)

#### RC6: Sub-script Execution Gaps
~40 of the 82 tests invoke sub-scripts via `${THIS_SH} ./something.sub`. These exercise:
- Correct `$0` propagation
- Environment inheritance for exported variables/functions
- Subshell variable isolation
- Exit code propagation across script boundaries
- Sourced file scope sharing

**Tests affected:** Nearly all medium-to-high diff tests that use `.sub` files

#### RC7: Missing Builtins & Features
Some tests exercise features that are simply absent:
- `mapfile`/`readarray` builtin
- Process substitution `<(cmd)`, `>(cmd)`
- Job control (`&`, `bg`, `fg`, `wait`, `jobs`)
- `export -f` (function export)
- `alias` expansion
- Complete `printf` format specifier coverage
- `exec` builtin
- Coprocesses (`coproc`)

**Tests affected:** mapfile, procsub, jobs, coproc, exportfunc, alias, printf, complete (~8 tests)

---

## 2. Proposed C Function Modules

The proposal is to build **seven C modules**, each independently unit-testable, that encapsulate the core behaviors GNU tests demand. The transpiler then becomes a thin code-generation layer that calls these modules.

### Module 1: Word Expansion Engine (`bash_expand.c`)

**Purpose:** Implements the full POSIX word expansion pipeline as a single reentrant function.

```c
// Main entry point: expand a word in the given context
// flags control which stages run (e.g., skip word-split inside double quotes)
Item bash_expand_word(Item word, int flags);

// Individual stages (callable independently for specific contexts)
Item bash_expand_braces(Item word);              // {a,b} → a b
Item bash_expand_tilde(Item word);               // ~ → /home/user  (already exists)
Item bash_expand_parameter(Item word, Item var_name, Item var_value);  // ${var...}
Item bash_expand_command_sub(Item word);          // $(cmd) (already exists)
Item bash_expand_arithmetic(Item expr);           // $(( )) (already exists)
Item bash_word_split(Item expanded, Item ifs);    // split on IFS
Item bash_pathname_expand(Item word);             // glob  (already exists)
Item bash_quote_remove(Item word);                // strip syntactic quotes

// Expansion flags
#define BASH_EXPAND_FULL       0xFF   // all stages
#define BASH_EXPAND_NO_SPLIT   0x01   // inside double quotes
#define BASH_EXPAND_NO_GLOB    0x02   // inside double quotes or set -f
#define BASH_EXPAND_ASSIGNMENT 0x04   // in assignment context (tilde after :)
#define BASH_EXPAND_PATTERN    0x08   // in pattern context (no quote removal of glob chars)
```

**Key design decisions:**
- **IFS word splitting** is implemented here as a stage, not bolted onto individual expansions
- **Quote tracking** is maintained throughout as a bitmask on each character (literal vs. syntactic)
- The function accepts an `Item` (Lambda string) and returns an `Item` (single string or array of words)
- The transpiler calls `bash_expand_word()` with appropriate flags depending on context (assignment, double-quoted, pattern, etc.)

**Unit tests:**
- IFS splitting: default IFS (space/tab/newline), custom IFS (`:` separator), empty IFS (no splitting), IFS with whitespace and non-whitespace chars
- Quote removal: single quotes, double quotes, escape chars, nested quotes
- Pipeline integration: `"$var"` should not word-split, `$var` should
- Edge cases from ifs-posix.tests: the 6,856 combinations of IFS ordering

### Module 2: Pattern Matching Engine (`bash_pattern.c`)

**Purpose:** Unified pattern matching for case statements, `[[ == ]]`, `${var/pat/}`, and pathname globbing.

```c
// Match a string against a bash pattern; returns 1 on match, 0 on no match
int bash_pattern_match(const char* string, const char* pattern, int flags);

// Flags
#define BASH_PAT_EXTGLOB    0x01   // enable ?(pat), *(pat), +(pat), @(pat), !(pat)
#define BASH_PAT_NOCASE     0x02   // case-insensitive (nocasematch)
#define BASH_PAT_DOTGLOB    0x04   // dot files matched by *
#define BASH_PAT_GLOBSTAR   0x08   // ** matches directories recursively
#define BASH_PAT_PERIOD     0x10   // leading period requires explicit match

// Bracket expression matching (character class)
int bash_bracket_match(char c, const char* bracket_expr);

// Extended glob sub-pattern matching
int bash_extglob_match(const char* string, const char* pattern, int flags);

// Brace expansion (combinatorial, not pattern matching but closely related)
Item bash_brace_expand(Item word);  // already exists, but needs edge case fixes
```

**Key design decisions:**
- Single matching function used by case, `[[ ]]`, parameter expansion, and glob
- Extglob patterns compiled to a simple NFA for `*(pat)`, `+(pat)` etc.
- Bracket expressions handle POSIX character classes (`[:alpha:]`, `[:digit:]`)
- Pattern matching respects current locale settings

**Unit tests:**
- Basic globs: `*`, `?`, `[abc]`, `[a-z]`, `[!a]`
- Extglob: `?(a|b)`, `*(a|b)`, `+(a|b)`, `@(a|b)`, `!(a|b)`
- Bracket POSIX classes: `[[:alpha:]]`, `[[:digit:]]`, `[[:space:]]`
- Case statement patterns: literal, glob, extglob, fallthrough
- Nested extglob: `*(?(a)b)` etc.
- Edge cases from posixpat.tests and extglob3.tests

### Module 3: Variable Attribute Engine (`bash_var_attrs.c`)

**Purpose:** Enforce variable attributes (`-i`, `-l`, `-u`, `-r`, `-n`) on every assignment, not just at declaration.

```c
// Attribute flags stored per-variable
#define BASH_ATTR_INTEGER   0x01   // -i: arithmetic evaluate on assign
#define BASH_ATTR_LOWER     0x02   // -l: lowercase on assign
#define BASH_ATTR_UPPER     0x04   // -u: uppercase on assign
#define BASH_ATTR_READONLY  0x08   // -r: reject reassignment
#define BASH_ATTR_EXPORT    0x10   // -x: export to environment
#define BASH_ATTR_NAMEREF   0x20   // -n: name reference (indirection)
#define BASH_ATTR_ARRAY     0x40   // -a: indexed array
#define BASH_ATTR_ASSOC     0x80   // -A: associative array
#define BASH_ATTR_TRACE     0x100  // -t: trace (DEBUG trap on function)

// Apply attributes to a value before storing
// This is called on EVERY assignment path (=, +=, read, etc.)
Item bash_apply_attrs(Item value, int attrs, Item var_name);

// Resolve a nameref chain: follow -n references to find the target variable
Item bash_resolve_nameref(Item var_name);

// Set/get/modify attributes for a variable
void bash_set_attrs(Item var_name, int attrs);
int  bash_get_attrs(Item var_name);
void bash_add_attrs(Item var_name, int add_flags);
void bash_remove_attrs(Item var_name, int remove_flags);

// Attribute-aware assignment (combines resolve + apply + store)
void bash_attr_assign(Item var_name, Item value);
void bash_attr_append(Item var_name, Item append_value);

// Error: write to readonly variable
// Returns false if assignment should be rejected
bool bash_check_readonly(Item var_name);
```

**Key design decisions:**
- Attributes stored in the scope hashmap alongside the value
- `bash_apply_attrs()` is called on every assignment path (transpiler emits it before storing)
- Nameref resolution is iterative with cycle detection (max 10 levels)
- `-i` attribute causes `bash_arith_eval_value()` on the RHS automatically
- Error messages match bash format: `bash: var_name: readonly variable`

**Unit tests:**
- `-i` attribute: `declare -i x; x=4+5` → 9; `x+=3` → 12
- `-l`/`-u`: `declare -l x="HELLO"` → "hello"; later `x="WORLD"` → "world"
- `-r`: `readonly x=5; x=6` → error message, value unchanged
- `-n`: `declare -n ref=x; ref=42` → x becomes 42
- Combined: `declare -il x; x="HELLO"` → 0 (integer of lowercase "hello")
- `+=` with `-i`: arithmetic add, not string concat
- Nameref cycles: `declare -n a=b; declare -n b=a` → error

### Module 4: Error Formatting Engine (`bash_errors.c`)

**Purpose:** Produce error messages that exactly match GNU Bash format for each error condition.

```c
// Standard error message format: "bash: [line N: ]context: message\n"
// Writes to stderr capture or stderr
void bash_error(const char* fmt, ...);
void bash_error_at(int lineno, const char* fmt, ...);

// Specific error producers (matching exact bash output)
void bash_err_readonly(const char* var_name);
void bash_err_bad_substitution(const char* expr);
void bash_err_unbound_variable(const char* var_name);
void bash_err_not_found(const char* cmd_name);
void bash_err_syntax(const char* token);
void bash_err_numeric_arg(const char* func, const char* arg);
void bash_err_invalid_option(const char* opt);
void bash_err_too_many_args(const char* cmd);
void bash_err_not_valid_identifier(const char* cmd, const char* name);
void bash_err_ambiguous_redirect(const char* target);
void bash_err_division_by_zero(void);

// Set the shell name used in error prefix (default: "bash")
void bash_set_shell_name(const char* name);
```

**Key design decisions:**
- All error messages go through these functions (single source of truth for format)
- Line numbers tracked and included when available
- Shell name configurable (some tests override `$0`)
- Errors written to stderr by default, but captured when in `$()` substitution context

**Unit tests:**
- Each error function produces exact bash-format output
- Line number inclusion when available
- Custom shell name propagation
- Error in command substitution: captured vs. printed

### Module 5: Printf Engine (`bash_printf.c`)

**Purpose:** Complete POSIX/Bash printf implementation with all format specifiers, escape sequences, and options.

```c
// Main printf builtin entry point
// Returns 0 on success, 1 on error
int bash_printf_main(int argc, Item* argv);

// Core format string processor
// output_func is called with each produced chunk (for -v redirection)
int bash_printf_format(const char* format, int argc, const char** argv,
                       void (*output_func)(const char* str, int len, void* ctx),
                       void* ctx);

// Escape sequence processor (shared with echo -e and $'...')
// Returns new string with escapes resolved
Item bash_process_escapes(Item input);

// Format specifiers supported:
//   %s  - string
//   %d  - signed decimal
//   %i  - signed integer (alias for %d)
//   %o  - unsigned octal
//   %u  - unsigned decimal
//   %x  - unsigned hex (lower)
//   %X  - unsigned hex (upper)
//   %f  - floating point
//   %e  - scientific notation
//   %g  - shorter of %f and %e
//   %c  - character
//   %b  - string with backslash escapes
//   %q  - shell-quoted output
//   %Q  - shell-quoted output (alternate)
//   %(fmt)T - strftime-based date/time
//   %%  - literal percent
//
// Modifiers: width, precision, -, +, 0, space, #
// Arguments reused cyclically if more format specs than args
// Leading ' or " on numeric args uses character value
```

**Key design decisions:**
- Reusable format processor (not just a builtin — also used for `$'...'` escape processing)
- `-v varname` option writes to variable instead of stdout
- Argument recycling: if format has 3 `%s` but only 1 arg, format repeats
- Single/double quote prefix on numbers: `printf '%d' "'A"` → 65
- `%(fmt)T` for date/time formatting via strftime
- Missing arguments treated as 0 (numeric) or "" (string)

**Unit tests:**
- All format specifiers with various widths and precisions
- Escape sequences: `\n`, `\t`, `\a`, `\\`, `\0NNN`, `\xHH`, `\uHHHH`
- `-v` variable assignment
- Argument recycling
- Error cases: invalid format, missing arguments
- `%q` quoting and round-trip safety

### Module 6: Arithmetic Evaluator (`bash_arith.c`)

**Purpose:** Complete arithmetic expression evaluator supporting all Bash arithmetic operators, variable references, and array subscripts.

Most of this already exists in the transpiler's MIR code generation. The enhancement is to provide a **runtime string-based evaluator** for contexts where arithmetic expressions are strings (e.g., `$(( $expr ))`, `let "expr"`, array subscripts, `declare -i` assignments).

```c
// Evaluate an arithmetic expression string, return the integer result
// This is needed for: let "expr", declare -i var="expr", arr[expr], $(( string ))
long long bash_arith_eval_string(const char* expr);

// Evaluate and assign: handles "var=expr", "var+=expr", "var++", "++var"
long long bash_arith_eval_assign(const char* expr);

// Array subscript evaluation (common case)
long long bash_arith_subscript(const char* expr);

// Error state
int bash_arith_get_error(void);
const char* bash_arith_get_error_msg(void);
```

**Key design decisions:**
- String-based evaluator complements the existing MIR-compiled arithmetic
- Needed for dynamic contexts where the expression is constructed at runtime
- Supports all C-style operators: `+`, `-`, `*`, `/`, `%`, `**`, `<<`, `>>`, `&`, `|`, `^`, `~`, `!`, `&&`, `||`, `?:`, `=`, `+=`, `-=`, etc.
- Variable references resolved through `bash_get_var()` dynamically
- Array subscript references: `arr[idx]` evaluated recursively
- Base conversion: `0x` hex, `0` octal, `N#val` arbitrary base

**Unit tests:**
- All operators with precedence verification
- Variable references in expressions
- Nested expressions
- Assignment operators within expressions
- Base conversion: `16#ff` → 255, `2#1010` → 10
- Error cases: division by zero, syntax errors, overflow

### Module 7: Redirection & I/O Engine (`bash_redir.c`)

**Purpose:** Complete file descriptor management for redirections, here-documents, and process substitution.

```c
// File descriptor redirection operations
int  bash_redir_open(int fd, const char* path, int flags);  // open and redirect
void bash_redir_close(int fd);
void bash_redir_dup(int oldfd, int newfd);    // N>&M
void bash_redir_save(int fd);                  // save for later restore
void bash_redir_restore_all(void);             // restore after command

// Variable-target redirections: {varname}>file
int  bash_redir_open_varfd(Item var_name, const char* path, int flags);

// Here-document support
int  bash_redir_heredoc(int fd, const char* content);

// Here-string support
int  bash_redir_herestring(int fd, Item value);

// Process substitution
Item bash_procsub_input(Item command);    // <(cmd) → /dev/fd/N
Item bash_procsub_output(Item command);   // >(cmd) → /dev/fd/N

// I/O context for compound commands (save/restore fd state)
void bash_io_push(void);
void bash_io_pop(void);
```

**Key design decisions:**
- File descriptor save/restore stack for compound commands
- Variable-target redirections (`{fd}>file`) assign fd number to variable
- Process substitution creates pipes and returns `/dev/fd/N` path
- Here-strings append newline (bash behavior)
- All redirections cleaned up automatically on scope exit

**Unit tests:**
- Basic redirections: `>`, `>>`, `<`, `2>`, `2>&1`
- Variable-target: `{fd}>file; echo hello >&$fd`
- Here-document with expansion and literal modes
- Here-strings
- Nested redirections with save/restore
- Process substitution (requires fork)

---

## 3. Implementation Phases

### Phase A: Foundation Modules (Highest Impact)

**Goal:** Build and unit-test the three modules that unblock the most tests.

| Module | Estimated Tests Unlocked | Priority |
|--------|--------------------------|----------|
| Word Expansion Engine | ~25 (all expansion/quoting tests) | **Critical** |
| Pattern Matching Engine | ~10 (case, glob, extglob tests) | **High** |
| Variable Attribute Engine | ~5 (attr, nameref, appendop) | **High** |

**Approach:**
1. Write `bash_expand.c` with IFS word splitting as the centerpiece
2. Write `bash_pattern.c` with extglob support
3. Write `bash_var_attrs.c` with nameref chains
4. Create GTest file `test/test_bash_expand.cpp` exercising IFS edge cases
5. Create GTest file `test/test_bash_pattern.cpp` exercising glob/extglob
6. Create GTest file `test/test_bash_attrs.cpp` exercising attribute enforcement
7. All tests must pass before wiring to transpiler

**Transpiler integration:**
- `bm_transpile_word()` calls `bash_expand_word()` with context-appropriate flags
- `bm_transpile_case_item()` calls `bash_pattern_match()` instead of hard-coded comparison
- `bm_transpile_assignment()` calls `bash_attr_assign()` instead of raw `bash_set_var()`

### Phase B: Error Formatting & Printf (Quick Wins)

**Goal:** Fix error message formatting and complete printf — these are self-contained and can flip several tests.

| Module | Estimated Tests Unlocked |
|--------|--------------------------|
| Error Formatting | ~6 (errors, plus partial fixes in arith, array, etc.) |
| Printf Engine | ~3 (printf, cprint, intl) |

**Approach:**
1. Audit all `bash_error()` calls in existing code — standardize format
2. Implement printf engine with full format specifier coverage
3. Wire `$'...'` escape processing through the shared escape engine
4. Test against GNU printf.tests and cprint.tests expected output

### Phase C: Arithmetic & Redirections (Depth)

**Goal:** Close remaining gaps in arithmetic evaluation and file I/O.

| Module | Estimated Tests Unlocked |
|--------|--------------------------|
| Arithmetic Evaluator | ~3 (arith, arith-for, appendop) |
| Redirection Engine | ~4 (redir, vredir, heredoc, herestr) |

**Approach:**
1. String-based arithmetic evaluator for `let`, `declare -i`, array subscripts
2. Variable-target redirections and fd save/restore
3. Process substitution (fork + pipe + /dev/fd)
4. Test against arith.tests and redir.tests

### Phase D: Transpiler Simplification

**Goal:** Refactor the transpiler to become a thin MIR code generator that delegates to the C modules.

**Current problem:** The transpiler (~2800 LOC) contains significant logic for variable resolution, expansion, and quoting that duplicates (imperfectly) what should be in the runtime. With the new C modules, the transpiler should:

1. **Emit word expansion calls** — instead of inline-expanding variables, emit `bash_expand_word(raw_word, flags)`
2. **Emit pattern match calls** — instead of generating comparison sequences for case/`[[ ]]`, emit `bash_pattern_match()`
3. **Emit attribute-aware assignment** — instead of raw `bash_set_var()`, emit `bash_attr_assign()`
4. **Remove duplicated logic** — the transpiler should not contain expansion or matching logic

This reduces the transpiler's complexity and makes it easier to add new features (they go in the C modules, not the code generator).

---

## 4. Unit Testing Strategy

Each C module gets a dedicated GTest file under `test/`:

| Test File | Tests | Focus |
|-----------|-------|-------|
| `test_bash_expand.cpp` | ~50 | IFS splitting, quote removal, expansion pipeline |
| `test_bash_pattern.cpp` | ~40 | Glob, extglob, bracket expressions, case match |
| `test_bash_attrs.cpp` | ~30 | Integer, lowercase, uppercase, readonly, nameref |
| `test_bash_errors.cpp` | ~20 | Error format strings, line numbers, shell name |
| `test_bash_printf.cpp` | ~40 | Format specifiers, escapes, -v, argument recycling |
| `test_bash_arith.cpp` | ~30 | All operators, variables, subscripts, base conversion |
| `test_bash_redir.cpp` | ~20 | FD management, save/restore, process substitution |

**Testing contract:**
- Each module's tests must pass 100% before integration
- Tests are added to `build_lambda_config.json` as **baseline** tests (not extended)
- Tests run with `make test-bash-baseline`
- Each test case is self-contained (no inter-test dependencies)

**Test derivation:** Unit test cases are derived directly from the GNU test scripts. For example, `ifs-posix.tests` defines 6,856 IFS splitting scenarios — a representative subset of these becomes `test_bash_expand.cpp` test cases.

---

## 5. Projected Impact

### Conservative Estimate

If all seven modules are implemented and integrated:

| Current | After Phase A | After Phase B | After Phase C+D |
|---------|--------------|---------------|-----------------|
| 4/82 PASS | 15–20 PASS | 20–25 PASS | 30–40 PASS |

### Near-Pass Tests Most Likely to Flip

| Test | Diff | Blocking Issue | Module |
|------|------|----------------|--------|
| ifs-posix | 2 | IFS word splitting | Word Expansion |
| tilde | 2 | `~root` resolves to current user instead of root's home | Special variables (minor) |
| tilde2 | 6 | POSIX-mode tilde differences | Word Expansion |
| ifs | 9 | IFS word splitting | Word Expansion |
| dynvar | 10 | Missing `$BASHPID`, `$EPOCHSECONDS` | Special variables (minor) |
| coproc | 11 | Coprocess support | Builtins |
| printf | 11 | Format specifier gaps | Printf |
| exportfunc | 15 | `export -f` function export | Builtins |
| case | 18 | `;&` and `;;&` operators | Pattern Matching |
| appendop | 20 | `-i` attribute + array append | Variable Attributes |
| posixpat | 31 | Bracket expression edge cases | Pattern Matching |

### Tests That Remain Hard

| Test | Why |
|------|-----|
| jobs, coproc | Require real job control (signals, process groups, terminal control) |
| complete | Programmable completion — interactive feature, not targeted |
| histexp, history | Interactive history expansion — not targeted |
| extglob2 | 7.4M diff lines — likely infinite loop or crash |
| arith-for | 18.7M diff lines — likely infinite loop in sub-scripts |

---

## 6. Architecture Diagram

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
                          │  (AST → MIR IR generation)   │
                          │                              │
                          │  THIN layer: emits calls to  │
                          │  runtime C modules below     │
                          └──────────┬──────────────────┘
                                     │ MIR IR → JIT compile → native
                                     │
          ┌──────────────────────────┼──────────────────────────────┐
          │                          │                              │
    ┌─────▼──────┐           ┌──────▼───────┐            ┌────────▼────────┐
    │   Module 1  │           │   Module 2    │            │   Module 3       │
    │   Word      │           │   Pattern     │            │   Variable       │
    │   Expansion │           │   Matching    │            │   Attributes     │
    │             │           │               │            │                  │
    │ • IFS split │           │ • glob        │            │ • -i integer     │
    │ • quote rm  │           │ • extglob     │            │ • -l/-u case     │
    │ • param exp │           │ • bracket     │            │ • -r readonly    │
    │ • tilde     │           │ • case match  │            │ • -n nameref     │
    └─────────────┘           └───────────────┘            └──────────────────┘
          │                          │                              │
    ┌─────▼──────┐           ┌──────▼───────┐            ┌────────▼────────┐
    │   Module 4  │           │   Module 5    │            │   Module 6       │
    │   Error     │           │   Printf      │            │   Arithmetic     │
    │   Formatting│           │   Engine       │            │   Evaluator      │
    │             │           │               │            │                  │
    │ • bash fmt  │           │ • %d %s %q    │            │ • string eval    │
    │ • line nums │           │ • escapes     │            │ • base convert   │
    │ • stderr    │           │ • -v var      │            │ • var deref      │
    └─────────────┘           └───────────────┘            └──────────────────┘
                                     │
                          ┌──────────▼──────────────────┐
                          │   Module 7: Redirection      │
                          │   & I/O Engine               │
                          │                              │
                          │ • FD save/restore             │
                          │ • {var}>file                  │
                          │ • process substitution        │
                          └──────────────────────────────┘
```

---

## 7. Key Design Principles

1. **C functions are the source of truth.** The transpiler generates code that calls C functions — it does not contain bash semantics itself. If a behavior is wrong, fix the C function, not the code generator.

2. **Unit test before integration.** Every C function is tested in isolation before the transpiler is wired to call it. This ensures bugs are caught at the module level, not discovered through 82-file diff chasing.

3. **Match GNU Bash output exactly.** The goal is byte-for-byte output match against `*.right` files. Error messages, whitespace, quoting — all must match. The error formatting module ensures this.

4. **Progressive enhancement.** Each module is independently valuable. Implementing Module 1 (Word Expansion) alone would flip ~10 tests. There's no big-bang integration — each module is wired in as it's ready.

5. **Reuse existing runtime.** The 137+ existing C functions are not thrown away. The new modules wrap and organize them. For example, `bash_expand_parameter()` calls the existing `bash_param_default()`, `bash_param_assign()`, etc. — but within a proper pipeline context.

6. **Stay within Lambda's C+ convention.** Use `Str`, `ArrayList`, `pool_calloc()`, `log_debug()` — not `std::string`, `std::vector`, `printf`.

---

## 8. Progress Log

### 2025-03-31: Preprocessor Array Fix + Baseline Recovery

**Baseline:** 31/31 passing (was 30/31 with `arrays` failing)

**GNU tests:** 4/82 passing (dbg-support2, dstack2, invert, strip)
- `dstack2` newly passing (was low-diff fail)
- `tilde` regressed from PASS to FAIL[2] — `~root` resolves to current user's home instead of root's

**Bugs fixed:**

1. **Preprocessor array literal corruption** (root cause of `arrays` baseline failure)
   - `preprocess_bash_source()` splits multi-assignment lines but didn't handle `(...)` array values
   - When scanning `arr=(alpha beta)`, it broke on the space inside parentheses, stripping it and producing `arr=(alphabeta)`
   - Fixed by adding `(` handling to copy array values verbatim with matched parentheses
   - Files: `transpile_bash_mir.cpp` (`preprocess_bash_source`)

2. **Tilde expansion applied to array literal values**
   - `bash_expand_tilde_assign()` was called on all non-string assignment values, including array literals
   - Added `BASH_AST_NODE_ARRAY_LITERAL` exclusion in assignment transpilation
   - Files: `transpile_bash_mir.cpp` (assignment tilde expansion guard)

3. **Defense-in-depth word splitting in AST builder**
   - Added safety net in `build_array()` to split word nodes containing unquoted spaces into multiple elements
   - Guards against any future tree-sitter misparsing of array contents
   - Files: `build_bash_ast.cpp` (`build_array`)

4. **`bash_var_append` integer arithmetic evaluation** (from prior session)
   - Changed from `strtol` to `bash_arith_eval_value` for `BASH_ATTR_INTEGER` variables
   - Enables `typeset -i b; b+=37` to do arithmetic addition instead of string concatenation
   - Files: `bash_runtime.cpp` (`bash_var_append`)

**Key discovery:** What was initially suspected as a tree-sitter-bash parser bug (array elements merged when followed by variable assignment) turned out to be caused by the preprocessor in `transpile_bash_mir.cpp`. The tree-sitter parser was parsing correctly — it was just receiving corrupted source text.
