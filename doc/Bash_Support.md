# Bash Support in Lambda

Lambda can compile and execute Bash scripts via JIT compilation. Bash source code is parsed with Tree-sitter, transformed into a typed AST, transpiled to MIR (Medium Intermediate Representation), and JIT-compiled to native machine code — no interpreter loop is involved.

```bash
./lambda.exe script.sh          # Run a Bash script
```

## Compilation Pipeline

```
  .sh source
      │
      ▼
  Tree-sitter parser (tree-sitter-bash grammar)
      │
      ▼
  Concrete Syntax Tree (CST)
      │
      ▼
  AST Builder (build_bash_ast.cpp)
      │
      ▼
  Typed Bash AST (BashAstNode tree)
      │
      ▼
  MIR Transpiler (transpile_bash_mir.cpp)  ← two-pass: functions first, then top-level
      │
      ▼
  MIR JIT Compilation → native execution
```

The transpiler makes two passes over the AST: the first pass emits all function definitions so that functions can be called before their lexical definition (matching Bash behavior), and the second pass emits top-level statements.

---

## Supported Features

### Variables

| Feature | Syntax | Status |
|---------|--------|--------|
| Assignment | `var=value` | ✅ |
| Expansion | `$var`, `${var}` | ✅ |
| Concatenation in strings | `"Hello, $name!"` | ✅ |
| Adjacent concatenation | `${name}Script` | ✅ |
| Reassignment | `name="new"` | ✅ |
| Unquoted expansion | `echo $x` | ✅ |
| Local variables | `local var=value` | ✅ |
| Export | `export var` / `export var=value` | ✅ |
| Unset | `unset var` | ✅ |

Bash variables are strings by default and are coerced to integers in arithmetic contexts (following standard `atoi` semantics).

### Strings

| Feature | Syntax | Status |
|---------|--------|--------|
| Double-quoted strings | `"hello $name"` | ✅ |
| Single-quoted strings (no expansion) | `'literal $text'` | ✅ |
| String concatenation via expansion | `"$first, $second!"` | ✅ |
| Multi-line strings | `msg="line one\nline two"` | ✅ |
| Whitespace preservation | `"  hello  world  "` | ✅ |
| Empty string | `""` | ✅ |

### Arithmetic Expansion

Arithmetic expressions inside `$(( ))` are fully supported with integer arithmetic.

| Operator | Syntax | Description |
|----------|--------|-------------|
| Addition | `$(( a + b ))` | ✅ |
| Subtraction | `$(( a - b ))` | ✅ |
| Multiplication | `$(( a * b ))` | ✅ |
| Division | `$(( a / b ))` | Integer division ✅ |
| Modulo | `$(( a % b ))` | ✅ |
| Exponentiation | `$(( 2 ** 8 ))` | ✅ |
| Unary negation | `$(( -a ))` | ✅ |
| Grouping | `$(( (3 + 4) * 2 ))` | ✅ |
| Variable references | `$(( a + b ))` | No `$` needed ✅ |
| String-to-int coercion | `$(( num_str + 8 ))` | ✅ |
| Compound expressions | `$(( 1 + 2 + 3 + 4 + 5 ))` | ✅ |

**Bitwise operators:**

| Operator | Syntax | Description |
|----------|--------|-------------|
| AND | `$(( a & b ))` | ✅ |
| OR | `$(( a \| b ))` | ✅ |
| XOR | `$(( a ^ b ))` | ✅ |
| NOT | `$(( ~a ))` | ✅ |
| Left shift | `$(( a << n ))` | ✅ |
| Right shift | `$(( a >> n ))` | ✅ |

**Comparison operators (arithmetic context):**

| Operator | Syntax |
|----------|--------|
| Equal | `$(( a == b ))` |
| Not equal | `$(( a != b ))` |
| Less than | `$(( a < b ))` |
| Less or equal | `$(( a <= b ))` |
| Greater than | `$(( a > b ))` |
| Greater or equal | `$(( a >= b ))` |

**Assignment operators (arithmetic context):**

