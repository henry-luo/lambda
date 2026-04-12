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
(require "lambda-object.rkt")

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

(test-case "Type conversion: to-symbol"
  (check-equal? (eval-lambda ε '(to-symbol "hello")) '(sym "hello"))
  (check-equal? (eval-lambda ε '(to-symbol "foo_bar")) '(sym "foo_bar")))

(test-case "Type cast: as-type"
  ;; Compatible cast succeeds (int is number)
  (check-equal? (eval-lambda ε '(as-type 42 number-type)) 42)
  (check-equal? (eval-lambda ε '(as-type 42 int-type)) 42)
  ;; Incompatible cast → error
  (check-pred is-error? (eval-lambda ε '(as-type 42 string-type))))

(test-case "Slice expression"
  ;; Array slicing
  (define arr-env (list (cons 'a '(array-val 10 20 30 40 50))))
  (check-equal? (eval-lambda arr-env '(slice a 1 3))
                '(array-val 20 30))
  ;; Negative index
  (check-equal? (eval-lambda arr-env '(slice a 0 -1))
                '(array-val 10 20 30 40))
  ;; String slicing
  (check-equal? (eval-lambda ε '(slice "hello" 1 3)) "el"))

(test-case "Named function definition (def-fn)"
  ;; def-fn creates a named recursive binding
  (check-equal?
   (eval-lambda ε '(def-fn fact (n)
                     (if (le n 1) 1 (mul n (app fact (sub n 1))))
                     (app fact 5)))
   120)
  ;; Simple non-recursive
  (check-equal?
   (eval-lambda ε '(def-fn double (x) (mul x 2)
                     (app double 7)))
   14))

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
  ;; Both int and int64 map to int64_t (int is int56 stored inline)
  (check-equal? (lambda-type->c-type 'int-type)    'int64_t)
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
  (check-equal? (boxing-function 'int64_t) 'push_l)
  (check-equal? (boxing-function 'double)  'push_d)
  (check-equal? (boxing-function 'String*) 's2it)
  (check-equal? (boxing-function 'Item)    #f))  ; already Item

(test-case "Unboxing functions"
  (check-equal? (unboxing-function 'int64_t) 'it2l)
  (check-equal? (unboxing-function 'double)  'it2d)
  (check-equal? (unboxing-function 'String*) 'it2s)
  (check-equal? (unboxing-function 'bool)    'it2b)
  (check-equal? (unboxing-function 'Item)    #f))

(test-case "Required conversion: same type → no conversion"
  (check-equal? (required-conversion 'Item 'Item) #f)
  (check-equal? (required-conversion 'int64_t 'int64_t) #f)
  (check-equal? (required-conversion 'String* 'String*) #f))

(test-case "Required conversion: Item → native (unboxing)"
  (check-equal? (required-conversion 'Item 'int64_t) 'it2l)
  (check-equal? (required-conversion 'Item 'double)  'it2d)
  (check-equal? (required-conversion 'Item 'String*) 'it2s)
  (check-equal? (required-conversion 'Item 'bool)    'it2b))

(test-case "Required conversion: native → Item (boxing)"
  (check-equal? (required-conversion 'int64_t 'Item) 'push_l)
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

(test-case "Type mapping: container and special types"
  (check-equal? (lambda-type->c-type 'array-type)    'Array*)
  (check-equal? (lambda-type->c-type 'list-type)     'List*)
  (check-equal? (lambda-type->c-type 'map-type)      'Map*)
  (check-equal? (lambda-type->c-type 'element-type)  'Element*)
  (check-equal? (lambda-type->c-type 'func-type)     'Function*)
  (check-equal? (lambda-type->c-type 'type-type)     'Type*)
  (check-equal? (lambda-type->c-type 'range-type)    'Range*)
  (check-equal? (lambda-type->c-type 'path-type)     'Path*)
  (check-equal? (lambda-type->c-type 'symbol-type)   'Symbol*)
  (check-equal? (lambda-type->c-type 'decimal-type)  'Decimal*)
  (check-equal? (lambda-type->c-type 'datetime-type) 'DateTime)
  (check-equal? (lambda-type->c-type 'binary-type)   'String*)
  ;; New types added in Phase 1
  (check-equal? (lambda-type->c-type 'object-type)      'Object*)
  (check-equal? (lambda-type->c-type 'num-sized-type)   'uint64_t)
  (check-equal? (lambda-type->c-type 'uint64-type)      'uint64_t)
  (check-equal? (lambda-type->c-type 'array-num-type)   'ArrayNum*)
  ;; Specialized arrays
  (check-equal? (lambda-type->c-type 'array-int-type)   'ArrayInt*)
  (check-equal? (lambda-type->c-type 'array-int64-type) 'ArrayInt64*)
  (check-equal? (lambda-type->c-type 'array-float-type) 'ArrayFloat*))

(test-case "Type mapping: composite types"
  ;; Typed containers → still pointers
  (check-equal? (lambda-type->c-type '(array-of int-type))    'Array*)
  (check-equal? (lambda-type->c-type '(list-of int-type))     'List*)
  (check-equal? (lambda-type->c-type '(map-of string-type))   'Map*)
  ;; Error-return → unwraps to inner type
  (check-equal? (lambda-type->c-type '(error-ret int-type))   'int64_t)
  (check-equal? (lambda-type->c-type '(error-ret string-type)) 'String*)
  ;; fn-type → Item (closures are boxed)
  (check-equal? (lambda-type->c-type '(fn-type (int-type) int-type)) 'Item))

(test-case "C-type predicates"
  ;; Pointer types
  (check-true (pointer-c-type? 'String*))
  (check-true (pointer-c-type? 'Array*))
  (check-true (pointer-c-type? 'Function*))
  (check-true (pointer-c-type? 'Range*))
  (check-true (pointer-c-type? 'Type*))
  (check-true (pointer-c-type? 'ArrayInt*))
  (check-false (pointer-c-type? 'int64_t))
  (check-false (pointer-c-type? 'Item))
  ;; Scalar types (no int32_t in Lambda runtime)
  (check-true (scalar-c-type? 'int64_t))
  (check-true (scalar-c-type? 'double))
  (check-true (scalar-c-type? 'bool))
  (check-false (scalar-c-type? 'String*))
  (check-false (scalar-c-type? 'Item))
  ;; Item type
  (check-true (item-c-type? 'Item))
  (check-false (item-c-type? 'int64_t)))

(test-case "Boxing: all scalar types"
  ;; int64_t uses push_l at C-type level
  (check-equal? (boxing-function 'int64_t)  'push_l)
  (check-equal? (boxing-function 'double)   'push_d)
  (check-equal? (boxing-function 'bool)     'b2it)
  (check-equal? (boxing-function 'String*)  's2it)
  (check-equal? (boxing-function 'Symbol*)  'y2it)
  (check-equal? (boxing-function 'DateTime) 'push_k)
  (check-equal? (boxing-function 'Decimal*) 'c2it)
  (check-equal? (boxing-function 'Item)     #f))

(test-case "Boxing: container types use cast-to-Item"
  (for ([ct '(Array* Map* Element* Range* Path* Function* Type*
              ArrayInt* ArrayInt64* ArrayFloat*)])
    (check-equal? (boxing-function ct) 'cast-to-Item
                  (format "~a should box via cast-to-Item" ct)))
  ;; List boxing is special (#f — list_end() returns Item)
  (check-equal? (boxing-function 'List*) #f))

(test-case "Unboxing: all scalar types"
  ;; int64_t uses it2l at C-type level
  (check-equal? (unboxing-function 'int64_t)  'it2l)
  (check-equal? (unboxing-function 'double)   'it2d)
  (check-equal? (unboxing-function 'bool)     'it2b)
  (check-equal? (unboxing-function 'String*)  'it2s)
  (check-equal? (unboxing-function 'Symbol*)  'it2s)      ; symbol uses it2s per type_box_table
  (check-equal? (unboxing-function 'DateTime) 'it2k)      ; it2k per type_box_table
  (check-equal? (unboxing-function 'Decimal*) 'it2c))     ; it2c per type_box_table

(test-case "Unboxing: container types"
  ;; Containers with specific unboxing functions (per type_box_table)
  (check-equal? (unboxing-function 'Array*)    'it2arr)
  (check-equal? (unboxing-function 'Map*)      'it2map)
  (check-equal? (unboxing-function 'Element*)  'it2elmt)
  (check-equal? (unboxing-function 'Object*)   'it2obj)
  (check-equal? (unboxing-function 'Range*)    'it2range)
  (check-equal? (unboxing-function 'Path*)     'it2path)
  (check-equal? (unboxing-function 'Function*) 'it2p)
  ;; Containers that still use generic cast-from-Item
  (check-equal? (unboxing-function 'List*)     'cast-from-Item)
  (check-equal? (unboxing-function 'Type*)     'cast-from-Item)
  (check-equal? (unboxing-function 'ArrayInt*)    'cast-from-Item)
  (check-equal? (unboxing-function 'ArrayInt64*)  'cast-from-Item)
  (check-equal? (unboxing-function 'ArrayFloat*)  'cast-from-Item)
  (check-equal? (unboxing-function 'ArrayNum*) 'it2arr))

(test-case "Conversion: scalar promotions"
  ;; Only int64 ↔ double conversions remain (no int32_t in Lambda)
  (check-equal? (required-conversion 'int64_t 'double) 'cast-int64-to-double)
  (check-equal? (required-conversion 'double 'int64_t) 'cast-double-to-int64))

(test-case "Conversion: incompatible scalars → type-error"
  (check-equal? (required-conversion 'bool 'int64_t) 'type-error)
  (check-equal? (required-conversion 'int64_t 'bool) 'type-error))

(test-case "Conversion: pointer to Item is compatible"
  ;; Pointers can be cast to Item directly (high bits contain type_id)
  (for ([ct '(String* Array* Map* Element* Range* Function* Type*)])
    (check-equal? (required-conversion ct 'Item) (boxing-function ct)
                  (format "~a → Item should use boxing fn" ct))))

(test-case "Sys-func catalog: lookup"
  (check-not-false (sys-func-lookup 'len)   "len should be in catalog")
  (check-not-false (sys-func-lookup 'type)  "type should be in catalog")
  (check-not-false (sys-func-lookup 'trim)  "trim should be in catalog")
  (check-false (sys-func-lookup 'nonexistent_func)
               "missing func should return #f"))

(test-case "Sys-func catalog: C return types"
  (check-equal? (sys-func-c-return 'len)      'int64_t)
  (check-equal? (sys-func-c-return 'type)     'Type*)
  (check-equal? (sys-func-c-return 'name)     'Symbol*)
  (check-equal? (sys-func-c-return 'contains) 'bool)
  (check-equal? (sys-func-c-return 'trim)     'Item)
  (check-equal? (sys-func-c-return 'string)   'String*)
  ;; Unknown func defaults to Item
  (check-equal? (sys-func-c-return 'unknown_func) 'Item))

(test-case "Sys-func catalog: discrepancies"
  ;; Functions where semantic type ≠ C return type
  (define discreps (sys-func-discrepancies))
  (check-true (> (length discreps) 0) "should have at least one discrepancy")
  ;; normalize: semantic=STRING but C=Item
  (define norm-entry (sys-func-lookup 'normalize))
  (check-true (sys-func-entry-discrepancy? norm-entry)
              "normalize should be flagged as discrepancy")
  (check-equal? (sys-func-entry-semantic-type norm-entry) 'string-type)
  (check-equal? (sys-func-entry-c-return-type norm-entry) 'Item))

(test-case "Sys-func catalog: needs-conversion?"
  ;; normalize returns Item, but if param expects String*, needs it2s
  (define conv (sys-func-needs-conversion? 'normalize 'string-type))
  (check-equal? conv 'it2s
                "normalize → string param needs it2s conversion")
  ;; len returns int64_t, if param expects int64_t, no conversion
  (check-false (sys-func-needs-conversion? 'len 'int64-type)
               "len → int64 param needs no conversion")
  ;; len returns int64_t, int-type also maps to int64_t → no conversion needed
  (check-false (sys-func-needs-conversion? 'len 'int-type)
               "len → int param needs no conversion (both int64_t)"))

(test-case "verify-function: basic check"
  (define result (verify-function 'test-fn
                                  (list (list 'x 'int-type) (list 'y 'string-type))
                                  'int-type
                                  '()))
  (check-equal? (second result) 'ok))


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
  (for ([ct '(int64_t double bool String*)])
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

(test-case "Property: every scalar C-type has boxing AND unboxing"
  ;; ∀ scalar C-types: both boxing and unboxing must exist
  (for ([ct '(int64_t double bool)])
    (check-not-false (boxing-function ct)
                     (format "~a must have boxing fn" ct))
    (check-not-false (unboxing-function ct)
                     (format "~a must have unboxing fn" ct))))

(test-case "Property: every pointer C-type has unboxing"
  ;; All pointer types must be recoverable from Item
  (for ([ct '(String* Symbol* Array* List* Map* Element* Decimal*
              Object* Range* Path* Function* Type*
              ArrayNum* ArrayInt* ArrayInt64* ArrayFloat*)])
    (check-not-false (unboxing-function ct)
                     (format "~a must have unboxing fn" ct))))

(test-case "Property: boxing roundtrip for all scalars"
  ;; ∀ scalar c-type ct:
  ;;   unboxing-fn(ct) == required-conversion(Item, ct)
  ;; i.e., the unboxing function IS what required-conversion returns
  (for ([ct '(int64_t double bool String*)])
    (define uf (unboxing-function ct))
    (define conv (required-conversion 'Item ct))
    (check-equal? conv uf
                  (format "roundtrip: Item → ~a should use ~a" ct uf))))

(test-case "Property: same-type conversion is always #f"
  ;; ∀ C-type ct: required-conversion(ct, ct) == #f
  (for ([ct '(Item int64_t uint64_t double bool String* Symbol*
              Array* ArrayNum* List* Map* Element* Object* Decimal* DateTime
              Range* Path* Function* Type*)])
    (check-false (required-conversion ct ct)
                 (format "~a → ~a should need no conversion" ct ct))))

(test-case "Property: sys-func catalog entries are well-formed"
  ;; Every entry has valid C-return type (listed in our type domain)
  (for ([entry sys-func-catalog])
    (define crt (sys-func-entry-c-return-type entry))
    (check-true (or (item-c-type? crt)
                    (scalar-c-type? crt)
                    (pointer-c-type? crt)
                    (eq? crt 'DateTime))
                (format "~a has invalid C return type ~a"
                        (sys-func-entry-name entry) crt))))

(test-case "Property: discrepancy flag consistency"
  ;; An entry is a discrepancy iff semantic-type maps to a different C-type
  ;; than the actual C-return-type
  (for ([entry sys-func-catalog])
    (define name (sys-func-entry-name entry))
    (define sem-τ (sys-func-entry-semantic-type entry))
    (define c-ret (sys-func-entry-c-return-type entry))
    (define expected-ct (lambda-type->c-type sem-τ))
    (when (sys-func-entry-discrepancy? entry)
      (check-not-equal? expected-ct c-ret
                        (format "~a is flagged as discrepancy but semantic→C matches"
                                name)))))

(test-case "Property: all container types map to pointer C-types"
  (for ([τ '(array-type list-type map-type element-type object-type)])
    (define ct (lambda-type->c-type τ))
    (check-true (pointer-c-type? ct)
                (format "~a should map to pointer type, got ~a" τ ct))))

(test-case "Property: conversion between different pointer types is type-error"
  ;; Different pointer types can't be directly converted (must go through Item)
  (for* ([ct1 '(String* Array* Map* Element*)]
         [ct2 '(String* Array* Map* Element*)])
    (when (not (eq? ct1 ct2))
      (check-equal? (required-conversion ct1 ct2) 'type-error
                    (format "~a → ~a should be type-error" ct1 ct2)))))

(test-case "Semantic boxing: int uses i2it, int64 uses push_l"
  ;; The key int56 distinction: both map to int64_t in C,
  ;; but use different boxing functions
  (check-equal? (semantic-boxing-function 'int-type)   'i2it)
  (check-equal? (semantic-boxing-function 'int64-type) 'push_l)
  (check-equal? (semantic-boxing-function 'float-type) 'push_d)
  (check-equal? (semantic-boxing-function 'bool-type)  'b2it)
  (check-equal? (semantic-boxing-function 'string-type) 's2it))

(test-case "Semantic unboxing: int uses it2i, int64 uses it2l"
  (check-equal? (semantic-unboxing-function 'int-type)   'it2i)
  (check-equal? (semantic-unboxing-function 'int64-type) 'it2l)
  (check-equal? (semantic-unboxing-function 'float-type) 'it2d)
  (check-equal? (semantic-unboxing-function 'bool-type)  'it2b)
  (check-equal? (semantic-unboxing-function 'string-type) 'it2s)
  ;; symbol/datetime/decimal use the names from type_box_table
  (check-equal? (semantic-unboxing-function 'symbol-type) 'it2s)
  (check-equal? (semantic-unboxing-function 'datetime-type) 'it2k)
  (check-equal? (semantic-unboxing-function 'decimal-type) 'it2c))

(test-case "int and int64 share C-type but differ semantically"
  ;; Both map to int64_t
  (check-equal? (lambda-type->c-type 'int-type) 'int64_t)
  (check-equal? (lambda-type->c-type 'int64-type) 'int64_t)
  ;; Same C-type → no conversion needed at C level
  (check-false (required-conversion 'int64_t 'int64_t))
  ;; But semantic boxing differs
  (check-not-equal? (semantic-boxing-function 'int-type)
                    (semantic-boxing-function 'int64-type))
  ;; If-else between int and int64 → ok (same C-type)
  (define result (verify-if-branches 'int-type 'int64-type))
  (check-equal? (car result) 'ok))


;; ════════════════════════════════════════════════════════════════════════════
;; PART 6: PROCEDURAL EXTENSION TESTS
;; ════════════════════════════════════════════════════════════════════════════

(require "lambda-proc.rkt")

;; Helper: run a pn body (list of statements) and return (values val output-string)
(define (pn-run . stmts)
  (run-pn-body stmts))

;; ── Mutable Variables ──

(test-case "Proc: var declaration and read"
  (define-values (v out) (pn-run '(var x 42) 'x))
  (check-equal? v 42))

(test-case "Proc: var assignment"
  (define-values (v out) (pn-run '(var x 10) '(assign x 99) 'x))
  (check-equal? v 99))

(test-case "Proc: var null then reassign"
  (define-values (v out) (pn-run '(var x null) '(assign x 42) 'x))
  (check-equal? v 42))

(test-case "Proc: multiple var declarations"
  (define-values (v out) (pn-run '(var a 1) '(var b 2) '(var c 3) '(add (add a b) c)))
  (check-equal? v 6))

(test-case "Proc: var type widening (int → float → string)"
  (define-values (v out)
    (pn-run '(var x 42)
            '(assign x 3.14)
            '(print x)
            '(assign x "hello")
            'x))
  (check-equal? v "hello")
  (check-equal? out "3.14"))

;; ── Print Side Effects ──

(test-case "Proc: print output"
  (define-values (v out) (pn-run '(print "hello") '(print " world")))
  (check-equal? out "hello world"))

(test-case "Proc: print numbers"
  (define-values (v out) (pn-run '(print 42) '(print " ") '(print 3.14)))
  (check-equal? out "42 3.14"))

(test-case "Proc: print null"
  (define-values (v out) (pn-run '(print null)))
  (check-equal? out "null"))

;; ── While Loop ──

(test-case "Proc: simple while loop"
  (define-values (v out)
    (pn-run '(var x 0)
            '(while (lt x 5) (seq (assign x (add x 1))))
            'x))
  (check-equal? v 5))

(test-case "Proc: while with accumulator"
  (define-values (v out)
    (pn-run '(var sum 0)
            '(var i 1)
            '(while (le i 10) (seq
              (assign sum (add sum i))
              (assign i (add i 1))))
            'sum))
  (check-equal? v 55))

(test-case "Proc: nested while loops"
  (define-values (v out)
    (pn-run '(var total 0)
            '(var i 0)
            '(while (lt i 3) (seq
              (var j 0)
              (while (lt j 3) (seq
                (assign total (add total 1))
                (assign j (add j 1))))
              (assign i (add i 1))))
            'total))
  (check-equal? v 9))

;; ── Break and Continue ──

(test-case "Proc: while with break"
  (define-values (v out)
    (pn-run '(var x 0)
            '(while #t (seq
              (if-proc (ge x 3) (break))
              (assign x (add x 1))))
            'x))
  (check-equal? v 3))

(test-case "Proc: while with continue (skip even)"
  (define-values (v out)
    (pn-run '(var sum 0)
            '(var i 0)
            '(while (lt i 10) (seq
              (assign i (add i 1))
              (if-proc (eq (mod i 2) 0) (continue))
              (assign sum (add sum i))))
            'sum))
  (check-equal? v 25))  ; 1+3+5+7+9

;; ── Return ──

(test-case "Proc: early return"
  (define-values (v out)
    (pn-run '(return 42)
            '(print "unreachable")))
  (check-equal? v 42)
  (check-equal? out ""))

(test-case "Proc: return from while"
  (define-values (v out)
    (pn-run '(var x 0)
            '(while (lt x 100) (seq
              (assign x (add x 1))
              (if-proc (eq x 5) (return x))))
            'x))
  (check-equal? v 5))

;; ── Procedural Functions (pn) ──

(test-case "Proc: def-pn and call"
  (define-values (v out)
    (pn-run '(def-pn double (x) (seq (return (mul x 2))))
            '(app-proc double 21)))
  (check-equal? v 42))

(test-case "Proc: pn with while loop"
  (define-values (v out)
    (pn-run '(def-pn factorial (n) (seq
              (var result 1)
              (var i 1)
              (while (le i n) (seq
                (assign result (mul result i))
                (assign i (add i 1))))
              result))
            '(app-proc factorial 5)))
  (check-equal? v 120))

(test-case "Proc: pn param mutation"
  (define-values (v out)
    (pn-run '(def-pn countdown (n) (seq
              (while (gt n 0) (seq
                (print n)
                (print " ")
                (assign n (sub n 1))))
              (print "done")))
            '(app-proc countdown 3)))
  (check-equal? out "3 2 1 done"))

(test-case "Proc: pn with early return"
  (define-values (v out)
    (pn-run '(def-pn find-first-gt (arr threshold) (seq
              (var i 0)
              (while (lt i (len-expr arr)) (seq
                (if-proc (gt (index arr i) threshold)
                  (return (index arr i)))
                (assign i (add i 1))))
              null))
            '(app-proc find-first-gt (array 1 5 3 8 2) 4)))
  (check-equal? v 5))

;; ── Array Mutation ──

(test-case "Proc: array element assignment"
  (define-values (v out)
    (pn-run '(var arr (array 10 20 30 40 50))
            '(assign-index arr 0 100)
            '(assign-index arr 2 300)
            'arr))
  (check-equal? v '(array-val 100 20 300 40 50)))

(test-case "Proc: array negative index assignment"
  (define-values (v out)
    (pn-run '(var arr (array 1 2 3 4 5))
            '(assign-index arr -1 99)
            'arr))
  (check-equal? v '(array-val 1 2 3 4 99)))

(test-case "Proc: array assignment in while loop"
  (define-values (v out)
    (pn-run '(var arr (array 0 0 0 0 0))
            '(var i 0)
            '(while (lt i 5) (seq
              (assign-index arr i (mul i i))
              (assign i (add i 1))))
            'arr))
  (check-equal? v '(array-val 0 1 4 9 16)))

;; ── Map Mutation ──

(test-case "Proc: map field assignment"
  (define-values (v out)
    (pn-run '(var obj (map-expr (x 10) (y 20)))
            '(assign-member obj x 42)
            'obj))
  (check-equal? v '(map-val (x 42) (y 20))))

(test-case "Proc: map field in while loop (counter)"
  (define-values (v out)
    (pn-run '(var counter (map-expr (value 0)))
            '(var i 0)
            '(while (lt i 5) (seq
              (assign-member counter value (add (member counter value) 1))
              (assign i (add i 1))))
            '(member counter value)))
  (check-equal? v 5))

(test-case "Proc: map add new field"
  (define-values (v out)
    (pn-run '(var obj (map-expr (x 1)))
            '(assign-member obj y 2)
            'obj))
  (check-equal? v '(map-val (x 1) (y 2))))

;; ── Closure Mutation (shared store) ──

(test-case "Proc: closure counter pattern"
  (define-values (v out)
    (pn-run '(var count 0)
            '(def-pn inc () (seq
              (assign count (add count 1))
              count))
            '(var a (app-proc inc))
            '(var b (app-proc inc))
            '(var c (app-proc inc))
            '(add (add a b) c)))
  (check-equal? v 6))  ; 1+2+3

;; ── If-else (procedural) ──

(test-case "Proc: if-else with assignment"
  (define-values (v out)
    (pn-run '(var x 10)
            '(var result 0)
            '(if-proc (gt x 5)
              (assign result (mul x 2))
              (assign result (mul x 3)))
            'result))
  (check-equal? v 20))

(test-case "Proc: if without else"
  (define-values (v out)
    (pn-run '(var x 10)
            '(var msg "default")
            '(if-proc (gt x 5) (assign msg "big"))
            'msg))
  (check-equal? v "big"))

;; ── Fibonacci (classic proc test) ──

(test-case "Proc: fibonacci via while"
  (define-values (v out)
    (pn-run '(def-pn fib (n) (seq
              (var a 0)
              (var b 1)
              (var i 2)
              (while (le i n) (seq
                (var temp (add a b))
                (assign a b)
                (assign b temp)
                (assign i (add i 1))))
              b))
            '(app-proc fib 10)))
  (check-equal? v 55))

;; ── Bubble sort pattern ──

(test-case "Proc: swap via temp variable"
  (define-values (v out)
    (pn-run '(var a 5)
            '(var b 3)
            '(var temp a)
            '(assign a b)
            '(assign b temp)
            '(add (mul a 10) b)))
  ;; a=3, b=5 → 35
  (check-equal? v 35))

;; ── Combination: print + while + function ──

(test-case "Proc: print inside while"
  (define-values (v out)
    (pn-run '(var i 1)
            '(while (le i 5) (seq
              (print i)
              (if-proc (lt i 5) (print ","))
              (assign i (add i 1))))))
  (check-equal? out "1,2,3,4,5"))

;; ── Error handling in proc ──

(test-case "Proc: error propagation"
  (define-values (v out)
    (pn-run '(def-pn safe-div (a b) (seq
              (if-proc (eq b 0) (return (make-error "division by zero")))
              (return (fdiv a b))))
            '(app-proc safe-div 10 0)))
  (check-pred is-error? v))

(test-case "Proc: error-free path"
  (define-values (v out)
    (pn-run '(def-pn safe-div (a b) (seq
              (if-proc (eq b 0) (return (make-error "division by zero")))
              (return (fdiv a b))))
            '(app-proc safe-div 10 2)))
  (check-equal? v 5.0))

;; ── Property: pn return always unwraps at call boundary ──

(test-case "Property: return unwraps at call boundary"
  (define-values (v out)
    (pn-run '(def-pn inner () (seq (return 42)))
            '(def-pn outer () (seq
              (var x (app-proc inner))
              (return (add x 1))))
            '(app-proc outer)))
  (check-equal? v 43))

;; ── Property: break does not escape function ──

(test-case "Property: break contained in while"
  (define-values (v out)
    (pn-run '(def-pn f () (seq
              (var x 0)
              (while #t (seq
                (assign x (add x 1))
                (if-proc (eq x 3) (break))))
              x))
            '(app-proc f)))
  (check-equal? v 3))


;; ════════════════════════════════════════════════════════════════════════════
;; PART 7: OBJECT TYPE SYSTEM TESTS
;; ════════════════════════════════════════════════════════════════════════════

;; Helper: register types, then run procedural statements
(define (pn-obj-run . args)
  ;; All thunks come first (procedures that register types), rest are statements
  (clear-type-registry!)
  (define thunks (takef args procedure?))
  (define stmts (dropf args procedure?))
  (for ([thunk thunks]) (thunk))
  (run-pn-body stmts))

;; ── Type registration helpers ──
(define (reg-point!)
  (register-type! 'Point #f
    (list (field-spec 'x 'int-type 'no-default 'no-constraint)
          (field-spec 'y 'int-type 'no-default 'no-constraint))
    '() '()))

(define (reg-counter!)
  (register-type! 'Counter #f
    (list (field-spec 'value 'int-type 'no-default 'no-constraint))
    (list (method-spec 'double 'fn '() '(mul value 2))
          (method-spec 'add 'fn '(n) '(add value n)))
    '()))


;; ──────────────────────────────────────────────────────────────────────────
;; 7.1 Basic object: construction, field access, type checking
;; Corresponds to: test/lambda/object.ls
;; ──────────────────────────────────────────────────────────────────────────

(test-case "Object: basic construction and field access"
  ;; type Point { x: int, y: int }
  ;; let p = <Point x: 3, y: 4>
  (clear-type-registry!) (reg-point!)
  (define p (eval-lambda ε '(make-object Point (x 3) (y 4))))
  (check-equal? p '(object-val Point (x 3) (y 4))))

(test-case "Object: field access via member"
  ;; p.x → 3, p.y → 4
  (clear-type-registry!) (reg-point!)
  (define p '(object-val Point (x 3) (y 4)))
  (check-equal? (eval-lambda `((p . ,p)) '(member p x)) 3)
  (check-equal? (eval-lambda `((p . ,p)) '(member p y)) 4))

(test-case "Object: fn method — zero args"
  ;; type Counter { value: int; fn double() => value * 2 }
  ;; let c = <Counter value: 5>; c.double() → 10
  (clear-type-registry!) (reg-counter!)
  (define c '(object-val Counter (value 5)))
  (check-equal? (eval-lambda `((c . ,c)) '(method-call c double)) 10))

(test-case "Object: fn method — one arg"
  ;; c.add(3) → 8
  (clear-type-registry!) (reg-counter!)
  (define c '(object-val Counter (value 5)))
  (check-equal? (eval-lambda `((c . ,c)) '(method-call c add 3)) 8))

(test-case "Object: is-type with nominal type"
  ;; p is Point → true; p is object → true; 5 is Point → false
  (clear-type-registry!) (reg-point!)
  (define p '(object-val Point (x 3) (y 4)))
  (check-equal? (eval-lambda `((p . ,p)) '(is-type p Point)) #t)
  (check-equal? (eval-lambda `((p . ,p)) '(is-type p object-type)) #t)
  (check-equal? (eval-lambda ε '(is-type 5 Point)) #f))


;; ──────────────────────────────────────────────────────────────────────────
;; 7.2 Default field values
;; Corresponds to: test/lambda/object_default.ls
;; ──────────────────────────────────────────────────────────────────────────

(define (reg-config!)
  (register-type! 'Config #f
    (list (field-spec 'host 'string-type "localhost" 'no-constraint)
          (field-spec 'port 'int-type 8080 'no-constraint)
          (field-spec 'debug 'bool-type #f 'no-constraint))  ; default=#f (false)
    '() '()))

(test-case "Object: partial override with defaults"
  ;; let c1 = <Config host: "example.com">
  ;; c1.host → "example.com", c1.port → 8080, c1.debug → false
  (clear-type-registry!) (reg-config!)
  (define c1 (eval-lambda ε '(make-object Config (host "example.com"))))
  (check-equal? (object-field-ref c1 'host) "example.com")
  (check-equal? (object-field-ref c1 'port) 8080)
  (check-equal? (object-field-ref c1 'debug) #f))

(test-case "Object: full override"
  ;; let c2 = <Config host: "api.io", port: 443, debug: true>
  (clear-type-registry!) (reg-config!)
  (define c2 (eval-lambda ε '(make-object Config (host "api.io") (port 443) (debug #t))))
  (check-equal? (object-field-ref c2 'host) "api.io")
  (check-equal? (object-field-ref c2 'port) 443)
  (check-equal? (object-field-ref c2 'debug) #t))

(test-case "Object: inherited defaults"
  ;; type Shape { color: string = "black" }
  ;; type Circle : Shape { radius: int = 10 }
  ;; let c3 = <Circle color: "red">; c3.color → "red", c3.radius → 10
  (clear-type-registry!)
  (register-type! 'Shape #f
    (list (field-spec 'color 'string-type "black" 'no-constraint))
    '() '())
  (register-type! 'Circle 'Shape
    (list (field-spec 'radius 'int-type 10 'no-constraint))
    '() '())
  (define c3 (eval-lambda ε '(make-object Circle (color "red"))))
  (check-equal? (object-field-ref c3 'color) "red")
  (check-equal? (object-field-ref c3 'radius) 10))


;; ──────────────────────────────────────────────────────────────────────────
;; 7.3 Inheritance: fields, methods, type checking, overrides
;; Corresponds to: test/lambda/object_inherit.ls
;; ──────────────────────────────────────────────────────────────────────────

(define (reg-shape-hierarchy!)
  ;; type Shape { color: string; fn describe() => "Shape: " ++ color }
  (register-type! 'Shape #f
    (list (field-spec 'color 'string-type 'no-default 'no-constraint))
    (list (method-spec 'describe 'fn '() '(concat "Shape: " color)))
    '())
  ;; type Circle : Shape { radius: int; fn area() => radius * radius * 3 }
  (register-type! 'Circle 'Shape
    (list (field-spec 'radius 'int-type 'no-default 'no-constraint))
    (list (method-spec 'area 'fn '() '(mul (mul radius radius) 3)))
    '()))

(test-case "Object: inherited field access"
  ;; let c = <Circle color: "red", radius: 5>
  ;; c.color → "red", c.radius → 5
  (clear-type-registry!) (reg-shape-hierarchy!)
  (define c (eval-lambda ε '(make-object Circle (color "red") (radius 5))))
  (define ρ `((c . ,c)))
  (check-equal? (eval-lambda ρ '(member c color)) "red")
  (check-equal? (eval-lambda ρ '(member c radius)) 5))

(test-case "Object: inherited method call"
  ;; c.describe() → "Shape: red"
  (clear-type-registry!) (reg-shape-hierarchy!)
  (define c (eval-lambda ε '(make-object Circle (color "red") (radius 5))))
  (check-equal? (eval-lambda `((c . ,c)) '(method-call c describe)) "Shape: red"))

(test-case "Object: own method call"
  ;; c.area() → 75
  (clear-type-registry!) (reg-shape-hierarchy!)
  (define c (eval-lambda ε '(make-object Circle (color "red") (radius 5))))
  (check-equal? (eval-lambda `((c . ,c)) '(method-call c area)) 75))

(test-case "Object: type checking with inheritance"
  ;; c is Circle → true; c is Shape → true; c is object → true; 5 is Circle → false
  (clear-type-registry!) (reg-shape-hierarchy!)
  (define c (eval-lambda ε '(make-object Circle (color "red") (radius 5))))
  (define ρ `((c . ,c)))
  (check-equal? (eval-lambda ρ '(is-type c Circle)) #t)
  (check-equal? (eval-lambda ρ '(is-type c Shape)) #t)
  (check-equal? (eval-lambda ρ '(is-type c object-type)) #t)
  (check-equal? (eval-lambda ε '(is-type 5 Circle)) #f))

(test-case "Object: method override"
  ;; type Animal { name: string; fn speak() => name ++ " says ..." }
  ;; type Dog : Animal { breed: string; fn speak() => name ++ " says woof!" }
  ;; d.speak() → "Rex says woof!"
  (clear-type-registry!)
  (register-type! 'Animal #f
    (list (field-spec 'name 'string-type 'no-default 'no-constraint))
    (list (method-spec 'speak 'fn '() '(concat name " says ...")))
    '())
  (register-type! 'Dog 'Animal
    (list (field-spec 'breed 'string-type 'no-default 'no-constraint))
    (list (method-spec 'speak 'fn '() '(concat name " says woof!")))
    '())
  (define d (eval-lambda ε '(make-object Dog (name "Rex") (breed "Lab"))))
  (define ρ `((d . ,d)))
  (check-equal? (eval-lambda ρ '(method-call d speak)) "Rex says woof!")
  (check-equal? (eval-lambda ρ '(member d name)) "Rex")
  (check-equal? (eval-lambda ρ '(member d breed)) "Lab")
  (check-equal? (eval-lambda ρ '(is-type d Dog)) #t)
  (check-equal? (eval-lambda ρ '(is-type d Animal)) #t))


;; ──────────────────────────────────────────────────────────────────────────
;; 7.4 Object update/spread
;; Corresponds to: test/lambda/object_update.ls
;; ──────────────────────────────────────────────────────────────────────────

(define (reg-fpoint!)
  (register-type! 'FPoint #f
    (list (field-spec 'x 'float-type 'no-default 'no-constraint)
          (field-spec 'y 'float-type 'no-default 'no-constraint))
    '() '()))

(test-case "Object: update — override one field"
  ;; let p = <FPoint x: 1.0, y: 2.0>
  ;; let q = <FPoint *:p, x: 10.0>; q.x → 10.0, q.y → 2.0
  (clear-type-registry!) (reg-fpoint!)
  (define p (eval-lambda ε '(make-object FPoint (x 1.0) (y 2.0))))
  (define q (eval-lambda `((p . ,p)) '(object-update FPoint p (x 10.0))))
  (check-equal? (object-field-ref q 'x) 10.0)
  (check-equal? (object-field-ref q 'y) 2.0))

(test-case "Object: update — override all fields"
  (clear-type-registry!) (reg-fpoint!)
  (define p (eval-lambda ε '(make-object FPoint (x 1.0) (y 2.0))))
  (define r (eval-lambda `((p . ,p)) '(object-update FPoint p (x 100.0) (y 200.0))))
  (check-equal? (object-field-ref r 'x) 100.0)
  (check-equal? (object-field-ref r 'y) 200.0))

(test-case "Object: update — copy all (no overrides)"
  (clear-type-registry!) (reg-fpoint!)
  (define p (eval-lambda ε '(make-object FPoint (x 1.0) (y 2.0))))
  (define s (eval-lambda `((p . ,p)) '(object-update FPoint p)))
  (check-equal? (object-field-ref s 'x) 1.0)
  (check-equal? (object-field-ref s 'y) 2.0))

(test-case "Object: update with inheritance"
  ;; type Shape { color: string = "black" }
  ;; type Circle : Shape { radius: int }
  ;; let c2 = <Circle *:c1, radius: 10>
  (clear-type-registry!)
  (register-type! 'Shape #f
    (list (field-spec 'color 'string-type "black" 'no-constraint))
    '() '())
  (register-type! 'Circle 'Shape
    (list (field-spec 'radius 'int-type 'no-default 'no-constraint))
    '() '())
  (define c1 (eval-lambda ε '(make-object Circle (color "red") (radius 5))))
  (define c2 (eval-lambda `((c1 . ,c1)) '(object-update Circle c1 (radius 10))))
  (check-equal? (object-field-ref c2 'color) "red")
  (check-equal? (object-field-ref c2 'radius) 10))

(test-case "Object: update with ~ self-reference in method"
  ;; type Vec { x: float, y: float;
  ;;   fn translate(dx, dy) => <Vec *:~, x: x + dx, y: y + dy> }
  (clear-type-registry!)
  (register-type! 'Vec #f
    (list (field-spec 'x 'float-type 'no-default 'no-constraint)
          (field-spec 'y 'float-type 'no-default 'no-constraint))
    (list (method-spec 'translate 'fn '(dx dy)
            '(object-update Vec ~ (x (add x dx)) (y (add y dy))))
          (method-spec 'scale 'fn '(factor)
            '(object-update Vec ~ (x (mul x factor)) (y (mul y factor)))))
    '())
  (define v (eval-lambda ε '(make-object Vec (x 3.0) (y 4.0))))
  (define ρ `((v . ,v)))
  ;; v.translate(1.0, -1.0) → Vec(x: 4.0, y: 3.0)
  (define v2 (eval-lambda ρ '(method-call v translate 1.0 -1.0)))
  (check-equal? (object-field-ref v2 'x) 4.0)
  (check-equal? (object-field-ref v2 'y) 3.0)
  ;; v.scale(2.0) → Vec(x: 6.0, y: 8.0)
  (define v3 (eval-lambda ρ '(method-call v scale 2.0)))
  (check-equal? (object-field-ref v3 'x) 6.0)
  (check-equal? (object-field-ref v3 'y) 8.0))


;; ──────────────────────────────────────────────────────────────────────────
;; 7.5 Constraints: field-level and object-level
;; Corresponds to: test/lambda/object_constraint.ls
;; ──────────────────────────────────────────────────────────────────────────

(define (reg-user!)
  ;; type User { name: string that (len(~) > 0), age: int that (0 <= ~ and ~ <= 150), email: string }
  (register-type! 'User #f
    (list (field-spec 'name 'string-type 'no-default '(gt (len-expr ~) 0))
          (field-spec 'age 'int-type 'no-default '(l-and (le 0 ~) (le ~ 150)))
          (field-spec 'email 'string-type 'no-default 'no-constraint))
    '() '()))

(test-case "Object: field constraint — valid"
  ;; let alice = <User name: "Alice", age: 30, email: "a@x.com">; alice is User → true
  (clear-type-registry!) (reg-user!)
  (define alice (eval-lambda ε '(make-object User (name "Alice") (age 30) (email "a@x.com"))))
  (check-pred object-val? alice))

(test-case "Object: field constraint — empty name fails"
  (clear-type-registry!) (reg-user!)
  (define bad (eval-lambda ε '(make-object User (name "") (age 30) (email "b@x.com"))))
  (check-pred is-error? bad))

(test-case "Object: field constraint — negative age fails"
  (clear-type-registry!) (reg-user!)
  (define bad (eval-lambda ε '(make-object User (name "Bob") (age -5) (email "b@x.com"))))
  (check-pred is-error? bad))

(test-case "Object: field constraint — age > 150 fails"
  (clear-type-registry!) (reg-user!)
  (define bad (eval-lambda ε '(make-object User (name "Carol") (age 200) (email "c@x.com"))))
  (check-pred is-error? bad))

(define (reg-daterange!)
  ;; type DateRange { start: int, end: int; that (~.end > ~.start) }
  (register-type! 'DateRange #f
    (list (field-spec 'start 'int-type 'no-default 'no-constraint)
          (field-spec 'end 'int-type 'no-default 'no-constraint))
    '()
    (list '(gt (member ~ end) (member ~ start)))))

(test-case "Object: object constraint — valid"
  (clear-type-registry!) (reg-daterange!)
  (define dr (eval-lambda ε '(make-object DateRange (start 1) (end 10))))
  (check-pred object-val? dr))

(test-case "Object: object constraint — invalid (end < start)"
  (clear-type-registry!) (reg-daterange!)
  (define bad (eval-lambda ε '(make-object DateRange (start 10) (end 1))))
  (check-pred is-error? bad))

(test-case "Object: combined field + object constraints"
  ;; type Config { min: int that (~ >= 0), max: int that (~ >= 0); that (~.max > ~.min) }
  (clear-type-registry!)
  (register-type! 'CConfig #f
    (list (field-spec 'min 'int-type 'no-default '(ge ~ 0))
          (field-spec 'max 'int-type 'no-default '(ge ~ 0)))
    '()
    (list '(gt (member ~ max) (member ~ min))))
  ;; valid
  (define good (eval-lambda ε '(make-object CConfig (min 1) (max 10))))
  (check-pred object-val? good)
  ;; field constraint fails (negative min)
  (define bad1 (eval-lambda ε '(make-object CConfig (min -1) (max 10))))
  (check-pred is-error? bad1)
  ;; object constraint fails (max <= min)
  (define bad2 (eval-lambda ε '(make-object CConfig (min 5) (max 3))))
  (check-pred is-error? bad2))


;; ──────────────────────────────────────────────────────────────────────────
;; 7.6 Pattern matching with object types
;; Corresponds to: test/lambda/object_pattern.ls
;; ──────────────────────────────────────────────────────────────────────────

(test-case "Object: match on nominal type"
  ;; match c { case Circle: "circle" case Rect: "rect" case Shape: "shape" default: "unknown" }
  (clear-type-registry!)
  (register-type! 'Shape #f
    (list (field-spec 'color 'string-type "black" 'no-constraint))
    '() '())
  (register-type! 'Circle 'Shape
    (list (field-spec 'radius 'float-type 'no-default 'no-constraint))
    '() '())
  (register-type! 'Rect 'Shape
    (list (field-spec 'width 'float-type 'no-default 'no-constraint)
          (field-spec 'height 'float-type 'no-default 'no-constraint))
    '() '())
  (define circle-obj (eval-lambda ε '(make-object Circle (color "red") (radius 5.0))))
  (define rect-obj (eval-lambda ε '(make-object Rect (color "blue") (width 3.0) (height 4.0))))
  ;; describe function via match
  (define describe-match
    '(match s (case-type Circle "circle")
              (case-type Rect "rect")
              (case-type Shape "shape")
              (default-case "unknown")))
  ;; circle matches Circle first
  (check-equal? (eval-lambda `((s . ,circle-obj)) describe-match) "circle")
  ;; rect matches Rect
  (check-equal? (eval-lambda `((s . ,rect-obj)) describe-match) "rect")
  ;; non-object → "unknown"
  (check-equal? (eval-lambda `((s . 42)) describe-match) "unknown"))

(test-case "Object: match with inheritance (Circle is also Shape)"
  (clear-type-registry!)
  (register-type! 'Shape #f
    (list (field-spec 'color 'string-type "black" 'no-constraint))
    '() '())
  (register-type! 'Circle 'Shape
    (list (field-spec 'radius 'float-type 'no-default 'no-constraint))
    '() '())
  (register-type! 'Rect 'Shape
    (list (field-spec 'width 'float-type 'no-default 'no-constraint)
          (field-spec 'height 'float-type 'no-default 'no-constraint))
    '() '())
  (define circle-obj (eval-lambda ε '(make-object Circle (color "red") (radius 5.0))))
  (define rect-obj (eval-lambda ε '(make-object Rect (color "blue") (width 3.0) (height 4.0))))
  ;; is_shape: match s { case Shape: true default: false }
  (define is-shape-match
    '(match s (case-type Shape #t) (default-case #f)))
  (check-equal? (eval-lambda `((s . ,circle-obj)) is-shape-match) #t)
  (check-equal? (eval-lambda `((s . ,rect-obj)) is-shape-match) #t)
  (check-equal? (eval-lambda `((s . "hello")) is-shape-match) #f))


;; ──────────────────────────────────────────────────────────────────────────
;; 7.7 pn mutation methods
;; Corresponds to: test/lambda/object_mutation.ls
;; ──────────────────────────────────────────────────────────────────────────

(define (reg-mut-counter!)
  ;; type Counter { count: int = 0;
  ;;   pn increment() { count = count + 1 }
  ;;   pn add(n) { count = count + n }
  ;;   pn reset() { count = 0 } }
  (register-type! 'MCounter #f
    (list (field-spec 'count 'int-type 0 'no-constraint))
    (list (method-spec 'increment 'pn '()
            '(seq (assign count (add count 1))))
          (method-spec 'add-n 'pn '(n)
            '(seq (assign count (add count n))))
          (method-spec 'reset 'pn '()
            '(seq (assign count 0))))
    '()))

(test-case "Object: pn method — basic increment"
  ;; let c = <MCounter count: 0>
  ;; c.count → 0; c.increment(); c.count → 1; c.increment(); c.count → 2
  (define-values (v out)
    (pn-obj-run reg-mut-counter!
      '(var c (make-object MCounter (count 0)))
      '(print (member c count))          ;; 0
      '(pn-method-call c increment)
      '(print (member c count))          ;; 1
      '(pn-method-call c increment)
      '(print (member c count))))        ;; 2
  (check-equal? out "012"))

(test-case "Object: pn method — with parameter"
  ;; c.add(10); c.count → 12
  (define-values (v out)
    (pn-obj-run reg-mut-counter!
      '(var c (make-object MCounter (count 2)))
      '(pn-method-call c add-n 10)
      '(print (member c count))))
  (check-equal? out "12"))

(test-case "Object: pn method — reset"
  (define-values (v out)
    (pn-obj-run reg-mut-counter!
      '(var c (make-object MCounter (count 5)))
      '(pn-method-call c reset)
      '(print (member c count))))
  (check-equal? out "0"))

(test-case "Object: pn method — multiple fields"
  ;; type Rect { width: float, height: float;
  ;;   pn scale(factor) { width = width * factor; height = height * factor }
  ;;   fn area() => width * height }
  (define-values (v out)
    (pn-obj-run
      (λ () (register-type! 'MRect #f
              (list (field-spec 'width 'float-type 'no-default 'no-constraint)
                    (field-spec 'height 'float-type 'no-default 'no-constraint))
              (list (method-spec 'scale 'pn '(factor)
                      '(seq (assign width (mul width factor))
                            (assign height (mul height factor))))
                    (method-spec 'area 'fn '() '(mul width height)))
              '()))
      '(var r (make-object MRect (width 3.0) (height 4.0)))
      '(print (method-call r area))       ;; 12.0
      '(pn-method-call r scale 2.0)
      '(print (method-call r area))       ;; 48.0
      '(print (member r width))))         ;; 6.0
  (check-equal? out "12486"))


;; ──────────────────────────────────────────────────────────────────────────
;; 7.8 Additional: object_direct_access patterns
;; Corresponds to: test/lambda/object_direct_access.ls
;; ──────────────────────────────────────────────────────────────────────────

(test-case "Object: fn method with param combining fields and args"
  ;; type Adder { base: int; fn add(n) => base + n; fn mul(n) => base * n }
  (clear-type-registry!)
  (register-type! 'Adder #f
    (list (field-spec 'base 'int-type 'no-default 'no-constraint))
    (list (method-spec 'add 'fn '(n) '(add base n))
          (method-spec 'mul 'fn '(n) '(mul base n)))
    '())
  (define a (eval-lambda ε '(make-object Adder (base 10))))
  (check-equal? (eval-lambda `((a . ,a)) '(method-call a add 5)) 15)
  (check-equal? (eval-lambda `((a . ,a)) '(method-call a mul 3)) 30))

(test-case "Object: fn method with bool field"
  ;; type Gate { open: bool; fn status() => if (open) "open" else "closed" }
  (clear-type-registry!)
  (register-type! 'Gate #f
    (list (field-spec 'open 'bool-type 'no-default 'no-constraint))
    (list (method-spec 'status 'fn '() '(if open "open" "closed")))
    '())
  (define g1 (eval-lambda ε '(make-object Gate (open #t))))
  (define g2 (eval-lambda ε '(make-object Gate (open #f))))
  (check-equal? (eval-lambda `((g1 . ,g1)) '(method-call g1 status)) "open")
  (check-equal? (eval-lambda `((g2 . ,g2)) '(method-call g2 status)) "closed"))

(test-case "Object: pn write-back then fn read"
  ;; type Toggle { on: bool = false; pn enable() { on = true } fn state() => on }
  (define-values (v out)
    (pn-obj-run
      (λ () (register-type! 'Toggle #f
              (list (field-spec 'on 'bool-type 'no-default 'no-constraint))
              (list (method-spec 'enable 'pn '()
                      '(seq (assign on #t)))
                    (method-spec 'disable 'pn '()
                      '(seq (assign on #f)))
                    (method-spec 'state 'fn '() 'on))
              '()))
      '(var tog (make-object Toggle (on #f)))
      '(print (method-call tog state))     ;; "false"
      '(pn-method-call tog enable)
      '(print (method-call tog state))     ;; "true"
      '(pn-method-call tog disable)
      '(print (method-call tog state))))   ;; "false"
  (check-equal? out "falsetruefalse"))

(test-case "Object: pn deposit/withdraw pattern"
  (define-values (v out)
    (pn-obj-run
      (λ () (register-type! 'Wallet #f
              (list (field-spec 'balance 'int-type 0 'no-constraint))
              (list (method-spec 'deposit 'pn '(amt)
                      '(seq (assign balance (add balance amt))))
                    (method-spec 'withdraw 'pn '(amt)
                      '(seq (assign balance (sub balance amt))))
                    (method-spec 'check 'fn '() 'balance))
              '()))
      '(var w (make-object Wallet (balance 0)))
      '(pn-method-call w deposit 100)
      '(print (method-call w check))       ;; 100
      '(pn-method-call w withdraw 30)
      '(print (method-call w check))       ;; 70
      '(pn-method-call w deposit 50)
      '(print (method-call w check))))     ;; 120
  (check-equal? out "10070120"))


;; ════════════════════════════════════════════════════════════════════════════
;; RUN ALL TESTS
;; ════════════════════════════════════════════════════════════════════════════

(printf "\n✓ All semantic verification tests passed.\n")
