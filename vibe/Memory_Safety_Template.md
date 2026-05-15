# Compile-Time Memory Ownership & Tagged Union Safety for Lambda/Radiant

## Motivation

Lambda/Radiant code today mixes several allocation strategies — GC heap, pool/arena, memtracked session memory, plain `malloc`, and the occasional `std::`-adjacent container. We want to tighten the contract so that **only three** memory domains exist, and the rules governing them are enforced as much as possible at compile time, without paying the runtime cost of Fil-C-style capability pointers.

Target rules:

1. **GC heap** — script runtime values, traced.
2. **Pool/arena** — bulk-freed lifetime, no individual destruction.
3. **Memtracked session memory** — temporally scoped to a session of work (e.g. a layout pass); must be manually freed.

Additional invariant: **pool/GC-allocated objects must not hold pointers into session memory** (or any shorter-lived domain) unless ownership has been formally transferred (copied/promoted) into the destination domain. This prevents dangling-pointer bugs when a session ends but pool objects persist.

This document proposes several header-only facilities under `lib/`, modelled after Microsoft GSL (`owner<T>`, `not_null<T>`) but stricter, combining a domain-tagged ownership model, ownership-aware arrays, and compile-time tagged-union dispatch.

**Implementation order**: Radiant first (where session ↔ pool ownership boundaries are clearest and the `View*` discriminated union is most painful today). Once the templates are validated there, migrate to Lambda.

---

## 1. Three-Domain Ownership Model via Templates

### Domain tags

```cpp
// lib/ownership.hpp
namespace lam {

// allocation domain tags (empty types; pure compile-time markers)
struct GcHeapDomain        {};   // GC heap, traced by collector
struct PoolDomain          {};   // pool/arena, freed in bulk
struct LayoutSessionDomain {};   // memtracked, scoped to one layout pass

// Future session variants — add only when an actual boundary appears:
//   struct RenderSessionDomain {};
//   struct ParserSessionDomain {};
//   struct ValidatorSessionDomain {};

} // namespace lam
```

Start with three domains. Splitting `LayoutSessionDomain` into `RenderSessionDomain` etc. happens only when a concrete cross-session leakage bug or API boundary forces it.

### Borrowed vs Owned

Pool/arena pointers are almost always **borrowed** — the arena owns the storage and frees it in bulk. Putting RAII on individual pool pointers is misleading. Session memory, by contrast, is **owned** per-pointer and must be manually freed.

```cpp
// non-owning view, any domain
template<class T, class Domain>
class BorrowedPtr {
    T* p_ = nullptr;
public:
    constexpr BorrowedPtr() = default;
    explicit constexpr BorrowedPtr(T* p) : p_(p) {}

    T*  get()        const { return p_; }
    T*  operator->() const { return p_; }
    T&  operator*()  const { return *p_; }
    explicit operator bool() const { return p_ != nullptr; }
};

// owning pointer, parameterised by domain (move-only)
template<class T, class Domain>
class OwnedPtr {
    T* p_ = nullptr;
public:
    OwnedPtr() = default;
    explicit OwnedPtr(T* p) : p_(p) {}
    OwnedPtr(OwnedPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    OwnedPtr& operator=(OwnedPtr&& o) noexcept { reset(o.release()); return *this; }
    OwnedPtr(const OwnedPtr&)            = delete;
    OwnedPtr& operator=(const OwnedPtr&) = delete;
    ~OwnedPtr() { reset(); }

    T*   get()        const { return p_; }
    T*   operator->() const { return p_; }
    T*   release() { T* t = p_; p_ = nullptr; return t; }
    void reset(T* n = nullptr) {
        if (p_) DomainTraits<Domain>::destroy(p_);
        p_ = n;
    }

    // implicit decay to non-owning borrow
    operator BorrowedPtr<T, Domain>() const { return BorrowedPtr<T, Domain>(p_); }
};

// domain destroy policies
template<class Domain> struct DomainTraits;
template<> struct DomainTraits<GcHeapDomain>        { template<class T> static void destroy(T*) {} };
template<> struct DomainTraits<PoolDomain>          { template<class T> static void destroy(T*) {} };
template<> struct DomainTraits<LayoutSessionDomain> {
    template<class T> static void destroy(T* p) { p->~T(); memtrack_free(p); }
};

// aliases
template<class T> using GcPtr      = BorrowedPtr<T, GcHeapDomain>;
template<class T> using PoolPtr    = BorrowedPtr<T, PoolDomain>;
template<class T> using SessionPtr = OwnedPtr   <T, LayoutSessionDomain>;
```

