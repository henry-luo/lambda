# Proposal: Formal Semantics for Lambda Script

## Problem Statement

The Lambda Script transpiler (Lambda → C → C2MIR → native) has an ongoing class of bugs in type inference, boxing/unboxing, and code generation that are only caught by ad-hoc test scripts. Recent examples:

| Issue | Category | Bug |
|-------|----------|-----|
| #15 | Boxing | `if-else` ternary arms have `String*` vs `Item` — C2MIR type mismatch |
| #16 | Param passing | `: string` annotation makes C param `String*`, callers pass `Item` |
| #18 | Parsing | `//` comment parsed as division operator |

These bugs share a pattern: **the gap between Lambda's high-level semantics and the transpiled C representation is not formally specified**, so each code path is hand-checked and errors slip through silently.

### Goals

1. **Define** Lambda's operational semantics formally — what each expression *means*.
2. **Validate** that expected test outputs (`test/lambda/*.txt`) agree with the semantics.
3. **Verify** that the transpiler's type inference, boxing/unboxing, and code generation preserve semantic correctness.

---

## Framework Comparison

### Candidates

| Framework | Based On | Strengths | Weaknesses | Maturity |
|-----------|----------|-----------|------------|----------|
| **PLT Redex** | Racket | Purpose-built for PL modeling; built-in random testing (`redex-check`); fast iteration; great visualization | No mechanized proofs; Racket-only | Very mature, 20+ years |
| **K Framework** | Rewriting logic | Full language semantics → auto-generated interpreter, model checker; used for real languages (C11, Java, JS, Python) | Steep learning curve; heavy tooling; slower iteration | Mature, industrial |
| **Lean 4** | Dependent type theory | Mechanized proofs; strong type system; growing ecosystem | Extreme effort for real languages; slow to encode | Active, growing |
| **Coq** | Calculus of Constructions | Gold standard for mechanized proofs (CompCert) | Very steep learning curve; enormous encoding effort | Very mature |
| **Agda** | Dependent type theory | Elegant; extractable Haskell code | Small community; less tooling | Mature, niche |
| **Maude** | Rewriting logic | General rewriting; good for concurrency | Less PL-specific tooling than K | Mature |
| **Alloy** | Relational logic | Excellent counterexample finding | Not designed for operational semantics | Mature |

### Evaluation Criteria for Lambda

| Criterion | Weight | Rationale |
|-----------|--------|-----------|
| Effort to encode Lambda's features | High | Lambda has 20+ types, higher-order functions, closures, error handling, pipes |
| Testing/counterexample generation | High | Primary goal is *finding bugs*, not proving theorems |
| Ability to model the C representation layer | High | Need to reason about `Item`, `String*`, boxing/unboxing |
| Iteration speed | Medium | Semantics will evolve as Lambda evolves |
| Mechanized proof capability | Low | Not the primary goal at this stage |
| Team familiarity / learning curve | Medium | Must be maintainable |

---

## Recommendation: PLT Redex (Primary) + K Framework (Long-term)

### Why PLT Redex First

1. **Designed for exactly this**: Redex is a DSL for defining and testing programming language semantics. It directly models reduction rules, evaluation contexts, and type judgments.

2. **Built-in random testing**: `redex-check` generates random well-typed programs and checks that semantic properties hold — *this directly finds transpiler-class bugs without writing individual test cases*.

3. **Two-layer modeling**: Redex naturally supports defining both:
   - **Source semantics** (Lambda expressions → values)
   - **Target semantics** (C representation with `Item`, `String*`, boxing)
   
   Then reasoning about the relationship between them.

4. **Fast iteration**: A new reduction rule can be added and tested in minutes. Contrast with K Framework (hours) or Lean/Coq (days).

5. **Visualization**: Redex can render reduction traces, type derivation trees, and evaluation step graphs — invaluable for debugging the semantics itself.

6. **Proven track record**: Used extensively in PL research for languages of comparable complexity (Typed Racket, contracts, gradual typing).

### Why Not K Framework (Yet)

K Framework is more powerful — it can auto-generate a reference interpreter from the semantics — but the startup cost is significantly higher. Once the Redex model stabilizes, porting the core semantics to K becomes a viable Phase 3 goal for getting an auto-generated reference interpreter and more comprehensive model checking.

