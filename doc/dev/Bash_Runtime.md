# LambdaBash Runtime — Design Proposal

## Overview

LambdaBash is a proposed embedded Bash engine for the Lambda runtime. Following the same architecture as LambdaJS and LambdaPy, it would compile Bash scripts to native machine code through the Tree-sitter → AST → MIR JIT pipeline, reusing Lambda's type system, memory management, and shared runtime infrastructure. Bash programs would execute within the same `Item`-based runtime as Lambda, JS, and Python scripts.

### Motivation

1. **Shell scripting is ubiquitous** — Bash is the default automation language on Linux/macOS. Embedding it in Lambda enables data processing pipelines that mix shell semantics with Lambda's richer type system.
2. **Performance** — JIT-compiled Bash eliminates interpreter overhead for compute-heavy shell scripts (loops, string processing, arithmetic).
3. **Unified toolchain** — A single `lambda.exe` binary can run `.ls`, `.js`, `.py`, and `.sh` scripts with zero-copy data interchange.
4. **Lambda interop** — Bash scripts gain access to Lambda's JSON/XML/YAML/CSV parsers, formatters, and the Radiant layout engine without shelling out to external tools.

### Design Goals

1. **Unified runtime** — Bash values are Lambda `Item` values. No conversion boundaries.
2. **Near-native performance** — MIR JIT compilation to native machine code.
3. **Bash semantics** — Word splitting, globbing, parameter expansion, pipelines, redirections, subshells, exit codes.
4. **Reuse over reimplementation** — String interning, GC heap, I/O parsers, formatters all delegate to existing Lambda subsystems.

### Non-Goals (Phase 1)

- Full POSIX compliance (focus on Bash 5.x subset)
- Interactive mode / job control (`fg`, `bg`, `jobs`)
- Signal trapping (`trap`)
- `eval` / dynamic code generation
- Networking builtins (`/dev/tcp`)
- Process substitution (`<(cmd)`)

---

## 1. Architecture

### 1.1 Compilation Pipeline

```
Bash Source (.sh)
    │
    ▼
Tree-sitter Parser       (tree-sitter-bash grammar)
    │
    ▼
Bash AST Builder          (build_bash_ast.cpp → typed BashAstNode tree)
    │
    ▼
MIR Transpiler            (transpile_bash_mir.cpp → MIR IR instructions)
    │
    ▼
MIR JIT Compiler          (MIR_link + MIR_gen → native machine code)
    │
    ▼
Execution                 (bash_main() function pointer call → Item result)
```

### 1.2 Unified Runtime Architecture

LambdaBash shares the same runtime layer as Lambda, LambdaJS, and LambdaPy:

```
┌──────────────────────────────────────────────────────────────────┐
│                      CLI (main.cpp)                              │
│              ./lambda.exe bash script.sh                         │
├──────────┬──────────┬──────────┬─────────────────────────────────┤
│  Lambda  │    JS    │  Python  │          Bash Path              │
│  (.ls)   │  (.js)   │  (.py)   │          (.sh files)            │
│          │          │          │                                  │
│ build_ast│build_js_ │build_py_ │ build_bash_ast.cpp              │
│ transpile│ ast      │ ast      │ transpile_bash_mir.cpp          │
│          │transpile_│transpile_│                                  │
│          │ js_mir   │ py_mir   │                                  │
├──────────┴──────────┴──────────┴─────────────────────────────────┤
│                  Shared Runtime Infrastructure                    │
│                                                                   │
│  ┌────────────┐ ┌──────────┐ ┌─────────────┐ ┌───────────────┐  │
│  │ Item Type  │ │ GC Heap  │ │  MIR JIT    │ │ import_resolver│  │
│  │ System     │ │ & Nursery│ │ (mir.c)     │ │ (sys_func_    │  │
│  │ (TypeId)   │ │          │ │             │ │  registry.c)  │  │
│  └────────────┘ └──────────┘ └─────────────┘ └───────────────┘  │
│  ┌────────────┐ ┌──────────┐ ┌─────────────┐ ┌───────────────┐  │
│  │ Name Pool  │ │ Memory   │ │ Shape System│ │ I/O Parsers & │  │
│  │ (interning)│ │ Pool     │ │ (ShapeEntry)│ │ Formatters    │  │
│  └────────────┘ └──────────┘ └─────────────┘ └───────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

### 1.3 Execution Entry Point

```
main.cpp                           transpile_bash_mir.cpp
────────                           ────────────────────
argv[1] == "bash"
  │
  ├─ runtime_init(&runtime)        // shared Runtime struct
  ├─ lambda_stack_init()           // GC stack bounds
  ├─ read_text_file(bash_file)     // read Bash source
  │
  └─ transpile_bash_to_mir() ────→  bash_transpiler_create()
                                      bash_transpiler_parse()       // TS parse
                                      build_bash_ast()              // TS CST → Bash AST
                                      heap_init()                   // GC heap
                                      jit_init(2)                   // MIR ctx
                                      bm_transpile_ast()            // AST → MIR IR
                                      MIR_load_module()             // prepare for linking
                                      MIR_link(import_resolver)     // link imports
                                      find_func(ctx, "bash_main")   // locate entry
                                      bash_main(&eval_context)      // execute
                                      ← return Item result
