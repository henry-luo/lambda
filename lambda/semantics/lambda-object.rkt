#lang racket
;; ============================================================================
;; Lambda Script — Object Type System (PLT Redex)
;;
;; Provides the type registry and object operations for nominal typing,
;; single inheritance, methods, constraints, and object construction.
;;
;; This module has NO dependency on the evaluator (lambda-eval.rkt),
;; so both lambda-eval.rkt and lambda-proc.rkt can import it.
;;
;; Architecture:
;;   lambda-core.rkt
;;        ↑
;;   lambda-object.rkt  (this file — type registry)
;;        ↑
;;   lambda-eval.rkt  (imports core + object)
;;        ↑
;;   lambda-proc.rkt  (imports core + eval + object)
;;
;; Reference: doc/Lambda_Type.md, test/lambda/object*.ls
;; ============================================================================

(require racket/match)


;; ════════════════════════════════════════════════════════════════════════════
;; 1. STRUCTS
;; ════════════════════════════════════════════════════════════════════════════

;; A field specification in a type definition.
;; name: field name (symbol)
;; type: field type (symbol like 'int-type, 'string-type, etc.)
;; default: default value expression (#f if none)
;; constraint: constraint expression (#f if none) — ~ refers to field value
(struct field-spec (name type default constraint) #:transparent)

;; A method specification in a type definition.
;; name: method name (symbol)
;; kind: 'fn (pure) or 'pn (mutable)
;; params: list of parameter specs (same as function params)
;; body: method body expression
(struct method-spec (name kind params body) #:transparent)

;; A complete type definition.
;; name: type name (symbol)
;; parent: parent type name (symbol) or #f
;; fields: list of field-spec
;; methods: list of method-spec
;; constraints: list of object-level constraint expressions — ~ refers to self
(struct type-def (name parent fields methods constraints) #:transparent)


;; ════════════════════════════════════════════════════════════════════════════
;; 2. TYPE REGISTRY
;; ════════════════════════════════════════════════════════════════════════════

;; Global mutable hash: type-name (symbol) → type-def
(define *type-registry* (make-hash))

(define (clear-type-registry!)
  (hash-clear! *type-registry*))

(define (register-type! name parent fields methods constraints)
  (hash-set! *type-registry* name
    (type-def name parent fields methods constraints)))

(define (lookup-type name)
  (hash-ref *type-registry* name #f))

(define (type-registered? name)
  (hash-has-key? *type-registry* name))


;; ════════════════════════════════════════════════════════════════════════════
;; 3. INHERITANCE
;; ════════════════════════════════════════════════════════════════════════════

;; Check if type `child-name` inherits from `ancestor-name`.
;; Returns #t if child-name == ancestor-name or any parent chain reaches ancestor-name.
(define (type-inherits? child-name ancestor-name)
  (cond
    [(eq? child-name ancestor-name) #t]
    [else
     (define td (lookup-type child-name))
     (and td
          (type-def-parent td)
          (type-inherits? (type-def-parent td) ancestor-name))]))

;; Get all fields for a type, including inherited fields.
;; Inherited fields come first (parent → child order).
(define (all-fields-for-type type-name)
  (define td (lookup-type type-name))
  (cond
    [(not td) '()]
    [(type-def-parent td)
     (append (all-fields-for-type (type-def-parent td))
             (type-def-fields td))]
    [else (type-def-fields td)]))

;; Get all methods for a type, including inherited methods.
;; Child methods override parent methods with the same name.
(define (all-methods-for-type type-name)
  (define td (lookup-type type-name))
  (cond
    [(not td) '()]
    [(type-def-parent td)
     (define parent-methods (all-methods-for-type (type-def-parent td)))
     (define own-methods (type-def-methods td))
     (define own-names (map method-spec-name own-methods))
     ;; filter out parent methods overridden by child
     (define parent-filtered
       (filter (λ (m) (not (memq (method-spec-name m) own-names)))
               parent-methods))
     (append parent-filtered own-methods)]
    [else (type-def-methods td)]))

;; Get all constraints for a type, including inherited.
(define (all-constraints-for-type type-name)
  (define td (lookup-type type-name))
  (cond
    [(not td) '()]
    [(type-def-parent td)
     (append (all-constraints-for-type (type-def-parent td))
             (type-def-constraints td))]
    [else (type-def-constraints td)]))

;; Resolve a specific method by name, walking up the inheritance chain.
(define (resolve-method type-name method-name)
  (define all (all-methods-for-type type-name))
  (findf (λ (m) (eq? (method-spec-name m) method-name)) all))


;; ════════════════════════════════════════════════════════════════════════════
;; 4. OBJECT VALUE HELPERS
;; ════════════════════════════════════════════════════════════════════════════

(define (object-val? v)
  (match v [`(object-val ,_ ,_ ...) #t] [_ #f]))

(define (object-type-name v)
  (match v [`(object-val ,name ,_ ...) name] [_ #f]))

(define (object-fields v)
  (match v [`(object-val ,_ ,fields ...) fields] [_ '()]))

(define (object-field-ref v field-name)
  (define fields (object-fields v))
  (define pair (assq field-name fields))
  (if pair (cadr pair) 'null))

;; Check if a field has a default value.
;; Uses 'no-default as sentinel; #f is a valid default (boolean false).
(define (has-default? fs)
  (not (eq? (field-spec-default fs) 'no-default)))

;; Check if a field has a constraint.
;; Uses 'no-constraint as sentinel.
(define (has-constraint? fs)
  (not (eq? (field-spec-constraint fs) 'no-constraint)))


;; ════════════════════════════════════════════════════════════════════════════
;; EXPORTS
;; ════════════════════════════════════════════════════════════════════════════

(provide field-spec field-spec? field-spec-name field-spec-type
         field-spec-default field-spec-constraint
         method-spec method-spec? method-spec-name method-spec-kind
         method-spec-params method-spec-body
         type-def type-def? type-def-name type-def-parent
         type-def-fields type-def-methods type-def-constraints
         clear-type-registry! register-type! lookup-type type-registered?
         type-inherits?
         all-fields-for-type all-methods-for-type all-constraints-for-type
         resolve-method
         object-val? object-type-name object-fields object-field-ref
         has-default? has-constraint?)
