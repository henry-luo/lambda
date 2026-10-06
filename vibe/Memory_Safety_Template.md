# Memory Safety Templates — Lambda / Radiant

**Status:** design partly implemented. This document consolidates the former
`Memory_Safety_Template.md` (ownership domains, typed lists, Radiant tag
dispatch), `Memory_Safety_Template2.md` (typed `Item` layer),
`Memory_Safety_Template3.md` (bounds, overflow, recursion, allocation results,
handles) and `Memory_Safety_Template4.md` (scope-bound temporaries and pointer
escapes) into one design, together with the two early proposals it grew from
(`vibe/idea/Memory_Safety_Template_GPT.md`, `vibe/idea/Memory_Safety_Template_Opus.md`).
It states the final design only; §11 records what the code implements as of the
survey, §12 what is still outstanding.
**Date:** 2026-10-06 (code survey against master `2e21d6e5b`).
**Spec linkage:** D2.1.1 (the three `Item` storage classes), D2.1.2
(`get_type_id` is the only semantic-type interface), D2.1.5 (container
constructors never OR a TypeId), D4.1.1v2 (content tiers), D4.1.4v5 (the four
allocation mechanisms), D4.2.2v2 (`MemContext` is the single allocation-failure
coordinator), D4.2.5v3 (no libc allocation outside `lib/` memory managers),
D4.5.1v4 (Radiant seam: arenas-as-regions, type-stable pools, generation
handles, RAII; contracts pin / gen-check / copy-as-value), D4.5.2 (Radiant
never retains a GC pointer; ruled 2026-10-06, argued in §2.3). Apart from
D4.5.2 nothing here is a new ruling; where this document and the formal spec
disagree, the spec wins.
**Scope:** header-only C++ facilities in `lib/`, `lambda/core/` and `radiant/`,
the lint rules that enforce them, and the ownership rules for raw pointers that
cross a lifetime boundary. Allocator internals, the GC and the MIR-emitted code
are out of scope.
**Related:** field-kind templates for Radiant heap objects —
[radiant/Radiant_Design_Mem_Safety.md](radiant/Radiant_Design_Mem_Safety.md);
runtime OOM machinery — [Memory_Context.md](Memory_Context.md); retained CSS
value audit — [Memory_CSS_Value_Retention_Audit.md](Memory_CSS_Value_Retention_Audit.md);
C+ language subset — [doc/dev/C_Plus_Convention.md](../doc/dev/C_Plus_Convention.md).

---

## 1. Goals and constraints

### 1.1 The safety axes

Each facility answers one question about a pointer:

| Axis | Question | Facility |
|---|---|---|
| Ownership | who frees it, and may this holder keep it? | domains, `OwnedPtr`/`BorrowedPtr`, retained-field setters (§2), typed lists (§3) |
| Type | what does the tag say it is? | tag→type maps, witnesses, `visit` (§4) |
| Bounds | how big is it? | `Span<T>`, `ByteCursor` (§5.1) |
| Size arithmetic | did the size computation overflow? | checked math (§5.2) |
| Depth | can recursion exhaust the stack? | `RecursionGuard` (§5.3) |
| Allocation | can this allocation return null? | `NonNull<T>` vs `[[nodiscard]]` (§6) |
| Liveness | does the target still exist? | generational `Handle<T>` (§7) |
| Lexical lifetime | does the pointee outlive every use? | scope-bound helpers and escape rules (§8) |

### 1.2 Design constraints

Every facility must:

1. **Be zero-cost.** A wrapper is the size of what it wraps
   (`sizeof(ItemOf<Tag>) == sizeof(Item)`, `sizeof(BorrowedPtr<T,D>) == sizeof(T*)`),
   trivially convertible, and compiles to the same machine code.
2. **Leave the C ABI unchanged.** MIR-emitted code and `extern "C"` entry points
   keep raw `Item`, `Map*`, `Array*`, `void*`. Wrappers live on the C++ side and
   decay to raw at the edge; struct layouts (`View`, `DomNode`, `Item`,
   `CssDeclaration`, …) do not change.
3. **Follow the C+ convention.** No exceptions, no STL containers, no
   `new`/`delete`, no vtables. Failure is a return value (`bool`, null,
   `[[nodiscard]]`).
4. **Sit at boundaries, not everywhere.** A wrapper marks the point where
   ownership, tag or bound is established; internal code below the boundary may
   use raw pointers.
5. **Make unsafe operations noisy.** Bypasses are named `unsafe_*`,
   `unchecked*` or `raw*`, or carry a greppable `*_OK: <reason>` marker.
6. **Come with a lint rule.** Templates protect only the call sites that use
   them; a raw C field (`CssDeclaration::value`, a registry slot) can still
   encode the wrong operation. Each facility is paired with a `make lint` rule
   that keeps the raw pattern from returning (§9).
7. **Avoid metaprogramming gymnastics.** The goal is readable safety, not a
   second type system hidden in templates.

### 1.3 Bug classes

A cross-subsystem audit (2026-06) found that the memory-safety defects cluster
into a few recurring root causes. The facilities are organised around them,
not around individual bugs.

| Class | Root cause | Audit findings | Facility |
|---|---|---|---|
| A | allocator *can* return null, hot path writes through it | H2 `heap_strcpy`, H3 realloc-then-write, H4 size math → alloc, M5 mmap metadata / datetime | §6 |
| B | fixed buffer trusts a length computed elsewhere | C1 URL path normalisation stack overflow, H5 `snprintf` cursor overflow | §5.1 |
| C | cursor advanced past end of input without recheck | H1 truncated `\u` escape over-read | §5.1 |
| D | `int` size/count/index feeds an allocation or index | H3, H4, M2 `int64`→`int` truncation, M3 grid track count, arraylist doubling | §5.2 |
| E | long-lived holder keeps a raw pointer into storage freed on relayout | C3 animation `View*` use-after-free | §2, §7 |
| F | unbounded recursion on untrusted nesting | C2 AST-build stack overflow into an unarmed recovery point | §5.3 |
| G | pointer to a narrower-scope local escapes into a longer-lived container | CSS shorthand `decl.value = &local`, render clip shape on a dead frame, registry destroying a stack backend | §8 |
| H | wrong representation built by hand | JS companion map pointer OR-ed with a TypeId | §4.2, §8.7 |

All audit findings of severity Critical, High and Medium are fixed (§11).

### 1.4 Why not a different language

Rust would turn classes A–D into clean panics instead of corruption, but it
does not dissolve class E: a view tree with back-pointers plus registries into
pool storage is exactly where the borrow checker pushes code toward
generational arenas — a discipline adoptable in C++ now (§7). The unsafe core
(tagged `Item` packing, the GC nursery, MIR code emission, C FFI) would stay
unsafe in any language. The chosen strategy is therefore: reproduce the
compile-time guarantees of the safe subset as zero-cost C++ wrappers, enforce
them with lint, and cover the still-raw remainder with sanitizers and fuzzing.

