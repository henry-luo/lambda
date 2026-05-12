# Proposal: C++ Template-Based Memory and Tag Safety for Lambda/Radiant

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

**Goal**: Tighten Lambda/Radiant memory management and tagged-union usage with lightweight C++17 template headers, in the spirit of Fil-C ownership clarity and Microsoft GSL-style checked views, while preserving the current C/C++ runtime layout and incremental migration path.

This proposal complements the existing memory tracking and pooling work. It does not replace `memtrack`, `Pool`, `Arena`, `ScratchArena`, or the GC heap. Instead, it adds compile-time and debug-time guardrails around how pointers and tagged values are passed, stored, cast, and promoted across lifetime domains.

---

## 1. Target Memory Model

All Lambda/Radiant memory should belong to exactly one of three ownership families:

1. **GC heap**
   - Used by script runtime values and objects.
   - Managed by the garbage collector.
   - Exposed to native code as GC-domain pointers or `Item` values.

2. **Pool/Arena allocated memory**
   - Used for document, AST, DOM, view tree, style, parser, and other owner-scoped data.
   - Freed by `pool_destroy`, `arena_destroy`, `arena_reset`, `scratch_restore`, or similar owner lifecycle operations.
   - Appropriate for data retained by a larger graph, such as DOM/View nodes or type metadata.

3. **Memtracked session memory**
   - Used for temporary allocations during a bounded session of work, such as layout, render, parsing, validation, or formatting.
   - Allocated with `mem_alloc`, `mem_calloc`, `mem_realloc`, `mem_strdup`, or a typed wrapper over those APIs.
   - Must be manually freed or explicitly promoted before it can be retained by pool/arena/GC-owned structures.

**Key rule**: Session-owned memory must not be assigned or linked into pool/arena/GC-owned graphs unless ownership is explicitly moved or the data is copied/promoted into the destination lifetime domain.

---

## 2. Feasibility Summary

C++17 template headers can provide strong local safety for ownership and tag usage, but they cannot fully prove whole-program memory safety on their own.

They can catch:

- Passing session memory to APIs that require document-owned arena memory.
- Storing temporary layout/render buffers into retained DOM/View fields.
- Calling typed APIs with unchecked `Item` values.
- Casting a `View*` to a target view struct that is not valid for the requested `ViewType`.
- Forgetting to spell an ownership transfer as an explicit operation.

They cannot fully prove:

- A raw `T*` did not escape through legacy APIs.
- A pool or arena outlives every pointer allocated from it.
- A runtime `Item` has a particular tag without checking the tag.
- A manually freed raw pointer is never used afterward if it escaped wrapper ownership.

Therefore, the practical goal is **GSL-like safety plus Lambda/Radiant-specific ownership domains**, not a complete Fil-C/Rust-style temporal safety system. Full safety would require either compiler support, a custom static analyzer, or a much deeper rewrite of all raw pointer fields.

---

## 3. Proposed Header Set

Add three small C++ headers first:

| Header | Purpose |
|--------|---------|
| `lambda/lib/ownership.hpp` | Lifetime-domain pointer wrappers and allocation helpers |
| `lambda/lambda/item_ref.hpp` | Typed wrappers for checked `Item` access |
| `lambda/radiant/view_ref.hpp` | Typed wrappers for checked `View`/`DomNode` casts |

These headers should be header-only at first. They should not change ABI layout and should avoid replacing the existing C structs.

---

## 4. Ownership Domains

Define zero-sized domain tags for ownership/lifetime families:

```cpp
struct GcHeapDomain {};
struct PoolDomain {};
struct DocumentArenaDomain {};
struct AstArenaDomain {};
struct LayoutSessionDomain {};
struct RenderSessionDomain {};
struct ParserSessionDomain {};
struct MemtrackDomain {};
```

Domains describe where a pointer came from and how long it is allowed to live. They are compile-time labels only; they do not change pointer representation.

---

## 5. Pointer Wrapper Types

The core wrappers should be deliberately small:

```cpp
template <typename T, typename Domain>
class BorrowedPtr {
public:
    constexpr BorrowedPtr() : ptr_(nullptr) {}
    explicit constexpr BorrowedPtr(T* ptr) : ptr_(ptr) {}

    T* get() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    T* ptr_;
};

template <typename T, typename Domain>
class OwnedPtr {
public:
    OwnedPtr() : ptr_(nullptr) {}
    explicit OwnedPtr(T* ptr) : ptr_(ptr) {}

    OwnedPtr(const OwnedPtr&) = delete;
    OwnedPtr& operator=(const OwnedPtr&) = delete;

    OwnedPtr(OwnedPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    OwnedPtr& operator=(OwnedPtr&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    ~OwnedPtr() { reset(); }

    T* get() const { return ptr_; }
    T* release() {
        T* ptr = ptr_;
        ptr_ = nullptr;
        return ptr;
    }

    void reset(T* ptr = nullptr) {
        if (ptr_) {
            OwnershipTraits<Domain>::free(ptr_);
        }
        ptr_ = ptr;
    }

private:
    T* ptr_;
};
```

For arena and pool memory, ownership usually belongs to the arena/pool, not an individual pointer. Therefore those wrappers should usually be non-owning borrowed references:

```cpp
template <typename T, typename Domain>
using ArenaPtr = BorrowedPtr<T, Domain>;

template <typename T>
using GcPtr = BorrowedPtr<T, GcHeapDomain>;

template <typename T, typename SessionDomain>
using SessionPtr = OwnedPtr<T, SessionDomain>;
```

Session memory is the best initial target for `OwnedPtr`, because it maps naturally to manual `mem_free` cleanup.

---

## 6. Allocation Helpers

Wrap existing allocators with domain-specific helpers:

```cpp
template <typename T, typename Domain>
ArenaPtr<T, Domain> arena_make(Arena* arena) {
    return ArenaPtr<T, Domain>((T*)arena_calloc(arena, sizeof(T)));
}

template <typename T, typename Domain>
SessionPtr<T, Domain> mem_make(size_t count, MemCategory category) {
    return SessionPtr<T, Domain>((T*)mem_calloc(count, sizeof(T), category));
}

template <typename Domain>
SessionPtr<char, Domain> mem_strdup_owned(const char* text, MemCategory category) {
    return SessionPtr<char, Domain>(mem_strdup(text, category));
}

template <typename T>
GcPtr<T> gc_make(Heap* heap, size_t size, TypeId type_id) {
    return GcPtr<T>((T*)heap_alloc(size, type_id));
}
```

The wrappers should remain thin and obvious. They should not hide allocation categories or ownership costs.

---

## 7. Preventing Temporary-To-Persistent Assignment

The main compile-time value comes from making retained fields and APIs reject session memory.

Example retained-field wrapper:

```cpp
template <typename T, typename Domain>
class PersistentField {
public:
    void set(ArenaPtr<T, Domain> ptr) { ptr_ = ptr.get(); }
    void set(GcPtr<T> ptr) = delete;

    template <typename SessionDomain>
    void set(SessionPtr<T, SessionDomain>&&) = delete;

    T* get() const { return ptr_; }

private:
    T* ptr_ = nullptr;
};
```

Example usage:

```cpp
struct SafeDomElementFields {
    PersistentField<char, DocumentArenaDomain> tag_name;
    PersistentField<const char, DocumentArenaDomain> id;
};
```

This intentionally fails:

```cpp
auto temp_name = mem_strdup_owned<LayoutSessionDomain>("div", MEM_CAT_LAYOUT);
fields.tag_name.set(std::move(temp_name));  // compile error
```

The correct operation is explicit promotion:

```cpp
auto temp_name = mem_strdup_owned<LayoutSessionDomain>("div", MEM_CAT_LAYOUT);
auto stable_name = promote_to_arena<DocumentArenaDomain>(doc->arena, temp_name.get());
fields.tag_name.set(stable_name);
```

This makes lifetime crossing visible in code review.

---

## 8. Ownership Promotion

Promotion should copy data into the target lifetime domain and leave the source owner responsible for freeing its original allocation.

```cpp
template <typename Domain>
ArenaPtr<char, Domain> promote_to_arena(Arena* arena, const char* text) {
    return ArenaPtr<char, Domain>(arena_strdup(arena, text));
}

template <typename Domain, typename T>
ArenaPtr<T, Domain> promote_to_arena(Arena* arena, const T* value) {
    T* copy = (T*)arena_alloc(arena, sizeof(T));
    if (copy && value) {
        *copy = *value;
    }
    return ArenaPtr<T, Domain>(copy);
}
```

