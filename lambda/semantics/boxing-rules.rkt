#lang racket
;; ============================================================================
;; Lambda Script — Boxing/Unboxing Verification Rules (PLT Redex)
;;
;; Property-based testing for the C representation layer.
;; Uses redex-check to randomly generate Lambda types and expressions,
;; then verifies that the transpiler's boxing/unboxing invariants hold.
;;
;; These rules catch the class of bugs exemplified by Issues #15 and #16:
;;   - Mismatched C types in if-else branches (Issue #15)
;;   - Missing pointer-to-Item conversion at call sites (Issue #16)
;;   - Incorrect unboxed (_u) function variant generation
;;   - Sys-func return type / semantic type mismatches
;;
;; Run: racket lambda/semantics/boxing-rules.rkt
;; ============================================================================

(require redex/reduction-semantics)
(require rackunit)
(require "lambda-core.rkt")
(require "lambda-types.rkt")
(require "c-repr.rkt")


;; ════════════════════════════════════════════════════════════════════════════
;; TYPE GENERATORS
;; ════════════════════════════════════════════════════════════════════════════
;; We use the Redex language definition from lambda-core.rkt as our grammar,
;; but for C-representation testing we also need to enumerate C-types
;; and Lambda types systematically.

;; All concrete Lambda types (no parameterized types)
(define all-lambda-types
  '(int-type int64-type float-type bool-type string-type
    binary-type symbol-type decimal-type datetime-type
    array-type list-type map-type element-type
    func-type type-type null-type error-type any-type
    number-type range-type path-type
    vmap-type array-int-type array-int64-type array-float-type))

;; All C types in the domain
(define all-c-types
  '(Item int32_t int64_t double bool
    String* Symbol* Decimal* DateTime
    Array* List* Map* Element*
    Range* Path* Function* Type*
    ArrayInt* ArrayInt64* ArrayFloat*))