### Why Not Lean/Coq

Mechanized proofs of transpiler correctness (à la CompCert) require 10-100x the effort of Redex modeling. It's premature for Lambda's current stage. The Redex model can later serve as the specification for a Lean/Coq formalization if desired.

---

## Architecture: Three-Layer Semantic Model

```
┌──────────────────────────────────────────────┐
│  Layer 1: Lambda Source Semantics (λ-calc)   │  ← "What Lambda programs mean"
│  Expressions → Values via reduction rules     │
│  Type judgments: Γ ⊢ e : τ                   │
└──────────────┬───────────────────────────────┘
               │  Semantic equivalence
               ▼
┌──────────────────────────────────────────────┐
│  Layer 2: C Representation Semantics         │  ← "What the transpiler produces"
│  Item, String*, int32_t, boxing/unboxing     │
│  Type mapping: τ → C-type                    │
│  Boxing: box(v, τ) → Item                    │
│  Unboxing: unbox(Item, τ) → v               │
└──────────────┬───────────────────────────────┘
               │  Verified correspondence
               ▼
┌──────────────────────────────────────────────┐
│  Layer 3: Test Oracle                        │  ← "Do *.txt files match?"
│  eval(program) = expected_output             │
│  Compares Redex evaluation against *.txt     │
└──────────────────────────────────────────────┘
```

---

## Phase 1: Core Lambda Source Semantics

### 1.1 Value Domain

Define the Lambda value domain as a Redex language:

```racket
(define-language Lambda
  ;; ─── Types ───
  (τ ::= int float bool string symbol null
         (→ τ ... τ)           ; function type
         (array τ)             ; array type  
         (list τ ...)          ; list/tuple type
         (map (x τ) ...)       ; map type
         (union τ τ)           ; union type
         (optional τ)          ; τ | null
         any error)

  ;; ─── Values ───
  (v ::= n                    ; integer literal
         r                    ; float literal
         b                    ; bool literal
         s                    ; string literal
         sym                  ; symbol literal
         null
         (array v ...)        ; array value
         (list v ...)         ; list/tuple value
         (map (x v) ...)      ; map value
         (closure ρ (x ...) e) ; closure value
         (error string))      ; error value

  ;; ─── Expressions ───
  (e ::= v                    ; literal
         x                    ; variable
         (let ((x e) ...) e)  ; let binding
         (if e e e)           ; conditional (requires else)
         (op e e)             ; binary operation
         (app e e ...)        ; function application
         (fn (x ...) e)       ; lambda abstraction
         (member e x)         ; field access: e.x
         (index e e)          ; index access: e[e]
         (for x e e)          ; for comprehension
         (pipe e e)           ; pipe: e | e
         (where e e)          ; filter: e where e
         (match e clause ...) ; match expression
         (concat e e)         ; ++ operator
         (spread e)           ; *e spread
         )

  ;; ─── Operators ───
  (op ::= + - * / div % ^ == != < <= > >= and or ++ to in is)

  ;; ─── Environments ───
  (ρ ::= ((x v) ...))

  ;; ─── Auxiliary ───
  (x ::= variable-not-otherwise-mentioned)
  (n ::= integer)
  (r ::= real)
  (b ::= boolean)
  (s ::= string)
  (sym ::= (symbol string))

  ;; ─── Match clauses ───
  (clause ::= (case-type τ e)
              (case-literal v e)
              (default e)))
```

### 1.2 Core Reduction Rules

