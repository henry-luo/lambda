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
(require "lambda-object.rkt")


;; ────────────────────────────────────────────────────────────────────────────
;; BIG-STEP EVALUATOR
;; ────────────────────────────────────────────────────────────────────────────
;; We use a Racket-level evaluator (rather than judgment-form) for
;; flexibility with side-conditions and error propagation.

;; Dynamic parameter: when set, apply-closure dispatches pn-closures through it.
;; Signature: (σ ρ fn-val args) → value
;; Set by ast-bridge.rkt when running proc scripts.
(define current-proc-apply (make-parameter #f))

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
    [`(vmap-val ,pairs ...)    `(vmap-val ,@pairs)]
    [`(range-val ,a ,b)        `(range-val ,a ,b)]
    [`(closure ,env ,params ,body)  `(closure ,env ,params ,body)]
    [`(error-val ,msg ,code)   `(error-val ,msg ,code)]
    [`(type-val ,τ)            `(type-val ,τ)]

    ;; ── Variables ──
    [(? symbol? x)
     (cond
       [(eq? x 'inf)   +inf.0]
       [(eq? x 'nan)   +nan.0]
       [(eq? x 'true)  #t]
       [(eq? x 'false) #f]
       [(eq? x 'null)  'null]
       [(eq? x '~)  (env-ref ρ '_pipe_item)]
       [(eq? x '~#) (env-ref ρ '_pipe_index)]
       [(eq? x 'math.pi) (exact->inexact pi)]
       [(eq? x 'math.e)  (exp 1.0)]
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

    ;; ── Error destructuring: let a^err = expr ──
    [`(let-err ,val-name ,err-name ,expr ,body)
     (define v (eval-lambda ρ expr))
     (define ρ*
       (if (is-error? v)
           (env-set (env-set ρ val-name 'null) err-name v)
           (env-set (env-set ρ val-name v) err-name 'null)))
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

    ;; ── Named function definition ──
    ;; (def-fn name (params ...) body rest-expr)
    ;; Binds name as recursive closure in environment, then evaluates rest-expr.
    ;; Uses a mutable box to tie the recursive knot (letrec-style).
    [`(def-fn ,name (,params ...) ,body ,rest)
     (define self-box (box #f))
     ;; Env where name → box (env-ref auto-unboxes)
     (define ρ* (env-set ρ name self-box))
     ;; Create closure capturing ρ* (which has the box for self-reference)
     (define clos `(closure ,ρ* ,params ,body))
     ;; Tie the knot: box now holds the closure
     (set-box! self-box clos)
     (eval-lambda ρ* rest)]

    ;; ── Type alias definition ──
    ;; (def-type-alias name type-sym) — binds name as type-val in env
    [`(def-type-alias ,name ,type-sym)
     `(type-val ,type-sym)]

    ;; ── Function application ──
    [`(app ,e-fn ,e-args ...)
     (define fn-v (eval-lambda ρ e-fn))
     (cond
       [(is-error? fn-v) fn-v]
       ;; handle sys_* built-in symbols as system functions
       [(and (symbol? e-fn) (not (closure? fn-v)))
        (define arg-vals (map (λ (a) (eval-lambda ρ a)) e-args))
        (eval-sys-func e-fn arg-vals)]
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
       ;; null ++ x → x, x ++ null → x (C treats null as empty string in concat)
       [(eq? v1 'null) v2]
       [(eq? v2 'null) v1]
       ;; string ++ string
       [(and (string? v1) (string? v2))
        (string-append v1 v2)]
       ;; string ++ non-string: coerce RHS to string
       [(string? v1)
        (define s2 (match v2 [`(sym ,s) s] [_ (value->string v2)]))
        (string-append v1 s2)]
       ;; non-string ++ string: coerce LHS to string
       [(string? v2)
        (define s1 (match v1 [`(sym ,s) s] [_ (value->string v1)]))
        (string-append s1 v2)]
       ;; array ++ array
       [(and (array-val? v1) (array-val? v2))
        `(array-val ,@(array-items v1) ,@(array-items v2))]
       ;; list ++ list
       [(and (list-val? v1) (list-val? v2))
        `(list-val ,@(list-items v1) ,@(list-items v2))]
       ;; symbol ++ symbol → symbol
       [(and (match v1 [`(sym ,_) #t] [_ #f])
             (match v2 [`(sym ,_) #t] [_ #f]))
        `(sym ,(string-append (match v1 [`(sym ,s) s]) (match v2 [`(sym ,s) s])))]
       ;; symbol ++ anything → string concatenation
       [(match v1 [`(sym ,_) #t] [_ #f])
        (string-append (match v1 [`(sym ,s) s]) (value->string v2))]
       [(match v2 [`(sym ,_) #t] [_ #f])
        (string-append (value->string v1) (match v2 [`(sym ,s) s]))]
       ;; last resort: coerce both to string
       [else (string-append (value->string v1) (value->string v2))])]

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
     ;; τ could be: type symbol (int-type), registered nominal type, variable,
     ;; compound type (nullable/union), or a value expression for equality.
     (define known-type-syms
       '(any-type null-type bool-type int-type float-type number-type
         string-type symbol-type array-type list-type map-type range-type
         func-type error-type type-type element-type datetime-type
         path-type object-type))
     (define resolved-τ
       (cond
         ;; Known built-in type → use directly
         [(and (symbol? τ) (memq τ known-type-syms)) τ]
         ;; Registered nominal type → use directly
         [(and (symbol? τ) (type-registered? τ)) τ]
         ;; Variable in scope → resolve
         [(and (symbol? τ) (assq τ ρ))
          (define tv (env-ref ρ τ))
          (match tv
            [`(type-val ,inner) inner]
            [_ tv])]
         ;; Compound type (nullable, union) → use directly
         [(and (pair? τ) (memq (car τ) '(nullable union))) τ]
         ;; Otherwise → evaluate as value expression
         [else (eval-lambda ρ τ)]))
     ;; Determine if result is a type check or value comparison
     (if (or (and (symbol? resolved-τ)
                  (or (memq resolved-τ known-type-syms)
                      (type-registered? resolved-τ)))
             (and (pair? resolved-τ)
                  (memq (car resolved-τ) '(nullable union))))
         (type-check-is v resolved-τ)
         (val-eq-racket? v resolved-τ))]

    [`(is-not-type ,expr ,τ)
     (define v (eval-lambda ρ expr))
     (define known-type-syms2
       '(any-type null-type bool-type int-type float-type number-type
         string-type symbol-type array-type list-type map-type range-type
         func-type error-type type-type element-type datetime-type
         path-type object-type))
     (define resolved-τ
       (cond
         [(and (symbol? τ) (memq τ known-type-syms2)) τ]
         [(and (symbol? τ) (type-registered? τ)) τ]
         [(and (symbol? τ) (assq τ ρ))
          (define tv (env-ref ρ τ))
          (match tv [`(type-val ,inner) inner] [_ tv])]
         [(and (pair? τ) (memq (car τ) '(nullable union))) τ]
         [else (eval-lambda ρ τ)]))
     (define result
       (if (or (and (symbol? resolved-τ)
                    (or (memq resolved-τ known-type-syms2)
                        (type-registered? resolved-τ)))
               (and (pair? resolved-τ)
                    (memq (car resolved-τ) '(nullable union))))
           (type-check-is v resolved-τ)
           (val-eq-racket? v resolved-τ)))
     (not (truthy-val? result))]

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
       [(list-val? v2)
        (ormap (λ (item) (val-eq-racket? v1 item)) (list-items v2))]
       [(map-val? v2)
        (ormap (λ (pair) (val-eq-racket? v1 (cadr pair))) (map-pairs v2))]
       ;; string in string: substring check
       [(and (string? v1) (string? v2))
        (string-contains? v2 v1)]
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

    ;; ── Map with spread (e.g., (map-expr (c 1) m (g 2)) where m is a variable to spread) ──
    [`(map-expr ,items ...)
     (define pairs
       (let loop ([rest items] [acc '()])
         (cond
           [(null? rest) (reverse acc)]
           [(and (list? (car rest)) (= (length (car rest)) 2))
            (define k (car (car rest)))
            (define e (cadr (car rest)))
            (loop (cdr rest) (cons `(,k ,(eval-lambda ρ e)) acc))]
           ;; bare symbol is a spread variable
           [(symbol? (car rest))
            (define v (eval-lambda ρ (car rest)))
            (define extra-pairs (if (map-val? v) (map-pairs v) '()))
            (loop (cdr rest) (append (reverse extra-pairs) acc))]
           [else (loop (cdr rest) acc)])))
     ;; deduplicate: later entries override earlier ones (last-wins)
     (define deduped
       (let loop ([ps (reverse pairs)] [seen '()] [acc '()])
         (cond
           [(null? ps) acc]
           [(memq (caar ps) seen) (loop (cdr ps) seen acc)]
           [else (loop (cdr ps) (cons (caar ps) seen) (cons (car ps) acc))])))
     `(map-val ,@deduped)]

    ;; ── Element construction ──
    [`(element ,items ...)
     (define-values (attrs content)
       (for/fold ([attrs '()] [content '()]) ([item items])
         (match item
           [`(content ,children ...)
            ;; flatten seq forms inside content
            (define flat-children
              (apply append
                (for/list ([c children])
                  (match c
                    [`(seq ,items ...) items]
                    [_ (list c)]))))
            (define evaled-children
              (for/list ([c flat-children])
                (eval-lambda ρ c)))
            ;; coalesce consecutive strings (C concatenates adjacent text children)
            (define coalesced
              (let loop ([items evaled-children] [acc '()])
                (cond
                  [(null? items) (reverse acc)]
                  [(and (string? (car items))
                        (pair? acc)
                        (string? (car acc)))
                   (loop (cdr items) (cons (string-append (car acc) (car items)) (cdr acc)))]
                  [else (loop (cdr items) (cons (car items) acc))])))
            (values attrs coalesced)]
           [`(_tag ,name)
            ;; _tag is the element tag name — keep as literal, normalize to string
            (define name-str (if (symbol? name) (symbol->string name) name))
            (values (append attrs (list `(_tag ,name-str))) content)]
           [`(,k ,v)
            (values (append attrs (list `(,k ,(eval-lambda ρ v)))) content)]
           [_ (values attrs content)])))
     (if (null? content)
         `(element-val ,@attrs)
         `(element-val ,@attrs (element-content ,@content)))]

    ;; ── Collection access ──
    [`(member ,e-obj ,field)
     (define obj (eval-lambda ρ e-obj))
     (cond
       [(is-error? obj)
        ;; error member access: error.code, error.message
        (match obj
          [`(error-val ,msg ,code)
           (cond
             [(eq? field 'code) code]
             [(eq? field 'message) msg]
             [else obj])]  ; propagate error for other fields
          [_ obj])]
       [(eq? obj 'null) 'null]    ; null-safe: null.field → null
       [(object-val? obj) (object-member-ref obj field)]
       [(element-val? obj)
        ;; element member: check attributes and content
        (define pairs (element-attrs obj))
        (define found (assq field pairs))
        (cond
          [found (cadr found)]
          [(eq? field 'name)
           (define tag (element-tag obj))
           (if tag `(sym ,tag) 'null)]
          [(eq? field 'length)
           (+ (length pairs) (length (element-content obj)))]
          [else 'null])]
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
     (cond
       [(string? v) v]   ;; already a string — return as-is
       [(eq? v #t)  "true"]
       [(eq? v #f)  "false"]
       [(eq? v 'null) "null"]
       [(and (pair? v) (eq? (car v) 'sym)) (cadr v)]
       [else (value->string v)])]

    [`(to-bool ,e1)
     (define v (eval-lambda ρ e1))
     (truthy-val? v)]

    ;; ── to-symbol ──
    [`(to-symbol ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(and (list? v) (eq? (first v) 'sym)) v]  ; already a symbol
       [(string? v) `(sym ,v)]
       [else `(sym ,(value->string v))])]

    ;; ── as-type (type cast) ──
    ;; Runtime cast: if value is compatible, return as-is; otherwise error
    [`(as-type ,e1 ,τ)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(type-check-is v τ) v]
       [else `(error-val ,(format "cannot cast to ~a" τ) 300)])]

    ;; ── slice ──
    ;; e[i to j] — extract sub-array or sub-string (inclusive start, exclusive end)
    [`(slice ,e-obj ,e-start ,e-end)
     (define obj (eval-lambda ρ e-obj))
     (define s (eval-lambda ρ e-start))
     (define e (eval-lambda ρ e-end))
     (cond
       [(is-error? obj) obj]
       [(is-error? s) s]
       [(is-error? e) e]
       [(and (array-val? obj) (exact-integer? s) (exact-integer? e))
        (define items (array-items obj))
        (define len (length items))
        (define i (if (< s 0) (max 0 (+ len s)) (min s len)))
        (define j (if (< e 0) (max 0 (+ len e)) (min e len)))
        `(array-val ,@(if (<= i j) (take (drop items i) (- j i)) '()))]
       [(and (string? obj) (exact-integer? s) (exact-integer? e))
        (define len (string-length obj))
        (define i (if (< s 0) (max 0 (+ len s)) (min s len)))
        (define j (if (< e 0) (max 0 (+ len e)) (min e len)))
        (if (<= i j) (substring obj i j) "")]
       [else 'null])]

    ;; ── Collection builtins ──
    [`(len-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(array-val? v) (length (array-items v))]
       [(list-val? v)  (length (list-items v))]
       [(vmap-val? v) (length (map-pairs v))]
       [(map-val? v)   0]     ;; regular map: len counts content children (0)
       [(element-val? v) (length (element-content v))]
       [(string? v)    (string-length v)]
       [(range-val? v)
        (define s (range-start v))
        (define e (range-end v))
        (if (and (exact-integer? s) (exact-integer? e))
            (max 0 (+ 1 (- e s)))
            0)]
       [else 0])]      ;; Lambda: scalar len = 0 (null, bool, int, float)

    [`(sum-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(is-error? v) v]
       [(array-val? v)
        (foldl (λ (item acc) (arith-add acc item)) 0 (array-items v))]
       [(number? v) v]
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

    ;; sort with key function, direction symbol, or options map
    [`(sort-expr ,e1 ,e2)
     (define v (eval-lambda ρ e1))
     (define opt (eval-lambda ρ e2))
     (cond
       [(is-error? v) v]
       [(not (array-val? v)) v]
       [else
        (define items (array-items v))
        (cond
          ;; sort(arr, 'desc') — sort descending
          [(and (match opt [`(sym ,s) (equal? s "desc")] [_ #f]))
           `(array-val ,@(sort items (λ (a b) (val-less-than? b a))))]
          ;; sort(arr, key_fn) — sort by key function ascending
          [(closure? opt)
           `(array-val ,@(sort items (λ (a b)
                                       (val-less-than? (apply-closure opt (list a))
                                                       (apply-closure opt (list b))))))]
          ;; sort(arr, {by: key_fn, dir: 'desc'}) — options map
          [(map-val? opt)
           (define by-fn (map-ref opt 'by))
           (define dir (map-ref opt 'dir))
           (define desc? (and (match dir [`(sym ,s) (equal? s "desc")] [_ #f])))
           (define sorted
             (if (and by-fn (closure? by-fn))
                 (sort items (λ (a b)
                               (define ka (apply-closure by-fn (list a)))
                               (define kb (apply-closure by-fn (list b)))
                               (if desc? (val-less-than? kb ka) (val-less-than? ka kb))))
                 (if desc?
                     (sort items (λ (a b) (val-less-than? b a)))
                     (sort items val-less-than?))))
           `(array-val ,@sorted)]
          [else `(array-val ,@(sort items val-less-than?))])])]

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
        `(list-val ,@(take (array-items coll) (min n (length (array-items coll)))))]
       [else coll])]

    [`(drop-expr ,e1 ,e2)
     (define coll (eval-lambda ρ e1))
     (define n (eval-lambda ρ e2))
     (cond
       [(is-error? coll) coll]
       [(is-error? n) n]
       [(and (array-val? coll) (exact-integer? n))
        `(list-val ,@(drop (array-items coll) (min n (length (array-items coll)))))]
       [else coll])]

    ;; ── min / max / abs ──
    [`(min-expr ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (cond
       [(and (numeric-val? v1) (numeric-val? v2))
        (if (< (to-number v1) (to-number v2)) v1 v2)]
       [else 'null])]

    [`(max-expr ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (cond
       [(and (numeric-val? v1) (numeric-val? v2))
        (if (> (to-number v1) (to-number v2)) v1 v2)]
       [else 'null])]

    [`(abs-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(exact-integer? v) (abs v)]
       [(real? v) (abs v)]
       [else 'null])]

    ;; ── keys / values ──
    [`(keys-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(map-val? v)
        `(array-val ,@(map (λ (p) `(sym ,(symbol->string (car p)))) (map-pairs v)))]
       [(object-val? v)
        `(array-val ,@(map (λ (p) `(sym ,(symbol->string (car p)))) (object-fields v)))]
       [else `(array-val)])]

    [`(values-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(map-val? v)
        `(array-val ,@(map cadr (map-pairs v)))]
       [(object-val? v)
        `(array-val ,@(map cadr (object-fields v)))]
       [else `(array-val)])]

    ;; ── contains ──
    [`(contains-expr ,e1 ,e2)
     (define coll (eval-lambda ρ e1))
     (define item (eval-lambda ρ e2))
     (cond
       [(string? coll)
        (and (string? item) (string-contains? coll item))]
       [(array-val? coll)
        (ormap (λ (x) (val-eq-racket? x item)) (array-items coll))]
       [else #f])]

    ;; ── flatten ──
    [`(flatten-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(array-val? v)
        (define (flat items)
          (apply append
                 (map (λ (x)
                        (if (array-val? x) (flat (array-items x)) (list x)))
                      items)))
        `(array-val ,@(flat (array-items v)))]
       [else v])]

    ;; ── map / filter / reduce (higher-order) ──
    [`(map-fn ,e-coll ,e-fn)
     (define coll (eval-lambda ρ e-coll))
     (define fn-val (eval-lambda ρ e-fn))
     (cond
       [(and (array-val? coll) (closure? fn-val))
        `(array-val ,@(map (λ (item) (apply-closure fn-val (list item))) (array-items coll)))]
       [else `(error-val "map requires array and function" 300)])]

    [`(filter-fn ,e-coll ,e-fn)
     (define coll (eval-lambda ρ e-coll))
     (define fn-val (eval-lambda ρ e-fn))
     (cond
       [(and (array-val? coll) (closure? fn-val))
        `(array-val ,@(filter (λ (item) (truthy-val? (apply-closure fn-val (list item)))) (array-items coll)))]
       [else `(error-val "filter requires array and function" 300)])]

    [`(reduce-fn ,e-coll ,e-fn ,e-init)
     (define coll (eval-lambda ρ e-coll))
     (define fn-val (eval-lambda ρ e-fn))
     (define init (eval-lambda ρ e-init))
     (cond
       [(and (array-val? coll) (closure? fn-val))
        (foldl (λ (item acc) (apply-closure fn-val (list acc item))) init (array-items coll))]
       [else `(error-val "reduce requires array, function and init" 300)])]

    [`(reduce-fn ,e-coll ,e-fn)
     (define coll (eval-lambda ρ e-coll))
     (define fn-val (eval-lambda ρ e-fn))
     (cond
       [(and (array-val? coll) (closure? fn-val) (> (length (array-items coll)) 0))
        (define items (array-items coll))
        (foldl (λ (item acc) (apply-closure fn-val (list acc item))) (car items) (cdr items))]
       [else `(error-val "reduce requires non-empty array and function" 300)])]

    ;; ── Math functions ──
    [`(round-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(exact-integer? v) v]
       [(real? v) (inexact->exact (round (exact->inexact v)))]
       [else 'null])]

    [`(floor-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(exact-integer? v) v]
       [(real? v) (inexact->exact (floor (exact->inexact v)))]
       [else 'null])]

    [`(ceil-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(exact-integer? v) v]
       [(real? v) (inexact->exact (ceiling (exact->inexact v)))]
       [else 'null])]

    [`(sqrt-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(numeric-val? v)
        (define n (to-number v))
        (if (< n 0) +nan.0 (exact->inexact (sqrt n)))]
       [else 'null])]

    [`(log-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(numeric-val? v)
        (define n (to-number v))
        (cond
          [(< n 0) +nan.0]
          [(= n 0) -inf.0]
          [else (exact->inexact (log n))])]
       [else 'null])]

    [`(log10-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(numeric-val? v)
        (define n (to-number v))
        (cond
          [(< n 0) +nan.0]
          [(= n 0) -inf.0]
          [else (exact->inexact (/ (log n) (log 10)))])]
       [else 'null])]

    [`(sin-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (sin (to-number v)))] [else 'null])]

    [`(cos-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (cos (to-number v)))] [else 'null])]

    [`(tan-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (tan (to-number v)))] [else 'null])]

    [`(is-nan ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(real? v) (eqv? +nan.0 (exact->inexact v))]
       [else #f])]

    [`(avg-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0))
        (define items (array-items v))
        (define sum (foldl (λ (item acc) (+ acc (to-number item))) 0 items))
        (exact->inexact (/ sum (length items)))]
       [(number? v) v]
       [else 'null])]

    ;; ── String functions ──
    [`(split-expr ,e1 ,e2)
     (define s (eval-lambda ρ e1))
     (define sep (eval-lambda ρ e2))
     (cond
       [(and (string? s) (string? sep))
        `(list-val ,@(string-split s sep))]
       [else 'null])]

    [`(join-expr ,e1 ,e2)
     (define v (eval-lambda ρ e1))
     (define sep (eval-lambda ρ e2))
     (cond
       [(and (or (array-val? v) (list-val? v)) (string? sep))
        (define items (if (array-val? v) (array-items v) (list-items v)))
        (string-join (map (λ (x) (if (string? x) x (value->string x))) items) sep)]
       [else 'null])]

    [`(trim-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(string? v)
        (define trimmed (string-trim v))
        (if (string=? trimmed "") 'null trimmed)]
       [(match v [`(sym ,s) s] [_ #f])
        => (λ (s)
             (define trimmed (string-trim s))
             (if (string=? trimmed "") 'null `(sym ,trimmed)))]
       [else 'null])]

    [`(trim-start-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(string? v)
        (define trimmed (string-trim v #:right? #f))
        (if (string=? trimmed "") 'null trimmed)]
       [(match v [`(sym ,s) s] [_ #f])
        => (λ (s)
             (define trimmed (string-trim s #:right? #f))
             (if (string=? trimmed "") 'null `(sym ,trimmed)))]
       [else 'null])]

    [`(trim-end-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(string? v)
        (define trimmed (string-trim v #:left? #f))
        (if (string=? trimmed "") 'null trimmed)]
       [(match v [`(sym ,s) s] [_ #f])
        => (λ (s)
             (define trimmed (string-trim s #:left? #f))
             (if (string=? trimmed "") 'null `(sym ,trimmed)))]
       [else 'null])]

    [`(starts-with ,e1 ,e2)
     (define s (eval-lambda ρ e1))
     (define prefix (eval-lambda ρ e2))
     (cond
       [(and (string? s) (string? prefix)) (string-prefix? s prefix)]
       [else #f])]

    [`(ends-with ,e1 ,e2)
     (define s (eval-lambda ρ e1))
     (define suffix (eval-lambda ρ e2))
     (cond
       [(and (string? s) (string? suffix)) (string-suffix? s suffix)]
       [else #f])]

    [`(replace-expr ,e1 ,e2 ,e3)
     (define s (eval-lambda ρ e1))
     (define from (eval-lambda ρ e2))
     (define to (eval-lambda ρ e3))
     (cond
       [(and (string? s) (string? from) (string? to))
        (string-replace s from to)]
       [else s])]

    [`(index-of-expr ,e1 ,e2)
     (define s (eval-lambda ρ e1))
     (define sub (eval-lambda ρ e2))
     (cond
       [(and (string? s) (string? sub))
        (define pos (string-contains? s sub))
        (if pos pos -1)]
       [(and (array-val? s))
        (define idx (for/first ([item (in-list (array-items s))]
                                [i (in-naturals)]
                                #:when (val-eq-racket? item sub))
                      i))
        (or idx -1)]
       [else -1])]

    [`(slice-expr ,e1 ,e2 ,e3)
     (define v (eval-lambda ρ e1))
     (define start (eval-lambda ρ e2))
     (define end (eval-lambda ρ e3))
     (cond
       [(and (string? v) (exact-integer? start) (exact-integer? end))
        (define len (string-length v))
        (define s (max 0 (min start len)))
        (define e (max s (min end len)))
        (substring v s e)]
       [(and (array-val? v) (exact-integer? start) (exact-integer? end))
        (define items (array-items v))
        (define len (length items))
        (define s (max 0 (min start len)))
        (define e (max s (min end len)))
        `(array-val ,@(take (drop items s) (- e s)))]
       [else 'null])]

    [`(slice-expr ,e1 ,e2)
     (define v (eval-lambda ρ e1))
     (define start (eval-lambda ρ e2))
     (cond
       [(and (string? v) (exact-integer? start))
        (define len (string-length v))
        (define s (max 0 (min start len)))
        (substring v s)]
       [(and (array-val? v) (exact-integer? start))
        (define items (array-items v))
        (define s (max 0 (min start (length items))))
        `(array-val ,@(drop items s))]
       [else 'null])]

    [`(upper-expr ,e1)
     (define v (eval-lambda ρ e1))
     (if (string? v) (string-upcase v) 'null)]

    [`(lower-expr ,e1)
     (define v (eval-lambda ρ e1))
     (if (string? v) (string-downcase v) 'null)]

    [`(ord-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (string? v) (> (string-length v) 0))
        (char->integer (string-ref v 0))]
       [else 'null])]

    [`(chr-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(exact-integer? v) (string (integer->char v))]
       [else 'null])]

    [`(name-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(match v [`(sym ,_) #t] [_ #f]) v]  ; symbol → return symbol itself
       [(element-val? v)
        (define tag (element-tag v))
        (if tag `(sym ,tag) 'null)]
       [(match v [`(type-val ,t) t] [_ #f])
        => (λ (t) (define s (symbol->string t))
                  (define name (if (string-suffix? s "-type")
                                   (substring s 0 (- (string-length s) 5))
                                   s))
                  `(sym ,name))]
       [else 'null])]

    [`(to-int64 ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(exact-integer? v) v]
       [(real? v) (inexact->exact (truncate (exact->inexact v)))]
       [(string? v) (or (string->number v) 'null)]
       [else 'null])]

    ;; ── head / last / tail / init ──
    [`(head-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0))
        (car (array-items v))]
       [(and (string? v) (> (string-length v) 0))
        (substring v 0 1)]
       [else 'null])]

    [`(last-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0))
        (last (array-items v))]
       [(and (string? v) (> (string-length v) 0))
        (substring v (- (string-length v) 1))]
       [else 'null])]

    [`(tail-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0))
        `(array-val ,@(cdr (array-items v)))]
       [(and (string? v) (> (string-length v) 0))
        (substring v 1)]
       [else 'null])]

    [`(init-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0))
        `(array-val ,@(take (array-items v) (- (length (array-items v)) 1)))]
       [(and (string? v) (> (string-length v) 0))
        (substring v 0 (- (string-length v) 1))]
       [else 'null])]

    ;; ── zip / enumerate ──
    [`(zip-expr ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (cond
       [(and (array-val? v1) (array-val? v2))
        `(array-val ,@(map (λ (a b) `(array-val ,a ,b))
                           (array-items v1) (array-items v2)))]
       [else 'null])]

    [`(enumerate-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(array-val? v)
        `(array-val ,@(for/list ([item (in-list (array-items v))]
                                 [i (in-naturals)])
                        `(array-val ,i ,item)))]
       [else 'null])]

    [`(range-fn ,e1 ,e2)
     (define start (eval-lambda ρ e1))
     (define end (eval-lambda ρ e2))
     (cond
       [(and (exact-integer? start) (exact-integer? end))
        `(array-val ,@(range start end))]
       [else 'null])]

    ;; ── Bitwise operations ──
    [`(bit-and ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (if (and (exact-integer? v1) (exact-integer? v2))
         (bitwise-and v1 v2) 'null)]

    [`(bit-or ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (if (and (exact-integer? v1) (exact-integer? v2))
         (bitwise-ior v1 v2) 'null)]

    [`(bit-xor ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (if (and (exact-integer? v1) (exact-integer? v2))
         (bitwise-xor v1 v2) 'null)]

    [`(bit-not ,e1)
     (define v (eval-lambda ρ e1))
     (if (exact-integer? v) (bitwise-not v) 'null)]

    [`(bit-shl ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (if (and (exact-integer? v1) (exact-integer? v2))
         (arithmetic-shift v1 v2) 'null)]

    [`(bit-shr ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (if (and (exact-integer? v1) (exact-integer? v2))
         (arithmetic-shift v1 (- v2)) 'null)]

    ;; ── sign / trunc / exp / cbrt ──
    [`(sign-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (real? v) (not (equal? v 'null)))
        (define n (if (exact? v) v (exact->inexact v)))
        (cond [(positive? n) 1] [(negative? n) -1] [else 0])]
       [else 'null])]

    [`(trunc-expr ,e1)
     (define v (eval-lambda ρ e1))
     (define (trunc-one x)
       (cond [(exact-integer? x) x]
             [(real? x)
              (define t (truncate (exact->inexact x)))
              ;; preserve -0.0 as float (not 0)
              (if (and (zero? t) (negative? (exact->inexact x))) -0.0
                  (inexact->exact t))]
             [else 'null]))
     (cond
       [(or (exact-integer? v) (real? v)) (trunc-one v)]
       [(array-val? v)
        `(array-val ,@(map trunc-one (array-items v)))]
       [else 'null])]

    [`(exp-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (exp (to-number v)))] [else 'null])]

    [`(expm-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (- (exp (to-number v)) 1))] [else 'null])]

    [`(cbrt-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (expt (to-number v) 1/3))] [else 'null])]

    [`(hypot-expr ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (cond [(and (numeric-val? v1) (numeric-val? v2))
            (exact->inexact (sqrt (+ (* (to-number v1) (to-number v1))
                                     (* (to-number v2) (to-number v2)))))]
           [else 'null])]

    ;; ── inverse trig ──
    [`(atan-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (atan (to-number v)))] [else 'null])]

    [`(asin-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (asin (to-number v)))] [else 'null])]

    [`(acos-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (acos (to-number v)))] [else 'null])]

    ;; ── hyperbolic ──
    [`(sinh-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (sinh (to-number v)))] [else 'null])]

    [`(cosh-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (cosh (to-number v)))] [else 'null])]

    [`(tanh-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v) (exact->inexact (tanh (to-number v)))] [else 'null])]

    [`(asinh-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v)
            (define x (exact->inexact (to-number v)))
            (exact->inexact (log (+ x (sqrt (+ (* x x) 1)))))]
           [else 'null])]

    [`(acosh-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v)
            (define x (exact->inexact (to-number v)))
            (exact->inexact (log (+ x (sqrt (- (* x x) 1)))))]
           [else 'null])]

    [`(atanh-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond [(numeric-val? v)
            (define x (exact->inexact (to-number v)))
            (exact->inexact (* 0.5 (log (/ (+ 1 x) (- 1 x)))))]
           [else 'null])]

    ;; ── random ──
    [`(random-expr) (random)]

    ;; ── clock ──
    [`(clock-expr) (exact->inexact (/ (current-inexact-milliseconds) 1000.0))]

    ;; ── statistical functions ──
    [`(prod-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0))
        (foldl (λ (item acc) (* acc (to-number item))) 1 (array-items v))]
       [else 'null])]

    [`(cumsum-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(array-val? v)
        (define items (array-items v))
        (define-values (result _)
          (for/fold ([acc '()] [sum 0]) ([item items])
            (define new-sum (+ sum (to-number item)))
            (values (append acc (list new-sum)) new-sum)))
        `(array-val ,@result)]
       [else 'null])]

    [`(cumprod-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(array-val? v)
        (define items (array-items v))
        (define-values (result _)
          (for/fold ([acc '()] [prod 1]) ([item items])
            (define new-prod (* prod (to-number item)))
            (values (append acc (list new-prod)) new-prod)))
        `(array-val ,@result)]
       [else 'null])]

    [`(deviation-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0))
        (define items (map to-number (array-items v)))
        (define mean (/ (apply + items) (length items)))
        (exact->inexact (sqrt (/ (apply + (map (λ (x) (expt (- x mean) 2)) items)) (length items))))]
       [else 'null])]

    [`(variance-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0))
        (define items (map to-number (array-items v)))
        (define mean (/ (apply + items) (length items)))
        (exact->inexact (/ (apply + (map (λ (x) (expt (- x mean) 2)) items)) (length items)))]
       [else 'null])]

    [`(median-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0))
        (define items (sort (map to-number (array-items v)) <))
        (define n (length items))
        (if (odd? n)
            (exact->inexact (list-ref items (quotient n 2)))
            (exact->inexact (/ (+ (list-ref items (- (quotient n 2) 1))
                                  (list-ref items (quotient n 2))) 2)))]
       [else 'null])]

    [`(norm-expr ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0))
        (define items (map to-number (array-items v)))
        (exact->inexact (sqrt (apply + (map (λ (x) (* x x)) items))))]
       [else 'null])]

    [`(dot-expr ,e1 ,e2)
     (define v1 (eval-lambda ρ e1))
     (define v2 (eval-lambda ρ e2))
     (cond
       [(and (array-val? v1) (array-val? v2))
        (define result (apply + (map (λ (a b) (* (to-number a) (to-number b)))
                                     (array-items v1) (array-items v2))))
        (if (exact-integer? result) result (exact->inexact result))]
       [else 'null])]

    [`(quantile-expr ,e1 ,e2)
     (define v (eval-lambda ρ e1))
     (define q (eval-lambda ρ e2))
     (cond
       [(and (array-val? v) (> (length (array-items v)) 0) (real? q))
        (define items (sort (map to-number (array-items v)) <))
        (define n (length items))
        (define pos (* (to-number q) (- n 1)))
        (define lo (inexact->exact (floor pos)))
        (define hi (min (+ lo 1) (- n 1)))
        (define frac (- pos lo))
        (exact->inexact (+ (* (- 1 frac) (list-ref items lo))
                           (* frac (list-ref items hi))))]
       [else 'null])]

    ;; ── find / exists (higher-order) ──
    [`(find-fn ,e1 ,e2)
     (define coll (eval-lambda ρ e1))
     (define fn (eval-lambda ρ e2))
     (cond
       [(array-val? coll)
        (define found (for/first ([item (in-list (array-items coll))]
                                  #:when (truthy-val? (apply-closure fn (list item))))
                        item))
        (or found 'null)]
       [else 'null])]

    [`(exists-fn ,e1 ,e2)
     (define coll (eval-lambda ρ e1))
     (define fn (eval-lambda ρ e2))
     (cond
       [(array-val? coll)
        (for/or ([item (in-list (array-items coll))])
          (truthy-val? (apply-closure fn (list item))))]
       [else #f])]

    ;; ── fill ──
    [`(fill-expr ,e1 ,e2)
     (define n (eval-lambda ρ e1))
     (define val (eval-lambda ρ e2))
     (cond
       [(exact-integer? n)
        `(array-val ,@(make-list n val))]
       [else 'null])]

    ;; ── last-index-of ──
    [`(last-index-of-expr ,e1 ,e2)
     (define coll (eval-lambda ρ e1))
     (define target (eval-lambda ρ e2))
     (cond
       [(and (string? coll) (string? target))
        (define positions
          (for/list ([i (in-range (- (string-length coll) (string-length target) -1) -1 -1)]
                     #:when (string=? (substring coll i (+ i (string-length target))) target))
            i))
        (if (null? positions) -1 (car positions))]
       [(array-val? coll)
        (define items (array-items coll))
        (define idx
          (for/fold ([found -1]) ([item items] [i (in-naturals)])
            (if (val-eq-racket? item target) i found)))
        idx]
       [else -1])]

    ;; ── apply-fn (apply function to array of args) ──
    [`(apply-fn ,e1 ,e2)
     (define fn (eval-lambda ρ e1))
     (define args (eval-lambda ρ e2))
     (cond
       [(and (closure? fn) (array-val? args))
        (apply-closure fn (array-items args))]
       [else 'null])]

    ;; ── to-decimal (identity for now) ──
    [`(to-decimal ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(exact-integer? v) (exact->inexact v)]
       [(real? v) (exact->inexact v)]
       [(string? v) (or (string->number v) 'null)]
       [else 'null])]

    ;; ── format-expr (basic number formatting) ──
    [`(format-expr ,e1 ,e2)
     (define v (eval-lambda ρ e1))
     (define fmt (eval-lambda ρ e2))
     ;; simplified: just convert to string
     (value->string v)]

    ;; ── parse-expr / input-expr (parse string as JSON-like data) ──
    [`(parse-expr ,e1)
     (define v (eval-lambda ρ e1))
     ;; simplified: parse number-like strings
     (cond
       [(string? v) (or (string->number v) v)]
       [else v])]

    [`(input-expr ,e1)
     ;; input("json-string") — simplified: parse numbers
     (define v (eval-lambda ρ e1))
     (cond
       [(string? v) (or (string->number v) v)]
       [else v])]

    [`(input-expr ,e1 ,e2)
     (define v (eval-lambda ρ e1))
     (define fmt (eval-lambda ρ e2))
     (cond
       [(string? v) (or (string->number v) v)]
       [else v])]

    ;; ── argmin / argmax ──
    [`(argmin-fn ,e1 ,e2)
     (define coll (eval-lambda ρ e1))
     (define fn (eval-lambda ρ e2))
     (cond
       [(and (array-val? coll) (closure? fn))
        (define items (array-items coll))
        (define scored (map (λ (item) (cons (to-number (apply-closure fn (list item))) item)) items))
        (cdr (argmin car scored))]
       [else 'null])]

    [`(argmax-fn ,e1 ,e2)
     (define coll (eval-lambda ρ e1))
     (define fn (eval-lambda ρ e2))
     (cond
       [(and (array-val? coll) (closure? fn))
        (define items (array-items coll))
        (define scored (map (λ (item) (cons (to-number (apply-closure fn (list item))) item)) items))
        (cdr (argmax car scored))]
       [else 'null])]

    ;; ── def-type: register type definition, evaluate rest ──
    ;; The type is already registered via the registry API before evaluation,
    ;; so def-type in the functional evaluator just evaluates the rest expression.
    [`(def-type ,name ,rest-expr)
     (eval-lambda ρ rest-expr)]

    ;; ── Object construction: <TypeName field: val, ...> ──
    [`(make-object ,type-name (,field-names ,field-exprs) ...)
     (eval-make-object ρ type-name field-names field-exprs)]

    ;; ── Object update/spread: <TypeName *:source, field: val, ...> ──
    [`(object-update ,type-name ,source-expr (,field-names ,field-exprs) ...)
     (eval-object-update ρ type-name source-expr field-names field-exprs)]

    ;; ── fn method call: obj.method(args...) ──
    [`(method-call ,obj-expr ,method-name ,arg-exprs ...)
     (eval-method-call ρ obj-expr method-name arg-exprs)]

    ;; ── Statement sequence (block) ──
    ;; (seq s1 s2 ...) — evaluate left-to-right in functional mode
    ;; def-fn / bind / var forms extend the environment for subsequent forms
    ;; Returns the value of the last form
    [`(seq ,stmts ...)
     ;; Forward reference support: pre-scan for def-fn and create boxes
     (define fn-boxes
       (for/list ([s stmts]
                  #:when (match s
                           [`(def-fn ,name ,_ ,_ ,_) #t]
                           [_ #f]))
         (match s [`(def-fn ,name ,_ ,_ ,_) (cons name (box #f))])))
     (define ρ-fwd
       (for/fold ([env ρ]) ([fb fn-boxes])
         (env-set env (car fb) (cdr fb))))
     (let loop ([ss stmts] [ρ* ρ-fwd] [last-val 'null])
       (cond
         [(null? ss) last-val]
         [else
          (define s (car ss))
          (match s
            ;; def-fn: bind function name, continue
            [`(def-fn ,name (,params ...) ,body ,rest)
             (define existing-box (assoc name fn-boxes))
             (define self-box (if existing-box (cdr existing-box) (box #f)))
             (define ρ** (env-set ρ* name self-box))
             (define clos `(closure ,ρ** ,params ,body))
             (set-box! self-box clos)
             ;; if rest is non-null, evaluate rest in extended env
             (if (eq? rest 'null)
                 (loop (cdr ss) ρ** last-val)
                 ;; rest is already the continuation — evaluate it
                 (loop (cdr ss) ρ** (eval-lambda ρ** rest)))]
            ;; pn-def (procedural function): bind like fn
            [`(pn-def ,name (,params ...) ,body)
             (define clos `(closure ,ρ* ,params ,body))
             (loop (cdr ss) (env-set ρ* name clos) last-val)]
            ;; var: evaluate and bind
            [`(var ,name ,e)
             (define v (eval-lambda ρ* e))
             (loop (cdr ss) (env-set ρ* name v) last-val)]
            ;; bind: evaluate and bind
            [`(bind ,name ,e)
             (define v (eval-lambda ρ* e))
             (loop (cdr ss) (env-set ρ* name v) last-val)]
            ;; let: evaluate bindings, extend env
            [`(let ((,xs ,es) ...) ,_)
             (define ρ** (let-bind-seq ρ* xs es))
             (loop (cdr ss) ρ** last-val)]
            ;; let-err: error destructuring, extend env with both bindings
            [`(let-err ,val-name ,err-name ,expr ,_)
             (define v (eval-lambda ρ* expr))
             (define ρ**
               (if (is-error? v)
                   (env-set (env-set ρ* val-name 'null) err-name v)
                   (env-set (env-set ρ* val-name v) err-name 'null)))
             (loop (cdr ss) ρ** last-val)]
            ;; print: evaluate and capture as side effect
            ;; In functional seq, print is treated as producing null
            [`(print ,e)
             (define v (eval-lambda ρ* e))
             (loop (cdr ss) ρ* last-val)]
            ;; assign: update binding in env (for proc code running in functional eval)
            [`(assign ,name ,e)
             (define v (eval-lambda ρ* e))
             (loop (cdr ss) (env-set ρ* name v) last-val)]
            ;; def-type-alias: bind type alias as type-val
            [`(def-type-alias ,name ,type-sym)
             (loop (cdr ss) (env-set ρ* name `(type-val ,type-sym)) last-val)]
            ;; anything else: evaluate as expression, update last-val
            [_
             (define v (eval-lambda ρ* s))
             (loop (cdr ss) ρ* v)])]))]

    ;; ── Content sequence (string concatenation of all sub-expressions) ──
    ;; (content-seq e1 e2 ...) — evaluate all, convert to strings, concatenate
    [`(content-seq ,stmts ...)
     (let loop ([ss stmts] [ρ* ρ] [parts '()])
       (cond
         [(null? ss)
          (apply string-append (reverse parts))]
         [else
          (define s (car ss))
          (match s
            ;; def-fn: bind function name, no output
            [`(def-fn ,name (,params ...) ,body ,rest)
             (define self-box (box #f))
             (define ρ** (env-set ρ* name self-box))
             (define clos `(closure ,ρ** ,params ,body))
             (set-box! self-box clos)
             (if (eq? rest 'null)
                 (loop (cdr ss) ρ** parts)
                 (begin
                   (eval-lambda ρ** rest)
                   (loop (cdr ss) ρ** parts)))]
            ;; pn-def: bind, no output
            [`(pn-def ,name (,params ...) ,body)
             (define clos `(closure ,ρ* ,params ,body))
             (loop (cdr ss) (env-set ρ* name clos) parts)]
            ;; bind: evaluate and bind, no output
            [`(bind ,name ,e)
             (define v (eval-lambda ρ* e))
             (loop (cdr ss) (env-set ρ* name v) parts)]
            ;; let: evaluate bindings, extend env, no output
            [`(let ((,xs ,es) ...) ,_)
             (define ρ** (let-bind-seq ρ* xs es))
             (loop (cdr ss) ρ** parts)]
            ;; let-err: error destructuring, extend env, no output
            [`(let-err ,val-name ,err-name ,expr ,_)
             (define v (eval-lambda ρ* expr))
             (define ρ**
               (if (is-error? v)
                   (env-set (env-set ρ* val-name 'null) err-name v)
                   (env-set (env-set ρ* val-name v) err-name 'null)))
             (loop (cdr ss) ρ** parts)]
            ;; expression: evaluate, convert to raw string, accumulate
            [_
             (define v (eval-lambda ρ* s))
             (define str (match v
                           [(? string?) v]           ;; strings → raw (no quotes)
                           [`(sym ,name) name]       ;; symbols → raw
                           [_ (value->string v)]))   ;; others → formatted
             (loop (cdr ss) ρ* (cons str parts))])]))]

    ;; ── Type-of function ──
    [`(type-of ,e1)
     (define v (eval-lambda ρ e1))
     (cond
       [(eq? v 'null)          '(type-val null-type)]
       [(boolean? v)           '(type-val bool-type)]
       [(exact-integer? v)     '(type-val int-type)]
       [(real? v)              '(type-val float-type)]
       [(string? v)            '(type-val string-type)]
       [(match v [`(sym ,_) #t] [_ #f])   '(type-val symbol-type)]
       [(array-val? v)         '(type-val array-type)]
       [(list-val? v)          '(type-val list-type)]
       [(map-val? v)           '(type-val map-type)]
       [(range-val? v)         '(type-val range-type)]
       [(element-val? v)       '(type-val element-type)]
       [(match v [`(datetime-val ,_) #t] [_ #f]) '(type-val datetime-type)]
       [(match v [`(path-val ,_) #t] [_ #f]) '(type-val path-type)]
       [(closure? v)           '(type-val func-type)]
       [(is-error? v)          '(type-val error-type)]
       [(object-val? v)        `(type-val ,(object-type-name v))]
       [(match v [`(type-val ,_) #t] [_ #f]) '(type-val type-type)]
       [else                   '(type-val any-type)])]

    ;; ── Datetime literal ──
    [`(datetime ,s)
     `(datetime-val ,s)]

    ;; ── Path literal ──
    [`(path ,s)
     `(path-val ,s)]

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
    [`(pn-closure ,_ ,_ ,_ ,_) #t]
    [_ #f]))

(define (array-val? v)
  (match v [`(array-val ,_ ...) #t] [_ #f]))

(define (list-val? v)
  (match v [`(list-val ,_ ...) #t] [_ #f]))

(define (map-val? v)
  (match v [`(map-val ,_ ...) #t] [`(vmap-val ,_ ...) #t] [_ #f]))

(define (vmap-val? v)
  (match v [`(vmap-val ,_ ...) #t] [_ #f]))

(define (range-val? v)
  (match v [`(range-val ,_ ,_) #t] [_ #f]))

(define (element-val? v)
  (match v [`(element-val ,_ ...) #t] [_ #f]))

(define (element-attrs v)
  (define raw
    (match v [`(element-val ,pairs ... (element-content ,_ ...)) pairs]
             [`(element-val ,pairs ...) pairs]
             [_ '()]))
  ;; Filter out _tag (tag name) — it's not a user-visible attribute
  (filter (λ (p) (not (eq? (car p) '_tag))) raw))

(define (element-tag v)
  ;; Extract the tag name from the _tag pair
  (define raw
    (match v [`(element-val ,pairs ... (element-content ,_ ...)) pairs]
             [`(element-val ,pairs ...) pairs]
             [_ '()]))
  (define tag-pair (assq '_tag raw))
  (if tag-pair (cadr tag-pair) #f))

(define (element-content v)
  (match v [`(element-val ,_ ... (element-content ,children ...)) children]
           [_ '()]))

(define (numeric-val? v)
  (or (exact-integer? v) (real? v)))


;; ── Value accessors ──

(define (array-items v)
  (match v [`(array-val ,items ...) items] [_ '()]))

(define (list-items v)
  (match v [`(list-val ,items ...) items] [_ '()]))

(define (map-pairs v)
  (match v [`(map-val ,pairs ...) pairs] [`(vmap-val ,pairs ...) pairs] [_ '()]))

(define (range-start v)
  (match v [`(range-val ,s ,_) s] [_ 0]))

(define (range-end v)
  (match v [`(range-val ,_ ,e) e] [_ 0]))


;; ── Environment operations (Racket-level) ──

(define (env-ref ρ x)
  (cond
    [(assq x ρ) => (λ (pair)
                      (define v (cdr pair))
                      ;; Support recursive bindings via mutable box
                      (if (box? v) (unbox v) v))]
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

;; helper: render value with base indentation (for maps inside arrays)
(define (value->string-indent v base-indent)
  (match v
    [`(,(or 'map-val 'vmap-val) ,pairs ...)
     (define indent-str (make-string (+ base-indent 2) #\space))
     (define close-str (make-string base-indent #\space))
     (string-append "{\n"
                    (string-join
                     (map (λ (p) (format "~a~a: ~a" indent-str (car p) (value->string (cadr p))))
                          pairs)
                     ",\n")
                    (format "\n~a}" close-str))]
    [_ (value->string v)]))

;; helper: render element with children using multi-line indented format
(define (element->string-indented v indent)
  (define tag (or (element-tag v) "elmt"))
  (define attrs (element-attrs v))
  (define children (element-content v))
  (define indent-str (make-string indent #\space))
  (define child-indent-str (make-string (+ indent 2) #\space))
  (define content-indent (+ indent 2))
  (define parts (list (format "~a<~a" indent-str tag)))
  ;; attrs on same line
  (when (> (length attrs) 0)
    (define attr-str
      (string-join
       (map (λ (p) (format "~a: ~a" (car p) (value->string (cadr p)))) attrs) ", "))
    (set! parts (append parts (list (format " ~a" attr-str)))))
  ;; children
  (define child-strs
    (for/list ([c children])
      (cond
        [(element-val? c)
         (element->string-indented c (+ indent 2))]
        [(string? c)
         (format "~a~a" (make-string content-indent #\space) (value->string c))]
        [else
         (format "~a~a" (make-string content-indent #\space) (value->string c))])))
  ;; combine: tag line, then children, then closing >
  (string-append
   (car parts)
   (if (> (length (cdr parts)) 0) (apply string-append (cdr parts)) "")
   "\n"
   (string-join child-strs "\n")
   (format ">")))

(define (value->string v)
  (match v
    ['null       "null"]
    [#t          "true"]
    [#f          "false"]
    [(? exact-integer?) (number->string v)]
    [(? real?)   (cond
                   [(eqv? v +inf.0)  "inf"]
                   [(eqv? v -inf.0)  "-inf"]
                   [(eqv? v +nan.0)  "nan"]
                   [else
                    (let* ([n (exact->inexact v)])
                      ;; Lambda uses %.10f with trailing zero trimming for normal range
                      ;; and %g/%e for very large/small numbers
                      (define expt-val (if (zero? n) 0 (inexact->exact (floor (/ (log (abs n)) (log 2))))))
                      (cond
                        [(and (> expt-val -20) (< expt-val 30))
                         ;; normal range: %.10f then trim
                         (define s (real->decimal-string n 10))
                         ;; trim trailing zeros after decimal point
                         (define trimmed
                           (if (string-contains? s ".")
                               (let ([t (string-trim s "0" #:left? #f #:repeat? #t)])
                                 (if (string-suffix? t ".") (substring t 0 (- (string-length t) 1)) t))
                               s))
                         trimmed]
                        [else
                         ;; scientific notation: use ~g format (6 significant digits, like C %g)
                         (define s (~r n #:notation 'exponential #:precision 6))
                         ;; Racket ~r produces e.g. "1.22465e-16" — but we need to match C %g
                         ;; which strips trailing zeros. Use a manual approach:
                         (define g-str
                           (let* ([mag (abs n)]
                                  [exp-val (if (zero? mag) 0
                                               (inexact->exact (floor (/ (log mag) (log 10)))))]
                                  [sig-digits 6]
                                  [mantissa (/ mag (expt 10.0 exp-val))]
                                  [mant-str (real->decimal-string mantissa (- sig-digits 1))]
                                  [mant-trimmed (let ([t (string-trim mant-str "0" #:left? #f #:repeat? #t)])
                                                  (if (string-suffix? t ".") (substring t 0 (- (string-length t) 1)) t))]
                                  [sign (if (negative? n) "-" "")])
                             (format "~a~ae~a~a" sign mant-trimmed
                                     (if (negative? exp-val) "-" "+")
                                     ;; Lambda: positive exponents pad to 2 digits (e+09),
                                     ;; negative exponents use minimal digits (e-7)
                                     (let ([e (format "~a" (abs exp-val))])
                                       (if (and (not (negative? exp-val)) (< (string-length e) 2))
                                           (string-append "0" e)
                                           e)))))
                         ;; cleanup: normalize e+NN → e+NN, e-N → e-0N for C compat
                         g-str]))])]
    [(? string?) (format "\"~a\"" v)]           ;; Lambda: "hello"
    [`(sym ,s)   (format "'~a'" s)]             ;; Lambda: 'info'
    [`(error-val ,msg ,code)  "error"]
    [`(datetime-val ,s)       (format "t'~a'" s)]
    [`(path-val ,s)           (format "p'~a'" s)]
    [`(array-val ,items ...)
     (cond
       ;; Array containing maps: use special formatting with 4-space indent for maps
       [(ormap map-val? items)
        (format "[~a]"
          (string-join
           (map (λ (m) (if (map-val? m) (value->string-indent m 2) (value->string m))) items) ", "))]
       [else
        (format "[~a]" (string-join (map value->string items) ", "))])]
    [`(list-val ,items ...)
     (format "(~a)" (string-join (map value->string items) ", "))]
    [`(,(or 'map-val 'vmap-val) ,pairs ...)
     (if (<= (length pairs) 1)
         ;; single-entry map: inline
         (format "{~a}" (string-join
                         (map (λ (p) (format "~a: ~a" (car p) (value->string (cadr p))))
                              pairs) ", "))
         ;; multi-entry map: multi-line like Lambda
         (string-append "{\n"
                        (string-join
                         (map (λ (p) (format "  ~a: ~a" (car p) (value->string (cadr p))))
                              pairs)
                         ",\n")
                        "\n}"))]
    [`(object-val ,name ,pairs ...)
     (format "<~a ~a>" name
       (string-join
        (map (λ (p) (format "~a: ~a" (car p) (value->string (cadr p))))
             pairs) ", "))]
    [`(element-val ,rest ...)
     ;; Use element-tag and element-attrs accessors
     (define tag (or (element-tag v) "elmt"))
     (define attrs (element-attrs v))
     (define children (element-content v))
     (define attr-str
       (string-join
        (map (λ (p) (format "~a: ~a" (car p) (value->string (cadr p)))) attrs) ", "))
     (define has-attrs (> (string-length attr-str) 0))
     (define has-children (> (length children) 0))
     (cond
       [(and (not has-attrs) (not has-children))
        (format "<~a>" tag)]
       [(and has-attrs (not has-children))
        (format "<~a ~a>" tag attr-str)]
       [else
        ;; element with children: multi-line format
        (element->string-indented v 0)])]
    [`(range-val ,s ,e)
     ;; C expands ranges to arrays when displayed
     (define items (collection->list `(range-val ,s ,e)))
     (value->string `(array-val ,@items))]
    [`(closure ,_ ,_ ,_) "<function>"]
    [`(type-val ,τ)
     ;; format type values matching Lambda output
     ;; Most types: just the name. null: type.null. func: function.
     (match τ
       ['null-type   "type.null"]
       ['bool-type   "bool"]
       ['int-type    "int"]
       ['float-type  "float"]
       ['number-type "number"]
       ['string-type "string"]
       ['symbol-type "symbol"]
       ['array-type  "array"]
       ['list-type   "list"]
       ['map-type    "map"]
       ['range-type  "range"]
       ['func-type   "function"]
       ['error-type  "error"]
       ['element-type "element"]
       ['datetime-type "datetime"]
       ['path-type  "path"]
       ['type-type   "type"]
       ['any-type    "any"]
       ['object-type "object"]
       [_ (format "~a" τ)])]
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
    [`(,(or 'map-val 'vmap-val) ,pairs ...)
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
    ['element-type (element-val? v)]
    ['datetime-type (match v [`(datetime-val ,_) #t] [_ #f])]
    ['path-type     (match v [`(path-val ,_) #t] [_ #f])]
    ['object-type (object-val? v)]
    [`(nullable ,inner) (or (eq? v 'null) (type-check-is v inner))]
    [`(union ,t1 ,t2) (or (type-check-is v t1) (type-check-is v t2))]
    ;; Nominal type: check if value is an object whose type matches or inherits
    [_ (cond
         [(and (symbol? τ) (type-registered? τ))
          (and (object-val? v)
               (type-inherits? (object-type-name v) τ))]
         ;; Not a type — fall back to value equality (e.g., 42 is 42)
         [(val-eq-racket? v τ) #t]
         [else #f])]))


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
    [(eq? a 'null) b]  ; null + x → x
    [(eq? b 'null) a]  ; x + null → x
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
    ;; string * int = string repeat
    [(and (string? a) (exact-integer? b))
     (if (<= b 0) "" (apply string-append (make-list b a)))]
    [(and (exact-integer? a) (string? b))
     (if (<= a 0) "" (apply string-append (make-list a b)))]
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
     (define num (exact->inexact (to-number a)))
     (define denom (exact->inexact (to-number b)))
     (/ num denom)]  ;; Racket handles inf/nan/div-by-zero for floats
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
         (remainder a b))]  ;; C-style truncated modulo
    [(and (numeric-val? a) (numeric-val? b))
     (define denom (to-number b))
     (if (= denom 0.0)
         `(error-val "modulo by zero" 300)
         (let* ([na (to-number a)]
                [q (truncate (/ na denom))])
           (- na (* q denom))))]
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
    [`(pn-closure ,σ ,env ,params ,body)
     (define proc-apply (current-proc-apply))
     (if proc-apply
         (proc-apply σ env fn-val args)
         `(error-val "pn-closure not available in functional context" 300))]
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
    ;; map-val + kv destructuring: for (k, v in map) → k=key sym, v=value
    [(and (map-val? coll) x-idx)
     (define pairs (map-pairs coll))
     (define results
       (for/list ([pair pairs])
         (define k `(sym ,(symbol->string (car pair))))
         (define v (cadr pair))
         (define ρ* (env-set (env-set ρ '_pipe_item v) '_pipe_index k))
         (define ρ** (env-set (env-set ρ* x-idx k) x-item v))
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
     `(array-val ,@final-items)]
    [else
     (define items (collection->list coll))
     (define results
       (for/list ([item items]
                  [idx (in-naturals)])
         (define ρ* (env-set (env-set ρ '_pipe_item item) '_pipe_index idx))
         (define ρ** (env-set (if x-idx (env-set ρ* x-idx idx) ρ*) x-item item))
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
    [`(,(or 'map-val 'vmap-val) ,pairs ...) (map cadr pairs)]  ; values
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
;;
;; Aggregate pipe: when transform doesn't reference ~ or ~#, the entire
;; collection is passed as the first argument to the transform function.
;; e.g. arr | join(", ")  →  join(arr, ", ")
;;      arr | sort        →  sort(arr)

;; check if a form references ~ or ~# (pipe item/index)
(define (uses-pipe-var? form)
  (match form
    ['~ #t]
    ['~# #t]
    [(? pair?) (or (uses-pipe-var? (car form)) (uses-pipe-var? (cdr form)))]
    [_ #f]))

;; evaluate aggregate pipe: transform gets entire collection as first arg
(define (eval-pipe-aggregate ρ coll e-transform)
  (match e-transform
    ;; bare symbols for aggregate operations
    ['sum
     (define items (collection->list coll))
     (foldl arith-add 0 items)]
    ['sort
     (define items (collection->list coll))
     `(array-val ,@(sort items val-less-than?))]
    ['reverse
     (cond
       [(array-val? coll) `(array-val ,@(reverse (array-items coll)))]
       [(list-val? coll) `(list-val ,@(reverse (list-items coll)))]
       [(string? coll) (list->string (reverse (string->list coll)))]
       [else coll])]
    ;; system function forms (piped value = implicit first arg)
    [`(join-expr ,e-sep ,_)
     (define sep-raw (eval-lambda ρ e-sep))
     (define sep (if (eq? sep-raw 'null) "" sep-raw))  ;; null separator → empty string
     (cond
       [(and (or (array-val? coll) (list-val? coll)) (string? sep))
        (define items (if (array-val? coll) (array-items coll) (list-items coll)))
        (string-join (map (λ (x) (if (string? x) x (value->string x))) items) sep)]
       [else 'null])]
    [`(replace-expr ,e-old ,e-new ,e-flags)
     (define old (eval-lambda ρ e-old))
     (define new (eval-lambda ρ e-new))
     (if (and (string? coll) (string? old) (string? new))
         (string-replace coll old new)
         'null)]
    [`(slice-expr ,e-start ,e-end)
     (define start-v (eval-lambda ρ e-start))
     (define end-v (eval-lambda ρ e-end))
     (cond
       [(and (string? coll) (exact-integer? start-v) (exact-integer? end-v))
        (substring coll (min start-v (string-length coll)) (min end-v (string-length coll)))]
       [(and (array-val? coll) (exact-integer? start-v) (exact-integer? end-v))
        (define items (array-items coll))
        (define s (min start-v (length items)))
        (define e (min end-v (length items)))
        `(list-val ,@(take (drop items s) (max 0 (- e s))))]
       [else 'null])]
    [`(slice-expr ,e-start)
     (define start-v (eval-lambda ρ e-start))
     (cond
       [(and (string? coll) (exact-integer? start-v))
        (substring coll (min start-v (string-length coll)))]
       [(and (array-val? coll) (exact-integer? start-v))
        `(array-val ,@(drop (array-items coll) (min start-v (length (array-items coll)))))]
       [else 'null])]
    [`(split-expr ,e-sep ,_ ...)
     (define sep (eval-lambda ρ e-sep))
     (if (and (string? coll) (string? sep))
         `(list-val ,@(string-split coll sep))
         'null)]
    ;; app calls to sys_* functions: piped value = prepended first arg
    [`(app ,e-fn ,e-args ...)
     (define fn-sym (if (symbol? e-fn) e-fn #f))
     (cond
       [(eq? fn-sym 'sys_take)
        (define n (eval-lambda ρ (car e-args)))
        (define items (collection->list coll))
        `(list-val ,@(take items (min (if (exact-integer? n) n 0) (length items))))]
       [(eq? fn-sym 'sys_drop)
        (define n (eval-lambda ρ (car e-args)))
        (define items (collection->list coll))
        `(list-val ,@(drop items (min (if (exact-integer? n) n 0) (length items))))]
       [(eq? fn-sym 'sys_split)
        (define sep (eval-lambda ρ (car e-args)))
        (if (and (string? coll) (string? sep))
            `(list-val ,@(string-split coll sep))
            'null)]
       [(eq? fn-sym 'sys_contains)
        (define v (eval-lambda ρ (car e-args)))
        (cond
          [(and (string? coll) (string? v)) (if (string-contains? coll v) #t #f)]
          [else #f])]
       [(eq? fn-sym 'sys_starts_with)
        (define v (eval-lambda ρ (car e-args)))
        (if (and (string? coll) (string? v))
            (string-prefix? coll v)
            #f)]
       [(eq? fn-sym 'sys_ends_with)
        (define v (eval-lambda ρ (car e-args)))
        (if (and (string? coll) (string? v))
            (string-suffix? coll v)
            #f)]
       [(eq? fn-sym 'sys_index_of)
        (define v (eval-lambda ρ (car e-args)))
        (cond
          [(and (string? coll) (string? v))
           (define idx (let ([p (regexp-match-positions (regexp-quote v) coll)])
                         (if p (caar p) -1)))
           idx]
          [else -1])]
       [(eq? fn-sym 'sys_min)
        (define items (collection->list coll))
        (if (null? items) `(error-val "min of empty collection" 300)
            (foldl (λ (x acc) (if (< (to-number x) (to-number acc)) x acc))
                   (car items) (cdr items)))]
       [(eq? fn-sym 'sys_max)
        (define items (collection->list coll))
        (if (null? items) `(error-val "max of empty collection" 300)
            (foldl (λ (x acc) (if (> (to-number x) (to-number acc)) x acc))
                   (car items) (cdr items)))]
       ;; general function: pass collection as first arg
       [else
        (define fn-v (eval-lambda ρ e-fn))
        (define extra-args (map (λ (a) (eval-lambda ρ a)) e-args))
        (if (closure? fn-v)
            (apply-closure fn-v (cons coll extra-args))
            `(error-val "not a function in pipe" 300))])]
    ;; default fallback: bind ~ to collection
    [_ (define ρ* (env-set ρ '_pipe_item coll))
       (eval-lambda ρ* e-transform)]))

(define (eval-pipe ρ e-coll e-transform)
  (define coll (eval-lambda ρ e-coll))
  (cond
    [(is-error? coll) coll]
    ;; aggregate pipe: transform doesn't reference ~ or ~#
    [(not (uses-pipe-var? e-transform))
     (eval-pipe-aggregate ρ coll e-transform)]
    ;; mapping pipe
    [(array-val? coll)
     (define items (array-items coll))
     (if (null? items) 'null
         (let ([results (for/list ([item items] [idx (in-naturals)])
                          (define ρ* (env-set (env-set ρ '_pipe_item item) '_pipe_index idx))
                          (eval-lambda ρ* e-transform))])
           `(array-val ,@results)))]
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
                             '_pipe_index `(sym ,(symbol->string k))))
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
     (if (null? filtered)
         'null
         `(array-val ,@filtered))]
    [(list-val? coll)
     (define items (list-items coll))
     (define filtered
       (filter (λ (item)
                 (truthy-val? (eval-lambda (env-set ρ '_pipe_item item) e-pred)))
               items))
     `(list-val ,@filtered)]
    [(map-val? coll)
     ;; where on map: filter by value, return array of matching values
     (define pairs (map-pairs coll))
     (define filtered
       (for/list ([pair pairs]
                  #:when (truthy-val?
                          (eval-lambda
                           (env-set (env-set ρ '_pipe_item (cadr pair))
                                    '_pipe_index `(sym ,(symbol->string (car pair))))
                           e-pred)))
         (cadr pair)))
     (if (null? filtered)
         'null
         `(array-val ,@filtered))]
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
         [`(case-val ,v-expr ,body)
          (define v (eval-lambda ρ v-expr))
          (if (val-eq-racket? scrut v)
              (eval-lambda ρ* body)
              (loop (cdr cs)))]
         [`(case-range ,lo-expr ,hi-expr ,body)
          (define lo (eval-lambda ρ lo-expr))
          (define hi (eval-lambda ρ hi-expr))
          (if (and (numeric-val? scrut)
                   (>= (to-number scrut) (to-number lo))
                   (<= (to-number scrut) (to-number hi)))
              (eval-lambda ρ* body)
              (loop (cdr cs)))]
         [`(case-union . ,rest)
          ;; range or-pattern: (case-union (to-range lo hi) (to-range lo hi) ... body)
          ;; or legacy: (case-union lo1 to hi1 lo2 to hi2 ... body)
          (define body (last rest))
          (define range-parts (drop-right rest 1))
          (define matches?
            (let rloop ([r range-parts])
              (cond
                [(null? r) #f]
                ;; (to-range lo hi) form
                [(and (pair? (car r)) (eq? (caar r) 'to-range))
                 (define rng (car r))
                 (define lo (eval-lambda ρ (cadr rng)))
                 (define hi (eval-lambda ρ (caddr rng)))
                 (if (and (numeric-val? scrut)
                          (>= (to-number scrut) (to-number lo))
                          (<= (to-number scrut) (to-number hi)))
                     #t
                     (rloop (cdr r)))]
                ;; lo to hi flat form
                [(and (>= (length r) 3) (eq? (cadr r) 'to))
                 (define lo (eval-lambda ρ (car r)))
                 (define hi (eval-lambda ρ (caddr r)))
                 (if (and (numeric-val? scrut)
                          (>= (to-number scrut) (to-number lo))
                          (<= (to-number scrut) (to-number hi)))
                     #t
                     (rloop (cdddr r)))]
                ;; type union fallback: (case-union type1 type2 body)
                [(= (length r) 0) #f]
                [else
                 (if (type-check-is scrut (car r))
                     #t
                     (rloop (cdr r)))])))
          (if matches?
              (eval-lambda ρ* body)
              (loop (cdr cs)))]
         [`(default-case ,body)
          (eval-lambda ρ* body)]
         [_ (loop (cdr cs))])])))


;; ────────────────────────────────────────────────────────────────────────────
;; OBJECT OPERATIONS
;; ────────────────────────────────────────────────────────────────────────────

;; ── Object field access ──
;; Returns the value of a field on an object-val, or null if not found.
(define (object-member-ref obj field)
  (define fields (object-fields obj))
  (define pair (assq field fields))
  (cond
    [pair (cadr pair)]
    [else
     ;; not a field — try method lookup in type registry
     (define tname (object-type-name obj))
     (define mspec (and tname (resolve-method tname field)))
     (cond
       [(not mspec) 'null]
       [else
        ;; build a method closure: bind object field values + ~ in env
        (define field-env
          (for/fold ([env '()]) ([p fields])
            (cons (cons (car p) (cadr p)) env)))
        (define env-with-self (cons (cons '_pipe_item obj) field-env))
        `(closure ,env-with-self ,(method-spec-params mspec)
                  ,(method-spec-body mspec))])]))

;; ── Object construction ──
;; Evaluates field expressions, applies defaults, checks constraints.
;; Returns object-val or error-val.
(define (eval-make-object ρ type-name field-names field-exprs)
  (define td (lookup-type type-name))
  (cond
    [(not td) `(error-val ,(format "unknown type ~a" type-name) 300)]
    [else
     ;; evaluate provided field values
     (define provided-vals
       (for/list ([name field-names] [expr field-exprs])
         (list name (eval-lambda ρ expr))))
     ;; check for errors in provided values
     (define first-err
       (findf (λ (pair) (is-error? (cadr pair))) provided-vals))
     (cond
       [first-err (cadr first-err)]
       [else
        ;; resolve all fields (inherited + own) with defaults
        (define all-fields (all-fields-for-type type-name))
        (define resolved-fields
          (for/list ([fs all-fields])
            (define fname (field-spec-name fs))
            (define provided (assq fname provided-vals))
            (cond
              [provided (list fname (cadr provided))]
              [(has-default? fs)
               (define def-val (field-spec-default fs))
               (list fname (if (or (symbol? def-val) (pair? def-val))
                               (eval-lambda ρ def-val)
                               def-val))]
              [else (list fname 'null)])))
        ;; check field constraints
        (define constraint-error
          (for/or ([fs all-fields])
            (cond
              [(not (has-constraint? fs)) #f]  ; no constraint
              [else
               (define fname (field-spec-name fs))
               (define fc (field-spec-constraint fs))
               (define fval (cadr (assq fname resolved-fields)))
               ;; evaluate constraint with ~ bound to field value
               (define ρ* (env-set ρ '_pipe_item fval))
               (define result (eval-lambda ρ* fc))
               (if (truthy-val? result)
                   #f  ; constraint passed
                   `(error-val ,(format "field constraint failed for ~a" fname) 300))])))
        (cond
          [constraint-error constraint-error]
          [else
           ;; build the object
           (define obj `(object-val ,type-name ,@resolved-fields))
           ;; check object-level constraints
           (define obj-constraint-error
             (for/or ([c (all-constraints-for-type type-name)])
               (define ρ* (env-set ρ '_pipe_item obj))
               ;; also bind field names for accessing ~ member in constraints
               (define ρ**
                 (for/fold ([env ρ*]) ([pair resolved-fields])
                   (env-set env (car pair) (cadr pair))))
               (define result (eval-lambda ρ** c))
               (if (truthy-val? result)
                   #f  ; constraint passed
                   `(error-val "object constraint failed" 300))))
           (if obj-constraint-error
               obj-constraint-error
               obj)])])]))

;; ── Object update/spread ──
;; <TypeName *:source, field: val, ...>
;; Copies source fields, overrides with provided fields.
(define (eval-object-update ρ type-name source-expr field-names field-exprs)
  (define source (eval-lambda ρ source-expr))
  (cond
    [(is-error? source) source]
    [(not (object-val? source))
     `(error-val "spread source must be an object" 300)]
    [else
     ;; evaluate override field values
     (define overrides
       (for/list ([name field-names] [expr field-exprs])
         (list name (eval-lambda ρ expr))))
     ;; check for errors
     (define first-err
       (findf (λ (pair) (is-error? (cadr pair))) overrides))
     (cond
       [first-err (cadr first-err)]
       [else
        ;; get all fields from type definition
        (define all-fields (all-fields-for-type type-name))
        (define source-fields (object-fields source))
        ;; resolve each field: override > source > default > null
        (define resolved-fields
          (for/list ([fs all-fields])
            (define fname (field-spec-name fs))
            (define override-pair (assq fname overrides))
            (cond
              [override-pair (list fname (cadr override-pair))]
              [else
               (define source-pair (assq fname source-fields))
               (cond
                 [source-pair (list fname (cadr source-pair))]
                 [(has-default? fs)
                  (define def-val (field-spec-default fs))
                  (list fname (if (or (symbol? def-val) (pair? def-val))
                                  (eval-lambda ρ def-val)
                                  def-val))]
                 [else (list fname 'null)])])))
        ;; check field constraints
        (define constraint-error
          (for/or ([fs all-fields])
            (cond
              [(not (has-constraint? fs)) #f]
              [else
               (define fname (field-spec-name fs))
               (define fc (field-spec-constraint fs))
               (define fval (cadr (assq fname resolved-fields)))
               (define ρ* (env-set ρ '_pipe_item fval))
               (define result (eval-lambda ρ* fc))
               (if (truthy-val? result) #f
                   `(error-val ,(format "field constraint failed for ~a" fname) 300))])))
        (cond
          [constraint-error constraint-error]
          [else
           (define obj `(object-val ,type-name ,@resolved-fields))
           ;; check object-level constraints
           (define obj-constraint-error
             (for/or ([c (all-constraints-for-type type-name)])
               (define ρ* (env-set ρ '_pipe_item obj))
               (define ρ**
                 (for/fold ([env ρ*]) ([pair resolved-fields])
                   (env-set env (car pair) (cadr pair))))
               (define result (eval-lambda ρ** c))
               (if (truthy-val? result) #f
                   `(error-val "object constraint failed" 300))))
           (if obj-constraint-error obj-constraint-error obj)])])]))

;; ── fn method call ──
;; Dispatches a fn method on an object: evaluates args, binds fields + self,
;; evaluates method body.
(define (eval-method-call ρ obj-expr method-name arg-exprs)
  (define obj (eval-lambda ρ obj-expr))
  (cond
    [(is-error? obj) obj]
    [(not (object-val? obj))
     `(error-val ,(format "method call on non-object: ~a" method-name) 300)]
    [else
     (define tname (object-type-name obj))
     (define mspec (resolve-method tname method-name))
     (cond
       [(not mspec)
        `(error-val ,(format "unknown method ~a on ~a" method-name tname) 300)]
       [else
        ;; evaluate arguments
        (define arg-vals (map (λ (ae) (eval-lambda ρ ae)) arg-exprs))
        (define first-err (findf is-error? arg-vals))
        (cond
          [first-err first-err]
          [else
           ;; bind fields in method environment
           (define field-pairs (object-fields obj))
           (define ρ-fields
             (for/fold ([env '()]) ([pair field-pairs])
               (env-set env (car pair) (cadr pair))))
           ;; bind ~ to self
           (define ρ-self (env-set ρ-fields '_pipe_item obj))
           ;; bind method params
           (define ρ-final (bind-params ρ-self (method-spec-params mspec) arg-vals))
           ;; evaluate method body
           (eval-lambda ρ-final (method-spec-body mspec))])])]))


;; ────────────────────────────────────────────────────────────────────────────
;; EXPORTS
;; ────────────────────────────────────────────────────────────────────────────
;; SYSTEM FUNCTION DISPATCH (for (app sys_* ...) forms)
;; ────────────────────────────────────────────────────────────────────────────
(define (eval-sys-func name args)
  (match name
    ['sys_min
     (cond
       [(and (= (length args) 1) (or (array-val? (car args)) (list-val? (car args))))
        (define items (collection->list (car args)))
        (if (null? items) `(error-val "min of empty collection" 300)
            (foldl (λ (x acc) (if (< (to-number x) (to-number acc)) x acc))
                   (car items) (cdr items)))]
       [else 'null])]
    ['sys_max
     (cond
       [(and (= (length args) 1) (or (array-val? (car args)) (list-val? (car args))))
        (define items (collection->list (car args)))
        (if (null? items) `(error-val "max of empty collection" 300)
            (foldl (λ (x acc) (if (> (to-number x) (to-number acc)) x acc))
                   (car items) (cdr items)))]
       [else 'null])]
    ['sys_take
     (cond
       [(and (= (length args) 2) (or (array-val? (car args)) (list-val? (car args))) (exact-integer? (cadr args)))
        (define items (collection->list (car args)))
        (define n (cadr args))
        `(list-val ,@(take items (min n (length items))))]
       [else 'null])]
    ['sys_drop
     (cond
       [(and (= (length args) 2) (or (array-val? (car args)) (list-val? (car args))) (exact-integer? (cadr args)))
        (define items (collection->list (car args)))
        (define n (cadr args))
        `(list-val ,@(drop items (min n (length items))))]
       [else 'null])]
    ['sys_split
     (cond
       [(and (= (length args) 2) (string? (car args)) (string? (cadr args)))
        `(list-val ,@(string-split (car args) (cadr args)))]
       [else 'null])]
    ['sys_contains
     (cond
       [(and (= (length args) 2) (string? (car args)) (string? (cadr args)))
        (if (string-contains? (car args) (cadr args)) #t #f)]
       [else #f])]
    ['sys_starts_with
     (cond
       [(and (= (length args) 2) (string? (car args)) (string? (cadr args)))
        (string-prefix? (car args) (cadr args))]
       [else #f])]
    ['sys_ends_with
     (cond
       [(and (= (length args) 2) (string? (car args)) (string? (cadr args)))
        (string-suffix? (car args) (cadr args))]
       [else #f])]
    ['sys_index_of
     (cond
       [(and (= (length args) 2) (string? (car args)) (string? (cadr args)))
        (define p (regexp-match-positions (regexp-quote (cadr args)) (car args)))
        (if p (caar p) -1)]
       [else -1])]
    ['sys_math_log1p
     (cond
       [(and (= (length args) 1) (numeric-val? (car args)))
        (exact->inexact (log (+ 1 (to-number (car args)))))]
       [(and (= (length args) 1) (array-val? (car args)))
        `(array-val ,@(map (λ (x) (if (numeric-val? x) (exact->inexact (log (+ 1 (to-number x)))) 'null))
                           (array-items (car args))))]
       [else 'null])]
    ;; keys(map) → array of symbol keys
    ['sys_keys
     (cond
       [(and (= (length args) 1) (map-val? (car args)))
        `(array-val ,@(map (λ (p) `(sym ,(symbol->string (car p)))) (map-pairs (car args))))]
       [else 'null])]
    ;; values(map) → array of values
    ['sys_values
     (cond
       [(and (= (length args) 1) (map-val? (car args)))
        `(array-val ,@(map cadr (map-pairs (car args))))]
       [else 'null])]
    ;; map(array_of_kv_pairs) → ordered map (VMap)
    ['sys_map
     (cond
       [(= (length args) 0) '(vmap-val)]  ;; map() → empty VMap
       [(and (= (length args) 1) (array-val? (car args)))
        (define items (array-items (car args)))
        ;; pairs of consecutive elements [k1, v1, k2, v2, ...]
        (define pairs
          (let loop ([rest items] [acc '()])
            (cond
              [(or (null? rest) (null? (cdr rest))) (reverse acc)]
              [else
               (define k (car rest))
               (define v (cadr rest))
               (define key-sym (cond [(string? k) (string->symbol k)]
                                     [(and (pair? k) (eq? (car k) 'sym))
                                      (string->symbol (cadr k))]
                                     [(symbol? k) k]
                                     [else (string->symbol (value->string k))]))
               (loop (cddr rest) (cons `(,key-sym ,v) acc))])))
        `(vmap-val ,@pairs)]
       [else 'null])]
    [_ `(error-val ,(format "unknown system function: ~a" name) 300)]))

;; ────────────────────────────────────────────────────────────────────────────

(provide eval-lambda
         current-proc-apply
         truthy-val? value->string val-eq-racket?
         is-error? closure? numeric-val? to-number
         array-val? list-val? map-val? vmap-val? range-val?
         array-items list-items map-pairs
         collection->list apply-closure
         bind-params env-ref env-set
         type-check-is
         eval-method-call eval-make-object eval-object-update
         object-member-ref)
