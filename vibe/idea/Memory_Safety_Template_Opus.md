# Compile-Time Memory Ownership & Tagged Union Safety for Lambda/Radiant

## Prompt
If I want to further tighten memory management under Lambda/Radiant code base,
I want memroy to be managed only in 3 ways:
1. GC heap for the script runtime;
2. pool/arena allocated;
3. memtracked memory, temporal allocated for a session of work, e.g. layout session, which needs to be manually freed.
and should not be assigned/linked to the pooled memory, unless ownership is moved.
Is it possible to develop some C++ template headers like Fil-C or Guidelines Support Library 
https://github.com/microsoft/GSL,
to track memory ownership and safety, at comptime?

Is it possible to enhance tagged union usage under Lambda/Radiant, for Items and Views, to ensure they are casted based on the tag at comptime?

## Motivation

Today Lambda/Radiant code mixes several allocation strategies (GC heap, pool/arena, memtracked session memory, plain `malloc`, occasional `std::`-adjacent containers). We want to tighten the contract so that **only three** memory domains exist, and the rules governing them are enforced as much as possible at compile time — without paying the runtime cost of Fil-C-style capability pointers.

Target rules:

1. **GC heap** — script runtime values, traced.
2. **Pool/arena** — bulk-freed lifetime, no individual destruction.
3. **Memtracked session memory** — temporally scoped to a session of work (e.g. a layout pass); must be manually freed.

Additional invariant: **pool-allocated objects must not hold pointers into session memory** (or any shorter-lived domain) unless ownership has been formally transferred. This prevents dangling-pointer classes of bug when a session ends but pool objects persist.

This document proposes two header-only facilities under `lib/`, modelled after Microsoft GSL (`owner<T>`, `not_null<T>`) but stricter, with a domain-tagged ownership model and compile-time tagged-union dispatch.

---

## 1. Three-Domain Ownership Model via Templates

### Core idea: ownership tag as a type parameter

```cpp
// lib/own.hpp
namespace lam {

// allocation domain tags (empty types; pure compile-time markers)
struct GcDomain      {};   // GC heap, traced
struct PoolDomain    {};   // pool/arena, freed in bulk
struct SessionDomain {};   // memtracked, must be manually freed

// non-owning view, any domain
template<class T>
struct Ref { T* p; T* get() const { return p; } T* operator->() const { return p; } };

// owning pointer, parameterised by domain
template<class T, class Domain>
class Own {
    T* p_;
public:
    explicit Own(T* p) : p_(p) {}
    Own(Own&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    Own& operator=(Own&& o) noexcept { reset(o.release()); return *this; }
    Own(const Own&)            = delete;   // no copy ⇒ no aliasing of ownership
    Own& operator=(const Own&) = delete;

    ~Own() { Domain::destroy(p_); }        // domain decides what destroy means

    T* get() const     { return p_; }
    T* operator->() const { return p_; }
    T* release()       { T* t = p_; p_ = nullptr; return t; }
    void reset(T* n = nullptr) { Domain::destroy(p_); p_ = n; }

    // implicit decay to non-owning ref
    operator Ref<T>() const { return {p_}; }
};

// domain policies
struct GcDomain      { template<class T> static void destroy(T*) { /* nop: GC traces */ } };
struct PoolDomain    { template<class T> static void destroy(T*) { /* nop: arena resets */ } };
struct SessionDomain {
    template<class T> static void destroy(T* p) { if (p) { p->~T(); memtrack_free(p); } }
};

template<class T> using GcPtr      = Own<T, GcDomain>;
template<class T> using PoolPtr    = Own<T, PoolDomain>;
template<class T> using SessionPtr = Own<T, SessionDomain>;

} // namespace lam
```

### Enforcing the "no pool → session linkage" rule

This is what GSL doesn't give you, and where the type system earns its keep. Add a **storable-into** trait:

```cpp
// A field of a PoolDomain object may store: pointers to GC/Pool (not Session).
// A field of a SessionDomain object may store: pointers to any domain.
template<class Container, class Pointee> struct can_store : std::true_type {};

template<class T> struct can_store<PoolDomain, Own<T, SessionDomain>> : std::false_type {};
template<class T> struct can_store<GcDomain,   Own<T, SessionDomain>> : std::false_type {};
template<class T> struct can_store<GcDomain,   Own<T, PoolDomain>>    : std::false_type {};

// Field wrapper used inside structs to opt-in to the check.
template<class Owner, class Field>
struct Field_ {
    static_assert(can_store<Owner, Field>::value,
        "memory rule: cannot store this pointer inside this domain without ownership transfer");
    Field f;
};
```

