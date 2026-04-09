#lang racket
;; ============================================================================
;; Lambda Script — Procedural Extension Semantics (PLT Redex)
;;
;; Extends the functional core (lambda-eval.rkt) with mutable state,
;; control flow, and side effects.
;;
;; Evaluation model:
;;   eval-proc : σ × ρ × e → Result
;;
;; where:
;;   σ = store (mutable hash: location → value)
;;   ρ = environment (alist: name → value-or-location)
;;   Result = (result 'normal value output)
;;          | (result 'break  #f    output)
;;          | (result 'continue #f  output)
;;          | (result 'return value output)
;;
;; Key design decisions:
;;   - σ is a Racket mutable hash (not threaded — mutated in place)
;;   - Mutable `var` bindings store a (loc sym) in ρ; σ[loc] holds the value
;;   - Immutable `let` bindings store the value directly in ρ
;;   - `pn` closures carry both ρ and σ; `fn` closures carry only ρ
;;   - `print` appends to an output accumulator (list of strings)
;;   - `while` catches break/continue; function call catches return
;;
;; Per Lambda spec (doc/Lambda_Procedural.md):
;;   - `var` = mutable binding (supports type widening)
;;   - `let` = immutable binding (assignment is error E211)
;;   - `pn` parameters are mutable (can be reassigned)
;;   - `fn` parameters are immutable
;; ============================================================================

(require redex)
(require "lambda-core.rkt")
(require "lambda-eval.rkt")


;; ════════════════════════════════════════════════════════════════════════════
;; 1. STORE (MUTABLE HEAP)
;; ════════════════════════════════════════════════════════════════════════════

;; A store is a mutable hash: location (gensym) → value.
;; We use Racket's mutable hashes so mutations are visible through closures.

(define (make-store) (make-hash))

(define (store-alloc! σ v)
  ;; Allocate a new location in the store, initialize with v
  (define loc (gensym 'loc))
  (hash-set! σ loc v)
  loc)

(define (store-read σ loc)
  (hash-ref σ loc (λ () 'null)))

(define (store-write! σ loc v)
  (hash-set! σ loc v))


;; ════════════════════════════════════════════════════════════════════════════
;; 2. ENVIRONMENT HELPERS (extended for mutable bindings)
;; ════════════════════════════════════════════════════════════════════════════

;; A mutable binding is stored as (name . (loc <sym>)) in the env alist.
;; An immutable binding is stored as (name . value) directly.

(define (is-loc? v) (and (list? v) (= (length v) 2) (eq? (car v) 'loc)))
(define (loc-sym v) (cadr v))

(define (proc-env-ref σ ρ x)
  ;; Look up x in ρ. If it's a location, dereference through σ.
  (define pair (assq x ρ))
  (cond
    [(not pair) 'null]
    [else
     (define v (cdr pair))
     (cond
       [(box? v) (unbox v)]            ; recursive binding (box)
       [(is-loc? v) (store-read σ (loc-sym v))]  ; mutable var
       [else v])]))                     ; immutable let

(define (proc-env-set ρ x v)
  ;; Add a new binding (shadows previous)
  (cons (cons x v) ρ))

(define (proc-env-find-loc ρ x)
  ;; Find the location for a mutable var, or #f if immutable/missing
  (define pair (assq x ρ))
  (cond
    [(not pair) #f]
    [else
     (define v (cdr pair))
     (if (is-loc? v) (loc-sym v) #f)]))


;; ════════════════════════════════════════════════════════════════════════════
;; 3. RESULT TYPE
;; ════════════════════════════════════════════════════════════════════════════

(struct result (tag value output) #:transparent)
;; tag: 'normal | 'break | 'continue | 'return
;; value: the result value (or #f for break/continue)
;; output: list of strings (accumulated print output)

(define (normal-result v out) (result 'normal v out))
(define (break-result out) (result 'break #f out))
(define (continue-result out) (result 'continue #f out))
(define (return-result v out) (result 'return v out))

(define (result-normal? r) (eq? (result-tag r) 'normal))
(define (result-break? r) (eq? (result-tag r) 'break))
(define (result-continue? r) (eq? (result-tag r) 'continue))
(define (result-return? r) (eq? (result-tag r) 'return))

;; Shorthand for normal result with no output
(define (norm v) (normal-result v '()))


;; ════════════════════════════════════════════════════════════════════════════
;; 4. PROCEDURAL EVALUATOR
;; ════════════════════════════════════════════════════════════════════════════

;; eval-proc : σ × ρ × e → Result
;; Evaluates expression/statement e in store σ and environment ρ.
;; Returns a Result with tag, value, and accumulated output.

(define (eval-proc σ ρ e)
  (match e

    ;; ── Statement sequence (block) ──
    ;; (seq s1 s2 ...) — evaluate left-to-right, threading env changes from var/def-pn
    [`(seq ,stmts ...)
     (eval-block σ ρ stmts)]

    ;; ── Mutable variable declaration ──
    ;; (var x e) — allocate store location, bind x to location
    [`(var ,x ,init-e)
     (define init-r (eval-proc σ ρ init-e))
     (cond
       [(not (result-normal? init-r)) init-r]
       [else
        (define loc (store-alloc! σ (result-value init-r)))
        ;; Return the new env binding as the "value" — caller must thread env
        ;; Actually, var is a statement that mutates env. We handle this
        ;; by having seq-with-env track env changes.
        ;; For simplicity, we return a special env-change result.
        (result 'normal `(env-extend ,x (loc ,loc)) (result-output init-r))])]

    ;; ── Assignment to variable ──
    ;; (assign x e) — find location for x, write new value
    [`(assign ,x ,rhs-e)
     (define rhs-r (eval-proc σ ρ rhs-e))
     (cond
       [(not (result-normal? rhs-r)) rhs-r]
       [else
        (define loc (proc-env-find-loc ρ x))
        (cond
          [loc
           (store-write! σ loc (result-value rhs-r))
           (normal-result (result-value rhs-r) (result-output rhs-r))]
          [else
           ;; Check if it's a pn param (mutable, stored as env-change)
           ;; pn params are also stored via locations
           (normal-result `(error-val ,(format "cannot assign to immutable '~a'" x) 211)
                          (result-output rhs-r))])])]

    ;; ── Assignment to array element ──
    ;; (assign-index arr-e idx-e val-e)
    [`(assign-index ,arr-x ,idx-e ,val-e)
     (define loc (proc-env-find-loc ρ arr-x))
     (define idx-r (eval-proc σ ρ idx-e))
     (define val-r (eval-proc σ ρ val-e))
     (cond
       [(not (result-normal? idx-r)) idx-r]
       [(not (result-normal? val-r)) val-r]
       [else
        (define arr-v (if loc (store-read σ loc) (proc-env-ref σ ρ arr-x)))
        (define idx (result-value idx-r))
        (define new-val (result-value val-r))
        (define out (append (result-output idx-r) (result-output val-r)))
        (match arr-v
          [`(array-val ,items ...)
           (define len (length items))
           (define real-idx (if (< idx 0) (+ len idx) idx))
           (if (and (>= real-idx 0) (< real-idx len))
               (let ([new-arr `(array-val ,@(list-set items real-idx new-val))])
                 (cond
                   [loc (store-write! σ loc new-arr) (normal-result new-val out)]
                   ;; if it's a let binding with a map/array, Lambda allows mutation
                   ;; on let containers (they're reference types)
                   [else (normal-result `(error-val "cannot index-assign immutable" 211) out)]))
               (normal-result 'null out))]
          [_ (normal-result `(error-val "not an array" 300) out)])])]

    ;; ── Assignment to map member ──
    ;; (assign-member obj-x field val-e)
    [`(assign-member ,obj-x ,field ,val-e)
     (define loc (proc-env-find-loc ρ obj-x))
     (define val-r (eval-proc σ ρ val-e))
     (cond
       [(not (result-normal? val-r)) val-r]
       [else
        (define obj-v (if loc (store-read σ loc) (proc-env-ref σ ρ obj-x)))
        (define new-val (result-value val-r))
        (define out (result-output val-r))
        (match obj-v
          [`(map-val ,pairs ...)
           (define new-pairs
             (cond
               [(assq field pairs)
                => (λ (existing) (map (λ (p) (if (eq? (car p) field) (list field new-val) p)) pairs))]
               [else (append pairs (list (list field new-val)))]))
           (define new-map `(map-val ,@new-pairs))
           (cond
             [loc (store-write! σ loc new-map) (normal-result new-val out)]
             [else
              ;; Let-bound containers can be mutated in Lambda
              ;; (they are reference types under the hood)
              ;; We simulate by updating the store through the container's own loc
              ;; For now, we allow it by env shadowing handled externally
              (normal-result `(error-val "cannot member-assign immutable" 211) out)])]
          [`(object-val ,type-name ,pairs ...)
           (define new-pairs
             (cond
               [(assq field pairs)
                => (λ (existing) (map (λ (p) (if (eq? (car p) field) (list field new-val) p)) pairs))]
               [else (append pairs (list (list field new-val)))]))
           (define new-obj `(object-val ,type-name ,@new-pairs))
           (cond
             [loc (store-write! σ loc new-obj) (normal-result new-val out)]
             [else (normal-result `(error-val "cannot member-assign immutable" 211) out)])]
          [_ (normal-result `(error-val "not a map/object" 300) out)])])]

    ;; ── While loop ──
    ;; (while cond-e body-e)
    [`(while ,cond-e ,body-e)
     (let loop ([out '()])
       (define cond-r (eval-proc σ ρ cond-e))
       (cond
         [(not (result-normal? cond-r))
          (result (result-tag cond-r) (result-value cond-r)
                  (append out (result-output cond-r)))]
         [(not (truthy-val? (result-value cond-r)))
          ;; condition is falsy → exit loop, return null
          (normal-result 'null (append out (result-output cond-r)))]
         [else
          (define body-r (eval-proc σ ρ body-e))
          (define new-out (append out (result-output cond-r) (result-output body-r)))
          (cond
            [(result-break? body-r)    (normal-result 'null new-out)]
            [(result-continue? body-r) (loop new-out)]
            [(result-return? body-r)   (result 'return (result-value body-r) new-out)]
            ;; normal → continue looping
            [else (loop new-out)])]))]

    ;; ── Break ──
    [`(break) (break-result '())]

    ;; ── Continue ──
    [`(continue) (continue-result '())]

    ;; ── Return ──
    ;; (return e)
    [`(return ,e)
     (define r (eval-proc σ ρ e))
     (cond
       [(not (result-normal? r)) r]
       [else (return-result (result-value r) (result-output r))])]

    ;; ── Print (side effect) ──
    ;; (print e) — evaluate e, convert to string, append to output
    [`(print ,e)
     (define r (eval-proc σ ρ e))
     (cond
       [(not (result-normal? r)) r]
       [else
        (define s (value->string (result-value r)))
        (normal-result 'null (append (result-output r) (list s)))])]

    ;; ── If-else (procedural, with side effects) ──
    ;; (if-proc cond then else)
    [`(if-proc ,cond-e ,then-e ,else-e)
     (define cond-r (eval-proc σ ρ cond-e))
     (cond
       [(not (result-normal? cond-r)) cond-r]
       [else
        (define branch-r
          (if (truthy-val? (result-value cond-r))
              (eval-proc σ ρ then-e)
              (eval-proc σ ρ else-e)))
        (result (result-tag branch-r) (result-value branch-r)
                (append (result-output cond-r) (result-output branch-r)))])]

    ;; ── If without else (procedural) ──
    [`(if-proc ,cond-e ,then-e)
     (eval-proc σ ρ `(if-proc ,cond-e ,then-e (seq)))]

    ;; ── Define procedural function ──
    ;; (def-pn name (params ...) body) — like def-fn but creates pn-closure
    [`(def-pn ,name (,params ...) ,body)
     (define self-box (box #f))
     (define ρ* (proc-env-set ρ name self-box))
     (define clos `(pn-closure ,σ ,ρ* ,params ,body))
     (set-box! self-box clos)
     (norm `(env-extend ,name ,self-box))]

    ;; ── Function application (procedural-aware) ──
    ;; (app-proc f args...) — handles both fn closures and pn closures
    [`(app-proc ,f-e ,arg-es ...)
     (define f-r (eval-proc σ ρ f-e))
     (cond
       [(not (result-normal? f-r)) f-r]
       [else
        (define arg-results
          (for/list ([ae arg-es])
            (eval-proc σ ρ ae)))
        (define first-non-normal
          (for/first ([r arg-results] #:when (not (result-normal? r))) r))
        (cond
          [first-non-normal first-non-normal]
          [else
           (define args (map result-value arg-results))
           (define arg-out (apply append (map result-output arg-results)))
           (define all-out (append (result-output f-r) arg-out))
           (define fn-val (result-value f-r))
           (apply-proc-closure σ ρ fn-val args all-out)])])]

    ;; ── Delegate to functional evaluator for pure expressions ──
    [_
     (with-handlers
         ([exn:fail? (λ (exn)
                       (norm `(error-val ,(exn-message exn) 300)))])
       (norm (eval-lambda (make-functional-env σ ρ) e)))]))


;; ════════════════════════════════════════════════════════════════════════════
;; 5. PROCEDURAL CLOSURE APPLICATION
;; ════════════════════════════════════════════════════════════════════════════

(define (apply-proc-closure σ caller-ρ fn-val args accumulated-out)
  (match fn-val
    ;; Pure functional closure — delegate to eval-lambda
    [`(closure ,clos-ρ ,params ,body)
     (define ρ* (bind-params clos-ρ params args))
     (define v (eval-lambda ρ* body))
     (normal-result v accumulated-out)]

    ;; Procedural closure — pn semantics with mutable params
    [`(pn-closure ,clos-σ ,clos-ρ ,params ,body)
     ;; pn params are mutable: allocate store locations for each
     (define ρ* (bind-pn-params clos-σ clos-ρ params args))
     (define r (eval-proc clos-σ ρ* body))
     (define out (append accumulated-out (result-output r)))
     (cond
       ;; return-result in pn → becomes normal result at call boundary
       [(result-return? r) (normal-result (result-value r) out)]
       ;; break/continue should not escape function boundary
       [(result-break? r) (normal-result 'null out)]
       [(result-continue? r) (normal-result 'null out)]
       [else (normal-result (result-value r) out)])]

    [_ (normal-result `(error-val "not a function" 300) accumulated-out)]))

;; Bind pn params — each param gets a store location (mutable)
(define (bind-pn-params σ ρ params args)
  (cond
    [(null? params) ρ]
    [(null? args)
     ;; remaining params get default or null
     (match (car params)
       [(? symbol? x)
        (define loc (store-alloc! σ 'null))
        (bind-pn-params σ (proc-env-set ρ x `(loc ,loc)) (cdr params) '())]
       [`(typed ,x ,τ)
        (define loc (store-alloc! σ 'null))
        (bind-pn-params σ (proc-env-set ρ x `(loc ,loc)) (cdr params) '())]
       [`(opt ,x)
        (define loc (store-alloc! σ 'null))
        (bind-pn-params σ (proc-env-set ρ x `(loc ,loc)) (cdr params) '())]
       [`(default ,x ,e)
        (define default-v (eval-lambda (make-functional-env σ ρ) e))
        (define loc (store-alloc! σ default-v))
        (bind-pn-params σ (proc-env-set ρ x `(loc ,loc)) (cdr params) '())]
       [`(typed-default ,x ,τ ,e)
        (define default-v (eval-lambda (make-functional-env σ ρ) e))
        (define loc (store-alloc! σ default-v))
        (bind-pn-params σ (proc-env-set ρ x `(loc ,loc)) (cdr params) '())]
       [_ ρ])]
    [else
     (match (car params)
       [(? symbol? x)
        (define loc (store-alloc! σ (car args)))
        (bind-pn-params σ (proc-env-set ρ x `(loc ,loc)) (cdr params) (cdr args))]
       [`(typed ,x ,τ)
        (define loc (store-alloc! σ (car args)))
        (bind-pn-params σ (proc-env-set ρ x `(loc ,loc)) (cdr params) (cdr args))]
       [`(opt ,x)
        (define loc (store-alloc! σ (car args)))
        (bind-pn-params σ (proc-env-set ρ x `(loc ,loc)) (cdr params) (cdr args))]
       [`(default ,x ,e)
        (define loc (store-alloc! σ (car args)))
        (bind-pn-params σ (proc-env-set ρ x `(loc ,loc)) (cdr params) (cdr args))]
       [`(typed-default ,x ,τ ,e)
        (define loc (store-alloc! σ (car args)))
        (bind-pn-params σ (proc-env-set ρ x `(loc ,loc)) (cdr params) (cdr args))]
       [_ ρ])]))


;; ════════════════════════════════════════════════════════════════════════════
;; 6. ENVIRONMENT BRIDGE
;; ════════════════════════════════════════════════════════════════════════════

;; Convert a procedural env (with locations) to a functional env (with values)
;; for delegating to eval-lambda.
(define (make-functional-env σ ρ)
  (map (λ (pair)
         (define v (cdr pair))
         (cond
           [(is-loc? v) (cons (car pair) (store-read σ (loc-sym v)))]
           [(box? v) (cons (car pair) (unbox v))]
           [else pair]))
       ρ))


;; ════════════════════════════════════════════════════════════════════════════
;; 7. BLOCK EVALUATOR (with env threading for var declarations)
;; ════════════════════════════════════════════════════════════════════════════

;; eval-block : σ × ρ × (list-of stmt) → Result
;; Evaluates a list of statements sequentially, threading env changes
;; from var declarations.

(define (eval-block σ ρ stmts)
  (cond
    [(null? stmts) (norm 'null)]
    [(null? (cdr stmts))
     ;; last statement — its result is the block's result
     (eval-block-stmt σ ρ (car stmts))]
    [else
     (define r (eval-block-stmt σ ρ (car stmts)))
     (cond
       [(not (result-normal? r)) r]  ; propagate control flow
       [else
        ;; Check if the result is an env-extend
        (define new-ρ (apply-env-change ρ (result-value r)))
        (define rest-r (eval-block σ new-ρ (cdr stmts)))
        (result (result-tag rest-r) (result-value rest-r)
                (append (result-output r) (result-output rest-r)))])]))

(define (eval-block-stmt σ ρ stmt)
  ;; Evaluate a single statement, handling var + def-pn env extensions
  (define r (eval-proc σ ρ stmt))
  r)

(define (apply-env-change ρ v)
  ;; If v is (env-extend name val), apply it to ρ
  (match v
    [`(env-extend ,name ,val) (proc-env-set ρ name val)]
    [_ ρ]))


;; ════════════════════════════════════════════════════════════════════════════
;; 8. TOP-LEVEL PROGRAM RUNNER
;; ════════════════════════════════════════════════════════════════════════════

;; run-proc : expr → (values value string)
;; Run a procedural expression/program. Returns the value and output string.
(define (run-proc e)
  (define σ (make-store))
  (define ρ '())
  (define r (eval-block σ ρ (if (and (pair? e) (eq? (car e) 'block))
                                (cdr e)
                                (list e))))
  (values (result-value r)
          (apply string-append (result-output r))))

;; run-pn-call : pn-body args → (values value string)
;; Convenience: define a pn and call it with given args.
(define (run-pn-call params body . args)
  (define σ (make-store))
  (define ρ '())
  (define fn-val `(pn-closure ,σ ,ρ ,params ,body))
  (define r (apply-proc-closure σ ρ fn-val args '()))
  (values (result-value r)
          (apply string-append (result-output r))))

;; run-pn-body : block-stmts → (values value string)
;; Convenience: evaluate a sequence of statements as a pn body.
(define (run-pn-body stmts)
  (define σ (make-store))
  (define ρ '())
  ;; Wrap stmts in a pn context (mutable)
  (define r (eval-block σ ρ stmts))
  ;; If the result was a return, unwrap it
  (define val (if (result-return? r) (result-value r) (result-value r)))
  ;; Clean up env-extend values
  (define clean-val (match val [`(env-extend ,_ ,_) 'null] [_ val]))
  (values clean-val
          (apply string-append (result-output r))))


;; ════════════════════════════════════════════════════════════════════════════
;; EXPORTS
;; ════════════════════════════════════════════════════════════════════════════

(provide eval-proc eval-block
         run-proc run-pn-call run-pn-body
         make-store store-alloc! store-read store-write!
         proc-env-ref proc-env-set proc-env-find-loc
         apply-proc-closure bind-pn-params
         make-functional-env
         result result-tag result-value result-output
         result-normal? result-break? result-continue? result-return?
         normal-result break-result continue-result return-result
         norm)
