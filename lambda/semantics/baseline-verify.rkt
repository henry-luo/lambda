#lang racket/base
;; baseline-verify.rkt — Phase 4: Test baseline verification harness
;;
;; Iterates test/lambda/*.ls files, runs each through the ast-bridge,
;; and compares Redex evaluator output against expected .txt files.
;;
;; Usage:
;;   racket baseline-verify.rkt                  ; run all tests
;;   racket baseline-verify.rkt --verbose        ; show details
;;   racket baseline-verify.rkt --file NAME.ls   ; run single test
;;   racket baseline-verify.rkt --summary        ; summary only

(require racket/cmdline
         racket/path
         racket/string
         racket/list
         racket/port
         racket/file
         "ast-bridge.rkt")

;; ─── Configuration ─────────────────────────────────────────────────

(define test-dir "../../test/lambda")
(define verbose? (make-parameter #f))
(define single-file (make-parameter #f))

;; ─── Test result structure ─────────────────────────────────────────

(struct test-result (name status detail) #:transparent)
;; status: 'pass, 'fail, 'eval-error, 'skip-imports, 'skip-unsupported, 'skip-no-txt, 'emit-error

;; ─── Find test files ───────────────────────────────────────────────

(define (find-test-files)
  (define base (build-path test-dir))
  (define ls-files
    (for/list ([f (in-list (directory-list base #:build? #t))]
               #:when (path-has-extension? f #".ls"))
      f))
  ;; Also check proc/ subdirectory
  (define proc-dir (build-path test-dir "proc"))
  (define proc-files
    (if (directory-exists? proc-dir)
        (for/list ([f (in-list (directory-list proc-dir #:build? #t))]
                   #:when (path-has-extension? f #".ls"))
          f)
        '()))
  (sort (append ls-files proc-files) path<?))

;; ─── Run a single test ─────────────────────────────────────────────

(define (run-test ls-path)
  (define name (path->string (file-name-from-path ls-path)))
  (define txt-path
    (path-replace-extension ls-path #".txt"))

  ;; Check for expected output file
  (unless (file-exists? txt-path)
    (return-result name 'skip-no-txt "no .txt file"))

  (define expected
    (string-trim (file->string txt-path)))

  ;; Try to get s-expression from lambda.exe
  (define sexpr
    (with-handlers
      ([exn:fail?
        (λ (exn)
          (return-result name 'emit-error (exn-message exn)))])
      (read-sexpr-from-exe (path->string ls-path))))

  (when (test-result? sexpr)
    sexpr) ;; early return from error

  ;; Classify
  (define class (classify-script sexpr))
  (when (eq? class 'unsupported)
    (return-result name 'skip-unsupported "has unsupported forms"))

  ;; Evaluate
  (define mode (cadr sexpr))
  (define forms (cddr sexpr))
  (define outputs
    (with-handlers
      ([exn:fail?
        (λ (exn)
          (return-result name 'eval-error (exn-message exn)))])
      (if (string=? mode "pn")
          (eval-procedural-forms forms)
          (eval-functional-forms forms))))

  (when (test-result? outputs)
    outputs) ;; early return from error

  (define actual (string-join outputs "\n"))

  (if (string=? actual expected)
      (test-result name 'pass "")
      (test-result name 'fail
                   (format "Expected:\n~a\nActual:\n~a"
                           (truncate-str expected 200)
                           (truncate-str actual 200)))))

;; Helper to use return-style with test-result
(define-syntax-rule (return-result name status detail)
  (test-result name status detail))

;; Truncate string for display
(define (truncate-str s max-len)
  (if (<= (string-length s) max-len)
      s
      (string-append (substring s 0 max-len) "...")))

;; ─── Safer test runner (catches all errors) ───────────────────────

(define (safe-run-test ls-path)
  (define name (path->string (file-name-from-path ls-path)))
  (define txt-path (path-replace-extension ls-path #".txt"))

  ;; Check for expected output file
  (cond
    [(not (file-exists? txt-path))
     (test-result name 'skip-no-txt "no .txt file")]
    [else
     (with-handlers
       ([exn:fail?
         (λ (exn) (test-result name 'eval-error (exn-message exn)))])
       (define expected (string-trim (file->string txt-path)))

       ;; Try to get s-expression
       (define sexpr (read-sexpr-from-exe (path->string ls-path)))

       ;; Classify
       (define class (classify-script sexpr))
       (cond
         [(eq? class 'unsupported)
          (test-result name 'skip-unsupported "has unsupported forms")]
         [else
          ;; Evaluate
          (define mode (cadr sexpr))
          (define forms (cddr sexpr))
          (define outputs
            (if (string=? mode "pn")
                (eval-procedural-forms forms)
                (eval-functional-forms forms)))
          (define actual (string-join outputs "\n"))

          (if (string=? actual expected)
              (test-result name 'pass "")
              (test-result name 'fail
                           (format "Expected:\n~a\nActual:\n~a"
                                   (truncate-str expected 300)
                                   (truncate-str actual 300))))]))]))

;; ─── Main ──────────────────────────────────────────────────────────

(define (main)
  (command-line
   #:program "baseline-verify"
   #:once-each
   [("--verbose" "-v") "Show detailed results"
    (verbose? #t)]
   [("--file" "-f") name "Run single test file"
    (single-file name)]
   #:args ()
   (void))

  (printf "Lambda Semantics Baseline Verification (Phase 4)\n")
  (printf "================================================\n\n")

  ;; Find tests
  (define test-files
    (if (single-file)
        (let ([p (build-path test-dir (single-file))])
          (if (file-exists? p)
              (list p)
              (begin
                (printf "File not found: ~a\n" p)
                '())))
        (find-test-files)))

  (printf "Found ~a test files\n\n" (length test-files))

  ;; Run tests
  (define results
    (for/list ([f (in-list test-files)])
      (when (verbose?)
        (printf "  Testing: ~a ... " (path->string (file-name-from-path f))))
      (define r (safe-run-test f))
      (when (verbose?)
        (printf "~a\n"
                (case (test-result-status r)
                  [(pass) "PASS"]
                  [(fail) "FAIL"]
                  [(eval-error) (format "EVAL-ERROR: ~a"
                                        (truncate-str (test-result-detail r) 60))]
                  [(skip-no-txt) "SKIP (no .txt)"]
                  [(skip-unsupported) "SKIP (unsupported)"]
                  [(emit-error) (format "EMIT-ERROR: ~a"
                                        (truncate-str (test-result-detail r) 60))]
                  [else "???"])))
      r))

  ;; Summary
  (define pass-count (count (λ (r) (eq? (test-result-status r) 'pass)) results))
  (define fail-count (count (λ (r) (eq? (test-result-status r) 'fail)) results))
  (define eval-error-count (count (λ (r) (eq? (test-result-status r) 'eval-error)) results))
  (define emit-error-count (count (λ (r) (eq? (test-result-status r) 'emit-error)) results))
  (define skip-no-txt-count (count (λ (r) (eq? (test-result-status r) 'skip-no-txt)) results))
  (define skip-unsup-count (count (λ (r) (eq? (test-result-status r) 'skip-unsupported)) results))

  (printf "\n")
  (printf "=== Summary ===\n")
  (printf "  Total:          ~a\n" (length results))
  (printf "  Pass:           ~a\n" pass-count)
  (printf "  Fail:           ~a\n" fail-count)
  (printf "  Eval errors:    ~a\n" eval-error-count)
  (printf "  Emit errors:    ~a\n" emit-error-count)
  (printf "  Skip (no .txt): ~a\n" skip-no-txt-count)
  (printf "  Skip (unsup.):  ~a\n" skip-unsup-count)

  ;; Show failures if not verbose (verbose already showed them)
  (when (and (not (verbose?)) (> fail-count 0))
    (printf "\n--- Failures ---\n")
    (for ([r (in-list results)]
          #:when (eq? (test-result-status r) 'fail))
      (printf "\n~a:\n~a\n" (test-result-name r) (test-result-detail r))))

  ;; Exit code
  (exit (if (and (zero? fail-count) (> pass-count 0)) 0 1)))

(main)