Now writing:

```cpp
struct MyPoolNode {
    Field_<PoolDomain, SessionPtr<Linebox>> bad;   // ❌ static_assert fires
    Field_<PoolDomain, PoolPtr<MyPoolNode>>  ok;   // ✅
};
```

Ownership transfer is explicit via `release()` + a typed constructor, so violations surface at the call site rather than after a session crash.

### Practical adoption path

1. Land `lib/own.hpp` alongside existing allocators (no behaviour change).
2. New code uses `PoolPtr<T>` / `SessionPtr<T>` instead of raw `T*` returned by `pool_calloc` / `memtrack_alloc`.
3. Convert hot ownership boundaries first: `BlockContext`, `Linebox`, `View*` lifetime in radiant; `MarkBuilder` outputs in Lambda input parsers.
4. Raw `T*` remains valid for non-owning views (or migrate to `Ref<T>` for clarity).

This is essentially **Fil-C's capability model done statically** — no fat pointers, no runtime cost, but we trade dynamic safety for compile-time rejection of the rules we listed.

---

## 2. Compile-Time Tag Dispatch for `Item` and `View*`

Tagged-union dispatch today is runtime: `get_type_id(item)` → `switch` → `reinterpret_cast`. We can make the **casted pointer's type** depend on the tag at compile time, eliminating wrong-cast bugs and making exhaustiveness checkable.

### Pattern: typed cast helper + visitor

```cpp
// lib/tagged.hpp
template<TypeId Tag> struct TagToType;          // primary, undefined
template<> struct TagToType<LMD_TYPE_STRING>  { using type = String;  };
template<> struct TagToType<LMD_TYPE_ARRAY>   { using type = Array;   };
template<> struct TagToType<LMD_TYPE_MAP>     { using type = Map;     };
template<> struct TagToType<LMD_TYPE_ELEMENT> { using type = Element; };
// ... one line per tag

template<TypeId Tag>
auto as(Item it) -> typename TagToType<Tag>::type* {
    // debug-only: assert(get_type_id(it) == Tag);
    return reinterpret_cast<typename TagToType<Tag>::type*>(item_pointer(it));
}

// Exhaustive visit; compiler verifies every case returns the same type.
template<class F>
decltype(auto) visit_item(Item it, F&& f) {
    switch (get_type_id(it)) {
        case LMD_TYPE_STRING:  return f(as<LMD_TYPE_STRING>(it));
        case LMD_TYPE_ARRAY:   return f(as<LMD_TYPE_ARRAY>(it));
        case LMD_TYPE_MAP:     return f(as<LMD_TYPE_MAP>(it));
        case LMD_TYPE_ELEMENT: return f(as<LMD_TYPE_ELEMENT>(it));
        // ...
        default: return f(static_cast<void*>(nullptr));
    }
}
```

Call site:

```cpp
visit_item(it, [](auto* p) {
    using T = std::remove_pointer_t<decltype(p)>;
    if constexpr (std::is_same_v<T, Array>)    { /* p is Array*, fully typed */ }
    else if constexpr (std::is_same_v<T, Map>) { /* ... */ }
});
```

Benefits:

- Each branch sees a strongly typed pointer; no chance of treating a `Map*` as `Array*`.
- Adding a new `TypeId` without specialising `TagToType` is a **compile error** at the `as<>` site.
- Zero runtime cost vs current `switch` + `reinterpret_cast`.

### Stronger: phantom-tagged `Item` wrapper

For places where the tag is known statically (e.g., after an `is_array` check, or inside a typed builder), wrap:

```cpp
template<TypeId Tag>
struct TypedItem {
    Item raw;
    typename TagToType<Tag>::type* operator->() const { return as<Tag>(raw); }
};

TypedItem<LMD_TYPE_ARRAY> make_array(...);   // builder return types carry tags
```

Functions that only accept arrays take `TypedItem<LMD_TYPE_ARRAY>` — wrong types fail to compile.

### Same pattern for Radiant `View*`

```cpp
template<ViewType T> struct ViewTagToType;
template<> struct ViewTagToType<RDT_VIEW_BLOCK>  { using type = ViewBlock;  };
template<> struct ViewTagToType<RDT_VIEW_INLINE> { using type = ViewInline; };
template<> struct ViewTagToType<RDT_VIEW_TEXT>   { using type = ViewText;   };
// ...

template<ViewType T>
auto view_as(View* v) -> typename ViewTagToType<T>::type* {
    return static_cast<typename ViewTagToType<T>::type*>(v);
}

template<class F> decltype(auto) visit_view(View* v, F&& f) { /* switch on v->type */ }
```

