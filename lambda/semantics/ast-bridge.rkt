#lang racket/base
;; ast-bridge.rkt — Phase 4: Bridge between Lambda .ls files and Redex evaluator
;;
;; Calls ./lambda.exe --emit-sexpr to parse a Lambda script, reads the
;; resulting s-expression, converts it to a Redex-evaluable term, and
;; evaluates it using eval-lambda (functional) or run-pn-body (procedural).
;;
;; The output is a list of strings matching what Lambda would print.

(require racket/match
         racket/port
         racket/string
         racket/list
         racket/system
         "lambda-eval.rkt"
         "lambda-proc.rkt"
         "lambda-object.rkt")

(provide run-script-file
         read-sexpr-from-exe
         eval-functional-forms
         eval-procedural-forms
         classify-script
         lambda-exe-path)

;; ─── Configuration ─────────────────────────────────────────────────

;; Path to lambda.exe (relative to project root)
(define lambda-exe-path (make-parameter "../../lambda.exe"))

;; ─── Shell bridge ──────────────────────────────────────────────────

;; Call lambda.exe --emit-sexpr and read the resulting s-expression
(define (read-sexpr-from-exe script-path)
  (define exe (lambda-exe-path))
  (define-values (proc stdout stdin stderr)
    (subprocess #f #f #f exe "--emit-sexpr" script-path))
  (close-output-port stdin)
  ;; Read raw text first, sanitize escape sequences Racket doesn't understand
  (define raw-text (port->string stdout))
  (close-input-port stdout)
  ;; Replace \/ (JSON-style forward-slash escape) with / inside strings
  (define s1 (regexp-replace* #rx"\\\\/" raw-text "/"))
  ;; Replace \u{XXXX} braced unicode escapes with \uXXXX (4-digit) or \UXXXXXXXX (8-digit)
  (define (pad-hex hex width)
    (define s (string-upcase hex))
    (if (>= (string-length s) width) s
        (string-append (make-string (- width (string-length s)) #\0) s)))
  (define (convert-braced-unicode m)
    (define hex (cadr (regexp-match #rx"\\\\u\\{([0-9a-fA-F]+)\\}" m)))
    (define cp (string->number hex 16))
    (if (> cp #xFFFF)
        (format "\\U~a" (pad-hex (number->string cp 16) 8))
        (format "\\u~a" (pad-hex (number->string cp 16) 4))))
  (define s2 (regexp-replace* #rx"\\\\u\\{[0-9a-fA-F]+\\}" s1 convert-braced-unicode))
  ;; Replace surrogate pairs \uD800-\uDBFF \uDC00-\uDFFF with single \UXXXXXXXX
  (define (convert-surrogate-pair m)
    (define parts (regexp-match #rx"\\\\u([0-9a-fA-F]{4})\\\\u([0-9a-fA-F]{4})" m))
    (define hi (string->number (cadr parts) 16))
    (define lo (string->number (caddr parts) 16))
    (define cp (+ #x10000 (arithmetic-shift (- hi #xD800) 10) (- lo #xDC00)))
    (format "\\U~a" (pad-hex (number->string cp 16) 8)))
  (define s3 (regexp-replace* #rx"\\\\u([dD][89aAbB][0-9a-fA-F]{2})\\\\u([dD][cCdDeEfF][0-9a-fA-F]{2})" s2 convert-surrogate-pair))
  (define result (read (open-input-string s3)))
  (define err-text (port->string stderr))
  (close-input-port stderr)
  (subprocess-wait proc)
  (define exit-code (subprocess-status proc))
  (cond
    [(not (zero? exit-code))
     (error 'read-sexpr-from-exe
            "lambda.exe failed for ~a (exit ~a): ~a"
            script-path exit-code (string-trim err-text))]
    [(eof-object? result)
     (error 'read-sexpr-from-exe
            "No output from lambda.exe for ~a" script-path)]
    [else result]))

;; ─── Form classification helpers ───────────────────────────────────

(define (definition-form? f)
  (and (pair? f)
       (memq (car f) '(bind fn-def pn-def def-type def-type-alias
                        def-type-raw pattern-def view-def edit-view
                        bind-decompose))))

(define (let-null-form? f)
  ;; (let ((x expr) ...) null) — a let binding that serves as definition
  ;; (let-err val err expr null) — error destructuring definition
  (or (and (pair? f) (eq? (car f) 'let)
           (= (length f) 3)
           (eq? (caddr f) 'null))
      (and (pair? f) (eq? (car f) 'let-err)
           (= (length f) 5)
           (eq? (list-ref f 4) 'null))))

(define (def-fn-null-form? f)
  ;; (def-fn name (params) body null) — fn def inside content
  (and (pair? f) (eq? (car f) 'def-fn)
       (= (length f) 5)
       (eq? (list-ref f 4) 'null)))

(define (seq-form? f)
  (and (pair? f) (eq? (car f) 'seq)))

(define (content-seq-form? f)
  (and (pair? f) (eq? (car f) 'content-seq)))

;; ─── Wrap accumulated definitions around a body expression ─────────

(define (wrap-defs defs body)
  ;; If there are fn-defs, use seq-based wrapping for forward reference support
  (define has-fn-defs?
    (ormap (λ (d) (match d
                    [`(fn-def . ,_) #t]
                    [`(def-fn ,_ ,_ ,_ null) #t]
                    [_ #f])) defs))
  (if has-fn-defs?
      ;; Build a (seq ...) with all defs converted to seq-style statements, then body
      (let ([seq-stmts
             (map (λ (def)
                    (match def
                      [`(bind ,name ,expr) `(bind ,name ,expr)]
                      [`(fn-def ,name ,params ,fbody)
                       `(def-fn ,name ,params ,fbody null)]
                      [`(pn-def ,name ,params ,pbody)
                       `(pn-def ,name ,params ,pbody)]
                      [`(let ,bindings null) `(let ,bindings null)]
                      [`(def-fn ,name ,params ,fbody null)
                       `(def-fn ,name ,params ,fbody null)]
                      [`(def-type-alias ,name ,type-sym)
                       `(def-type-alias ,name ,type-sym)]
                      [`(def-type . ,_) def]
                      [`(def-type-raw . ,_) def]
                      [_ def]))
                  defs)])
        `(seq ,@seq-stmts ,body))
      ;; No fn-defs: use original foldr approach for simplicity
      (foldr
       (λ (def inner)
         (match def
           [`(bind ,name ,expr)
            `(let ((,name ,expr)) ,inner)]
           [`(pn-def ,name ,params ,pbody)
            `(def-pn ,name ,params ,pbody ,inner)]
           [`(let ,bindings null)
            `(let ,bindings ,inner)]
           [`(def-type-alias ,name ,type-sym)
            `(let ((,name (type-val ,type-sym))) ,inner)]
           [`(def-type . ,_) inner]
           [`(def-type-raw . ,_) inner]
           [`(pattern-def . ,_) inner]
           [`(view-def . ,_) inner]
           [`(edit-view . ,_) inner]
           [`(bind-decompose . ,_) inner]
           [_ inner]))
       body
       defs)))

;; ─── Object type registration from emitter output ──────────────────

;; Map type symbol to zero value (for fields without explicit :init)
(define (type-zero-value type-sym)
  (case type-sym
    [(int-type int64-type) 0]
    [(float-type) 0.0]
    [(string-type) ""]
    [(bool-type) #f]
    [else 'no-default]))

;; Parse a field spec from emitter: (name :default (type-val T) [:init expr])
;; or (name :default (constrained T constraint) [:init expr])
(define (parse-field-spec raw)
  (match raw
    ;; constrained with explicit init
    [`(,name :default (constrained ,type ,constraint) :init ,init-expr)
     (field-spec name type init-expr constraint)]
    ;; constrained without init
    [`(,name :default (constrained ,type ,constraint))
     (field-spec name type (type-zero-value type) constraint)]
    ;; typed with explicit init
    [`(,name :default (type-val ,type) :init ,init-expr)
     (field-spec name type init-expr 'no-constraint)]
    ;; typed without init
    [`(,name :default (type-val ,type))
     (field-spec name type (type-zero-value type) 'no-constraint)]
    ;; fallback: just field name
    [`(,name . ,_)
     (field-spec name 'any-type 'no-default 'no-constraint)]
    [_ (field-spec (car raw) 'any-type 'no-default 'no-constraint)]))

;; Parse a method spec from emitter: (fn name (params) body) or (pn name (params) body)
(define (parse-method-spec raw)
  (match raw
    [`(fn ,name ,params ,body)
     (method-spec name 'fn params body)]
    [`(pn ,name ,params ,body)
     (method-spec name 'pn params body)]
    [_ (error 'parse-method-spec "bad method: ~a" raw)]))

;; Parse keyword args from a def-type rest list
(define (parse-kw-args rest)
  (define result (make-hash))
  (let loop ([r rest])
    (cond
      [(or (null? r) (null? (cdr r))) result]
      [(symbol? (car r))
       (hash-set! result (car r) (cadr r))
       (loop (cddr r))]
      [else (loop (cdr r))])))

;; Register a single def-type form with the type registry
(define (register-type-from-form! form)
  (match form
    [`(def-type ,name . ,rest)
     (define kw (parse-kw-args rest))
     (define parent (hash-ref kw ':parent #f))
     (define raw-fields (hash-ref kw ':fields '()))
     (define raw-methods (hash-ref kw ':methods '()))
     (define raw-constraints (hash-ref kw ':constraints '()))
     (define fields (map parse-field-spec raw-fields))
     (define methods (map parse-method-spec raw-methods))
     (register-type! name parent fields methods raw-constraints)]
    [_ (void)]))

;; Walk all forms (flattening seqs) and register any def-type forms
(define (register-types-from-forms! forms)
  (for ([f (in-list forms)])
    (match f
      [`(def-type . ,_) (register-type-from-form! f)]
      [`(seq . ,stmts) (register-types-from-forms! stmts)]
      [_ (void)])))

;; ─── Normalize make-object with source → object-update ─────────────

;; Recursively transform (make-object Type source (field val) ...) where source
;; is a symbol into (object-update Type source (field val) ...)
(define (normalize-forms form)
  (match form
    ;; make-object with symbol source (update pattern)
    [`(make-object ,type ,first . ,rest)
     #:when (symbol? first)
     `(object-update ,type ,(normalize-forms first)
                     ,@(map normalize-forms rest))]
    ;; Any proper list: recurse into sub-forms
    [(? list?)
     (map normalize-forms form)]
    ;; Atom: return as-is
    [_ form]))

;; ─── Functional script evaluation ──────────────────────────────────

(define (eval-functional-forms forms)
  ;; Pre-process: register object types and normalize object update forms
  (clear-type-registry!)
  (register-types-from-forms! forms)
  (define nforms (normalize-forms forms))
  ;; Pre-pass: collect ALL definitions for forward reference support
  (define (collect-all-defs fs)
    (cond
      [(null? fs) '()]
      [(seq-form? (car fs))
       (append (collect-all-defs (cdr (car fs))) (collect-all-defs (cdr fs)))]
      [(or (definition-form? (car fs))
           (let-null-form? (car fs))
           (def-fn-null-form? (car fs)))
       (cons (car fs) (collect-all-defs (cdr fs)))]
      [else (collect-all-defs (cdr fs))]))
  (define all-defs (collect-all-defs nforms))
  ;; Walk forms left-to-right:
  ;; - Definition forms accumulate (will be wrapped around each evaluation)
  ;; - Bare expressions get wrapped with all preceding defs and evaluated
  ;; - Adjacent string results get concatenated (Lambda content block semantics)
  ;; Returns list of output strings.
  ;;
  ;; string-acc accumulates raw strings for concatenation. When a non-string
  ;; result is produced (or end of forms), the accumulator is flushed.
  (define (flush-acc acc outputs)
    (if (null? acc)
        outputs
        (let ([combined (apply string-append (reverse acc))])
          (cons (format "\"~a\"" combined) outputs))))

  (let loop ([fs nforms] [defs all-defs] [outputs '()] [string-acc '()])
    (cond
      [(null? fs)
       (define result (reverse (flush-acc string-acc outputs)))
       ;; Lambda outputs "null" for scripts with only definitions
       (if (null? result) (list "null") result)]
      ;; seq wrapper from CONTENT node — flatten
      [(seq-form? (car fs))
       (loop (append (cdr (car fs)) (cdr fs)) defs outputs string-acc)]
      ;; definition forms — accumulate (no output, don't flush)
      [(definition-form? (car fs))
       (loop (cdr fs) (append defs (list (car fs))) outputs string-acc)]
      [(let-null-form? (car fs))
       (loop (cdr fs) (append defs (list (car fs))) outputs string-acc)]
      [(def-fn-null-form? (car fs))
       (loop (cdr fs) (append defs (list (car fs))) outputs string-acc)]
      [else
       ;; bare expression — wrap with accumulated defs and evaluate
       (with-handlers
         ([exn:fail?
           (λ (exn)
             (define err-str (format "EVAL-ERROR: ~a" (exn-message exn)))
             (loop (cdr fs) defs
                   (cons err-str (flush-acc string-acc outputs))
                   '()))])
         (define form (car fs))
         ;; handle print() calls in functional mode: evaluate arg, output raw
         (cond
           [(and (pair? form) (eq? (car form) 'print))
            (define arg-expr (cadr form))
            (define wrapped (wrap-defs defs arg-expr))
            (define val (eval-lambda '() wrapped))
            (define str (match val
                          [(? string?) val]
                          [`(sym ,name) name]
                          [_ (value->string val)]))
            ;; print always outputs as separate line
            (loop (cdr fs) defs
                  (cons str (flush-acc string-acc outputs))
                  '())]
           [else
            (let* ([wrapped (wrap-defs defs form)]
                   [val (eval-lambda '() wrapped)])
              ;; For-loop at content level: expand array items individually
              (cond
                [(and (pair? form) (eq? (car form) 'for) (array-val? val))
                 (define items (array-items val))
                 ;; For-loop items participate in string accumulation (matching C behavior)
                 (define-values (new-outputs new-acc)
                   (for/fold ([outs outputs] [acc string-acc]) ([item items])
                     (cond
                       [(string? item) (values outs (cons item acc))]
                       [(eq? item 'null) (values outs acc)]
                       [else (values (cons (value->string item) (flush-acc acc outs)) '())])))
                 (loop (cdr fs) defs new-outputs new-acc)]
                ;; List-val: expand each item through same output logic
                ;; (strings accumulate, non-strings flush — matching C's output concatenation)
                [(list-val? val)
                 (define items (list-items val))
                 (define-values (new-outputs new-acc)
                   (for/fold ([outs outputs] [acc string-acc]) ([item items])
                     (cond
                       [(string? item) (values outs (cons item acc))]
                       [(eq? item 'null) (values (flush-acc acc outs) '())]
                       [else (values (cons (value->string item) (flush-acc acc outs)) '())])))
                 (loop (cdr fs) defs new-outputs new-acc)]
                ;; String results accumulate; non-strings flush
                [(string? val)
                 (loop (cdr fs) defs outputs (cons val string-acc))]
                ;; Null results are suppressed (not printed by C impl)
                ;; Don't flush accumulator — null is invisible, strings keep concatenating
                [(eq? val 'null)
                 (loop (cdr fs) defs outputs string-acc)]
                [else
                 (let ([str (value->string val)])
                   (loop (cdr fs) defs
                         (cons str (flush-acc string-acc outputs))
                         '()))]))]))])))

;; helper: format print output (raw strings/symbols, value->string for others)
(define (print-format val)
  (match val
    [(? string?) val]
    [`(sym ,name) name]
    [_ (value->string val)]))
;; ─── Procedural script evaluation ──────────────────────────────────

(define (eval-procedural-forms forms)
  ;; Pre-process: register object types and normalize object update forms
  (clear-type-registry!)
  (register-types-from-forms! forms)
  (define nforms (normalize-forms forms))
  ;; Find pn main(), collect all other definitions, evaluate main body.
  ;; Returns list of output strings.
  (define all-forms (flatten-seqs nforms))
  (define main-body #f)
  (define other-defs '())

  (for ([f (in-list all-forms)])
    (match f
      [`(pn-def main ,params ,body)
       (set! main-body body)]
      [(? definition-form?)
       (set! other-defs (append other-defs (list f)))]
      [(? let-null-form?)
       (set! other-defs (append other-defs (list f)))]
      [(? def-fn-null-form?)
       (set! other-defs (append other-defs (list f)))]
      [_ (void)]))

  (cond
    [(not main-body)
     (list "EVAL-ERROR: No pn main() found")]
    [else
     (with-handlers
       ([exn:fail?
         (λ (exn) (list (format "EVAL-ERROR: ~a" (exn-message exn))))])
       ;; Convert top-level defs to proc statements and prepend to main body
       (define def-stmts
         (apply append
           (map (λ (d)
                  (match d
                    [`(bind ,name ,expr) (list `(let ((,name ,expr)) null))]
                    [`(fn-def ,name ,params ,fbody) (list `(def-pn ,name ,params ,fbody))]
                    [`(pn-def ,name ,params ,pbody) (list `(def-pn ,name ,params ,pbody))]
                    [`(let ,bindings null) (list d)]
                    [`(def-fn ,name ,params ,fbody null) (list `(def-pn ,name ,params ,fbody))]
                    [_ '()]))
                other-defs)))
       ;; Prepend def statements to the main body seq
       (define body-stmts
         (match main-body
           [`(seq ,stmts ...) stmts]
           [_ (list main-body)]))
       (define full-body `(seq ,@def-stmts ,@body-stmts))
       ;; Set up proc-apply callback so eval-lambda can dispatch pn-closures
       (define (proc-apply-callback σ ρ fn-val args)
         (define r (apply-proc-closure σ ρ fn-val args '()))
         (result-value r))
       (define-values (val out)
         (parameterize ([current-proc-apply proc-apply-callback])
           (run-proc full-body)))
       (if (non-empty-string? out)
           (string-split out "\n")
           (list (value->string val))))]))
;; Flatten nested seq forms
(define (flatten-seqs forms)
  (let loop ([fs forms] [result '()])
    (cond
      [(null? fs) (reverse result)]
      [(seq-form? (car fs))
       (loop (append (cdr (car fs)) (cdr fs)) result)]
      [else
       (loop (cdr fs) (cons (car fs) result))])))

;; ─── Script classification ─────────────────────────────────────────

;; Classify a script: can the Redex model handle it?
;; Returns: 'feasible, 'has-imports, 'unsupported, or a specific reason.
(define (classify-script sexpr)
  (unless (and (pair? sexpr) (eq? (car sexpr) 'script))
    (values 'invalid "not a (script ...) form"))
  (define mode (cadr sexpr))
  (define forms (cddr sexpr))
  (define flat (flatten-seqs forms))

  ;; Check for unsupported forms
  (define has-unsupported
    (for/or ([f (in-list flat)])
      (and (pair? f)
           (let ([tag (symbol->string (car f))])
             (or (string-prefix? tag "unsupported")
                 (string=? tag "unsupported-import"))))))

  (cond
    [has-unsupported 'unsupported]
    [else 'feasible]))

;; ─── Main entry point ──────────────────────────────────────────────

(define (run-script-file script-path)
  (define sexpr (read-sexpr-from-exe script-path))
  (unless (and (pair? sexpr) (eq? (car sexpr) 'script))
    (error 'run-script-file "Invalid s-expression from ~a" script-path))
  (define mode (cadr sexpr))
  (define forms (cddr sexpr))
  (define outputs
    (if (string=? mode "pn")
        (eval-procedural-forms forms)
        (eval-functional-forms forms)))
  ;; Join outputs with newlines; if no outputs, script result is "null"
  (if (null? outputs) "null" (string-join outputs "\n")))