```racket
(define lambda-red
  (reduction-relation Lambda

   ;; ── Arithmetic ──
   (--> (op + (n_1) (n_2))     ,(+ (term n_1) (term n_2))     "add-int")
   (--> (op + (r_1) (r_2))     ,(+ (term r_1) (term r_2))     "add-float")
   (--> (op * (n_1) (n_2))     ,(* (term n_1) (term n_2))     "mul-int")

   ;; ── String concatenation (++ operator, NOT +) ──
   (--> (concat (s_1) (s_2))   ,(string-append (term s_1) (term s_2))  "concat-str")

   ;; ── Comparison ──
   (--> (op == v_1 v_2)        ,(equal? (term v_1) (term v_2))  "eq")
   (--> (op < (n_1) (n_2))     ,(< (term n_1) (term n_2))      "lt-int")

   ;; ── Boolean ──
   (--> (op and #t e)          e                                 "and-true")
   (--> (op and #f e)          #f                                "and-false")

   ;; ── Conditional ──
   (--> (if #t e_1 e_2)        e_1                               "if-true")
   (--> (if #f e_1 e_2)        e_2                               "if-false")

   ;; ── Let ──
   (--> (let ((x_1 v_1) ...) e)
        ,(subst* (term (x_1 ...)) (term (v_1 ...)) (term e))
        "let-subst")

   ;; ── Application ──
   (--> (app (closure ρ (x ...) e) v ...)
        ,(subst-env (extend-env (term ρ) (term (x ...)) (term (v ...))) (term e))
        "beta")

   ;; ── For comprehension ──
   (--> (for x (array v_1 ... v_n) e)
        (array (subst x v_1 e) ... (subst x v_n e))
        "for-array")

   ;; ── Pipe ──
   ;; e1 | e2  where e2 contains ~ (current item)
   ;; For collections: map e2 over e1
   ;; For scalars: apply e2 to e1
   ))
```

### 1.3 Type Judgment Rules

```racket
(define-judgment-form Lambda
  #:mode (typeof I I O)
  #:contract (typeof ρ e τ)

  ;; ── Literals ──
  [(typeof ρ n int)]
  [(typeof ρ r float)]
  [(typeof ρ b bool)]
  [(typeof ρ s string)]
  [(typeof ρ null null)]

  ;; ── Variable ──
  [(typeof ρ x τ)
   (where τ (lookup ρ x))]

  ;; ── If-else (both branches must have compatible types) ──
  [(typeof ρ e_cond bool)
   (typeof ρ e_then τ_1)
   (typeof ρ e_else τ_2)
   (where τ (join τ_1 τ_2))
   ───────────────────────────
   (typeof ρ (if e_cond e_then e_else) τ)]

  ;; ── Function application ──
  [(typeof ρ e_fn (→ τ_param ... τ_ret))
   (typeof ρ e_arg τ_param) ...
   ───────────────────────────
   (typeof ρ (app e_fn e_arg ...) τ_ret)]

  ;; ── Let binding ──
  [(typeof ρ e_init τ_init) ...
   (typeof (extend ρ (x τ_init) ...) e_body τ_body)
   ───────────────────────────
   (typeof ρ (let ((x e_init) ...) e_body) τ_body)]
  )
```

### 1.4 Key Semantic Properties to Test

```racket
;; Property: if-else returns the same type regardless of branch taken
(redex-check Lambda
  #:satisfying (typeof () (if e_cond e_then e_else) τ)
  (equal? (typeof-result (if #t e_then e_else))
          (typeof-result (if #f e_then e_else))))

;; Property: for-comprehension preserves element type
(redex-check Lambda
  #:satisfying (typeof () (for x (array v ...) e_body) (array τ))
  (for/and ([v_i (term (v ...))])
    (judgment-holds (typeof ((x τ_elem)) e_body τ))))

;; Property: concat of two strings is a string
(redex-check Lambda
  (typeof () (concat e_1 e_2) string)
  #:when (and (judgment-holds (typeof () e_1 string))
              (judgment-holds (typeof () e_2 string))))
```

---

## Phase 2: C Representation Layer (Boxing/Unboxing)

This is where the transpiler bugs live. We model the C-level representation to verify correctness.

### 2.1 C-Type Domain