| Operator | Syntax |
|----------|--------|
| Assign | `$(( a = 5 ))` |
| Add-assign | `$(( a += 1 ))` |
| Sub-assign | `$(( a -= 1 ))` |
| Mul-assign | `$(( a *= 2 ))` |
| Div-assign | `$(( a /= 2 ))` |
| Mod-assign | `$(( a %= 3 ))` |
| Increment | `$(( a++ ))`, `$(( ++a ))` |
| Decrement | `$(( a-- ))`, `$(( --a ))` |

**Ternary operator:**
```bash
$(( a > b ? a : b ))
```

### Parameter Expansion

| Feature | Syntax | Description |
|---------|--------|-------------|
| Default value | `${var:-default}` | Use default if unset/empty |
| Assign default | `${var:=default}` | Assign and use default if unset/empty |
| Alternate value | `${var:+alt}` | Use alt if set and non-empty |
| Error if unset | `${var:?msg}` | Error with msg if unset/empty |
| String length | `${#var}` | Length of value |
| Prefix removal (shortest) | `${var#pattern}` | Remove shortest prefix match |
| Prefix removal (longest) | `${var##pattern}` | Remove longest prefix match |
| Suffix removal (shortest) | `${var%pattern}` | Remove shortest suffix match |
| Suffix removal (longest) | `${var%%pattern}` | Remove longest suffix match |
| Replace first | `${var/pattern/string}` | Replace first occurrence |
| Replace all | `${var//pattern/string}` | Replace all occurrences |
| Substring | `${var:offset:length}` | Extract substring |
| Uppercase first | `${var^}` | Uppercase first character |
| Uppercase all | `${var^^}` | Uppercase all characters |
| Lowercase first | `${var,}` | Lowercase first character |
| Lowercase all | `${var,,}` | Lowercase all characters |
| Indirect expansion | `${!var}` | Expand variable named by var's value |

### Control Flow

#### If / Elif / Else

```bash
if [ $x -gt 5 ]; then
    echo "big"
elif [ $x -gt 0 ]; then
    echo "positive"
else
    echo "non-positive"
fi
```

Supports:
- Simple `if`/`fi`
- `if`/`else`/`fi`
- `if`/`elif`/`else`/`fi` chains
- Nested `if` statements
- Negation with `!`

#### For Loops

**For-in loop:**
```bash
for item in apple banana cherry; do
    echo "$item"
done
```

**C-style for loop:**
```bash
for (( i=0; i<5; i++ )); do
    echo "count: $i"
done
```

Supports:
- Plain word lists
- Quoted strings in word lists
- Nested for loops
- Accumulator patterns

#### While / Until Loops

```bash
while [ $i -lt 5 ]; do
    echo "$i"
    i=$(( i + 1 ))
done

until [ $count -le 0 ]; do
    echo "countdown: $count"
    count=$(( count - 1 ))
done
```

Supports:
- `while` loops with test conditions
- `until` loops (inverted while)
- `while true` infinite loops
- `break` and `continue` with nesting depth

#### Case Statement

```bash
case $lang in
    bash)
        echo "Shell"
        ;;
    python|ruby)
        echo "Scripting"
        ;;
    *)
        echo "Unknown"
        ;;
esac
```

Supports:
- Literal patterns
- OR patterns (`pat1|pat2`)
- Wildcard default (`*`)
- Numeric and string matching

### Functions

```bash
# Parenthesis syntax
greet() {
    echo "Hello from function"
}

# function keyword syntax
function say_hello {
    echo "hello $1"
}
```

Supports:
- Both `name()` and `function name` definition syntax
- Positional parameters (`$1`, `$2`, ...)
- `local` variable declarations
- `return` with exit codes
- Recursive functions (e.g., factorial)
- Functions calling other functions
- Command substitution to capture function output

### Arrays (Indexed)

| Feature | Syntax | Status |
|---------|--------|--------|
| Declaration | `arr=(a b c d)` | ✅ |
| Element access | `${arr[0]}` | ✅ |
| Set element | `arr[1]="BETA"` | ✅ |
| Array length | `${#arr[@]}` | ✅ |
| All elements | `${arr[@]}` | ✅ |
| Append | `arr+=(value)` | ✅ |
| Iterate | `for n in "${arr[@]}"` | ✅ |
| Slice | `${arr[@]:offset:length}` | ✅ |
| Unset element | `unset 'arr[i]'` | ✅ |