This dovetails with rule 11 in `AGENTS.md` (float dimensions in `radiant/`): the typed casts make it harder to accidentally reach the wrong field.

---

## Phasing & Risk

| Phase | Work | Risk |
|---|---|---|
| 1 | Add `lib/own.hpp` + `lib/tagged.hpp`. No changes elsewhere. | None — header-only, opt-in. |
| 2 | Convert one subsystem end-to-end (suggest `radiant/layout_inline.cpp` — moderate size, clear session ownership) to `SessionPtr<T>` + `visit_view`. Validate compile times & ergonomics. | Low; isolated. |
| 3 | Enable the `Field_<Owner, Field>` cross-domain rule for new code paths only. | Low. |
| 4 | Sweep `lambda/input/*` to return tagged builder outputs (`TypedItem<>`). Catches a real class of bug. | Medium; lots of files. |
| 5 | Optional: clang plugin / `[[clang::annotate]]` markers (e.g., flag any raw `pool_calloc` outside `lib/`). | Optional. |

### Honest caveats

- Header-only templates increase compile time modestly; keep `TagToType` specialisations in one TU-friendly header.
- `if constexpr` exhaustiveness needs discipline — provide a `static_assert(always_false_v<T>)` default branch to force coverage.
- The cross-domain rule cannot catch *type-erased* stores (e.g., storing into `void*` then back). Keep `void*` use rare and audited.
- This is **not** memory safety in the Fil-C sense (no temporal safety at runtime). It only prevents the *categories of mistakes* enumerated above, statically.
- Interop with existing C APIs (`pool_calloc`, `memtrack_alloc`, MIR JIT runtime callbacks) requires thin adapter functions that wrap the raw `T*` into the appropriate `Own<T, Domain>` at the allocation boundary.

---

## Next Step

If approved, Phase 1 deliverable is:

- `lib/own.hpp` — `Own<T, Domain>`, `Ref<T>`, domain policies, `can_store` trait, `Field_<Owner, Field>` wrapper.
- `lib/tagged.hpp` — `TagToType<>`, `as<Tag>()`, `visit_item`, `visit_view`, `TypedItem<Tag>`.
- `test/test_own_tagged.cpp` — GTest unit suite exercising:
  - move-only semantics of `Own`,
  - `static_assert` rejection of pool→session storage,
  - exhaustiveness compile error when a new `TypeId` is added without a `TagToType` specialisation,
  - typed dispatch correctness on representative `Item` / `View*` inputs.

No production code changes in Phase 1; ergonomics can be evaluated before broader rollout.

---

## Appendix: Comparison with the GPT Variant

A parallel proposal (`Memory_Safety_Template_GPT.md`) explores the same problem with a broader and more pragmatic framing. The following refinements from that variant should be folded into this design.

### High-value additions to adopt

1. **Borrowed vs Owned split (`BorrowedPtr` / `OwnedPtr`).**
   This document conflates ownership with the domain. The correct distinction is: pool/arena pointers are almost always *borrowed* (the arena owns the storage), only session memory is *owned* per-pointer. Putting RAII semantics on pool pointers is unnecessary and misleading.
   → Refine: `ArenaPtr<T>` = borrowed, `SessionPtr<T>` = owned, `GcPtr<T>` = borrowed. Keep `Own<T, Domain>` only for the session case.

2. **Finer-grained session domains.**
   `LayoutSessionDomain`, `RenderSessionDomain`, `ParserSessionDomain` instead of a single `SessionDomain`. Prevents cross-session leakage (e.g., a parser-session pointer accidentally retained by render code). Start with three (Gc/Pool/Session) on day one; split only when a concrete boundary forces it.

3. **Explicit promotion vocabulary.**
   Forbid generic `set()` / `assign()` for lifetime-crossing operations. Use dedicated names: `promote_to_arena()`, `copy_to_gc()`, `take_ownership()`, `detach_session_buffer()`. This is the single most useful code-review signal in the entire proposal.

4. **Runtime debug ownership checks.**
   Wrap a `#ifndef NDEBUG` assertion inside the boundary constructors using `arena_owns(arena, ptr)` and an optional `memtrack_owns(ptr)` query. Zero cost in release builds; catches stray pointers in debug.

