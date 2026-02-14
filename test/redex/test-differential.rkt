#lang racket/base

;; test-differential.rkt — Batch runner for reference-driven differential testing
;;
;; Phase 2: Reference-Driven Testing
;;
;; Iterates over browser reference JSONs, converts each to a Redex box tree,
;; runs the Redex layout engine, and compares against browser-expected positions.
;;
;; Usage:
;;   racket test-differential.rkt                  ; run all simple baseline tests
;;   racket test-differential.rkt --limit 10       ; run first 10 tests
;;   racket test-differential.rkt --filter flex     ; run tests matching "flex"
;;   racket test-differential.rkt --verbose        ; show per-test details

(require racket/match
         racket/list
         racket/string
         racket/format
         racket/path
         racket/cmdline
         "css-layout-lang.rkt"
         "layout-common.rkt"
         "layout-dispatch.rkt"
         "reference-import.rkt"
         "compare-layouts.rkt")

;; ============================================================
;; Path Configuration
;; ============================================================

;; resolve the project test/ directory from our location (test/redex/)
(define test-dir
  (simplify-path (build-path (current-directory) "..")))

(define (baseline-html-dir)
  (build-path test-dir "layout" "data" "baseline"))

(define (reference-dir)
  (build-path test-dir "layout" "reference"))

;; ============================================================
;; Test Discovery
;; ============================================================

;; find all test pairs: (html-path . ref-path) for simple baseline tests
(define (discover-tests #:filter-pattern [pattern #f]
                        #:limit [limit #f])
  (define html-dir (baseline-html-dir))
  (define ref-dir (reference-dir))

  (define html-files
    (sort (filter (lambda (f) (regexp-match? #rx"\\.html$" (path->string f)))
                  (directory-list html-dir))
          string<? #:key path->string))

  ;; filter by pattern if provided
  (define filtered
    (if pattern
        (filter (lambda (f)
                  (regexp-match? (regexp pattern) (path->string f)))
                html-files)
        html-files))

  ;; limit count
  (define limited
    (if limit
        (take filtered (min limit (length filtered)))
        filtered))

  ;; pair with reference JSONs, keeping only those that:
  ;; 1. have a matching reference JSON
  ;; 2. are classified as 'simple (inline-only, no <style> blocks)
  (filter-map
   (lambda (html-file)
     (define stem (path-replace-extension html-file #""))
     (define ref-file (build-path ref-dir
                                   (string-append (path->string stem) ".json")))
     (define html-path (build-path html-dir html-file))
     (and (file-exists? ref-file)
          (eq? (classify-html-test (path->string html-path)) 'simple)
          (cons (path->string html-path) (path->string ref-file))))
   limited))

;; ============================================================
;; Single Test Runner
;; ============================================================

;; run a single differential test.
;; returns: (list test-name passed? compare-result-or-error-string)
(define (run-single-test html-path ref-path config)
  (define test-name
    (path->string (file-name-from-path (string->path html-path))))

  (with-handlers
    ([exn:fail?
      (lambda (e)
        (list test-name #f (format "ERROR: ~a" (exn-message e))))])

    ;; 1. build test case
    (define tc (reference-file->test-case html-path ref-path))
    (define box-tree (reference-test-case-box-tree tc))
    (define expected (reference-test-case-expected tc))
    (define vp (reference-test-case-viewport tc))

    ;; 2. run Redex layout
    (define view (layout-document box-tree (car vp) (cdr vp)))

    ;; 3. compare
    (define result (compare-layouts view expected config))

    (list test-name (compare-result-passed? result) result)))

;; ============================================================
;; Batch Runner
;; ============================================================

(define (run-batch tests config #:verbose? [verbose? #f])
  (define total (length tests))
  (define passed 0)
  (define failed 0)
  (define errors 0)
  (define failure-details '())

  (for ([pair (in-list tests)]
        [i (in-naturals 1)])
    (define html-path (car pair))
    (define ref-path (cdr pair))
    (define result (run-single-test html-path ref-path config))
    (define test-name (car result))
    (define ok? (cadr result))
    (define detail (caddr result))

    (cond
      [(string? detail)
       ;; error
       (set! errors (add1 errors))
       (set! failure-details
             (cons (format "[ERROR] ~a: ~a" test-name detail) failure-details))
       (when verbose?
         (printf "  [~a/~a] ERROR ~a: ~a\n" i total test-name detail))]
      [ok?
       (set! passed (add1 passed))
       (when verbose?
         (printf "  [~a/~a] PASS  ~a (~a elements)\n"
                 i total test-name (compare-result-total detail)))]
      [else
       (set! failed (add1 failed))
       (set! failure-details
             (cons (format "[FAIL] ~a:\n~a"
                           test-name (compare-result-summary detail))
                   failure-details))
       (when verbose?
         (printf "  [~a/~a] FAIL  ~a\n~a\n"
                 i total test-name (compare-result-summary detail)))]))

  ;; print summary
  (printf "\n========================================\n")
  (printf "Differential Test Results\n")
  (printf "========================================\n")
  (printf "Total:  ~a\n" total)
  (printf "Passed: ~a\n" passed)
  (printf "Failed: ~a\n" failed)
  (printf "Errors: ~a\n" errors)
  (printf "========================================\n")

  (when (> (+ failed errors) 0)
    (printf "\nDetails:\n")
    (for ([d (in-list (reverse failure-details))])
      (printf "~a\n\n" d)))

  ;; return counts
  (values passed failed errors))

;; ============================================================
;; Main Entry Point
;; ============================================================

(module+ main
  (define filter-pattern (make-parameter #f))
  (define limit (make-parameter #f))
  (define verbose? (make-parameter #f))
  (define base-tol (make-parameter 5))
  (define prop-tol (make-parameter 0.03))

  (command-line
   #:program "test-differential"
   #:once-each
   ["--filter" pat "Filter test names by pattern"
    (filter-pattern pat)]
   ["--limit" n "Maximum number of tests to run"
    (limit (string->number n))]
   ["--verbose" "Show per-test details"
    (verbose? #t)]
   ["--base-tolerance" t "Base tolerance in px (default: 5)"
    (base-tol (string->number t))]
   ["--proportional-tolerance" t "Proportional tolerance fraction (default: 0.03)"
    (prop-tol (string->number t))])

  (define config
    (make-compare-config
     #:base-tolerance (base-tol)
     #:proportional-tolerance (prop-tol)))

  (printf "Discovering tests...\n")
  (define tests
    (discover-tests #:filter-pattern (filter-pattern)
                    #:limit (limit)))
  (printf "Found ~a simple baseline tests\n\n" (length tests))

  (when (null? tests)
    (printf "No tests found! Check paths.\n")
    (printf "  HTML dir: ~a\n" (baseline-html-dir))
    (printf "  Ref dir:  ~a\n" (reference-dir))
    (exit 1))

  (define-values (passed failed errors)
    (run-batch tests config #:verbose? (verbose?)))

  (exit (if (and (= failed 0) (= errors 0)) 0 1)))
