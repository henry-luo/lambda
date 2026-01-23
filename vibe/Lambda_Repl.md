# Lambda REPL Design Document

## Overview

Lambda Script provides an interactive **Read-Eval-Print Loop (REPL)** for exploring the language, testing expressions, and rapid prototyping. The REPL is the default mode when `lambda.exe` is invoked without arguments.

## Current Implementation Status

### Implemented Features

#### 1. Interactive Line Editor (`lib/cmdedit.c`)
- Custom readline-compatible implementation (no external dependencies)
- Cross-platform support: macOS, Linux, Windows
- **Line editing features:**
  - Cursor movement: Left/Right arrows, Home/End, Ctrl-A/Ctrl-E
  - Character deletion: Backspace, Delete, Ctrl-D (at empty line = EOF)
  - Word operations: Ctrl-W (backward kill word), Alt-D (forward kill word)
  - Line operations: Ctrl-K (kill to end), Ctrl-U (kill whole line), Ctrl-Y (yank)
  - Transpose: Ctrl-T (swap characters)
- **History management:**
  - Up/Down arrow navigation through command history
  - History search (Ctrl-R for reverse search)
  - Persistent history file support (`read_history`/`write_history`)
- **Signal handling:**
  - Ctrl-C (SIGINT) graceful handling
  - Window resize (SIGWINCH) detection
  - Proper terminal cleanup on exit

#### 2. REPL Commands
| Command | Description |
|---------|-------------|
| `.quit`, `.q`, `.exit` | Exit the REPL |
| `.help`, `.h` | Show help message |
| `.clear` | Clear REPL history buffer |

#### 3. Multi-line Input Support
- **Continuation prompt (`.. `)** when statement is incomplete
- Tree-sitter based detection of incomplete statements
- Checks for `MISSING` nodes (parser-inserted expected tokens)
- Automatically continues collecting input until statement is complete

```
λ> let add = fn(a, b) {
..   a + b
.. }
λ> add(1, 2)
3
```

#### 4. Syntax Error Recovery
- If a statement has syntax errors, it is discarded
- User can retry without corrupting the REPL history
- Distinguishes between:
  - **INCOMPLETE**: Missing closing braces/parens → continue with `..` prompt
  - **ERROR**: Actual syntax error → discard input, show error message

#### 5. Incremental Output Display
- Tracks previous output to avoid reprinting unchanged lines
- Only prints new output from the latest evaluation
- Reduces visual clutter in long REPL sessions

#### 6. Script Execution
- JIT compilation via C2MIR (default) or pure MIR (`--mir` flag)
- Full Lambda language support in REPL mode
- Error reporting with line/column information

#### 7. Unicode Support
- UTF-8 prompt display (λ> on supported terminals)
- Automatic detection of UTF-8 locale
- Fallback to ASCII prompt (>) when UTF-8 not available

---

## Design Architecture

### Implementation Structure

```
┌─────────────────────────────────────────────────────────────┐
│                     Lambda REPL                              │
├─────────────────────────────────────────────────────────────┤
│  main-repl.cpp                    │  main.cpp               │
│  - Statement completeness check   │  - run_repl() loop      │
│  - Tree-sitter MISSING detection  │  - Multi-line handling  │
│  - Prompt functions               │  - Error recovery       │
│  - Line editor wrappers           │  - Incremental output   │
└─────────────────────────────────────────────────────────────┘
         │                              │
         ▼                              ▼
┌─────────────────────┐      ┌─────────────────────┐
│    runner.cpp       │      │  lib/cmdedit.c      │
│  - load_script()    │      │  - Terminal I/O     │
│  - transpile()      │      │  - Line editing     │
│  - JIT compile      │      │  - History mgmt     │
│  - execute()        │      │  - Signal handling  │
└─────────────────────┘      └─────────────────────┘
```

### Key Files

| File | Purpose |
|------|---------|
| [lambda/main-repl.cpp](../lambda/main-repl.cpp) | REPL init, completeness check, prompts |
| [lambda/main.cpp](../lambda/main.cpp) | `run_repl()` - main loop with multi-line support |
| [lambda/runner.cpp](../lambda/runner.cpp) | Script loading, transpilation, JIT execution |
| [lib/cmdedit.c](../lib/cmdedit.c) | Custom command-line editor |
| [lib/cmdedit.h](../lib/cmdedit.h) | cmdedit public API |

---

## Core Design: Buffer-Based Evaluation

### 1. Internal Buffer Accumulation

When the user enters text, lines are accumulated in two buffers:

```cpp
StrBuf *repl_history = strbuf_new_cap(1024);  // accumulated script (all complete statements)
StrBuf *pending_input = strbuf_new_cap(256);  // current multi-line input being collected
StrBuf *last_output = strbuf_new_cap(256);    // previous output for incremental display
```

### 2. Statement Completeness Detection

Uses a **hybrid approach** combining lexical bracket counting and Tree-sitter parsing:

