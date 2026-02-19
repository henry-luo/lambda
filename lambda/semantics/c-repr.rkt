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
    ['func-type     'Function*]   ; write_type → "Function*"
    ['type-type     'Type*]       ; write_type → "Type*"
    ['null-type     'Item]        ; null is represented as Item
    ['error-type    'Item]        ; errors are always Item
    ['any-type      'Item]        ; any/unknown → Item (universal)
    ['number-type   'Item]        ; abstract number → Item
    ['range-type    'Range*]      ; write_type → "Range*"
    ['path-type     'Path*]       ; write_type → "Path*"
    ['vmap-type     'Item]        ; vmap is always Item
    ['array-int-type   'ArrayInt*]   ; specialized int array
    ['array-int64-type 'ArrayInt64*] ; specialized int64 array
    ['array-float-type 'ArrayFloat*] ; specialized float array

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
  (and (member ct '(String* Symbol* Array* List* Map* Element* Decimal*
                    Range* Path* Function* Type*
                    ArrayInt* ArrayInt64* ArrayFloat*)) #t))

;; Is this an inline scalar type? (packed in registers, not pointers)
(define (scalar-c-type? ct)
  (and (member ct '(int32_t int64_t double bool)) #t))

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
    ['Symbol*   'y2it]
    ['Array*    'cast-to-Item]   ; (Item)(ptr) — pointer cast
    ['List*     #f]              ; list_end() already returns Item
    ['Map*      'cast-to-Item]
    ['Element*  'cast-to-Item]
    ['Range*    'cast-to-Item]
    ['Path*     'cast-to-Item]
    ['Function* 'cast-to-Item]
    ['Type*     'cast-to-Item]
    ['DateTime  'push_k]
    ['Decimal*  'c2it]
    ['ArrayInt*    'cast-to-Item]
    ['ArrayInt64*  'cast-to-Item]
    ['ArrayFloat*  'cast-to-Item]
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
    ;; Container pointers: cast from Item (uint64_t → ptr)
    ['Array*    'cast-from-Item]
    ['List*     'cast-from-Item]
    ['Map*      'cast-from-Item]
    ['Element*  'cast-from-Item]
    ['Range*    'cast-from-Item]
    ['Path*     'cast-from-Item]
    ['Function* 'cast-from-Item]
    ['Type*     'cast-from-Item]
    ['ArrayInt*    'cast-from-Item]
    ['ArrayInt64*  'cast-from-Item]
    ['ArrayFloat*  'cast-from-Item]
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
       ;; int64 → int: use (int32_t) cast (truncation)
       [('int64_t 'int32_t) 'cast-int64-to-int]
       ;; double → int64: use (int64_t) cast (truncation)
       [('double 'int64_t) 'cast-double-to-int64]
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
;; Maps system function names to (semantic-return-type . actual-C-return-type).
;; When these differ, the transpiler may need boxing/unboxing conversions.
;;
;; Source: build_ast.cpp (sys_funcs[]) for semantic types,
;;         lambda.h declarations for actual C return types.
;;
;; CRITICAL: Entries where semantic ≠ C return are potential bug sources.

;; Struct: (name semantic-type c-return-type discrepancy?)
(struct sys-func-entry (name semantic-type c-return-type discrepancy?) #:transparent)

(define sys-func-catalog
  (list
   ;; ── Type conversion ──
   ;; string() has matching types: semantic=STRING, C=String*
   (sys-func-entry 'string    'string-type  'String*    #f)
   ;; int() returns TYPE_ANY semantically but Item in C → both are "untyped"
   (sys-func-entry 'int       'any-type     'Item       #f)
   ;; int64() returns TYPE_INT64 semantically and int64_t in C → match
   (sys-func-entry 'int64     'int64-type   'int64_t    #f)
   ;; float() returns TYPE_ANY semantically and Item in C
   (sys-func-entry 'float     'any-type     'Item       #f)
   ;; decimal() returns TYPE_ANY semantically and Item in C
   (sys-func-entry 'decimal   'any-type     'Item       #f)
   ;; binary() returns TYPE_ANY semantically and Item in C
   (sys-func-entry 'binary    'any-type     'Item       #f)
   ;; symbol() returns TYPE_SYMBOL and Symbol* → match
   (sys-func-entry 'symbol    'symbol-type  'Symbol*    #f)

   ;; ── String operations ── (CRITICAL: many are TYPE_ANY → Item despite working on strings)
   ;; trim: semantic=ANY, C=Item → matches (but semantically should be STRING)
   (sys-func-entry 'trim      'any-type     'Item       #f)
   (sys-func-entry 'trim_start 'any-type    'Item       #f)
   (sys-func-entry 'trim_end  'any-type     'Item       #f)
   ;; lower/upper: semantic=ANY, C=Item
   (sys-func-entry 'lower     'any-type     'Item       #f)
   (sys-func-entry 'upper     'any-type     'Item       #f)
   ;; normalize: semantic=STRING, C=Item → DISCREPANCY
   (sys-func-entry 'normalize 'string-type  'Item       #t)
   ;; str_join: semantic=STRING, C=Item → DISCREPANCY
   (sys-func-entry 'str_join  'string-type  'Item       #t)
   ;; replace: semantic=ANY, C=Item
   (sys-func-entry 'replace   'any-type     'Item       #f)
   ;; split: semantic=ANY, C=Item
   (sys-func-entry 'split     'any-type     'Item       #f)
   ;; chars: semantic=ANY, C=Item
   (sys-func-entry 'chars     'any-type     'Item       #f)
   ;; contains: semantic=BOOL, C=Bool → match
   (sys-func-entry 'contains  'bool-type    'bool       #f)
   ;; starts_with: semantic=BOOL, C=Bool → match
   (sys-func-entry 'starts_with 'bool-type  'bool       #f)
   (sys-func-entry 'ends_with 'bool-type    'bool       #f)
   ;; index_of: semantic=INT64, C=int64_t → match
   (sys-func-entry 'index_of  'int64-type   'int64_t    #f)
   (sys-func-entry 'last_index_of 'int64-type 'int64_t  #f)
   ;; substring: semantic type not listed (ANY), C=Item
   (sys-func-entry 'substring 'any-type     'Item       #f)
   ;; url_resolve: semantic=STRING, C=Item → DISCREPANCY
   (sys-func-entry 'url_resolve 'string-type 'Item      #t)
   ;; strcat (++ operator): C=String*
   (sys-func-entry 'strcat    'string-type  'String*    #f)

   ;; ── Format/IO ──
   ;; format: semantic=STRING, C=String* → match
   (sys-func-entry 'format    'string-type  'String*    #f)
   ;; error: semantic=ERROR, C=Item → match
   (sys-func-entry 'error     'error-type   'Item       #f)
   ;; input: semantic=ANY, C=Item, can_raise=true
   (sys-func-entry 'input     'any-type     'Item       #f)

   ;; ── Length ──
   ;; len: semantic=INT64, C=int64_t → match
   (sys-func-entry 'len       'int64-type   'int64_t    #f)

   ;; ── Type introspection ──
   ;; type: semantic=TYPE, C=Type* → match
   (sys-func-entry 'type      'type-type    'Type*      #f)
   ;; name: semantic=SYMBOL, C=Symbol* → match
   (sys-func-entry 'name      'symbol-type  'Symbol*    #f)
   ;; is: semantic=BOOL (operator), C=Bool
   (sys-func-entry 'is        'bool-type    'bool       #f)
   ;; in: semantic=BOOL (operator), C=Bool
   (sys-func-entry 'in        'bool-type    'bool       #f)
   ;; exists: semantic=BOOL, C=Bool
   (sys-func-entry 'exists    'bool-type    'bool       #f)

   ;; ── Arithmetic ── (all semantic=ANY, C=Item)
   (sys-func-entry 'add       'any-type     'Item       #f)
   (sys-func-entry 'sub       'any-type     'Item       #f)   ; not in sys_funcs
   (sys-func-entry 'mul       'any-type     'Item       #f)   ; operators
   (sys-func-entry 'div       'any-type     'Item       #f)
   (sys-func-entry 'idiv      'any-type     'Item       #f)
   (sys-func-entry 'pow       'any-type     'Item       #f)
   (sys-func-entry 'mod       'any-type     'Item       #f)
   (sys-func-entry 'abs       'any-type     'Item       #f)
   (sys-func-entry 'round     'any-type     'Item       #f)
   (sys-func-entry 'floor     'any-type     'Item       #f)
   (sys-func-entry 'ceil      'any-type     'Item       #f)
   (sys-func-entry 'neg       'any-type     'Item       #f)
   (sys-func-entry 'pos       'any-type     'Item       #f)

   ;; ── Comparison ── (all semantic implied BOOL, C=Bool)
   (sys-func-entry 'eq        'bool-type    'bool       #f)
   (sys-func-entry 'ne        'bool-type    'bool       #f)
   (sys-func-entry 'lt        'bool-type    'bool       #f)
   (sys-func-entry 'gt        'bool-type    'bool       #f)
   (sys-func-entry 'le        'bool-type    'bool       #f)
   (sys-func-entry 'ge        'bool-type    'bool       #f)
   (sys-func-entry 'not       'bool-type    'bool       #f)

   ;; ── Logical (short-circuit) ── return operand value, not bool
   ;; and/or: semantic=Item (returns operand), C=Item
   (sys-func-entry 'and       'any-type     'Item       #f)
   (sys-func-entry 'or        'any-type     'Item       #f)

   ;; ── Aggregation ── (all semantic=ANY, C=Item)
   (sys-func-entry 'sum       'any-type     'Item       #f)
   (sys-func-entry 'avg       'any-type     'Item       #f)
   (sys-func-entry 'min       'any-type     'Item       #f)
   (sys-func-entry 'max       'any-type     'Item       #f)
   (sys-func-entry 'prod      'any-type     'Item       #f)
   (sys-func-entry 'mean      'any-type     'Item       #f)
   (sys-func-entry 'median    'any-type     'Item       #f)
   (sys-func-entry 'variance  'any-type     'Item       #f)
   (sys-func-entry 'deviation 'any-type     'Item       #f)
   (sys-func-entry 'quantile  'any-type     'Item       #f)

   ;; ── Collection operations ── (all semantic=ANY, C=Item)
   (sys-func-entry 'reverse   'any-type     'Item       #f)
   (sys-func-entry 'sort      'any-type     'Item       #f)
   (sys-func-entry 'unique    'any-type     'Item       #f)
   (sys-func-entry 'concat    'any-type     'Item       #f)
   (sys-func-entry 'take      'any-type     'Item       #f)
   (sys-func-entry 'drop      'any-type     'Item       #f)
   (sys-func-entry 'slice     'any-type     'Item       #f)
   (sys-func-entry 'zip       'any-type     'Item       #f)
   (sys-func-entry 'fill      'any-type     'Item       #f)
   (sys-func-entry 'range     'any-type     'Item       #f)
   (sys-func-entry 'cumsum    'any-type     'Item       #f)
   (sys-func-entry 'cumprod   'any-type     'Item       #f)
   (sys-func-entry 'argmin    'any-type     'Item       #f)
   (sys-func-entry 'argmax    'any-type     'Item       #f)
   (sys-func-entry 'dot       'any-type     'Item       #f)
   (sys-func-entry 'norm      'any-type     'Item       #f)
   (sys-func-entry 'join      'any-type     'Item       #f)  ; fn_join (different from str_join)

   ;; ── Math (element-wise) ── (all semantic=ANY, C=Item)
   (sys-func-entry 'sqrt      'any-type     'Item       #f)
   (sys-func-entry 'log       'any-type     'Item       #f)
   (sys-func-entry 'log10     'any-type     'Item       #f)
   (sys-func-entry 'exp       'any-type     'Item       #f)
   (sys-func-entry 'sin       'any-type     'Item       #f)
   (sys-func-entry 'cos       'any-type     'Item       #f)
   (sys-func-entry 'tan       'any-type     'Item       #f)
   (sys-func-entry 'sign      'any-type     'Item       #f)

   ;; ── Boolean predicates ──
   (sys-func-entry 'all       'bool-type    'bool       #f)
   (sys-func-entry 'any_pred  'bool-type    'bool       #f)

   ;; ── Datetime ── (all semantic=DTIME, C=DateTime)
   (sys-func-entry 'datetime  'datetime-type 'DateTime  #f)
   (sys-func-entry 'date      'datetime-type 'DateTime  #f)
   (sys-func-entry 'time      'datetime-type 'DateTime  #f)
   (sys-func-entry 'justnow   'datetime-type 'DateTime  #f)

   ;; ── Range constructor ──
   (sys-func-entry 'to        'range-type   'Range*     #f)

   ;; ── Pipe operations ── (all C=Item)
   (sys-func-entry 'pipe_map  'any-type     'Item       #f)
   (sys-func-entry 'pipe_where 'any-type    'Item       #f)
   (sys-func-entry 'pipe_call 'any-type     'Item       #f)

   ;; ── Member/index ── (all C=Item)
   (sys-func-entry 'member    'any-type     'Item       #f)
   (sys-func-entry 'index     'any-type     'Item       #f)

   ;; ── Bitwise ── (semantic=INT64, C=int64_t)
   ;; Bitwise ops work on int64_t in C; semantic type is int64 (not int)
   (sys-func-entry 'band      'int64-type   'int64_t    #f)
   (sys-func-entry 'bor       'int64-type   'int64_t    #f)
   (sys-func-entry 'bxor      'int64-type   'int64_t    #f)
   (sys-func-entry 'bnot      'int64-type   'int64_t    #f)
   (sys-func-entry 'shl       'int64-type   'int64_t    #f)
   (sys-func-entry 'shr       'int64-type   'int64_t    #f)
   ))

;; Lookup a system function by name
(define (sys-func-lookup name)
  (findf (λ (e) (eq? (sys-func-entry-name e) name)) sys-func-catalog))

;; Get the ACTUAL C return type for a system function
(define (sys-func-c-return name)
  (define entry (sys-func-lookup name))
  (if entry (sys-func-entry-c-return-type entry) 'Item))

;; Find all system functions with semantic/C type discrepancies
(define (sys-func-discrepancies)
  (filter sys-func-entry-discrepancy? sys-func-catalog))

;; Check if a sys func's semantic return type could cause a boxing mismatch
;; when used as an argument to a typed parameter
(define (sys-func-needs-conversion? func-name param-type)
  (define entry (sys-func-lookup func-name))
  (cond
    [(not entry) #f]
    [else
     (define c-ret (sys-func-entry-c-return-type entry))
     (define param-ct (lambda-type->c-type param-type))
     (and (not (eq? c-ret param-ct))
          (required-conversion c-ret param-ct))]))


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
         sys-func-entry sys-func-entry-name sys-func-entry-semantic-type
         sys-func-entry-c-return-type sys-func-entry-discrepancy?
         sys-func-catalog sys-func-lookup sys-func-c-return
         sys-func-discrepancies sys-func-needs-conversion?
         verify-function)