```

---

## 2. Bash Data Types ↔ Lambda Runtime Types

Bash is fundamentally string-typed, but the Lambda runtime can represent richer types. LambdaBash will auto-detect integer values in arithmetic contexts and use native `Item` integers for performance.

### 2.1 Type Mapping Table

| Bash Concept | Lambda TypeId | Representation | Notes |
|--------------|--------------|----------------|-------|
| String (default) | `LMD_TYPE_STRING` | `String*` via `heap_create_name()` | All unquoted/quoted words |
| Integer (arithmetic) | `LMD_TYPE_INT` | Inline tag + 56-bit signed | Inside `$(( ))` and `declare -i` |
| Exit code | `LMD_TYPE_INT` | Inline tag + value 0–255 | `$?` result of last command |
| Indexed array | `LMD_TYPE_ARRAY` | Lambda `Array` | `arr=(a b c)`, `${arr[0]}` |
| Associative array | `LMD_TYPE_MAP` | Lambda `Map` with `ShapeEntry` | `declare -A map` |
| Null/unset | `LMD_TYPE_NULL` | Tag sentinel | Unset variables, empty expansion |
| Boolean (test) | `LMD_TYPE_BOOL` | Inline tag + 0/1 | Result of `[[ ]]` / `test` |
| Function | `LMD_TYPE_FUNC` | Pool-allocated `Function` | `function foo() { }` |

### 2.2 String-First Design

Unlike JS/Python where types are explicit, Bash treats everything as strings by default. LambdaBash follows this convention:

- **Variable assignment:** `x=hello` → `Item` of type `LMD_TYPE_STRING`
- **Arithmetic context:** `$(( x + 1 ))` → Coerce `x` to `LMD_TYPE_INT`, perform integer arithmetic
- **Array indexing:** `${arr[0]}` → Array access returns `Item` (string or int depending on content)
- **Test expressions:** `[[ -f file ]]` → `LMD_TYPE_BOOL` (true/false)

```c
// Bash-specific coercion
Item bash_to_int(Item value);       // string "42" → int 42, non-numeric → 0
Item bash_to_string(Item value);    // int 42 → string "42"
bool bash_is_truthy(Item value);    // exit code 0 → true, non-zero → false
                                    // (inverse of C convention!)
```

### 2.3 Reuse of Lambda System Functions

| Capability | Lambda Subsystem | Bash Usage |
|-----------|-----------------|------------|
| String interning | `name_pool.hpp` → `heap_create_name()` | All string values, variable names |
| GC heap | `lambda-mem.cpp` → `heap_alloc()` | String/array/map allocation |
| Memory pools | `lib/mempool.h` → `pool_create()` | AST nodes, temporary allocations |
| Shape system | `shape_builder.hpp` | Associative array property layout |
| Array | `lambda-data.hpp` → `Array` | Indexed arrays |
| Map | `lambda-data.hpp` → `Map` | Associative arrays, environment |
| Format output | `format/format.h` | Result formatting |
| MIR JIT | `mir.c` → `jit_init()` | Code generation and execution |
| Import resolver | `sys_func_registry.c` | Linking runtime functions |
| JSON parser | `input-json.cpp` | Future: `jq`-style JSON processing |
| Regex (RE2) | `re2_wrapper.hpp` | `[[ str =~ pattern ]]` matching |

---

## 3. Bash-Specific Transpiler & Runtime Design

### 3.1 AST Node Types (`bash_ast.hpp`)

```cpp
enum BashAstType {
    // Statements
    BASH_COMMAND,           // simple command: cmd arg1 arg2
    BASH_PIPELINE,          // cmd1 | cmd2 | cmd3
    BASH_LIST,              // cmd1 && cmd2 || cmd3
    BASH_COMPOUND_LIST,     // { cmd1; cmd2; }
    BASH_SUBSHELL,          // ( cmd1; cmd2 )
    BASH_IF,                // if/elif/else/fi
    BASH_FOR,               // for var in list; do ...; done
    BASH_FOR_ARITHMETIC,    // for (( i=0; i<10; i++ ))
    BASH_WHILE,             // while cond; do ...; done
    BASH_UNTIL,             // until cond; do ...; done
    BASH_CASE,              // case $var in pattern) ;; esac
    BASH_FUNCTION_DEF,      // function foo() { }