For ownership-moving APIs, use names that make the transfer explicit:

```cpp
take_ownership(...);
promote_to_arena(...);
copy_to_gc(...);
detach_session_buffer(...);
```

Avoid ambiguous names like `set`, `assign`, or `attach` for lifetime-crossing operations.

---

## 9. Runtime Debug Checks

Compile-time wrappers should be paired with debug checks where possible:

```cpp
template <typename T, typename Domain>
ArenaPtr<T, Domain> checked_arena_ptr(Arena* arena, T* ptr) {
#ifndef NDEBUG
    if (ptr && !arena_owns(arena, ptr)) {
        log_error("checked_arena_ptr: pointer is not owned by arena");
        assert(false);
    }
#endif
    return ArenaPtr<T, Domain>(ptr);
}
```

Useful debug checks:

- `arena_owns(arena, ptr)` before wrapping retained arena pointers.
- `memtrack_owns(ptr)` if a query API is added to memtrack.
- GC heap ownership check if the heap supports pointer-range validation.
- Tag validation before creating typed `Item` or `View` wrappers.

---

## 10. Typed `Item` Wrappers

Lambda `Item` is a compact runtime-tagged value. Its tag is stored in the value, so C++ cannot know the tag at compile time for arbitrary runtime results.

However, once a runtime check succeeds, the checked result can be represented by a C++ type:

```cpp
template <TypeId Id>
class ItemOf {
public:
    explicit ItemOf(Item item) : item_(item) {
        assert(item_.type_id() == Id);
    }

    Item raw() const { return item_; }
    TypeId type_id() const { return Id; }

private:
    Item item_;
};

using StringItem = ItemOf<LMD_TYPE_STRING>;
using MapItem = ItemOf<LMD_TYPE_MAP>;
using ElementItem = ItemOf<LMD_TYPE_ELEMENT>;
using ArrayItem = ItemOf<LMD_TYPE_ARRAY>;
```

Checked conversion:

```cpp
template <TypeId Id>
Optional<ItemOf<Id>> item_as(Item item) {
    if (item.type_id() != Id) {
        return Optional<ItemOf<Id>>();
    }
    return Optional<ItemOf<Id>>(ItemOf<Id>(item));
}
```

Typed accessors can then be specialized:

```cpp
template <>
inline String* item_ptr<LMD_TYPE_STRING>(ItemOf<LMD_TYPE_STRING> item) {
    return item.raw().get_string();
}

template <>
inline Map* item_ptr<LMD_TYPE_MAP>(ItemOf<LMD_TYPE_MAP> item) {
    return item.raw().map;
}
```

This changes downstream code from:

```cpp
String* str = (String*)item.string_ptr;
```

to:

```cpp
StringItem str_item = require_item<LMD_TYPE_STRING>(item);
String* str = item_ptr(str_item);
```

The runtime tag is still checked at the boundary, but after that, the C++ type records the tag and prevents accidental cross-tag use.

---

## 11. Typed `View` and DOM Cast Wrappers

Radiant `View` nodes currently use `DomNode::view_type` as the discriminator and C++ inheritance-like structs for layout-specific views.

Add a compile-time map from `ViewType` to concrete C++ type:

```cpp
template <ViewType Tag>
struct ViewTypeMap;

template <>
struct ViewTypeMap<RDT_VIEW_TEXT> {
    using type = ViewText;
};

template <>
struct ViewTypeMap<RDT_VIEW_TABLE> {
    using type = ViewTable;
};

template <>
struct ViewTypeMap<RDT_VIEW_TABLE_CELL> {
    using type = ViewTableCell;
};
```

Then expose checked casts:

```cpp
template <ViewType Tag>
typename ViewTypeMap<Tag>::type* view_as(View* view) {
    if (!view || view->view_type != Tag) {
        return nullptr;
    }
    return static_cast<typename ViewTypeMap<Tag>::type*>(view);
}

template <ViewType Tag>
typename ViewTypeMap<Tag>::type* view_require(View* view) {
    assert(view && view->view_type == Tag);
    return static_cast<typename ViewTypeMap<Tag>::type*>(view);
}
```

