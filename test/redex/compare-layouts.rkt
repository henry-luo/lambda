#lang racket/base

;; compare-layouts.rkt — Tree-walk comparator for layout results
;;
;; Phase 2: Reference-Driven Testing
;;
;; Compares a Redex view tree (layout output) against a browser reference
;; expected layout tree. Reports per-element pass/fail with configurable
;; tolerance, matching the comparison infrastructure in compare-layout.js.
;;
;; Tolerance model:
;;   base tolerance: 5px (absolute)
;;   proportional: 3% of reference dimension (for width/height)
;;   effective tolerance = max(base, proportional)

(require racket/match
         racket/list
         racket/string
         racket/format
         racket/math)

(provide compare-layouts
         compare-result?
         compare-result-passed?
         compare-result-total
         compare-result-failures
         compare-result-summary
         make-compare-config
         compare-config?
         default-config
         layout-failure?
         layout-failure-path
         layout-failure-property
         layout-failure-expected
         layout-failure-actual
         layout-failure-tolerance)

;; ============================================================
;; Configuration
;; ============================================================

(struct compare-config
  (base-tolerance         ; absolute tolerance in px (default: 5)
   proportional-tolerance ; fraction of reference value (default: 0.03 = 3%)
   check-x?              ; whether to compare x positions
   check-y?              ; whether to compare y positions
   check-width?          ; whether to compare widths
   check-height?         ; whether to compare heights
   )
  #:transparent)

(define default-config
  (compare-config 5 0.03 #t #t #t #t))

(define (make-compare-config
         #:base-tolerance [base 5]
         #:proportional-tolerance [prop 0.03]
         #:check-x? [cx #t]
         #:check-y? [cy #t]
         #:check-width? [cw #t]
         #:check-height? [ch #t])
  (compare-config base prop cx cy cw ch))

;; ============================================================
;; Comparison Result
;; ============================================================

;; a failure records: which element, which property, expected, actual, tolerance
(struct layout-failure
  (path          ; list of ids from root to element
   property      ; 'x, 'y, 'width, 'height
   expected      ; reference value
   actual        ; Redex value
   tolerance     ; effective tolerance used
   )
  #:transparent)

(struct compare-result
  (passed?       ; #t if all comparisons passed
   total         ; total number of elements compared
   failures      ; list of layout-failure
   )
  #:transparent)

(define (compare-result-summary result)
  (define fails (compare-result-failures result))
  (define total (compare-result-total result))
  (define n-fail (length fails))
  (if (compare-result-passed? result)
      (format "PASS: ~a elements compared, all within tolerance" total)
      (format "FAIL: ~a/~a elements have mismatches\n~a"
              n-fail total
              (string-join
               (map failure->string fails)
               "\n"))))

(define (failure->string f)
  (format "  ~a.~a: expected=~a, actual=~a (tol=~a)"
          (string-join (map symbol->string (layout-failure-path f)) " > ")
          (layout-failure-property f)
          (real->decimal-string* (layout-failure-expected f))
          (real->decimal-string* (layout-failure-actual f))
          (real->decimal-string* (layout-failure-tolerance f))))

(define (real->decimal-string* n)
  (cond
    [(not (number? n)) (format "~a" n)]
    [(eqv? n +inf.0) "+inf.0"]
    [(eqv? n -inf.0) "-inf.0"]
    [(nan? n) "NaN"]
    [(integer? n) (number->string (exact->inexact n))]
    [else (~r n #:precision '(= 2))]))

;; ============================================================
;; Main Comparison
;; ============================================================

;; compare a Redex view tree against a reference expected layout.
;; view: (view id x y width height (children ...))
;; expected: (expected id x y width height (children ...))
;; config: compare-config
;; returns: compare-result
(define (compare-layouts view expected [config default-config])
  (define failures '())
  (define total (box 0))

  (define (walk! view-node exp-node path)
    (set-box! total (add1 (unbox total)))

    ;; extract view values
    (define-values (v-x v-y v-w v-h v-children)
      (parse-view-node view-node))

    ;; extract expected values
    (define-values (e-x e-y e-w e-h e-children)
      (parse-expected-node exp-node))

    ;; compare each property
    (when (compare-config-check-x? config)
      (let ([tol (effective-tolerance e-x config)])
        (unless (within-tolerance? v-x e-x tol)
          (set! failures
                (cons (layout-failure path 'x e-x v-x tol) failures)))))

    (when (compare-config-check-y? config)
      (let ([tol (effective-tolerance e-y config)])
        (unless (within-tolerance? v-y e-y tol)
          (set! failures
                (cons (layout-failure path 'y e-y v-y tol) failures)))))

    (when (compare-config-check-width? config)
      (let ([tol (effective-tolerance e-w config)])
        (unless (within-tolerance? v-w e-w tol)
          (set! failures
                (cons (layout-failure path 'width e-w v-w tol) failures)))))

    (when (compare-config-check-height? config)
      (let ([tol (effective-tolerance e-h config)])
        (unless (within-tolerance? v-h e-h tol)
          (set! failures
                (cons (layout-failure path 'height e-h v-h tol) failures)))))

    ;; recurse into children (pair up by index)
    (define v-kids (or v-children '()))
    (define e-kids (or e-children '()))
    (define min-count (min (length v-kids) (length e-kids)))

    (for ([i (in-range min-count)])
      (define v-child (list-ref v-kids i))
      (define e-child (list-ref e-kids i))
      (define child-id (extract-id e-child))
      (walk! v-child e-child
             (append path (list child-id))))

    ;; if child counts differ, report as well
    (when (not (= (length v-kids) (length e-kids)))
      (set! failures
            (cons (layout-failure path 'child-count
                                  (length e-kids) (length v-kids) 0)
                  failures))))

  ;; start the walk
  (when (and view expected)
    (define root-id (extract-id expected))
    (walk! view expected (list root-id)))

  (compare-result (null? failures)
                  (unbox total)
                  (reverse failures)))

;; ============================================================
;; Node Parsing Helpers
;; ============================================================

;; parse a Redex view node
(define (parse-view-node view)
  (match view
    [`(view ,id ,x ,y ,w ,h ,children)
     ;; filter out view-text children (text nodes aren't in the reference expected tree)
     (define element-children
       (filter (lambda (c)
                 (match c [`(view-text . ,_) #f] [_ #t]))
               children))
     (values (->num x) (->num y) (->num w) (->num h) element-children)]
    [`(view ,id ,x ,y ,w ,h)
     (values (->num x) (->num y) (->num w) (->num h) '())]
    [`(view-text ,id ,x ,y ,w ,h ,text)
     (values (->num x) (->num y) (->num w) (->num h) '())]
    [_ (values 0 0 0 0 '())]))

;; parse an expected layout node
(define (parse-expected-node exp)
  (match exp
    [`(expected ,id ,x ,y ,w ,h ,children)
     (values (->num x) (->num y) (->num w) (->num h) children)]
    [`(expected ,id ,x ,y ,w ,h)
     (values (->num x) (->num y) (->num w) (->num h) '())]
    [_ (values 0 0 0 0 '())]))

;; extract id from an expected or view node
(define (extract-id node)
  (match node
    [`(expected ,id . ,_) id]
    [`(view ,id . ,_) id]
    [`(view-text ,id . ,_) id]
    [_ 'unknown]))

;; ensure numeric
(define (->num v)
  (cond
    [(number? v) (exact->inexact v)]
    [(string? v) (or (string->number v) 0.0)]
    [else 0.0]))

;; ============================================================
;; Tolerance Calculation
;; ============================================================

;; compute effective tolerance:
;;   max(base_tolerance, proportional_tolerance * |reference_value|)
(define (effective-tolerance ref-val config)
  (define base (compare-config-base-tolerance config))
  (define prop (compare-config-proportional-tolerance config))
  (max base (* prop (abs (->num ref-val)))))

;; check if actual is within tolerance of expected
(define (within-tolerance? actual expected tolerance)
  (<= (abs (- (->num actual) (->num expected))) tolerance))
