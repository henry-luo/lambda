#lang racket
;; ============================================================================
;; Lambda Script — Type Inference & Judgment Rules (PLT Redex)
;;
;; Defines static type inference for the functional core.
;; These rules correspond to the compile-time type checking in build_ast.cpp
;; and the type inference used by transpile.cpp for C code generation.
;;
;; The type system is:
;;   - Strong, with inference (types inferred from initializers)
;;   - Structural (maps/elements use structural subtyping)
;;   - First-class (types are values)
;;   - Union types supported (T | U)
;;   - Nullable shorthand: T? = T | null
;;
;; Type hierarchy:
;;   int <: int64 <: float <: number <: any
;;   date <: datetime, time <: datetime
;;   Every type <: any
;; ============================================================================

(require redex)
(require "lambda-core.rkt")


;; ────────────────────────────────────────────────────────────────────────────
;; 1. TYPE ENVIRONMENT
;; ────────────────────────────────────────────────────────────────────────────
;; Γ maps variable names to types

(define-language LambdaTypes
  (Γ ::= ((x τ) ...)))


;; ────────────────────────────────────────────────────────────────────────────
;; 2. SUBTYPE RELATION
;; ────────────────────────────────────────────────────────────────────────────
;; Matches the behavior of fn_is() in lambda-eval.cpp and type checking
;; in build_ast.cpp.
;;
;; Key rules:
;;   int <: int64 <: float <: number <: any
;;   T <: T (reflexive)
;;   T <: any (everything is a subtype of any)
;;   T <: T | U and U <: T | U (union intro)
;;   T <: T? (nullable intro, since T? = T | null)
;;   null <: T? (null inhabits every nullable type)