If a tag has no `ViewTypeMap` specialization, `view_as<Tag>` will fail to compile. This prevents unsupported casts from being silently introduced.

---

## 12. View Group Traits

Some view operations are valid for groups of tags rather than a single tag. Encode those groups with traits:

```cpp
template <ViewType Tag>
struct IsBlockView : std::false_type {};

template <> struct IsBlockView<RDT_VIEW_BLOCK> : std::true_type {};
template <> struct IsBlockView<RDT_VIEW_INLINE_BLOCK> : std::true_type {};
template <> struct IsBlockView<RDT_VIEW_LIST_ITEM> : std::true_type {};
template <> struct IsBlockView<RDT_VIEW_TABLE> : std::true_type {};
template <> struct IsBlockView<RDT_VIEW_TABLE_CELL> : std::true_type {};

template <ViewType Tag>
using EnableIfBlockView = typename std::enable_if<IsBlockView<Tag>::value, int>::type;
```

Then constrain APIs:

```cpp
template <ViewType Tag, EnableIfBlockView<Tag> = 0>
ViewBlock* view_as_block(View* view) {
    if (!view || view->view_type != Tag) {
        return nullptr;
    }
    return static_cast<ViewBlock*>(view);
}
```

This improves correctness while still matching the existing layout model.

---

## 13. DOM Node Cast Wrappers

`DomNode` already has `as_element`, `as_text`, and `as_comment` helpers. The proposal is to formalize and use them consistently, then discourage direct C-style casts.

Potential wrapper:

```cpp
template <DomNodeType Tag>
struct DomNodeTypeMap;

template <>
struct DomNodeTypeMap<DOM_NODE_ELEMENT> {
    using type = DomElement;
};

template <>
struct DomNodeTypeMap<DOM_NODE_TEXT> {
    using type = DomText;
};

template <DomNodeType Tag>
typename DomNodeTypeMap<Tag>::type* dom_as(DomNode* node) {
    if (!node || node->node_type != Tag) {
        return nullptr;
    }
    return static_cast<typename DomNodeTypeMap<Tag>::type*>(node);
}
```

This gives DOM casts the same shape as `Item` and `View` casts.

---

## 14. Enforcement Strategy

Headers alone are not enough if legacy code can freely use raw pointer fields and C-style casts. Pair the wrappers with build checks.

Suggested checks:

1. **Ban new C-style downcasts in selected directories**
   - Allow only in documented low-level compatibility files.
   - Prefer `item_as`, `view_as`, `dom_as`, or explicit `unsafe_*` helpers.

2. **Ban direct assignment from session wrappers to persistent fields**
   - Enforced naturally where fields are wrapped.
   - Enforced by grep/clang-tidy where fields remain raw.

3. **Require explicit promotion functions**
   - `promote_to_arena`, `copy_to_gc`, `take_ownership`, or `detach_session_buffer`.

4. **Keep `RAWALLOC_OK`-style escape hatches**
   - Unsafe operations should be searchable and justified.
   - Prefer names like `unsafe_view_cast`, `unsafe_item_cast`, `unsafe_borrow_raw`.

5. **Add clang-tidy checks later**
   - Detect assignments from temporary/session buffers to retained struct fields.
   - Detect direct reads of union pointer fields without prior tag check.
   - Detect C-style casts from `View*`, `DomNode*`, and `Item`-derived pointers.

---

## 15. Suggested Migration Plan

### Phase 1: Add Headers and Unit Tests

- Add `ownership.hpp`, `item_ref.hpp`, and `view_ref.hpp`.
- Add compile-only tests that verify invalid conversions fail where practical.
- Add runtime tests for successful/failed `item_as`, `view_as`, and `dom_as` checks.

### Phase 2: Use Wrappers in New Code

- New layout/render code should use typed session owners for memtracked temporaries.
- New `Item` APIs should accept `ItemOf<Tag>` after the boundary check.
- New Radiant view casts should use `view_as<Tag>` or `view_require<Tag>`.

### Phase 3: Migrate High-Risk Retained Fields

