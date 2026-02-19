#lang racket
;; ============================================================================
;; Lambda Script — C Representation Model (PLT Redex)
;;
;; Models the C-level representation layer used by the transpiler.
;; This is where transpiler bugs live: mismatches between Lambda's
;; semantic types and the C types used for JIT compilation.
;;
;; The transpiler maps:
;;   Lambda type τ  →  C type (via write_type() in print.cpp)
;;   Lambda value v →  C representation (Item, String*, int32_t, etc.)
;;
;; Boxing: converting native C values to the universal Item type
;; Unboxing: extracting native C values from Item
;;
;; Verified against: lambda/transpile.cpp, lambda/lambda-data.hpp,
;;                   lambda/print.cpp (write_type), lambda/mir.c
;; ============================================================================

(require "lambda-core.rkt")
(require "lambda-types.rkt")


;; ────────────────────────────────────────────────────────────────────────────
;; 1. C TYPE DOMAIN
;; ────────────────────────────────────────────────────────────────────────────
;; These are the actual C types the transpiler generates.
;; From write_type() in print.cpp and TypeInfo in transpile.cpp.

;; C-type is one of:
;; - 'Item         : 64-bit tagged union (universal type, uint64_t)
;; - 'int32_t      : unboxed 32-bit signed integer
;; - 'int64_t      : unboxed 64-bit signed integer
;; - 'double       : unboxed 64-bit IEEE 754
;; - 'bool         : unboxed boolean (C++ bool / uint8_t)
;; - 'String*      : pointer to String struct (refcounted)
;; - 'Symbol*      : pointer to Symbol struct (interned)
;; - 'Array*       : pointer to Array struct (Container)
;; - 'List*        : pointer to List struct (Container)
;; - 'Map*         : pointer to Map struct (Container)
;; - 'Element*     : pointer to Element struct (Container)
;; - 'DateTime     : inline DateTime struct
;; - 'Decimal*     : pointer to Decimal struct
;; - 'void         : no return value (procedures)


;; ────────────────────────────────────────────────────────────────────────────
;; 2. TYPE MAPPING: Lambda τ → C-type
;; ────────────────────────────────────────────────────────────────────────────
;; Corresponds to write_type() in print.cpp
;; This is the fundamental mapping that must be consistent throughout
;; the transpiler.