Rule of thumb:
- Returning from `pool_calloc` / `arena_alloc` → wrap in `PoolPtr<T>` (borrowed).
- Returning from `memtrack_alloc` → wrap in `SessionPtr<T>` (owned, RAII frees on scope exit).
- GC-managed pointers → `GcPtr<T>` (borrowed; collector owns storage).

### Allocation helpers

```cpp
template<class T>
PoolPtr<T> pool_make(Pool* pool) {
    return PoolPtr<T>(static_cast<T*>(pool_calloc(pool, sizeof(T))));
}

template<class T>
SessionPtr<T> session_make(MemCategory cat) {
    return SessionPtr<T>(static_cast<T*>(memtrack_calloc(1, sizeof(T), cat)));
}

inline OwnedPtr<char, LayoutSessionDomain>
session_strdup(const char* s, MemCategory cat) {
    return OwnedPtr<char, LayoutSessionDomain>(memtrack_strdup(s, cat));
}
```

### Debug ownership assertions

Boundary constructors validate provenance under debug builds; release builds compile the check away.

```cpp
template<class T>
PoolPtr<T> checked_pool_ptr(Pool* pool, T* raw) {
#ifndef NDEBUG
    if (raw && !pool_owns(pool, raw)) {
        log_error("checked_pool_ptr: pointer not owned by pool");
        assert(false);
    }
#endif
    return PoolPtr<T>(raw);
}
```

Useful queries (some require small additions to existing allocators):
- `pool_owns(pool, ptr)` / `arena_owns(arena, ptr)` — range check against pool blocks.
- `memtrack_owns(ptr)` — optional, if memtrack maintains a registry.

### Domain lifetime trait

The pool↔session invariant is really one rule applied to many container shapes (raw pointer fields, list fields, future map/set fields). State the rule once as a trait, then have each retained-field wrapper consult it:

```cpp
// DomainOutlives<A, B>::value is true iff a value allocated in domain A
// is guaranteed to live at least as long as a container stored in domain B.
template<class A, class B> struct DomainOutlives : std::false_type {};

// reflexive cases
template<> struct DomainOutlives<PoolDomain,          PoolDomain>          : std::true_type {};
template<> struct DomainOutlives<GcHeapDomain,        GcHeapDomain>        : std::true_type {};
template<> struct DomainOutlives<LayoutSessionDomain, LayoutSessionDomain> : std::true_type {};

// pool/arena and GC are both "long-lived" relative to a session: a session
// container may freely borrow from them.
template<> struct DomainOutlives<PoolDomain,   LayoutSessionDomain> : std::true_type {};
template<> struct DomainOutlives<GcHeapDomain, LayoutSessionDomain> : std::true_type {};

// pool fields may borrow from GC (collector keeps it alive across the pool's lifetime
// — relies on the collector tracing through pool-rooted pointers; revisit if that changes).
template<> struct DomainOutlives<GcHeapDomain, PoolDomain> : std::true_type {};

// All other pairs (notably LayoutSessionDomain → PoolDomain or GcHeapDomain) are false.
```

When new domains are introduced (§7 Q2), each new pair must be added here explicitly. An unknown pair defaults to `false` — fail-closed.

### Enforcing the "no pool ← session linkage" rule — `PersistentField`

This is where the type system earns its keep. Use overload resolution with `= delete` rather than `static_assert` — better per-call-site diagnostics than a trait-driven check.

```cpp
// A raw-pointer field retained inside a struct living in StorageDomain.
// Accepts borrowed pointers from any domain that outlives StorageDomain.
template<class T, class StorageDomain>
class PersistentField {
    T* p_ = nullptr;
public:
    template<class SourceDomain>
    auto set(BorrowedPtr<T, SourceDomain> b)
        -> std::enable_if_t<DomainOutlives<SourceDomain, StorageDomain>::value>
    { p_ = b.get(); }

    // explicitly forbidden — must promote first
    template<class S>
    void set(OwnedPtr<T, S>&&) = delete;
    template<class S>
    auto set(BorrowedPtr<T, S> b)
        -> std::enable_if_t<!DomainOutlives<S, StorageDomain>::value> = delete;

    T* get() const { return p_; }
};
```