### Test Expressions

#### `[ ]` (test command)

| Operator | Type | Description |
|----------|------|-------------|
| `-eq` | Numeric | Equal |
| `-ne` | Numeric | Not equal |
| `-gt` | Numeric | Greater than |
| `-ge` | Numeric | Greater or equal |
| `-lt` | Numeric | Less than |
| `-le` | Numeric | Less or equal |
| `=` | String | Equal |
| `!=` | String | Not equal |
| `-z` | String | Empty |
| `-n` | String | Non-empty |
| `!` | Logical | Negation |

#### `[[ ]]` (extended test)

All `[ ]` operators plus:

| Operator | Type | Description |
|----------|------|-------------|
| `==` | String | Equal (also supports glob patterns) |
| `!=` | String | Not equal |
| `<` | String | Less than (lexicographic) |
| `>` | String | Greater than (lexicographic) |
| `=~` | String | Regex match |
| `==` with `*`/`?` | Pattern | Glob matching |
| `&&` | Logical | AND (inside `[[ ]]`) |
| `\|\|` | Logical | OR (inside `[[ ]]`) |
| `!` | Logical | NOT |

**File test operators** are defined in the AST (`-f`, `-d`, `-e`, `-r`, `-w`, `-x`, `-s`, `-L`) but are not yet tested.

### Command Substitution

```bash
result=$(echo "hello")
inner=$(echo $(echo "nested"))
echo "Hello, $(echo $user)!"
```

Supports:
- `$(command)` syntax
- Nested command substitution
- Command substitution within double-quoted strings
- Capture of stdout (trailing newline stripped, matching Bash behavior)
- Stack-based capture (up to 32 nesting levels)

### Subshells

```bash
x=10
( x=20; echo "inside: $x" )    # prints 20
echo "outside: $x"              # prints 10
```

Subshells execute in an isolated scope — variable modifications do not affect the parent scope.

### Pipelines

```bash
cmd1 | cmd2 | cmd3
```

Pipeline support is present. Commands in a pipeline are executed sequentially with stdout capture and passing between stages.

### List Operators

| Operator | Syntax | Description |
|----------|--------|-------------|
| AND | `cmd1 && cmd2` | Execute cmd2 only if cmd1 succeeds |
| OR | `cmd1 \|\| cmd2` | Execute cmd2 only if cmd1 fails |
| Sequential | `cmd1 ; cmd2` | Execute both |
| Chained | `true && false \|\| echo fallback` | Left-to-right evaluation |

### Here-Documents and Here-Strings

```bash
# Here-document with variable expansion
cat <<EOF
Hello, $name!
Version: $(echo "1.0")
EOF

# Here-document with no expansion (quoted delimiter)
cat <<'EOF'
No expansion: $name
EOF

# Here-string
cat <<< "single line here-string"
cat <<< "$variable"
```

Supports:
- Unquoted delimiters (with variable and command expansion)
- Quoted delimiters (no expansion)
- Here-strings (`<<<`)

### Redirections

The AST supports redirect nodes with these modes:

| Mode | Syntax | Status |
|------|--------|--------|
| Read | `< file` | Parsed |
| Write | `> file` | Parsed |
| Append | `>> file` | Parsed |
| Duplicate | `>&` | Parsed |
| Heredoc | `<<` | ✅ Fully working |
| Here-string | `<<<` | ✅ Fully working |

File-based redirections (`<`, `>`, `>>`) are parsed in the AST but actual file I/O redirection is not yet implemented at runtime.

### Special Variables

| Variable | Description | Status |
|----------|-----------|--------|
| `$?` | Exit code of last command | ✅ |
| `$#` | Number of positional parameters | ✅ |
| `$@` | All positional parameters (separate words) | ✅ |
| `$*` | All positional parameters (single string) | ✅ |
| `$$` | Process ID | ✅ (returns simulated PID) |
| `$!` | Last background process PID | ✅ (defined) |
| `$-` | Shell option flags | ✅ (defined) |
| `$0` | Script name | ✅ |
| `$1`–`$9` | Positional parameters | ✅ |

### Builtin Commands

