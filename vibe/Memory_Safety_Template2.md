# Templating Lambda's Runtime Data Model for Compile-Time Safety

> Companion to [`Memory_Safety_Template.md`](./Memory_Safety_Template.md).
> That doc covers Radiant (`View*`, `DomNode*`) and the three-domain ownership
> model. This doc extends the same pattern to Lambda's `Item` / `Container` /
> `Map` / `Array` / `Element` runtime data model, with a dual C++/C interface
> so MIR JIT continues to see the unchanged C ABI.
>
> Note: Lambda's runtime no longer has a separate `List` type — `Array` is
> the sole array-of-`Item` container, and `Element` extends `Array`.

## Yes — and the existing scaffolding already covers half of it

`lib/tagged.hpp` / `lib/ownership.hpp` and the `item_tagged.hpp` it includes
are already the right shape for Radiant. The Lambda runtime needs the same
pattern but with three extra constraints:

1. The discriminant lives in the high 8 bits of `Item` (a `uint64_t`), not in
   a struct field.
2. Container subtypes (`Map`, `Element`, `Object`, `VMap`) form a **subtype
   lattice**, not a flat 1-to-1 map — `Element` *is-a* `Map` **and** *is-a*
   `Array` (it carries both shape entries and an array of `Item`).
3. MIR JIT must keep calling the existing `extern "C"` ABI (raw `Item`, raw
   `Map*`, raw `Array*`) — we cannot change pointer sizes or add hidden
   destructors at C boundaries.

So the design is: **C++ wrapper layer over the unchanged C structs, zero ABI
impact, all C++ types are the same size as the underlying pointer/`Item`.**

---

## 1. The two layers (dual interface)

```
                    Lambda runtime (.cpp)
                          │
                          │  uses
                          ▼
            ┌─────────────────────────────┐
            │  C++ template layer         │  lib/lambda_typed.hpp
            │  ItemOf<Tag>, Ref<T>,       │  (new — header-only, zero-overhead)
            │  match(), as<>, require<>   │
            └────────────┬────────────────┘
                         │ static_cast / bit ops only
                         ▼
            ┌─────────────────────────────┐
            │  C ABI (unchanged)          │  lambda/lambda.h
            │  Item, Map*, List*, etc.    │  (MIR JIT continues to use)
            └─────────────────────────────┘
```

Rule: every C++ wrapper is **trivially convertible** to and from the
underlying C type (`reinterpret_cast` round-trips), so MIR-emitted code calling
a C++ helper compiled to the same signature is bit-identical.

---

## 2. Tag → type map for `Item` (mirroring `ViewTagToType`)

```cpp
// lib/lambda_typed.hpp
namespace lam {

template<TypeId Tag> struct ItemTagToType;       // primary undefined → unmapped tags fail to compile

// scalar (boxed) types — pointer in low 56 bits
template<> struct ItemTagToType<LMD_TYPE_INT64>   { using type = int64_t;     static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_FLOAT>   { using type = double;      static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_STRING>  { using type = String;      static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_SYMBOL>  { using type = Symbol;      static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_BINARY>  { using type = Binary;      static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_DECIMAL> { using type = Decimal;     static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_DTIME>   { using type = DateTime;    static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_UINT64>  { using type = uint64_t;    static constexpr bool is_pointer = true; };

// container types (direct pointer in Item)
template<> struct ItemTagToType<LMD_TYPE_RANGE>     { using type = Range;     static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_ARRAY_NUM> { using type = ArrayNum;  static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_ARRAY>     { using type = Array;     static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_MAP>       { using type = Map;       static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_VMAP>      { using type = VMap;      static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_ELEMENT>   { using type = Element;   static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_OBJECT>    { using type = Object;    static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_FUNC>      { using type = Function;  static constexpr bool is_pointer = true; };
template<> struct ItemTagToType<LMD_TYPE_ERROR>     { using type = LambdaError; static constexpr bool is_pointer = true; };

// inline-packed scalars: the "type" is the C++ value type, no pointer
template<> struct ItemTagToType<LMD_TYPE_NULL>      { using type = void;      static constexpr bool is_pointer = false; };
template<> struct ItemTagToType<LMD_TYPE_BOOL>      { using type = bool;      static constexpr bool is_pointer = false; };
template<> struct ItemTagToType<LMD_TYPE_INT>       { using type = int32_t;   static constexpr bool is_pointer = false; };
template<> struct ItemTagToType<LMD_TYPE_UNDEFINED> { using type = void;      static constexpr bool is_pointer = false; };

} // namespace lam
```