(For the most common case `StorageDomain = PoolDomain`, this expands to the original two overloads accepting `BorrowedPtr<T, PoolDomain>` and `BorrowedPtr<T, GcHeapDomain>` and rejecting `BorrowedPtr<T, LayoutSessionDomain>`.)

Now writing:

```cpp
SessionPtr<char> tag = session_strdup("div", MEM_CAT_LAYOUT);
dom_element_fields.tag_name.set(std::move(tag));   // ❌ compile error
```

is the wrong code, and:

```cpp
SessionPtr<char> tag = session_strdup("div", MEM_CAT_LAYOUT);
PoolPtr<char>    stable = promote_to_pool(doc->pool, tag.get());
dom_element_fields.tag_name.set(stable);            // ✅
```

is the right one. The cross-domain transition is visible in code review.

### Explicit promotion vocabulary

Forbid generic `set` / `assign` for lifetime-crossing operations. Use dedicated, greppable names:

```cpp
template<class T> PoolPtr<T>   promote_to_pool(Pool* pool, const T* src);
template<class T> GcPtr<T>     copy_to_gc(Heap* heap, const T* src);
template<class T> SessionPtr<T> take_ownership(T* raw);   // wrap an existing alloc
template<class T> T*           detach_session_buffer(SessionPtr<T>& p) { return p.release(); }

inline PoolPtr<char> promote_to_pool(Pool* pool, const char* s) {
    return PoolPtr<char>(pool_strdup(pool, s));
}
```

`promote_to_*` performs a copy into the destination domain; the source `SessionPtr` continues to own its original allocation until scope exit, where the destructor frees it. This is the single most useful code-review signal in the design.

### Escape hatches

Anything that must bypass the rules — legacy C APIs, raw MIR runtime callbacks, `void*` round-trips — uses an `unsafe_*` helper, grep-checkable in the same spirit as the existing `RAWALLOC_OK` / `INT_CAST_OK` markers:

```cpp
template<class T, class D> T* unsafe_borrow_raw(BorrowedPtr<T, D> p) { return p.get(); }
template<class T, class D> T* unsafe_release(OwnedPtr<T, D>& p)      { return p.release(); }
```

Add `make check-unsafe-casts` mirroring `make check-int-cast`.

---

## 2. Typed Array Lists with Explicit Ownership

The legacy C `ArrayList` stores `void*` and cannot distinguish between borrowed pointers, owned pointers, integer-as-pointer sentinels, and domain-tagged ownership. The C++ replacement in `lib/arraylist.hpp` splits the two important cases:

1. `lam::ArrayList<T>` owns only the array element storage.
2. `lam::ArrayOwnedList<T, Domain>` owns the pointees through `OwnedPtr<T, Domain>`.

This keeps the most dangerous decision visible in the type name and element type. A list of borrowed pool views should not look the same as a list of session-owned objects that will be freed on `clear()`.

### `ArrayList<T>` — typed values and borrowed references

`ArrayList<T>` is the default typed replacement for the C list. It owns the backing storage and destroys stored `T` values, but it does not claim ownership of pointees stored inside pointer-like elements.

```cpp
lam::ArrayList<int> values(MEM_CAT_LAYOUT);
values.append(12);

lam::ArrayList<View*> legacy_views(MEM_CAT_LAYOUT);          // borrowed by convention
lam::ArrayList<lam::PoolPtr<View>> pool_views(MEM_CAT_LAYOUT); // borrowed by type
```

`ArrayList<T>` rejects `OwnedPtr<T, Domain>` at compile time:

```cpp
lam::ArrayList<lam::SessionPtr<Image>> images; // compile error
```

That error is intentional. A generic list should not silently become responsible for freeing domain-owned pointees.

### `ArrayOwnedList<T, Domain>` — owned pointees

`ArrayOwnedList<T, Domain>` stores `OwnedPtr<T, Domain>` internally and exposes borrowed access for normal reads. Destruction, `clear()`, and `remove()` free the pointees through `DomainTraits<Domain>`.