| Command | Description | Flags/Notes |
|---------|-------------|-------------|
| `echo` | Print to stdout | `-n` (no newline), `-e` (escape sequences) |
| `printf` | Formatted output | `%s`, `%d` format specifiers |
| `test` / `[ ]` | Conditional test | See Test Expressions |
| `true` | Return success (exit 0) | |
| `false` | Return failure (exit 1) | |
| `exit` | Exit script with code | Code masked to 0–255 |
| `return` | Return from function | Sets `$?` |
| `read` | Read line from stdin | Reads into variable |
| `shift` | Shift positional parameters | Optional count argument |
| `local` | Declare local variable | With optional value |
| `export` | Mark variable for export | With optional value |
| `unset` | Remove variable or array element | Supports `unset 'arr[i]'` |
| `cd` | Change directory | Falls back to `$HOME` with no arg |
| `pwd` | Print working directory | Uses `getcwd` |

### Scope Management

- **Global scope**: Default variable scope
- **Function scope**: `local` creates function-scoped variables; `bash_scope_push()`/`bash_scope_pop()` manage stack frames
- **Subshell scope**: `( commands )` operates in an isolated snapshot; modifications don't leak to parent
- **Positional parameter stack**: `bash_push_positional()`/`bash_pop_positional()` save/restore `$1`, `$2`, ... across function calls

### Loop Control

| Statement | Description |
|-----------|-------------|
| `break` | Exit innermost loop |
| `break N` | Exit N levels of nested loops |
| `continue` | Skip to next iteration |
| `continue N` | Continue in N-th enclosing loop |

---

## Test Coverage

All 17 test suites pass:

| Test Script | Features Covered |
|-------------|-----------------|
| `echo_basic.sh` | Basic echo output, empty lines |
| `variables.sh` | Assignment, expansion, `${}`, reassignment, concatenation |
| `arithmetic.sh` | `$(( ))`, all arithmetic operators, variables, nesting, negatives |
| `string_ops.sh` | String comparison, `${#var}`, concatenation, multi-line, `-z`/`-n`, quoting |
| `param_expansion.sh` | `:-`, `:=`, `:+`, `${#}`, `#`, `##`, `%`, `%%`, `/`, `//`, `:off:len` |
| `if_elif_else.sh` | `if`/`elif`/`else`, nesting, negation, string/numeric comparison |
| `for_loop.sh` | For-in, quoted items, C-style `for((;;))`, nesting, accumulators |
| `while_loop.sh` | `while`, `until`, `break`, `continue`, accumulators |
| `case_statement.sh` | Literal patterns, OR patterns, wildcard default, numeric case |
| `functions.sh` | Both def syntaxes, params, `local`, `return`, recursion, mutual calls |
| `arrays.sh` | Declaration, access, modify, append, length, iterate, slice, unset |
| `subshell.sh` | `$()` capture, nested substitution, subshell scope isolation |
| `special_vars.sh` | `$#`, `$1`–`$3`, `$@`, `$*`, `$?`, `shift`, default values |
| `exit_codes.sh` | `true`/`false`, `return`, `$?`, `&&`, `\|\|`, chaining |
| `test_expr.sh` | `[[ ]]` numeric/string comparisons, `&&`/`\|\|`/`!`, `=~` regex, glob |
| `heredoc.sh` | `<<EOF`, variable expansion, quoted delimiters, `<<<` here-strings |
| `case_conversion.sh` | `${var^}`, `${var^^}`, `${var,}`, `${var,,}` |

---

## Missing / Unsupported Features

### Not Implemented

