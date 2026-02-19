#lang racket
;; ============================================================================
;; Lambda Script — Core Language Definition (PLT Redex)
;;
;; This file defines the abstract syntax, value domain, and environments
;; for the Lambda Script functional core.
;;
;; Reference: doc/Lambda_Reference.md, doc/Lambda_Data.md
;; Verified against: lambda/lambda-data.hpp, lambda/lambda-eval.cpp
;; ============================================================================

(require redex)

;; ────────────────────────────────────────────────────────────────────────────
;; 1. LANGUAGE GRAMMAR
;; ────────────────────────────────────────────────────────────────────────────

(define-language Lambda

  ;; ── Primitive type tags ──
  ;; Matches TypeId enum in lambda.h (values 1–24)
  (base-type ::= null-type bool-type int-type int64-type float-type
                 decimal-type number-type
                 string-type symbol-type binary-type
                 datetime-type path-type
                 list-type array-type map-type element-type
                 range-type func-type type-type
                 error-type any-type)

  ;; ── Composite type expressions ──
  (τ ::= base-type
         (union τ τ)         ; T | U
         (nullable τ)        ; T? = T | null
         (array-of τ)        ; [T]
         (list-of τ ...)     ; (T1, T2, ...) — tuple
         (map-of (x τ) ...)  ; {k1: T1, k2: T2, ...}
         (fn-type (τ ...) τ) ; fn (T1, ...) -> Tret
         (error-ret τ)       ; T^ — may return error
         (error-ret-typed τ τ) ; T^E — specific error type
         )

  ;; ── Values (fully evaluated) ──
  (v ::= null                  ; the null value
         boolean               ; #t or #f
         integer               ; Racket exact integers (models 56-bit int)
         number                ; Racket inexact reals (models 64-bit float)
         string                ; Racket strings (models UTF-8 String*)
         (sym string)          ; symbol with name
         (array-val v ...)     ; array value [v1, v2, ...]
         (list-val v ...)      ; list/tuple value (v1, v2, ...)
         (map-val (x v) ...)   ; map value {k1: v1, k2: v2, ...}
         (range-val v v)       ; range start..end (inclusive)
         (closure ρ (p ...) e) ; closure: env, params, body
         (error-val string integer) ; error: message, code
         (type-val τ)          ; first-class type value
         )

  ;; ── Parameters ──
  (p ::= x                    ; required parameter
         (typed x τ)           ; typed parameter x: T
         (opt x)               ; optional parameter x?
         (default x e)         ; parameter with default x = e
         (typed-default x τ e) ; typed with default x: T = e
         )

  ;; ── Expressions ──
  (e ::= v                    ; literal value
         x                    ; variable reference
         ~                    ; pipe current item
         ~#                   ; pipe current index/key

         ;; binding & control flow
         (let ((x e) ...) e)             ; let binding (parallel)
         (let-seq ((x e) ...) e)         ; let binding (sequential, later can ref earlier)
         (let-typed ((x τ e) ...) e)     ; typed let binding
         (if e e e)                      ; if-else (required else for fn)
         (if-stam e e)                   ; if without else (statement, evaluates to null)

         ;; functions
         (lam (p ...) e)                 ; anonymous function (λ)
         (app e e ...)                   ; function application

         ;; arithmetic & logic (binary)
         (add e e)        ; +
         (sub e e)        ; -
         (mul e e)        ; *
         (fdiv e e)       ; / (float division, always returns float)
         (idiv e e)       ; div (integer/floor division)
         (mod e e)        ; %
         (pow e e)        ; ^

         ;; comparison
         (eq e e)         ; ==
         (neq e e)        ; !=
         (lt e e)         ; <
         (le e e)         ; <=
         (gt e e)         ; >
         (ge e e)         ; >=

         ;; logical (short-circuit)
         (l-and e e)      ; and
         (l-or e e)       ; or
         (l-not e)        ; not

         ;; unary
         (neg e)          ; -e (arithmetic negate)

         ;; string / collection
         (concat e e)     ; ++ (string concat, array concat, etc.)

         ;; range
         (to-range e e)   ; a to b (inclusive range)

         ;; type operations
         (is-type e τ)    ; e is T → bool
         (is-not-type e τ)  ; e is not T → bool
         (in-coll e e)    ; e in collection → bool
         (as-type e τ)    ; e as T (unsafe cast)

         ;; collection construction
         (array e ...)    ; [e1, e2, ...]
         (list-expr e ...)  ; (e1, e2, ...)
         (map-expr (x e) ...)  ; {k1: e1, k2: e2, ...}

         ;; collection access
         (member e x)     ; e.field
         (index e e)      ; e[i]
         (slice e e e)    ; e[i to j]

         ;; spread
         (spread e)       ; *e

         ;; for expression
         (for x e e)                    ; for (x in coll) body
         (for-where x e e e)            ; for (x in coll where pred) body
         (for-idx x x e e)              ; for (i, x in coll) body
         (for-at x e e)                 ; for (k at map) body
         (for-at-kv x x e e)            ; for (k, v at map) body

         ;; pipe & filter
         (pipe e e)       ; e | transform (with ~ and ~#)
         (pipe-agg e e)   ; e | fn (aggregate, no ~)
         (where e e)      ; e where predicate

         ;; match
         (match e clause ...)

         ;; error handling
         (make-error e)         ; error("msg")
         (make-error-2 e e)     ; error("msg", source)
         (raise-expr e)         ; raise e
         (try-prop e)           ; e? (propagate error)
         (let-err x x e e)     ; let a^err = expr in body
         (is-error e)           ; ^e

         ;; type conversion builtins
         (to-int e)
         (to-float e)
         (to-string e)
         (to-symbol e)
         (to-bool e)

         ;; collection builtins
         (len-expr e)
         (sum-expr e)
         (sort-expr e)
         (reverse-expr e)
         (unique-expr e)
         (take-expr e e)
         (drop-expr e e)
         )

  ;; ── Match clauses ──
  (clause ::= (case-type τ e)       ; case T: body
              (case-val v e)         ; case literal: body
              (case-range v v e)     ; case a to b: body
              (case-union τ τ e)     ; case T | U: body
              (default-case e))      ; default: body

  ;; ── Environments ──
  ;; Association list: variable → value
  (ρ ::= ((x v) ...))

  ;; ── Variables ──
  (x ::= variable-not-otherwise-mentioned)

  ;; ── Evaluation contexts (for small-step if needed later) ──
  (E ::= hole
         (add E e) (add v E)
         (sub E e) (sub v E)
         (mul E e) (mul v E)
         (fdiv E e) (fdiv v E)
         (eq E e) (eq v E)
         (concat E e) (concat v E)
         (app E e ...) (app v ... E e ...)
         (if E e e)
         (array v ... E e ...)
         (index E e) (index v E)
         (member E x)
         )
  )


;; ────────────────────────────────────────────────────────────────────────────
;; 2. HELPER METAFUNCTIONS
;; ────────────────────────────────────────────────────────────────────────────

;; ── Environment lookup ──
(define-metafunction Lambda
  env-lookup : ρ x -> v or #f
  [(env-lookup ((x_0 v_0) (x_1 v_1) ... ) x_0) v_0]
  [(env-lookup ((x_0 v_0) (x_1 v_1) ... ) x_k)
   (env-lookup ((x_1 v_1) ... ) x_k)]
  [(env-lookup () x) null])    ; undefined variable → null (per Lambda semantics)

;; ── Environment extension ──
(define-metafunction Lambda
  env-extend : ρ x v -> ρ
  [(env-extend ((x_0 v_0) ...) x_new v_new)
   ((x_new v_new) (x_0 v_0) ...)])

;; ── Multi-variable environment extension ──
(define-metafunction Lambda
  env-extend* : ρ (x ...) (v ...) -> ρ
  [(env-extend* ρ () ()) ρ]
  [(env-extend* ρ (x_0 x_1 ...) (v_0 v_1 ...))
   (env-extend* (env-extend ρ x_0 v_0) (x_1 ...) (v_1 ...))])


;; ────────────────────────────────────────────────────────────────────────────
;; 3. TRUTHINESS
;; ────────────────────────────────────────────────────────────────────────────
;; Per Lambda specification (doc/Lambda_Reference.md):
;;   Falsy: null, false, error values, "" (empty string), '' (empty symbol)
;;   Truthy: true, ALL numbers (including 0), non-empty strings/symbols,
;;           all collections, all functions, types, datetimes
;;
;; NOTE: Implementation (it2b in lambda-data.cpp) currently treats 0 as falsy.
;;       This is a known discrepancy. The formal model follows the spec.

(define-metafunction Lambda
  truthy? : v -> boolean
  ;; falsy cases
  [(truthy? null)                      #f]
  [(truthy? #f)                        #f]
  [(truthy? (error-val string integer)) #f]
  [(truthy? "")                        #f]
  [(truthy? (sym ""))                  #f]

  ;; everything else is truthy
  [(truthy? v)                         #t])


;; ────────────────────────────────────────────────────────────────────────────
;; 4. TYPE-OF (runtime type tag)
;; ────────────────────────────────────────────────────────────────────────────
;; Corresponds to get_type_id() in lambda-data.hpp

(define-metafunction Lambda
  type-of-val : v -> base-type
  [(type-of-val null)                        null-type]
  [(type-of-val #t)                          bool-type]
  [(type-of-val #f)                          bool-type]
  [(type-of-val integer)                     int-type]
  [(type-of-val number)                      float-type]
  [(type-of-val string)                      string-type]
  [(type-of-val (sym string))                symbol-type]
  [(type-of-val (array-val v ...))           array-type]
  [(type-of-val (list-val v ...))            list-type]
  [(type-of-val (map-val (x v) ...))         map-type]
  [(type-of-val (range-val v_1 v_2))         range-type]
  [(type-of-val (closure ρ (p ...) e))       func-type]
  [(type-of-val (error-val string integer))  error-type]
  [(type-of-val (type-val τ))                type-type])


;; ────────────────────────────────────────────────────────────────────────────
;; 5. TYPE COMPATIBILITY (is operator)
;; ────────────────────────────────────────────────────────────────────────────
;; Corresponds to fn_is() in lambda-eval.cpp
;; Numeric subtyping: int <: int64 <: float <: number <: any

(define-metafunction Lambda
  is-compatible? : v τ -> boolean

  ;; any matches everything
  [(is-compatible? v any-type) #t]

  ;; null
  [(is-compatible? null null-type) #t]

  ;; booleans
  [(is-compatible? #t bool-type) #t]
  [(is-compatible? #f bool-type) #t]

  ;; int is also int64, float, number
  [(is-compatible? integer int-type) #t]
  [(is-compatible? integer int64-type) #t]
  [(is-compatible? integer float-type) #t]
  [(is-compatible? integer number-type) #t]

  ;; float is also number
  [(is-compatible? number float-type) #t]
  [(is-compatible? number number-type) #t]

  ;; string, symbol
  [(is-compatible? string string-type) #t]
  [(is-compatible? (sym string) symbol-type) #t]

  ;; collections
  [(is-compatible? (array-val v ...) array-type) #t]
  [(is-compatible? (list-val v ...) list-type) #t]
  [(is-compatible? (map-val (x v) ...) map-type) #t]
  [(is-compatible? (range-val v_1 v_2) range-type) #t]

  ;; functions
  [(is-compatible? (closure ρ (p ...) e) func-type) #t]

  ;; errors
  [(is-compatible? (error-val string integer) error-type) #t]

  ;; types
  [(is-compatible? (type-val τ) type-type) #t]

  ;; nullable: T? = T | null
  [(is-compatible? null (nullable τ)) #t]
  [(is-compatible? v (nullable τ)) (is-compatible? v τ)]

  ;; union: T | U
  [(is-compatible? v (union τ_1 τ_2))
   ,(or (term (is-compatible? v τ_1))
        (term (is-compatible? v τ_2)))]

  ;; default: not compatible
  [(is-compatible? v τ) #f])


;; ────────────────────────────────────────────────────────────────────────────
;; 6. NUMERIC PROMOTION
;; ────────────────────────────────────────────────────────────────────────────
;; Corresponds to promotion logic in lambda-eval-num.cpp
;; int + float → float (promote int to double)

(define-metafunction Lambda
  promote-to-float : v -> number
  [(promote-to-float integer) ,(exact->inexact (term integer))]
  [(promote-to-float number) number])

(define-metafunction Lambda
  numeric? : v -> boolean
  [(numeric? integer) #t]
  [(numeric? number) #t]
  [(numeric? v) #f])


;; ────────────────────────────────────────────────────────────────────────────
;; 7. MAP ACCESS
;; ────────────────────────────────────────────────────────────────────────────
;; Null-safe: missing key → null (per Lambda spec)

(define-metafunction Lambda
  map-get : v x -> v
  [(map-get (map-val (x_0 v_0) ... (x_k v_k) (x_n v_n) ...) x_k) v_k]
  [(map-get (map-val (x v) ...) x_k) null]   ; key not found → null
  [(map-get null x) null])                     ; null-safe access


;; ────────────────────────────────────────────────────────────────────────────
;; 8. ARRAY/LIST ACCESS
;; ────────────────────────────────────────────────────────────────────────────
;; Out-of-bounds → null (per Lambda spec)

(define-metafunction Lambda
  array-get : v integer -> v
  [(array-get (array-val v ...) integer)
   ,(let ([items (term (v ...))]
          [idx (term integer)])
      (cond
        [(and (>= idx 0) (< idx (length items)))
         (list-ref items idx)]
        ;; negative indexing: -1 = last
        [(and (< idx 0) (>= (+ (length items) idx) 0))
         (list-ref items (+ (length items) idx))]
        [else (term null)]))]
  [(array-get null integer) null])

(define-metafunction Lambda
  list-get : v integer -> v
  [(list-get (list-val v ...) integer)
   ,(let ([items (term (v ...))]
          [idx (term integer)])
      (cond
        [(and (>= idx 0) (< idx (length items)))
         (list-ref items idx)]
        [(and (< idx 0) (>= (+ (length items) idx) 0))
         (list-ref items (+ (length items) idx))]
        [else (term null)]))]
  [(list-get null integer) null])


;; ────────────────────────────────────────────────────────────────────────────
;; 9. VALUE LENGTH
;; ────────────────────────────────────────────────────────────────────────────

(define-metafunction Lambda
  val-length : v -> integer
  [(val-length (array-val v ...))  ,(length (term (v ...)))]
  [(val-length (list-val v ...))   ,(length (term (v ...)))]
  [(val-length (map-val (x v) ...)) ,(length (term ((x v) ...)))]
  [(val-length string)             ,(string-length (term string))]
  [(val-length (range-val integer_1 integer_2))
   ,(max 0 (+ 1 (- (term integer_2) (term integer_1))))]
  [(val-length null) 0])


;; ────────────────────────────────────────────────────────────────────────────
;; 10. VALUE EQUALITY
;; ────────────────────────────────────────────────────────────────────────────
;; Structural equality per fn_eq() in lambda-eval.cpp
;; null == null → true; type mismatch → false (no error)

(define-metafunction Lambda
  val-eq? : v v -> boolean
  [(val-eq? null null) #t]
  [(val-eq? #t #t) #t]
  [(val-eq? #f #f) #t]
  [(val-eq? #t #f) #f]
  [(val-eq? #f #t) #f]

  ;; numeric: promote both to float for comparison
  [(val-eq? integer_1 integer_2) ,(= (term integer_1) (term integer_2))]
  [(val-eq? integer number) ,(= (exact->inexact (term integer)) (term number))]
  [(val-eq? number integer) ,(= (term number) (exact->inexact (term integer)))]
  [(val-eq? number_1 number_2) ,(= (term number_1) (term number_2))]

  ;; strings
  [(val-eq? string_1 string_2) ,(string=? (term string_1) (term string_2))]

  ;; symbols
  [(val-eq? (sym string_1) (sym string_2)) ,(string=? (term string_1) (term string_2))]

  ;; arrays: element-wise
  [(val-eq? (array-val) (array-val)) #t]
  [(val-eq? (array-val v_1 v_1r ...) (array-val v_2 v_2r ...))
   ,(and (term (val-eq? v_1 v_2))
         (term (val-eq? (array-val v_1r ...) (array-val v_2r ...))))]

  ;; type mismatch or incomparable → false
  [(val-eq? v_1 v_2) #f])


;; ────────────────────────────────────────────────────────────────────────────
;; 11. VALUE COMPARISON (<, <=, >, >=)
;; ────────────────────────────────────────────────────────────────────────────
;; Type mismatch → error (BOOL_ERROR in implementation)

(define-metafunction Lambda
  val-lt? : v v -> v ; returns #t, #f, or error
  ;; numeric (promote if needed)
  [(val-lt? integer_1 integer_2) ,(< (term integer_1) (term integer_2))]
  [(val-lt? integer number) ,(< (exact->inexact (term integer)) (term number))]
  [(val-lt? number integer) ,(< (term number) (exact->inexact (term integer)))]
  [(val-lt? number_1 number_2) ,(< (term number_1) (term number_2))]
  ;; strings: lexicographic
  [(val-lt? string_1 string_2) ,(string<? (term string_1) (term string_2))]
  ;; type mismatch → error
  [(val-lt? v_1 v_2) (error-val "comparison type mismatch" 300)])


;; ────────────────────────────────────────────────────────────────────────────
;; EXPORTS
;; ────────────────────────────────────────────────────────────────────────────

(provide Lambda
         env-lookup env-extend env-extend*
         truthy? type-of-val is-compatible?
         promote-to-float numeric?
         map-get array-get list-get val-length
         val-eq? val-lt?)