```cpp
lam::ArrayOwnedList<Image, lam::LayoutSessionDomain> images(MEM_CAT_RENDER);

lam::SessionPtr<Image> image = lam::session_make<Image>(MEM_CAT_RENDER);
images.append(static_cast<lam::SessionPtr<Image>&&>(image));

lam::BorrowedPtr<Image, lam::LayoutSessionDomain> borrowed = images[0];
borrowed->width = 640;
```

When ownership must leave the list, use `remove_owned()`:

```cpp
lam::SessionPtr<Image> image = images.remove_owned(0);
```

`remove()` is the destructive form: it drops the element and frees the pointee. `remove_owned()` is the transfer form: it removes the element and returns the `OwnedPtr`.

### Bounds and capacity checks

Both lists perform runtime bounds checks in `operator[]` and `at()`. Out-of-bounds access logs an `arraylist_*_oob` prefix and aborts, making memory corruption fail early. Non-aborting probes use `try_get()`.

Mutation functions return `bool` so allocation failure and invalid ranges can be handled without exceptions:

```cpp
if (!items.append(value)) {
    log_error("layout_items_append_failed: count=%zu", items.size());
    return false;
}
```

### `PersistentList<List, StorageDomain>` — owning lists as fields

`ArrayOwnedList<T, Domain>` answers *which domain owns the pointees*. It does **not** answer *where the list itself may live*. If a pool-owned struct directly holds an `ArrayOwnedList<Image, LayoutSessionDomain>`, the §1 invariant is silently broken: the list's `OwnedPtr` slots will outlive the session that allocated their pointees.

`PersistentList` is the list-shaped analog of `PersistentField`. It composes any owning-list type with the storage domain it lives in and rejects (at compile time) combinations where the pointee domain does not outlive the storage domain. The check reuses `DomainOutlives`, so the rule is stated exactly once.

```cpp
// concept-ish: any list type exposing ::value_type (the pointee) and ::domain (the pointee domain).
template<class List, class StorageDomain>
class PersistentList {
    static_assert(
        DomainOutlives<typename List::domain, StorageDomain>::value,
        "PersistentList: list's pointee domain does not outlive its storage domain — "
        "the list would hold dangling pointers when the shorter-lived domain is reset. "
        "Either change the storage domain, or promote elements before retaining them.");
    List list_;
public:
    template<class... Args>
    explicit PersistentList(Args&&... args) : list_(static_cast<Args&&>(args)...) {}

    List&       get()       { return list_; }
    const List& get() const { return list_; }

    List*       operator->()       { return &list_; }
    const List* operator->() const { return &list_; }
};
```

`ArrayOwnedList<T, Domain>` exposes `using domain = Domain;` and `using value_type = T;` so any future owning-list type (e.g. `OwnedHashMap`) can plug into the same `PersistentList` wrapper.

Applied to a DOM element living in pool memory:

```cpp
struct DomElement {
    // ✅ pool storage, pool-owned pointees: stable across sessions.
    PersistentList<ArrayOwnedList<Image, PoolDomain>, PoolDomain> images;

    // ❌ pool storage, session-owned pointees: rejected by static_assert in PersistentList.
    // PersistentList<ArrayOwnedList<Image, LayoutSessionDomain>, PoolDomain> bad;

    // (legal as a stack local inside a layout function — not as a field of DomElement)
    // ArrayOwnedList<Image, LayoutSessionDomain> scratch;
};
```

The naked `ArrayOwnedList<T, Domain>` form is still appropriate — and necessary — for stack locals and for fields whose storage domain matches the pointee domain (a session-living struct holding session-owned children). `PersistentList` only intercedes when the list becomes a *field*.

#### Why one parameterised list, not `ArraySessionList` / `ArrayPoolList`

The two questions "who owns the pointees?" and "where can the list live?" are independent. Splitting the list into per-domain classes (`ArraySessionList<T>`, `ArrayPoolList<T>`, …) collapses both axes into the type name and forces a new class per future domain. Keeping `ArrayOwnedList<T, Domain>` parameterised — and pushing the storage-domain question into `PersistentList` — mirrors the design of `OwnedPtr` / `PersistentField` and lets generic code (promotion helpers, debug assertions, traits) be written once.

