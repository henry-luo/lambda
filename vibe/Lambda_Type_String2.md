# Proposal: Standardize String/Symbol Return Types in Native Functions

## Problem Statement

Lambda's string-related system functions currently have **inconsistent return conventions**:

| Function      | Current C Signature                 | `c_ret_type`   | Who boxes?                                      |
| ------------- | ----------------------------------- | -------------- | ----------------------------------------------- |
| `string`      | `String* fn_string(Item)`           | `C_RET_STRING` | **Caller** (transpiler emits `emit_box_string`) |
| `format`      | `String* fn_format1(Item)`          | `C_RET_STRING` | **Caller**                                      |
| `name`        | `Symbol* fn_name(Item)`             | `C_RET_SYMBOL` | **Caller**                                      |
| `upper`       | `Item fn_upper(Item)`               | `C_RET_ITEM`   | **Self** (boxes internally with `s2it()`)       |
| `lower`       | `Item fn_lower(Item)`               | `C_RET_ITEM`   | **Self**                                        |
| `trim`        | `Item fn_trim(Item)`                | `C_RET_ITEM`   | **Self**                                        |
| `trim_start`  | `Item fn_trim_start(Item)`          | `C_RET_ITEM`   | **Self**                                        |
| `trim_end`    | `Item fn_trim_end(Item)`            | `C_RET_ITEM`   | **Self**                                        |
| `replace`     | `Item fn_replace(Item, Item, Item)` | `C_RET_ITEM`   | **Self**                                        |
| `join`        | `Item fn_join2(Item, Item)`         | `C_RET_ITEM`   | **Self**                                        |
| `split`       | `Item fn_split(Item, Item)`         | `C_RET_ITEM`   | **Self** (returns List)                         |
| `normalize`   | `Item fn_normalize1(Item)`          | `C_RET_ITEM`   | **Self**                                        |
| `chr`         | `Item fn_chr(Item)`                 | `C_RET_ITEM`   | **Self**                                        |
| `url_resolve` | `Item fn_url_resolve(Item, Item)`   | `C_RET_ITEM`   | **Self**                                        |

`fn_string` and `fn_format` return raw `String*` and let the caller box. All others self-box into `Item` internally. This inconsistency complicates the transpiler and prevents optimizations.

### Why it matters

1. **JIT overhead**: Functions returning `Item` force boxing inside the C function, even when the caller immediately unboxes (e.g., chained `upper(trim(s))`). Returning `String*` lets the transpiler skip redundant box/unbox.
2. **Transpiler complexity**: The MIR transpiler maintains a hardcoded switch on `SysFunc` enums to determine C return types — this should be data-driven from `c_ret_type` in the registry.
3. **Dual string/symbol handling**: Functions like `fn_upper`, `fn_lower`, `fn_trim` accept both `string` and `symbol` and preserve the input type. This dual-nature forces them to return `Item` because `C_RET_STRING` and `C_RET_SYMBOL` are distinct. Standardizing to `String*` means these functions become string-only at the C level, with separate `_sym` variants for symbol input.

---

## Prior Art: Dual-Version System Functions

Lambda already has an established pattern of maintaining **two versions** of the same logical operation — one for boxed `Item`, one for native C types. Currently **~50 functions** follow this pattern across five categories:

### Category 1: Math — Item + libc native (30 functions)

The `SysFuncInfo` struct has both `c_func_name` (Item-based) and `native_c_name` (libc `double→double`). The transpiler auto-selects the native path when operand types are statically known as float.

| Lambda Name | Item Version | Native Version |
|---|---|---|
| `abs` | `fn_abs(Item) → Item` | `fabs(double) → double` |
| `round` | `fn_round(Item) → Item` | `round(double) → double` |
| `math_sqrt` | `fn_math_sqrt(Item) → Item` | `sqrt(double) → double` |
| `math_sin` | `fn_math_sin(Item) → Item` | `sin(double) → double` |
| `math_pow` | `fn_math_pow(Item, Item) → Item` | `fn_pow_u(double, double) → double` |
| `min` | `fn_min2(Item, Item) → Item` | `fn_min2_u(double, double) → double` |
| `max` | `fn_max2(Item, Item) → Item` | `fn_max2_u(double, double) → double` |
| ... | *(+23 more trig/log/exp functions)* | |

### Category 2: Numeric — `_i`/`_f` specialized variants (13 functions)

Registered in `jit_runtime_imports[]` only (not exposed as Lambda sys funcs). The transpiler emits direct calls when arg types are int or float:

| Operation | Item Version | Int Variant | Float Variant |
|---|---|---|---|
| `abs` | `fn_abs(Item)` | `fn_abs_i(int64_t)` | `fn_abs_f(double)` |
| `neg` | `fn_neg(Item)` | `fn_neg_i(int64_t)` | `fn_neg_f(double)` |
| `idiv` | `fn_idiv(Item, Item)` | `fn_idiv_i(int64_t, int64_t)` | — |
| `mod` | `fn_mod(Item, Item)` | `fn_mod_i(int64_t, int64_t)` | — |
| `sign` | `fn_sign(Item)` | `fn_sign_i(int64_t)` | `fn_sign_f(double)` |

### Category 3: `fn_len` — Type-specialized collection length (4 variants)

| Item Version | Native Variants |
|---|---|
| `fn_len(Item) → int64_t` | `fn_len_s(String*) → int64_t` |
| | `fn_len_l(List*) → int64_t` |
| | `fn_len_a(Array*) → int64_t` |
| | `fn_len_e(Element*) → int64_t` |

### Category 4: String concat (1 pair)

| Function | Signature | Purpose |
|---|---|---|
| `fn_strcat` | `String* fn_strcat(String*, String*)` | Native pointer-level concat |
| `fn_join` | `Item fn_join(Item, Item)` | Boxed Item-level (string + string, list + list, path + string) |

`fn_join` calls `fn_strcat` internally when both args are strings.

### Category 5: Equality (2 pairs)

| Item Version | Native Version |
|---|---|
| `fn_eq(Item, Item) → Bool` | `fn_str_eq_ptr(String*, String*) → Bool` |
| `fn_ne(Item, Item) → Bool` | `fn_sym_eq_ptr(Symbol*, Symbol*) → Bool` |

### The Gap: String Manipulation — Item Only, No Native Variant

`fn_upper`, `fn_lower`, `fn_trim`, `fn_trim_start`, `fn_trim_end`, `fn_replace`, `fn_join2`, `fn_normalize`, `fn_chr`, `fn_url_resolve` — all **10 functions** exist only in Item-based form with `C_RET_ITEM`. This proposal closes that gap.

---

## Proposal

### 1. Standardize: String Functions Return `String*`, Use `NULL` for Empty/Error

All string-returning sys functions should return raw `String*` at the C level:

```c
// BEFORE (current) — takes Item, returns Item, self-boxes
Item fn_upper(Item str_item);    // C_RET_ITEM, boxes internally
Item fn_lower(Item str_item);    // C_RET_ITEM, boxes internally
Item fn_trim(Item str_item);     // C_RET_ITEM, boxes internally
Item fn_join2(Item, Item);       // C_RET_ITEM, boxes internally
Item fn_replace(Item, Item, Item); // C_RET_ITEM, boxes internally

// AFTER (proposed) — native versions take String*, return String*
String* fn_upper_str(String* str);     // C_RET_STRING, pure string→string
String* fn_lower_str(String* str);     // C_RET_STRING, pure string→string
String* fn_trim_str(String* str);      // C_RET_STRING, pure string→string
String* fn_join2_str(Item list, Item sep); // C_RET_STRING, list→string (keeps Item params)
String* fn_replace_str(String* str, String* old_str, String* new_str); // C_RET_STRING
```

The native versions take `String*` directly — no Item unboxing inside. The **Item versions** (`fn_upper`, `fn_lower`, etc.) keep their current names and handle unboxing, null-guard, and type dispatch:

```c
// Item version — keeps existing name, registered as the sys func
Item fn_upper(Item str_item) {
    if (is_null(str_item)) return ItemNull;
    if (is_error(str_item)) return str_item;
    TypeId tid = get_type_id(str_item);
    if (tid == LMD_TYPE_STRING) {
        String* result = fn_upper_str(it2s(str_item)); // call native
        return result ? s2it(result) : ItemNull;       // box
    }
    if (tid == LMD_TYPE_SYMBOL) {
        Symbol* result = fn_upper_sym(it2y(str_item)); // call native
        return result ? y2it(result) : ItemNull;       // box
    }
    return ItemError;
}
```

Naming convention:
- `fn_upper` — Item version (existing name, `sys_func_defs[]`)
- `fn_upper_str` — native String* version (`jit_runtime_imports[]`)
- `fn_upper_sym` — native Symbol* version (`jit_runtime_imports[]`)

When the transpiler knows the static type is `String`, it calls `fn_upper_str(String*)` directly and boxes with `emit_box_string`. When type is unknown, it falls back to `fn_upper(Item)`.

**Convention**: Return C `NULL` for both error and zero-length string `""`.

The existing boxing code in `emit_box_string` already handles this correctly:

```
emit_box_string(ptr):
    if (ptr != NULL)  →  STRING_TAG | ptr    // valid string Item
    if (ptr == NULL)  →  ITEM_NULL           // Lambda null
```

So `NULL → Lambda null` is already the established boxing behavior. Under Lambda script, the user sees:

```lambda
upper(null)   // => null
upper("")     // => null  (empty string collapses to null)
trim("   ")   // => null  (all whitespace collapses to null)
```

This matches the existing behavior where `fn_trim` already returns `ItemNull` for all-whitespace input.

### 2. Introduce `LMD_TYPE_STRING_OPT` Registry Shorthand

For the function signature registry, introduce a shorthand type to represent "returns `String*` which may be NULL":

```c
// In sys_func_registry.h or lambda.h
// Shorthand for registry annotations — NOT a runtime TypeId
#define LMD_TYPE_STRING_OPT  ((TypeId)0xF0)  // string? — C_RET_STRING with nullable semantics
#define LMD_TYPE_SYMBOL_OPT  ((TypeId)0xF1)  // symbol? — C_RET_SYMBOL with nullable semantics
```

These are **compile-time annotations only**, not runtime type tags. They signal:
- The C function returns a raw pointer (`String*` or `Symbol*`)
- The pointer may be `NULL`
- The caller (transpiler) must box with null-check (`emit_box_string` / `emit_box_symbol`)

At the Lambda script level, the user perceives:

```lambda
fn upper(str: string?) -> string?
fn lower(str: string?) -> string?
fn trim(str: string?) -> string?
fn replace(str: string?, old: string, new: string) -> string?
fn join(arr: [string], sep: string?) -> string?
```

#### Registry Usage

```c
// In sys_func_defs[] — Item versions keep existing names
// (interpreter uses these; transpiler overrides with _str/_sym for known types)
{SYSFUNC_UPPER, "upper", 1, &TYPE_STRING_OPT, false, false, true, LMD_TYPE_STRING, false,
 C_RET_ITEM, C_ARG_ITEM, "fn_upper", FPTR(fn_upper), NULL, NULL, false, 0},

{SYSFUNC_LOWER, "lower", 1, &TYPE_STRING_OPT, false, false, true, LMD_TYPE_STRING, false,
 C_RET_ITEM, C_ARG_ITEM, "fn_lower", FPTR(fn_lower), NULL, NULL, false, 0},

{SYSFUNC_TRIM, "trim", 1, &TYPE_STRING_OPT, false, false, true, LMD_TYPE_STRING, false,
 C_RET_ITEM, C_ARG_ITEM, "fn_trim", FPTR(fn_trim), NULL, NULL, false, 0},

// ... similarly for trim_start, trim_end, replace, join, chr, normalize, url_resolve
```

The key change per function:
- `return_type`: `&TYPE_ANY` → `&TYPE_STRING_OPT` (for type inference)
- `c_func_name` stays as `"fn_upper"` — no rename needed for Item version
- Native `fn_upper_str(String*)` registered separately in `jit_runtime_imports[]`
- Transpiler dispatches to `_str`/`_sym` when static type is known

### 3. Same Treatment for Symbol Functions

Apply the same pattern to symbol-returning functions:

```c
Symbol* fn_name(Item);      // already C_RET_SYMBOL — no change needed
Symbol* fn_symbol1(Item);   // already C_RET_SYMBOL — no change needed
```

Symbol functions are already standardized. The `LMD_TYPE_SYMBOL_OPT` shorthand is introduced for consistency but currently only `fn_name` and `fn_symbol1` need it, and they already return `Symbol*`.

### 4. Eliminate the Hardcoded Switch in Transpiler

Currently `transpile-mir.cpp` has a manual switch to determine C return type:

```cpp
// BEFORE — hardcoded in transpile-mir.cpp:5886
switch (info->fn) {
    case SYSFUNC_STRING: case SYSFUNC_FORMAT1: case SYSFUNC_FORMAT2:
        c_ret_tid = LMD_TYPE_STRING; break;
    case SYSFUNC_NAME: case SYSFUNC_SYMBOL:
        c_ret_tid = LMD_TYPE_SYMBOL; break;
    // ... many more cases
    default: break;
}
```

After standardization, this becomes data-driven:

```cpp
// AFTER — read from registry
TypeId c_ret_tid = LMD_TYPE_ANY;
switch (info->c_ret_type) {
    case C_RET_STRING:    c_ret_tid = LMD_TYPE_STRING; break;
    case C_RET_SYMBOL:    c_ret_tid = LMD_TYPE_SYMBOL; break;
    case C_RET_INT64:     c_ret_tid = LMD_TYPE_INT;    break;
    case C_RET_BOOL:      c_ret_tid = LMD_TYPE_BOOL;   break;
    case C_RET_DTIME:     c_ret_tid = LMD_TYPE_DTIME;  break;
    case C_RET_DOUBLE:    c_ret_tid = LMD_TYPE_FLOAT;  break;
    case C_RET_TYPE_PTR:  c_ret_tid = LMD_TYPE_TYPE;   break;
    case C_RET_CONTAINER: c_ret_tid = LMD_TYPE_ANY;    break;
    case C_RET_ITEM:      c_ret_tid = LMD_TYPE_ANY;    break;
    default: break;
}
```

This eliminates the need to update the transpiler switch every time a new function is added or changed.

---

## Handling the String/Symbol Dual-Type Problem

Currently `fn_upper`, `fn_lower`, `fn_trim`, `fn_replace` accept **both** string and symbol, preserving the input type. Under the new convention (return `String*`), two approaches:

### Option A: String-Only at C Level

Functions always return `String*`. If called with a symbol, convert to string semantics.

- Simpler C code — remove all symbol branches
- Lambda script type: `fn upper(str: string?) -> string?`
- **Problem**: `String` and `Symbol` have **different structs** — Symbol carries a `Target* ns` (namespace) field. Silently converting symbol→string loses the namespace, which breaks map key semantics and element tag identity.

```c
typedef struct String {         typedef struct Symbol {
    uint32_t len;                   uint32_t len;
    uint8_t is_ascii;               Target* ns;    // ← namespace, no equivalent in String
    char chars[];                   char chars[];
} String;                       } Symbol;
```

### Option B: Separate Symbol Variants (Recommended)

Add `fn_upper_sym`, `fn_lower_sym`, `fn_trim_sym` etc. returning `Symbol*` for the symbol case. The transpiler dispatches based on known argument type. Both variants call the **same internal char-processing logic**.

#### Architecture: Three Layers

The design has three layers: (1) shared char-processing helpers, (2) native `String*`/`Symbol*` functions, (3) Item wrappers for dynamic dispatch.

```c
// ═══════ Layer 1: shared internal helpers (raw chars) ═══════
static inline void str_to_upper_chars(const char* src, char* dst, uint32_t len);
static inline void str_to_lower_chars(const char* src, char* dst, uint32_t len);
static inline const char* str_trim_chars(const char* chars, uint32_t len,
                                          uint32_t* out_start, uint32_t* out_len);

// ═══════ Layer 2: native String*/Symbol* functions ═══════
// These take native pointers, return native pointers. No Item awareness.
// NULL input → NULL return. Empty result → NULL return.

String* fn_upper_str(String* str) {
    if (!str || str->len == 0) return NULL;
    String* result = (String*)heap_alloc(sizeof(String) + str->len + 1, LMD_TYPE_STRING);
    str_to_upper_chars(str->chars, result->chars, str->len);
    result->len = str->len;
    result->is_ascii = str->is_ascii;
    result->chars[str->len] = '\0';
    return result;
}

Symbol* fn_upper_sym(Symbol* sym) {
    if (!sym || sym->len == 0) return NULL;
    char buf[256];
    char* dst = (sym->len < sizeof(buf)) ? buf : (char*)malloc(sym->len + 1);
    str_to_upper_chars(sym->chars, dst, sym->len);
    dst[sym->len] = '\0';
    Symbol* result = heap_create_symbol(dst, sym->len);
    if (dst != buf) free(dst);
    return result;
}

// ═══════ Layer 3: Item version (keeps existing name) ═══════
// Handles unboxing, null/error guard, type dispatch, and boxing.

Item fn_upper(Item str_item) {
    GUARD_ERROR1(str_item);
    TypeId tid = get_type_id(str_item);
    if (tid == LMD_TYPE_NULL) return ItemNull;
    if (tid == LMD_TYPE_STRING) {
        String* r = fn_upper_str(it2s(str_item));
        return r ? s2it(r) : ItemNull;
    }
    if (tid == LMD_TYPE_SYMBOL) {
        Symbol* r = fn_upper_sym(it2y(str_item));
        return r ? y2it(r) : ItemNull;
    }
    return ItemError;  // wrong type
}
```

This gives:
- **Pure native functions** — `fn_upper_str(String*)` has no Item/unboxing overhead, ideal for JIT chains
- **Zero duplication** of char logic — Layer 1 helpers shared across String and Symbol
- **Type safety** at the C level — no accidental String↔Symbol confusion
- **Namespace preservation** — `heap_create_symbol` handles ns correctly
- **Three dispatch paths** for the transpiler:
  - Static `String` type → call `fn_upper_str(String*)` directly, box with `emit_box_string`
  - Static `Symbol` type → call `fn_upper_sym(Symbol*)` directly, box with `emit_box_symbol`
  - Unknown type → call `fn_upper(Item)`, already boxed

