# CSS Shorthand Temporary Lifetime Safety

> Companion to [`Memory_Safety_Template.md`](./Memory_Safety_Template.md),
> [`Memory_Safety_Template2.md`](./Memory_Safety_Template2.md), and
> [`Memory_Safety_Template3.md`](./Memory_Safety_Template3.md).
>
> Those docs cover domain ownership, typed tagged values, bounds/overflow
> safety, and runtime UAF handles. This doc covers a narrower but important
> lifetime class found in Radiant CSS shorthand expansion: a synthetic
> `CssDeclaration` temporarily points at a stack-built `CssValue`.

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

### Phase 1: Add helpers and tests

1. Add `radiant/css_temp_decl.hpp`.
2. Add a small compile/runtime test covering:
   - one-value `CssTempDecl`;
   - two-value `CssTempListDecl<2>`;
   - non-copyable helper behavior;
   - capacity overflow returns false.
3. Do not change `CssDeclaration` ABI.

### Phase 2: Migrate high-risk shorthand expansion

Convert manual copied-declaration call sites in `resolve_css_style.cpp`,
starting with:

1. `background`;
2. `background-size` slash routing;
3. `gap`;
4. `font`;
5. border/padding/margin shorthands that synthesize lists.

Keep each conversion behavior-preserving. Do not reinterpret CSS syntax while
doing the safety migration.

### Phase 3: Add the check target

Add `make check-css-temp-decl` and run it in the same spirit as
`make check-int-cast`.

Initial policy:

1. reject `.value = &` in `radiant/resolve_css_style.cpp`;
2. allow only `CSS_TEMP_DECL_OK` markers for legacy exceptions;
3. prefer replacing exceptions with `CssTempDecl` helpers.

### Phase 4: Persistent CSS value audit

After shorthand temporaries are contained, audit retained CSS value storage:

1. style tree declarations;
2. DOM inline style declarations;
3. CSSOM-created declarations;
4. animation keyframe declarations;
5. computed-style caches.

Use the domain model from `Memory_Safety_Template.md` for retained values.

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

## 12. Open Questions

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