If the long form `ArrayOwnedList<T, LayoutSessionDomain>` becomes painful in practice, introduce **aliases** rather than parallel classes:

```cpp
template<class T> using SessionOwnedList = ArrayOwnedList<T, LayoutSessionDomain>;
template<class T> using PoolOwnedList    = ArrayOwnedList<T, PoolDomain>;
```

Defer even these until Phase 2 shows they're earning their keep.

### Promoting an owning list across domains

Promotion follows the §1 vocabulary. There is no list-level `set()` that crosses domains; there is a named, greppable helper:

```cpp
// Copies each pointee from the session-owned list into pool storage.
// The source list still owns its session pointees and frees them on scope exit.
template<class T>
PersistentList<ArrayOwnedList<T, PoolDomain>, PoolDomain>
promote_list_to_pool(Pool* pool,
                     ArrayOwnedList<T, LayoutSessionDomain>& src);
```

This is the only sanctioned way to take a list built during a layout pass and stabilise it inside a pool-resident DOM/View struct.

### Design rules

- Use `ArrayList<T>` for scalar values, small structs, and borrowed references.
- Prefer `ArrayList<PoolPtr<T>>` or `ArrayList<GcPtr<T>>` when a borrowed pointer domain should be visible in the type.
- Use `ArrayOwnedList<T, Domain>` when a *local* lifetime owns the pointees.
- When an owning list becomes a *field*, wrap it in `PersistentList<List, StorageDomain>` so the §1 invariant is enforced.
- Use `remove_owned()` whenever ownership is transferred out of an owning list; use `promote_list_to_pool` when the whole list crosses a domain boundary.
- `ArrayOwnedList<T, Domain>` rejects `T*`, `OwnedPtr`, and `BorrowedPtr` as `T`; the owned type is the pointee type, and the domain is the second template argument.
- Keep legacy `ArrayList*` only at C ABI boundaries or while migrating old call sites.

---

## 3. Compile-Time Tag Dispatch for `View*`, `DomNode*`, and `Item`

Tagged-union dispatch today is runtime: read tag → `switch` → `reinterpret_cast`. We can make the **casted pointer's type** depend on the tag at compile time, eliminating wrong-cast bugs and making exhaustiveness checkable.

### Pattern: typed cast helper, post-check witness, and visitor

```cpp
// lib/tagged.hpp

// 1-to-1 tag → concrete type map. Primary is undefined ⇒ unmapped tags fail to compile.
template<ViewType T> struct ViewTagToType;
template<> struct ViewTagToType<RDT_VIEW_BLOCK>      { using type = ViewBlock;     };
template<> struct ViewTagToType<RDT_VIEW_INLINE>     { using type = ViewInline;    };
template<> struct ViewTagToType<RDT_VIEW_TEXT>       { using type = ViewText;      };
template<> struct ViewTagToType<RDT_VIEW_TABLE>      { using type = ViewTable;     };
template<> struct ViewTagToType<RDT_VIEW_TABLE_CELL> { using type = ViewTableCell; };
// ... one line per tag

// Checked entry point — returns nullptr on tag mismatch.
template<ViewType T>
auto view_as(View* v) -> typename ViewTagToType<T>::type* {
    if (!v || v->type != T) return nullptr;
    return static_cast<typename ViewTagToType<T>::type*>(v);
}

// Asserting variant — for hot paths where the tag has already been established.
template<ViewType T>
auto view_require(View* v) -> typename ViewTagToType<T>::type* {
    assert(v && v->type == T);
    return static_cast<typename ViewTagToType<T>::type*>(v);
}

// Exhaustive visit; each branch sees a strongly typed pointer.
template<class F>
decltype(auto) visit_view(View* v, F&& f) {
    switch (v->type) {
        case RDT_VIEW_BLOCK:      return f(view_require<RDT_VIEW_BLOCK>(v));
        case RDT_VIEW_INLINE:     return f(view_require<RDT_VIEW_INLINE>(v));
        case RDT_VIEW_TEXT:       return f(view_require<RDT_VIEW_TEXT>(v));
        case RDT_VIEW_TABLE:      return f(view_require<RDT_VIEW_TABLE>(v));
        case RDT_VIEW_TABLE_CELL: return f(view_require<RDT_VIEW_TABLE_CELL>(v));
        // ...
    }
    return f(static_cast<View*>(nullptr));
}
```