#### Functions Needing Symbol Variants

| String Version | Symbol Version | Item Version | Shared Internal |
|---|---|---|---|
| `String* fn_upper_str(String*)` | `Symbol* fn_upper_sym(Symbol*)` | `Item fn_upper(Item)` | `str_to_upper_chars()` |
| `String* fn_lower_str(String*)` | `Symbol* fn_lower_sym(Symbol*)` | `Item fn_lower(Item)` | `str_to_lower_chars()` |
| `String* fn_trim_str(String*)` | `Symbol* fn_trim_sym(Symbol*)` | `Item fn_trim(Item)` | `str_trim_chars()` |
| `String* fn_trim_start_str(String*)` | `Symbol* fn_trim_start_sym(Symbol*)` | `Item fn_trim_start(Item)` | `str_trim_start_chars()` |
| `String* fn_trim_end_str(String*)` | `Symbol* fn_trim_end_sym(Symbol*)` | `Item fn_trim_end(Item)` | `str_trim_end_chars()` |
| `String* fn_replace_str(String*,String*,String*)` | `Symbol* fn_replace_sym(Symbol*,String*,String*)` | `Item fn_replace(Item,Item,Item)` | `str_replace_chars()` |

Functions that **don't** need symbol variants:
- `fn_join2` — joins a list, always returns string
- `fn_chr` — int→string only
- `fn_normalize` — unicode normalization, string only
- `fn_url_resolve` — URL semantics, string only

#### Registry Entries

```c
// Item version — keeps existing name, registered as the sys func
{SYSFUNC_UPPER, "upper", 1, &TYPE_ANY, false, false, true, LMD_TYPE_STRING, false,
 C_RET_ITEM, C_ARG_ITEM, "fn_upper", FPTR(fn_upper), NULL, NULL, false, 0},
```

The native `fn_upper_str(String*)` and `fn_upper_sym(Symbol*)` are registered in `jit_runtime_imports[]` only (like `fn_strcat`, `fn_len_s`):

```c
// In jit_runtime_imports[]
{"fn_upper_str",     (fn_ptr)fn_upper_str},      // String* → String*
{"fn_upper_sym",     (fn_ptr)fn_upper_sym},      // Symbol* → Symbol*
{"fn_lower_str",     (fn_ptr)fn_lower_str},
{"fn_lower_sym",     (fn_ptr)fn_lower_sym},
// ... etc.
```

The transpiler resolves by static arg type:
- `LMD_TYPE_STRING` → emit call to `fn_upper_str`, box result with `emit_box_string`
- `LMD_TYPE_SYMBOL` → emit call to `fn_upper_sym`, box result with `emit_box_symbol`
- Unknown type → emit call to `fn_upper` (already boxed `Item` return)

**Recommendation**: Option B. String and Symbol have fundamentally different structs (Symbol carries `Target* ns`). The shared-internal-function pattern keeps code DRY while preserving type correctness.

---

## Migration Path

### Phase 1: Infrastructure
1. Define `LMD_TYPE_STRING_OPT` / `LMD_TYPE_SYMBOL_OPT` constants
2. Define `TYPE_STRING_OPT` / `TYPE_SYMBOL_OPT` Type structs for registry
3. Replace transpiler hardcoded switch with `c_ret_type`-driven dispatch

### Phase 2: Extract Shared Internal Functions
1. Factor out char-processing logic from existing `fn_upper`, `fn_lower`, `fn_trim`, `fn_trim_start`, `fn_trim_end`, `fn_replace` into static internal helpers (`str_to_upper_chars`, `str_to_lower_chars`, `str_trim_chars`, `str_replace_chars`, etc.)
2. Verify helpers work on raw `const char*` + length — no Item/String/Symbol dependencies

### Phase 3: Create Native String Functions
For each function (`fn_trim`, `fn_upper`, `fn_lower`, `fn_trim_start`, `fn_trim_end`, `fn_replace`, `fn_join2`, `fn_chr`, `fn_normalize`, `fn_url_resolve`):

1. Create native `String* fn_xxx_str(String* str)` — takes `String*`, returns `String*`, no Item awareness
2. Return `NULL` for null input or empty result
3. Call shared internal helper for char logic
4. Register in `jit_runtime_imports[]`
5. Run baseline tests