    // Expressions / Words
    BASH_WORD,              // plain word / string literal
    BASH_STRING,            // "double quoted" or 'single quoted'
    BASH_CONCATENATION,     // adjacent words: "$a"_"$b"
    BASH_VARIABLE_REF,      // $var, ${var}
    BASH_EXPANSION,         // ${var:-default}, ${var##pattern}, etc.
    BASH_COMMAND_SUB,       // $(command) or `command`
    BASH_ARITHMETIC_EXPR,   // $(( expr ))
    BASH_PROCESS_SUB,       // <(cmd) / >(cmd)  [Phase 2]
    BASH_GLOB,              // *, ?, [a-z]
    BASH_ARRAY_LITERAL,     // (a b c)
    BASH_ARRAY_ACCESS,      // ${arr[idx]}
    BASH_TEST_EXPR,         // [[ expr ]] / [ expr ]
    BASH_REGEX_MATCH,       // [[ str =~ regex ]]

    // Redirections
    BASH_REDIRECT,          // > >> < << 2>&1 etc.
    BASH_HEREDOC,           // <<EOF ... EOF
    BASH_HERESTRING,        // <<< "string"

    // Assignments
    BASH_ASSIGNMENT,        // var=value
    BASH_ARRAY_ASSIGN,      // arr=(a b c)
    BASH_ASSOC_ASSIGN,      // declare -A map=([k]=v)
    BASH_COMPOUND_ASSIGN,   // var+=value

    // Control
    BASH_RETURN,            // return [n]
    BASH_BREAK,             // break [n]
    BASH_CONTINUE,          // continue [n]
    BASH_EXIT,              // exit [n]
};

struct BashAstNode {
    BashAstType type;
    BashAstNode* children;      // child node list
    int child_count;
    const char* text;           // literal text (for words, variable names)
    int text_len;
    int redirect_fd;            // for redirections: fd number
    int line;                   // source line number
};
```

### 3.2 Transpiler Architecture (`transpile_bash_mir.cpp`)

```c
struct BashMirTranspiler {
    MIR_context_t ctx;              // MIR JIT context
    MIR_module_t module;            // current MIR module
    MIR_item_t current_func_item;   // MIR function being emitted
    MIR_func_t current_func;        // current MIR function
    BashTranspiler* tp;             // Bash parser/AST context

    struct hashmap* import_cache;   // runtime function import dedup
    struct hashmap* local_funcs;    // user-defined functions

    // Variable scopes (local/global/environment)
    struct hashmap* var_scopes[64];
    int scope_depth;

    // Loop control
    BashLoopLabels loop_stack[32];
    int loop_depth;

    // Counters
    int reg_counter;
    int label_counter;

    // Collected function definitions
    BashFuncCollected func_entries[128];
    int func_count;

    // Environment variable table (inherited + exported)
    struct hashmap* env_vars;

    // Pipeline state
    int pipe_depth;

    // Exit code register ($?)
    MIR_reg_t exit_code_reg;
};
```

### 3.3 Multi-Phase Compilation

| Phase | Name | Description |
|-------|------|-------------|
| 1 | Function Collection | Walk AST to collect all `function` definitions, record name |
| 2 | Global Variable Scan | Identify top-level variable assignments and `export` declarations |
| 3 | Function Compilation | Emit MIR for each user-defined function (`bashf_<name>`) |
| 4 | Entry Point Creation | Create `bash_main()` — transpile top-level statements sequentially |

### 3.4 Code Generation Examples

**Simple command:**
```bash
echo "Hello, World!"
```
```
MIR:     str_0 = MOV <namepool "Hello, World!">
         boxs_1 = OR(BASH_STR_TAG, str_0)
         CALL bash_builtin_echo(boxs_1)
         MOV exit_code, 0
```

**Variable assignment and expansion:**
```bash
name="Lambda"
echo "Hello, $name"
```
```
MIR:     str_0 = MOV <namepool "Lambda">
         boxs_1 = OR(BASH_STR_TAG, str_0)
         CALL bash_set_var("name", boxs_1)

         var_2 = CALL bash_get_var("name")
         str_3 = CALL bash_concat("Hello, ", var_2)
         CALL bash_builtin_echo(str_3)