Call site:

```cpp
visit_view(v, [](auto* p) {
    using T = std::remove_pointer_t<decltype(p)>;
    if      constexpr (std::is_same_v<T, ViewBlock>)     { /* p is ViewBlock*   */ }
    else if constexpr (std::is_same_v<T, ViewTableCell>) { /* p is ViewTableCell* */ }
    // exhaustiveness enforced by static_assert(always_false_v<T>) on the catch-all
});
```

Benefits:
- Each branch sees a strongly typed pointer; no chance of treating a `ViewTable*` as `ViewBlock*`.
- Adding a new `ViewType` without specialising `ViewTagToType` is a **compile error** at every `view_as<>` / `view_require<>` site.
- Zero runtime cost vs the current `switch` + `static_cast`.
- Dovetails with rule 11 in `AGENTS.md` (float dimensions in `radiant/`): typed casts make it harder to accidentally reach the wrong field.

### View group traits

Some operations apply to a *family* of tags (block-like, table-like, replaced). Group traits cover the real dispatch patterns the 1-to-1 map can't:

```cpp
template<ViewType T> struct IsBlockView : std::false_type {};
template<> struct IsBlockView<RDT_VIEW_BLOCK>        : std::true_type {};
template<> struct IsBlockView<RDT_VIEW_INLINE_BLOCK> : std::true_type {};
template<> struct IsBlockView<RDT_VIEW_LIST_ITEM>    : std::true_type {};
template<> struct IsBlockView<RDT_VIEW_TABLE>        : std::true_type {};
template<> struct IsBlockView<RDT_VIEW_TABLE_CELL>   : std::true_type {};

template<ViewType T>
using EnableIfBlockView = std::enable_if_t<IsBlockView<T>::value, int>;

template<ViewType T, EnableIfBlockView<T> = 0>
ViewBlock* view_as_block(View* v) {
    return v && v->type == T ? static_cast<ViewBlock*>(v) : nullptr;
}
```

### `DomNode` cast wrappers (third axis)

DOM nodes already have informal `as_element` / `as_text` helpers. Formalise as the same shape:

```cpp
template<DomNodeType T> struct DomNodeTagToType;
template<> struct DomNodeTagToType<DOM_NODE_ELEMENT> { using type = DomElement; };
template<> struct DomNodeTagToType<DOM_NODE_TEXT>    { using type = DomText;    };

template<DomNodeType T>
auto dom_as(DomNode* n) -> typename DomNodeTagToType<T>::type* {
    if (!n || n->node_type != T) return nullptr;
    return static_cast<typename DomNodeTagToType<T>::type*>(n);
}
```

### `Item` (Lambda — Phase 2)

Same pattern; defer until Radiant rollout proves the ergonomics:

```cpp
template<TypeId Tag> struct TagToType;
template<> struct TagToType<LMD_TYPE_STRING>  { using type = String;  };
template<> struct TagToType<LMD_TYPE_MAP>     { using type = Map;     };
template<> struct TagToType<LMD_TYPE_ARRAY>   { using type = Array;   };
template<> struct TagToType<LMD_TYPE_ELEMENT> { using type = Element; };

// Post-check witness: the runtime tag is verified once at the boundary,
// then the C++ type carries the proof. Unchecked code downstream is type-safe.
template<TypeId Tag>
class ItemOf {
    Item raw_;
public:
    explicit ItemOf(Item it) : raw_(it) { assert(get_type_id(it) == Tag); }
    Item raw() const { return raw_; }
    typename TagToType<Tag>::type* operator->() const {
        return reinterpret_cast<typename TagToType<Tag>::type*>(item_pointer(raw_));
    }
};

template<TypeId Tag>
std::optional<ItemOf<Tag>> item_as(Item it) {
    if (get_type_id(it) != Tag) return std::nullopt;
    return ItemOf<Tag>(it);
}
```

Functions that only accept arrays take `ItemOf<LMD_TYPE_ARRAY>` — wrong types fail to compile after the boundary.

---

## 4. Design Rules