Adding a new `LMD_TYPE_*` without a specialization is **a compile error at
every `as<>` / `require<>` call site** — the same property `view_as<>`
already gives Radiant.

---

## 3. The post-check witness — `ItemOf<Tag>`

This is the heart of the type/pattern-matching answer. After one boundary
check, the C++ type carries the proof — code in the matched branch sees a
strongly typed pointer.

```cpp
template<TypeId Tag>
class ItemOf {
    Item raw_;
public:
    using Pointee = typename ItemTagToType<Tag>::type;

    explicit ItemOf(Item it) : raw_(it) { assert(get_type_id(it) == Tag); }

    Item raw() const { return raw_; }

    // pointer-ish container/scalar tags
    template<TypeId T = Tag, std::enable_if_t<ItemTagToType<T>::is_pointer, int> = 0>
    Pointee* ptr() const {
        return reinterpret_cast<Pointee*>(raw_ & 0x00FFFFFFFFFFFFFFULL);
    }
    template<TypeId T = Tag, std::enable_if_t<ItemTagToType<T>::is_pointer, int> = 0>
    Pointee* operator->() const { return ptr(); }
};

// boundary check — single source of truth for tag inspection
template<TypeId Tag>
std::optional<ItemOf<Tag>> as(Item it) {
    if (get_type_id(it) != Tag) return std::nullopt;
    return ItemOf<Tag>(it);
}

// asserting variant for hot paths where the tag has already been established
template<TypeId Tag>
ItemOf<Tag> require(Item it) {
    assert(get_type_id(it) == Tag);
    return ItemOf<Tag>(it);
}
```

`ItemOf<Tag>` is `sizeof(Item)` and trivially copyable, so it lowers to
identical machine code to passing a raw `Item`. A function that only accepts
arrays takes `ItemOf<LMD_TYPE_ARRAY>` and **wrong types fail to compile**
after the boundary, not at runtime.

---

## 4. Pattern matching with branch-local refinement

Three shapes of pattern match are useful in practice. They share one rule:
**after one tag check, the matched name is a strongly typed witness for the
rest of the block** — no further casts, no defensive `if (get_type_id(...))`.

### 4.1 Single-tag refinement — `if (auto x = as<Tag>(it))`

The lightweight, every-other-line use. Equivalent to Rust
`if let Some(x) = …` or C# `if (it is Array a)`.

```cpp
if (auto arr = as<LMD_TYPE_ARRAY>(it)) {
    // arr->length, arr->items — Array* methods, type-checked
    for (size_t i = 0; i < arr->length; ++i) {
        format_json(out, arr->items[i]);
    }
}
```

Stack several of them for "first matching arm" dispatch:

```cpp
bool is_truthy(Item it) {
    if (auto b = as<LMD_TYPE_BOOL>(it))   return b.value();          // .value() : bool
    if (auto i = as<LMD_TYPE_INT>(it))    return i.value() != 0;     // .value() : int32_t
    if (auto s = as<LMD_TYPE_STRING>(it)) return s->len > 0;         // s-> is String*
    if (auto a = as<LMD_TYPE_ARRAY>(it))  return a->length > 0;      // a-> is Array*
    return get_type_id(it) != LMD_TYPE_NULL
        && get_type_id(it) != LMD_TYPE_UNDEFINED;
}
```

### 4.2 Tag-typed function parameters

The "match" can also live in the parameter type. The function body assumes
the witness; wrong tags fail to compile at every call site.

