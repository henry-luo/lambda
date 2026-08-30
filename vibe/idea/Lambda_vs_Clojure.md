# Lambda Script vs Clojure — A Comparative Analysis

Lambda Script and Clojure share deep philosophical roots but diverge significantly in syntax and practical design.

## Shared Philosophy

| Principle | Lambda Script | Clojure |
|-----------|--------------|---------|
| **Immutability by default** | ✅ Data structures immutable | ✅ Persistent data structures |
| **Functional-first** | ✅ Pure functions, expressions | ✅ Pure functions, expressions |
| **First-class functions** | ✅ Closures, higher-order | ✅ Closures, higher-order |
| **Rich collection types** | ✅ list, array, map, element | ✅ list, vector, map, set |
| **Data-oriented** | ✅ "Data as Code" philosophy | ✅ "Data > Objects" philosophy |

## Key Differences

### Syntax

Lambda uses a **C/JS-family syntax** (`fn`, `let`, `{}`, `=>`) versus Clojure's **Lisp S-expressions** (`(defn ...)`, `(let [...] ...)`). Lambda feels much more approachable to mainstream developers.

### Type System

Lambda has **static typing with inference** (`let x: int = 42`, union types `int | string`, `T^E` error types). Clojure is **dynamically typed** — optional typing only via `clojure.spec` or Typed Clojure, neither of which is compile-time enforced in the same way.

### Error Handling

This is a major divergence. Lambda uses **error-as-return-value** with `T^E` types, `raise`, postfix `^` propagation, and `e ^ { … }` handlers (S7.6) — compile-time enforced, no exceptions. Clojure uses **JVM exceptions** (`try`/`catch`/`throw`). Lambda's approach is closer to Rust's `Result<T, E>` than anything in the Clojure world.

### Pipe Expressions

Lambda's `|>` pipe with the `~` placeholder (`data |> ~.name`) and `where` filtering (`data where len(~) > 3`) are very expressive for data pipelines (S10.1.1: `|` is union, `|>` is the pipe). Clojure has `->` and `->>` threading macros, which are powerful but work differently — they thread the result positionally rather than using a placeholder symbol.

### Document Processing

Lambda has **built-in markup elements** (`<div class: "toc", ...>`) and native parsers for 12+ formats (JSON, XML, HTML, CSS, Markdown, PDF, etc.). Clojure has nothing like this natively — you'd pull in libraries for each format.

### Procedural Escape Hatch

Lambda explicitly separates pure (`fn`) from impure (`pn`) functions, with I/O operations restricted to `pn`. Clojure doesn't enforce purity at the language level — any function can do I/O.

### Runtime

Lambda is a **custom C/C++ runtime with MIR JIT** and a tracing garbage collector. Clojure runs on the **JVM** (or JS via ClojureScript), inheriting its garbage collector and ecosystem. This gives Clojure a massive library ecosystem but Lambda much tighter control over memory and startup time.

### Concurrency

Clojure has **STM, atoms, agents, core.async** — coordinating *shared* mutable state is a first-class design concern. Lambda takes the opposite route: there is no shared mutable state at all (S9.1.7), so nothing needs coordinating. Concurrency enters through the colorless `start` builtin plus bounded per-task mailboxes with `send`/`receive`/`select` (S13) — closer to Erlang's actors than to Clojure's coordinated references. `core.async` channels are the nearest Clojure analog, but Lambda adds structured scope (block-owned handles, cancel-then-join on error exit, S13.3.1) and typed backpressure (`send` returns `ok^E`, S13.2.2).

## Where Lambda Stands Out

- **Stricter purity enforcement** (`fn` vs `pn` separation)
- **Compile-time error handling** — no silent ignored errors
- **Native document processing** — a scripting language that truly understands markup
- **C-family syntax** — lower barrier to entry than S-expressions
- **Lightweight runtime** — no JVM startup cost

## Where Clojure Stands Out

- **Mature ecosystem** — JVM interop gives access to thousands of libraries
- **Concurrency model** — STM, atoms, agents are battle-tested
- **REPL-driven development** — arguably the gold standard
- **Homoiconicity** — code-as-data enables powerful macros
- **Community & production track record** — widely used in industry

## Overall Impression

Lambda feels like it took the **functional ethos of Clojure/Haskell**, married it with **Rust-style error handling**, wrapped it in **modern scripting syntax**, and added a **domain-specific focus on document processing**. It's less general-purpose than Clojure but more opinionated about correctness (enforced purity, enforced error handling) and more batteries-included for its target domain.