---

## 2. Ownership domains (`lib/ownership.hpp`)

### 2.1 Domains

A domain is an empty tag type naming where storage lives and how it is
released. Domains are a compile-time view of the D4.1.4v5 mechanisms:

| Domain | Storage | Release | D4.1.4v5 mechanism |
|---|---|---|---|
| `GcHeapDomain` | runtime values | collector | GC heap |
| `PoolDomain` | document/style pools and arenas | bulk (arena) or owner teardown (pool) | arena, pool |
| `LayoutSessionDomain` | memtracked temporaries of one session (a layout pass, a function) | manual `mem_free`, RAII | tracked raw allocation |
| `InputScratchDomain` | memtracked scratch of one input parse | at parse completion | tracked raw allocation |
| `StaticDomain` | string literals and constant tables in the executable image | never | none — not heap bytes |

New domains are added only when a concrete cross-boundary bug or API boundary
demands one; session domains stay broad until then.

**`StaticDomain` is static storage only** (decided 2026-10-06): storage that
lives for the whole process because it is part of the program image. It is a
label, not an allocation mechanism — nothing allocates or frees it — so it does
not add a fifth mechanism to D4.1.4v5. Storage that is merely long-lived is
**not** static: interned names live as long as their name pool, and the
immortal Mark data of D4.1.2 as long as its `Input` arena or const pool; those
take their owner's domain (`PoolDomain`).

Scratch-arena allocations (tail-rewound, owned by one scratch stack, D4.1.4v5)
still have no domain; that is an open decision (§12.7).

### 2.2 Borrowed and owned pointers

```cpp
template<class T, class Domain> class BorrowedPtr;  // non-owning; any domain
template<class T, class Domain> class OwnedPtr;     // move-only; destroys via DomainTraits<Domain>

template<class T> using GcPtr      = BorrowedPtr<T, GcHeapDomain>;
template<class T> using PoolPtr    = BorrowedPtr<T, PoolDomain>;
template<class T> using SessionPtr = OwnedPtr<T, LayoutSessionDomain>;
template<class T> using Temp       = SessionPtr<T>;   // function-scoped memtracked buffer
template<class T> using StaticPtr  = BorrowedPtr<T, StaticDomain>;
```

- Pool, GC and static pointers are **borrowed**: the arena, pool, collector or
  program image owns the storage, so per-pointer RAII would be a lie.
- Session memory is **owned** per pointer: `OwnedPtr` frees on scope exit and
  decays implicitly to a `BorrowedPtr` of the same domain.
- `DomainTraits<Domain>::destroy` is the per-domain release policy (a no-op for
  GC, pool and input scratch; destruct + `mem_free` for a session). Static
  storage has no `DomainTraits`, so `OwnedPtr<T, StaticDomain>` does not
  compile — static storage is never owned.
- Construction helpers: `pool_make<T>`, `session_make<T>`, `session_strdup`,
  `temp_array<T>` / `temp_array_zero<T>` (overflow-checked), `gc_borrow(p)`,
  `static_borrow(p)`.

### 2.3 The outlives lattice

The single rule — *a holder may only keep a pointer into storage that lives at
least as long as the holder* — is stated once as a trait and consulted by every
retained-field and retained-list wrapper:

| `DomainOutlives<Source, Storage>` | Storage: GC | Pool | LayoutSession | InputScratch |
|---|---|---|---|---|
| Source: GC | ✅ | — (D4.5.2) | ✅ transient | ✅ transient |
| Source: Pool | — | ✅ | ✅ | ✅ |
| Source: LayoutSession | — | — | ✅ | — |
| Source: InputScratch | — | — | — | ✅ |
| Source: Static | ✅ | ✅ | ✅ | ✅ |

Unlisted pairs are false: the lattice **fails closed**. Static storage is
read-only, so it is never a storage domain — no holder lives in it.

**GC values in Radiant: borrow to read, copy to keep (D4.5.2).** Radiant
storage — the document arena, the view tree and their pools — is invisible to
the collector (D4.1.1v2), so a GC value referenced only from there can be
collected while the holder still points at it. Hence:

- **Retaining is a copy.** A pool-domain holder never stores a `GcPtr`; a GC
  value Radiant must keep is copied into the document arena
  (`promote_to_arena` / `promote_to_pool`), whose lifetime covers every holder
  in the document. GC→Pool is therefore absent from the lattice, and a retained
  field rejects a `GcPtr` like any other shorter-lived borrow.
- **Reading is a transient borrow.** Passing a GC value from Lambda into
  Radiant to be read during the current work — a `GcPtr` parameter, a local,
  a session-scoped temporary — stays allowed, which is why GC→LayoutSession
  and GC→InputScratch remain. This allowance is provisional: it stands until a
  breaking case shows a GC value collected under a transient reader.
- **Rooted references are exempt** (ruled 2026-10-06). A holder that needs the
  script value itself — a `FileList`, a Range/Selection wrapper whose identity
  scripts observe, history `state`, the editing session root, a custom-paint
  content element, a non-string UI attribute value — may keep the GC pointer
  if it holds a registered GC root that lasts at least as long as the
  document. The root keeps the value alive, so the storage's invisibility to
  the collector does not matter. An unrooted GC pointer in document storage is
  always a violation.
- A string literal is not a GC value. Labelling it `GcPtr` to satisfy the old
  GC→Pool edge (the `GcPtr` overload of `radiant_retain_font_family`) is a
  mislabel; its honest label is `StaticPtr`, which every retained field
  accepts without a copy.

### 2.4 Retained fields

A field that outlives the current session accepts only a borrow from a domain
that outlives its storage domain. Overload resolution with `= delete` gives a
per-call-site error:

```cpp
template<class T, class StorageDomain> class PersistentField;     // owns the slot
template<class T, class StorageDomain> class PersistentFieldRef;  // setter over an existing raw field

field.set(PoolPtr<char>(p));        // ✅ pool outlives pool
field.set(std::move(session_ptr));  // ❌ deleted: an owned session pointer must be promoted first
field.set(gc_borrow(s));            // ❌ deleted: copy a GC value into the document arena first (D4.5.2)
field.set(session_borrow);          // ❌ deleted: session does not outlive pool
```

`PersistentFieldRef` is the preferred form: it wraps an existing raw field, so
struct layout is preserved (constraint 2). Radiant exposes retained fields only
through named setters (`radiant_retain_font_family`,
`radiant_retain_background_image`, `radiant_take_image_source_path`, …) that
take a typed borrow or transfer; direct assignment to a retained field is a
lint error (§9).