5. **`PersistentField<T, Domain>` with deleted overloads.**
   Cleaner than the `Field_<Owner, Field>` + `can_store` trait — overload resolution with `= delete` produces better per-call-site diagnostics than `static_assert`. Replace `Field_` with this pattern:

   ```cpp
   template <typename T, typename Domain>
   class PersistentField {
   public:
       void set(ArenaPtr<T, Domain> ptr) { ptr_ = ptr.get(); }
       template <typename SessionDomain>
       void set(SessionPtr<T, SessionDomain>&&) = delete;
   private:
       T* ptr_ = nullptr;
   };
   ```

6. **`unsafe_*` escape-hatch naming + grep enforcement.**
   Following the existing `RAWALLOC_OK` / `INT_CAST_OK` conventions in `AGENTS.md`. Names: `unsafe_view_cast`, `unsafe_item_cast`, `unsafe_borrow_raw`. Add `make check-unsafe-casts` mirroring `make check-int-cast`.

7. **`ItemOf<Tag>` as a post-check witness type.**
   Same shape as `TypedItem<Tag>` here, but framed correctly: the runtime tag is verified once at the boundary, then the C++ type *carries the proof*. The entry point returns `Optional<ItemOf<Tag>>` rather than panic-casting; keep the unchecked `as<Tag>()` for hot paths where the tag was already established.

8. **View group traits (`IsBlockView` / `EnableIfBlockView`).**
   Some view operations apply to a *family* of tags (block-like, table-like, replaced). The 1-to-1 `TagToType` map alone is insufficient; group traits cover real radiant dispatch patterns.

9. **`DomNode` cast wrappers as a third axis.**
   This proposal covered `Item` and `View*` but missed DOM nodes, which already have informal `as_element` / `as_text` helpers. Formalise as `dom_as<DOM_NODE_ELEMENT>(node)` so the three discriminated unions follow the same pattern.

10. **Concrete first-migration field list.**
    Replace "convert one subsystem" with named high-risk fields where temporary memory most often leaks into retained state:
    - `DomElement::tag_name`, `DomElement::id`, `DomElement::class_names`
    - `FontProp::family`
    - `BackgroundProp::image`
    - `MarkerProp::text_content`
    - `ImageSurface::source_path`, `ImageSurface::source_data`

### Worth borrowing but trim

- **"Use wrappers at boundaries, not everywhere"** — promote to a top-level design rule. Internal code may still use raw pointers once the boundary has validated ownership and tag.
- **Honest "what headers cannot do" section** — merge GPT's more rigorous list of limitations (raw pointer escape, arena outliving pointers, post-free use after escape, legacy C API gaps) into the caveats above.
- **Open Questions section** — useful discipline. Carry forward a short version covering: should `ScratchArena` be its own domain? Should session domains be broad or per-subsystem? Where to start enforcement first?

### Skip / disagree

- **Eight separate domain tags up front** (`DocumentArenaDomain`, `AstArenaDomain`, plus three session variants on day one). Too many. The user's original ask is three domains; split only when a real boundary appears.
- **Wrapping low-level allocator internals.** Correct to *not* do this; both proposals agree. The two-header layout here (`own.hpp` + `tagged.hpp`) is slightly tidier than GPT's three-header split, but either works.
- **Custom clang-tidy checks as a planned phase.** High value, high cost. Keep as an explicit optional Phase 5, not a deliverable.

### Net effect on the plan

Phase 1 deliverables update to:

- `lib/ownership.hpp` — `BorrowedPtr<T, Domain>`, `OwnedPtr<T, Domain>`, aliases (`ArenaPtr`, `GcPtr`, `SessionPtr`), domain tags (start with `GcHeapDomain`, `PoolDomain`, `LayoutSessionDomain` only), `PersistentField<T, Domain>` with deleted session overloads, explicit promotion helpers (`promote_to_arena`, `copy_to_gc`, `take_ownership`), debug `arena_owns` / `memtrack_owns` assertions.
- `lib/tagged.hpp` — `TagToType<>`, `as<Tag>()`, `visit_item`, `visit_view`, `ItemOf<Tag>` + `item_as<Tag>` returning `Optional`, view group traits (`IsBlockView`), `dom_as<>` for DOM nodes, `unsafe_*` escape-hatch helpers.
- `test/test_own_tagged.cpp` — as before, plus tests for `PersistentField` deleted overloads and `item_as` failure paths.

Phase 3 becomes concrete: migrate the named field list above first.