| Feature | Description |
|---------|-------------|
| **Associative arrays** | `declare -A map; map[key]=value` |
| **`declare` / `typeset`** | Type declaration builtins (`declare -i`, `declare -a`, etc.) |
| **`select` statement** | `select item in list; do ...; done` menu construct |
| **Coprocesses** | `coproc` command |
| **Process substitution** | `<(cmd)` and `>(cmd)` |
| **Job control** | `bg`, `fg`, `jobs`, `wait`, `kill`, `&` background execution |
| **Signal handling** | `trap` command |
| **`source` / `.`** | Source/include another script |
| **`eval`** | Dynamic evaluation |
| **`exec`** | Replace shell process |
| **`set`** | Shell option configuration (`set -e`, `set -u`, etc.) |
| **`getopts`** | Option parsing |
| **`let`** | Arithmetic evaluation (alternative to `$(( ))`) |
| **`(( ))` as command** | Arithmetic evaluation without `$` (as statement, not expansion) |
| **Brace expansion** | `{1..10}`, `{a,b,c}` |
| **Tilde expansion** | `~` → `$HOME` |
| **Globbing** | `*.txt`, `?`, `[a-z]` as filename expansion (glob AST node exists but no fs matching) |
| **`$'...'` strings** | ANSI-C quoting (`$'\n'`, `$'\t'`) |
| **Backtick substitution** | `` `command` `` (only `$(command)` is supported) |
| **File redirections** | `> file`, `>> file`, `< file` (parsed but not executed — no file I/O at runtime) |
| **File test operators** | `-f`, `-d`, `-e`, etc. (defined in AST but not tested against actual filesystem) |
| **Here-doc indentation** | `<<-EOF` (strip leading tabs) |
| **Arithmetic `&&`/`\|\|`** | Short-circuit evaluation in `$(( ))` (currently evaluates both sides) |
| **Real pipe I/O** | Pipeline commands run sequentially, not with fd bridging |
| **External commands** | Running system binaries (only builtins are supported) |
| **`mapfile` / `readarray`** | Read lines into array |
| **`printf -v`** | Print into variable |
| **Multiple assignment** | `a=1 b=2 c=3` on one line (prefix assignments without command) |
| **`IFS`** | Input field separator customization |
| **Arithmetic in `[ ]`** | `[ $((a+b)) -gt 5 ]` (arithmetic inside test — may partially work) |

### Partial / Limited

| Feature | Limitation |
|---------|-----------|
| **Pipelines** | Commands execute sequentially; stdout is captured and passed, but not via real Unix pipe file descriptors |
| **`echo -e`** | Escape sequences are supported but coverage of all sequences may be limited |
| **`printf`** | Only `%s` and `%d` format specifiers |
| **`read`** | Basic single-variable read from stdin; no `-p`, `-a`, `-t`, `-r` flags |
| **`$$`, `$!`, `$-`** | Defined but return simulated/static values, not real process data |
| **File tests** | AST supports `-f`, `-d`, `-e`, `-r`, `-w`, `-x`, `-s`, `-L` but no filesystem integration |

---

## Architecture Notes

### Source Files

| File | Role |
|------|------|
| `lambda/bash/bash_ast.hpp` | AST node types, operator enums, struct definitions |
| `lambda/bash/build_bash_ast.cpp` | Tree-sitter CST → Bash AST builder |
| `lambda/bash/transpile_bash_mir.cpp` | Bash AST → MIR code generation |
| `lambda/bash/bash_runtime.h` | C API for all runtime functions (callable from JIT) |
| `lambda/bash/bash_runtime.cpp` | Runtime function implementations |
| `lambda/bash/bash_builtins.cpp` | Builtin command implementations |
| `lambda/bash/bash_scope.cpp` | Variable scope and positional parameter management |
| `lambda/bash/bash_transpiler.hpp` | Transpiler class header |

### Runtime Data Model

All Bash values are represented as Lambda `Item` (64-bit tagged values). Bash's string-first semantics are layered on top:

- **Variables** are stored as Lambda strings by default
- **Arithmetic contexts** coerce strings to integers via `bash_to_int()` (atoi semantics)
- **Exit codes** are integers 0–255, with 0 = success; mapped to Lambda booleans for control flow via `bash_exit_code()` / `bash_from_exit_code()`
- **Arrays** use Lambda's container types, managed through `bash_array_*()` functions
- **Truthiness** follows Bash rules: non-empty strings are truthy, empty strings and unset variables are falsy

### Command Substitution Capture

Command substitution (`$(...)`) uses a stack-based stdout capture mechanism:
1. `bash_begin_capture()` pushes a new capture buffer
2. All `bash_write_stdout()` / `bash_raw_write()` calls go to the capture buffer instead of real stdout
3. `bash_end_capture()` pops the buffer and returns the captured string with trailing newline stripped

This supports nesting up to 32 levels deep.