```racket
(define-language CRepr
  ;; ─── C-level types ───
  (C-type ::= Item              ; 64-bit tagged union (the universal type)
              int32_t            ; unboxed 32-bit integer
              int64_t            ; unboxed 64-bit integer
              double             ; unboxed 64-bit float
              bool               ; unboxed boolean
              String*            ; pointer to String struct
              Symbol*            ; pointer to Symbol struct
              Array*             ; pointer to Array struct
              List*              ; pointer to List struct
              Map*               ; pointer to Map struct
              Element*           ; pointer to Element struct
              Function*          ; pointer to Function struct
              Decimal*           ; pointer to Decimal struct
              DateTime)          ; inline DateTime struct

  ;; ─── Boxing operations ───
  (box-op ::= i2it               ; int → Item (inline, tag in high bits)
              s2it               ; String* → Item (tag pointer)
              push_d             ; double → Item (store in num_stack, return ptr)
              push_l             ; int64 → Item (store in num_stack, return ptr)
              b2it               ; bool → Item
              sym2it             ; Symbol* → Item
              const_s2it)        ; const String* → Item

  ;; ─── Unboxing operations ───
  (unbox-op ::= it2i             ; Item → int32_t
                it2l             ; Item → int64_t
                it2d             ; Item → double
                it2s             ; Item → String*
                it2b))           ; Item → bool
```

### 2.2 Type Mapping Function

The critical specification: how Lambda types map to C types.

```racket
;; type-to-ctype : τ → C-type
;; This is what write_type() in print.cpp implements
(define (type-to-ctype τ)
  (match τ
    ['int      'int32_t]
    ['int64    'int64_t]
    ['float    'double]
    ['bool     'bool]
    ['string   'String*]
    ['binary   'String*]
    ['symbol   'Symbol*]
    ['decimal  'Decimal*]
    ['datetime 'DateTime]
    [(list 'array _) 'Array*]
    ['list     'List*]
    ['map      'Map*]
    ['element  'Element*]
    ['any      'Item]
    ['null     'Item]
    ['error    'Item]
    [_         'Item]))
```

### 2.3 Boxing/Unboxing Correctness Rules

These are the rules the transpiler must respect. **Violations are transpiler bugs.**

```racket
;; Rule 1: Function parameters — when caller type ≠ param C-type, must convert
;; If param expects C-type T and arg has C-type S where S ≠ T:
;;   - If T = Item:        box(arg)           → transpile_box_item
;;   - If T = String*:     it2s(box(arg))     → Issue #16 fix
;;   - If T = int32_t:     it2i(arg) if arg is Item
;;   - If T = double:      it2d(arg) if arg is Item
;;   - If T = bool:        it2b(arg) if arg is Item

;; Rule 2: If-else ternary arms must have identical C-types
;; If type-to-ctype(τ_then) ≠ type-to-ctype(τ_else):
;;   box both arms to Item    → Issue #15 fix
;; EVEN IF semantic types match (STRING), because C return types can differ
;;   (fn_string → String*, fn_str_join → Item)

;; Rule 3: Variable assignment — C-type of LHS must match RHS
;; If var C-type is String* but RHS returns Item:
;;   must wrap with it2s()

;; Rule 4: System function return types
;; The *semantic* return type (in sys_func_info) and the *C* return type
;; (in lambda.h) may differ. Known cases:
;;   fn_string()      → String*   (matches STRING semantic type)
;;   fn_str_join()    → Item      (MISMATCHES STRING semantic type)
;;   fn_trim()        → Item      (MISMATCHES STRING semantic type)
;;   fn_lower()       → Item      (MISMATCHES STRING semantic type)
;; The transpiler must never assume semantic type == C return type for system funcs.

;; Rule 5: Unboxed function variants (_u suffix)
;; Only generate _u variant when the body's C return type matches the declared
;; return C-type. Currently safe only for INT and INT64.
```

### 2.4 Verification Properties

```racket
;; Property: roundtrip — unbox(box(v, τ), τ) = v
(redex-check CRepr
  (term (unbox (box v τ) τ))
  (equal? (eval-crepr (term (unbox (box v τ) τ))) (term v)))

;; Property: if-else arms have same C-type after transpilation
(redex-check Lambda+CRepr
  #:satisfying (typeof () (if e_cond e_then e_else) τ)
  (let ([ct_then (expr-ctype e_then)]
        [ct_else (expr-ctype e_else)])
    (or (equal? ct_then ct_else)        ; same C-type: OK
        (equal? ct_then 'Item)           ; both boxed: OK
        (equal? ct_else 'Item))))

;; Property: call args match param C-types
(redex-check Lambda+CRepr
  #:satisfying (typeof () (app e_fn e_arg ...) τ_ret)
  (for/and ([arg (term (e_arg ...))]
            [param-ctype (fn-param-ctypes e_fn)])
    (compatible-ctype? (expr-ctype arg) param-ctype)))
```