```

**Arithmetic expansion:**
```bash
x=10
y=$(( x + 5 ))
```
```
MIR:     str_0 = MOV <namepool "10">
         CALL bash_set_var("x", str_0)

         var_1 = CALL bash_get_var("x")
         int_2 = CALL bash_to_int(var_1)
         boxi_3 = OR(BASH_INT_TAG, 5)
         result_4 = CALL bash_add(int_2, boxi_3)
         CALL bash_set_var("y", result_4)
```

**Pipeline:**
```bash
cat file.txt | grep "pattern" | wc -l
```
```
MIR:     // Pipeline creates chained Item streams
         pipe_0 = CALL bash_builtin_cat("file.txt")         // → Item (string content)
         pipe_1 = CALL bash_builtin_grep("pattern", pipe_0) // → Item (filtered lines)
         pipe_2 = CALL bash_builtin_wc("-l", pipe_1)         // → Item (line count)
         MOV exit_code, <last command exit code>
```

### 3.5 Scope & Variable Model

Bash has a unique variable scope model that differs from both JS and Python:

```
┌─────────────────────────────────────────────┐
│ Environment Variables (inherited from OS)    │
│  PATH, HOME, USER, ...                       │
├─────────────────────────────────────────────┤
│ Global Script Variables (default scope)      │
│  Assignments at top level or in functions    │
│  unless `local` is used                      │
├─────────────────────────────────────────────┤
│ Local Function Variables (`local var`)       │
│  Dynamic scoping — visible to callees!       │
├─────────────────────────────────────────────┤
│ Special Variables (read-only)                │
│  $0, $1..$9, $#, $@, $*, $$, $!, $?, $-    │
└─────────────────────────────────────────────┘
```

**Key difference from JS/Python:** Bash uses **dynamic scoping** for `local` variables — a local variable in function `A` is visible to function `B` if `A` calls `B`. This requires a runtime scope stack rather than compile-time lexical analysis.

```c
// Runtime scope management
Item bash_get_var(const char* name);                      // walk scope stack
void bash_set_var(const char* name, Item value);          // set in current scope
void bash_set_local_var(const char* name, Item value);    // set in local scope
void bash_export_var(const char* name, Item value);       // mark for export
void bash_unset_var(const char* name);                    // remove variable
Item bash_get_special_var(int special_id);                // $?, $#, $@, etc.
```

### 3.6 Parameter Expansion

Bash parameter expansion is one of the most complex areas of the language. Each expansion form maps to a runtime function:

| Expansion | Runtime Function | Description |
|-----------|-----------------|-------------|
| `$var` / `${var}` | `bash_get_var(name)` | Simple variable lookup |
| `${var:-default}` | `bash_expand_default(var, default)` | Use default if unset/empty |
| `${var:=default}` | `bash_expand_assign_default(var, default)` | Assign default if unset/empty |
| `${var:+alt}` | `bash_expand_alt(var, alt)` | Use alt if set and non-empty |
| `${var:?msg}` | `bash_expand_error(var, msg)` | Error if unset/empty |
| `${#var}` | `bash_expand_length(var)` | String length |
| `${var#pat}` | `bash_expand_trim_prefix(var, pat)` | Remove shortest prefix match |
| `${var##pat}` | `bash_expand_trim_prefix_long(var, pat)` | Remove longest prefix match |
| `${var%pat}` | `bash_expand_trim_suffix(var, pat)` | Remove shortest suffix match |
| `${var%%pat}` | `bash_expand_trim_suffix_long(var, pat)` | Remove longest suffix match |
| `${var/pat/str}` | `bash_expand_replace(var, pat, str)` | Replace first match |
| `${var//pat/str}` | `bash_expand_replace_all(var, pat, str)` | Replace all matches |
| `${var^}` / `${var^^}` | `bash_expand_upper(var, all)` | Uppercase first/all |
| `${var,}` / `${var,,}` | `bash_expand_lower(var, all)` | Lowercase first/all |
| `${var:off:len}` | `bash_expand_substring(var, off, len)` | Substring extraction |
| `${!prefix@}` | `bash_expand_indirect(prefix)` | Variable indirection |

---

## 4. Builtin Commands

### 4.1 Native Builtins (In-Process)

These builtins are implemented as C functions within the Lambda runtime, avoiding process creation overhead:

| Builtin | Runtime Function | Description |
|---------|-----------------|-------------|
| `echo` | `bash_builtin_echo(args[], argc)` | Print arguments to stdout |
| `printf` | `bash_builtin_printf(fmt, args[], argc)` | Formatted output |
| `read` | `bash_builtin_read(opts, varnames[])` | Read line from stdin |
| `test` / `[` | `bash_builtin_test(args[], argc)` | Conditional evaluation |
| `[[ ]]` | Compiled inline (MIR) | Extended test — compiled directly |
| `declare`/`typeset` | `bash_builtin_declare(opts, name, val)` | Variable attributes |
| `local` | `bash_builtin_local(name, val)` | Local variable declaration |
| `export` | `bash_builtin_export(name, val)` | Environment export |
| `unset` | `bash_builtin_unset(name)` | Remove variable |
| `shift` | `bash_builtin_shift(n)` | Shift positional params |
| `return` | Compiled as MIR `RET` | Function return |
| `exit` | `bash_builtin_exit(code)` | Script termination |
| `true` | Returns exit code 0 | No-op success |
| `false` | Returns exit code 1 | No-op failure |
| `cd` | `bash_builtin_cd(dir)` | Change directory |
| `pwd` | `bash_builtin_pwd()` | Print working directory |
| `source` / `.` | `bash_builtin_source(file)` | Execute file in current scope |
| `type` | `bash_builtin_type(name)` | Describe command type |
| `set` | `bash_builtin_set(opts)` | Shell option management |
| `getopts` | `bash_builtin_getopts(optstr, name)` | Option parsing |

### 4.2 Lambda-Enhanced Builtins

These builtins go beyond standard Bash by leveraging Lambda's data processing capabilities:

| Builtin | Description | Lambda Subsystem |
|---------|-------------|-----------------|
| `json_parse` | Parse JSON string → Lambda `Map`/`Array` | `input-json.cpp` |
| `json_fmt` | Format Lambda value → JSON string | `format-json.cpp` |
| `xml_parse` | Parse XML string → Lambda `Element` tree | `input-xml.cpp` |
| `csv_parse` | Parse CSV → Lambda `Array` of `Array`s | `input-csv.cpp` |
| `yaml_parse` | Parse YAML → Lambda `Map` | `input-yaml.cpp` |
| `lambda_eval` | Evaluate a Lambda expression inline | `lambda-eval.cpp` |

Example usage:
```bash
# Parse JSON with Lambda's parser, access fields
data=$(json_parse '{"name": "Alice", "age": 30}')
echo ${data.name}    # "Alice" — map field access

# Pipeline: CSV → Lambda → JSON
csv_parse < input.csv | lambda_eval '.filter(|r| r.age > 25)' | json_fmt
```

### 4.3 External Command Execution

Commands not recognized as builtins are dispatched to the OS via `posix_spawn()` or `fork()/exec()`:

```c
Item bash_exec_external(const char* cmd, Item* args, int argc,
                        Item stdin_data, int* exit_code);
```

- **stdin**: If pipeline feeds data, it is passed as an `Item` string to the child's stdin
- **stdout**: Child stdout is captured into a Lambda `String` Item
- **stderr**: Captured separately, accessible via special variable
- **exit code**: Stored in `$?` register

---

## 5. Pipelines & Redirections

### 5.1 Pipeline Model

Bash pipelines are central to the language. In LambdaBash, pipelines operate on `Item` values rather than raw byte streams where possible (for builtins), falling back to byte-stream pipes for external commands.

```
Builtin-to-Builtin Pipeline (zero-copy):
    echo "hello" | wc -c
    → bash_builtin_echo() returns Item string
    → bash_builtin_wc() receives Item string directly

Builtin-to-External Pipeline (byte bridge):
    echo "hello" | /usr/bin/sort
    → bash_builtin_echo() returns Item string
    → String bytes written to pipe fd
    → External reads from pipe fd
    → External stdout captured back to Item string

External-to-External Pipeline (OS pipes):
    /usr/bin/ls | /usr/bin/sort
    → Standard pipe()/fork()/exec() chain
    → Final stdout captured to Item string
```

### 5.2 Redirection Handling

```c
// Redirection descriptor
struct BashRedirect {
    int fd;             // file descriptor (0=stdin, 1=stdout, 2=stderr)
    int mode;           // REDIR_READ, REDIR_WRITE, REDIR_APPEND, REDIR_DUP
    Item target;        // filename Item or fd number
};

Item bash_exec_with_redirects(BashRedirect* redirs, int redir_count,
                              Item (*command_fn)(void*), void* cmd_data);
```

| Syntax | Redirect Type | Implementation |
|--------|--------------|----------------|
| `> file` | Write stdout | `open(file, O_WRONLY\|O_CREAT\|O_TRUNC)` |
| `>> file` | Append stdout | `open(file, O_WRONLY\|O_CREAT\|O_APPEND)` |
| `< file` | Read stdin | `open(file, O_RDONLY)` |
| `2> file` | Write stderr | Redirect fd 2 |
| `2>&1` | Merge stderr→stdout | `dup2(1, 2)` |
| `&> file` | Write both | Redirect fd 1 and fd 2 |
| `<<EOF` | Here-document | Expand and pipe as stdin |
| `<<<str` | Here-string | Pipe string as stdin |

---

## 6. Control Flow Compilation

### 6.1 If/Elif/Else

```bash
if [[ $x -gt 10 ]]; then
    echo "big"
elif [[ $x -gt 5 ]]; then
    echo "medium"
else
    echo "small"
fi
```

Compiled to MIR with conditional branches:
```
MIR:     var_x = CALL bash_get_var("x")
         int_x = CALL bash_to_int(var_x)
         cmp_0 = GT(int_x, 10)
         BF cmp_0, elif_label

         // then block
         CALL bash_builtin_echo("big")
         JMP end_if

     elif_label:
         cmp_1 = GT(int_x, 5)
         BF cmp_1, else_label
         CALL bash_builtin_echo("medium")
         JMP end_if

     else_label:
         CALL bash_builtin_echo("small")

     end_if:
```

### 6.2 For Loops

```bash
for item in "apple" "banana" "cherry"; do
    echo "$item"
done
```

```
MIR:     // build array of items
         arr = CALL bash_create_array("apple", "banana", "cherry")
         len = CALL bash_array_length(arr)
         idx = MOV 0

     for_start:
         BEQ idx, len, for_end
         item = CALL bash_array_get(arr, idx)
         CALL bash_set_var("item", item)
         CALL bash_builtin_echo(item)
         idx = ADD(idx, 1)
         JMP for_start

     for_end:
```

### 6.3 While/Until Loops

```bash
while read -r line; do
    echo "Got: $line"
done < input.txt
```

Compiled with loop labels supporting `break` and `continue`:
```
MIR: while_start:
         result = CALL bash_builtin_read("-r", "line")
         exit = CALL bash_exit_code(result)
         BNE exit, 0, while_end       // read returns non-zero at EOF

         var_line = CALL bash_get_var("line")
         concat = CALL bash_concat("Got: ", var_line)
         CALL bash_builtin_echo(concat)
         JMP while_start

     while_end:
```

### 6.4 Case Statements

```bash
case $ext in
    *.js)  echo "JavaScript" ;;
    *.py)  echo "Python" ;;
    *.sh)  echo "Bash" ;;
    *)     echo "Unknown" ;;
esac
```

Compiled as cascading pattern-match branches using glob matching:
```
MIR:     var_ext = CALL bash_get_var("ext")

         match_0 = CALL bash_glob_match(var_ext, "*.js")
         BT match_0, case_0
         match_1 = CALL bash_glob_match(var_ext, "*.py")
         BT match_1, case_1
         match_2 = CALL bash_glob_match(var_ext, "*.sh")
         BT match_2, case_2
         JMP case_default

     case_0: CALL bash_builtin_echo("JavaScript"); JMP case_end
     case_1: CALL bash_builtin_echo("Python"); JMP case_end
     case_2: CALL bash_builtin_echo("Bash"); JMP case_end
     case_default: CALL bash_builtin_echo("Unknown")
     case_end:
```

---

## 7. File Organization

### 7.1 New Source Files (`lambda/bash/`)

| File | LOC (est.) | Purpose |
|------|-----------|---------|
| `bash_ast.hpp` | ~150 | AST node type definitions |
| `build_bash_ast.cpp` | ~800 | Tree-sitter CST → typed BashAstNode tree |
| `bash_transpiler.hpp` | ~100 | Transpiler context struct and helpers |
| `transpile_bash_mir.cpp` | ~3000 | AST → MIR IR code generation |
| `bash_runtime.cpp` | ~1500 | Operators, type coercions, string operations |
| `bash_runtime.h` | ~200 | Runtime C API (extern "C" for MIR linkage) |
| `bash_builtins.cpp` | ~1200 | Built-in command implementations |
| `bash_expand.cpp` | ~800 | Parameter expansion (`${var:-default}`, etc.) |
| `bash_scope.cpp` | ~400 | Dynamic scope management |
| `bash_exec.cpp` | ~600 | External command execution, pipeline plumbing |
| **Total** | **~8800** | Comparable to LambdaPy (~7.8K) |

### 7.2 Dependencies

| Dependency | Source | Purpose |
|-----------|--------|---------|
| `tree-sitter-bash` | npm / GitHub | Bash grammar for parsing |
| Lambda runtime | existing | Item types, GC, MIR JIT, I/O |
| POSIX APIs | system | `fork()`, `exec()`, `pipe()`, `dup2()`, `glob()` |
| RE2 | existing | `[[ str =~ regex ]]` matching |

### 7.3 Build Configuration

Add to `build_lambda_config.json`:
```json
{
    "name": "tree-sitter-bash",
    "lib": "lambda/tree-sitter-bash/libtree-sitter-bash.a"
}
```

Add source directory to compilation units:
```json
{
    "sources": ["lambda/bash/*.cpp"]
}
```

---

## 8. Testing Strategy

### 8.1 Unit Tests (`test/test_bash_runtime.cpp`)

GTest-based tests for runtime functions:

```cpp
TEST(BashRuntime, VariableAssignmentAndLookup) { ... }
TEST(BashRuntime, ArithmeticExpansion) { ... }
TEST(BashRuntime, ParameterExpansion_Default) { ... }
TEST(BashRuntime, ParameterExpansion_TrimPrefix) { ... }
TEST(BashRuntime, ArrayOperations) { ... }
TEST(BashRuntime, AssociativeArrayOperations) { ... }
TEST(BashRuntime, GlobMatching) { ... }
TEST(BashRuntime, ExitCodePropagation) { ... }
TEST(BashRuntime, DynamicScoping) { ... }
TEST(BashRuntime, PipelineBuiltinToBuiltin) { ... }
```

### 8.2 Integration Tests (`test/bash/`)

Bash scripts with expected output files (mirroring Lambda's `test/lambda/*.ls` pattern):

```
test/bash/echo_basic.sh          → test/bash/echo_basic.txt
test/bash/variables.sh           → test/bash/variables.txt
test/bash/arithmetic.sh          → test/bash/arithmetic.txt
test/bash/arrays.sh              → test/bash/arrays.txt
test/bash/if_elif_else.sh        → test/bash/if_elif_else.txt
test/bash/for_loop.sh            → test/bash/for_loop.txt
test/bash/while_loop.sh          → test/bash/while_loop.txt
test/bash/case_statement.sh      → test/bash/case_statement.txt
test/bash/functions.sh           → test/bash/functions.txt
test/bash/param_expansion.sh     → test/bash/param_expansion.txt
test/bash/pipelines.sh           → test/bash/pipelines.txt
test/bash/redirections.sh        → test/bash/redirections.txt
test/bash/string_ops.sh          → test/bash/string_ops.txt
test/bash/subshell.sh            → test/bash/subshell.txt
test/bash/json_interop.sh        → test/bash/json_interop.txt
```

### 8.3 Conformance Tests

Compare LambdaBash output against real Bash for a curated set of scripts:
```bash
make test-bash-conformance    # runs each .sh with both bash and lambda.exe bash, diffs output
```

---

## 9. Implementation Phases

### Phase 1 — Core Language (MVP)

**Goal:** Run simple Bash scripts with variables, arithmetic, control flow, and builtins.

- [ ] Integrate `tree-sitter-bash` grammar into build system
- [ ] `bash_ast.hpp` — AST node definitions
- [ ] `build_bash_ast.cpp` — CST → AST builder
- [ ] `bash_runtime.h/.cpp` — Type coercions, arithmetic operators
- [ ] `bash_scope.cpp` — Variable get/set with global scope
- [ ] `bash_builtins.cpp` — `echo`, `printf`, `read`, `test`, `true`, `false`, `exit`
- [ ] `transpile_bash_mir.cpp` — Simple commands, assignments, `if/else`, `for`, `while`, `case`
- [ ] CLI integration in `main.cpp` — `./lambda.exe bash script.sh`
- [ ] Arithmetic expansion `$(( ))`
- [ ] Basic parameter expansion (`$var`, `${var}`, `${var:-default}`)
- [ ] Unit tests and 10+ integration tests

### Phase 2 — Functions & Arrays

- [ ] Function definitions and calls (with `local` scope)
- [ ] Dynamic scoping implementation
- [ ] Indexed arrays (`arr=(a b c)`, `${arr[0]}`, `${#arr[@]}`)
- [ ] Associative arrays (`declare -A`)
- [ ] Full parameter expansion suite (`##`, `%%`, `//`, etc.)
- [ ] `[[ ]]` extended test with regex matching
- [ ] `source` / `.` command
- [ ] `getopts` builtin
- [ ] Here-documents and here-strings

### Phase 3 — Pipelines & External Commands

- [ ] Builtin-to-builtin pipelines (zero-copy `Item` passing)
- [ ] External command execution (`posix_spawn` / `fork`+`exec`)
- [ ] Builtin-to-external and external-to-builtin pipe bridging
- [ ] File redirections (`>`, `>>`, `<`, `2>&1`)
- [ ] Command substitution `$(command)`
- [ ] Subshells `( commands )`
- [ ] Exit code propagation (`$?`, `PIPESTATUS`)
- [ ] `set -e` / `set -o pipefail` error handling modes

### Phase 4 — Lambda Interop & Advanced Features

- [ ] Lambda-enhanced builtins (`json_parse`, `xml_parse`, `csv_parse`, etc.)
- [ ] Glob expansion (`*.txt`, `**/*.md`)
- [ ] Word splitting and quoting rules
- [ ] Brace expansion (`{a,b,c}`, `{1..10}`)
- [ ] Tilde expansion (`~`, `~user`)
- [ ] Command-not-found handler
- [ ] `trap` and basic signal handling
- [ ] Process substitution `<(cmd)` / `>(cmd)`
- [ ] Performance benchmarks against native Bash

---

## 10. Design Decisions & Tradeoffs

### 10.1 Dynamic Scoping vs. Lexical Scoping

**Decision:** Implement Bash's dynamic scoping faithfully.

Bash uses dynamic scoping for `local` variables — this is semantically different from JS/Python. The transpiler cannot resolve variable references at compile time in all cases. The runtime scope stack (`bash_get_var`) must walk up the call stack to find variables.

**Tradeoff:** Slightly slower variable lookup than JS/Python (which use register-allocated locals), but semantically correct. Hot-path variables in tight loops can be optimized by the transpiler into registers when no function calls are present.

### 10.2 String-First vs. Typed Values

**Decision:** Store values as typed `Item` values internally, but follow Bash's string coercion semantics at language boundaries.

- Variable assignment: `x=42` stores as `LMD_TYPE_STRING` (Bash convention)
- Arithmetic context: `$(( x + 1 ))` coerces to `LMD_TYPE_INT` for the operation
- Comparison: `[[ $x -gt 10 ]]` coerces to int; `[[ $x > "abc" ]]` uses string comparison

**Tradeoff:** A small overhead from repeated string↔int coercion, but maintains Bash compatibility. A `declare -i` optimization can keep integer variables as `LMD_TYPE_INT` permanently.

### 10.3 Pipeline Model — Item Passing vs. Byte Streams

**Decision:** Use `Item`-based passing between builtins, byte-stream pipes for external commands.

When both sides of a pipeline are LambdaBash builtins, data flows as `Item` values (zero-copy). When one side is an external command, LambdaBash serializes/deserializes through OS pipes. This provides the best of both worlds: performance for pure-Bash pipelines and compatibility for mixed pipelines.

### 10.4 External Command Security

**Decision:** External commands execute via `posix_spawn()` with `PATH` resolution, matching standard Bash behavior. No sandboxing in Phase 1.

Future consideration: an opt-in restricted mode (`--restricted`) that limits external command execution to an allowlist, similar to `rbash`.

---

## 11. CLI Interface

```bash
# Run a Bash script
./lambda.exe bash script.sh

# Run with arguments (accessible as $1, $2, etc.)
./lambda.exe bash script.sh arg1 arg2

# Inline Bash command
./lambda.exe bash -c 'echo "Hello, World!"'

# Pipe stdin
echo '{"key": "value"}' | ./lambda.exe bash -c 'json_parse | echo ${data.key}'
```

---

## 12. Open Questions

1. **Word splitting granularity** — Should unquoted `$var` expansion perform word splitting and globbing by default (strict Bash compat), or should LambdaBash default to a safer mode (like `set -f` / no globbing)?

2. **`eval` support** — Dynamic code generation via `eval` would require runtime compilation. Should this be supported, and if so, should it cache compiled MIR modules?

3. **Subshell isolation** — Subshells `( commands )` in Bash fork a new process. In LambdaBash, should these be true forks or lightweight scope-isolation (snapshot and restore variables)?

4. **POSIX vs. Bash** — The `tree-sitter-bash` grammar supports Bashisms. Should there be a `--posix` flag for strict POSIX `sh` compatibility?

5. **Performance target** — What is the acceptable performance target relative to native Bash? 1x (parity)? 2–5x faster for compute-heavy scripts?
