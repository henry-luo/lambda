# Lambda Script vs Racket — A Detailed Comparison

## Table of Contents

1. [Overview](#overview)
2. [Paradigm & Philosophy](#paradigm--philosophy)
3. [Type System](#type-system)
4. [Execution & Performance](#execution--performance)
5. [Data & Document Processing](#data--document-processing)
6. [Metaprogramming & Extensibility](#metaprogramming--extensibility)
7. [Ecosystem & Community](#ecosystem--community)
8. [Syntax Taste](#syntax-taste)
9. [Racket's Hygienic Macro System — Deep Dive](#rackets-hygienic-macro-system--deep-dive)
10. [Summary](#summary)

---

## Overview

**Lambda Script** is a general-purpose, cross-platform, pure functional scripting language designed for data processing and document presentation. Built from scratch in C/C++ with JIT compilation using MIR.

**Racket** is a multi-paradigm, language-oriented programming language descended from Scheme. It is renowned for its macro system and its philosophy of being a "programmable programming language."

---

## Paradigm & Philosophy

| Aspect | Lambda Script | Racket |
|--------|--------------|--------|
| **Paradigm** | Pure functional, expression-oriented | Multi-paradigm (functional, imperative, OOP, logic) |
| **Syntax** | C-family / ML-inspired (`fn`, `let`, `{}`) | S-expression / Lisp (`define`, `lambda`, `()`) |
| **Mutability** | Immutable by default; `var` opt-in | Mutable by default (`set!`), immutable variants available |
| **Side effects** | Segregated — only `pn` functions can do I/O | Unrestricted — any function can perform side effects |
| **Core philosophy** | Data processing & document transformation | Language creation & extensibility |

Lambda Script enforces a clean separation between pure computation (`fn`) and side-effecting procedures (`pn`). Racket takes a more permissive approach — any function can perform side effects, though idiomatic Racket encourages functional style.

---

## Type System

| Aspect | Lambda Script | Racket |
|--------|--------------|--------|
| **Typing** | Static with inference, checked at compile time | Dynamic by default; optional via Typed Racket |
| **Error model** | Error-as-value (`T^E`, `raise`, `?` propagation) — no exceptions | Exception-based (`raise`, `with-handlers`) |
| **Union types** | First-class: `int \| string`, `int?` | Via contracts or Typed Racket's `(U Integer String)` |
| **Type annotations** | Native: `let x: int = 42` | Only in Typed Racket: `(: x Integer)` |
| **Null safety** | Explicit nullable: `int?` means `int \| null` | No built-in null safety |

Lambda Script's error model is notable: errors are explicit return values, not exceptions. Functions declare error types with `T^E` syntax, and the compiler enforces that callers handle them — ignoring an error is a compile-time error.

```lambda
fn divide(a, b) int^ {
    if (b == 0) raise error("division by zero")
    else a / b
}

let result = divide(10, x)?          // propagate with ?
let result^err = divide(10, x)       // or destructure to handle locally
```

In Racket, errors use traditional exceptions:

```racket
(define (divide a b)
  (if (= b 0)
      (error 'divide "division by zero")
      (/ a b)))

(with-handlers ([exn:fail? (λ (e) (displayln (exn-message e)))])
  (divide 10 0))
```

---

## Execution & Performance

| Aspect | Lambda Script | Racket |
|--------|--------------|--------|
| **Compilation** | JIT via MIR — near-native speed | Bytecode VM + JIT (Chez Scheme backend since Racket 8) |
| **Memory** | Reference counting + pool/arena allocation | Tracing garbage collector |
| **Startup** | Lightweight C runtime, fast startup | Heavier runtime, slower cold start |
| **Runtime size** | Small, self-contained binary | Larger distribution (~100MB+) |

Lambda Script's reference-counting memory model avoids GC pauses entirely, which is advantageous for latency-sensitive workloads. Racket's Chez Scheme backend provides excellent throughput but relies on a tracing GC that can introduce occasional pauses.

---

## Data & Document Processing

| Aspect | Lambda Script | Racket |
|--------|--------------|--------|
| **Built-in formats** | 12+ (JSON, XML, HTML, CSS, Markdown, PDF, YAML, LaTeX, CSV, TOML…) | None built-in; libraries for JSON, XML, etc. |
| **Pipe expressions** | Native: `data \| ~.name where len(~) > 3` | No native pipes; use threading macros or `~>` from libraries |
| **Markup/elements** | First-class: `<div class: "x"; "content">` | Via Scribble or X-expressions (libraries) |
| **Document rendering** | Built-in CSS layout engine (Radiant), PDF/SVG/PNG output | Scribble for docs; no built-in layout engine |
| **Format conversion** | CLI: `lambda convert input.json -t yaml` | Requires writing custom scripts with library calls |

This is Lambda Script's strongest advantage. Document and data processing are first-class concerns:

```lambda
let data = input("sales.json", 'json)
let totals = data.sales | ~.amount | sum
let report = format({total: totals}, 'yaml)
```

In Racket, the same requires importing libraries and more boilerplate:

```racket
(require json)
(define data (call-with-input-file "sales.json" read-json))
(define totals (apply + (map (λ (s) (hash-ref s 'amount)) (hash-ref data 'sales))))
```

---

## Metaprogramming & Extensibility

| Aspect | Lambda Script | Racket |
|--------|--------------|--------|
| **Macros** | No macro system | World-class hygienic macro system — Racket's defining strength |
| **Language creation** | Not a goal | Racket is a *language-oriented* platform (`#lang`) |
| **DSLs** | Limited to the built-in syntax | First-class support for creating DSLs |
| **Syntax extensibility** | Fixed grammar | Fully extensible reader and expander |

This is Racket's strongest advantage. See the [deep dive below](#rackets-hygienic-macro-system--deep-dive).

---

## Ecosystem & Community

| Aspect | Lambda Script | Racket |
|--------|--------------|--------|
| **Maturity** | New / in development | ~30 years (PLT Scheme → Racket) |
| **Package ecosystem** | Self-contained runtime | Large package catalog (`raco pkg`) |
| **IDE support** | VS Code with Tree-sitter grammar | DrRacket (purpose-built IDE), VS Code plugin |
| **Use cases** | Data processing, document transformation, scripting | Education, PL research, web apps, scripting, DSLs |
| **Documentation** | Growing | Extensive, with integrated Scribble docs |

---

## Syntax Taste

### Factorial

**Lambda Script:**
```lambda
fn factorial(n: int) int {
    if (n <= 1) 1
    else n * factorial(n - 1)
}
```

**Racket:**
```racket
(define (factorial n)
  (if (<= n 1) 1 (* n (factorial (- n 1)))))
```

### Data Processing Pipeline

**Lambda Script:**
```lambda
let data = input("sales.json", 'json)
let high_value = data.sales where ~.amount > 1000
let names = high_value | ~.customer.name
let total = high_value | ~.amount | sum
```

**Racket:**
```racket
(define data (call-with-input-file "sales.json" read-json))
(define high-value (filter (λ (s) (> (hash-ref s 'amount) 1000))
                           (hash-ref data 'sales)))
(define names (map (λ (s) (hash-ref (hash-ref s 'customer) 'name)) high-value))
(define total (apply + (map (λ (s) (hash-ref s 'amount)) high-value)))
```

### Document Generation

**Lambda Script:**
```lambda
let toc = <div class: "toc";
    <h2; "Table of Contents">
    <ul;
        for (heading in headings) <li; <a href: "#" ++ heading; heading>>
    >
>
```

**Racket (using X-expressions):**
```racket
(define toc
  `(div ((class "toc"))
     (h2 "Table of Contents")
     (ul ,@(map (λ (h) `(li (a ((href ,(string-append "#" h))) ,h)))
                headings))))
```

---

## Racket's Hygienic Macro System — Deep Dive

### What Are Macros?

Macros are **compile-time code transformations** — they let you extend the language's syntax by writing code that generates code. Instead of operating on runtime values, macros operate on the **syntax tree** before the program runs.

Think of them as functions that run at compile time, taking syntax as input and producing new syntax as output.

### The Problem: Unhygienic Macros

In early Lisps (e.g., Common Lisp's `defmacro`), macros used **textual substitution**, which caused accidental name collisions:

```racket
;; Unhygienic (Common Lisp style) — BROKEN
(defmacro swap! (a b)
  `(let ((tmp ,a))
     (set! ,a ,b)
     (set! ,b tmp)))

(let ((tmp 1) (y 2))
  (swap! tmp y))
;; BUG: the macro's internal "tmp" clashes with the user's variable "tmp"
;; After expansion, "tmp" refers to the wrong binding
```

This class of bug is called **variable capture** — the macro accidentally captures names from the call site, or the call site accidentally captures names from the macro.

### The Solution: Hygienic Macros

**Hygienic macros** (invented by Kohlbecker et al., 1986, and perfected in Racket) automatically **rename variables** to prevent collisions. Each macro expansion gets its own scope — the macro's internal names can never accidentally capture or shadow the caller's names, and vice versa.

```racket
;; Hygienic (Racket) — CORRECT
(define-syntax-rule (swap! a b)
  (let ([tmp a])
    (set! a b)
    (set! b tmp)))

(let ([tmp 1] [y 2])
  (swap! tmp y))
;; Works perfectly — the macro's "tmp" is automatically renamed
;; to avoid clashing with the user's "tmp"
```

The key insight: in Racket, syntax isn't just text or symbols — it's **syntax objects** that carry lexical context (which scope they belong to). The expander uses this context to ensure names always refer to the right bindings.

### Three Layers of Racket Macros

#### Layer 1: `define-syntax-rule` — Simple Pattern-Based Macros

The simplest form. You write a pattern and a template:

```racket
(define-syntax-rule (unless condition body ...)
  (if (not condition) (begin body ...) (void)))

;; Usage:
(unless (= x 0)
  (displayln "x is not zero")
  (displayln "processing..."))

;; Expands to:
(if (not (= x 0))
    (begin (displayln "x is not zero")
           (displayln "processing..."))
    (void))
```

The `...` means "zero or more of the preceding pattern element" — it handles variadic syntax naturally.

#### Layer 2: `syntax-rules` / `syntax-case` — Multi-Pattern Macros with Guards

For macros that need to match different shapes of input:

```racket
(define-syntax my-cond
  (syntax-rules (else)
    [(_ (else e ...))
     (begin e ...)]
    [(_ (test e ...) rest ...)
     (if test (begin e ...) (my-cond rest ...))]))

;; Usage:
(my-cond
  ((> x 10) (displayln "big"))
  ((> x 5)  (displayln "medium"))
  (else     (displayln "small")))
```

`syntax-case` adds the ability to run arbitrary Racket code at compile time to inspect and manipulate syntax:

```racket
(define-syntax (debug-print stx)
  (syntax-case stx ()
    [(_ expr)
     ;; At compile time, we can inspect the syntax object
     (with-syntax ([expr-str (datum->syntax stx (format "~a" (syntax->datum #'expr)))])
       #'(begin
           (printf "~a = ~v\n" expr-str expr)
           expr))]))

;; Usage:
(debug-print (+ 1 2))
;; Prints: (+ 1 2) = 3
;; Returns: 3
```

#### Layer 3: `syntax-parse` — Industrial-Strength Macros

The most powerful layer, designed for writing production-quality macros with excellent error reporting:

```racket
(require syntax/parse)

(define-syntax (define-record stx)
  (syntax-parse stx
    [(_ name:id (field:id ...) option ...)
     #:fail-when (check-duplicate-identifier (syntax->list #'(field ...)))
                 "duplicate field name"
     #:with constructor-name (format-id #'name "make-~a" #'name)
     #'(begin
         (struct name (field ...) #:transparent)
         (define (constructor-name field ...)
           (name field ...)))]))

;; Usage:
(define-record point (x y))
(make-point 3 4)  ;; => (point 3 4)

;; Error case:
(define-record bad (x x))
;; Error: duplicate field name at "x"  ← points to exact source location
```

Key features of `syntax-parse`:
- **Syntax classes** — reusable, composable pattern components with custom validation
- **Automatic error messages** — pinpoints the exact source location of bad syntax
- **Backtracking** — tries multiple patterns and picks the best match
- **Attributes** — structured data extracted from patterns, available in templates

### What Makes This Racket's Defining Strength

#### A. Language Creation with `#lang`

Racket's macro system is so powerful that entirely new **languages** can be implemented as macro libraries:

```racket
#lang typed/racket        ;; Statically-typed Racket — implemented in macros
#lang datalog             ;; Logic programming — implemented in macros
#lang scribble/manual     ;; Documentation language — implemented in macros
#lang lazy                ;; Lazy evaluation — implemented in macros
#lang at-exp racket       ;; @-expression reader — implemented in macros
#lang racket/gui          ;; GUI toolkit language
```

Each `#lang` can redefine both the **reader** (surface syntax — how text is parsed into syntax objects) and the **expander** (semantics — what the syntax means). This is not just syntactic sugar — these are full languages with different evaluation rules, type systems, and semantics, all hosted on the same runtime.

For example, `#lang typed/racket` adds a **complete static type system** on top of Racket — type checking, type inference, type errors — all implemented using macros that run at expansion time.

#### B. The Language Tower

Racket macros compose: a macro can expand into code that uses other macros. This creates a **tower of abstractions** where each layer defines the next:

```
Your DSL
  ↓ (your macros)
Racket surface forms (define, let, cond, match, ...)
  ↓ (standard macros)
Core Racket (~12 primitive forms)
  ↓ (compiler)
Chez Scheme bytecode / native code
```

Every `define`, `let`, `cond`, and `match` in Racket is itself a macro that expands to a small core. Your macros sit at the top of this tower, indistinguishable from built-in forms.

#### C. Phase Separation

Racket strictly separates **compile-time** (phase 1) from **runtime** (phase 0). Macros run at compile time and can import their own libraries that are only available during compilation:

```racket
(require (for-syntax racket/list     ;; available at compile time only
                     racket/string))

(define-syntax (generate-dispatch stx)
  ;; Can use racket/list and racket/string functions HERE, at compile time
  ;; These libraries are NOT loaded at runtime
  ...)
```

This prevents **phase confusion** — a common source of bugs in other macro systems where compile-time and runtime code accidentally share state.

Phases extend further: phase 2 (macros that generate macros), phase 3, and so on. Each phase is fully isolated.

#### D. Source Location Tracking

Every syntax object in Racket carries its **source location** (file, line, column). When a macro transforms code, the generated syntax retains location information pointing back to the original source.

This means:
- **Error messages** from macro-generated code point to the user's source, not the macro's template
- **Debugger stepping** works through macro expansions
- **DrRacket's macro stepper** can visualize each expansion step

```racket
;; If this macro is used incorrectly:
(my-for-loop "not a number"
  (displayln "hello"))

;; The error message points to "not a number" in the USER's code,
;; not to the internals of my-for-loop's implementation
```

#### E. Interposition Points

Racket macros can **interpose** on any language form. Want to add logging to every function call? Redefine `#%app`. Want to change how modules work? Redefine `#%module-begin`.

```racket
;; A language where every function application is logged:
(define-syntax (#%app stx)
  (syntax-parse stx
    [(_ f arg ...)
     #'(begin
         (printf "Calling ~a with ~a\n" 'f (list arg ...))
         (racket:#%app f arg ...))]))
```

This is how `#lang typed/racket` intercepts every expression to insert type checks.

### Real-World Macro Examples

#### Example 1: A Testing DSL

```racket
(define-syntax (check-equal? stx)
  (syntax-parse stx
    [(_ actual expected)
     #:with loc (datum->syntax stx (format "~a:~a"
                   (syntax-source stx) (syntax-line stx)))
     #'(let ([a actual] [e expected])
         (unless (equal? a e)
           (error 'test "FAIL at ~a\n  expected: ~v\n  actual:   ~v"
                  loc e a)))]))

(check-equal? (+ 1 2) 3)   ;; passes
(check-equal? (+ 1 2) 4)   ;; FAIL at test.rkt:42
                            ;;   expected: 4
                            ;;   actual:   3
```

#### Example 2: Compile-Time Validation

```racket
(define-syntax (sql stx)
  (syntax-parse stx
    [(_ query:string)
     ;; Parse the SQL at COMPILE TIME — syntax errors caught before running
     (define parsed (parse-sql (syntax-e #'query)))
     (unless parsed
       (raise-syntax-error 'sql "invalid SQL syntax" #'query))
     #'(execute-query query)]))

(sql "SELECT * FROM users WHERE id = ?")   ;; OK
(sql "SLECT * FORM users")                 ;; Compile-time error: invalid SQL syntax
```

#### Example 3: Auto-Generated Serialization

```racket
(define-syntax (define-serializable stx)
  (syntax-parse stx
    [(_ name:id (field:id ...))
     #:with to-json-name (format-id #'name "~a->json" #'name)
     #:with from-json-name (format-id #'name "json->~a" #'name)
     #'(begin
         (struct name (field ...) #:transparent)
         (define (to-json-name obj)
           (hasheq 'field (name-field obj) ...))
         (define (from-json-name ht)
           (name (hash-ref ht 'field) ...)))]))

(define-serializable person (name age email))
;; Automatically generates:
;;   (struct person (name age email))
;;   (person->json obj) → hash
;;   (json->person ht) → person
```

---

## Summary

### Choose Lambda Script When You Want:

- A lightweight, fast, type-safe scripting language
- **Data processing and document transformation** with batteries included
- Built-in parsers and formatters for 12+ formats
- CSS layout and rendering (Radiant engine)
- Clean purity separation (`fn` vs `pn`)
- Static typing without boilerplate
- Quick startup and small runtime footprint

### Choose Racket When You Want:

- **Metaprogramming power** — extending the language itself
- To **design new languages or DSLs** with `#lang`
- A mature ecosystem with decades of libraries
- World-class macro system for compile-time abstractions
- Educational tools (DrRacket) for teaching programming
- Programming language research and experimentation

### The Core Tradeoff

Lambda Script trades Racket's macro system and language extensibility for:
- More approachable C-family syntax
- Static types with inference
- Built-in document processing pipeline
- Stricter purity model enforced by the compiler
- Smaller, faster runtime

Racket trades Lambda Script's domain-specific strengths for:
- Unlimited syntactic extensibility
- The ability to host entirely new languages
- A "tower of languages" architecture
- Decades of research and battle-tested libraries
