# Clojure's REPL-Driven Development & A Roadmap for Lambda

## Part 1: Clojure's REPL — What Makes It the Gold Standard

### 1.1 More Than a Command Prompt

Most languages ship a REPL — a prompt where you type an expression, hit Enter, and see a result. Clojure's REPL is fundamentally different: it is an **always-on, editor-integrated, application-connected evaluation server**. The REPL is not a toy sandbox — it *is* your development environment.

The canonical workflow:

```
Write code in editor → Send expression to running process → See result inline → Refine → Repeat
```

There is no "compile and re-run the whole app" step. The feedback loop is measured in **milliseconds**, not seconds.

### 1.2 nREPL — The Network REPL Protocol

The foundation of Clojure's REPL ecosystem is **nREPL** (network REPL), a client-server protocol that decouples the evaluation engine from the user interface.

**Architecture:**

```
┌─────────────────┐         TCP/socket         ┌──────────────────┐
│  Editor Client   │  ◄──────────────────────►  │   nREPL Server   │
│  (Calva, CIDER,  │     bencode messages       │  (inside your    │
│   Cursive, etc.) │                            │   running app)   │
└─────────────────┘                             └──────────────────┘
```

**Key properties:**
- **Transport-agnostic**: TCP sockets, Unix sockets, stdio — any transport works
- **Middleware stack**: Pluggable middleware for completion, debugging, pretty-printing, tracing
- **Session management**: Multiple independent evaluation sessions with isolated state
- **Asynchronous**: Clients send operations, server streams back results, output, errors as separate messages
- **Serialization**: Uses bencode (simple, language-agnostic format)

**A typical nREPL message exchange:**

```
Client → Server:
  {"op": "eval", "code": "(+ 1 2)", "session": "abc123", "id": "msg-1"}

Server → Client:
  {"value": "3", "session": "abc123", "id": "msg-1"}
  {"status": ["done"], "session": "abc123", "id": "msg-1"}
```

### 1.3 Editor Integration

Every major editor has a Clojure plugin built on nREPL:

| Editor | Plugin | Key Features |
|--------|--------|-------------|
| VS Code | **Calva** | Inline eval, paredit, test runner, debugger |
| Emacs | **CIDER** | The original and most mature; inspector, profiler, tracing |
| IntelliJ | **Cursive** | Full IDE: refactoring, debugging, structural editing |
| Vim/Neovim | **Conjure** | Lightweight, fast, HUD-based result display |

**What "editor-integrated" means in practice:**

1. **Inline evaluation**: Place cursor on any expression, press a keybinding — the result appears *next to the code* as a virtual overlay. No switching to a terminal.

2. **Evaluate any scope**: Evaluate a single expression, a top-level form, a selection, or an entire file — all with different keybindings.

3. **Rich results**: Results are not just text. Editors can render data structures as expandable trees, tables, or custom visualizations.

4. **Evaluation in namespace context**: Code is evaluated in the namespace of the current file, so all local definitions and imports are available.

5. **Live documentation**: Hover over a symbol to see its docstring, argument list, and source location — all fetched live from the running process, not from static analysis.

### 1.4 Incremental Redefinition

Clojure's dynamic runtime allows you to **redefine any var at any time**:

```clojure
;; Define a function
(defn greet [name]
  (str "Hello, " name))

(greet "Alice")  ;=> "Hello, Alice"

;; Oops, want it uppercase. Redefine just this function:
(defn greet [name]
  (str "HELLO, " (.toUpperCase name) "!"))

;; Immediately effective — no restart needed
(greet "Alice")  ;=> "HELLO, ALICE!"
```

This works because:
- **Vars are mutable references** to immutable values (including functions)
- **Namespaces are live registries** — redefining a var updates the registry
- **All call sites use var lookup** — they automatically see the new definition
- **No recompilation cascade** — only the redefined function is recompiled

### 1.5 State Inspection and Manipulation

Since the REPL is connected to your live application, you can:

```clojure
;; Query your running database
(db/query "SELECT count(*) FROM users")
;=> [{:count 14523}]

;; Inspect application state
@app-state
;=> {:config {...}, :cache-size 2048, :uptime-ms 384721}

;; Test a function against real data
(->> (db/query "SELECT * FROM users LIMIT 5")
     (map transform-user)
     (filter :active?))

;; Hot-patch a running production system (use with care!)
(alter-var-root #'config assoc :rate-limit 100)
```

### 1.6 REPL-Driven Testing

Instead of writing a test, running a test suite, and waiting:

```clojure
;; Write the function
(defn parse-date [s]
  (java.time.LocalDate/parse s))

;; Test it immediately, right here
(parse-date "2026-02-09")
;=> #object[java.time.LocalDate "2026-02-09"]

(parse-date "not-a-date")
;=> DateTimeParseException (see it instantly, fix it, re-eval)
```

Once you're confident, you formalize the ad-hoc tests into proper test functions. But the exploration happens interactively.

### 1.7 Debugging and Tracing

nREPL middleware provides:

- **Stacktrace navigation**: Click through frames, see locals at each level
- **Breakpoints**: Set breakpoints in the editor, execution pauses, inspect all bindings
- **Tracing**: Wrap any function to log every call with arguments and return value
- **Instrumentation**: CIDER can instrument a function so stepping through it shows intermediate values inline

### 1.8 The "REPL-Driven" Mindset

The REPL changes how you *think* about development:

| Traditional | REPL-Driven |
|-------------|-------------|
| Design → Implement → Test → Debug | Explore → Experiment → Formalize |
| Understand code by reading | Understand code by evaluating |
| Test via test suite (slow) | Test interactively (instant) |
| Debug with print statements or debugger | Debug by evaluating sub-expressions |
| Restart app to see changes | Never restart |
| State is opaque until logged | State is always inspectable |

---

## Part 2: Enhancing Lambda's REPL

### 2.1 Current State Assessment

Lambda's REPL today:

| Feature | Status |
|---------|--------|
| Interactive evaluation | ✅ Works — accumulates and re-executes full script |
| Multi-line input | ✅ Sophisticated — bracket counting + Tree-sitter parsing |
| Line editing | ✅ Custom `cmdedit` with Emacs keybindings |
| History | ✅ In-memory ring buffer (100 entries) |
| Prompt | ✅ Unicode `λ> ` with fallback |
| Tab completion | ⚠️ Framework exists in cmdedit, but no callback registered |
| Editor integration | ❌ None — VS Code extension is syntax-highlighting only |
| Network protocol | ❌ None |
| Incremental redefinition | ❌ Re-executes entire accumulated script each time |
| State inspection | ❌ No introspection commands |
| Debugger | ❌ No breakpoints or stepping |

### 2.2 Roadmap — Three Phases

---

#### Phase 1: Standalone REPL Excellence (CLI-only, no editor integration)

**Goal**: Make the CLI REPL genuinely powerful on its own.

##### 1a. Tab Completion

Wire up the existing `cmdedit` completion framework:

- **Keyword completion**: `fn`, `pn`, `let`, `var`, `if`, `for`, `where`, `import`, `pub`, `type`, etc.
- **Built-in function completion**: `print`, `len`, `sum`, `input`, `format`, `exists`, `map`, `filter`, etc.
- **Variable/function completion**: Track all `let`/`fn`/`pn` names from the accumulated script buffer and offer them
- **Module member completion**: After typing `module.`, complete with exported symbols from that module
- **File path completion**: Inside `input("...")` or pipe output `|> "..."`, complete file system paths

**Implementation sketch**: Register a completion callback in `init_repl()` that:
1. Parses the current line to determine context (keyword position, after a dot, inside a string, etc.)
2. Queries the current `LambdaScript` state for known symbols
3. Returns matching candidates to `cmdedit`

##### 1b. History Persistence

Save history to `~/.lambda_history` between sessions. The `cmdedit` library already has `save_history()` / `load_history()` — call them in `init_repl()` and `cleanup_repl()`.

##### 1c. REPL Introspection Commands

Extend the `.command` system:

| Command | Description |
|---------|-------------|
| `.type <expr>` | Show the inferred type of an expression |
| `.env` | List all variables and functions in scope with their types |
| `.source <name>` | Show the source definition of a function |
| `.time <expr>` | Evaluate and print execution time |
| `.ast <expr>` | Show the AST of an expression (for language developers) |
| `.reset` | Clear all accumulated state and start fresh |
| `.load <file>` | Load and evaluate a `.ls` file into the REPL session |
| `.save <file>` | Save the accumulated REPL buffer to a file |

##### 1d. Pretty Printing

Format output intelligently:
- Indent nested maps and lists
- Truncate long arrays with `[1, 2, 3, ... (247 more)]`
- Syntax-highlight output (colored types, strings, numbers)
- Show element trees in a readable indented format

##### 1e. Error Display Enhancement

- Colorized error messages with source location underlines
- Suggestion hints (e.g., "Did you mean `length`?" for `lenght`)
- Show the relevant source line with a caret pointing to the error position

---

#### Phase 2: Evaluation Server Protocol (λREPL Protocol)

**Goal**: Decouple the evaluation engine from the UI, enabling any client to connect.

##### 2a. Protocol Design

Design a simple JSON-based protocol (more natural for Lambda than Clojure's bencode):

**Client → Server:**
```json
{"op": "eval", "code": "1 + 2", "id": "msg-1"}
{"op": "complete", "prefix": "pri", "context": "top", "id": "msg-2"}
{"op": "type", "code": "let x = 42", "id": "msg-3"}
{"op": "env", "id": "msg-4"}
{"op": "load", "file": "./utils.ls", "id": "msg-5"}
```

**Server → Client:**
```json
{"id": "msg-1", "value": "3", "type": "int", "status": "done"}
{"id": "msg-2", "completions": ["print", "println"], "status": "done"}
{"id": "msg-1", "error": "division by zero", "line": 3, "col": 12, "status": "error"}
```

**Operations:**
| Op | Description |
|----|-------------|
| `eval` | Evaluate code in current session |
| `complete` | Request completions for a prefix |
| `type` | Infer type of an expression |
| `env` | List all bindings in current session |
| `load` | Load a file into the session |
| `reset` | Clear session state |
| `interrupt` | Cancel a running evaluation |
| `close` | End the session |

##### 2b. Transport

Support multiple transports:
- **stdio**: For subprocess-based editor integration (simplest)
- **TCP socket**: For network-based connections (write port to `.lambda-repl-port`)
- **Unix domain socket**: For local connections (faster than TCP, no port conflicts)

Launch with:
```bash
./lambda.exe repl --server              # stdio transport
./lambda.exe repl --server --port 7888  # TCP transport
./lambda.exe repl --server --socket /tmp/lambda.sock  # Unix socket
```

##### 2c. Session Management

Support multiple independent sessions:
- Each session has its own accumulated script buffer and state
- Sessions are identified by unique IDs
- Sessions can be forked (snapshot current state into a new session)

---

#### Phase 3: Editor Integration (VS Code Extension)

**Goal**: Bring the full REPL-driven experience into VS Code.

##### 3a. REPL Connection Manager

Extend the existing VS Code extension (`lambda/lib/lambda-highlighter/`):

- **Start REPL**: Command to launch a Lambda REPL server as a child process (stdio transport)
- **Connect to REPL**: Command to connect to an existing REPL server (TCP/socket)
- **Status bar**: Show connection status, session info
- **REPL terminal**: Integrated terminal panel that acts as a REPL client with output formatting

##### 3b. Inline Evaluation

The signature feature:

- **Evaluate expression**: `Ctrl+Enter` — evaluate the expression at cursor, show result as inline decoration
- **Evaluate top-level**: `Ctrl+Shift+Enter` — evaluate the entire `fn`/`pn`/`let` block
- **Evaluate selection**: Evaluate highlighted code
- **Evaluate file**: Send the entire file to the REPL
- **Clear inline results**: Command to dismiss all inline decorations

Result display:
- Short results (< 80 chars): Show inline as a dimmed comment-style annotation
- Medium results: Show in a hover popup
- Large results: Show in a dedicated output panel with expandable tree view

##### 3c. Completion and Hover (via REPL)

Use the live REPL session for intelligent features:

- **Auto-completion**: Query the REPL for completions based on live session state (knows all imported modules, defined functions, variable types)
- **Hover information**: Show type, value (for constants), and documentation for any symbol
- **Signature help**: Show parameter names and types when typing a function call

This provides **live semantic intelligence** — more accurate than static analysis because it reflects the actual runtime state.

##### 3d. Code Lens and Annotations

- **Type annotations**: Show inferred types above `let` bindings as CodeLens
- **Test indicators**: Show ▶️ Run buttons next to functions for quick evaluation
- **Performance hints**: Show execution time for previously evaluated expressions

##### 3e. Error Integration

- **Live diagnostics**: As you type, evaluate incrementally and show errors as VS Code diagnostics (red squiggles)
- **Error navigation**: Click errors to jump to source location
- **Quick fixes**: Suggest corrections for common errors (typos in function names, type mismatches)

##### 3f. Data Inspector

For complex results (maps, elements, large arrays):

- **Tree view**: Expandable tree in a side panel
- **Table view**: Render arrays-of-maps as sortable tables
- **Element view**: Render Lambda elements as formatted markup
- **Export**: Copy result as JSON, YAML, or Lambda literal

### 2.3 Architectural Considerations for Lambda

#### Incremental Evaluation

The biggest technical challenge: Lambda currently **re-executes the entire accumulated script** on each REPL input. This works for small sessions but won't scale. To match Clojure, Lambda needs:

1. **Persistent environment**: Keep the JIT-compiled module in memory. New definitions add to it; they don't rebuild from scratch.
2. **Redefinition support**: Allow `fn`/`pn`/`let` to be redefined. Since Lambda is immutable-by-default, this is conceptually "rebinding a name" — the old value is still referenced by anything that captured it, but new code sees the new binding.
3. **Dependency tracking**: When a function is redefined, identify callers that need recompilation (or use indirect dispatch via a function table, like Clojure's var lookup).

A pragmatic approach: use **MIR's ability to recompile individual functions** at runtime. Each top-level definition compiles to a separate MIR function. Redefinition recompiles just that function and updates the dispatch table.

#### Purity and Side Effects

Lambda's `fn`/`pn` separation is actually an *advantage* for REPL-driven development:
- Re-evaluating `fn` is always safe (pure, no side effects)
- Re-evaluating `pn` has clear, explicit side effects — the developer knows exactly what will happen
- The type system can warn when REPL evaluation would trigger I/O

#### JIT Warm-up

Lambda's C → MIR → native pipeline has more compilation overhead than Clojure's bytecode compilation. Mitigations:
- Use the **MIR-direct transpilation** path (`transpile-mir.cpp`) for REPL — skip the C intermediate step
- Cache compiled functions across evaluations
- Only recompile changed definitions

### 2.4 Priority Matrix

| Feature | Impact | Effort | Priority |
|---------|--------|--------|----------|
| Tab completion (CLI) | High | Low | **P0** |
| History persistence | Medium | Low | **P0** |
| REPL introspection commands | High | Medium | **P0** |
| Pretty printing | Medium | Medium | **P1** |
| λREPL protocol (stdio) | High | Medium | **P1** |
| VS Code inline eval | Very High | High | **P1** |
| Incremental redefinition | Very High | Very High | **P2** |
| VS Code completion via REPL | High | Medium | **P2** |
| Data inspector | Medium | High | **P3** |
| Multi-session support | Low | Medium | **P3** |

### 2.5 Quick Win — What to Build This Week

The single highest-value, lowest-effort improvement: **Tab completion in the CLI REPL**.

1. Collect all keywords, built-in function names, and user-defined names from the accumulated script
2. Register the completion callback in `init_repl()` 
3. On Tab press, filter candidates by prefix and present matches

This alone transforms the REPL from "command prompt" to "interactive environment" and requires no architectural changes — just wiring up the existing `cmdedit` framework.