(define (lambda-type->c-type τ)
  (match τ
    ['int-type      'int32_t]
    ['int64-type    'int64_t]
    ['float-type    'double]
    ['bool-type     'bool]
    ['string-type   'String*]
    ['binary-type   'String*]     ; binary uses same C layout as String
    ['symbol-type   'Symbol*]
    ['decimal-type  'Decimal*]
    ['datetime-type 'DateTime]
    ['array-type    'Array*]
    ['list-type     'List*]
    ['map-type      'Map*]
    ['element-type  'Element*]
    ['func-type     'Item]        ; functions are always boxed as Item
    ['type-type     'Item]        ; type values are always Item
    ['null-type     'Item]        ; null is represented as Item
    ['error-type    'Item]        ; errors are always Item
    ['any-type      'Item]        ; any/unknown → Item (universal)
    ['number-type   'Item]        ; abstract number → Item
    ['range-type    'Item]        ; ranges are Item (Container*)
    ['path-type     'Item]        ; paths are Item

    ;; nullable → Item (can't be unboxed since it might be null)
    [`(nullable ,_) 'Item]

    ;; union → Item (can't statically determine which branch)
    [`(union ,_ ,_) 'Item]

    ;; function type → Item
    [`(fn-type ,_ ,_) 'Item]

    ;; error return → same as inner type (error is encoded in Item)
    [`(error-ret ,inner) (lambda-type->c-type inner)]
    [`(error-ret-typed ,inner ,_) (lambda-type->c-type inner)]

    ;; typed arrays/lists/maps → pointer types
    [`(array-of ,_) 'Array*]
    [`(list-of ,_ ...) 'List*]
    [`(map-of ,_ ...) 'Map*]

    ;; default → Item
    [_ 'Item]))


;; ────────────────────────────────────────────────────────────────────────────
;; 3. C-TYPE PREDICATES
;; ────────────────────────────────────────────────────────────────────────────

;; Is this a pointer type? (vs inline/scalar)
(define (pointer-c-type? ct)
  (member ct '(String* Symbol* Array* List* Map* Element* Decimal*)))

;; Is this an inline scalar type? (packed in registers, not pointers)
(define (scalar-c-type? ct)
  (member ct '(int32_t int64_t double bool)))

;; Is this the universal Item type?
(define (item-c-type? ct)
  (eq? ct 'Item))


;; ────────────────────────────────────────────────────────────────────────────
;; 4. BOXING OPERATIONS
;; ────────────────────────────────────────────────────────────────────────────
;; Boxing: converting a typed C value to Item
;; These correspond to the C macros/functions in lambda.h / lambda.hpp
;;
;; i2it(int32_t)    → Item   (inline: pack value + type tag)
;; push_l(int64_t)  → Item   (store in num_stack, return tagged pointer)
;; push_d(double)   → Item   (store in num_stack, return tagged pointer)
;; b2it(bool)       → Item   (inline: BoolTrue or BoolFalse constant)
;; s2it(String*)    → Item   (tag pointer with STRING type_id)
;; sym2it(Symbol*)  → Item   (tag pointer with SYMBOL type_id)

(define (boxing-function c-type)
  (match c-type
    ['int32_t   'i2it]
    ['int64_t   'push_l]
    ['double    'push_d]
    ['bool      'b2it]
    ['String*   's2it]
    ['Symbol*   'sym2it]
    ['Array*    #f]       ; containers are already pointer-compatible with Item
    ['List*     #f]
    ['Map*      #f]
    ['Element*  #f]
    ['DateTime  'dt2it]
    ['Decimal*  'dec2it]
    ['Item      #f]       ; already Item, no boxing needed
    [_          #f]))


;; ────────────────────────────────────────────────────────────────────────────
;; 5. UNBOXING OPERATIONS
;; ────────────────────────────────────────────────────────────────────────────
;; Unboxing: extracting a typed C value from Item
;; These correspond to the C functions in lambda.h / lambda.hpp
;;
;; it2i(Item) → int32_t   (extract from inline tag)
;; it2l(Item) → int64_t   (dereference num_stack pointer)
;; it2d(Item) → double    (dereference num_stack pointer)
;; it2b(Item) → bool      (extract boolean)
;; it2s(Item) → String*   (untag pointer)

(define (unboxing-function c-type)
  (match c-type
    ['int32_t   'it2i]
    ['int64_t   'it2l]
    ['double    'it2d]
    ['bool      'it2b]
    ['String*   'it2s]
    ['Symbol*   'it2sym]
    ['DateTime  'it2dt]
    ['Decimal*  'it2dec]
    ['Item      #f]       ; already Item, no unboxing needed
    [_          #f]))


;; ────────────────────────────────────────────────────────────────────────────
;; 6. TRANSPILER CONVERSION RULES
;; ────────────────────────────────────────────────────────────────────────────
;; These rules define what conversion the transpiler MUST emit when
;; a value with C-type `source` needs to be used where C-type `target`
;; is expected. A #f means no conversion needed. A symbol names the
;; C function to call.
;;
;; This is the core specification that catches Issues #15, #16, etc.

(define (required-conversion source target)
  (cond
    ;; Same type → no conversion
    [(eq? source target) #f]

    ;; Source is Item → unbox to target
    [(eq? source 'Item)
     (unboxing-function target)]

    ;; Target is Item → box from source
    [(eq? target 'Item)
     (boxing-function source)]

    ;; Source is pointer, target is pointer of different type → must box+unbox
    ;; This shouldn't happen in well-typed Lambda code
    [(and (pointer-c-type? source) (pointer-c-type? target))
     'type-error]

    ;; Source is scalar, target is different scalar → must box then unbox
    ;; e.g., int32_t → double: use (it2d (i2it value))
    ;; The transpiler may optimize this to a direct C cast
    [(and (scalar-c-type? source) (scalar-c-type? target))
     (match* (source target)
       ;; int → double: use (double) cast
       [('int32_t 'double) 'cast-int-to-double]
       ;; int → int64: use (int64_t) cast
       [('int32_t 'int64_t) 'cast-int-to-int64]
       ;; double → int: use (int32_t) cast (truncate)
       [('double 'int32_t) 'cast-double-to-int]
       ;; int64 → double: use (double) cast
       [('int64_t 'double) 'cast-int64-to-double]
       [(_ _) 'type-error])]

    ;; Pointer to Item: containers are pointer-compatible
    ;; Array*, List*, Map*, Element* can be directly cast to Item (uint64_t)
    ;; because their type_id is stored in the first byte of the struct
    [(and (pointer-c-type? source) (eq? target 'Item))
     #f]  ; compatible without explicit conversion

    ;; Item to pointer: for containers, just cast
    [(and (eq? source 'Item) (pointer-c-type? target))
     (unboxing-function target)]

    [else 'type-error]))


;; ────────────────────────────────────────────────────────────────────────────
;; 7. CALL SITE VERIFICATION
;; ────────────────────────────────────────────────────────────────────────────
;; Verifies that a function call site correctly converts argument types.
;; This is the check that would have caught Issue #16.
;;
;; For each (arg-type, param-type) pair:
;;   1. Compute arg C-type and param C-type
;;   2. If they differ, a conversion is required
;;   3. If the required conversion is 'type-error, it's a transpiler bug

(define (verify-call-args arg-types param-types)
  (for/list ([arg-τ arg-types]
             [param-τ param-types]
             [idx (in-naturals)])
    (define arg-ct (lambda-type->c-type arg-τ))
    (define param-ct (lambda-type->c-type param-τ))
    (define conv (required-conversion arg-ct param-ct))
    (list idx arg-τ param-τ arg-ct param-ct conv
          (cond
            [(not conv) 'ok]
            [(eq? conv 'type-error) 'ERROR]
            [else 'needs-conversion]))))


;; ────────────────────────────────────────────────────────────────────────────
;; 8. IF-ELSE BRANCH VERIFICATION
;; ────────────────────────────────────────────────────────────────────────────
;; Verifies that if-else branches produce compatible C types.
;; This is the check that would have caught Issue #15.
;;
;; Rule: Both branches of a C ternary must have the SAME C type.
;; If they differ, both must be boxed to Item.

(define (verify-if-branches then-τ else-τ)
  (define then-ct (lambda-type->c-type then-τ))
  (define else-ct (lambda-type->c-type else-τ))
  (cond
    ;; Same C-type → OK, ternary is valid
    [(eq? then-ct else-ct)
     (list 'ok then-ct else-ct)]

    ;; Different C-types → both must be boxed to Item
    ;; The transpiler must NOT emit a raw ternary with mismatched types
    [else
     (list 'must-box-both then-ct else-ct
           (format "then-branch is ~a, else-branch is ~a → box both to Item"
                   then-ct else-ct))]))


;; ────────────────────────────────────────────────────────────────────────────
;; 9. UNBOXED FUNCTION VARIANT VERIFICATION
;; ────────────────────────────────────────────────────────────────────────────
;; The transpiler generates "_u" (unboxed) function variants for performance.
;; These return a native C type instead of Item.
;;
;; Rule: Only safe for types where the C type can represent all possible
;; return values, AND the function body actually produces that C type.
;;
;; Currently safe: int (int32_t), int64 (int64_t)
;; NOT safe: string (String*), float (double via num_stack), etc.
;; because system functions may return Item even when the semantic type
;; is string/float.

(define (safe-for-unboxed-variant? ret-τ)
  (and (member ret-τ '(int-type int64-type)) #t))


;; ────────────────────────────────────────────────────────────────────────────
;; 10. SYSTEM FUNCTION C-TYPE CATALOG
;; ────────────────────────────────────────────────────────────────────────────
;; Maps system function names to their ACTUAL C return types.
;; This may differ from the semantic return type!
;;
;; Known discrepancies (the root cause of Issue #16):
;;   fn_string()    → String*  (matches STRING semantic type)
;;   fn_trim()      → Item     (MISMATCHES STRING semantic type)
;;   fn_lower()     → Item     (MISMATCHES STRING semantic type)
;;   fn_upper()     → Item     (MISMATCHES STRING semantic type)
;;   fn_str_join()  → Item     (MISMATCHES STRING semantic type)
;;   fn_replace()   → Item     (MISMATCHES STRING semantic type)
;;
;; The transpiler must use the ACTUAL C return type, not the semantic type,
;; when determining whether boxing/unboxing is needed.

(define sys-func-c-returns
  (hash
   ;; String functions with matching C returns
   'fn_string    'String*
   'fn_symbol    'Symbol*

   ;; String functions with Item return (semantic type is string but C returns Item)
   'fn_trim      'Item
   'fn_lower     'Item
   'fn_upper     'Item
   'fn_str_join  'Item
   'fn_replace   'Item
   'fn_substr    'Item
   'fn_normalize 'Item

   ;; Numeric functions
   'fn_int       'int32_t    ; sometimes Item
   'fn_float     'double     ; sometimes Item
   'fn_abs       'Item
   'fn_round     'Item
   'fn_floor     'Item
   'fn_ceil      'Item

   ;; Collection functions
   'fn_len       'int32_t
   'fn_sum       'Item
   'fn_sort      'Item
   'fn_reverse   'Item
   'fn_unique    'Item
   'fn_take      'Item
   'fn_drop      'Item

   ;; Arithmetic operators
   'fn_add       'Item
   'fn_sub       'Item
   'fn_mul       'Item
   'fn_div       'Item
   'fn_mod       'Item
   'fn_pow       'Item

   ;; Comparison
   'fn_eq        'bool       ; returns Bool (3-state, but bool in C)
   'fn_lt        'bool
   'fn_gt        'bool
   'fn_le        'bool
   'fn_ge        'bool

   ;; Logical
   'fn_and       'Item       ; returns operand value, not bool
   'fn_or        'Item

   ;; Type checking
   'fn_is        'bool
   'fn_type      'Item       ; returns type value as Item
   ))


;; ────────────────────────────────────────────────────────────────────────────
;; 11. COMPREHENSIVE VERIFICATION
;; ────────────────────────────────────────────────────────────────────────────
;; Run all verification checks on a function definition.

(define (verify-function name params ret-τ body-expr-types)
  (define issues '())

  ;; Check 1: Parameter types have valid C representations
  (for ([p params])
    (define pt (param-type p))
    (define ct (lambda-type->c-type pt))
    (when (eq? ct 'type-error)
      (set! issues (cons (format "param ~a has no valid C type for ~a"
                                 (param-name p) pt)
                         issues))))

  ;; Check 2: Return type is safe for unboxed variant
  (define ret-ct (lambda-type->c-type ret-τ))
  (when (and (pointer-c-type? ret-ct)
             (not (safe-for-unboxed-variant? ret-τ)))
    (set! issues (cons (format "return type ~a (~a) not safe for _u variant"
                               ret-τ ret-ct)
                       issues)))

  ;; Return results
  (list name
        (if (null? issues) 'ok 'has-issues)
        issues))


;; ────────────────────────────────────────────────────────────────────────────
;; EXPORTS
;; ────────────────────────────────────────────────────────────────────────────

(provide lambda-type->c-type
         pointer-c-type? scalar-c-type? item-c-type?
         boxing-function unboxing-function
         required-conversion
         verify-call-args verify-if-branches
         safe-for-unboxed-variant?
         sys-func-c-returns
         verify-function)