```cpp
// Only accepts arrays. The body never re-checks the tag.
String* format_array_to_json(ItemOf<LMD_TYPE_ARRAY> arr, StrBuf* out);

void caller(Item it) {
    if (auto a = as<LMD_TYPE_ARRAY>(it)) {
        format_array_to_json(*a, out);          // ✅ refined witness
    }
    format_array_to_json(it, out);              // ❌ won't compile —
                                                //    Item ≠ ItemOf<LMD_TYPE_ARRAY>
}
```

This is where most of the daily safety win lives. The function signature
says exactly which tag it expects, the type system enforces it, and the body
skips the boilerplate.

### 4.3 Exhaustive `visit()` — the big switch replacement

For functions that must handle *every* tag (formatters, printers, GC tracers,
shape-walkers), mirror Radiant's `visit_view`. The dispatch is one switch on
`get_type_id(it)`; each branch passes a strongly typed `ItemOf<Tag>` to the
caller's lambda.

```cpp
template<class F>
decltype(auto) visit(Item it, F&& f) {
    switch (get_type_id(it)) {
        case LMD_TYPE_NULL:    return f(require<LMD_TYPE_NULL>(it));
        case LMD_TYPE_BOOL:    return f(require<LMD_TYPE_BOOL>(it));
        case LMD_TYPE_INT:     return f(require<LMD_TYPE_INT>(it));
        case LMD_TYPE_INT64:   return f(require<LMD_TYPE_INT64>(it));
        case LMD_TYPE_FLOAT:   return f(require<LMD_TYPE_FLOAT>(it));
        case LMD_TYPE_STRING:  return f(require<LMD_TYPE_STRING>(it));
        case LMD_TYPE_ARRAY:   return f(require<LMD_TYPE_ARRAY>(it));
        case LMD_TYPE_MAP:     return f(require<LMD_TYPE_MAP>(it));
        case LMD_TYPE_ELEMENT: return f(require<LMD_TYPE_ELEMENT>(it));
        // ...
        default:               return f(it); // raw fallback
    }
}
```

Used to replace the canonical `switch (get_type_id(it)) { case … reinterpret_cast<…>(...) }`
that lives today in `lambda/print.cpp` and `lambda/format/format-json.cpp`:

```cpp
void format_json(StrBuf* out, Item it) {
    visit(it, [&](auto v) {
        using V = std::remove_reference_t<decltype(v)>;

        // ─── inline scalars ───────────────────────────────────────────────
        if constexpr (std::is_same_v<V, ItemOf<LMD_TYPE_NULL>>) {
            strbuf_append(out, "null");
        }
        else if constexpr (std::is_same_v<V, ItemOf<LMD_TYPE_BOOL>>) {
            strbuf_append(out, v.value() ? "true" : "false");
        }
        else if constexpr (std::is_same_v<V, ItemOf<LMD_TYPE_INT>>) {
            strbuf_append_int(out, v.value());
        }

        // ─── boxed scalars: v->… is the typed pointee ─────────────────────
        else if constexpr (std::is_same_v<V, ItemOf<LMD_TYPE_FLOAT>>) {
            strbuf_append_double(out, *v.ptr());        // v.ptr() : double*
        }
        else if constexpr (std::is_same_v<V, ItemOf<LMD_TYPE_STRING>>) {
            json_escape(out, v->chars, v->len);         // v-> is String*
        }

        // ─── containers ───────────────────────────────────────────────────
        else if constexpr (std::is_same_v<V, ItemOf<LMD_TYPE_ARRAY>>) {
            strbuf_append_char(out, '[');
            for (size_t i = 0; i < v->length; ++i) {    // v-> is Array*
                if (i) strbuf_append_char(out, ',');
                format_json(out, v->items[i]);
            }
            strbuf_append_char(out, ']');
        }
        else if constexpr (std::is_same_v<V, ItemOf<LMD_TYPE_MAP>>) {
            strbuf_append_char(out, '{');
            for (ShapeEntry* e = v->shape; e; e = e->next) {
                json_escape(out, e->name->chars, e->name->len);
                strbuf_append_char(out, ':');
                format_json(out, map_field_value(v.ptr(), e));
            }
            strbuf_append_char(out, '}');
        }
        else if constexpr (std::is_same_v<V, ItemOf<LMD_TYPE_ELEMENT>>) {
            // Element extends Array AND Map — both views compile.
            //   as_map(v)   : Map*    — walk shape entries (attributes)
            //   as_array(v) : Array*  — iterate child items
            // The wrong call (e.g. as_array on an ItemOf<MAP>) is a compile error.
            strbuf_append(out, "{\"$tag\":");
            json_escape(out, v->name->chars, v->name->len);

            for (ShapeEntry* e = as_map(v)->shape; e; e = e->next) {
                strbuf_append_char(out, ',');
                json_escape(out, e->name->chars, e->name->len);
                strbuf_append_char(out, ':');
                format_json(out, map_field_value(as_map(v), e));
            }
            Array* children = as_array(v);
            if (children->length) {
                strbuf_append(out, ",\"$children\":[");
                for (size_t i = 0; i < children->length; ++i) {
                    if (i) strbuf_append_char(out, ',');
                    format_json(out, children->items[i]);
                }
                strbuf_append_char(out, ']');
            }
            strbuf_append_char(out, '}');
        }

        // ─── exhaustiveness guard ─────────────────────────────────────────
        else {
            // Forgetting a tag instantiates this branch, so the static_assert fires.
            // Adding a new LMD_TYPE_* without handling it breaks the build here.
            static_assert(always_false_v<V>,
                "format_json: non-exhaustive visit; handle the missing TypeId");
        }
    });
}
```

What the `visit()` rewrite buys, line for line, against the legacy `switch`:

| Legacy `switch` body | `visit()` branch |
|---|---|
| `Array* a = (Array*)(it & 0x00FFFFFFFFFFFFFFULL);` | `v->length`, `v->items[i]` — `v` is already typed |
| Easy to write `String* s = (String*)…` in the `LMD_TYPE_ARRAY` case by accident | Wrong field access fails to compile |
| Adding `LMD_TYPE_NEW_THING` silently falls through `default` | Adding it without a branch breaks the build |
| Element's dual nature is implicit ("remember to walk both sides") | `as_map(v)` / `as_array(v)` are both typed; misuse is rejected |
| Mixed inline-scalar bit ops and pointer derefs | `.value()` for inline tags, `->` for pointer tags — never confused |

### 4.4 Group-trait dispatch — "any map" / "any array"

Some callers want to handle a *family* of tags uniformly (e.g. anything
map-shaped, anything array-shaped). Combine `visit()` with the group traits
from §5:

```cpp
size_t length_of(Item it) {
    return visit(it, [](auto v) -> size_t {
        using V = std::remove_reference_t<decltype(v)>;
        if constexpr (requires { v.tag(); } && IsArrayLike<V::tag()>::value) {
            return as_array(v)->length;
        } else if constexpr (requires { v.tag(); } && IsMapLike<V::tag()>::value) {
            return map_field_count(as_map(v));
        } else {
            return 0;
        }
    });
}
```

(Where `ItemOf<Tag>::tag()` is a `static constexpr TypeId tag() { return Tag; }`.)
This is the practical equivalent of Rust `match Foo::Array(_) | Foo::Element(_)` —
one branch covers a whole family, but the witness inside the branch is still
narrowed to the matched concrete tag.

### 4.5 When *not* to use `visit()`

- Hot one-tag paths — `if (auto x = as<Tag>(it))` is cheaper to read.
- Code where the tag is statically known from context (e.g. a function that
  was just handed `ItemOf<LMD_TYPE_ARRAY>`). No dispatch needed.
- C ABI boundaries called from MIR — keep the raw `Item` signature; do the
  one-line `require<Tag>(it)` *inside* the function.