1. **Use wrappers at ownership boundaries, not everywhere.** Internal code may continue to use raw pointers once the boundary has validated ownership and tag.
2. **Make unsafe operations noisy.** Names include `unsafe_*`, `unchecked_*`, or `raw_*`.
3. **Make lifetime promotion explicit.** A retained graph receives pool/arena/GC memory, never raw session memory.
4. **Do not wrap allocator internals.** Start above `memtrack`, `Pool`, `Arena`, GC.
5. **Preserve ABI layout.** `View`, `DomNode`, `DomElement`, `Item`, and view subclass structs remain layout-compatible; wrappers are field/parameter types, not struct replacements.
6. **Avoid template metaprogramming gymnastics.** The goal is readable safety, not a second type system hidden in templates.

---

## 5. Phasing & Risk — Radiant First

| Phase | Subsystem | Work | Risk |
|---|---|---|---|
| 1 | — | Add `lib/ownership.hpp` + `lib/tagged.hpp` + `test/test_own_tagged.cpp`. Header-only, no production changes. | None. |
| 2 | radiant | Convert `radiant/layout_inline.cpp` end-to-end: `SessionPtr<T>` for memtracked temporaries, `view_as<>`/`visit_view` for `View*` dispatch. Validate compile times & ergonomics. | Low; isolated subsystem. |
| 3 | radiant | Wrap high-risk retained DOM/View fields with `PersistentField<T, PoolDomain>`. Concrete targets: `DomElement::tag_name`, `DomElement::id`, `DomElement::class_names`, `FontProp::family`, `BackgroundProp::image`, `MarkerProp::text_content`, `ImageSurface::source_path`, `ImageSurface::source_data`. | Medium; touches many call sites but each is local. |
| 4 | radiant | Sweep `radiant/layout_*.cpp` and `radiant/resolve_css_style.cpp` to use `view_as<>` / `dom_as<>` instead of C-style casts. Add `make check-radiant-casts` grep target. | Medium. |
| 5 | radiant | Finalise template API based on lessons. Lock down `lib/ownership.hpp` + `lib/tagged.hpp` as stable. | Low. |
| 6 | lambda | Phase 1 mirror for Lambda: `TagToType<>` + `ItemOf<Tag>` + `item_as<>` over `lambda/input/*` and `lambda/lambda-eval.cpp`. | Medium. |
| 7 | lambda | `MarkBuilder` outputs return `ItemOf<Tag>` where statically known. | Medium. |
| 8 | — | Optional: clang plugin / `[[clang::annotate]]` markers (e.g. flag any raw `pool_calloc` / `memtrack_alloc` outside `lib/` wrappers). | Optional. |

Why Radiant first:
- Session ↔ pool ownership boundary is sharpest (layout pass is a clear `mem_session_*` scope).
- `View*` discriminated union is more painful day-to-day than `Item` (more subclasses, deeper inheritance-like hierarchy).
- Smaller blast radius if the templates need rework — radiant doesn't gate the Lambda baseline tests.

---

## 6. Honest Caveats — What Templates Cannot Do

- **Raw pointer escape.** Once a `T*` leaves a wrapper into a legacy C API or `void*`, provenance is lost. Audit `void*` and `unsafe_*` sites.
- **Arena outliving pointers.** Templates cannot prove a pool/arena outlives every pointer it issued. That requires whole-program lifetime analysis.
- **Use-after-free after detach.** `unsafe_release` + manual `memtrack_free` reverts to raw discipline.
- **Dynamic tag-driven access without a check.** `Item` tag is a runtime value; the boundary check is unavoidable. Templates only prevent forgetting to *record* the check's result.
- **Compile-time growth.** Header-only templates increase build time modestly; keep `TagToType` / `ViewTagToType` specialisations in single TU-friendly headers.
- **MIR JIT runtime callbacks.** Thin adapter functions wrap raw `T*` into the appropriate domain at the allocation boundary; these adapters live in `lib/` and are the only sanctioned `unsafe_*` users.

This is not Fil-C-class temporal safety. It is GSL-style local safety + Lambda/Radiant-specific ownership domains: enough to statically reject the four categories of mistake we care about (pool→session linkage, missing tag checks, wrong-type casts, ad-hoc lifetime promotion), and to make remaining unsafe operations searchable.