Start with fields that frequently store string or buffer pointers:

- `DomElement::tag_name`
- `DomElement::id`
- `DomElement::class_names`
- `FontProp::family`
- `BackgroundProp::image`
- `MarkerProp::text_content`
- `ImageSurface::source_path`
- `ImageSurface::source_data`

Do not wrap every field immediately. Focus on fields where temporary data can accidentally become retained.

### Phase 4: Migrate Hot Cast Sites

Replace common direct casts with typed helpers:

- `Item` to `String`, `Map`, `Array`, `Element`, `Function`.
- `View*` to `ViewBlock`, `ViewTable`, `ViewTableCell`, `ViewText`.
- `DomNode*` to `DomElement` and `DomText`.

### Phase 5: Add Build Enforcement

- Add a `make check-safety-casts` target.
- Add a `make check-ownership-crossing` target if grep patterns are reliable enough.
- Later, move the high-value checks into clang-tidy.

---

## 16. API Design Rules

1. **Use wrappers at ownership boundaries, not everywhere.**
   Internal code may still use raw pointers once the boundary has validated ownership and tag.

2. **Make unsafe operations noisy.**
   Names should include `unsafe`, `unchecked`, or `raw`.

3. **Make lifetime promotion explicit.**
   A retained graph should receive arena/pool/GC memory, not session memory.

4. **Do not wrap low-level allocator internals first.**
   Start above `memtrack`, `Pool`, `Arena`, and GC internals.

5. **Preserve ABI layout.**
   Existing `Item`, `DomNode`, `DomElement`, `ViewTree`, and view structs should remain layout-compatible.

6. **Avoid complex template metaprogramming.**
   The goal is readable safety, not a second type system hidden in templates.

---

## 17. Relationship to GSL and Fil-C

This proposal borrows ideas from both but does not duplicate either.

From GSL:

- Explicit owner and view concepts.
- Narrower pointer types.
- Checked boundary APIs.
- Lightweight wrappers that compile away.

From Fil-C:

- Make pointer provenance and ownership more visible.
- Treat casts and ownership transfer as operations that deserve scrutiny.
- Prefer safe defaults and noisy escape hatches.

What is not provided by headers alone:

- Whole-program pointer provenance tracking.
- Temporal safety after raw pointer escape.
- Automatic prevention of all use-after-free bugs.
- Complete protection from legacy C APIs.

---

## 18. Example End State

Example layout code:

```cpp
auto temp_text = mem_strdup_owned<LayoutSessionDomain>(raw_text, MEM_CAT_LAYOUT);

// Cannot store temp_text directly into a retained DOM/View field.
auto stable_text = promote_to_arena<DocumentArenaDomain>(doc->arena, temp_text.get());
view_text_fields.text.set(stable_text);
```

Example `Item` code:

```cpp
auto maybe_string = item_as<LMD_TYPE_STRING>(item);
if (!maybe_string) {
    return make_type_error("expected string");
}

String* str = item_ptr(*maybe_string);
```

Example `View` code:

```cpp
ViewTable* table = view_as<RDT_VIEW_TABLE>(view);
if (!table) {
    return;
}

layout_table(table);
```

---

## 19. Open Questions

1. Should retained DOM/View string fields become wrapper fields, or should only setter functions be wrapped?
2. Should `memtrack` expose `memtrack_owns(ptr)` for debug validation?
3. Should session domains be broad (`LayoutSessionDomain`) or more specific (`TableLayoutSessionDomain`, `GridLayoutSessionDomain`)?
4. Should the first enforcement target be cast safety, ownership crossing, or retained string fields?
5. Should `ScratchArena` pointers be modeled as their own domain distinct from `ArenaPtr`?

---

## 20. Recommendation

This is worth doing incrementally.

The best first implementation is small:

- Add typed `Item` wrappers.
- Add typed `View` cast helpers.
- Add session-owned `memtrack` RAII wrappers.
- Use promotion helpers when temporary memory crosses into document/view ownership.
- Add grep or clang-tidy enforcement after a few representative migrations prove the shape.

This provides meaningful compile-time help without destabilizing the existing memory model. It also creates a path from today's disciplined C/C++ conventions toward a much more explicit ownership system.