---

## Phase 3: Test Oracle — Validating *.txt Files

### 3.1 Approach

Use the Redex evaluator as a **reference interpreter** to verify that `test/lambda/*.txt` files contain correct expected outputs.

```racket
;; Parse a .ls file into Redex AST
(define (parse-lambda-file path)
  (let ([src (file->string path)])
    (lambda-parse src)))

;; Evaluate to a value using the formal semantics
(define (eval-lambda-file path)
  (let ([ast (parse-lambda-file path)])
    (eval-lambda ast)))

;; Compare against expected .txt
(define (verify-test-file ls-path txt-path)
  (let ([actual (format-output (eval-lambda-file ls-path))]
        [expected (file->string txt-path)])
    (check-equal? actual expected
      (format "Mismatch for ~a" ls-path))))

;; Run all tests
(define (verify-all-tests)
  (for ([ls-file (directory-list "test/lambda" #rx"\\.ls$")])
    (let ([txt-file (path-replace-extension ls-file ".txt")])
      (when (file-exists? txt-file)
        (verify-test-file ls-file txt-file)))))
```

### 3.2 Test Categories to Formalize

| Category | Test Files | Semantic Focus |
|----------|-----------|----------------|
| Arithmetic | `expr.ls`, `numeric_expr.ls` | Operator semantics, type promotion |
| String ops | `string.ls`, `string_funcs.ls` | Concatenation (++), member access |
| Collections | `value.ls`, `spread.ls` | Array/list/map construction, indexing |
| Control flow | `if_expr_types.ls`, `match_expr.ls` | Branch typing, pattern matching |
| Functions | `func.ls`, `closure.ls`, `tail_call.ls` | Application, closures, TCO |
| Type system | `type.ls`, `type2.ls`, `type_pattern.ls` | Type inference, type checking |
| For-expressions | `for_clauses_test.ls`, `for_decompose.ls` | Comprehension semantics |
| Pipes | `pipe_where.ls` | Pipe/where evaluation |
| Errors | `error_handling.ls`, `error_propagation.ls` | Error-as-value, `?`, `^err` |
| Modules | `import.ls`, `import_chain.ls` | Module resolution, pub exports |
| Boxing | `box_unbox.ls`, `typed_param_string.ls` | Representation correctness |

---

## Phase 4: Transpiler Verification (Future)

Once Phases 1-3 are stable, extend to verify transpiler code generation:

### 4.1 Transpilation Relation

Define a formal relation `transpile : e × Γ → C-code` and prove:

```
∀ e, Γ, v.
  eval_lambda(e, Γ) = v  ⟹  eval_C(transpile(e, Γ)) = repr(v)
```

Where `repr(v)` maps Lambda values to their C representation.

### 4.2 What to Verify

| Component | Formalization | Catches |
|-----------|---------------|---------|
| `transpile_if()` | Ternary arm C-type reconciliation | Issue #15-class bugs |
| `transpile_call_argument()` | Param type conversion | Issue #16-class bugs |
| `transpile_assign_expr()` | Variable type assignment | Boxing mismatches |
| `transpile_box_item()` | Boxing correctness | Incorrect tag bits |
| `write_type()` | τ → C-type mapping | Type mapping errors |
| `transpile_binary_expr()` | Operator dispatch | Wrong function for type combo |
| `define_func_unboxed()` | _u variant safety | Broken unboxed variants |

### 4.3 Possible Approach: Property-Based Oracle Testing

Even without full transpiler verification, we can use the Redex model as a test oracle:

```
1. Generate random well-typed Lambda programs (redex-check)
2. Evaluate them with the Redex semantics → expected result
3. Transpile and execute with the real Lambda engine → actual result
4. Compare expected vs actual
```

This is essentially **differential testing** with a formally-grounded reference implementation.

---

## Implementation Plan

### Phase 1: Core Source Semantics (4-6 weeks)