---

## 7. Open Questions

1. Should `ScratchArena` pointers be modelled as their own domain distinct from `PoolDomain`?
2. Should session domains stay broad (one `LayoutSessionDomain`) or split per-subsystem (`TableLayoutSessionDomain`, `GridLayoutSessionDomain`) — and if split, when?
3. Should `memtrack` expose `memtrack_owns(ptr)` to support the debug assertion path?
4. First enforcement target: cast safety (`view_as<>`), ownership crossing (`PersistentField`), or retained string fields — which yields the biggest payoff per migration cost?
5. Should `PersistentField` wrap the storage (changes struct layout) or be a setter-only helper around an existing raw field (preserves layout)? Layout preservation is preferred per rule §5 unless evidence says otherwise.

---

## 8. Phase 1 Deliverable

- `lib/ownership.hpp`
  - Tags: `GcHeapDomain`, `PoolDomain`, `LayoutSessionDomain`.
  - `BorrowedPtr<T, Domain>`, `OwnedPtr<T, Domain>` + aliases `GcPtr`, `PoolPtr`, `SessionPtr`.
  - `DomainTraits<Domain>::destroy`.
  - `DomainOutlives<A, B>` trait (single source of truth for the pool↔session rule).
  - `pool_make`, `session_make`, `session_strdup`, `checked_pool_ptr`, etc.
  - `PersistentField<T, StorageDomain>` driven by `DomainOutlives` + deleted session overloads.
  - Promotion helpers: `promote_to_pool`, `copy_to_gc`, `take_ownership`, `detach_session_buffer`.
  - Escape hatches: `unsafe_borrow_raw`, `unsafe_release`.

- `lib/tagged.hpp`
  - `ViewTagToType<>`, `view_as<Tag>`, `view_require<Tag>`, `visit_view`.
  - View group traits: `IsBlockView`, `EnableIfBlockView`, `view_as_block<Tag>`.
  - `DomNodeTagToType<>`, `dom_as<Tag>`.
  - (Lambda side, scaffolded but not yet used): `TagToType<>`, `ItemOf<Tag>`, `item_as<Tag>`.

- `lib/arraylist.hpp`
  - `ArrayList<T>` for typed value storage and borrowed references.
  - `ArrayOwnedList<T, Domain>` for owned pointees stored as `OwnedPtr<T, Domain>`; exposes `using domain = Domain;` and `using value_type = T;` for trait introspection.
  - `PersistentList<List, StorageDomain>` for owning lists held as fields; rejects mismatched domains via `DomainOutlives` `static_assert`.
  - `promote_list_to_pool(pool, session_list)` and analogous helpers.
  - Runtime bounds checks for `operator[]` / `at()` and non-aborting `try_get()`.
  - Compile-time rejection of `ArrayList<OwnedPtr<...>>`.
  - Compile-time rejection of `ArrayOwnedList<T*, Domain>`, `ArrayOwnedList<OwnedPtr<...>, Domain>`, and `ArrayOwnedList<BorrowedPtr<...>, Domain>`.

- `test/test_own_tagged.cpp` (GTest)
  - Move-only semantics of `OwnedPtr`.
  - `PersistentField::set(SessionPtr&&)` failing to compile (negative compile test via `static_assert(!std::is_invocable_v<...>)`).
  - `view_as<>` returning `nullptr` on tag mismatch; `view_require<>` asserting.
  - Adding a fake `ViewType` without `ViewTagToType` specialisation fails to compile.
  - Round-trip: `SessionPtr<char>` → `promote_to_pool` → `PersistentField::set` works; direct assignment doesn't.
  - `ArrayList` / `ArrayOwnedList` positive compile probes and negative ownership rejection probes.
  - `PersistentList<ArrayOwnedList<T, PoolDomain>, PoolDomain>` compiles; `PersistentList<ArrayOwnedList<T, LayoutSessionDomain>, PoolDomain>` fails the `DomainOutlives` `static_assert`.
  - `promote_list_to_pool` round-trip: a session-owned list becomes a pool-resident `PersistentList` field; the source list still frees its session pointees on scope exit.

No existing production call sites change in Phase 1. Ergonomics are evaluated on a sample Radiant translation unit before broader rollout.
