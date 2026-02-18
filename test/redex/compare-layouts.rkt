#lang racket/base

;; compare-layouts.rkt — Tree-walk comparator for layout results
;;
;; Phase 2: Reference-Driven Testing
;;
;; Compares a Redex view tree (layout output) against a browser reference
;; expected layout tree. Reports per-element pass/fail with configurable
;; tolerance, matching the comparison infrastructure in compare-layout.js.
;;
;; Matching rules:
;;   1. Tree structure must match 100% (including text nodes).
;;      If child counts differ at any level, the test fails immediately.
;;   2. Property tolerance (only when structure matches):
;;      tolerance = min(max(3, 0.03 × |reference_value|), 10)
;;      Minimum 3px, maximum 10px (CSS logical pixels).

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
   max-tolerance          ; cap tolerance in px (default: 12)
   check-x?              ; whether to compare x positions
   check-y?              ; whether to compare y positions
   check-width?          ; whether to compare widths
   check-height?         ; whether to compare heights
   )
  #:transparent)

(define default-config
  (compare-config 3 0.03 10 #t #t #t #t))

(define (make-compare-config
         #:base-tolerance [base 3]
         #:proportional-tolerance [prop 0.03]
         #:max-tolerance [max-tol 10]
         #:check-x? [cx #t]
         #:check-y? [cy #t]
         #:check-width? [cw #t]
         #:check-height? [ch #t])
  (compare-config base prop max-tol cx cy cw ch))

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
          (string-join (map id->display-string (layout-failure-path f)) " > ")
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

    ;; check node-type match (element vs text)
    (define v-kind (node-kind view-node))
    (define e-kind (node-kind exp-node))
    (unless (eq? v-kind e-kind)
      (set! failures
            (cons (layout-failure path 'node-type e-kind v-kind 0) failures)))

    ;; check element tag match (only for element nodes)
    (when (and (eq? v-kind 'element) (eq? e-kind 'element))
      (let ([v-tag (extract-tag (extract-id view-node))]
            [e-tag (extract-tag (extract-id exp-node))])
        (when (and v-tag e-tag (not (equal? v-tag e-tag)))
          (set! failures
                (cons (layout-failure path 'element-tag e-tag v-tag 0) failures)))))

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

    ;; recurse into children — structure must match 100%
    (define v-kids (or v-children '()))
    (define e-kids (or e-children '()))

    ;; CSS 2.2 §17.2.1: Anonymous table objects.
    ;; Two kinds of anonymous wrappers:
    ;; 1. anon-table-/anon-tbody-: wrap table-internal children in non-table parents.
    ;;    These may correspond to real elements in the browser reference (e.g. auto-
    ;;    inserted <tbody>), so only flatten when child counts don't match.
    ;; 2. anon-row-/anon-cell-: wrap non-proper children of table boxes.
    ;;    These never appear in the browser reference, so always flatten them.
    (define (is-anon-table-or-tbody? node)
      (define id (extract-id node))
      (and (symbol? id)
           (let ([s (symbol->string id)])
             (or (string-prefix? s "anon-table-")
                 (string-prefix? s "anon-tbody-")
                 (string-prefix? s "anon-row-")
                 (string-prefix? s "anon-cell-")))))

    (define (is-anon-row-or-cell? node)
      (define id (extract-id node))
      (and (symbol? id)
           (let ([s (symbol->string id)])
             (or (string-prefix? s "anon-row-")
                 (string-prefix? s "anon-cell-")))))

    (define (flatten-anon-tables kids)
      (apply append
             (for/list ([k (in-list kids)])
               (if (is-anon-table-or-tbody? k)
                   (let-values ([(x y w h children) (parse-view-node k)])
                     (flatten-anon-tables (or children '())))
                   (list k)))))

    (define (flatten-anon-row-cell kids)
      ;; Flatten anonymous row/cell wrappers, adjusting child coordinates
      ;; by adding the wrapper's (x, y) offset since coords are parent-relative.
      (apply append
             (for/list ([k (in-list kids)])
               (if (is-anon-row-or-cell? k)
                   (let-values ([(px py pw ph children) (parse-view-node k)])
                     (define adjusted-children
                       (for/list ([c (in-list (or children '()))])
                         (offset-view-node c px py)))
                     (flatten-anon-row-cell adjusted-children))
                   (list k)))))

    ;; Always flatten anon-row/anon-cell wrappers (they never exist in references).
    ;; Always flatten anon-table- wrappers (anonymous table objects from CSS §17.2.1,
    ;; never present in browser DOM). Only flatten anon-tbody- when counts don't match.
    (define (is-anon-table-wrapper? node)
      (define id (extract-id node))
      (and (symbol? id)
           (string-prefix? (symbol->string id) "anon-table-")))

    (define v-kids-1
      (if (ormap is-anon-row-or-cell? v-kids)
          (flatten-anon-row-cell v-kids)
          v-kids))
    ;; Always flatten anon-table- wrappers (they are anonymous table objects
    ;; generated by CSS §17.2.1 and never appear in browser references)
    (define v-kids-2
      (if (ormap is-anon-table-wrapper? v-kids-1)
          (flatten-anon-tables v-kids-1)
          v-kids-1))
    (define effective-v-kids
      (if (and (not (= (length v-kids-2) (length e-kids)))
               (ormap is-anon-table-or-tbody? v-kids-2))
          (flatten-anon-tables v-kids-2)
          v-kids-2))

    (cond
      ;; structural mismatch: fail immediately, don't recurse into misaligned children
      [(not (= (length effective-v-kids) (length e-kids)))
       (set! failures
             (cons (layout-failure path 'child-count
                                   (length e-kids) (length effective-v-kids) 0)
                   failures))]
      [else
       (for ([i (in-range (length e-kids))])
         (define v-child (list-ref effective-v-kids i))
         (define e-child (list-ref e-kids i))
         (define child-id (extract-id e-child))
         (walk! v-child e-child
                (append path (list child-id))))]))

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

;; classify node as 'element or 'text
(define (node-kind node)
  (match node
    [`(view . ,_) 'element]
    [`(view-text . ,_) 'text]
    [`(expected . ,_) 'element]
    [`(expected-text . ,_) 'text]
    [_ 'unknown]))

;; extract the tag prefix from a tag:name encoded id symbol.
;; e.g. 'div:box0 → "div", 'div:anon → "div"
;; returns #f if no tag prefix found.
(define (extract-tag id)
  (define s (if (symbol? id) (symbol->string id) (format "~a" id)))
  (define parts (string-split s ":"))
  (if (> (length parts) 1) (car parts) #f))

;; parse a Redex view node
(define (parse-view-node view)
  (match view
    [`(view ,id ,x ,y ,w ,h ,children ,baseline)
     ;; view with stored baseline — ignore baseline for comparison
     (values (->num x) (->num y) (->num w) (->num h) children)]
    [`(view ,id ,x ,y ,w ,h ,children)
     ;; include all children (elements + text) for structural comparison
     (values (->num x) (->num y) (->num w) (->num h) children)]
    [`(view ,id ,x ,y ,w ,h)
     (values (->num x) (->num y) (->num w) (->num h) '())]
    [`(view-text ,id ,x ,y ,w ,h ,text)
     (values (->num x) (->num y) (->num w) (->num h) '())]
    [_ (values 0 0 0 0 '())]))

;; offset a view node's position by (dx, dy)
(define (offset-view-node node dx dy)
  (match node
    [`(view ,id ,x ,y ,w ,h ,children ,baseline)
     `(view ,id ,(+ (->num x) dx) ,(+ (->num y) dy) ,w ,h ,children ,baseline)]
    [`(view ,id ,x ,y ,w ,h ,children)
     `(view ,id ,(+ (->num x) dx) ,(+ (->num y) dy) ,w ,h ,children)]
    [`(view ,id ,x ,y ,w ,h)
     `(view ,id ,(+ (->num x) dx) ,(+ (->num y) dy) ,w ,h)]
    [`(view-text ,id ,x ,y ,w ,h ,text)
     `(view-text ,id ,(+ (->num x) dx) ,(+ (->num y) dy) ,w ,h ,text)]
    [_ node]))

;; parse an expected layout node
(define (parse-expected-node exp)
  (match exp
    [`(expected ,id ,x ,y ,w ,h ,children)
     (values (->num x) (->num y) (->num w) (->num h) children)]
    [`(expected ,id ,x ,y ,w ,h)
     (values (->num x) (->num y) (->num w) (->num h) '())]
    [`(expected-text ,id ,x ,y ,w ,h)
     (values (->num x) (->num y) (->num w) (->num h) '())]
    [_ (values 0 0 0 0 '())]))

;; extract id from an expected or view node
(define (extract-id node)
  (match node
    [`(expected ,id . ,_) id]
    [`(expected-text ,id . ,_) id]
    [`(view ,id . ,_) id]
    [`(view-text ,id . ,_) id]
    [_ 'unknown]))

;; format an id for display (strip tag prefix)
(define (id->display-string id)
  (if (symbol? id) (symbol->string id) (format "~a" id)))

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
;;   min(max(base_tolerance, proportional_tolerance * |reference_value|), max_tolerance)
(define (effective-tolerance ref-val config)
  (define base (compare-config-base-tolerance config))
  (define prop (compare-config-proportional-tolerance config))
  (define cap (compare-config-max-tolerance config))
  (min cap (max base (* prop (abs (->num ref-val))))))

;; check if actual is within tolerance of expected
(define (within-tolerance? actual expected tolerance)
  (<= (abs (- (->num actual) (->num expected))) tolerance))
