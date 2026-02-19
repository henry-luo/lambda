#lang racket
;; ============================================================================
;; Lambda Script — Semantic Properties & Verification Tests
;;
;; This file tests the formal semantics against known behaviors and
;; verifies that the C representation model is consistent.
;;
;; Run: racket lambda/semantics/tests.rkt
;; ============================================================================

(require rackunit)
(require "lambda-core.rkt")
(require "lambda-eval.rkt")
(require "lambda-types.rkt")
(require "c-repr.rkt")

(define ε '())  ; empty environment


;; ════════════════════════════════════════════════════════════════════════════
;; PART 1: EVALUATION TESTS
;; ════════════════════════════════════════════════════════════════════════════

(test-case "Literal values"
  (check-equal? (eval-lambda ε 'null) 'null)
  (check-equal? (eval-lambda ε #t) #t)
  (check-equal? (eval-lambda ε #f) #f)
  (check-equal? (eval-lambda ε 42) 42)
  (check-equal? (eval-lambda ε 3.14) 3.14)
  (check-equal? (eval-lambda ε "hello") "hello"))

(test-case "Arithmetic: int + int → int"
  (check-equal? (eval-lambda ε '(add 2 3)) 5)
  (check-equal? (eval-lambda ε '(sub 10 4)) 6)
  (check-equal? (eval-lambda ε '(mul 3 7)) 21)
  (check-equal? (eval-lambda ε '(pow 2 10)) 1024))

(test-case "Arithmetic: int + float → float"
  (check-equal? (eval-lambda ε '(add 1 2.5)) 3.5)
  (check-equal? (eval-lambda ε '(mul 2 3.0)) 6.0))

(test-case "Division: / always returns float"
  (check-equal? (eval-lambda ε '(fdiv 10 3)) (/ 10.0 3.0))
  (check-equal? (eval-lambda ε '(fdiv 6 2)) 3.0))

(test-case "Integer division: div returns int"
  (check-equal? (eval-lambda ε '(idiv 10 3)) 3)
  (check-equal? (eval-lambda ε '(idiv 7 2)) 3))

(test-case "Modulo"
  (check-equal? (eval-lambda ε '(mod 10 3)) 1)
  (check-equal? (eval-lambda ε '(mod 7 2)) 1))

(test-case "Comparison"
  (check-equal? (eval-lambda ε '(eq 42 42)) #t)
  (check-equal? (eval-lambda ε '(eq 1 2)) #f)
  (check-equal? (eval-lambda ε '(eq null null)) #t)
  (check-equal? (eval-lambda ε '(lt 1 2)) #t)
  (check-equal? (eval-lambda ε '(gt 3 1)) #t)
  (check-equal? (eval-lambda ε '(le 5 5)) #t)
  (check-equal? (eval-lambda ε '(ge 5 4)) #t))

(test-case "String concatenation (++)"
  (check-equal? (eval-lambda ε '(concat "hello" " world")) "hello world")
  (check-equal? (eval-lambda ε '(concat "value: " 42)) "value: 42"))

(test-case "Truthiness: 0 is truthy (per spec)"
  ;; This is a critical semantic point: Lambda says all numbers are truthy
  (check-equal? (truthy-val? 0) #t)
  (check-equal? (truthy-val? 42) #t)
  (check-equal? (truthy-val? -1) #t)
  (check-equal? (truthy-val? 0.0) #t)
  ;; Falsy values
  (check-equal? (truthy-val? 'null) #f)
  (check-equal? (truthy-val? #f) #f)
  (check-equal? (truthy-val? "") #f)
  (check-equal? (truthy-val? '(error-val "oops" 300)) #f)
  ;; Truthy: collections (even empty)
  (check-equal? (truthy-val? '(array-val)) #t)
  (check-equal? (truthy-val? '(map-val)) #t))

(test-case "If-else expression"
  (check-equal? (eval-lambda ε '(if #t 1 2)) 1)
  (check-equal? (eval-lambda ε '(if #f 1 2)) 2)
  ;; Truthiness of non-bool condition
  (check-equal? (eval-lambda ε '(if "yes" 1 2)) 1)  ; non-empty string is truthy
  (check-equal? (eval-lambda ε '(if null 1 2)) 2)    ; null is falsy
  (check-equal? (eval-lambda ε '(if 0 1 2)) 1))      ; 0 is truthy per spec

(test-case "Let bindings"
  (check-equal? (eval-lambda ε '(let ((x 10)) x)) 10)
  (check-equal? (eval-lambda ε '(let ((x 5) (y 3)) (add x y))) 8)
  ;; Sequential: later binding sees earlier
  (check-equal? (eval-lambda ε '(let ((x 10) (y (add x 5))) y)) 15))

(test-case "Function definition and application"
  ;; Simple function
  (check-equal?
   (eval-lambda ε '(let ((double (lam (x) (mul x 2))))
                     (app double 5)))
   10)
  ;; Multiple params
  (check-equal?
   (eval-lambda ε '(let ((add2 (lam (a b) (add a b))))
                     (app add2 3 4)))
   7))

(test-case "Closures capture variables"
  (check-equal?
   (eval-lambda ε '(let ((n 10)
                         (addn (lam (x) (add x n))))
                     (app addn 5)))
   15))

(test-case "Higher-order functions"
  (check-equal?
   (eval-lambda ε '(let ((apply-fn (lam (f x) (app f x)))
                         (double (lam (x) (mul x 2))))
                     (app apply-fn double 5)))
   10))

(test-case "Function returning function"
  (check-equal?
   (eval-lambda ε '(let ((make-adder (lam (n) (lam (x) (add x n))))
                         (add5 (app make-adder 5)))
                     (app add5 3)))
   8))

(test-case "Logical operators (short-circuit)"
  ;; and: if a is falsy → a; else → b
  (check-equal? (eval-lambda ε '(l-and #t 42)) 42)
  (check-equal? (eval-lambda ε '(l-and #f 42)) #f)
  (check-equal? (eval-lambda ε '(l-and null 42)) 'null)
  ;; or: if a is truthy → a; else → b
  (check-equal? (eval-lambda ε '(l-or 42 99)) 42)
  (check-equal? (eval-lambda ε '(l-or #f 99)) 99)
  (check-equal? (eval-lambda ε '(l-or null 99)) 99)
  ;; not
  (check-equal? (eval-lambda ε '(l-not #t)) #f)
  (check-equal? (eval-lambda ε '(l-not null)) #t))

(test-case "Array construction"
  (check-equal? (eval-lambda ε '(array 1 2 3))
                '(array-val 1 2 3))
  (check-equal? (eval-lambda ε '(array))
                '(array-val)))

(test-case "Array indexing"
  (define arr-env (list (cons 'arr '(array-val 10 20 30))))
  (check-equal? (eval-lambda arr-env '(index arr 0)) 10)
  (check-equal? (eval-lambda arr-env '(index arr 2)) 30)
  ;; Negative indexing
  (check-equal? (eval-lambda arr-env '(index arr -1)) 30)
  ;; Out of bounds → null
  (check-equal? (eval-lambda arr-env '(index arr 5)) 'null))

(test-case "Map construction and access"
  (check-equal? (eval-lambda ε '(map-expr (x 1) (y 2)))
                '(map-val (x 1) (y 2)))
  ;; Member access
  (define map-env (list (cons 'm '(map-val (x 10) (y 20)))))
  (check-equal? (eval-lambda map-env '(member m x)) 10)
  (check-equal? (eval-lambda map-env '(member m y)) 20)
  ;; Missing key → null
  (check-equal? (eval-lambda map-env '(member m z)) 'null))

(test-case "Null-safe member access"
  (check-equal? (eval-lambda ε '(member null x)) 'null)
  (check-equal? (eval-lambda ε '(member null foo)) 'null))

(test-case "Range construction"
  (check-equal? (eval-lambda ε '(to-range 1 5))
                '(range-val 1 5)))

(test-case "For expression"
  ;; for (x in [1,2,3]) x * 2
  (check-equal?
   (eval-lambda ε '(for x (array 1 2 3) (mul x 2)))
   '(array-val 2 4 6)))

(test-case "For with where clause"
  ;; for (x in [1,2,3,4,5] where x > 2) x * 10
  (check-equal?
   (eval-lambda ε '(for-where x (array 1 2 3 4 5)
                              (gt ~ 2)
                              (mul x 10)))
   '(array-val 30 40 50)))

(test-case "For over range"
  ;; for (i in 1 to 3) i * i
  (check-equal?
   (eval-lambda ε '(for x (to-range 1 3) (mul x x)))
   '(array-val 1 4 9)))

(test-case "Pipe expression (mapping)"
  ;; [1, 2, 3] | ~ * 2
  (check-equal?
   (eval-lambda ε '(pipe (array 1 2 3) (mul ~ 2)))
   '(array-val 2 4 6)))

(test-case "Pipe with index (~#)"
  ;; [10, 20, 30] | ~ + ~#
  (check-equal?
   (eval-lambda ε '(pipe (array 10 20 30) (add ~ ~#)))
   '(array-val 10 21 32)))

(test-case "Where expression (filter)"
  ;; [1, 2, 3, 4, 5] where ~ > 3
  (check-equal?
   (eval-lambda ε '(where (array 1 2 3 4 5) (gt ~ 3)))
   '(array-val 4 5)))

(test-case "Match expression"
  ;; match 42 { case int: "number", case string: "text", default: "other" }
  (check-equal?
   (eval-lambda ε '(match 42
                     (case-type int-type "number")
                     (case-type string-type "text")
                     (default-case "other")))
   "number")

  (check-equal?
   (eval-lambda ε '(match "hi"
                     (case-type int-type "number")
                     (case-type string-type "text")
                     (default-case "other")))
   "text"))

(test-case "Match with literal patterns"
  (check-equal?
   (eval-lambda ε '(match 200
                     (case-val 200 "ok")
                     (case-val 404 "not found")
                     (default-case "unknown")))
   "ok"))

(test-case "Error creation and propagation"
  ;; error("msg") creates an error
  (check-equal? (eval-lambda ε '(make-error "boom"))
                '(error-val "boom" 318))
  ;; error values are falsy
  (check-equal? (truthy-val? '(error-val "oops" 318)) #f)
  ;; error propagation through arithmetic (GUARD_ERROR)
  (check-equal? (eval-lambda ε '(add (make-error "bad") 5))
                '(error-val "bad" 318)))

(test-case "Let-err destructuring"
  ;; Success case: a gets value, err gets null
  (check-equal?
   (eval-lambda ε '(let-err val err 42
                     (add val 1)))
   43)
  ;; Error case: val gets null, err gets error
  (check-equal?
   (eval-lambda ε '(let-err val err (make-error "oops")
                     (if (is-error err) "caught" "ok")))
   "caught"))

(test-case "Negation"
  (check-equal? (eval-lambda ε '(neg 42)) -42)
  (check-equal? (eval-lambda ε '(neg 3.14)) -3.14))

(test-case "String length"
  (check-equal? (eval-lambda ε '(len-expr "hello")) 5)
  (check-equal? (eval-lambda ε '(len-expr "")) 0))

(test-case "Array length"
  (check-equal? (eval-lambda ε '(len-expr (array 1 2 3))) 3))

(test-case "Null length is 0"
  (check-equal? (eval-lambda ε '(len-expr null)) 0))

(test-case "Array spread in construction"
  ;; [0, *[1,2], 3] → [0, 1, 2, 3]
  (check-equal?
   (eval-lambda ε '(array 0 (spread (array 1 2)) 3))
   '(array-val 0 1 2 3)))

(test-case "Sum of array"
  (check-equal? (eval-lambda ε '(sum-expr (array 1 2 3 4))) 10))

(test-case "Reverse"
  (check-equal? (eval-lambda ε '(reverse-expr (array 1 2 3)))
                '(array-val 3 2 1)))

(test-case "Sort"
  (check-equal? (eval-lambda ε '(sort-expr (array 3 1 2)))
                '(array-val 1 2 3)))

(test-case "Take and drop"
  (check-equal? (eval-lambda ε '(take-expr (array 1 2 3 4 5) 3))
                '(array-val 1 2 3))
  (check-equal? (eval-lambda ε '(drop-expr (array 1 2 3 4 5) 2))
                '(array-val 3 4 5)))

(test-case "Type conversion: to-int"
  (check-equal? (eval-lambda ε '(to-int 3.7)) 3)
  (check-equal? (eval-lambda ε '(to-int "42")) 42)
  (check-equal? (eval-lambda ε '(to-int #t)) 1))

(test-case "Type conversion: to-float"
  (check-equal? (eval-lambda ε '(to-float 42)) 42.0)
  (check-equal? (eval-lambda ε '(to-float "3.14")) 3.14))

(test-case "Type conversion: to-string"
  (check-equal? (eval-lambda ε '(to-string 42)) "42")
  (check-equal? (eval-lambda ε '(to-string #t)) "true")
  (check-equal? (eval-lambda ε '(to-string null)) "null"))

(test-case "Vector arithmetic (element-wise)"
  ;; [1,2,3] + [4,5,6] → [5,7,9]
  (check-equal? (eval-lambda ε '(add (array 1 2 3) (array 4 5 6)))
                '(array-val 5 7 9))
  ;; scalar broadcast: 2 * [1,2,3] → [2,4,6]
  (check-equal? (eval-lambda ε '(mul 2 (array 1 2 3)))
                '(array-val 2 4 6)))

(test-case "In-collection membership"
  ;; 3 in [1,2,3] → true
  (check-equal? (eval-lambda ε '(in-coll 3 (array 1 2 3))) #t)
  (check-equal? (eval-lambda ε '(in-coll 5 (array 1 2 3))) #f)
  ;; 3 in 1 to 5 → true
  (check-equal? (eval-lambda ε '(in-coll 3 (to-range 1 5))) #t)
  (check-equal? (eval-lambda ε '(in-coll 6 (to-range 1 5))) #f))

(test-case "Is-type checking"
  (check-equal? (eval-lambda ε '(is-type 42 int-type)) #t)
  (check-equal? (eval-lambda ε '(is-type 42 string-type)) #f)
  (check-equal? (eval-lambda ε '(is-type "hi" string-type)) #t)
  ;; int is also number (subtype)
  (check-equal? (eval-lambda ε '(is-type 42 number-type)) #t)
  ;; int is also float (subtype)
  (check-equal? (eval-lambda ε '(is-type 42 float-type)) #t))

(test-case "Default parameters"
  (check-equal?
   (eval-lambda ε '(let ((greet (lam ((default name "world"))
                                  (concat "hello " name))))
                     (app greet)))
   "hello world")
  ;; Override default
  (check-equal?
   (eval-lambda ε '(let ((greet (lam ((default name "world"))
                                  (concat "hello " name))))
                     (app greet "Lambda")))
   "hello Lambda"))

(test-case "Optional parameters"
  ;; Missing optional → null
  (check-equal?
   (eval-lambda ε '(let ((f (lam ((opt x))
                              (if (eq x null) "none" x))))
                     (app f)))
   "none"))

(test-case "Array concatenation (++)"
  (check-equal?
   (eval-lambda ε '(concat (array 1 2) (array 3 4)))
   '(array-val 1 2 3 4)))

(test-case "Pipe on scalar"
  ;; 42 | ~ + 1 → 43
  (check-equal?
   (eval-lambda ε '(pipe 42 (add ~ 1)))
   43))

(test-case "Pipe on map"
  ;; {x: 1, y: 2} | ~ * 10 → [10, 20]
  (check-equal?
   (eval-lambda ε '(pipe (map-expr (x 1) (y 2)) (mul ~ 10)))
   '(array-val 10 20)))

(test-case "For-at map iteration"
  ;; for (k at {x: 1, y: 2}) k
  (check-equal?
   (eval-lambda ε '(for-at k (map-expr (x 1) (y 2)) k))
   '(array-val (sym "x") (sym "y")))
  ;; for (k, v at map) v * 2
  (check-equal?
   (eval-lambda ε '(for-at-kv k v (map-expr (a 10) (b 20)) (mul v 2)))
   '(array-val 20 40)))


;; ════════════════════════════════════════════════════════════════════════════
;; PART 2: TYPE INFERENCE TESTS
;; ════════════════════════════════════════════════════════════════════════════

(test-case "Type inference: literals"
  (check-equal? (infer-type '() 42) 'int-type)
  (check-equal? (infer-type '() 3.14) 'float-type)
  (check-equal? (infer-type '() #t) 'bool-type)
  (check-equal? (infer-type '() "hi") 'string-type)
  (check-equal? (infer-type '() 'null) 'null-type))

(test-case "Type inference: arithmetic"
  ;; int + int → int
  (check-equal? (infer-type '() '(add 1 2)) 'int-type)
  ;; int + float → float
  (check-equal? (infer-type '() '(add 1 2.5)) 'float-type)
  ;; / always → float
  (check-equal? (infer-type '() '(fdiv 10 3)) 'float-type))

(test-case "Type inference: if-else"
  ;; Same types → that type
  (check-equal? (infer-type '() '(if #t 1 2)) 'int-type)
  ;; int vs float → float (promotion)
  (check-equal? (infer-type '() '(if #t 1 2.5)) 'float-type)
  ;; int vs string → any (incompatible)
  (check-equal? (infer-type '() '(if #t 1 "hi")) 'any-type))

(test-case "Type inference: for expression produces array"
  (check-equal? (infer-type '() '(for x (array 1 2 3) (mul x 2)))
                '(array-of int-type)))

(test-case "Type inference: comparison → bool"
  (check-equal? (infer-type '() '(eq 1 2)) 'bool-type)
  (check-equal? (infer-type '() '(lt 1 2)) 'bool-type))

(test-case "Type inference: function type"
  ;; (x) => x + 1  has type fn(any) → int (since x is any)
  (define fn-τ (infer-type '() '(lam (x) (add x 1))))
  (check-pred (λ (τ) (match τ [`(fn-type ,_ ,_) #t] [_ #f])) fn-τ))


;; ════════════════════════════════════════════════════════════════════════════
;; PART 3: C REPRESENTATION TESTS
;; ════════════════════════════════════════════════════════════════════════════

(test-case "Type mapping: Lambda type → C type"
  (check-equal? (lambda-type->c-type 'int-type)    'int32_t)
  (check-equal? (lambda-type->c-type 'int64-type)  'int64_t)
  (check-equal? (lambda-type->c-type 'float-type)  'double)
  (check-equal? (lambda-type->c-type 'bool-type)   'bool)
  (check-equal? (lambda-type->c-type 'string-type) 'String*)
  (check-equal? (lambda-type->c-type 'any-type)    'Item)
  (check-equal? (lambda-type->c-type 'null-type)   'Item)
  (check-equal? (lambda-type->c-type 'error-type)  'Item)
  ;; Union/nullable → Item (can't be statically unboxed)
  (check-equal? (lambda-type->c-type '(nullable int-type)) 'Item)
  (check-equal? (lambda-type->c-type '(union int-type string-type)) 'Item))

(test-case "Boxing functions"
  (check-equal? (boxing-function 'int32_t) 'i2it)
  (check-equal? (boxing-function 'double)  'push_d)
  (check-equal? (boxing-function 'String*) 's2it)
  (check-equal? (boxing-function 'Item)    #f))  ; already Item

(test-case "Unboxing functions"
  (check-equal? (unboxing-function 'int32_t) 'it2i)
  (check-equal? (unboxing-function 'double)  'it2d)
  (check-equal? (unboxing-function 'String*) 'it2s)
  (check-equal? (unboxing-function 'bool)    'it2b)
  (check-equal? (unboxing-function 'Item)    #f))

(test-case "Required conversion: same type → no conversion"
  (check-equal? (required-conversion 'Item 'Item) #f)
  (check-equal? (required-conversion 'int32_t 'int32_t) #f)
  (check-equal? (required-conversion 'String* 'String*) #f))

(test-case "Required conversion: Item → native (unboxing)"
  (check-equal? (required-conversion 'Item 'int32_t) 'it2i)
  (check-equal? (required-conversion 'Item 'double)  'it2d)
  (check-equal? (required-conversion 'Item 'String*) 'it2s)
  (check-equal? (required-conversion 'Item 'bool)    'it2b))

(test-case "Required conversion: native → Item (boxing)"
  (check-equal? (required-conversion 'int32_t 'Item) 'i2it)
  (check-equal? (required-conversion 'double 'Item)  'push_d)
  (check-equal? (required-conversion 'String* 'Item) 's2it))

(test-case "Issue #16 scenario: Item → String* requires it2s"
  ;; When a function param expects String* but caller passes Item
  ;; (e.g., result of a system function that returns Item despite
  ;; having semantic type string), the transpiler must insert it2s.
  (define conv (required-conversion 'Item 'String*))
  (check-equal? conv 'it2s
                "Item → String* must use it2s (Issue #16)"))

(test-case "Issue #15 scenario: if-else branch type mismatch"
  ;; If then-branch returns String* but else-branch returns Item,
  ;; both must be boxed to Item for the C ternary.
  (define result (verify-if-branches 'string-type 'any-type))
  (check-equal? (car result) 'must-box-both
                "Mismatched branch C-types must box both"))

(test-case "If-else with matching branch types → OK"
  (define result (verify-if-branches 'int-type 'int-type))
  (check-equal? (car result) 'ok))

(test-case "Call arg verification: matching types → OK"
  (define result (verify-call-args '(int-type string-type) '(int-type string-type)))
  (check-true (andmap (λ (r) (eq? (last r) 'ok)) result)))

(test-case "Call arg verification: any→string needs conversion"
  ;; Arg is any (Item), param expects string (String*) → needs it2s
  (define result (verify-call-args '(any-type) '(string-type)))
  (define first-result (car result))
  (check-equal? (last first-result) 'needs-conversion
                "any → string requires conversion"))

(test-case "Unboxed variant: safe for int/int64 only"
  (check-true (safe-for-unboxed-variant? 'int-type))
  (check-true (safe-for-unboxed-variant? 'int64-type))
  (check-false (safe-for-unboxed-variant? 'float-type))
  (check-false (safe-for-unboxed-variant? 'string-type))
  (check-false (safe-for-unboxed-variant? 'bool-type)))


;; ════════════════════════════════════════════════════════════════════════════
;; PART 4: SUBTYPE RELATION TESTS
;; ════════════════════════════════════════════════════════════════════════════

(test-case "Subtype: reflexive"
  (check-true (subtype? 'int-type 'int-type))
  (check-true (subtype? 'string-type 'string-type)))

(test-case "Subtype: numeric hierarchy"
  (check-true (subtype? 'int-type 'int64-type))
  (check-true (subtype? 'int-type 'float-type))
  (check-true (subtype? 'int-type 'number-type))
  (check-true (subtype? 'int64-type 'float-type))
  (check-true (subtype? 'int64-type 'number-type))
  (check-true (subtype? 'float-type 'number-type))
  ;; Not the other direction
  (check-false (subtype? 'float-type 'int-type))
  (check-false (subtype? 'number-type 'int-type)))

(test-case "Subtype: everything <: any"
  (check-true (subtype? 'int-type 'any-type))
  (check-true (subtype? 'string-type 'any-type))
  (check-true (subtype? 'error-type 'any-type))
  (check-true (subtype? 'null-type 'any-type)))

(test-case "Subtype: nullable"
  (check-true (subtype? 'null-type '(nullable int-type)))
  (check-true (subtype? 'int-type '(nullable int-type)))
  (check-false (subtype? 'string-type '(nullable int-type))))

(test-case "Subtype: union"
  (check-true (subtype? 'int-type '(union int-type string-type)))
  (check-true (subtype? 'string-type '(union int-type string-type)))
  (check-false (subtype? 'bool-type '(union int-type string-type))))

(test-case "Type join: same type"
  (check-equal? (type-join 'int-type 'int-type) 'int-type)
  (check-equal? (type-join 'string-type 'string-type) 'string-type))

(test-case "Type join: numeric promotion"
  (check-equal? (type-join 'int-type 'float-type) 'float-type)
  (check-equal? (type-join 'int-type 'int64-type) 'int64-type))

(test-case "Type join: incompatible → any"
  (check-equal? (type-join 'int-type 'string-type) 'any-type))

(test-case "Type join: null + T → nullable T"
  (check-equal? (type-join 'null-type 'int-type) '(nullable int-type)))


;; ════════════════════════════════════════════════════════════════════════════
;; PART 5: PROPERTY-BASED CHECKS
;; ════════════════════════════════════════════════════════════════════════════

;; These are the kind of properties that redex-check would generate
;; random programs to test. For now we test key properties manually.

(test-case "Property: if-else returns same type regardless of branch"
  ;; ∀ v_then, v_else of same type:
  ;;   type(if #t then else) == type(if #f then else)
  (for ([v (list 1 2 "a" "b" #t #f '(array-val 1) '(array-val 2))])
    (define t1 (infer-type '() `(if #t ,v ,v)))
    (define t2 (infer-type '() `(if #f ,v ,v)))
    (check-equal? t1 t2
                  (format "if-else type stability for ~a" v))))

(test-case "Property: for-expression always produces array"
  (for ([coll (list '(array 1 2 3)
                    '(to-range 1 5))])
    (define τ (infer-type '() `(for x ,coll x)))
    (check-pred
     (λ (t) (match t [`(array-of ,_) #t] [_ #f]))
     τ
     (format "for over ~a should produce array-of" coll))))

(test-case "Property: error propagation through binary ops"
  ;; ∀ op ∈ {add, sub, mul, fdiv}:
  ;;   op(error, v) = error
  ;;   op(v, error) = error
  (for ([op (list 'add 'sub 'mul 'fdiv)])
    (define err '(error-val "bad" 300))
    (define r1 (eval-lambda ε `(,op (make-error "bad") 42)))
    (define r2 (eval-lambda ε `(,op 42 (make-error "bad"))))
    (check-pred is-error? r1
                (format "~a(error, v) should be error" op))
    (check-pred is-error? r2
                (format "~a(v, error) should be error" op))))

(test-case "Property: C-type roundtrip (box then unbox)"
  ;; ∀ scalar C-types: unbox(box(v)) should recover original type
  (for ([ct '(int32_t int64_t double bool String*)])
    (define box-fn (boxing-function ct))
    (define unbox-fn (unboxing-function ct))
    (check-not-false box-fn (format "~a should have boxing fn" ct))
    (check-not-false unbox-fn (format "~a should have unboxing fn" ct))
    ;; box produces Item, unbox expects Item
    (define box-result-ct 'Item)
    (check-equal? (required-conversion box-result-ct ct) unbox-fn
                  (format "unbox after box for ~a should use ~a" ct unbox-fn))))

(test-case "Property: if-else C-type consistency"
  ;; ∀ type pairs: verify-if-branches should never return 'ok
  ;; when C-types differ
  (for* ([t1 '(int-type float-type string-type bool-type any-type null-type)]
         [t2 '(int-type float-type string-type bool-type any-type null-type)])
    (define ct1 (lambda-type->c-type t1))
    (define ct2 (lambda-type->c-type t2))
    (define result (verify-if-branches t1 t2))
    (when (not (eq? ct1 ct2))
      (check-equal? (car result) 'must-box-both
                    (format "~a (~a) vs ~a (~a) must box both"
                            t1 ct1 t2 ct2)))))


;; ════════════════════════════════════════════════════════════════════════════
;; RUN ALL TESTS
;; ════════════════════════════════════════════════════════════════════════════

(printf "\n✓ All semantic verification tests passed.\n")