### Phase 4: Add Symbol Variants + Refactor Item Versions
For each applicable function (`upper`, `lower`, `trim`, `trim_start`, `trim_end`, `replace`):

1. Implement native `Symbol* fn_xxx_sym(Symbol*)` calling the same shared internal helper
2. Refactor existing `fn_xxx(Item)` to dispatch: unbox → `fn_xxx_str`/`fn_xxx_sym` → box result
3. Register `fn_xxx_str` and `fn_xxx_sym` in `jit_runtime_imports[]`
4. Update transpiler to dispatch by static arg type: `String` → `fn_xxx_str`, `Symbol` → `fn_xxx_sym`, unknown → `fn_xxx`
5. Run baseline tests

### Phase 5: Cleanup
1. Verify JIT chain optimization: `upper(trim(s))` keeps raw `String*` through the chain, boxes once at final assignment
2. Update `lambda.h` extern declarations — add `fn_xxx_str`/`fn_xxx_sym` with `String*`/`Symbol*` params
3. Verify interpreter path still works via existing `fn_xxx(Item)` functions

---

## Functions NOT Affected

| Function | Reason |
|----------|--------|
| `split` | Returns `List*` (container), not string |
| `find` | Returns `Item` (match result varies) |
| `contains` | Polymorphic — will extend to collections (list, map) |
| `index_of`, `last_index_of` | Polymorphic — will extend to collections (list) |
| `string`, `format1`, `format2` | Already `C_RET_STRING` — no change needed |
| `name`, `symbol1` | Already `C_RET_SYMBOL` — no change needed |

---

## Performance Impact

Returning `String*` instead of `Item` eliminates:
- One `s2it()` call per function invocation (tag OR operation)
- One redundant null check when the transpiler knows the type
- The unbox+rebox overhead in chains like `upper(trim(lower(s)))`

For JIT-compiled code, the transpiler can keep intermediate values as raw `String*` pointers and only box at the final assignment — this is the same optimization already used for `fn_string` and `fn_format`.

---

## Post-Refactoring C Signature Reference

Complete list of all string-related native function signatures after this refactoring.

### Native String Functions (new — `String*` in, `String*` out)

| Lambda Name | C Function | C Signature (After) | Registration | Notes |
|---|---|---|---|---|
| `upper` | `fn_upper_str` | `String* fn_upper_str(String* str)` | `jit_runtime_imports` | **new** native |
| `lower` | `fn_lower_str` | `String* fn_lower_str(String* str)` | `jit_runtime_imports` | **new** native |
| `trim` | `fn_trim_str` | `String* fn_trim_str(String* str)` | `jit_runtime_imports` | **new** native |
| `trim_start` | `fn_trim_start_str` | `String* fn_trim_start_str(String* str)` | `jit_runtime_imports` | **new** native |
| `trim_end` | `fn_trim_end_str` | `String* fn_trim_end_str(String* str)` | `jit_runtime_imports` | **new** native |
| `replace` | `fn_replace_str` | `String* fn_replace_str(String* str, String* old, String* new)` | `jit_runtime_imports` | **new** native |
| `join` (2-arg) | `fn_join2_str` | `String* fn_join2_str(Item list, Item sep)` | `jit_runtime_imports` | keeps Item params (list input) |
| `chr` | `fn_chr_str` | `String* fn_chr_str(int64_t codepoint)` | `jit_runtime_imports` | int→string |
| `normalize` (1-arg) | `fn_normalize1_str` | `String* fn_normalize1_str(String* str)` | `jit_runtime_imports` | **new** native |
| `normalize` (2-arg) | `fn_normalize_str` | `String* fn_normalize_str(String* str, Item form)` | `jit_runtime_imports` | form stays Item (symbol arg) |
| `url_resolve` | `fn_url_resolve_str` | `String* fn_url_resolve_str(String* base, String* rel)` | `jit_runtime_imports` | **new** native |

### Native Int-Returning String Functions (new — `String*` in, `int64_t` out)

| Lambda Name | C Function | C Signature (After) | Registration | Notes |
|---|---|---|---|---|
| `ord` | `fn_ord_str` | `int64_t fn_ord_str(String* str)` | `jit_runtime_imports` | **new** native |

### Native Bool-Returning String Functions (new — `String*` in, `Bool` out)

| Lambda Name | C Function | C Signature (After) | Registration | Notes |
|---|---|---|---|---|
| `starts_with` | `fn_starts_with_str` | `Bool fn_starts_with_str(String* str, String* pfx)` | `jit_runtime_imports` | **new** native |
| `ends_with` | `fn_ends_with_str` | `Bool fn_ends_with_str(String* str, String* sfx)` | `jit_runtime_imports` | **new** native |