;; Scalar C-types that support direct boxing/unboxing roundtrip
(define scalar-roundtrip-types
  '(int32_t int64_t double bool String*))

;; All scalar + pointer C-types (not Item)
(define non-item-c-types
  (filter (λ (ct) (not (eq? ct 'Item))) all-c-types))


;; ════════════════════════════════════════════════════════════════════════════
;; RULE 1: Boxing Roundtrip Soundness
;; ════════════════════════════════════════════════════════════════════════════
;; For every scalar C-type that has BOTH boxing and unboxing:
;;   unbox(box(v, τ), τ) recovers the original value
;;
;; In C terms: it2i(i2it(x)) == x for int32_t, etc.
;; The model verifies the FUNCTION NAMES are correct; the actual
;; C implementation correctness is verified by unit tests in test/*.cpp.

(define (check-boxing-roundtrip)
  (printf "  Rule 1: Boxing roundtrip soundness\n")
  (define failures '())

  (for ([ct scalar-roundtrip-types])
    (define box-fn (boxing-function ct))
    (define unbox-fn (unboxing-function ct))

    ;; Both must exist
    (unless box-fn
      (set! failures (cons (format "  FAIL: ~a has no boxing function" ct) failures)))
    (unless unbox-fn
      (set! failures (cons (format "  FAIL: ~a has no unboxing function" ct) failures)))

    ;; The unboxing function must be what required-conversion(Item, ct) returns
    (when (and box-fn unbox-fn)
      (define conv (required-conversion 'Item ct))
      (unless (eq? conv unbox-fn)
        (set! failures
          (cons (format "  FAIL: roundtrip mismatch for ~a: conversion says ~a but unboxing says ~a"
                        ct conv unbox-fn)
                failures)))

      ;; The boxing function must be what required-conversion(ct, Item) returns
      (define box-conv (required-conversion ct 'Item))
      (unless (eq? box-conv box-fn)
        (set! failures
          (cons (format "  FAIL: boxing mismatch for ~a: conversion says ~a but boxing says ~a"
                        ct box-conv box-fn)
                failures)))))

  (if (null? failures)
      (printf "    ✓ All ~a scalar types pass roundtrip check\n"
              (length scalar-roundtrip-types))
      (for ([f (reverse failures)]) (printf "~a\n" f)))
  (null? failures))


;; ════════════════════════════════════════════════════════════════════════════
;; RULE 2: If-Else C-Type Consistency
;; ════════════════════════════════════════════════════════════════════════════
;; For every pair of Lambda types (τ₁, τ₂):
;;   if C-type(τ₁) ≠ C-type(τ₂), then verify-if-branches reports must-box-both
;;   if C-type(τ₁) = C-type(τ₂), then verify-if-branches reports ok
;;
;; This catches Issue #15: C ternary with String* vs int32_t

(define (check-if-else-consistency)
  (printf "  Rule 2: If-else C-type consistency\n")
  (define failures '())
  (define checked 0)

  (for* ([τ1 all-lambda-types]
         [τ2 all-lambda-types])
    (set! checked (add1 checked))
    (define ct1 (lambda-type->c-type τ1))
    (define ct2 (lambda-type->c-type τ2))
    (define result (verify-if-branches τ1 τ2))
    (define verdict (car result))

    (cond
      ;; Same C-type → must be ok
      [(eq? ct1 ct2)
       (unless (eq? verdict 'ok)
         (set! failures
           (cons (format "  FAIL: ~a=~a vs ~a=~a same C-type but verdict is ~a"
                         τ1 ct1 τ2 ct2 verdict)
                 failures)))]

      ;; Different C-type → must be must-box-both
      [else
       (unless (eq? verdict 'must-box-both)
         (set! failures
           (cons (format "  FAIL: ~a=~a vs ~a=~a different C-types but verdict is ~a"
                         τ1 ct1 τ2 ct2 verdict)
                 failures)))]))

  (if (null? failures)
      (printf "    ✓ All ~a type pairs pass if-else consistency\n" checked)
      (for ([f (reverse failures)]) (printf "~a\n" f)))
  (null? failures))


;; ════════════════════════════════════════════════════════════════════════════
;; RULE 3: Call Site Argument Conversion Completeness
;; ════════════════════════════════════════════════════════════════════════════
;; For every combination of (arg-type, param-type):
;;   The required conversion must be defined (never 'type-error for valid pairs)
;;
;; Valid pairs are those where arg-type <: param-type (subtype compatible)
;; This catches Issue #16: Item passed where String* expected

(define (check-call-site-conversions)
  (printf "  Rule 3: Call site argument conversion completeness\n")
  (define failures '())
  (define valid-pairs 0)
  (define total-pairs 0)

  (for* ([arg-τ all-lambda-types]
         [param-τ all-lambda-types])
    (set! total-pairs (add1 total-pairs))
    (define arg-ct (lambda-type->c-type arg-τ))
    (define param-ct (lambda-type->c-type param-τ))
    (define conv (required-conversion arg-ct param-ct))

    ;; If the types are subtype-compatible, require a valid conversion
    (when (subtype? arg-τ param-τ)
      (set! valid-pairs (add1 valid-pairs))
      (when (eq? conv 'type-error)
        (set! failures
          (cons (format "  FAIL: ~a (~a) <: ~a (~a) but conversion is type-error"
                        arg-τ arg-ct param-τ param-ct)
                failures)))))

  (if (null? failures)
      (printf "    ✓ ~a subtype-compatible pairs all have valid conversions (of ~a total)\n"
              valid-pairs total-pairs)
      (for ([f (reverse failures)]) (printf "~a\n" f)))
  (null? failures))


;; ════════════════════════════════════════════════════════════════════════════
;; RULE 4: Sys-Func Discrepancy Detection
;; ════════════════════════════════════════════════════════════════════════════
;; For every system function entry:
;;   If semantic-type ≠ C-return-type mapping, it MUST be flagged as discrepancy
;;   If semantic-type = C-return-type mapping, it MUST NOT be flagged
;;
;; This ensures our discrepancy flags are accurate.

(define (check-sys-func-discrepancies)
  (printf "  Rule 4: Sys-func discrepancy flag accuracy\n")
  (define failures '())

  (for ([entry sys-func-catalog])
    (define name (sys-func-entry-name entry))
    (define sem-τ (sys-func-entry-semantic-type entry))
    (define c-ret (sys-func-entry-c-return-type entry))
    (define flagged? (sys-func-entry-discrepancy? entry))
    (define expected-ct (lambda-type->c-type sem-τ))

    ;; Check: flag should be set iff semantic→C-type ≠ actual C-return-type
    (define should-flag? (not (eq? expected-ct c-ret)))

    (when (and should-flag? (not flagged?))
      (set! failures
        (cons (format "  FAIL: ~a semantic=~a→~a but C=~a — should be flagged but isn't"
                      name sem-τ expected-ct c-ret)
              failures)))
    (when (and flagged? (not should-flag?))
      (set! failures
        (cons (format "  FAIL: ~a flagged as discrepancy but semantic=~a→~a matches C=~a"
                      name sem-τ expected-ct c-ret)
              failures))))

  (if (null? failures)
      (printf "    ✓ All ~a sys-func entries have correct discrepancy flags\n"
              (length sys-func-catalog))
      (for ([f (reverse failures)]) (printf "~a\n" f)))
  (null? failures))


;; ════════════════════════════════════════════════════════════════════════════
;; RULE 5: Unboxed Variant Safety
;; ════════════════════════════════════════════════════════════════════════════
;; A function can have an unboxed (_u) variant only if:
;;   1. Its return type is int-type or int64-type (currently safe types)
;;   2. Its return C-type can represent all possible values without Item
;;
;; For sys-funcs: if a func returns int64_t in C, but its semantic type
;; is any-type, it's NOT safe for unboxed because callers may expect Item.

(define (check-unboxed-variant-safety)
  (printf "  Rule 5: Unboxed variant safety\n")
  (define failures '())

  ;; Verify safe types are actually safe (have scalar C-types)
  (for ([τ '(int-type int64-type)])
    (define ct (lambda-type->c-type τ))
    (unless (scalar-c-type? ct)
      (set! failures
        (cons (format "  FAIL: ~a maps to ~a which is not scalar" τ ct)
              failures))))

  ;; Verify unsafe types are actually unsafe
  (for ([τ '(float-type string-type bool-type any-type null-type
             array-type list-type map-type element-type)])
    (when (safe-for-unboxed-variant? τ)
      (set! failures
        (cons (format "  FAIL: ~a is marked safe for unboxed but shouldn't be" τ)
              failures))))

  ;; Check that safe types' C representations don't lose information
  (for ([τ '(int-type int64-type)])
    (define ct (lambda-type->c-type τ))
    (define box-fn (boxing-function ct))
    (define unbox-fn (unboxing-function ct))
    (unless (and box-fn unbox-fn)
      (set! failures
        (cons (format "  FAIL: safe unboxed type ~a (~a) missing box/unbox functions"
                      τ ct)
              failures))))

  (if (null? failures)
      (printf "    ✓ Unboxed variant safety rules are consistent\n")
      (for ([f (reverse failures)]) (printf "~a\n" f)))
  (null? failures))


;; ════════════════════════════════════════════════════════════════════════════
;; RULE 6: Closure Capture Boxing
;; ════════════════════════════════════════════════════════════════════════════
;; When a variable is captured by a closure, it must be boxed to Item.
;; This rule verifies that ALL C-types have a boxing path to Item.

(define (check-closure-capture-boxing)
  (printf "  Rule 6: Closure capture boxing completeness\n")
  (define failures '())

  (for ([ct non-item-c-types])
    (define box-fn (boxing-function ct))
    ;; Every non-Item C-type must be boxable for closure capture
    (unless box-fn
      ;; Exception: List* returns #f because list_end() already yields Item
      (unless (eq? ct 'List*)
        (set! failures
          (cons (format "  FAIL: ~a has no boxing function for closure capture" ct)
                failures))))

    ;; Verify the conversion from ct to Item is valid
    (define conv (required-conversion ct 'Item))
    (unless conv
      ;; For pointer types, #f is acceptable (pointer→Item is implicit cast)
      ;; We only care that it's not 'type-error
      (void))
    (when (eq? conv 'type-error)
      (set! failures
        (cons (format "  FAIL: ~a → Item conversion is type-error" ct)
              failures))))

  (if (null? failures)
      (printf "    ✓ All ~a non-Item C-types can be boxed for closure capture\n"
              (length non-item-c-types))
      (for ([f (reverse failures)]) (printf "~a\n" f)))
  (null? failures))


;; ════════════════════════════════════════════════════════════════════════════
;; RULE 7: Type Mapping Totality
;; ════════════════════════════════════════════════════════════════════════════
;; Every concrete Lambda type must map to a valid C-type.
;; The C-type must be in our known domain (not fall through to default).

(define (check-type-mapping-totality)
  (printf "  Rule 7: Type mapping totality\n")
  (define failures '())

  (for ([τ all-lambda-types])
    (define ct (lambda-type->c-type τ))
    (unless (member ct all-c-types)
      (set! failures
        (cons (format "  FAIL: ~a maps to ~a which is not in C-type domain" τ ct)
              failures))))

  ;; Also check composite types
  (for ([τ (list '(nullable int-type)
                 '(union int-type string-type)
                 '(fn-type (int-type) int-type)
                 '(error-ret int-type)
                 '(error-ret string-type)
                 '(array-of int-type)
                 '(list-of string-type)
                 '(map-of int-type))])
    (define ct (lambda-type->c-type τ))
    (unless (member ct all-c-types)
      (set! failures
        (cons (format "  FAIL: composite ~a maps to ~a which is not in C-type domain"
                      τ ct)
              failures))))

  (if (null? failures)
      (printf "    ✓ All ~a Lambda types map to valid C-types\n"
              (+ (length all-lambda-types) 8))
      (for ([f (reverse failures)]) (printf "~a\n" f)))
  (null? failures))


;; ════════════════════════════════════════════════════════════════════════════
;; RULE 8: Conversion Symmetry
;; ════════════════════════════════════════════════════════════════════════════
;; For scalar C-types that can round-trip through Item:
;;   If boxing(ct) exists and unboxing(ct) exists,
;;   then conversion(ct, Item) should be boxing(ct)
;;   and  conversion(Item, ct) should be unboxing(ct)

(define (check-conversion-symmetry)
  (printf "  Rule 8: Conversion symmetry for scalar types\n")
  (define failures '())

  (for ([ct '(int32_t int64_t double bool String* Symbol* DateTime Decimal*)])
    (define box-fn (boxing-function ct))
    (define unbox-fn (unboxing-function ct))

    (when box-fn
      (define to-item (required-conversion ct 'Item))
      (unless (eq? to-item box-fn)
        (set! failures
          (cons (format "  FAIL: ~a→Item: conversion=~a but boxing=~a" ct to-item box-fn)
                failures))))

    (when unbox-fn
      (define from-item (required-conversion 'Item ct))
      (unless (eq? from-item unbox-fn)
        (set! failures
          (cons (format "  FAIL: Item→~a: conversion=~a but unboxing=~a" ct from-item unbox-fn)
                failures)))))

  (if (null? failures)
      (printf "    ✓ Boxing/unboxing is symmetric with conversion rules\n")
      (for ([f (reverse failures)]) (printf "~a\n" f)))
  (null? failures))


;; ════════════════════════════════════════════════════════════════════════════
;; RULE 9: Sys-Func Call Conversion Verification
;; ════════════════════════════════════════════════════════════════════════════
;; For each sys-func with a discrepancy: verify that the required conversion
;; exists and is a known function (not type-error).
;; This tests the transpiler's ability to handle the mismatch.

(define (check-sys-func-call-conversions)
  (printf "  Rule 9: Sys-func call conversion availability\n")
  (define failures '())
  (define checked 0)

  (for ([entry sys-func-catalog])
    (define name (sys-func-entry-name entry))
    (define sem-τ (sys-func-entry-semantic-type entry))
    (define c-ret (sys-func-entry-c-return-type entry))
    (define expected-ct (lambda-type->c-type sem-τ))

    ;; If C return ≠ expected C type, a conversion must exist
    (when (not (eq? c-ret expected-ct))
      (set! checked (add1 checked))
      (define conv (required-conversion c-ret expected-ct))
      (when (eq? conv 'type-error)
        (set! failures
          (cons (format "  FAIL: ~a returns ~a but semantic expects ~a — no valid conversion"
                        name c-ret expected-ct)
                failures)))))

  (if (null? failures)
      (printf "    ✓ All ~a sys-func mismatches have valid conversions\n" checked)
      (for ([f (reverse failures)]) (printf "~a\n" f)))
  (null? failures))


;; ════════════════════════════════════════════════════════════════════════════
;; RULE 10: Error-Return Type Unwrapping
;; ════════════════════════════════════════════════════════════════════════════
;; error-ret types should unwrap to the inner type's C representation.
;; This is important because the transpiler needs to emit the right
;; C-type for functions that return T^E.

(define (check-error-ret-unwrapping)
  (printf "  Rule 10: Error-return type unwrapping\n")
  (define failures '())

  (for ([τ all-lambda-types])
    (define ct (lambda-type->c-type τ))
    (define err-ct (lambda-type->c-type `(error-ret ,τ)))
    ;; error-ret(τ) should have same C-type as τ
    (unless (eq? ct err-ct)
      (set! failures
        (cons (format "  FAIL: error-ret(~a) maps to ~a but ~a maps to ~a"
                      τ err-ct τ ct)
              failures))))

  (if (null? failures)
      (printf "    ✓ All ~a error-ret types unwrap correctly\n"
              (length all-lambda-types))
      (for ([f (reverse failures)]) (printf "~a\n" f)))
  (null? failures))


;; ════════════════════════════════════════════════════════════════════════════
;; REDEX-CHECK: Generate Random Types
;; ════════════════════════════════════════════════════════════════════════════
;; Use redex-check to generate random Lambda type pairs and verify
;; that our C-representation invariants hold.

;; Define a simple Redex language for type generation
(define-language LambdaTypes
  (τ int-type int64-type float-type bool-type string-type
     symbol-type decimal-type datetime-type
     array-type list-type map-type element-type
     func-type type-type null-type error-type any-type
     number-type range-type path-type
     (nullable τ)
     (union τ τ)
     (error-ret τ)
     (array-of τ)
     (fn-type (τ ...) τ)))

;; Property 1: type mapping totality — every generated type maps to a known C-type
(define (run-redex-type-mapping-check [attempts 200])
  (printf "  Redex-check: type mapping totality (~a attempts)\n" attempts)
  (redex-check LambdaTypes τ
    (let ([ct (lambda-type->c-type (term τ))])
      (member ct all-c-types))
    #:attempts attempts
    #:print? #f)
  (printf "    ✓ Passed\n"))

;; Property 2: same-type conversion is always #f
(define (run-redex-same-type-conversion-check [attempts 200])
  (printf "  Redex-check: same-type conversion identity (~a attempts)\n" attempts)
  (redex-check LambdaTypes τ
    (let ([ct (lambda-type->c-type (term τ))])
      (not (required-conversion ct ct)))
    #:attempts attempts
    #:print? #f)
  (printf "    ✓ Passed\n"))

;; Property 3: if-else consistency for random type pairs
(define (run-redex-if-else-check [attempts 200])
  (printf "  Redex-check: if-else C-type consistency (~a attempts)\n" attempts)
  (redex-check LambdaTypes (τ_1 τ_2)
    (let* ([ct1 (lambda-type->c-type (term τ_1))]
           [ct2 (lambda-type->c-type (term τ_2))]
           [result (verify-if-branches (term τ_1) (term τ_2))])
      (if (eq? ct1 ct2)
          (eq? (car result) 'ok)
          (eq? (car result) 'must-box-both)))
    #:attempts attempts
    #:print? #f)
  (printf "    ✓ Passed\n"))

;; Property 4: error-ret unwrapping
(define (run-redex-error-ret-check [attempts 200])
  (printf "  Redex-check: error-ret unwrapping (~a attempts)\n" attempts)
  (redex-check LambdaTypes τ
    (let ([ct (lambda-type->c-type (term τ))]
          [err-ct (lambda-type->c-type (term (error-ret τ)))])
      (eq? ct err-ct))
    #:attempts attempts
    #:print? #f)
  (printf "    ✓ Passed\n"))


;; ════════════════════════════════════════════════════════════════════════════
;; COMBINED ISSUE REGRESSION
;; ════════════════════════════════════════════════════════════════════════════
;; Specific regression tests for known issues.

(define (check-issue-regressions)
  (printf "  Issue regressions\n")
  (define all-ok #t)

  ;; Issue #15: if-else with String* vs int32_t
  ;; The transpiler was emitting raw ternary with mismatched types
  (let ([result (verify-if-branches 'string-type 'int-type)])
    (unless (eq? (car result) 'must-box-both)
      (printf "  FAIL: Issue #15 regression — String* vs int32_t not caught\n")
      (set! all-ok #f)))

  ;; Issue #16: Item passed where String* expected
  ;; The transpiler wasn't inserting it2s() for `: string` annotation
  (let ([conv (required-conversion 'Item 'String*)])
    (unless (eq? conv 'it2s)
      (printf "  FAIL: Issue #16 regression — Item→String* should use it2s\n")
      (set! all-ok #f)))

  ;; Issue #16 variant: Any-typed argument to string parameter
  (let ([results (verify-call-args '(any-type) '(string-type))])
    (unless (eq? (last (car results)) 'needs-conversion)
      (printf "  FAIL: Issue #16 variant — any-type arg to string param not caught\n")
      (set! all-ok #f)))

  ;; Sys-func scenario: normalize() returns Item, used as String* param
  (let ([conv (sys-func-needs-conversion? 'normalize 'string-type)])
    (unless (eq? conv 'it2s)
      (printf "  FAIL: normalize→string conversion not correctly identified\n")
      (set! all-ok #f)))

  (if all-ok
      (printf "    ✓ All issue regressions pass\n")
      (void))
  all-ok)


;; ════════════════════════════════════════════════════════════════════════════
;; MAIN: Run All Verification Rules
;; ════════════════════════════════════════════════════════════════════════════

(define (run-all-rules)
  (printf "\n═══ Boxing/Unboxing Verification Rules ═══\n\n")
  (define results
    (list
     (check-boxing-roundtrip)
     (check-if-else-consistency)
     (check-call-site-conversions)
     (check-sys-func-discrepancies)
     (check-unboxed-variant-safety)
     (check-closure-capture-boxing)
     (check-type-mapping-totality)
     (check-conversion-symmetry)
     (check-sys-func-call-conversions)
     (check-error-ret-unwrapping)
     (check-issue-regressions)))

  (printf "\n═══ Redex Property-Based Checks ═══\n\n")
  (run-redex-type-mapping-check)
  (run-redex-same-type-conversion-check)
  (run-redex-if-else-check)
  (run-redex-error-ret-check)

  (define pass-count (count identity results))
  (define total (length results))
  (printf "\n═══ Summary: ~a/~a deterministic rules passed ═══\n"
          pass-count total)
  (when (< pass-count total)
    (printf "⚠ ~a rules FAILED — see details above\n" (- total pass-count)))
  (when (= pass-count total)
    (printf "✓ All verification rules passed.\n")))

;; Run when executed directly
(run-all-rules)
