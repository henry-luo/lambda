#lang racket
;; ============================================================================
;; Lambda Script — Big-Step Evaluation Semantics (PLT Redex)
;;
;; Defines eval : ρ × e → v  (environment × expression → value)
;; This is the reference evaluator for the functional core of Lambda.
;;
;; Conventions:
;;   ρ  = environment (association list)
;;   e  = expression
;;   v  = value (fully evaluated)
;;   ~  = pipe current item (bound in ρ as _pipe_item)
;;   ~# = pipe current index (bound in ρ as _pipe_index)
;;
;; Error propagation:
;;   Most operators short-circuit on error inputs (GUARD_ERROR pattern).
;;   An error value propagated through is preserved unchanged.
;; ============================================================================

(require redex)
(require "lambda-core.rkt")


;; ────────────────────────────────────────────────────────────────────────────
;; BIG-STEP EVALUATOR
;; ────────────────────────────────────────────────────────────────────────────
;; We use a Racket-level evaluator (rather than judgment-form) for
;; flexibility with side-conditions and error propagation.

;; eval-lambda : ρ × e → v
;; Main evaluation function. Reduces expression e in environment ρ to value v.

(define (eval-lambda ρ e)
  (match e

    ;; ── Values (already evaluated) ──
    [`null                     'null]
    [(? boolean? b)            b]
    [(? exact-integer? n)      n]
    [(? real? r)               (exact->inexact r)]
    [(? string? s)             s]
    [`(sym ,s)                 `(sym ,s)]
    [`(array-val ,vs ...)      `(array-val ,@vs)]
    [`(list-val ,vs ...)       `(list-val ,@vs)]
    [`(map-val ,pairs ...)     `(map-val ,@pairs)]
    [`(range-val ,a ,b)        `(range-val ,a ,b)]
    [`(closure ,env ,params ,body)  `(closure ,env ,params ,body)]
    [`(error-val ,msg ,code)   `(error-val ,msg ,code)]
    [`(type-val ,τ)            `(type-val ,τ)]

    ;; ── Variables ──
    [(? symbol? x)
     (cond
       [(eq? x '~)  (env-ref ρ '_pipe_item)]
       [(eq? x '~#) (env-ref ρ '_pipe_index)]
       [else        (env-ref ρ x)])]

    ;; ── Let bindings (sequential: later bindings see earlier ones) ──
    [`(let ((,xs ,es) ...) ,body)
     (define ρ* (let-bind-seq ρ xs es))
     (eval-lambda ρ* body)]

    [`(let-seq ((,xs ,es) ...) ,body)
     (define ρ* (let-bind-seq ρ xs es))
     (eval-lambda ρ* body)]

    [`(let-typed ((,xs ,τs ,es) ...) ,body)
     (define ρ* (let-bind-seq ρ xs es))
     (eval-lambda ρ* body)]

    ;; ── If-else ──
    ;; Spec: requires else branch in fn context. Returns value of taken branch.
    [`(if ,e-cond ,e-then ,e-else)
     (define cond-v (eval-lambda ρ e-cond))
     (cond
       [(is-error? cond-v) cond-v]          ; error propagation
       [(truthy-val? cond-v) (eval-lambda ρ e-then)]
       [else                 (eval-lambda ρ e-else)])]

    ;; ── If without else (statement form, returns null if false) ──
    [`(if-stam ,e-cond ,e-then)
     (define cond-v (eval-lambda ρ e-cond))
     (cond
       [(is-error? cond-v) cond-v]
       [(truthy-val? cond-v) (eval-lambda ρ e-then)]
       [else 'null])]

    ;; ── Lambda / anonymous function ──
    [`(lam (,params ...) ,body)
     `(closure ,ρ ,params ,body)]

    ;; ── Function application ──
    [`(app ,e-fn ,e-args ...)
     (define fn-v (eval-lambda ρ e-fn))
     (cond
       [(is-error? fn-v) fn-v]
       [(not (closure? fn-v))
        `(error-val "not a function" 300)]
       [else
        (define arg-vals (map (λ (a) (eval-lambda ρ a)) e-args))
        ;; short-circuit if any arg is error
        (define first-err (findf is-error? arg-vals))
        (if first-err
            first-err
            (apply-closure fn-v arg-vals))])]

    ;; ── Arithmetic ──
    [`(add ,e1 ,e2)  (eval-binary ρ e1 e2 arith-add)]
    [`(sub ,e1 ,e2)  (eval-binary ρ e1 e2 arith-sub)]
    [`(mul ,e1 ,e2)  (eval-binary ρ e1 e2 arith-mul)]
    [`(fdiv ,e1 ,e2) (eval-binary ρ e1 e2 arith-fdiv)]
    [`(idiv ,e1 ,e2) (eval-binary ρ e1 e2 arith-idiv)]
    [`(mod ,e1 ,e2)  (eval-binary ρ e1 e2 arith-mod)]
    [`(pow ,e1 ,e2)  (eval-binary ρ e1 e2 arith-pow)]

    ;; ── Comparison ──
    [`(eq ,e1 ,e2)   (eval-binary ρ e1 e2 cmp-eq)]
    [`(neq ,e1 ,e2)  (eval-binary ρ e1 e2 cmp-neq)]
    [`(lt ,e1 ,e2)   (eval-binary ρ e1 e2 cmp-lt)]
    [`(le ,e1 ,e2)   (eval-binary ρ e1 e2 cmp-le)]
    [`(gt ,e1 ,e2)   (eval-binary ρ e1 e2 cmp-gt)]
    [`(ge ,e1 ,e2)   (eval-binary ρ e1 e2 cmp-ge)]

    ;; ── Logical (short-circuit) ──
    ;; and: if a is falsy → a; else → b
    ;; or: if a is truthy → a; else → b
    [`(l-and ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (if (truthy-val? v1)
         (eval-lambda ρ e2)
         v1)]

    [`(l-or ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (if (truthy-val? v1)
         v1
         (eval-lambda ρ e2))]

    [`(l-not ,e1)
     (define v1 (eval-lambda ρ e1))
     (if (truthy-val? v1) #f #t)]

    ;; ── Negation ──
    [`(neg ,e1)
     (define v1 (eval-lambda ρ e1))
     (cond
       [(is-error? v1) v1]
       [(exact-integer? v1) (- v1)]
       [(real? v1) (- v1)]
       [else `(error-val "negation of non-number" 300)])]

    ;; ── String / collection concatenation (++) ──
    [`(concat ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (cond
       [(is-error? v1) v1]
       [(is-error? v2) v2]
       ;; string ++ string
       [(and (string? v1) (string? v2))
        (string-append v1 v2)]
       ;; string ++ non-string: coerce RHS to string
       [(string? v1)
        (string-append v1 (value->string v2))]
       ;; non-string ++ string: coerce LHS to string
       [(string? v2)
        (string-append (value->string v1) v2)]
       ;; array ++ array
       [(and (array-val? v1) (array-val? v2))
        `(array-val ,@(array-items v1) ,@(array-items v2))]
       ;; list ++ list
       [(and (list-val? v1) (list-val? v2))
        `(list-val ,@(list-items v1) ,@(list-items v2))]
       [else `(error-val "invalid concat operands" 300)])]

    ;; ── Range construction (to) ──
    [`(to-range ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (cond
       [(is-error? v1) v1]
       [(is-error? v2) v2]
       [else `(range-val ,v1 ,v2)])]

    ;; ── Type operations ──
    [`(is-type ,expr ,τ)
     (define v (eval-lambda ρ expr))
     (type-check-is v τ)]

    [`(is-not-type ,expr ,τ)
     (define v (eval-lambda ρ expr))
     (not (truthy-val? (type-check-is v τ)))]

    [`(in-coll ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (cond
       [(is-error? v1) v1]
       [(is-error? v2) v2]
       [(range-val? v2)
        (define start (range-start v2))
        (define end (range-end v2))
        (and (numeric-val? v1)
             (>= (to-number v1) (to-number start))
             (<= (to-number v1) (to-number end)))]
       [(array-val? v2)
        (ormap (λ (item) (val-eq-racket? v1 item)) (array-items v2))]
       [else #f])]

    ;; ── Collection construction ──
    [`(array ,es ...)
     (define vals (eval-spread-list ρ es))
     `(array-val ,@vals)]

    [`(list-expr ,es ...)
     (define vals (map (λ (e) (eval-lambda ρ e)) es))
     `(list-val ,@vals)]

    [`(map-expr (,ks ,es) ...)
     (define pairs
       (map (λ (k e) `(,k ,(eval-lambda ρ e))) ks es))
     `(map-val ,@pairs)]

    ;; ── Collection access ──
    [`(member ,e-obj ,field)
     (define obj (eval-lambda ρ e-obj))
     (cond
       [(is-error? obj) obj]
       [(eq? obj 'null) 'null]    ; null-safe: null.field → null
       [(map-val? obj) (map-ref obj field)]
       [(and (array-val? obj) (eq? field 'length))
        (length (array-items obj))]
       [(and (list-val? obj) (eq? field 'length))
        (length (list-items obj))]
       [(and (string? obj) (eq? field 'length))
        (string-length obj)]
       [else 'null])]

    [`(index ,e-obj ,e-idx)
     (define obj (eval-lambda ρ e-obj))
     (define idx (eval-lambda ρ e-idx))
     (cond
       [(is-error? obj) obj]
       [(is-error? idx) idx]
       [(eq? obj 'null) 'null]   ; null-safe
       ;; integer index
       [(and (array-val? obj) (exact-integer? idx))
        (array-ref-safe obj idx)]
       [(and (list-val? obj) (exact-integer? idx))
        (list-ref-safe obj idx)]
       ;; string/symbol index on map
       [(and (map-val? obj) (or (string? idx) (symbol? idx)))
        (map-ref obj (if (string? idx) (string->symbol idx) idx))]
       ;; string index
       [(and (string? obj) (exact-integer? idx))
        (if (and (>= idx 0) (< idx (string-length obj)))
            (string (string-ref obj idx))
            'null)]
       [else 'null])]

    ;; ── Spread (produces a "spread marker" for array construction) ──
    [`(spread ,e1)
     (define v (eval-lambda ρ e1))
     `(spread-val ,v)]

    ;; ── For expression ──
    [`(for ,x ,e-coll ,e-body)
     (eval-for ρ x #f e-coll #f e-body)]

    [`(for-where ,x ,e-coll ,e-pred ,e-body)
     (eval-for ρ x #f e-coll e-pred e-body)]

    [`(for-idx ,x-idx ,x-item ,e-coll ,e-body)
     (eval-for ρ x-item x-idx e-coll #f e-body)]

    ;; ── For-at (map iteration) ──
    [`(for-at ,x-key ,e-map ,e-body)
     (eval-for-at ρ x-key #f e-map e-body)]

    [`(for-at-kv ,x-key ,x-val ,e-map ,e-body)
     (eval-for-at ρ x-key x-val e-map e-body)]

    ;; ── Pipe (mapping pipe with ~ and ~#) ──
    [`(pipe ,e-coll ,e-transform)
     (eval-pipe ρ e-coll e-transform)]

    ;; ── Pipe aggregate (no ~, pass entire collection) ──
    [`(pipe-agg ,e-coll ,e-fn)
     (define coll (eval-lambda ρ e-coll))
     (define fn-val (eval-lambda ρ e-fn))
     (cond
       [(is-error? coll) coll]
       [(is-error? fn-val) fn-val]
       [(closure? fn-val) (apply-closure fn-val (list coll))]
       [else `(error-val "pipe aggregate target not a function" 300)])]

    ;; ── Where (filter) ──
    [`(where ,e-coll ,e-pred)
     (eval-where ρ e-coll e-pred)]

    ;; ── Match ──
    [`(match ,e-scrutinee ,clauses ...)
     (define scrut (eval-lambda ρ e-scrutinee))
     (eval-match ρ scrut clauses)]

    ;; ── Error handling ──
    [`(make-error ,e-msg)
     (define msg (eval-lambda ρ e-msg))
     (if (string? msg)
         `(error-val ,msg 318)    ; 318 = user_error
         `(error-val "error" 318))]

    [`(make-error-2 ,e-msg ,e-src)
     (define msg (eval-lambda ρ e-msg))
     (define src (eval-lambda ρ e-src))
     `(error-val ,(if (string? msg) msg "error") 318)]

    [`(raise-expr ,e1)
     (define v (eval-lambda ρ e1))
     (if (is-error? v)
         v
         `(error-val ,(if (string? v) v (value->string v)) 318))]

    [`(try-prop ,e1)
     ;; ? operator: evaluate, if error → propagate (return as-is)
     ;; In a real setting this would cause enclosing fn to return.
     ;; We model it as: if error, the result is the error value.
     ;; The enclosing function must check for error returns.
     (eval-lambda ρ e1)]

    [`(let-err ,x-val ,x-err ,e-init ,e-body)
     (define init-v (eval-lambda ρ e-init))
     (define ρ*
       (if (is-error? init-v)
           (env-set (env-set ρ x-val 'null) x-err init-v)
           (env-set (env-set ρ x-val init-v) x-err 'null)))
     (eval-lambda ρ* e-body)]

    [`(is-error ,e1)
     (define v (eval-lambda ρ e1))
     (is-error? v)]

    ;; ── Type conversion builtins ──
    [`(to-int ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(exact-integer? v) v]
       [(real? v) (exact-truncate v)]
       [(string? v)
        (define n (string->number v))
        (if n (exact-truncate n) 'null)]
       [(eq? v #t) 1]
       [(eq? v #f) 0]
       [else 'null])]

    [`(to-float ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(real? v) (exact->inexact v)]
       [(string? v)
        (define n (string->number v))
        (if n (exact->inexact n) 'null)]
       [else 'null])]

    [`(to-string ,e1)
     (define v (eval-lambda ρ e1))
     (value->string v)]

    [`(to-bool ,e1)
     (define v (eval-lambda ρ e1))
     (truthy-val? v)]

    ;; ── Collection builtins ──
    [`(len-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(array-val? v) (length (array-items v))]
       [(list-val? v)  (length (list-items v))]
       [(map-val? v)   (length (map-pairs v))]
       [(string? v)    (string-length v)]
       [(range-val? v)
        (define s (range-start v))
        (define e (range-end v))
        (if (and (exact-integer? s) (exact-integer? e))
            (max 0 (+ 1 (- e s)))
            0)]
       [(eq? v 'null) 0]    ; null.len() → 0
       [else 0])]

    [`(sum-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(array-val? v)
        (foldl (λ (item acc) (arith-add acc item)) 0 (array-items v))]
       [else 0])]

    [`(reverse-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(array-val? v)  `(array-val ,@(reverse (array-items v)))]
       [(list-val? v)   `(list-val ,@(reverse (list-items v)))]
       [(string? v)     (list->string (reverse (string->list v)))]
       [(eq? v 'null) 'null]
       [else v])]

    [`(sort-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(array-val? v)
        `(array-val ,@(sort (array-items v) val-less-than?))]
       [else v])]

    [`(unique-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(array-val? v)
        `(array-val ,@(remove-duplicates (array-items v) val-eq-racket?))]
       [else v])]

    [`(take-expr ,e1 ,e2)
     (define coll (eval-lambda ρ e1))
     (define n (eval-lambda ρ e2))
     (cond
       [(is-error? coll) coll]
       [(is-error? n) n]
       [(and (array-val? coll) (exact-integer? n))
        `(array-val ,@(take (array-items coll) (min n (length (array-items coll)))))]
       [else coll])]

    [`(drop-expr ,e1 ,e2)
     (define coll (eval-lambda ρ e1))
     (define n (eval-lambda ρ e2))
     (cond
       [(is-error? coll) coll]
       [(is-error? n) n]
       [(and (array-val? coll) (exact-integer? n))
        `(array-val ,@(drop (array-items coll) (min n (length (array-items coll)))))]
       [else coll])]

    ;; ── Fallback ──
    [_ `(error-val ,(format "unrecognized expression: ~a" e) 500)]))


;; ────────────────────────────────────────────────────────────────────────────
;; HELPER FUNCTIONS
;; ────────────────────────────────────────────────────────────────────────────

;; ── Value predicates ──

(define (is-error? v)
  (match v
    [`(error-val ,_ ,_) #t]
    [_ #f]))

(define (closure? v)
  (match v
    [`(closure ,_ ,_ ,_) #t]
    [_ #f]))

(define (array-val? v)
  (match v [`(array-val ,_ ...) #t] [_ #f]))

(define (list-val? v)
  (match v [`(list-val ,_ ...) #t] [_ #f]))

(define (map-val? v)
  (match v [`(map-val ,_ ...) #t] [_ #f]))

(define (range-val? v)
  (match v [`(range-val ,_ ,_) #t] [_ #f]))

(define (numeric-val? v)
  (or (exact-integer? v) (real? v)))


;; ── Value accessors ──

(define (array-items v)
  (match v [`(array-val ,items ...) items] [_ '()]))

(define (list-items v)
  (match v [`(list-val ,items ...) items] [_ '()]))

(define (map-pairs v)
  (match v [`(map-val ,pairs ...) pairs] [_ '()]))

(define (range-start v)
  (match v [`(range-val ,s ,_) s] [_ 0]))

(define (range-end v)
  (match v [`(range-val ,_ ,e) e] [_ 0]))


;; ── Environment operations (Racket-level) ──

(define (env-ref ρ x)
  (cond
    [(assq x ρ) => cdr]
    [else 'null]))

(define (env-set ρ x v)
  (cons (cons x v) ρ))


;; ── Sequential let binding ──

(define (let-bind-seq ρ xs es)
  (cond
    [(null? xs) ρ]
    [else
     (define v (eval-lambda ρ (car es)))
     (let-bind-seq (env-set ρ (car xs) v) (cdr xs) (cdr es))]))


;; ── Truthiness (Racket-level) ──
;; Spec: null, false, error, "" are falsy. Everything else (including 0) is truthy.

(define (truthy-val? v)
  (not (or (eq? v 'null)
           (eq? v #f)
           (is-error? v)
           (equal? v ""))))


;; ── Value to string conversion ──

(define (value->string v)
  (match v
    ['null       "null"]
    [#t          "true"]
    [#f          "false"]
    [(? exact-integer?) (number->string v)]
    [(? real?)   (let ([s (number->string v)])
                   ;; remove trailing .0 for whole numbers
                   (if (and (string-contains? s ".") 
                            (not (string-contains? s "e")))
                       (string-trim s "0" #:left? #f)
                       s))]
    [(? string?) v]
    [`(sym ,s)   s]
    [`(error-val ,msg ,code)  (format "error(~a)" msg)]
    [`(array-val ,items ...)
     (format "[~a]" (string-join (map value->string items) ", "))]
    [`(list-val ,items ...)
     (format "(~a)" (string-join (map value->string items) ", "))]
    [`(map-val ,pairs ...)
     (format "{~a}" (string-join
                     (map (λ (p) (format "~a: ~a" (car p) (value->string (cadr p))))
                          pairs) ", "))]
    [`(range-val ,s ,e)  (format "~a to ~a" (value->string s) (value->string e))]
    [`(closure ,_ ,_ ,_) "<function>"]
    [`(type-val ,τ)       (format "~a" τ)]
    [_ (format "~a" v)]))


;; ── Value equality (Racket-level boolean) ──

(define (val-eq-racket? v1 v2)
  (cond
    [(and (eq? v1 'null) (eq? v2 'null)) #t]
    [(and (boolean? v1) (boolean? v2)) (eq? v1 v2)]
    [(and (exact-integer? v1) (exact-integer? v2)) (= v1 v2)]
    [(and (numeric-val? v1) (numeric-val? v2))
     (= (to-number v1) (to-number v2))]
    [(and (string? v1) (string? v2)) (string=? v1 v2)]
    [(and (match v1 [`(sym ,_) #t] [_ #f])
          (match v2 [`(sym ,_) #t] [_ #f]))
     (equal? v1 v2)]
    [(and (array-val? v1) (array-val? v2))
     (and (= (length (array-items v1)) (length (array-items v2)))
          (andmap val-eq-racket? (array-items v1) (array-items v2)))]
    [else #f]))


;; ── Value less-than (for sort) ──

(define (val-less-than? a b)
  (cond
    [(and (numeric-val? a) (numeric-val? b))
     (< (to-number a) (to-number b))]
    [(and (string? a) (string? b))
     (string<? a b)]
    [else #f]))

(define (to-number v)
  (cond
    [(exact-integer? v) (exact->inexact v)]
    [(real? v) v]
    [else 0.0]))


;; ── Eval spread list (for array construction with *e) ──

(define (eval-spread-list ρ exprs)
  (apply append
    (map (λ (e)
           (define v (eval-lambda ρ e))
           (match v
             [`(spread-val (array-val ,items ...)) items]
             [`(spread-val (list-val ,items ...))  items]
             [`(spread-val (range-val ,s ,e))
              (if (and (exact-integer? s) (exact-integer? e))
                  (range s (+ e 1))
                  '())]
             [_ (list v)]))
         exprs)))


;; ── Map reference ──

(define (map-ref m key)
  (match m
    [`(map-val ,pairs ...)
     (define result (assq key pairs))
     (if result (cadr result) 'null)]
    [_ 'null]))

;; ── Array/list safe reference ──

(define (array-ref-safe arr idx)
  (define items (array-items arr))
  (define actual-idx
    (if (< idx 0) (+ (length items) idx) idx))
  (if (and (>= actual-idx 0) (< actual-idx (length items)))
      (list-ref items actual-idx)
      'null))

(define (list-ref-safe lst idx)
  (define items (list-items lst))
  (define actual-idx
    (if (< idx 0) (+ (length items) idx) idx))
  (if (and (>= actual-idx 0) (< actual-idx (length items)))
      (list-ref items actual-idx)
      'null))


;; ── Type checking (is operator) ──

(define (type-check-is v τ)
  (match τ
    ['any-type    #t]
    ['null-type   (eq? v 'null)]
    ['bool-type   (boolean? v)]
    ['int-type    (exact-integer? v)]
    ['float-type  (or (exact-integer? v) (and (real? v) (not (exact? v))))]
    ['number-type (numeric-val? v)]
    ['string-type (string? v)]
    ['symbol-type (match v [`(sym ,_) #t] [_ #f])]
    ['array-type  (array-val? v)]
    ['list-type   (list-val? v)]
    ['map-type    (map-val? v)]
    ['range-type  (range-val? v)]
    ['func-type   (closure? v)]
    ['error-type  (is-error? v)]
    ['type-type   (match v [`(type-val ,_) #t] [_ #f])]
    [`(nullable ,inner) (or (eq? v 'null) (type-check-is v inner))]
    [`(union ,t1 ,t2) (or (type-check-is v t1) (type-check-is v t2))]
    [_ #f]))


;; ────────────────────────────────────────────────────────────────────────────
;; ARITHMETIC OPERATIONS
;; ────────────────────────────────────────────────────────────────────────────
;; Verified against lambda-eval-num.cpp
;;
;; Promotion rules:
;;   int op int     → int (except / which → float)
;;   int op float   → float (promote int)
;;   float op float → float
;;
;; Vector arithmetic:
;;   scalar op array → element-wise broadcast
;;   array op array  → element-wise (length must match)

(define (eval-binary ρ e1 e2 op)
  (define v1 (eval-lambda ρ e1))
  (define v2 (eval-lambda ρ e2))
  ;; GUARD_ERROR pattern
  (cond
    [(is-error? v1) v1]
    [(is-error? v2) v2]
    [else (op v1 v2)]))

(define (arith-add a b)
  (cond
    ;; int + int → int
    [(and (exact-integer? a) (exact-integer? b)) (+ a b)]
    ;; numeric + numeric → float
    [(and (numeric-val? a) (numeric-val? b))
     (+ (to-number a) (to-number b))]
    ;; vector + vector (element-wise)
    [(and (array-val? a) (array-val? b))
     `(array-val ,@(map arith-add (array-items a) (array-items b)))]
    ;; scalar + array (broadcast)
    [(and (numeric-val? a) (array-val? b))
     `(array-val ,@(map (λ (x) (arith-add a x)) (array-items b)))]
    [(and (array-val? a) (numeric-val? b))
     `(array-val ,@(map (λ (x) (arith-add x b)) (array-items a)))]
    [else `(error-val "addition type error" 300)]))

(define (arith-sub a b)
  (cond
    [(and (exact-integer? a) (exact-integer? b)) (- a b)]
    [(and (numeric-val? a) (numeric-val? b))
     (- (to-number a) (to-number b))]
    [(and (array-val? a) (array-val? b))
     `(array-val ,@(map arith-sub (array-items a) (array-items b)))]
    [(and (numeric-val? a) (array-val? b))
     `(array-val ,@(map (λ (x) (arith-sub a x)) (array-items b)))]
    [(and (array-val? a) (numeric-val? b))
     `(array-val ,@(map (λ (x) (arith-sub x b)) (array-items a)))]
    [else `(error-val "subtraction type error" 300)]))

(define (arith-mul a b)
  (cond
    [(and (exact-integer? a) (exact-integer? b)) (* a b)]
    [(and (numeric-val? a) (numeric-val? b))
     (* (to-number a) (to-number b))]
    [(and (array-val? a) (array-val? b))
     `(array-val ,@(map arith-mul (array-items a) (array-items b)))]
    [(and (numeric-val? a) (array-val? b))
     `(array-val ,@(map (λ (x) (arith-mul a x)) (array-items b)))]
    [(and (array-val? a) (numeric-val? b))
     `(array-val ,@(map (λ (x) (arith-mul x b)) (array-items a)))]
    [else `(error-val "multiplication type error" 300)]))

;; / always returns float (per spec: "true division")
(define (arith-fdiv a b)
  (cond
    [(and (numeric-val? a) (numeric-val? b))
     (define denom (to-number b))
     (if (= denom 0)
         `(error-val "division by zero" 300)
         (/ (to-number a) denom))]
    [else `(error-val "division type error" 300)]))

;; div = integer/floor division
(define (arith-idiv a b)
  (cond
    [(and (exact-integer? a) (exact-integer? b))
     (if (= b 0)
         `(error-val "division by zero" 300)
         (quotient a b))]
    [(and (numeric-val? a) (numeric-val? b))
     (define denom (to-number b))
     (if (= denom 0)
         `(error-val "division by zero" 300)
         (exact-truncate (/ (to-number a) denom)))]
    [else `(error-val "integer division type error" 300)]))

(define (arith-mod a b)
  (cond
    [(and (exact-integer? a) (exact-integer? b))
     (if (= b 0)
         `(error-val "modulo by zero" 300)
         (modulo a b))]
    [(and (numeric-val? a) (numeric-val? b))
     (define denom (to-number b))
     (if (= denom 0.0)
         `(error-val "modulo by zero" 300)
         (let ([na (to-number a)])
           (- na (* (floor (/ na denom)) denom))))]
    [else `(error-val "modulo type error" 300)]))

(define (arith-pow a b)
  (cond
    [(and (exact-integer? a) (exact-integer? b) (>= b 0))
     (expt a b)]
    [(and (numeric-val? a) (numeric-val? b))
     (expt (to-number a) (to-number b))]
    [else `(error-val "exponentiation type error" 300)]))


;; ────────────────────────────────────────────────────────────────────────────
;; COMPARISON OPERATIONS
;; ────────────────────────────────────────────────────────────────────────────

(define (cmp-eq a b) (val-eq-racket? a b))
(define (cmp-neq a b) (not (val-eq-racket? a b)))

(define (cmp-lt a b)
  (cond
    [(and (numeric-val? a) (numeric-val? b))
     (< (to-number a) (to-number b))]
    [(and (string? a) (string? b))
     (string<? a b)]
    ;; null compared to non-null → error (per implementation: BOOL_ERROR)
    [(or (eq? a 'null) (eq? b 'null))
     `(error-val "comparison with null" 300)]
    [else `(error-val "comparison type mismatch" 300)]))

(define (cmp-le a b)
  (cond
    [(and (numeric-val? a) (numeric-val? b))
     (<= (to-number a) (to-number b))]
    [(and (string? a) (string? b))
     (string<=? a b)]
    [(or (eq? a 'null) (eq? b 'null))
     `(error-val "comparison with null" 300)]
    [else `(error-val "comparison type mismatch" 300)]))

(define (cmp-gt a b)
  (cond
    [(and (numeric-val? a) (numeric-val? b))
     (> (to-number a) (to-number b))]
    [(and (string? a) (string? b))
     (string>? a b)]
    [(or (eq? a 'null) (eq? b 'null))
     `(error-val "comparison with null" 300)]
    [else `(error-val "comparison type mismatch" 300)]))

(define (cmp-ge a b)
  (cond
    [(and (numeric-val? a) (numeric-val? b))
     (>= (to-number a) (to-number b))]
    [(and (string? a) (string? b))
     (string>=? a b)]
    [(or (eq? a 'null) (eq? b 'null))
     `(error-val "comparison with null" 300)]
    [else `(error-val "comparison type mismatch" 300)]))


;; ────────────────────────────────────────────────────────────────────────────
;; FUNCTION APPLICATION
;; ────────────────────────────────────────────────────────────────────────────
;; Verified against fn_call() in lambda-eval.cpp

(define (apply-closure fn-val args)
  (match fn-val
    [`(closure ,env ,params ,body)
     (define ρ* (bind-params env params args))
     (eval-lambda ρ* body)]
    [_ `(error-val "not a function" 300)]))

;; Bind parameters: required, typed, optional, default
;; Parameter order: required → optional → defaults
;; Missing required → compile error (we model as error)
;; Missing optional → null
;; Missing default → evaluate default expression
;; Extra args → discarded (with warning in impl)

(define (bind-params ρ params args)
  (cond
    [(null? params) ρ]  ; no more params
    [(null? args)
     ;; remaining params get default or null
     (match (car params)
       [(? symbol? x)          ;; required but missing → error in real impl
        (bind-params (env-set ρ x 'null) (cdr params) '())]
       [`(typed ,x ,τ)           ;; typed required but missing
        (bind-params (env-set ρ x 'null) (cdr params) '())]
       [`(opt ,x)              ;; optional → null
        (bind-params (env-set ρ x 'null) (cdr params) '())]
       [`(default ,x ,e)       ;; default → evaluate default expr in current env
        (define default-v (eval-lambda ρ e))
        (bind-params (env-set ρ x default-v) (cdr params) '())]
       [`(typed-default ,x ,τ ,e)
        (define default-v (eval-lambda ρ e))
        (bind-params (env-set ρ x default-v) (cdr params) '())]
       [_ ρ])]
    [else
     ;; bind first param to first arg
     (match (car params)
       [(? symbol? x)
        (bind-params (env-set ρ x (car args)) (cdr params) (cdr args))]
       [`(typed ,x ,τ)
        (bind-params (env-set ρ x (car args)) (cdr params) (cdr args))]
       [`(opt ,x)
        (bind-params (env-set ρ x (car args)) (cdr params) (cdr args))]
       [`(default ,x ,e)
        (bind-params (env-set ρ x (car args)) (cdr params) (cdr args))]
       [`(typed-default ,x ,τ ,e)
        (bind-params (env-set ρ x (car args)) (cdr params) (cdr args))]
       [_ ρ])]))


;; ────────────────────────────────────────────────────────────────────────────
;; FOR EXPRESSION
;; ────────────────────────────────────────────────────────────────────────────
;; Produces a "spreadable array" (can be flattened into enclosing array)
;; Verified against transpile_for() in transpile.cpp

(define (eval-for ρ x-item x-idx e-coll e-pred e-body)
  (define coll (eval-lambda ρ e-coll))
  (cond
    [(is-error? coll) coll]
    [else
     (define items (collection->list coll))
     (define results
       (for/list ([item items]
                  [idx (in-naturals)])
         (define ρ* (env-set (env-set ρ '_pipe_item item) '_pipe_index idx))
         (define ρ** (env-set (if x-idx (env-set ρ* x-idx idx) ρ*) x-item item))
         ;; apply where predicate
         (cond
           [(and e-pred
                 (let ([pred-v (eval-lambda ρ** e-pred)])
                   (not (truthy-val? pred-v))))
            'skip]
           [else (eval-lambda ρ** e-body)])))
     ;; filter out 'skip markers, flatten spread-vals
     (define final-items
       (apply append
              (map (λ (r)
                     (match r
                       ['skip '()]
                       [`(spread-val ,v) (collection->list v)]
                       [`(array-val ,items ...) ; nested for-expr result: flatten
                        items]
                       [_ (list r)]))
                   results)))
     `(array-val ,@final-items)]))


;; ── For-at (map iteration) ──

(define (eval-for-at ρ x-key x-val e-map e-body)
  (define m (eval-lambda ρ e-map))
  (cond
    [(is-error? m) m]
    [(map-val? m)
     (define pairs (map-pairs m))
     (define results
       (for/list ([pair pairs])
         (define k (car pair))
         (define v (cadr pair))
         (define ρ* (env-set ρ x-key `(sym ,(symbol->string k))))
         (define ρ** (if x-val (env-set ρ* x-val v) ρ*))
         (eval-lambda ρ** e-body)))
     `(array-val ,@results)]
    [else `(array-val)]))


;; ── Collection to list helper ──

(define (collection->list v)
  (match v
    [`(array-val ,items ...) items]
    [`(list-val ,items ...)  items]
    [`(range-val ,s ,e)
     (if (and (exact-integer? s) (exact-integer? e))
         (if (<= s e)
             (range s (+ e 1))   ; inclusive
             '())
         '())]
    [`(map-val ,pairs ...) (map cadr pairs)]  ; values
    ['null '()]
    [_ (list v)]))  ; scalar → single-element iteration


;; ────────────────────────────────────────────────────────────────────────────
;; PIPE EXPRESSION
;; ────────────────────────────────────────────────────────────────────────────
;; Mapping pipe: e | transform
;; ~ = current item, ~# = current index/key
;; Verified against transpile_pipe_expr() in transpile.cpp
;;
;; Semantics by left-side type:
;;   array  → map over elements, produce array
;;   list   → map over elements, produce list
;;   range  → map over numbers, produce array
;;   map    → map over values (~ = value, ~# = key), produce array
;;   scalar → apply transform once (~ = scalar), produce scalar

(define (eval-pipe ρ e-coll e-transform)
  (define coll (eval-lambda ρ e-coll))
  (cond
    [(is-error? coll) coll]
    [(array-val? coll)
     (define items (array-items coll))
     (define results
       (for/list ([item items] [idx (in-naturals)])
         (define ρ* (env-set (env-set ρ '_pipe_item item) '_pipe_index idx))
         (eval-lambda ρ* e-transform)))
     `(array-val ,@results)]
    [(list-val? coll)
     (define items (list-items coll))
     (define results
       (for/list ([item items] [idx (in-naturals)])
         (define ρ* (env-set (env-set ρ '_pipe_item item) '_pipe_index idx))
         (eval-lambda ρ* e-transform)))
     `(list-val ,@results)]
    [(range-val? coll)
     (define items (collection->list coll))
     (define results
       (for/list ([item items] [idx (in-naturals)])
         (define ρ* (env-set (env-set ρ '_pipe_item item) '_pipe_index idx))
         (eval-lambda ρ* e-transform)))
     `(array-val ,@results)]
    [(map-val? coll)
     (define pairs (map-pairs coll))
     (define results
       (for/list ([pair pairs])
         (define k (car pair))
         (define v (cadr pair))
         (define ρ* (env-set (env-set ρ '_pipe_item v)
                             '_pipe_index (symbol->string k)))
         (eval-lambda ρ* e-transform)))
     `(array-val ,@results)]
    [else
     ;; scalar: apply once
     (define ρ* (env-set ρ '_pipe_item coll))
     (eval-lambda ρ* e-transform)]))


;; ────────────────────────────────────────────────────────────────────────────
;; WHERE EXPRESSION
;; ────────────────────────────────────────────────────────────────────────────
;; collection where predicate(~) → filtered collection
;; Preserves input collection type (array → array, etc.)

(define (eval-where ρ e-coll e-pred)
  (define coll (eval-lambda ρ e-coll))
  (cond
    [(is-error? coll) coll]
    [(array-val? coll)
     (define items (array-items coll))
     (define filtered
       (for/list ([item items] [idx (in-naturals)]
                  #:when (truthy-val?
                          (eval-lambda
                           (env-set (env-set ρ '_pipe_item item) '_pipe_index idx)
                           e-pred)))
         item))
     `(array-val ,@filtered)]
    [(list-val? coll)
     (define items (list-items coll))
     (define filtered
       (filter (λ (item)
                 (truthy-val? (eval-lambda (env-set ρ '_pipe_item item) e-pred)))
               items))
     `(list-val ,@filtered)]
    [else coll]))


;; ────────────────────────────────────────────────────────────────────────────
;; MATCH EXPRESSION
;; ────────────────────────────────────────────────────────────────────────────
;; Arms tested top-to-bottom; first match wins.
;; ~ binds to scrutinee inside arms.

(define (eval-match ρ scrut clauses)
  (define ρ* (env-set ρ '_pipe_item scrut))
  (let loop ([cs clauses])
    (cond
      [(null? cs) 'null]  ; no match → null
      [else
       (match (car cs)
         [`(case-type ,τ ,body)
          (if (type-check-is scrut τ)
              (eval-lambda ρ* body)
              (loop (cdr cs)))]
         [`(case-val ,v ,body)
          (if (val-eq-racket? scrut v)
              (eval-lambda ρ* body)
              (loop (cdr cs)))]
         [`(case-range ,lo ,hi ,body)
          (if (and (numeric-val? scrut)
                   (>= (to-number scrut) (to-number lo))
                   (<= (to-number scrut) (to-number hi)))
              (eval-lambda ρ* body)
              (loop (cdr cs)))]
         [`(case-union ,t1 ,t2 ,body)
          (if (or (type-check-is scrut t1)
                  (type-check-is scrut t2))
              (eval-lambda ρ* body)
              (loop (cdr cs)))]
         [`(default-case ,body)
          (eval-lambda ρ* body)]
         [_ (loop (cdr cs))])])))


;; ────────────────────────────────────────────────────────────────────────────
;; EXPORTS
;; ────────────────────────────────────────────────────────────────────────────

(provide eval-lambda
         truthy-val? value->string val-eq-racket?
         is-error? closure? numeric-val?
         array-val? list-val? map-val? range-val?
         array-items list-items map-pairs
         collection->list apply-closure)