### Native Symbol Variant Functions (new — `Symbol*` in, `Symbol*` out)

| Lambda Name | C Function | C Signature | Registration | Notes |
|---|---|---|---|---|
| `upper` | `fn_upper_sym` | `Symbol* fn_upper_sym(Symbol* sym)` | `jit_runtime_imports` | **new** |
| `lower` | `fn_lower_sym` | `Symbol* fn_lower_sym(Symbol* sym)` | `jit_runtime_imports` | **new** |
| `trim` | `fn_trim_sym` | `Symbol* fn_trim_sym(Symbol* sym)` | `jit_runtime_imports` | **new** |
| `trim_start` | `fn_trim_start_sym` | `Symbol* fn_trim_start_sym(Symbol* sym)` | `jit_runtime_imports` | **new** |
| `trim_end` | `fn_trim_end_sym` | `Symbol* fn_trim_end_sym(Symbol* sym)` | `jit_runtime_imports` | **new** |
| `replace` | `fn_replace_sym` | `Symbol* fn_replace_sym(Symbol* sym, String* old, String* new)` | `jit_runtime_imports` | **new** |

### Item Versions (sys_func_defs — existing names, interpreter + unknown-type fallback)

| Lambda Name     | C Function       | C Signature                                     | `c_ret_type` | Notes                                           |
| --------------- | ---------------- | ----------------------------------------------- | ------------ | ----------------------------------------------- |
| `upper`         | `fn_upper`       | `Item fn_upper(Item str)`                       | `C_RET_ITEM` | unbox → `fn_upper_str`/`fn_upper_sym` → box     |
| `lower`         | `fn_lower`       | `Item fn_lower(Item str)`                       | `C_RET_ITEM` | unbox → dispatch → box                          |
| `trim`          | `fn_trim`        | `Item fn_trim(Item str)`                        | `C_RET_ITEM` | unbox → dispatch → box                          |
| `trim_start`    | `fn_trim_start`  | `Item fn_trim_start(Item str)`                  | `C_RET_ITEM` | unbox → dispatch → box                          |
| `trim_end`      | `fn_trim_end`    | `Item fn_trim_end(Item str)`                    | `C_RET_ITEM` | unbox → dispatch → box                          |
| `replace`       | `fn_replace`     | `Item fn_replace(Item str, Item old, Item new)`  | `C_RET_ITEM` | unbox → dispatch → box                          |
| `join` (2-arg)  | `fn_join2`       | `Item fn_join2(Item list, Item sep)`             | `C_RET_ITEM` | wrapper for `fn_join2_str`                      |
| `chr`           | `fn_chr`         | `Item fn_chr(Item codepoint)`                    | `C_RET_ITEM` | wrapper for `fn_chr_str`                        |
| `normalize` (1) | `fn_normalize1`  | `Item fn_normalize1(Item str)`                   | `C_RET_ITEM` | wrapper for `fn_normalize1_str`                 |
| `normalize` (2) | `fn_normalize`   | `Item fn_normalize(Item str, Item form)`         | `C_RET_ITEM` | wrapper for `fn_normalize_str`                  |
| `url_resolve`   | `fn_url_resolve` | `Item fn_url_resolve(Item base, Item rel)`       | `C_RET_ITEM` | wrapper for `fn_url_resolve_str`                |
| `ord`           | `fn_ord`         | `int64_t fn_ord(Item str)`                       | `C_RET_INT64`| unbox → `fn_ord_str` (keeps int64 return)       |
| `starts_with`   | `fn_starts_with` | `Bool fn_starts_with(Item str, Item pfx)`        | `C_RET_BOOL` | unbox → `fn_starts_with_str`                    |
| `ends_with`     | `fn_ends_with`   | `Bool fn_ends_with(Item str, Item sfx)`          | `C_RET_BOOL` | unbox → `fn_ends_with_str`                      |

### Already Native — No Change Needed

| Lambda Name      | C Function         | C Signature                                      | `c_ret_type`    |
| ---------------- | ------------------ | ------------------------------------------------ | --------------- |
| `string`         | `fn_string`        | `String* fn_string(Item item)`                   | `C_RET_STRING`  |
| `format` (1-arg) | `fn_format1`       | `String* fn_format1(Item item)`                  | `C_RET_STRING`  |
| `format` (2-arg) | `fn_format2`       | `String* fn_format2(Item item, Item opts)`       | `C_RET_STRING`  |
| `name`           | `fn_name`          | `Symbol* fn_name(Item item)`                     | `C_RET_SYMBOL`  |
| `symbol` (1-arg) | `fn_symbol1`       | `Symbol* fn_symbol1(Item item)`                  | `C_RET_SYMBOL`  |