The rule of thumb: reach for `visit()` whenever the existing code says
`switch (get_type_id(...))`.

---

## 5. Element = Map ∪ Array → group traits, not 1-to-1

`Element` is the painful case: it extends `Array` and acts as a `Map`. Model
it with **group traits**, the same way `IsBlockView` already does for
Radiant:

```cpp
template<TypeId T> struct IsMapLike : std::false_type {};
template<> struct IsMapLike<LMD_TYPE_MAP>     : std::true_type {};
template<> struct IsMapLike<LMD_TYPE_VMAP>    : std::true_type {};
template<> struct IsMapLike<LMD_TYPE_ELEMENT> : std::true_type {};
template<> struct IsMapLike<LMD_TYPE_OBJECT>  : std::true_type {};

template<TypeId T> struct IsArrayLike : std::false_type {};
template<> struct IsArrayLike<LMD_TYPE_ARRAY>     : std::true_type {};
template<> struct IsArrayLike<LMD_TYPE_ARRAY_NUM> : std::true_type {};
template<> struct IsArrayLike<LMD_TYPE_ELEMENT>   : std::true_type {};   // dual nature

// refinement helpers — branch-local narrowing for "any map" / "any array"
template<TypeId T, std::enable_if_t<IsMapLike<T>::value, int> = 0>
Map* as_map(ItemOf<T> v) { return reinterpret_cast<Map*>(v.ptr()); }

template<TypeId T, std::enable_if_t<IsArrayLike<T>::value, int> = 0>
Array* as_array(ItemOf<T> v) { return reinterpret_cast<Array*>(v.ptr()); }
```

Now `as_map(elem)` and `as_array(elem)` both compile, while `as_array(map)`
does not.

This is the right place to also encode "Element extends Array" as an
inheritance-style coercion — `ItemOf<LMD_TYPE_ELEMENT>` can implicitly decay
to a `BorrowedPtr<Array, GcHeapDomain>` via a converting operator.

---

## 6. Memory domain for Lambda data — extend the existing `DomainOutlives`

Lambda has the GC heap, the pool/arena (script heap, namepool), and the
input/print scratch buffers. Map them onto the existing trait:

```cpp
// lib/ownership.hpp already declares GcHeapDomain, PoolDomain, LayoutSessionDomain.
// Add a Lambda-side scratch domain when needed; otherwise reuse PoolDomain
// for arena/namepool because they share the "bulk-freed, no individual destroy" rule.

struct InputScratchDomain {};   // memtracked during one input parse, freed on completion
template<> struct DomainOutlives<GcHeapDomain, InputScratchDomain> : std::true_type {};
template<> struct DomainOutlives<PoolDomain,   InputScratchDomain> : std::true_type {};
// pool/GC fields may NOT borrow from input scratch — use promote_to_pool() / copy_to_gc().
```

Then a `MarkBuilder` field that retains a string from the input parser uses
`PersistentField<char, GcHeapDomain>` and the wrong choice — pointing into
freed scratch — fails to compile.

---

## 7. Dual interface — what MIR sees vs what C++ sees

| Surface | Continues to use | Reasoning |
|---|---|---|
| MIR JIT call sites in `mir.c`, `transpile-mir.cpp`, `sys_func_registry.c` | raw `Item`, `Map*`, `Array*`, `Element*` | ABI stable; `fn_ptr` table unchanged |
| Runtime helpers exposed to MIR (`extern "C"`) | raw types in signature, `ItemOf<>` internally | `ItemOf<Tag>` is `sizeof(Item)`, trivially convertible — `static_cast` / no-op at the boundary |
| Lambda runtime C++ code (`lambda-eval.cpp`, `mark_builder.cpp`, `print.cpp`, `input/*.cpp`, `format/*.cpp`) | `ItemOf<>`, `as<>`, `visit()`, `IsMapLike` traits | All gains live here |

Concretely, a runtime helper looks like this:

```cpp
extern "C" Item lmd_array_get(Array* arr, int64_t idx) {     // C ABI unchanged
    auto a = lam::ItemOf<LMD_TYPE_ARRAY>::from_ptr(arr);     // free witness
    return lam::array_get_impl(a, idx).raw();                // C++ implementation
}
```

`ItemOf<>::from_ptr(p)` reconstructs the witness from a raw pointer that the
caller already knows is an array — same trick as Radiant's `view_require<>`,
no runtime work.

---

## 8. Other safety levers worth pulling

These are all low blast-radius and complement the template work:

1. **Replace raw `Map*` / `Array*` parameters with `BorrowedPtr<Map, GcHeapDomain>`**
   in C++ runtime functions. Same size, same calling convention; documents the
   borrow.
2. **`PersistentField<char, GcHeapDomain>`** for retained string fields on
   `String` / `Symbol` / element tag names. Today these accept raw `char*`
   from anywhere; the trait check rejects scratch-allocated strings without
   `promote_to_*`.
3. **`ItemOrError`**: a typed wrapper around the existing `RetMap` / `RetArray`
   / etc. result structs — `[[nodiscard]]` so callers can't drop the error.
4. **`ShapeRef`** (`BorrowedPtr<ShapeEntry, GcHeapDomain>`) — currently
   `ShapeEntry*` chains are walked everywhere with raw pointers, which has
   caused real bugs (see `/memories/repo/shape-pool-uninitialized-ns-bug.md`).
5. **`HoleSentinel` distinct type for array elision values** — today the
   sentinel is just an `Item` value; making it a wrapper that only `Array`
   accessors can produce/consume catches accidental leaks of holes into
   user-visible code.
6. **`MarkBuilder` typed outputs**: builder methods that statically know
   their tag (e.g. `start_map`, `start_element`) return `ItemOf<Tag>` so
   downstream code carries the proof without re-checking.
7. **`make check-item-cast`** lint target mirroring `make check-int-cast` —
   forbid raw `reinterpret_cast<Map*>(item_pointer(it))` outside `lib/`.
8. **Phase migration**: do **`MarkBuilder` / `MarkReader` first** — it's the
   natural choke point for input → Lambda data construction, and most
   insidious lifetime bugs (string borrowed from scratch, retained on GC
   element) live there. The same pattern that worked for Radiant — pick one
   TU, convert end-to-end, validate ergonomics — applies cleanly.

---

## 9. Honest limits (same caveats as the Radiant doc)

- MIR JIT-emitted code remains raw `Item` arithmetic — templates can't prove
  anything inside `mir.c`. The win is at the C/C++ helper boundary.
- `ItemOf<Tag>` only proves the tag at construction. If the underlying object
  is freed, you still get UAF. Pair with `BorrowedPtr<T, GcHeapDomain>` and
  the existing GC tracing; templates don't replace the collector.
- Compile-time blow-up is real once specializations multiply; keep all
  `ItemTagToType` specializations in one header, exactly as `ViewTagToType`
  is today.

---

## 10. Concrete next deliverable (mirrors the Radiant Phase 1 + 2)

1. `lib/lambda_typed.hpp` — `ItemTagToType`, `ItemOf<Tag>`, `as<>` /
   `require<>`, `visit()`, `IsMapLike` / `IsArrayLike`, `as_map` / `as_array`.
2. `test/test_lambda_typed.cpp` — positive/negative compile tests:
    - adding a fake `LMD_TYPE_*` without specialization fails to compile;
    - `as<LMD_TYPE_ARRAY>(map_item)` returns `nullopt`;
    - `require<>` asserts;
    - ABI sizes verified with
      `static_assert(sizeof(ItemOf<LMD_TYPE_ARRAY>) == sizeof(Item))`.
3. Convert one Lambda TU end-to-end as the validation target — recommend
   `lambda/print.cpp` or `lambda/format/format-json.cpp` because both already
   do exhaustive tag-driven dispatch and would benefit immediately from
   `visit()`.

No existing production call sites change in step 1 or 2. Ergonomics are
evaluated on the chosen TU in step 3 before broader rollout.