For Radiant heap objects the per-field ownership vocabulary is the field-kind
template set of `lib/mem_kind.hpp` (`Own`, `OwnArr`, `Up`, `Counted`, `Shared`,
`Handle`, `ViewProp`, `Foreign`), specified in
[Radiant_Design_Mem_Safety.md](radiant/Radiant_Design_Mem_Safety.md). Kinds say
how a field relates to its holder's ownership tree; domains say which
allocation mechanism a borrow came from. The two compose: a retained-field
setter takes a domain-typed borrow and stores it into a kind-typed field.

### 2.5 Promotion vocabulary

Crossing a lifetime boundary is always a named, greppable copy or transfer —
never a generic `set`/`assign`:

| Operation | Meaning |
|---|---|
| `promote_to_pool(pool, src)` / `promote_to_arena(arena, s)` | copy into the longer-lived domain; the source keeps its own allocation. Also the only way for Radiant to keep a GC value (D4.5.2) |
| `copy_to_gc(heap, src)` | copy into the GC heap (declared, deliberately undefined until a GC copy path exists) |
| `take_ownership(raw)` | wrap an existing memtracked allocation as a `SessionPtr` |
| `detach_session_buffer(p)` | release a session buffer for an explicit hand-over to an owning field |

### 2.6 Escape hatches

`unsafe_borrow_raw(BorrowedPtr)` and `unsafe_release(OwnedPtr&)` are the only
sanctioned ways to strip a wrapper outside an ABI edge. Thin adapters at the
MIR/C boundary are their intended users.

### 2.7 Debug provenance checks

A domain label is a claim; the type system cannot see whether a raw pointer
really came from the storage its label names. The constructors that turn a raw
pointer into a domain-typed borrow or owner therefore verify provenance in
debug builds, and the check compiles away in release:

| Wrapping into | Debug check |
|---|---|
| `PoolPtr` from a pool or arena (`checked_pool_ptr` and arena equivalents) | `pool_owns(pool, p)` / `arena_owns(arena, p)` |
| `GcPtr` (`gc_borrow`) | `gc_is_managed(heap, p)` — the D4.1.1v2 gatekeeper range test |
| `SessionPtr` (`take_ownership`) | a lookup in the debug-build memtrack allocation registry (D4.2.5v3 keeps allocation-level records debug-only) |
| `StaticPtr` (`static_borrow`) | the address lies in the executable image's read-only data |
| `ItemOf<Tag>`, `view_require<Tag>`, `dom_require<Tag>` | the tag check itself (§4) |

A failure logs one line with a distinct prefix naming the wrapper and the
claimed domain, then asserts. A mislabelled borrow — a string literal passed as
`GcPtr`, a scratch pointer passed as `PoolPtr` — then fails at the boundary in
debug instead of passing the lattice under a false label.

---

## 3. Typed containers (`lib/arraylist.hpp`)

The legacy C `ArrayList` stores `void*` and cannot tell a borrowed pointer from
an owned one. The C++ list types make that decision visible in the type:

| Type | Owns | Use |
|---|---|---|
| `ArrayList<T>` | the element storage only | scalars, small structs, borrowed references; `ArrayList<PoolPtr<T>>` when the borrow's domain should be visible. Rejects `OwnedPtr` elements at compile time. |
| `ArrayOwnedList<T, Domain>` | the pointees, via `OwnedPtr<T, Domain>` | a local lifetime that owns its elements. `T` is the pointee type; `T*`, `OwnedPtr` and `BorrowedPtr` are rejected as `T`. `remove()` frees, `remove_owned()` transfers out. Exposes `domain` and `value_type`. |
| `PersistentList<List, StorageDomain>` | — (wrapper) | an owning list held as a *field*: `static_assert(DomainOutlives<List::domain, StorageDomain>)`. |

Rules:

- "Who owns the pointees" (the list's `Domain`) and "where may the list live"
  (`PersistentList`'s storage domain) are independent axes; one parameterised
  list plus a wrapper, not one class per domain. Aliases such as
  `SessionOwnedList<T>` only when the long form proves painful.
- A whole list crosses a domain only through `promote_list_to_pool(pool, src)`,
  which copies each pointee; the source list still frees its own.
- `operator[]` / `at()` are bounds-checked and abort with an
  `arraylist_*_oob` log line; `try_get()` is the non-aborting probe. Mutations
  return `bool` so allocation failure is handled without exceptions.
- The C `ArrayList*` survives only at C ABI boundaries and in unmigrated code.

---

## 4. Tag dispatch

Tagged-union dispatch is a runtime tag read followed by a cast. The templates
make the *cast result's type* a function of the tag, so a wrong cast does not
compile, an unmapped tag does not compile, and a forgotten case in an exhaustive
visit does not compile.

### 4.1 Radiant `View` and `DomNode` (`lib/tagged.hpp`)

```cpp
template<ViewType T> struct ViewTagToType;          // primary undefined: unmapped tag = compile error
template<ViewType T> auto view_as(View*)      -> typename ViewTagToType<T>::type*;  // null on mismatch
template<ViewType T> auto view_require(View*) -> typename ViewTagToType<T>::type*;  // LAM_CHECK on mismatch
template<class F> decltype(auto) visit_view(View*, F&&);                            // exhaustive

template<DomNodeType T> struct DomNodeTagToType;
template<DomNodeType T> auto dom_as(DomNode*);  template<DomNodeType T> auto dom_require(DomNode*);
```

- **Group helpers** cover families the 1-to-1 map cannot: `view_as_block` /
  `view_require_block` (every block-like tag), `view_as_element` /
  `view_require_element`, `view_require_text`, `view_require_table_cell`;
  `IsBlockView<T>` is the compile-time form.
- **`*_require` uses `LAM_CHECK`, not `assert`.** A wrong-tag downcast is type
  confusion; the check stays in release builds and fails closed.
- `dom_view` / `view_dom_node` convert between the DOM and view faces of the
  same node; `unsafe_view_block_api_span` is the one named bypass.

### 4.2 Lambda `Item` (`lambda/core/lambda_typed.hpp`)

The discriminant is the high byte of the 64-bit `Item` for tagged storage
classes and the first field of the object for containers (D2.1.1); the typed
layer reads it only through `get_type_id` (D2.1.2).

```cpp
template<TypeId Tag> struct ItemTagToType;   // pointee type + is_pointer / is_direct_pointer
template<TypeId Tag> class  ItemOf;          // post-check witness, sizeof(Item), trivially copyable
template<TypeId Tag> ItemMatch<Tag> as(Item);       // boundary check: `if (auto a = as<LMD_TYPE_ARRAY>(it))`
template<TypeId Tag> ItemOf<Tag>    require(Item);  // asserting form for an established tag
template<class F> decltype(auto)    visit(Item, F&&);  // exhaustive dispatch, one ItemOf<Tag> per branch
```

- **Witness.** After one check, `ItemOf<Tag>` carries the proof:
  `.value()` for inline tags, `->` / `.ptr()` for pointer tags, never confused.
  A function that takes `ItemOf<LMD_TYPE_ARRAY>` cannot be called with a raw
  `Item`. Reconstructing a witness from a pointer the caller already knows
  (`from_ptr`) costs nothing.
- **Exhaustiveness.** A `visit` lambda ends in
  `static_assert(always_false<V>)`; adding a TypeId without a branch breaks the
  build at every exhaustive visitor. Replace `switch (get_type_id(it))` with
  `visit`; use `as<>` for single-tag refinement; take `ItemOf<Tag>` parameters
  where the tag is statically known.
- **Group traits.** `IsMapLike` (Map, VMap, Element), `IsArrayLike` (Array,
  ArrayNum, Element), `HasArrayStorage` (Array, Element). Narrowing helpers
  follow the **real storage layout**: `as_array` accepts any `HasArrayStorage`
  tag (Element has an `Array` prefix); `as_map` accepts only `LMD_TYPE_MAP`.
  **`Element*` is never reinterpreted as `Map*`** — its attribute fields sit
  after the list fields; attribute access goes through `as_element` /
  `ElementReader`.
- **Representation helpers.** `HoleSentinel` is the only producer/consumer of
  the array-hole sentinel (`is_hole_sentinel`, `hole_sentinel_item`), so holes
  cannot leak into user-visible values as an ordinary `Item`.
  `ShapeRef` / `shape_next(owner, ref)` walk shape chains bounded by the owner
  type (D3.4.3v3), replacing raw `ShapeEntry*` walks.
- **Typed data construction and reading.** `MarkBuilder` methods that know
  their tag return `ItemOf<Tag>`; container parameters are typed GC borrows
  (`GcPtr<Map>`, `GcPtr<Element>`); `ItemReader::asItem<Tag>()` and the
  typed reader constructors carry witnesses through `MarkReader`.
- **Fallible results.** `ItemOrError` — a `[[nodiscard]]` typed wrapper over
  the existing `Ret*` result structs — is the `Result<T,E>` analogue for
  fallible runtime boundaries.

### 4.3 The dual interface

| Surface | Uses |
|---|---|
| MIR call sites (`transpile-mir.cpp`, `mir.c`, system-function registry) | raw `Item`, `Map*`, `Array*`, `Element*` — unchanged |
| `extern "C"` runtime helpers | raw types in the signature; `require<Tag>` / `ItemOf::from_ptr` inside |
| C++ runtime (`lambda/core`, `lambda/io`, `lambda/format`, `lambda/input`, `lambda/js`) | `ItemOf`, `as`, `visit`, group traits, `GcPtr` parameters |

Internal C++ helpers with no ABI boundary take `GcPtr<Array>` / `GcPtr<Map>` /
`PoolPtr<T>` instead of raw container pointers.

---

## 5. Bounds, size arithmetic and depth

### 5.1 Spans and cursors (`lib/span.hpp`)

A pointer that carries its end, so no callee can trust a foreign length
(classes B, C):

- `Span<T>` — non-owning `{data, len}`. Checked `operator[]` / `at()` (log +
  abort on out-of-bounds), non-aborting `get(i, &out)`, clamped `subspan` /
  `first`, range-for, explicit `raw()` decay at the ABI edge, and `unchecked(i)`
  as the named bypass.
- `ByteCursor` — `{p, end}` for recursive-descent parsers. `has(n)` /
  `advance(n)` / `take(&b)` fail instead of overrunning; `peek(i)` returns 0
  past the end. "Advance past the buffer" is not expressible.
- A writer into a caller buffer takes the buffer's real capacity (`Span<char>`
  or an explicit `cap`) — never a literal bound of its own. A formatted append
  clamps its cursor to `[0, cap)` instead of adding the would-have-written
  length.
- Format-specific readers (`read_hex4`, `read_until`, `expect`) are built once
  on `ByteCursor` and shared, so an escape decoder is audited once rather than
  copy-pasted per parser. `StrView` converges on `Span<const char>` plus string
  helpers.

### 5.2 Checked size arithmetic (`lib/math_checked.hpp`)

- `math_checked_mul` / `math_checked_add` / `math_checked_mul_add` (C) and
  `checked_mul` / `checked_add` / `checked_mul_add` (C++) return `false` on
  overflow; `math_size_round_up` / `math_size_align_up` likewise.
  `checked_narrow<To>(v, &out)` rejects a lossy narrowing.
- **Rule:** a size, count or capacity that feeds an allocation or an index is
  `size_t` (or `int64_t` where the data model says so), and every
  `count * elem_size` goes through a checked multiply. Growth goes through the
  overflow-checked helpers of `lib/mem_grow.hpp`, never an open `realloc`.
- Unit-bearing integers that are easily confused (`ByteLen` vs `CharIdx`,
  `ColIndex` vs column count) get a strong-typedef newtype, and fixed stack
  buffers become a `Bounded<T, N>` that refuses an overflowing write — both
  zero-cost.

### 5.3 Recursion depth (`lib/recursion_guard.hpp`)

`RecursionGuard g(&depth, LIMIT); if (!g) return error;` — one RAII guard per
recursive-descent or tree-walk entry over untrusted nesting, balanced on every
exit. The root-cause fix is a depth cap at every untrusted recursion (parsers,
AST build, CSS nesting, formatters). Defense in depth: the stack-overflow signal
handler jumps only to a recovery point that is **armed** for the current
activation and otherwise re-raises with the default action — never a
`siglongjmp` into an unarmed buffer.

---

## 6. Allocation results and OOM

### 6.1 Two allocator kinds, two return types

Class A is not fixed by sprinkling null checks; it is fixed by giving each
allocator kind an honest return type so the obligation lives in the type:

| | Fallible (Type 1) | Infallible (Type 2) |
|---|---|---|
| Allocators | pool, explicit large/mmap requests, growth | arena unit allocations within a chunk |
| On failure | returns null to the caller | never returns failure to the caller (§6.3) |
| Return type | `[[nodiscard]] T*` | `NonNull<T>` |
| Caller obligation | must check | none |
| Helpers | `checked_pool_array`, `checked_pool_sized`, `pool_try_new` (`lib/checked_alloc.hpp`, `lib/ownership.hpp`) | `arena_new<T>`, `arena_new_sized<T>(extra)`, `arena_bytes(n)`, `arena_emplace<T>` |

This mirrors Rust: the global allocator is infallible at the call site,
`try_reserve` is fallible.

### 6.2 `NonNull<T>`

Pointer-sized, storable in fields, usable for header+payload (flexible array)
objects and raw byte buffers (`NonNull<void>`), and decays to `T*` at the C/MIR
edge. A C++ reference cannot do any of those, so a reference appears only as
call-site sugar (`arena_emplace<T>`). **Non-null is not non-dangling:**
`NonNull` says nothing about lifetime; that is the domain/handle axis (§2, §7).

### 6.3 The OOM ladder

`NonNull` is honest only if the arena upholds it at its single failure point —
chunk acquisition. Unit allocations inside a live chunk are pointer bumps that
cannot fail. At the chunk boundary, the `MemContext` failure coordinator
(D4.2.2v2) runs the escalation ladder:

```
arena needs a chunk → page allocation
  └ fails → reclaim, cost-ordered: evictable caches → idle scratch arenas
            → forced GC → retire finished per-document sub-contexts
      └ retry fails → per-thread policy:
            PARK  (wait for a watermark, retry — request threads)
            FAIL  (surface OOM as a Lambda error — batch tools)
            ABORT (last resort, logged with a memory snapshot)
```

Sequencing: until the full ladder exists, chunk failure **aborts** with a
logged reason, so `NonNull` is truthful from day one; the ladder later replaces
only that one function body, and no `NonNull`-returning signature changes. The
FAIL policy deliberately reintroduces fallibility at the chunk boundary for
whole threads; unit allocations below it still never see null.

---

## 7. Generational handles

`PersistentField` forbids storing a pointer into shorter-lived storage; a
generational handle is the safe alternative for a holder that legitimately
refers to something rebuilt, freed or recycled while the holder lives (class E
— an animation targeting a DOM element whose arena slot may be retired and
reused, a display list referencing an image surface):

- `Handle<T> = {index, generation}` — 8 bytes, trivially copyable, not
  dereferenceable.
- The slot table belongs to a node that outlives every holder. Releasing a
  target advances the slot generation, so every copy of the handle goes stale
  at once with no walk over holders; a slot whose generation would wrap is
  retired for good.
- `lookup` reads only the table, never the target, and returns null for a stale
  handle — **fail closed**: the holder retires itself instead of dereferencing
  freed memory. A pointer returned by lookup is a borrow for the current turn;
  it is never stored back into a heap object.
- This is the D4.5.1v4 seam contract *gen-check*. Manual pointer scrubbing
  after a bulk free is the interim substitute for holders not yet migrated.

---

## 8. Scope-bound temporaries and pointer escapes

### 8.1 The escape rule

> Do not store `&local` or `&field_of_local` in a context, stack, queue, display
> list, callback, registry or deferred work item unless that container is proven
> to be consumed before the local's scope ends. Returning the local by value
> does not rescue pointers to its fields — they still name the old frame.

Any raw pointer inserted into a longer-lived container is a storage-domain
transition and is reviewed as one:

| Source storage | Destination | Policy |
|---|---|---|
| stack local | context stack, registry, display list, deferred task | reject, or clone/promote first |
| field of a returned-by-value local | any retained raw pointer field | reject |
| scratch arena | the active render/layout scope | allow if popped before the arena resets |
| static storage (literal, constant table) | any holder | allow as `StaticPtr`, no copy |
| pool allocation | retained tree or cache | allow through a §2 setter |
| GC allocation | Radiant document arena, view tree, their pools | reject; copy into the document arena (D4.5.2) |
| GC allocation under a registered root that outlives the document | document-side holder | allow (D4.5.2 exemption) |
| GC allocation | Radiant work that only reads it now | allow as a transient borrow (provisional, D4.5.2) |
| borrowed external object | lookup registry | allow only if the registry never owns the element's lifecycle |

Review question: *will every use of this pointer finish before the pointee's
storage can be reused, destroyed or reset?* If the answer depends on informal
call ordering, introduce a helper whose type or name encodes the storage domain
and lint the raw pattern.

### 8.2 CSS resolve-only declarations

Shorthand expansion routes each component to a longhand resolver through a
synthetic `CssDeclaration`. `CssDeclaration::value` is a raw C field that may
point at a pooled parsed value, a sub-value of a parsed shorthand, a synthetic
stack list, or a CSSOM-created value — the type cannot tell them apart.
The helpers (`radiant/view.hpp`) tie the scratch storage to the call:

```cpp
lam::CssTempDecl color(decl, CSS_PROPERTY_BACKGROUND_COLOR, item);   // one component
color.resolve(lycon);

lam::CssTempListDecl<2> pos(decl, CSS_PROPERTY_BACKGROUND_POSITION); // ≤ N components
pos.append(item); pos.append(next);
pos.resolve(lycon);   // the scratch list lives for the whole resolve call
```

Both are non-copyable and never hand out the declaration or the list; capacity
is in the type and `append` returns `false` when full.

**Resolver contract:** `resolve_css_property()` may read `decl->value` during
the call; it must not retain `decl`, `decl->value` or any child pointer of a
resolve-only declaration. A handler that genuinely needs to retain a value uses
a separate, explicitly retaining API, which resolve-only helpers never call.

**Retained CSS values** follow §2 instead:

| Site | Rule |
|---|---|
| style-tree, DOM inline-style and CSSOM declarations | pool/arena allocation; pool promotion before retaining a synthetic value |
| shorthand → longhand routing | `CssTempDecl` / `CssTempListDecl<N>` |
| animation keyframes, computed-style caches | pool-stable values only |
| style-tree clone / subset across pools | shallow: values stay owned by the source pool, which must outlive the clone (documented contract at the clone sites) |

### 8.3 Render-state stacks

A render-state stack (`RenderContext::clip_shapes`) outlives the function that
pushes onto it. A synthetic shape built on the stack is cloned into the render
scratch arena (`render_clip_clone_shape`) before it is pushed; the clip scope
records ownership and releases the clone when popped. A scope object never
embeds the storage a pushed pointer refers to.

### 8.4 Borrowed registries versus lifecycle owners

A registry of raw pointers means *borrow for lookup and dispatch*. Its destroy
function frees only the container; it never runs shutdown, free or destructor
callbacks on elements. Element lifecycle belongs to a distinct owner
(`BackendRuntimeOwner`-style: registry + `initialized` flag) whose teardown
shuts elements down while they are alive and then destroys the registry. Tests
may therefore register stack fixtures in a borrowed registry.

### 8.5 Owner bundles for deferred and reset state

- **Retained source.** When AST nodes, source ranges or generated code keep
  pointing at source bytes, the bytes move into the retained owner
  (`RetainedSource` → the deferred-compilation owner record). The deferred owner
  holds MIR context, source buffer, name pool and AST pool as **one lifetime
  bundle**; a failure before transfer frees through the local owner.
- **Reset paths.** Every runtime recycle path calls one teardown helper that
  owns the release order (name pool, type list, MIR context, then heap) — reset
  paths cannot drift apart.
- **Heap epochs.** State that may survive a heap recycle records the heap epoch
  it was created in; use after a recycle is an immediate invariant failure (at
  least in debug builds) instead of a delayed crash elsewhere.

### 8.6 Native callbacks

A function object whose native target is null looks callable and fails later.
Native functions are created only from a non-null, correctly typed target:
per-arity typed factories (`js_new_native_function(JsNativeP<n>)`), with no
untyped `void*` constructor in the public runtime API.

### 8.7 Item construction from container pointers

Containers are raw header pointers; only scalar leaves carry a high-byte tag
(D2.1.1). Container constructors never OR a TypeId into a pointer (D2.1.5) —
doing so corrupts the address. A container pointer becomes an `Item` only
through the canonical constructors or a dedicated accessor (for JS, the
property-storage layer of `lambda/js/js_props.h`); scalar packing goes through
the canonical `*2it` constructors; audited low-level packing carries a marker.

---

## 9. Enforcement

### 9.1 Lint rules

The gates are `make lint` rules (`utils/lint/run.sh --list`; one rule with
`make lint ARGS='--rule ^<id>$'`). Suppression is a trailing marker with a
reason.

| Design point | Rule id | Marker |
|---|---|---|
| §2.4 retained-field writes via setters | `retained-field-write` | `RETAINED_FIELD_OK` |
| §2.4 `Up`/`Shared` field kinds point at an outliving level | `mem-kind-nodes` (structural) | — |
| §4.1 View/DOM downcasts via `lib/tagged.hpp` | `no-radiant-view-cast-layout`, `no-radiant-view-cast-render` | `RADIANT_CAST_OK` |
| §4.2 no hand-masked `Item` payload casts | `no-item-payload-cast` | `ITEM_CAST_OK` |
| §4.2 / §8.7 tag literals and tag comparisons | `no-item-high-tag-literal`, `no-item-tag-literal-shift` | `ITEM_TAG_LITERAL_OK` |
| §4.2 semantic equality through one entry | `no-raw-item-equality` | `RAW_ITEM_EQ_OK` |
| §4.2 shape walks via `ShapeRef` | `no-raw-shape-chain-walk` | `SHAPE_CHAIN_OK` |
| §5.1 unbounded string scans, unsafe libc string calls | `unsafe-string-scan`, `no-unsafe-libc-str` | `STR_SCAN_LOCAL_OK`, `UNSAFE_LIBC_OK` |
| §5.2 growth through checked helpers | `no-open-realloc` (Radiant) | `REALLOC_OK` |
| §5.3 depth parameters actually bound recursion | `unused-depth-param` | `UNUSED_DEPTH_OK` |
| §6 / D4.2.5v3 no libc allocation in engine code | `no-raw-alloc`, `radiant-alloc-allowlist`, `radiant-no-bare-mem-free` | `RAWALLOC_OK`, `ALLOC_API_OK` |
| §8.2 CSS shorthand temporaries | `css-temp-decl` (`resolve_css_style.cpp`), `no-raw-css-value-stack` (rest of `radiant/`) | `CSS_TEMP_DECL_OK` |
| §8.1 large stack buffers | `large-stack-array` (info) | — |

### 9.2 Runtime backstop

Templates protect only migrated call sites. The still-raw remainder is covered
at test time: an AddressSanitizer build (`make build-debug-asan`) driven by the
script fuzzers (`make fuzz-lambda-asan`, `fuzz-radiant`, `fuzz-js`), the
fuzzy-crash regression scripts, the page-load suite and the GTest corpus. The target end state
adds UndefinedBehaviorSanitizer and one coverage-guided fuzz target per input
format.

---

## 10. Limits

- **Opt-in, not a borrow checker.** Wrappers protect the call sites that use
  them; lint keeps raw patterns from returning, but cannot prove a raw pointer
  correct.
- **They stop at the ABI.** Every wrapper decays to a raw pointer in an
  `extern "C"` signature and in MIR-emitted code.
- **Raw escape loses provenance.** Once a `T*` passes through `void*` or a
  legacy C API, no template sees it; audit `void*` round-trips and `unsafe_*`
  sites.
- **No whole-program lifetime proof.** Templates cannot prove an arena outlives
  every pointer it issued, nor that a claimed field kind is true.
- **Witnesses prove the tag, not liveness.** `ItemOf<Tag>` after the object is
  freed is still a use-after-free; the collector and rooting rules own that.
- **Handles cost a branch** and rely on the owner advancing generations on every
  release — the intended trade of a null check for a use-after-free.
- **The lattice is hand-curated.** An unmodelled domain pair over-rejects, which
  tempts `unsafe_*`; keep the bypass set small and lint it.
- **Compile time.** Keep each tag→type map in one header.

---

## 11. Implementation status (survey 2026-10-06)

Counts are production call sites in `lambda/`, `radiant/` and `lib/` outside
the defining headers.

### 11.1 Facilities

| Facility | Status | Adoption / notes |
|---|---|---|
| Domains, `BorrowedPtr`/`OwnedPtr`, `DomainOutlives`, `DomainTraits` | ✅ landed | used through the aliases; `InputScratchDomain` has no users |
| `GcPtr` / `gc_borrow` | ✅ adopted | 26 `GcPtr` sites, 143 `gc_borrow` sites (MarkBuilder, input parsers, JS runtime helpers) |
| `PoolPtr`, `SessionPtr`, `Temp<T>`, `temp_array` | ✅ adopted | `PoolPtr` 32; `Temp` 139 sites in 31 files (Radiant function-scoped temporaries) |
| `PersistentField` (owning slot) | ⚪ unused | the layout-preserving `PersistentFieldRef` was chosen instead |
| `PersistentFieldRef` + Radiant retained-field setters | ✅ landed | setters in `radiant/render.hpp` / `radiant/radiant.hpp`; lint currently failing (§12.1) |
| Field kinds (`lib/mem_kind.hpp`) | ✅ adopting | `Own` 198, `OwnArr` 97, `Up` 388, `Counted` 6, `Handle` 18 sites |
| Promotion vocabulary | 🟡 partial | `promote_to_pool` 9, `detach_session_buffer` 2; `promote_to_arena`, `take_ownership` unused; `copy_to_gc` declared `= delete` |
| `unsafe_*` escape hatches | ⚪ unused | |
| No GC pointer in retained Radiant storage (D4.5.2) | 🟡 partial | the lattice has no GC→Pool edge and a pool field rejects `GcPtr` (compile-time probe in `test_own_tagged`); every JS DOM and `lambda/dom` setter audited copies into the document's Input arena or pool; rooted document-side references are exempt; every UI-mode content append (runtime `list_push`, Input-owned `list_push_with_owner`, `MarkEditor` imports) copies content the document does not own, through one shared helper set (forced-GC regression test). Residue: values reaching the arena tree outside any append (§12.5 item 18) |
| `StaticDomain` / `StaticPtr` (§2.1) | ✅ landed | `static_borrow`; the six built-in font-family literals use it |
| Debug provenance checks (§2.7) | ⬜ not implemented | `checked_pool_ptr` exists (unused) but checks nothing; `gc_borrow` and `take_ownership` check nothing; `pool_owns`, `arena_owns` and `gc_is_managed` exist; no memtrack ownership query |
| `ArrayList<T>` | ✅ adopted | 47 sites in 13 files |
| `ArrayOwnedList<T, Domain>` | ✅ landed | 1 user (table collapsed borders) |
| `PersistentList`, `promote_list_to_pool` | ⬜ not implemented | |
| Radiant `view_as` / `view_require` / group helpers / `dom_as` | ✅ adopted | `view_require<>` 174, `view_as_block` 264, `view_require_block` 259, `dom_as<>` 31; `*_require` is `LAM_CHECK`; `visit_view` and the `IsBlockView` template path unused |
| `ItemOf` / `as` / `require` / `visit` / group traits | ✅ landed | in `lambda/core/lambda_typed.hpp` (`lib/lambda_typed.hpp` forwards); `visit` in `print.cpp` and MarkBuilder deep copy; witnesses in MarkReader / MarkBuilder; `as_map` excludes Element |
| `HoleSentinel`, `ShapeRef` / `shape_next` | ✅ landed | 2 and 11 sites |
| `MarkBuilder` typed outputs, typed GC-borrow parameters | ✅ landed | |
| `ItemOrError` | ⬜ not implemented | `MaybeItemOf` exists in `item_tagged.hpp`, unused |
| `Span<T>`, `ByteCursor` | 🟡 landed, unused | parser fixes applied the same discipline inline; no `StrView` convergence; format readers not built |
| Checked math | ✅ adopted | `checked_mul` 56, `math_checked_mul` 30, `checked_add` 44; growth via `lib/mem_grow.hpp` (50 files); `checked_narrow` unused |
| Newtypes, `Bounded<T, N>` | ⬜ not implemented | |
| `RecursionGuard` | 🟡 partial | CSS function and rule nesting; the Lambda parser has its own depth cap (`LAMBDA_RD_MAX_DEPTH`); formatters use a separate nested `RecursionGuard` class |
| Armed stack-overflow recovery | ✅ landed | per-frame `signal_armed`; unarmed → default action |
| `NonNull<T>`, `arena_new*` | 🟡 landed, unused | not yet honest: arena chunk failure still returns null (§12.2) |
| `checked_alloc.hpp` | 🟡 landed, unused | `checked_realloc` / `checked_malloc` call libc directly |
| OOM ladder | 🟡 partial | reclaimer registry and `MemContext` coordinator exist (D4.2.2v2); no cost-ordered reclaim at chunk failure, no PARK/FAIL/ABORT policy |
| `Handle<T>` + slot tables | 🟡 partial | image surfaces in display lists use generation handles; `lib/slot_table.hpp` has no users; animation targets and interaction-state holders are still raw |
| `CssTempDecl` / `CssTempListDecl<N>` | ✅ landed | in `radiant/view.hpp`; both CSS lints pass |
| Render clip clone-before-push | ✅ landed | `render_clip_clone_shape`; no dedicated lint |
| Borrowed backend registry | ✅ fixed | destroy frees the container only; no lifecycle-owner type, no lint |
| Retained JS source | 🟡 fixed ad hoc | preamble/deferred state owns `source_buffer`; no owner type |
| Single reset path, heap-epoch checks | ⬜ not implemented | several paths call `heap_destroy()` directly; `js_heap_epoch` drives cache invalidation only |
| Typed native callbacks | ✅ superseded | per-arity typed factories; no untyped constructor in the runtime headers |
| JS companion storage | ✅ superseded | companion properties live behind the `js_props` layer; `arr->extra` no longer holds a map pointer |
| Sanitizers / fuzzing | 🟡 partial | ASan build + script fuzzers; no UBSan build, no per-format fuzz targets |

### 11.2 Audit findings

| ID | Finding | Status |
|---|---|---|
| C1 | URL path normalisation wrote past a 1 KiB stack buffer | ✅ writer takes the real capacity |
| C2 | stack overflow during AST build jumped into an unarmed recovery point | ✅ parser depth cap + armed-frame recovery |
| C3 | animation `View*` used after relayout freed the view pool | ✅ vector gone: animation targets are now retained DOM elements that survive relayout, and targets detached from the document are cancelled at the layout reset; the target is still a raw pointer (§12.3) |
| H1 | truncated `\u` / trailing `\` read past end of input | ✅ peek-validate-then-advance; shared escape helper guards end of input |
| H2 | `heap_strcpy` wrote through a null allocation | ✅ |
| H3 | realloc result written before null check (display list, grid, flex) | ✅ checked growth; Radiant open `realloc` is now a lint error |
| H4 | overflowing size math in `str_repeat`, array factories, list expansion | ✅ checked multiply |
| H5 | `snprintf` return value walked a cursor out of `href_buf` | ✅ clamping append |
| M1 | null `parent` dereference before `is_block()` | ✅ |
| M2 | `int64` length truncated to an `int` loop counter | ✅ |
| M3 | grid auto-repeat track count overflow | ✅ |
| M4 | table column index one past the end | ✅ clamped |
| M5 | unchecked mmap-metadata `malloc` and datetime `pool_calloc` | ✅ |
| Low | HTML entity buffer sized without `static_assert`; PDF `compressed_len*4` (capped at 10 MB) | ⬜ not pursued |

---

## 12. Outstanding

### 12.1 Lint regressions and stale rule scopes

1. **`retained-field-write` fails with 17 findings.** Nine are the setter
   bodies themselves: the helpers now live in `radiant/render.hpp` and
   `lambda/input/css/dom_element.hpp` (moved out of `radiant/retained_fields.hpp`
   on 2026-07-14), but the rule still exempts only the old file. The other
   eight are direct writes to review — `layout_flex.cpp`, `layout_list.cpp`,
   `render_svg_inline.cpp`, `resource_loaders.cpp`, `svg_animation.cpp`,
   `view_prop_ensure.cpp`, `dom_element.cpp` (×2): route each through a setter
   or mark it `RETAINED_FIELD_OK` with the reason.
2. **The view-cast rules regressed:** `no-radiant-view-cast-layout` has 43
   findings and `no-radiant-view-cast-render` 29 (`layout_flex_multipass`,
   `layout_block`, `layout_positioned`, `layout_table`, `layout_multicol`,
   `event`, `grid_positioning`, …). Most are C-style casts to `DomNode*` or
   `ViewElement*`.
3. **`no-item-payload-cast` silently skips two of its four files:** its scope
   names `lambda/mark_builder.cpp` and `lambda/mark_reader.cpp`, which moved to
   `lambda/io/mark_builder.cpp` and `lambda/core/mark_reader.cpp`.
4. **Other stale paths:** `no-item-high-tag-literal` and `no-raw-item-equality`
   list `lib/lambda_typed.hpp` (now a forwarder to `lambda/core/lambda_typed.hpp`);
   `no-item-high-tag-literal` lists `lib/gc/gc_heap.c` (now
   `lambda/runtime/gc/gc_heap.c`); `no-raw-css-value-stack` ignores the removed
   `radiant/css_temp_decl.hpp`.

### 12.2 Allocation results

5. Make arena chunk failure honest before any code relies on `NonNull`: abort
   with a logged reason at the chunk boundary, then replace that body with the
   §6.3 ladder (cost-ordered reclaim, per-thread PARK/FAIL/ABORT).
6. Route `checked_realloc` / `checked_malloc` through `mem_realloc` /
   `mem_alloc` (D4.2.5v3) and mark `pool_try_new` `[[nodiscard]]`; then adopt
   the helpers or remove them.
7. Implement `ItemOrError` and convert the first `Ret*` path.
8. Build the unchecked-allocation-dereference lint (§6.1).

### 12.3 Liveness

9. Migrate long-lived DOM/view holders to generation handles —
   `AnimationInstance::target` (`void*`), cursor, drag/drop and
   interaction-state pointers. Animation targets are pruned only on the
   layout-reset path, while a removed element's arena slot can be retired and
   reused (D4.1.4v5), so a raw target can alias a later element in between.
   Decide whether DOM nodes use `lib/slot_table.hpp` or the image-surface
   registry pattern. `animation_scheduler_remove_views` now has test callers
   only; remove it once the handle migration lands.
10. Implement the debug provenance checks (§2.7): `pool_owns` / `arena_owns`
    in `checked_pool_ptr` and an arena equivalent, `gc_is_managed` in
    `gc_borrow`, and a debug memtrack ownership query for `take_ownership`.
    Relabel the `GcPtr`-labelled string literals as `StaticPtr` first
    (item 18), or they trip the GC check.

### 12.4 Bounds and depth

11. Adopt `Span` / `ByteCursor` in at least one input parser and build the
    shared format readers (§5.1); converge `StrView`.
12. Fold the formatter-local `RecursionGuard` classes (`format-utils.hpp`,
    the TOML formatter) onto `lam::RecursionGuard`; audit the remaining
    recursive-descent entries for a depth cap.
13. Newtypes and `Bounded<T, N>` (§5.2); the two Low audit items.

### 12.5 Ownership coverage

14. `PersistentList` and `promote_list_to_pool` (§3).
15. Retained strings in Lambda data: `InputScratchDomain` and GC-domain
    retained-field setters for MarkBuilder string retention have no users yet.
16. JS owner bundles (§8.5): one reset/teardown helper for every heap-recycle
    path; a retained-source owner type; heap-epoch assertions on state that can
    survive a recycle.
17. Lifecycle-owner type for serve registries (§8.4).
18. Enforce D4.5.2 (§2.3). Done 2026-10-06: `StaticDomain` / `StaticPtr`
    added, the GC→Pool edge dropped (probed in `test/lib/test_own_tagged.cpp`),
    and the font-family literals relabelled. The audit of the Lambda→Radiant
    write paths found every JS DOM and `lambda/dom` setter copying into the
    document's Input arena or pool.
    - **UI-mode script results — fixed 2026-10-06.** The result tree's
      elements live in the result `Input`'s arena, and UI-mode `list_push`
      used to copy only strings into it, leaving runtime symbols,
      non-spreadable arrays (with their strings) and GC elements — mutable
      clones, `group` results, join tuples — as unrooted GC children that the
      DOM build aliased and `rebuild_lambda_doc` re-read after later
      collections. `list_push` now deep-copies every non-string content value
      not already owned by the result `Input` into it (the runtime records the
      `Input` via `runtime_set_ui_result_input`), and a merged binary
      likewise. Regression: `RadiantViewTest.UiScriptContentSurvivesForcedGc`
      (`test/html/ui_script_content_gc.ls` under forced, poisoning GC).
    - **One implementation for every content append — 2026-10-06.** The UI
      content helpers (`ui_copy_string_to_arena`, `ui_merge_strings_to_arena`,
      `ui_copy_content_to_input`, the last through `MarkBuilder::deep_copy`)
      live once, in `lambda/io/collection_io.cpp`, and serve both the runtime
      `list_push` and the Input-owned `list_push_with_owner`, which now takes
      the UI `Input` instead of a flag (parsers reach it through
      `InputAllocationContext::input`). A value the `Input`'s arena or pool
      already holds is never copied, so parser data costs only the ownership
      test.
    - **`MarkEditor` imports — 2026-10-06.** `import_child` used to keep any
      child whose bytes in front looked like a DOM node header, reading in
      front of arbitrary GC objects. It now keeps a node by identity only when
      the node's storage is in the editor's arena or the UI node arena the edit
      bridge registers (`set_ui_node_arena`); anything else is deep-copied.
      `dom_edit_child` keeps the direct check: its DOM callers pass a
      `DomElement`'s backing element or a `DomText`'s string.
    - **Still open:** values that reach the arena tree without passing through
      any append — a script's top-level result (a GC element returned as the
      root) and the math package result spliced in by `cmd_layout.cpp`.
    - **Rooted document-side references are exempt** (§2.3): non-string UI
      attribute values (rooted by the context root vector), the
      custom-layout paint content element, history `state`, a file input's
      `FileList`, the editing session root, and Range/Selection host wrappers.

### 12.6 Enforcement gaps

19. Lints not built: `unsafe_*` bypass census; span/raw `ptr+len` indexing in
    `lambda/input/`; recursive entry without a guard; render-clip
    address-of-local push; registry destroy calling element shutdown; container
    pointer OR-ed with a TypeId (§8.7).
20. UndefinedBehaviorSanitizer build and per-format coverage-guided fuzz
    targets (§9.2).
21. Type-aware checks. The §9.1 rules match source patterns in listed files,
    so they cannot see types. Three checks need type information (the
    clang-tidy backend of `make lint-full`, or a libclang pass): a value whose
    type is a session wrapper or memtracked buffer assigned to a retained
    struct field; a read of an `Item` union member (`item.map`, `item.array`,
    …) with no dominating tag check; a C-style or `reinterpret_cast` from an
    `Item` payload to a container type anywhere in `lambda/`, not only in the
    files `no-item-payload-cast` lists.

### 12.7 Decisions needed

22. **Scratch arenas.** Should scratch-arena pointers get their own domain,
    outliving only the session that owns the scratch stack and never a pool
    holder, or be treated as `LayoutSessionDomain` borrows? §8.1 already
    allows them only within the active scope.
23. `require<Tag>` and the `ItemOf` constructor check with `assert` (debug
    only) while Radiant `*_require` uses always-on `LAM_CHECK`. Should Lambda's
    witness construction also fail closed in release, at the cost of a branch on
    hot runtime paths?
24. Should `resolve_css_property()` gain an overload that accepts only
    resolve-only or retained declarations, leaving the raw-pointer form as a
    legacy adapter, and should resolve-only declarations carry a debug flag that
    retained-field setters assert against?
25. Should `CssTempDecl` move next to the CSS parser so CSSOM and parser-side
    synthetic declarations can share it?
26. Heap epochs: debug-only assertion, or a release-build LambdaJS internal
    error?
