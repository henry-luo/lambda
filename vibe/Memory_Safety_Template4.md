# CSS Shorthand Temporary Lifetime Safety

> Companion to [`Memory_Safety_Template.md`](./Memory_Safety_Template.md),
> [`Memory_Safety_Template2.md`](./Memory_Safety_Template2.md), and
> [`Memory_Safety_Template3.md`](./Memory_Safety_Template3.md).
>
> Those docs cover domain ownership, typed tagged values, bounds/overflow
> safety, and runtime UAF handles. This doc starts with a narrower but
> important lifetime class found in Radiant CSS shorthand expansion: a
> synthetic `CssDeclaration` temporarily points at a stack-built `CssValue`.
> Later sections collect follow-on LambdaJS memory-safety patterns found while
> repairing `test_js_gtest.exe`.

---

## 0. Implementation Status

The CSS shorthand temporary-lifetime work (the doc's titled subject, §1–§11) is
**implemented and verified**. The JS/serve/render sections (§12–§17) document
bugs that were already fixed at the time of writing and propose optional
regression lints; those lints are **not yet added**.

| Item | Status | Artifact |
|---|---|---|
| §10 Phase 1 — `CssTempDecl` / `CssTempListDecl<N>` helpers + unit test | ✅ Done | `radiant/css_temp_decl.hpp`, `test/css/test_css_temp_decl_gtest.cpp` (7 tests pass) |
| §10 Phase 2 — migrate background + gap shorthand expansion | ✅ Done | `radiant/resolve_css_style.cpp` (all `= *decl` / `.value = &` sites removed) |
| §10 Phase 3 — `make check-css-temp-decl` gate | ✅ Done | `Makefile` target (passes clean; catches injected violations) |
| §10 Phase 4 — persistent CSS value retention audit | ✅ Done | `vibe/CSS_Value_Retention_Audit.md` + in-code lifetime contracts |
| §12–§17 regression lints (JS/serve/render) | ⬜ Not started | bugs already fixed; lints optional |

Verification: `make build` clean; `make test-radiant-baseline` 5716/5716 (incl.
the Layout Page Suite that carried the original ASan failure); helper unit tests
7/7; `test_css_system` 33/33 and `test_css_dom_crud` 63/63 (clone/subset paths).

Phase 4 outcome in brief: all CSS-value retention reachable in production is
pool/arena-stable (SAFE). Computed-style cache fields are dead (never assigned).
The one concern is a latent, **test-only** cross-pool aliasing in
`style_tree_clone` / `style_tree_create_subset` (declarations/values owned by the
source pool, retained in a tree backed by a different `target_pool`); refcounting
does not protect across pools. Now made explicit via in-code lifetime contracts
rather than fixed with a speculative deep `CssValue` copy on an unreached path.
See `vibe/CSS_Value_Retention_Audit.md`.

---

## 1. Concrete Bug: Background Shorthand Stack Lifetime

The page-suite ASan failure came from background shorthand expansion in
`radiant/resolve_css_style.cpp`. The shorthand handler copies a parsed
`CssDeclaration`, changes `property_id`, and points `value` at either an
existing parsed `CssValue` or a small synthetic list value used to call a
longhand resolver.

The unsafe shape was:

```cpp
CssDeclaration position_decl = *decl;
position_decl.property_id = CSS_PROPERTY_BACKGROUND_POSITION;

if (position_count == 1) {
    position_decl.value = position_values[0];
} else {
    CssValue position_list = {};
    position_list.type = CSS_VALUE_TYPE_LIST;
    position_list.data.list.values = position_values;
    position_list.data.list.count = position_count;
    position_decl.value = &position_list;
}

resolve_css_property(CSS_PROPERTY_BACKGROUND_POSITION, &position_decl, lycon);
```

When the two-value path runs, `position_list` dies at the end of the `else`
block, but `position_decl.value` is read after that block by
`resolve_css_property()`. AddressSanitizer correctly reports
`stack-use-after-scope`.

The local fix was to move `position_list` to the wider scope that encloses the
`resolve_css_property()` call. That fixes the immediate bug, but the broader
pattern remains fragile: any shorthand expansion can accidentally point a
copied declaration at a narrower-scope stack `CssValue`.

---

## 2. Root Cause

`CssDeclaration` is a C ABI struct:

```cpp
typedef struct CssDeclaration {
    CssPropertyId property_id;
    CssValue* value;
    ...
} CssDeclaration;
```

The raw `CssValue*` field has no lifetime information. In current code it can
mean several different things:

1. A persistent parsed value allocated in the document/style pool.
2. A borrowed sub-value inside a parsed shorthand value.
3. A temporary stack value built only to call a longhand resolver.
4. A synthetic value allocated during CSSOM or DOM style mutation.

The bug is not a classic pool-vs-session leak. It is an intra-function lexical
lifetime error: the pointed-to value lives on the stack, but in a smaller block
than the call that consumes it.

The existing `PersistentField` / `DomainOutlives` model from
`Memory_Safety_Template.md` can catch retained pool/session mismatches, but it
cannot see `&position_list` through a raw pointer field. C++ templates only
help once the call site is forced to use a typed helper instead of assigning to
`decl.value` directly.

---

## 3. Safety Goal

The shorthand expansion contract should be explicit:

1. Persistent style-tree declarations may point only at pool-stable CSS values.
2. Synthetic shorthand declarations are resolve-only and must not be retained.
3. Stack-built `CssValue` lists must be consumed while the list object is alive.
4. Shorthand call sites should not manually assign `decl.value = &local`.
5. `resolve_css_property()` may inspect the declaration during the call, but
   it must not store pointers from a resolve-only declaration.

This does not require changing the HTML parser, CSS parser, or `CssDeclaration`
ABI. It requires wrapping shorthand expansion sites in a small template helper
and adding a lint/check target to keep raw escape patterns from returning.

---

## 4. Proposed Template Helper: Resolve-Only Declarations

Add a header-local or small shared helper for CSS shorthand expansion, for
example `radiant/css_temp_decl.hpp`.

```cpp
namespace lam {

struct CssResolveOnlyTag {};

class CssTempDecl {
    CssDeclaration decl_;

public:
    CssTempDecl(const CssDeclaration* base, CssPropertyId prop, CssValue* value)
        : decl_(*base) {
        decl_.property_id = prop;
        decl_.value = value;
    }

    CssTempDecl(const CssTempDecl&) = delete;
    CssTempDecl& operator=(const CssTempDecl&) = delete;

    void resolve(LayoutContext* lycon) {
        resolve_css_property(decl_.property_id, &decl_, lycon);
    }

    CssDeclaration* raw_decl() = delete;
};

template<size_t N>
class CssTempListDecl {
    CssDeclaration decl_;
    CssValue list_;
    CssValue* values_[N];
    int count_;

public:
    explicit CssTempListDecl(const CssDeclaration* base, CssPropertyId prop)
        : decl_(*base), list_(), values_(), count_(0) {
        decl_.property_id = prop;
    }

    CssTempListDecl(const CssTempListDecl&) = delete;
    CssTempListDecl& operator=(const CssTempListDecl&) = delete;

    bool append(CssValue* value) {
        if (!value || count_ >= (int)N) return false; // INT_CAST_OK: N is a template capacity.
        values_[count_++] = value;
        return true;
    }

    void resolve(LayoutContext* lycon) {
        if (count_ <= 0) return;
        if (count_ == 1) {
            decl_.value = values_[0];
        } else {
            list_.type = CSS_VALUE_TYPE_LIST;
            list_.data.list.values = values_;
            list_.data.list.count = count_;
            decl_.value = &list_;
        }
        resolve_css_property(decl_.property_id, &decl_, lycon);
    }

    CssDeclaration* raw_decl() = delete;
    CssValue* raw_list() = delete;
};

} // namespace lam
```

The helper still uses stack storage internally. That is fine: the stack list
lives for the whole `resolve()` call, and the copied declaration cannot be
obtained directly by callers.

The helper is not a full temporal-safety proof. It is a construction rule:
call sites can no longer accidentally put a stack value in a narrower block
and use it afterward, because the stack value and the call are tied together
inside `CssTempListDecl::resolve()`.

---

## 5. Example Migration

The background-position shorthand code becomes:

```cpp
lam::CssTempListDecl<2> position_decl(
    decl, CSS_PROPERTY_BACKGROUND_POSITION);
position_decl.append(item);

if (i + 1 < value->data.list.count) {
    CssValue* next_item = value->data.list.values[i + 1];
    if (css_value_is_background_position_candidate(next_item)) {
        position_decl.append(next_item);
        i++;
    }
}

position_decl.resolve(lycon);
continue;
```

Single-value longhand routing uses `CssTempDecl`:

```cpp
lam::CssTempDecl color_decl(
    decl, CSS_PROPERTY_BACKGROUND_COLOR, item);
color_decl.resolve(lycon);
```

This makes shorthand expansion code read as "route this parsed shorthand part
to this longhand resolver" instead of "copy a declaration and hand-edit a raw
pointer field".

---

## 6. Compile-Time Coverage and Limits

What templates can mitigate:

1. Accidental copying of a temporary declaration helper.
2. Accidental extraction of the raw `CssDeclaration*` from the helper.
3. Accidental use of a helper after its owned scratch list has been separated
   from the resolve call.
4. Capacity mistakes for small shorthand lists, because `CssTempListDecl<N>`
   carries its maximum list size in the type.

What templates cannot detect by themselves:

1. Existing raw assignments to `CssDeclaration::value`.
2. A resolver that stores `decl->value` internally despite the resolve-only
   contract.
3. Lifetimes after a raw pointer escapes through legacy C APIs.
4. Arbitrary lexical stack lifetime through `CssValue*`; raw pointers erase it.

Because of those limits, templates should be paired with a source check.

---

## 7. Grep/Check Gate

Add a check target, for example `make check-css-temp-decl`, that flags raw
temporary-value assignment patterns in Radiant CSS resolution code:

```text
CssDeclaration .* = *decl
\.value = &
CssValue .* = {}
resolve_css_property\(.*&[a-zA-Z_][a-zA-Z0-9_]*_decl
```

The practical first rule should be simple and low-noise:

```text
radiant/resolve_css_style.cpp: any ".value = &" outside css_temp_decl helpers
```

Allowed cases should use an explicit marker, similar to `INT_CAST_OK`:

```cpp
decl.value = &local_value; // CSS_TEMP_DECL_OK: consumed before local scope exits.
```

The preferred path is to need very few markers because shorthand expansion uses
`CssTempDecl` or `CssTempListDecl`.

This check is important because `CssDeclaration` remains a public C struct.
Without a check, new raw assignments can bypass the helper completely.

---

## 8. Relationship to `PersistentField`

The existing domain template design still matters for persistent CSS data:

```cpp
template<class T, class StorageDomain>
class PersistentField;
```

Use it, or a setter-only equivalent, for CSS values retained by style trees,
DOM nodes, CSSOM shadow declarations, animations, or computed style caches.
Those retained objects should not point to layout-session or stack scratch.

For shorthand expansion, however, `PersistentField` is the wrong primary tool:
the desired lifetime is deliberately short. The right model is:

| Site | Desired wrapper |
|---|---|
| Style tree / DOM / CSSOM retained declaration | `PersistentField<CssValue, PoolDomain>` or pool-only creation helper |
| Shorthand longhand routing inside `resolve_css_property()` | `CssTempDecl` / `CssTempListDecl<N>` |
| External CSS parser output | pool allocation checked at parser boundary |
| JS style mutation creating synthetic declarations | pool promotion before retaining |

This split keeps the design honest: retained CSS data is domain-checked, while
resolve-only CSS data is scope-bound.

---

## 9. Resolver Contract

To make `CssTempDecl` safe, longhand resolvers must follow this rule:

```text
resolve_css_property() may read decl->value during the call.
It must not retain decl, decl->value, or child pointers owned by a resolve-only helper.
```

Most longhand handlers already compute into `ViewSpan`, `ViewBlock`,
`BackgroundProp`, etc. They should copy the resolved result, not store the
input `CssValue*`.

When a handler truly needs to retain a CSS value, it should require a different
API that makes retention explicit:

```cpp
void retain_css_property_from_pool(
    CssPropertyId prop_id,
    const CssDeclaration* decl,
    LayoutContext* lycon);
```

Do not let a resolve-only helper call a retain API.

---

## 10. Suggested Phasing

> Status: Phases 1–4 are **implemented and verified** (see §0). The notes below
> are the original plan, annotated with what shipped.

### Phase 1: Add helpers and tests  ✅ Done

1. Add `radiant/css_temp_decl.hpp`.
2. Add a small compile/runtime test covering:
   - one-value `CssTempDecl`;
   - two-value `CssTempListDecl<2>`;
   - non-copyable helper behavior;
   - capacity overflow returns false.
3. Do not change `CssDeclaration` ABI.

### Phase 2: Migrate high-risk shorthand expansion  ✅ Done (background + gap)

Convert manual copied-declaration call sites in `resolve_css_style.cpp`,
starting with:

1. `background`;
2. `background-size` slash routing;
3. `gap`;
4. `font`;
5. border/padding/margin shorthands that synthesize lists.

Keep each conversion behavior-preserving. Do not reinterpret CSS syntax while
doing the safety migration.

Shipped: every `CssDeclaration … = *decl` copy and both `.value = &local`
sites in `resolve_css_style.cpp` were migrated — the two synthetic-list sites
(`background-size`, `background-position`, the actual stack-lifetime bug class)
to `CssTempListDecl<2>`, and the background var()/multi-layer/url/color/repeat
routing plus `gap` (single + two-value) to `CssTempDecl`. `font` and
border/padding/margin did not have raw copied-declaration list synthesis in this
file, so no migration was needed there; the `check-css-temp-decl` gate keeps the
file clean going forward.

### Phase 3: Add the check target  ✅ Done

Add `make check-css-temp-decl` and run it in the same spirit as
`make check-int-cast`.

Initial policy:

1. reject `.value = &` in `radiant/resolve_css_style.cpp`;
2. allow only `CSS_TEMP_DECL_OK` markers for legacy exceptions;
3. prefer replacing exceptions with `CssTempDecl` helpers.

Shipped: `make check-css-temp-decl` flags both `CssDeclaration … = *decl` raw
copies and `.value = &` address-of assignments in `resolve_css_style.cpp`,
allowing audited exceptions via a trailing `// CSS_TEMP_DECL_OK: <reason>`.

### Phase 4: Persistent CSS value audit  ✅ Done

After shorthand temporaries are contained, audit retained CSS value storage:

1. style tree declarations;
2. DOM inline style declarations;
3. CSSOM-created declarations;
4. animation keyframe declarations;
5. computed-style caches.

Use the domain model from `Memory_Safety_Template.md` for retained values.

Shipped: full audit in `vibe/CSS_Value_Retention_Audit.md`. All production
retention is pool/arena-stable (SAFE); inline-style, JS/CSSOM mutation, and
animation keyframes all allocate from `doc->pool` / `rule->pool` /
`doc->arena`. Computed-style cache fields are dead (never assigned). The single
finding is a latent, test-only cross-pool aliasing in `style_tree_clone` /
`style_tree_create_subset`, now documented with explicit in-code lifetime
contracts (`css_style_node.cpp`, `dom_element.cpp`) instead of a speculative
deep-copy on an unreached path. A `check-` lint was intentionally not added:
the residual risk is semantic cross-pool aliasing through `void* value`, not a
grep-able pattern.

---

## 11. Expected Payoff

This would have prevented the background shorthand ASan bug structurally:

1. the call site would not assign `position_decl.value = &position_list`;
2. the synthetic list storage would live inside the helper until
   `resolve_css_property()` returns;
3. grep/check enforcement would flag future raw `decl.value = &local` patterns.

It also improves maintainability. Shorthand expansion is currently a long
sequence of copied declarations and pointer edits. A resolve-only helper makes
the intended operation visible: build a short-lived view of one shorthand
component, route it through an existing longhand resolver, then discard it.

---

## 12. Additional Pattern: JS Companion Map Item Construction

This session also exposed a related memory-safety class outside CSS:
ad-hoc construction of tagged `Item` values from raw container pointers.

### 12.1 Concrete Bug: Companion Map Pointer Corruption

JS arrays use `arr->extra` as a companion property map pointer. That companion
map stores sparse properties, descriptors, accessors, and special array/object
metadata. The unsafe shape was in array deletion code:

```cpp
Item pm_item = (Item){.item =
    (uint64_t)(uintptr_t)arr->extra | ((uint64_t)LMD_TYPE_MAP << 56)};
```

This is wrong for Lambda container values. Packed scalar items carry their tag
in the high byte of `Item`, but container items are direct pointers to objects
whose first field is `type_id`. A `Map*` companion pointer must therefore be
wrapped as:

```cpp
Item pm_item = (Item){.map = (Map*)(uintptr_t)arr->extra};
```

OR-ing `LMD_TYPE_MAP << 56` into the pointer corrupts the address itself. The
failure surfaced when jQuery/Sizzle shifted a `RegExp.exec()` result array:
`Array.prototype.shift()` deletes the last indexed slot, and the regex result
array has companion properties such as `index`, `input`, and `groups`. That
combination routed through the corrupted companion-map `Item` and crashed.

### 12.2 Why Templates Alone Did Not Catch It

`Memory_Safety_Template2.md` already defines the right general model:
`ItemOf<Tag>`, `GcPtr<T>`, typed tag witnesses, and `make check-item-cast`.
However, the JS companion-map bridge still has many raw `arr->extra` sites.
When a call site manually manufactures an `Item` with `.item = ...`, it bypasses
the typed witness layer entirely.

The lesson is the same as the CSS temporary-lifetime bug: templates only help
after unsafe representation changes are forced through a typed helper. A raw C
field such as `arr->extra` or `CssDeclaration::value` can still encode the wrong
operation unless direct writes and direct casts are made searchable and rare.

### 12.3 Proposed Helper

Add a single JS helper for converting an array companion map pointer into an
`Item`. Keep the raw storage detail in one place:

```cpp
static inline Map* js_array_companion_map(Array* arr) {
    return (arr && arr->extra != 0) ? (Map*)(uintptr_t)arr->extra : NULL;
}

static inline Item js_array_companion_map_item(Array* arr) {
    Map* map = js_array_companion_map(arr);
    assert(map && "array has no companion map");
    return (Item){.map = map};
}
```

For internal C++ helpers that are not MIR/`extern "C"` boundaries, prefer the
typed form:

```cpp
static inline Item js_array_companion_map_item(lam::GcPtr<Array> arr) {
    Map* map = js_array_companion_map(arr.get());
    assert(map && "array has no companion map");
    return (Item){.map = map};
}
```

Then deletion, descriptor, and accessor code should call the helper instead of
rebuilding the `Item` locally. This creates a natural migration path toward the
Template2 model without changing the C ABI or the `Array` layout.

### 12.4 Proposed Lint

Add `make check-js-item-construction` or fold the rule into
`make check-item-cast`.

Initial policy:

1. reject `LMD_TYPE_MAP << 56` and similar high-byte OR tagging in `lambda/js/`
   except in a small allowlisted bridge file;
2. reject `(Item){.item = (uint64_t)(uintptr_t)...}` for container pointers;
3. require helper calls such as `js_array_companion_map_item(...)`;
4. allow scalar packed-item construction only through existing helpers such as
   `i2it`, `b2it`, and `s2it`;
5. add an explicit marker such as `ITEM_PACK_OK` only for audited low-level
   packing code.

This check would have flagged the bad companion-map construction before
runtime. ASan can catch the resulting crash, but the lint catches the root
representation mistake at review/build time.

### 12.5 Expected Payoff

This prevents a whole class of tag/pointer confusion:

1. companion maps cannot be accidentally pointer-corrupted;
2. future JS array descriptor code has one obvious way to obtain the companion
   map `Item`;
3. migrated helpers can accept `lam::GcPtr<Array>` and return a valid
   `ItemOf<LMD_TYPE_MAP>` or raw `Item` at the boundary;
4. the unsafe representation rule becomes searchable instead of tribal
   knowledge.

## 13. Additional Pattern: Retained JS Source Lifetime

The JS MIR/eval fixes exposed another raw-lifetime class: source text was freed
while AST/source ranges or generated MIR code could still refer to it.

### 13.1 Concrete Bugs: Dynamic Function and Preamble Source

Two paths had the same ownership shape:

1. `eval()` / `new Function()` built source text dynamically, parsed it, built
   an AST, then freed the source buffer too early.
2. Module/preamble lowering accepted caller-provided source memory and retained
   AST/source metadata across MIR context lifetime.

The unsafe shape is:

```cpp
char* source = build_dynamic_source(...);
JsTranspiler* tp = js_transpiler_create(source);
JsAstNode* ast = build_js_ast(tp, root);
mem_free(source);
lower_ast_to_mir(ast);
```

This is wrong when `tp->source`, AST source ranges, early-error metadata, or
generated MIR debug/source metadata still point at the source bytes. The local
fix kept the source buffer alive beside the deferred MIR context and freed it
when that context is cleaned up.

### 13.2 Proposed Template: RetainedSource and Deferred Owner

Create a tiny owner type for source buffers that are allowed to outlive the
parsing helper:

```cpp
class JsRetainedSource {
    char* chars_;

public:
    explicit JsRetainedSource(char* chars) : chars_(chars) {
        assert(chars && "retained JS source cannot be null");
    }
    JsRetainedSource(const JsRetainedSource&) = delete;
    JsRetainedSource& operator=(const JsRetainedSource&) = delete;
    ~JsRetainedSource() {
        if (chars_) mem_free(chars_);
    }

    const char* chars() const { return chars_; }
    char* release() {
        char* out = chars_;
        chars_ = NULL;
        return out;
    }
};
```

Then replace parallel cleanup arrays with one owner record:

```cpp
struct JsMirDeferredOwner {
    MIR_context_t ctx;
    char* source_buffer;
    NamePool* name_pool;
    Pool* ast_pool;
};
```

The important invariant is not the exact class name. The invariant is:

1. if AST/source metadata is retained, source bytes move into the retained
   owner;
2. if lowering fails before transfer, the local owner frees the source;
3. deferred MIR cleanup owns source, name pool, AST pool, and MIR context as
   one lifetime bundle.

### 13.3 Proposed Lint

Add a `make check-js-source-lifetime` rule.

Initial policy:

1. flag `mem_free(source)` in JS parsing/lowering files when `source` has been
   passed into `js_transpiler_create`, `js_parse_source`, or equivalent;
2. allow the free only when a local marker such as `SOURCE_TRANSFER_OK` shows
   ownership was moved into `JsRetainedSource` or `JsMirDeferredOwner`;
3. flag retained preamble/deferred state structs that keep AST/name pools
   without a matching source owner field.

This would have caught the premature free structurally. ASan might catch a
use-after-free only when the freed storage is reused soon enough; the owner
type makes the intended source lifetime explicit at compile/review time.

## 14. Additional Pattern: Batch Heap Reset Ownership

The JS batch/hot-reload fix exposed a reset-path ownership issue. Some reset
paths destroyed the heap while `batch_context` still held name-pool and type-list
state associated with the previous runtime context.

### 14.1 Concrete Bug: Partial Runtime Teardown

The unsafe shape was:

```cpp
js_batch_reset();
heap_destroy();
jm_cleanup_deferred_mir();
```

Other reset paths released more state before the heap went away. The incomplete
path left stale context-owned memory around after a heap recycle. Later tests
could then run with state that belonged to the previous runtime lifetime.

### 14.2 Proposed Template: Runtime Context Owner

Centralize batch teardown in a helper that owns the reset order:

```cpp
struct JsBatchRuntimeOwner {
    NamePool* name_pool;
    ArrayList* type_list;
    MIR_context_t batch_mir_context;
};

static void js_batch_runtime_owner_destroy(JsBatchRuntimeOwner* owner) {
    if (!owner) return;
    if (owner->name_pool) {
        name_pool_release(owner->name_pool);
        owner->name_pool = NULL;
    }
    if (owner->type_list) {
        arraylist_free(owner->type_list);
        owner->type_list = NULL;
    }
}
```

The helper should be the only API that batch recycle paths call before
`heap_destroy()`. That prevents reset paths from drifting apart.

For a stronger Template3-style guard, add a heap epoch to retained runtime
pointers:

```cpp
struct JsHeapEpochPtr {
    void* ptr;
    uint64_t epoch;
};
```

Debug builds can assert that the pointer epoch matches the current JS heap
epoch before use. That turns "state from the old heap survived" into an
immediate invariant failure instead of a delayed crash in an unrelated test.

### 14.3 Proposed Lint

Add `make check-js-reset-paths`.

Initial policy:

1. flag direct `heap_destroy()` calls in JS batch/main paths unless preceded by
   `js_batch_runtime_owner_destroy` in the same teardown helper;
2. flag direct `name_pool_release(batch_context.name_pool)` duplicates outside
   that helper once migration is complete;
3. require every heap recycle path to call the same teardown helper.

This is not a template-only fix. The template makes the ownership bundle
explicit; the lint keeps reset paths from reintroducing partial teardown.

## 15. Additional Pattern: Non-Null Native Callbacks

Two JS native-function paths created functions with null callback pointers.
The immediate fix was to replace them with real no-op native functions, but the
safer model is to make null callbacks unrepresentable in normal code.

### 15.1 Concrete Bug: Null Native Function Pointer

The unsafe shape was:

```cpp
Item fn = js_new_function(NULL, 0);
```

or:

```cpp
Item fn = js_new_function(nullptr, 0);
```

That creates an object that looks callable but has no native target. Whether it
crashes depends on later call flow, which makes the bug easy to miss.

### 15.2 Proposed Template: JsNativeCallback

Wrap callback pointers in a non-null type:

```cpp
struct JsNativeCallback {
    void* ptr;

    explicit JsNativeCallback(void* callback) : ptr(callback) {
        assert(callback && "JS native callback cannot be null");
    }
};

static inline Item js_new_native_function(JsNativeCallback callback, int argc) {
    return js_new_function(callback.ptr, argc);
}
```

Call sites become:

```cpp
static Item js_noop(void) {
    return make_js_undefined();
}

Item fn = js_new_native_function(JsNativeCallback((void*)js_noop), 0);
```

The C ABI can keep `js_new_function(void*, int)` for MIR or low-level bridge
code, but ordinary C++ runtime code should use the non-null wrapper.

### 15.3 Proposed Lint

Add `make check-js-native-callbacks`.

Initial policy:

1. reject `js_new_function(NULL` and `js_new_function(nullptr`;
2. reject raw `js_new_function((void*)` in migrated C++ files, requiring
   `js_new_native_function(JsNativeCallback(...))`;
3. allow direct `js_new_function` only in a small bridge layer with an explicit
   marker such as `JS_NATIVE_CALLBACK_BRIDGE_OK`.

This maps directly to the `NonNull<T>` idea in `Memory_Safety_Template3.md`:
the pointer is still pointer-sized and ABI-neutral, but normal construction
asserts the invariant at the boundary.

## 16. Additional Pattern: Borrowed Registry Element Lifecycle

The `test_serve_gtest.exe` crash exposed a stack lifetime issue with a
different shape from the CSS temporary declaration bug. The pointer was not
used after scope by the same function. Instead, a registry stored an external
pointer and later destroyed the registry as if it owned the pointed-to backend
lifecycle.

### 16.1 Concrete Bug: Registry Shutdown of Stack Backend

`BackendRegistryTest.AddAndFind` registers a stack-allocated backend:

```cpp
TEST_F(BackendRegistryTest, AddAndFind) {
    static const char *lambda_exts[] = {".ls"};
    LanguageBackend backend;
    memset(&backend, 0, sizeof(backend));
    backend.name = "lambda";
    backend.extensions = lambda_exts;
    backend.extension_count = 1;

    EXPECT_EQ(backend_registry_add(registry, &backend), 0);
    ...
}
```

The test fixture owns only the registry. The backend object is a local variable
owned by the test body. When the test body returns, `backend` is dead. The old
registry destroy path then called:

```cpp
void backend_registry_destroy(BackendRegistry *registry) {
    if (!registry) return;
    backend_registry_shutdown_all(registry);
    serve_free(registry);
}
```

That walked `registry->backends[]` and dereferenced a pointer to the dead stack
object. The immediate symptom was a segmentation fault after
`BackendRegistryTest.AddAndFind` completed.

The local fix was to align `backend_registry_destroy()` with the actual
ownership model:

```cpp
void backend_registry_destroy(BackendRegistry *registry) {
    if (!registry) return;
    serve_free(registry);
}
```

Callers that initialize backend lifecycle must now call
`backend_registry_shutdown_all()` explicitly while the backend objects are still
alive. This matches the existing server-side contract that backend registries
point at external backends and are not owned by `Server`.

### 16.2 Root Cause

The raw pointer field encodes two different concepts:

```cpp
struct BackendRegistry {
    LanguageBackend *backends[MAX_BACKENDS];
    int count;
};
```

At registration time, `LanguageBackend*` means "borrow this backend for lookup
and dispatch." At destroy time, the old implementation treated the same pointer
as "owned backend whose shutdown callback may be invoked." That ownership
meaning is not visible in the type.

This is not a null-check problem. The pointer can be non-null and still invalid
because its pointee's lexical lifetime ended. It is also not solved by making
the test backend static; that would hide the immediate crash while preserving
the wrong registry ownership contract.

### 16.3 Proposed Template: Borrowed Registry vs Lifecycle Owner

Split "borrowed lookup table" from "lifecycle owner" in the type system.

For borrowed registries, store a wrapper that cannot free or shut down the
element:

```cpp
template<class T>
class BorrowedPtr {
    T* ptr_;

public:
    explicit BorrowedPtr(T* ptr) : ptr_(ptr) {
        assert(ptr && "borrowed pointer cannot be null");
    }

    T* get() const { return ptr_; }
};

struct BackendRegistry {
    BorrowedPtr<LanguageBackend> backends[MAX_BACKENDS];
    int count;
};
```

The important invariant is:

```text
Borrowed registries may look up and iterate borrowed objects.
Borrowed registry destroy functions may only free the registry container.
They must not call shutdown/free/destruct callbacks on borrowed elements.
```

For code that truly owns backend initialization and shutdown, introduce a
different owner type:

```cpp
struct BackendRuntimeOwner {
    BackendRegistry* registry;
    bool initialized;
};

static void backend_runtime_owner_destroy(BackendRuntimeOwner* owner) {
    if (!owner) return;
    if (owner->initialized) {
        backend_registry_shutdown_all(owner->registry);
        owner->initialized = false;
    }
    backend_registry_destroy(owner->registry);
    owner->registry = NULL;
}
```

This makes shutdown authority explicit. A plain `BackendRegistry` is only a
borrowed index. A `BackendRuntimeOwner` owns the lifecycle order.

### 16.4 Proposed Lint

Add a focused serve check, for example `make check-serve-registry-ownership`.

Initial policy:

1. flag `backend_registry_destroy()` or other `*_registry_destroy()` helpers
   that call `*_shutdown_all()` directly;
2. flag structs with fields named `backends`, `handlers`, `routes`, or
   `callbacks` that are documented as external/borrowed but whose destroy
   function calls element lifecycle callbacks;
3. flag registry tests that register `&local_stack_object` unless the registry
   API is explicitly documented as borrowed;
4. require lifecycle teardown through an owner helper such as
   `backend_runtime_owner_destroy(...)`;
5. allow legacy exceptions only with a marker such as
   `BORROWED_REGISTRY_DESTROY_OK: <reason>`.

A simple first grep rule would already catch this bug shape:

```text
lambda/serve/*registry*.cpp:
    *_registry_destroy(...) contains *_shutdown_all(...)
```

Then refine the check by matching nearby comments such as "external",
"borrowed", "not owned", or "registry stores external pointers".

### 16.5 Expected Payoff

This prevents a class of lifetime bugs where a container of raw pointers drifts
from borrowed lookup into owner teardown:

1. tests can safely register stack fixtures in borrowed registries;
2. production code has one explicit place to perform backend shutdown when it
   owns initialized backend lifecycle;
3. destroy functions become mechanically reviewable: borrowed destroy frees
   only the container, owner destroy tears down elements first;
4. future serve registries cannot silently acquire element-lifecycle behavior
   without tripping the check.

## 17. Render Clip Shape Stack Lifetime

The render batch failure in `make test-render` exposed a stack lifetime issue
that was close to the CSS temporary declaration bug, but easier to miss in
review because the pointer escaped through a render-state stack rather than a
single copied declaration.

### 17.1 Concrete Bug: Clip Scope Stored a Stack Shape

`radiant/render_clip.cpp` builds synthetic clip shapes for rectangular clips
and overflow clips. Before the fix, those shapes lived inside the local
`RenderClipScope` object:

```cpp
RenderClipScope scope = {};
scope.inline_shape.type = CLIP_SHAPE_ROUNDED_RECT;
scope.inline_shape.rounded_rect = {...};

render_clip_push_shape_scope(rdcon, &scope, &scope.inline_shape);
return scope;
```

`render_clip_push_shape_scope()` then stored that pointer in the render
context:

```cpp
rdcon->clip_shapes[rdcon->clip_shape_depth++] = shape;
```

The returned `RenderClipScope` is a copy. The pointer in
`rdcon->clip_shapes[]` still pointed at the callee's stack frame, not at the
returned scope object. Later positioned/nested rendering reused that stack
memory, and background/shadow rasterization read the stale `ClipShape*` through
`clip_shape_to_params()`. ASan reported the failure as a stack-buffer-overflow
near a later render walk frame because the stale pointer no longer pointed into
the original scope frame.

The fix was to remove the scope-owned inline shape and clone synthetic rect and
overflow shapes into `RenderContext::scratch` before pushing them onto
`rdcon->clip_shapes[]`. CSS clip-path shapes were already allocated from the
same scratch arena. `RenderClipScope::owns_shape` remains responsible for
freeing scratch-owned shapes when the clip scope is popped.

### 17.2 Root Cause

The unsafe field was not `RenderClipScope::shape` by itself. A scope object may
borrow a shape pointer safely if the pointee outlives all rendering that can
observe the pushed clip. The bug was that a long-lived render-state stack
accepted an arbitrary raw pointer with no storage-domain proof:

```cpp
ClipShape* clip_shapes[RDT_MAX_CLIP_SHAPES];
```

That pointer can mean several different things:

1. a CSS clip-path shape allocated in `RenderContext::scratch`;
2. a synthetic clip shape allocated in `RenderContext::scratch`;
3. a temporary stack `ClipShape` built only to call a helper;
4. a borrowed retained/display-list copy with a separate lifetime.

The third meaning is invalid once the pointer is stored in
`RenderContext::clip_shapes`, because rendering can re-enter nested paint code
before the clip is popped, and the original callee stack frame is already gone.

This is the generalized pattern:

```text
Do not store `&local` or `&field_of_local` in a context, stack, queue, display
list, callback, registry, or deferred work item unless that container is proven
to be consumed before the local scope ends.
```

Returning the local object by value does not rescue pointers to the original
local object's fields. Those pointers still name the old frame.

### 17.3 Proposed Template: Stable Pushed Pointers

For render-state stacks, prefer a small storage-qualified wrapper instead of a
plain pointer API. The important idea is that the push helper should accept
only pointers whose storage domain is stable for the active scope:

```cpp
template<class T>
class ScratchOwnedPtr {
    T* ptr_;

public:
    explicit ScratchOwnedPtr(T* ptr) : ptr_(ptr) {
        assert(ptr && "scratch-owned pointer cannot be null");
    }

    T* get() const { return ptr_; }
};

static ScratchOwnedPtr<ClipShape> render_clip_make_shape(
    ScratchArena* scratch, const ClipShape* shape);

static bool render_clip_push_shape_scope(
    RenderContext* rdcon,
    RenderClipScope* scope,
    ScratchOwnedPtr<ClipShape> shape);
```

The C-style version can use naming and constructors if a template wrapper is
too invasive:

```cpp
ClipShape* render_clip_clone_shape(ScratchArena* scratch, const ClipShape* shape);
bool render_clip_push_scratch_shape_scope(RenderContext* rdcon,
                                          RenderClipScope* scope,
                                          ClipShape* scratch_shape);
```

The rule is the same in either form: callers may build a temporary
`ClipShape` on the stack, but they must clone/promote it before inserting it
into `RenderContext::clip_shapes`.

### 17.4 Proposed Lint

Add a focused check, for example `make check-render-clip-lifetime`.

Initial policy:

1. reject `&scope.inline_shape` in `radiant/render_clip*.cpp`;
2. reject `rdcon->clip_shapes[...] = &...`;
3. reject `render_clip_push_shape_scope(..., &local...)` unless marked with
   `RENDER_CLIP_STACK_OK: <reason>`;
4. reject `RenderClipScope` fields that embed `ClipShape` storage;
5. require synthetic `ClipShape` values to pass through
   `render_clip_clone_shape()` or a storage-qualified helper before push.

This should be a grep-first check. The goal is not perfect pointer analysis;
it is to keep the high-risk address-taking shape from returning quietly.

### 17.5 Generalized Safety Rule

Any raw pointer inserted into a longer-lived container should be reviewed as a
storage-domain transition:

| Source storage | Destination | Default policy |
|---|---|---|
| stack local | context stack / registry / display list / deferred task | reject or clone/promote |
| stack field of returned-by-value object | any retained raw pointer field | reject |
| scratch arena | active render/layout scope | allow if popped before arena reset |
| pool/GC allocation | retained tree/cache | allow through ownership/domain helper |
| borrowed external object | lookup registry | allow only if destroy path never owns element lifecycle |

The mechanical review question is:

```text
Will every use of this pointer finish before the pointee's storage can be
reused, destroyed, or reset?
```

If the answer depends on informal call ordering, introduce a helper whose type
or name encodes the storage domain, and add a lint rule for the raw-pointer
escape pattern.

## 18. Open Questions

1. Should `CssTempDecl` live in `radiant/` only, or in `lambda/input/css/` so
   CSSOM and parser-side synthetic declarations can share it?
2. Should `resolve_css_property()` get a typed overload that accepts only
   `CssTempDecl` / persistent declarations, leaving the raw pointer overload as
   a legacy adapter?
3. Should the check target scan only `resolve_css_style.cpp` first, or all
   `radiant/*.cpp` files?
4. Should resolve-only declarations carry a debug flag so accidental retention
   can be asserted at runtime when a retained field setter sees them?
5. Should `CssDeclaration::value` eventually become private behind setters, or
   is the C ABI requirement strong enough that helper-plus-check is the right
   long-term compromise?
6. Should the JS companion-map helper return raw `Item`, `ItemOf<LMD_TYPE_MAP>`,
   or both, given that some callers remain at C ABI boundaries?
7. Should the item-construction lint cover all `lambda/` code immediately, or
   start with `lambda/js/` where `arr->extra` companion maps are concentrated?
8. Should `JsRetainedSource` own only raw source bytes, or should it also carry
   the source name and line-offset metadata used for diagnostics?
9. Should `JsMirDeferredOwner` replace the existing parallel deferred arrays in
   one migration, or first wrap the arrays behind owner-style helper functions?
10. Should heap epochs be debug-only assertions, or should release builds also
    check them and report a LambdaJS internal error?
11. Should `js_new_function(void*, int)` be made private to a bridge file after
    `JsNativeCallback` migration, or kept public with lint enforcement only?
12. Should `BackendRegistry` stay a borrowed-only container permanently, or
    should there be a separate `OwningBackendRegistry` for standalone tools?
13. Should the serve ownership check scan only `lambda/serve/` first, or all
    registry-like containers in `lambda/`?
14. Should borrowed-pointer wrappers be introduced broadly, or only at registry
    boundaries where destroy/shutdown confusion is likely?
15. Should `RenderContext::clip_shapes` accept only a storage-qualified wrapper,
    or is `render_clip_clone_shape()` plus `make check-render-clip-lifetime`
    enough for the current render architecture?