| Week | Deliverable |
|------|-------------|
| 1 | Redex language definition: values, expressions, types |
| 2 | Core reduction rules: arithmetic, comparisons, booleans, let |
| 3 | Functions: application, closures, named params, defaults |
| 4 | Collections: arrays, maps, for-comprehensions, pipes |
| 5 | Control flow: if-else, match, error handling |
| 6 | Type judgment rules, type inference |

### Phase 2: C Representation Layer (2-3 weeks)

| Week | Deliverable |
|------|-------------|
| 7 | C-type domain, type mapping function, boxing/unboxing rules |
| 8 | Boxing correctness properties, redex-check tests |
| 9 | System function C-type catalog, call-site verification rules |

### Phase 3: Test Oracle (2-3 weeks)

| Week | Deliverable |
|------|-------------|
| 10 | Lambda parser → Redex AST converter (subset) |
| 11 | Verify existing *.txt files against Redex evaluator |
| 12 | Property-based differential testing harness |

### Phase 4: Transpiler Verification (ongoing)

| Week | Deliverable |
|------|-------------|
| 13+ | Formalize transpile relation for critical code paths |
| | Random program generation + differential testing |
| | (Optional) Port core semantics to K Framework for reference interpreter |

---

## File Structure

```
lambda/
  semantics/                     # New directory
    lambda-core.rkt              # Phase 1: Core language definition
    lambda-types.rkt             # Phase 1: Type judgment rules
    lambda-eval.rkt              # Phase 1: Reduction semantics / evaluator
    c-repr.rkt                   # Phase 2: C representation model
    boxing-rules.rkt             # Phase 2: Boxing/unboxing verification
    test-oracle.rkt              # Phase 3: Test file verification
    differential-test.rkt        # Phase 3: Random program testing
    README.md                    # Setup instructions
```

### Prerequisites

```bash
# Install Racket (includes PLT Redex)
brew install minimal-racket      # macOS
# or
apt install racket               # Linux

# Install Redex
raco pkg install redex

# Run semantics tests
racket lambda/semantics/lambda-core.rkt
racket lambda/semantics/test-oracle.rkt
```

---

## Scope Limitations

This proposal focuses on the **functional core** of Lambda (`fn` functions). The following are deferred:

- **Procedural functions** (`pn`): Mutable state and side effects require a more complex semantic model (state-passing style or monadic semantics).
- **Module system**: Import resolution is a separate concern from expression semantics.
- **I/O operations**: `input()`, `output()`, `fetch()` are effectful and would need an I/O monad model.
- **CSS layout engine** (Radiant): Entirely separate domain.
- **Tree-sitter grammar**: Parsing correctness is orthogonal to semantic correctness (though Redex can model grammars if needed).

The functional core covers the vast majority of transpiler bugs (15, 16, 18 were all in functional expression transpilation).

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Redex model diverges from real Lambda semantics | High | Medium | Keep model focused on core; sync with doc changes |
| Effort exceeds benefit for rare bugs | Medium | Medium | Start with differential testing (Phase 3 shortcut) — highest ROI |
| Lambda evolves faster than the model | Medium | Low | Model the stable core first; add features incrementally |
| Parser (Tree-sitter) → Redex AST conversion is lossy | Medium | Medium | Start with a subset; expand as needed |

### Recommended Starting Point

If full Phase 1-3 isn't feasible initially, the **highest-ROI shortcut** is:

1. Write a minimal Redex evaluator for Lambda's expression core (~2 weeks)
2. Implement differential testing: generate random programs → compare Redex eval vs `./lambda.exe` output
3. Every mismatch is a potential transpiler bug

This alone would have caught Issues #15 and #16 via random testing, without needing full type system formalization.

---

## Summary

| Aspect | Decision |
|--------|----------|
| **Primary framework** | PLT Redex (Racket) |
| **Long-term option** | K Framework (auto-generated reference interpreter) |
| **Not recommended now** | Lean/Coq (mechanized proofs — premature) |
| **Architecture** | Three-layer: source semantics → C representation → test oracle |
| **Highest ROI** | Differential testing with minimal Redex evaluator |
| **Timeline** | 12 weeks for Phases 1-3; ongoing for Phase 4 |