| (concat)         | `fn_strcat`        | `String* fn_strcat(String* left, String* right)` | JIT import only |
| (equality)       | `fn_str_eq_ptr`    | `Bool fn_str_eq_ptr(String* a, String* b)`       | JIT import only |
| (equality)       | `fn_sym_eq_ptr`    | `Bool fn_sym_eq_ptr(Symbol* a, Symbol* b)`       | JIT import only |

### Remain Item-Based (not string-returning)

| Lambda Name | C Function | C Signature | `c_ret_type` | Reason |
|---|---|---|---|---|
| `split` (2-arg) | `fn_split` | `Item fn_split(Item str, Item sep)` | `C_RET_ITEM` | returns `List*` |
| `split` (3-arg) | `fn_split3` | `Item fn_split3(Item str, Item sep, Item keep)` | `C_RET_ITEM` | returns `List*` |
| `find` (2-arg) | `fn_find2` | `Item fn_find2(Item src, Item pat)` | `C_RET_ITEM` | mixed return type |
| `find` (3-arg) | `fn_find3` | `Item fn_find3(Item src, Item pat, Item opts)` | `C_RET_ITEM` | mixed return type |
| `symbol` (2-arg) | `fn_symbol2` | `Item fn_symbol2(Item name, Item url)` | `C_RET_ITEM` | namespaced symbol creation |
| `join` (binary) | `fn_join` | `Item fn_join(Item a, Item b)` | `C_RET_ITEM` | polymorphic (str+str, list+list, path+str) |
| `contains` | `fn_contains` | `Bool fn_contains(Item coll, Item sub)` | `C_RET_BOOL` | polymorphic — string, list, map |
| `index_of` | `fn_index_of` | `int64_t fn_index_of(Item coll, Item sub)` | `C_RET_INT64` | polymorphic — string, list |
| `last_index_of` | `fn_last_index_of` | `int64_t fn_last_index_of(Item coll, Item sub)` | `C_RET_INT64` | polymorphic — string, list |

### Shared Internal Helpers (static, not exported)

| Helper | Signature | Used by |
|---|---|---|
| `str_to_upper_chars` | `void str_to_upper_chars(const char* src, char* dst, uint32_t len)` | `fn_upper_str`, `fn_upper_sym` |
| `str_to_lower_chars` | `void str_to_lower_chars(const char* src, char* dst, uint32_t len)` | `fn_lower_str`, `fn_lower_sym` |
| `str_trim_chars` | `const char* str_trim_chars(const char* chars, uint32_t len, uint32_t* out_start, uint32_t* out_len)` | `fn_trim_str`, `fn_trim_sym` |
| `str_trim_start_chars` | `const char* str_trim_start_chars(const char* chars, uint32_t len, uint32_t* out_len)` | `fn_trim_start_str`, `fn_trim_start_sym` |
| `str_trim_end_chars` | `const char* str_trim_end_chars(const char* chars, uint32_t len, uint32_t* out_len)` | `fn_trim_end_str`, `fn_trim_end_sym` |
| `str_replace_chars` | `char* str_replace_chars(const char* src, uint32_t src_len, const char* old, uint32_t old_len, const char* new, uint32_t new_len, uint32_t* out_len)` | `fn_replace_str`, `fn_replace_sym` |

---

## Summary of Changes

| Component | Change |
|-----------|--------|
| `lambda.h` | Add `String* fn_xxx_str(String*)` native declarations. Add `Symbol* fn_xxx_sym(Symbol*)` declarations. Existing `fn_xxx(Item)` signatures unchanged |
| `lambda-eval.cpp` | Extract shared `str_*_chars()` internal helpers. Create ~11 native `fn_xxx_str` functions. Create ~6 native `fn_xxx_sym` functions. Refactor existing `fn_xxx(Item)` to dispatch to `_str`/`_sym` |
| `sys_func_registry.h` | Add `LMD_TYPE_STRING_OPT`, `LMD_TYPE_SYMBOL_OPT` defines |
| `sys_func_registry.c` | `sys_func_defs[]` entries keep existing `fn_xxx` names. Add `fn_xxx_str` + `fn_xxx_sym` to `jit_runtime_imports[]` |
| `transpile-mir.cpp` | Replace hardcoded switch with `c_ret_type`-driven dispatch. Add static-type dispatch: `String`→`fn_xxx_str`, `Symbol`→`fn_xxx_sym`, unknown→`fn_xxx` |
| `transpile.cpp` | Update C transpiler `type_box_table` if applicable |