```cpp
enum StatementStatus {
    STMT_COMPLETE,      // ready to execute
    STMT_INCOMPLETE,    // needs more input (show continuation prompt)
    STMT_ERROR          // syntax error (discard input)
};

StatementStatus check_statement_completeness(TSParser* parser, const char* source) {
    // Step 1: Quick lexical check for unclosed brackets
    // This catches incomplete statements that Tree-sitter would report as ERROR
    if (has_unclosed_brackets(source)) {
        return STMT_INCOMPLETE;
    }
    
    // Step 2: Use Tree-sitter for more sophisticated checking
    TSTree* tree = lambda_parse_source(parser, source);
    TSNode root = ts_tree_root_node(tree);
    
    // If no errors, statement is complete
    if (!ts_node_has_error(root)) {
        return STMT_COMPLETE;
    }
    
    // Check for MISSING nodes (incomplete)
    if (has_missing_nodes(root)) {
        return STMT_INCOMPLETE;
    }
    
    // ERROR nodes without MISSING = syntax error
    return STMT_ERROR;
}
```

**Bracket Counting (`has_unclosed_brackets`):**
- Handles `{ }`, `( )`, `[ ]`
- Respects string literals (single and double quoted)
- Handles escape sequences in strings
- Recognizes line comments (`//`) and block comments (`/* */`)
- Returns `true` if any bracket count is positive or still in string/comment
```

### 3. Multi-line REPL Loop

```cpp
while ((line = lambda_repl_readline(pending_input->length > 0 ? cont_prompt : main_prompt)) != NULL) {
    // Append to pending input
    strbuf_append_str(pending_input, line);
    
    // Check completeness
    StatementStatus status = check_statement_completeness(runtime->parser, pending_input->str);
    
    if (status == STMT_INCOMPLETE) {
        continue;  // show ".. " prompt and keep reading
    }
    
    if (status == STMT_ERROR) {
        printf("Syntax error. Input discarded.\n");
        strbuf_reset(pending_input);
        continue;
    }
    
    // STMT_COMPLETE: add to history and execute
    strbuf_append_str(repl_history, pending_input->str);
    strbuf_reset(pending_input);
    
    // Execute and print incremental output...
}
```

### 4. Incremental Output Display

```cpp
// Print only the new portion of output
if (full_output->length > last_output->length) {
    if (strncmp(full_output->str, last_output->str, last_output->length) == 0) {
        // Prefix matches - print only new part
        printf("%s", full_output->str + last_output->length);
    } else {
        // Output structure changed - print all
        printf("%s", full_output->str);
    }
}
// Save for next comparison
strbuf_reset(last_output);
strbuf_append_str(last_output, full_output->str);
```

---

## Future Enhancements (Roadmap)

### Short-term
- [x] ~~Multi-line input with continuation prompt~~
- [x] ~~Syntax error recovery (discard bad lines)~~
- [x] ~~Incremental output display~~

### Medium-term
- [ ] **Tab completion** for:
  - Built-in function names
  - User-defined variables and functions
  - Keywords and operators
- [ ] **`.load <file>`** command to load script files into REPL
- [ ] **`.save <file>`** command to save session to file
- [ ] Reverse history search (Ctrl-R) improvements

### Long-term
- [ ] **Incremental compilation cache** (avoid full recompile)
  - Cache compiled functions by hash
  - Only recompile new/changed definitions
- [ ] **Session state persistence**
  - Save REPL state to disk on exit
  - Restore state on startup with `--restore` flag
- [ ] Context-aware completion using type information
- [ ] REPL-specific error messages with suggestions
- [ ] Debugger integration

---

## Performance Considerations

### Current Implementation Trade-offs

**Pros:**
- Simple, reliable implementation
- Full language support (no special REPL mode)
- Consistent behavior with script files
- Proper error recovery without state corruption

**Cons:**
- Full re-compilation on each input (O(n) where n = total lines)
- Full re-execution of all statements
- Memory usage grows with session length

### Future Optimization: Incremental Compilation

When implementing incremental compilation:

1. **Parse tree caching**
   - Tree-sitter supports incremental parsing
   - Reuse parse tree, only re-parse changed portions

2. **Function-level caching**
   ```cpp
   struct CompiledCache {
       HashMap<uint64_t, void*> func_cache;  // hash → compiled function
       
       void* get_or_compile(const char* source, size_t len) {
           uint64_t hash = hash_fnv64(source, len);
           if (func_cache.contains(hash)) {
               return func_cache.get(hash);
           }
           void* compiled = jit_compile(source);
           func_cache.set(hash, compiled);
           return compiled;
       }
   };
   ```

3. **State snapshots**
   - Checkpoint execution state periodically
   - Restore from checkpoint instead of re-running

---

## Testing

### Manual Test Cases

```bash
# Basic REPL
./lambda.exe
λ> 1 + 2
3
λ> let x = 10
10
λ> x * 2
20

# Multi-line input (automatic continuation)
λ> let double = fn(x) {
..   x * 2
.. }
λ> double(5)
10

# Multi-line with nested braces
λ> let complex = fn(a) {
..   if a > 0 {
..     a * 2
..   } else {
..     0 - a
..   }
.. }
λ> complex(-5)
5

# Error recovery
λ> let y = @#$
Syntax error. Input discarded.
λ> let y = 42
42

# REPL commands
λ> .clear
REPL history cleared
λ> .help
[shows help]
λ> .quit
```

---

## References

- [lib/cmdedit.c](../lib/cmdedit.c) - Line editor implementation
- [lambda/main.cpp](../lambda/main.cpp) - `run_repl()` function
- [lambda/main-repl.cpp](../lambda/main-repl.cpp) - REPL utilities
- [lambda/runner.cpp](../lambda/runner.cpp) - Script execution
- [Tree-sitter API](https://tree-sitter.github.io/tree-sitter/) - `ts_node_is_missing()`, `ts_node_has_error()`