(define (subtype? τ1 τ2)
  (match* (τ1 τ2)
    ;; reflexive
    [(t t) #t]

    ;; everything <: any
    [(_ 'any-type) #t]

    ;; numeric hierarchy: int <: int64 <: float <: number
    [('int-type 'int64-type)   #t]
    [('int-type 'float-type)   #t]
    [('int-type 'number-type)  #t]
    [('int64-type 'float-type) #t]
    [('int64-type 'number-type) #t]
    [('float-type 'number-type) #t]

    ;; null <: T?
    [('null-type `(nullable ,_)) #t]
    ;; T <: T?
    [(t `(nullable ,t2)) (subtype? t t2)]

    ;; T <: T | U  and  U <: T | U
    [(t `(union ,t1 ,t2))
     (or (subtype? t t1) (subtype? t t2))]

    ;; union <: T  iff both branches <: T
    [(`(union ,t1 ,t2) t)
     (and (subtype? t1 t) (subtype? t2 t))]

    ;; array subtyping (covariant)
    [(`(array-of ,t1) `(array-of ,t2))
     (subtype? t1 t2)]
    [('array-type `(array-of ,_)) #t]  ; generic array <: array-of T? not quite right
    [(`(array-of ,_) 'array-type) #t]  ; array-of T <: array (yes)

    ;; function subtyping (contravariant params, covariant return)
    [(`(fn-type (,p1s ...) ,r1) `(fn-type (,p2s ...) ,r2))
     (and (= (length p1s) (length p2s))
          (andmap subtype? p2s p1s)    ; contravariant
          (subtype? r1 r2))]           ; covariant

    ;; error return types
    [(`(error-ret ,t1) `(error-ret ,t2))
     (subtype? t1 t2)]

    ;; default: not a subtype
    [(_ _) #f]))


;; ────────────────────────────────────────────────────────────────────────────
;; 3. TYPE JOIN (LEAST UPPER BOUND)
;; ────────────────────────────────────────────────────────────────────────────
;; Used for if-else branches, match arms, array element types.
;; Verified against build_ast.cpp:
;;   If types match → that type
;;   If both numeric → promote (int + float → float)
;;   Otherwise → any
;;
;; Note: The implementation currently does type_id comparison with max().
;; We model the intended semantics more precisely.

(define (type-join τ1 τ2)
  (cond
    ;; same type
    [(equal? τ1 τ2) τ1]

    ;; numeric promotion
    [(and (numeric-type? τ1) (numeric-type? τ2))
     (numeric-lub τ1 τ2)]

    ;; one is null → nullable
    [(equal? τ1 'null-type) `(nullable ,τ2)]
    [(equal? τ2 'null-type) `(nullable ,τ1)]

    ;; one is any → any
    [(equal? τ1 'any-type) 'any-type]
    [(equal? τ2 'any-type) 'any-type]

    ;; one is error → union
    [(equal? τ1 'error-type) `(union ,τ2 ,τ1)]
    [(equal? τ2 'error-type) `(union ,τ1 ,τ2)]

    ;; subtype relationship
    [(subtype? τ1 τ2) τ2]
    [(subtype? τ2 τ1) τ1]

    ;; incompatible types → any
    ;; (implementation uses LMD_TYPE_ANY in this case)
    [else 'any-type]))

(define (numeric-type? τ)
  (member τ '(int-type int64-type float-type number-type)))

(define (numeric-lub τ1 τ2)
  (define order '(int-type int64-type float-type number-type))
  (define i1 (index-of order τ1))
  (define i2 (index-of order τ2))
  (list-ref order (max (or i1 0) (or i2 0))))


;; ────────────────────────────────────────────────────────────────────────────
;; 4. TYPE INFERENCE
;; ────────────────────────────────────────────────────────────────────────────
;; infer-type : Γ × e → τ
;; Infers the type of expression e in type environment Γ.
;; Corresponds to the type inference done in build_ast.cpp's node
;; type assignment.

(define (infer-type Γ e)
  (match e

    ;; ── Literals ──
    ['null                   'null-type]
    [(? boolean?)            'bool-type]
    [(? exact-integer?)      'int-type]
    [(? real?)               'float-type]
    [(? string?)             'string-type]
    [`(sym ,_)               'symbol-type]
    [`(array-val)            'array-type]
    [`(array-val ,v ,vs ...)
     (define elem-types (map (λ (vi) (infer-type Γ vi)) (cons v vs)))
     (define joined (foldl type-join (car elem-types) (cdr elem-types)))
     `(array-of ,joined)]
    [`(list-val ,vs ...)
     (define elem-types (map (λ (vi) (infer-type Γ vi)) vs))
     `(list-of ,@elem-types)]
    [`(map-val (,ks ,vs) ...)
     (define field-types (map (λ (k v) `(,k ,(infer-type Γ v))) ks vs))
     `(map-of ,@field-types)]
    [`(range-val ,s ,e)      'range-type]
    [`(closure ,_ ,params ,body)  'func-type]  ; simplified
    [`(error-val ,_ ,_)      'error-type]
    [`(type-val ,τ)          'type-type]

    ;; ── Variables ──
    [(? symbol? x)
     (cond
       [(assq x Γ) => (λ (p) (cdr p))]
       [else 'any-type])]  ; unknown variable → any

    ;; ── Let ──
    [`(let ((,xs ,es) ...) ,body)
     (define Γ* (let-type-env Γ xs es))
     (infer-type Γ* body)]

    [`(let-seq ((,xs ,es) ...) ,body)
     (define Γ* (let-type-env Γ xs es))
     (infer-type Γ* body)]

    [`(let-typed ((,xs ,τs ,es) ...) ,body)
     (define Γ* (foldl (λ (x τ Γ-acc) (cons (cons x τ) Γ-acc)) Γ xs τs))
     (infer-type Γ* body)]

    ;; ── If-else ──
    ;; Type is join of both branches
    [`(if ,e-cond ,e-then ,e-else)
     (type-join (infer-type Γ e-then) (infer-type Γ e-else))]

    [`(if-stam ,e-cond ,e-then)
     `(nullable ,(infer-type Γ e-then))]

    ;; ── Lambda ──
    [`(lam (,params ...) ,body)
     (define param-types (map (λ (p) (param-type p)) params))
     (define Γ* (foldl (λ (p pt Γ-acc)
                         (cons (cons (param-name p) pt) Γ-acc))
                       Γ params param-types))
     (define ret-type (infer-type Γ* body))
     `(fn-type ,param-types ,ret-type)]

    ;; ── Application ──
    [`(app ,e-fn ,e-args ...)
     (define fn-τ (infer-type Γ e-fn))
     (match fn-τ
       [`(fn-type ,_ ,ret) ret]
       [`(error-ret ,ret) ret]  ; error-returning fn
       [_ 'any-type])]         ; unknown fn type → any

    ;; ── Arithmetic ──
    [`(add ,e1 ,e2)  (arith-result-type Γ e1 e2)]
    [`(sub ,e1 ,e2)  (arith-result-type Γ e1 e2)]
    [`(mul ,e1 ,e2)  (arith-result-type Γ e1 e2)]
    [`(pow ,e1 ,e2)  (arith-result-type Γ e1 e2)]
    [`(fdiv ,e1 ,e2) 'float-type]    ; / always returns float
    [`(idiv ,e1 ,e2) 'int-type]      ; div returns int
    [`(mod ,e1 ,e2)  (arith-result-type Γ e1 e2)]

    ;; ── Comparison ──
    [`(eq ,_ ,_)     'bool-type]
    [`(neq ,_ ,_)    'bool-type]
    [`(lt ,_ ,_)     'bool-type]
    [`(le ,_ ,_)     'bool-type]
    [`(gt ,_ ,_)     'bool-type]
    [`(ge ,_ ,_)     'bool-type]

    ;; ── Logical ──
    ;; and/or return one of their operands (short-circuit), not necessarily bool
    ;; In practice, the implementation often returns the operand value
    ;; For type inference purposes: we use the join of both branches
    [`(l-and ,e1 ,e2) (type-join (infer-type Γ e1) (infer-type Γ e2))]
    [`(l-or ,e1 ,e2)  (type-join (infer-type Γ e1) (infer-type Γ e2))]
    [`(l-not ,_)       'bool-type]

    ;; ── Negation ──
    [`(neg ,e1) (infer-type Γ e1)]

    ;; ── Concat (++) ──
    [`(concat ,e1 ,e2)
     (define t1 (infer-type Γ e1))
     (define t2 (infer-type Γ e2))
     (cond
       [(or (equal? t1 'string-type) (equal? t2 'string-type)) 'string-type]
       [(and (equal? t1 'array-type) (equal? t2 'array-type)) 'array-type]
       ;; array-of types
       [(and (match t1 [`(array-of ,_) #t] [_ #f])
             (match t2 [`(array-of ,_) #t] [_ #f]))
        (match* (t1 t2)
          [(`(array-of ,et1) `(array-of ,et2))
           `(array-of ,(type-join et1 et2))])]
       [(or (equal? t1 'symbol-type) (equal? t2 'symbol-type)) 'symbol-type]
       [else 'any-type])]

    ;; ── Range ──
    [`(to-range ,e1 ,e2) 'range-type]

    ;; ── Type operations ──
    [`(is-type ,_ ,_)     'bool-type]
    [`(is-not-type ,_ ,_) 'bool-type]
    [`(in-coll ,_ ,_)     'bool-type]

    ;; ── Collection construction ──
    [`(array ,es ...)
     (if (null? es)
         'array-type
         (let* ([elem-types (map (λ (e) (infer-type Γ e)) es)]
                [joined (foldl type-join (car elem-types) (cdr elem-types))])
           `(array-of ,joined)))]

    [`(list-expr ,es ...)
     `(list-of ,@(map (λ (e) (infer-type Γ e)) es))]

    [`(map-expr (,ks ,es) ...)
     `(map-of ,@(map (λ (k e) `(,k ,(infer-type Γ e))) ks es))]

    ;; ── Member access ──
    [`(member ,e-obj ,field)
     (define obj-τ (infer-type Γ e-obj))
     (match obj-τ
       [`(map-of ,fields ...)
        (define fld (assq field fields))
        (if fld (cadr fld) 'any-type)]
       ['string-type
        (cond [(eq? field 'length) 'int-type]
              [else 'any-type])]
       ['array-type
        (cond [(eq? field 'length) 'int-type]
              [else 'any-type])]
       [_ 'any-type])]   ; can't determine statically

    ;; ── Index access ──
    [`(index ,e-obj ,e-idx)
     (define obj-τ (infer-type Γ e-obj))
     (match obj-τ
       [`(array-of ,elem-τ) elem-τ]
       ['array-type 'any-type]
       [`(list-of ,elem-τs ...)
        ;; Could narrow if index is a literal, but generally any
        'any-type]
       ['string-type 'string-type]
       [_ 'any-type])]

    ;; ── For expression ── produces array
    [`(for ,x ,e-coll ,e-body)
     (define coll-τ (infer-type Γ e-coll))
     (define elem-τ (collection-elem-type coll-τ))
     (define Γ* (cons (cons x elem-τ) Γ))
     `(array-of ,(infer-type Γ* e-body))]

    [`(for-where ,x ,e-coll ,_ ,e-body)
     (define coll-τ (infer-type Γ e-coll))
     (define elem-τ (collection-elem-type coll-τ))
     (define Γ* (cons (cons x elem-τ) Γ))
     `(array-of ,(infer-type Γ* e-body))]

    ;; ── Pipe (mapping) ── produces same collection shape as input
    [`(pipe ,e-coll ,e-transform)
     (define coll-τ (infer-type Γ e-coll))
     (define elem-τ (collection-elem-type coll-τ))
     (define Γ* (cons (cons '_pipe_item elem-τ) Γ))
     (define result-τ (infer-type Γ* e-transform))
     ;; Pipe always produces array for collections
     `(array-of ,result-τ)]

    [`(pipe-agg ,e-coll ,e-fn) 'any-type]  ; hard to infer statically

    ;; ── Where (filter) ── preserves input type
    [`(where ,e-coll ,_) (infer-type Γ e-coll)]

    ;; ── Match ── join of all arm types
    [`(match ,e-scrutinee ,clauses ...)
     (define arm-types
       (map (λ (c)
              (match c
                [`(case-type ,_ ,body) (infer-type Γ body)]
                [`(case-val ,_ ,body) (infer-type Γ body)]
                [`(case-range ,_ ,_ ,body) (infer-type Γ body)]
                [`(case-union ,_ ,_ ,body) (infer-type Γ body)]
                [`(default-case ,body) (infer-type Γ body)]
                [_ 'any-type]))
            clauses))
     (if (null? arm-types)
         'any-type
         (foldl type-join (car arm-types) (cdr arm-types)))]

    ;; ── Error handling ──
    [`(make-error ,_) 'error-type]
    [`(make-error-2 ,_ ,_) 'error-type]
    [`(raise-expr ,_) 'error-type]
    [`(try-prop ,e1) (infer-type Γ e1)]  ; strips error from return type
    [`(is-error ,_) 'bool-type]
    [`(let-err ,x-val ,x-err ,e-init ,e-body)
     ;; x-val gets the success type, x-err gets error-type
     (define init-τ (infer-type Γ e-init))
     (define Γ* (cons (cons x-err 'error-type)
                      (cons (cons x-val init-τ) Γ)))
     (infer-type Γ* e-body)]

    ;; ── Builtins ──
    [`(to-int ,_)     'int-type]
    [`(to-float ,_)   'float-type]
    [`(to-string ,_)  'string-type]
    [`(to-bool ,_)    'bool-type]
    [`(to-symbol ,_)  'symbol-type]
    [`(len-expr ,_)   'int-type]
    [`(sum-expr ,e1)  (infer-type Γ e1)]  ; sum preserves numeric type
    [`(sort-expr ,e1) (infer-type Γ e1)]  ; sort preserves collection type
    [`(reverse-expr ,e1) (infer-type Γ e1)]
    [`(unique-expr ,e1)  (infer-type Γ e1)]
    [`(take-expr ,e1 ,_) (infer-type Γ e1)]
    [`(drop-expr ,e1 ,_) (infer-type Γ e1)]

    ;; ── Spread ──
    [`(spread ,e1) (infer-type Γ e1)]  ; spread doesn't change type conceptually

    ;; ── Fallback ──
    [_ 'any-type]))


;; ────────────────────────────────────────────────────────────────────────────
;; HELPER FUNCTIONS
;; ────────────────────────────────────────────────────────────────────────────

;; Arithmetic result type:
;;   int op int → int
;;   int op float, float op int → float
;;   int64 involved → int64 (unless float also)
;;   any numeric + float → float
(define (arith-result-type Γ e1 e2)
  (define t1 (infer-type Γ e1))
  (define t2 (infer-type Γ e2))
  (cond
    [(and (numeric-type? t1) (numeric-type? t2))
     (numeric-lub t1 t2)]
    ;; array operations (broadcasting)
    [(or (and (array-type-like? t1) (numeric-type? t2))
         (and (numeric-type? t1) (array-type-like? t2)))
     (if (array-type-like? t1) t1 t2)]
    [(and (array-type-like? t1) (array-type-like? t2))
     t1]  ; element-wise, same shape
    [else 'any-type]))

(define (array-type-like? τ)
  (or (equal? τ 'array-type)
      (match τ [`(array-of ,_) #t] [_ #f])))

;; Extract element type from collection type
(define (collection-elem-type τ)
  (match τ
    [`(array-of ,elem) elem]
    ['array-type 'any-type]
    ['range-type 'int-type]
    [`(list-of ,elems ...) (if (null? elems) 'any-type (car elems))]
    ['list-type 'any-type]
    ['map-type 'any-type]  ; iterating map → values
    [_ 'any-type]))

;; Extract parameter name
(define (param-name p)
  (match p
    [(? symbol? x) x]
    [`(typed ,x ,_) x]
    [`(opt ,x) x]
    [`(default ,x ,_) x]
    [`(typed-default ,x ,_ ,_) x]
    [_ (gensym 'p)]))

;; Extract parameter type
(define (param-type p)
  (match p
    [(? symbol?) 'any-type]
    [`(typed ,_ ,τ) τ]
    [`(opt ,_) 'any-type]            ; nullable in practice
    [`(default ,_ ,e) 'any-type]      ; could infer from default
    [`(typed-default ,_ ,τ ,_) τ]
    [_ 'any-type]))

;; Build type environment from let bindings (sequential)
(define (let-type-env Γ xs es)
  (cond
    [(null? xs) Γ]
    [else
     (define τ (infer-type Γ (car es)))
     (let-type-env (cons (cons (car xs) τ) Γ) (cdr xs) (cdr es))]))


;; ────────────────────────────────────────────────────────────────────────────
;; TYPE WELL-FORMEDNESS CHECK
;; ────────────────────────────────────────────────────────────────────────────
;; Checks that an expression is type-consistent (no type errors).
;; Returns #t if well-typed, or a list of type errors.

(define (check-types Γ e)
  (match e
    ;; Let: check init expressions, then body
    [`(let ((,xs ,es) ...) ,body)
     (define init-errors (append-map (λ (e) (check-types Γ e)) es))
     (define Γ* (let-type-env Γ xs es))
     (append init-errors (check-types Γ* body))]

    ;; If-else: condition should be usable as bool (always OK in Lambda due to truthiness)
    ;; Check that both branches are well-typed
    [`(if ,e-cond ,e-then ,e-else)
     (append (check-types Γ e-cond)
             (check-types Γ e-then)
             (check-types Γ e-else))]

    ;; Application: check function and args
    [`(app ,e-fn ,e-args ...)
     (append (check-types Γ e-fn)
             (append-map (λ (a) (check-types Γ a)) e-args))]

    ;; Binary operations: check operands
    [`(,(and op (or 'add 'sub 'mul 'fdiv 'idiv 'mod 'pow)) ,e1 ,e2)
     (define t1 (infer-type Γ e1))
     (define t2 (infer-type Γ e2))
     (define errors (append (check-types Γ e1) (check-types Γ e2)))
     ;; Check that operands are numeric (or array for broadcasting)
     (cond
       [(and (numeric-type? t1) (numeric-type? t2)) errors]
       [(and (array-type-like? t1) (or (numeric-type? t2) (array-type-like? t2))) errors]
       [(and (numeric-type? t1) (array-type-like? t2)) errors]
       [(or (equal? t1 'any-type) (equal? t2 'any-type)) errors]  ; can't check
       [else (cons (format "type error: ~a applied to ~a and ~a" op t1 t2) errors)])]

    ;; Default: no errors
    [_ '()]))


;; ────────────────────────────────────────────────────────────────────────────
;; EXPORTS
;; ────────────────────────────────────────────────────────────────────────────

(provide subtype? type-join infer-type check-types
         numeric-type? numeric-lub
         collection-elem-type param-name param-type)
