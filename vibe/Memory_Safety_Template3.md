# Memory Safety Fix Proposal — Lambda / Radiant

> Companion to [`Memory_Safety_Template.md`](./Memory_Safety_Template.md) (Radiant ownership + tag dispatch)
> and [`Memory_Safety_Template2.md`](./Memory_Safety_Template2.md) (Lambda `Item` typed model).
>
> Those two docs cover **temporal ownership** (domains, `OwnedPtr`/`PersistentField`) and
> **tag-confusion** (`ItemOf<Tag>`/`visit()`). This doc is the result of a fresh cross-subsystem
> memory-safety audit, and it targets the bug classes the existing templates **do not** cover:
> **spatial/bounds safety, allocation-failure safety, integer-overflow safety, and recursion-depth safety.**
> It reuses the existing template machinery for the ownership/UAF findings rather than re-inventing it.

---

## Part 0 — Executive Summary

A four-way parallel audit (lib/ data structures, lambda/ core runtime, lambda/input + format parsers,
radiant/ layout) found a set of concrete defects that cluster into **five recurring root-cause patterns**,
not random one-offs. The codebase is otherwise well-hardened (depth-limited parsers, correct tagged-pointer
scheme, bounds-checked getters, `SIZE_MAX/2`-guarded growth helpers, and the substantial ownership/tag
template work already landed).

The key finding for *this* proposal: **the existing safety templates already neutralize one of the five
patterns (raw-pointer-into-freed-pool, via `PersistentField`/domains) but the other four — which contain
the Critical and most of the High findings — have no structural defense yet.** The fix is to add four
small, zero-cost, C-ABI-safe header facilities that extend the existing design vocabulary, and to finish
*applying* the ownership templates to the Radiant view registries.

| Root-cause pattern | Covered by existing templates? | This proposal |
|---|---|---|
| **A.** Unchecked allocation return, then immediate write | ❌ (only `ItemOrError` sketched as future work) | **New:** two-type model — `NonNull<T>` (infallible arena, §3.7) + `[[nodiscard]] checked_alloc` (fallible pool, §3.3), backed by the Memory-Context OOM loop |
| **B.** Fixed buffer trusts a length computed elsewhere | ❌ | **New:** `lib/span.hpp` (`Span<T>` / `ByteCursor`) |
| **C.** Pointer advance past end-of-input without recheck | ❌ | **New:** `lib/span.hpp` (`ByteCursor`) |
| **D.** `int` for sizes/counts/indices feeding allocation | ❌ | **New:** `lib/checked_math.hpp` |
| **E.** Raw pointer into a pool freed out from under it | ✅ `PersistentField`/`DomainOutlives` (compile-time) | **Extend:** generational `Handle<T>` for the runtime case + apply to Radiant registries |
| (recursion / DoS, orthogonal) | ❌ | **New:** `lib/recursion_guard.hpp` |

---

## Part 1 — Memory Safety Issues Found

Severity key: **Critical** = exploitable memory corruption reachable from untrusted input; **High** =
corruption/over-read under realistic conditions; **Medium** = null-deref / off-by-one, mostly contained;
**Low** = latent / requires implausible inputs.

### Critical

**C1 — Stack buffer overflow in URL path normalization.**
[`lib/url_parser.c:1355`](../lib/url_parser.c) passes a 1024-byte `path_buf`; `url_normalize_path` writes
with a hard-coded 2047-byte bound. [`lib/url_parser.c:622`](../lib/url_parser.c)
`strncpy(path, "/", 2047)` NUL-pads **all 2047 bytes** whenever the path normalizes to zero segments
(e.g. path `"/"`), smashing ~1 KB of stack. Reachable from untrusted URL input via `url_parse_with_base`.
*Pattern B.*

**C2 — Stack-overflow → `siglongjmp` into a zeroed `jmp_buf`.**
[`lambda/build_ast.cpp:7307`](../lambda/build_ast.cpp) (`build_expr`) recurses unbounded over the CST.
The SIGSEGV recovery point `_lambda_recovery_point` is only armed immediately before JIT *execution*
([`lambda/transpile-mir.cpp:12353`](../lambda/transpile-mir.cpp)); a pathologically nested script overflows
the stack during the *earlier* AST-build phase, and the handler then `siglongjmp`s
([`lambda/lambda-stack.cpp:180`](../lambda/lambda-stack.cpp)) on a still-zero-initialized buffer → UB.
*Pattern: no recursion guard.*

**C3 — Use-after-free of `View*` in CSS animations after relayout.**
[`radiant/event.cpp:3599`](../radiant/event.cpp) destroys the view pool and scrubs stale pointers from the
cursor, drag/drop, `state_map`, and DOM nodes — but **not** the animation scheduler.
`AnimationInstance::target` holds a raw `View*`; the next tick dereferences it at
[`radiant/css_animation.cpp:829`](../radiant/css_animation.cpp). Triggered by any event-driven relayout while
an animation/transition runs. Same gap at three other `view_pool_destroy` sites (render_img.cpp, cmd_layout.cpp).
*Pattern E.*

### High

**H1 — Heap over-read on truncated `\u` escape in Mark parser.**
[`lambda/input/input-mark.cpp:58`](../lambda/input/input-mark.cpp): `(*mark) += 4` advances unconditionally,
jumping past the NUL terminator on input like `"\u"` or `"\uAB"`; the enclosing loop then reads out of bounds.
The bounds-checked pattern already exists in [`input-toml.cpp`](../lambda/input/input-toml.cpp) and
[`input-prop.cpp:77`](../lambda/input/input-prop.cpp). One-byte variant in
[`lambda/input/input-kv.cpp:136`](../lambda/input/input-kv.cpp). *Pattern C.*

**H2 — `heap_strcpy` dereferences allocation result without null check.**
[`lambda/lambda-mem.cpp:437`](../lambda/lambda-mem.cpp): `heap_alloc` can return NULL (lambda-mem.cpp:368-371)
but the caller `memcpy`s into `str->chars` unconditionally. Widely called (string indexing, formatting).
*Pattern A.*

**H3 — Unchecked `realloc` then immediate indexed write.**
[`radiant/display_list_storage.cpp:14`](../radiant/display_list_storage.cpp) (`dl_alloc_item`),
[`radiant/layout_grid.cpp:723`](../radiant/layout_grid.cpp) (`grid_item_array_realloc`),
[`radiant/layout_flex.cpp:2074`](../radiant/layout_flex.cpp) and `:3943` — realloc result unchecked, the new
slot written immediately after. Also `int new_cap` multiplied by `sizeof(...)` can overflow before the
multiply. *Patterns A + D.*

**H4 — Integer overflow in size math feeding allocation.**
`str_repeat` `str_len*times` with `uint32_t` `String::len` truncation
([`lambda/lambda-eval.cpp:331`](../lambda/lambda-eval.cpp));
`array_num_new`/`array_fill` `length*elem_size` ([`lambda/lambda-data-runtime.cpp:146`](../lambda/lambda-data-runtime.cpp));
`expand_list` unchecked `heap_data_alloc` + unguarded `capacity*2`
([`lambda/lambda-data.cpp:453`](../lambda/lambda-data.cpp)). *Patterns A + D.*

**H5 — `snprintf` return-value misuse overflows `href_buf`.**
[`lib/url_parser.c:493`](../lib/url_parser.c): `pos += n` uses the *would-have-written* length, so `pos` can
exceed `sizeof(href_buf)`; the next iteration computes an out-of-bounds pointer and an unsigned-wrapped size,
and `href_buf[pos]='\0'` writes OOB. Triggered by a URL path/query longer than ~4 KB. *Pattern B.*

### Medium

**M1 — Null-deref of `block->parent` before `is_block()`** at
[`radiant/layout_block.cpp:5746`](../radiant/layout_block.cpp), `:7366`, `:7543` (guarded everywhere else in
the file). *Null-pointer.*

**M2 — `int64 length` truncated to `int` loop counter** in deep-copy at
[`lambda/mark_builder.cpp:852`](../lambda/mark_builder.cpp). *Pattern D.*

**M3 — Grid auto-repeat track-count overflow** → undersized `mem_calloc` then OOB copy at
[`radiant/layout_grid.cpp:1033`](../radiant/layout_grid.cpp) (and mirrored rows path). *Pattern D.*

**M4 — Table `col_index` can equal `columns`** (off-by-one, currently contained) at
[`radiant/layout_table.cpp:5521`](../radiant/layout_table.cpp); unclamped `row_heights[start_row]` at
[`radiant/layout_table.cpp:2387`](../radiant/layout_table.cpp). *Off-by-one.*

**M5 — Unchecked `malloc` for mmap chunk metadata** at [`lib/mempool.c:183`](../lib/mempool.c); unchecked
`pool_calloc`+`strncpy` for datetime literals at [`lib/datetime.c:492`](../lib/datetime.c). *Pattern A.*

### Low / latent

- `int` capacity doubling in arraylist (needs ~16 GB to trigger) — [`lib/arraylist.c:85`](../lib/arraylist.c). *Pattern D.*
- HTML entity buffer sized exactly to current max with no `static_assert` — [`lambda/input/html5/html5_tokenizer.cpp:752`](../lambda/input/html5/html5_tokenizer.cpp).
- PDF decompressor `compressed_len*4` (mitigated by a 10 MB cap) — `lambda/input/pdf_decompress.cpp`. *Pattern D.*
- No recursion cap in the CSS parser (`lambda/input/css/css_parser.cpp`) — unlike JSON/XML/Mark/TOML.
- Memory leak in `url_resolve_path` segment overflow path — [`lib/url_parser.c:694`](../lib/url_parser.c).
- Stray `printf` in `strbuf_ensure_cap` overflow path — [`lib/strbuf.c:70`](../lib/strbuf.c) (violates the no-`printf` rule).

### The five root-cause patterns

| Pattern | One-line description | Findings |
|---|---|---|
| **A** | Alloc helper *can* return NULL, but the hot path writes through it unconditionally | H2, H3, H4, M5 |
| **B** | A fixed-size destination buffer trusts a length/bound computed by a different function | C1, H5 |
| **C** | Cursor advanced past end-of-input without re-checking the remaining length | H1 |
| **D** | `int` used for a size/count/index that multiplies into an allocation or indexes a buffer | H3, H4, M2, M3, arraylist/grid/pdf |
| **E** | A long-lived registry keeps a raw pointer into a pool that gets bulk-freed on relayout | C3 |

---

## Part 2 — Would Rust Solve This? (Adoption Analysis)

### What Rust prevents *by construction*

| Finding | Pattern | Rust outcome |
|---|---|---|
| C1 URL stack overflow | B | **Prevented** — `String`/`Vec` grow; slice writes are bounds-checked; no fixed stack array to smash. |
| H5 `snprintf` cursor overflow | B | **Prevented** — `write!`/`format!` can't walk a cursor off the end. |
| H1 / KV `\u` over-read | C | **Prevented** — slice indexing panics on OOB; `chars()`/`bytes()` iterators yield `None` on truncation. |
| H2 `heap_strcpy` null deref | A | **Prevented** — no null in safe Rust; allocation failure aborts rather than handing back a null. |
| H3 / H4 / M3 int-overflow → heap overflow | A+D | **Memory-safety prevented** — overflow is *defined* (panic in debug, wrap in release), and a wrapped capacity still can't cause an OOB write because the subsequent index is bounds-checked → clean panic, not corruption. |
| M2 `int64`→`int` truncation | D | Logic bug still possible (`as` truncates), but **not memory-unsafe** — no OOB follows. |
| M1 null `parent` deref | — | **Prevented** — `Option<&Parent>` forces the `None` case. |
| M4 off-by-one `col_index` | — | **Prevented** — turns into a panic instead of silent contained corruption. |
| C2 unbounded recursion → `siglongjmp` UB | — | **The UB is gone** (Rust hits a guard page and aborts cleanly; no setjmp/longjmp), **but the DoS remains** — still needs an explicit depth limit. |
| C3 animation `View*` UAF | E | **The interesting one** — see below. |

### What Rust does *not* solve

1. **The UAF (C3) is exactly where the borrow checker makes you work.** A view tree with parent/child/sibling
   back-pointers plus a registry holding references to pool-owned nodes is the canonical "fighting the borrow
   checker" shape. Plain `&` references can't express it; the idiomatic answers are `Rc<RefCell<>>` + `Weak`
   (a stale ref `upgrade()`s to `None`) or **generational-index arenas** (`slotmap` / `generational-arena`).
   Rust doesn't *solve* graph ownership — it **refuses to compile the unsafe version** and pushes you toward
   the generational-handle discipline. That discipline is adoptable in C+ today (Part 3.6).

2. **The unsafe core stays unsafe.** Lambda is a GC'd, JIT-compiling runtime with 56-bit tagged pointers, a
   bump-allocated nursery, and FFI to tree-sitter / SQLite / libuv / font libraries. Every one of those is
   `unsafe` in Rust: tagged-`Item` packing, the GC nursery, MIR machine-code emission, and all C FFI. The
   tagged-pointer type-confusion and truncation bugs *can still occur* inside those `unsafe` blocks — they'd
   be smaller and clearly marked, but not gone.

3. **Things Rust ignores entirely:** memory **leaks** are safe in Rust (`Rc` cycles leak; the `url_resolve_path`
   leak survives verbatim); **DoS** from deep recursion/huge input becomes a clean abort instead of corruption
   — better failure mode, still a denial of service; **logic errors** (wrong-but-in-bounds index) are untouched.

### The honest bottom line

For a runtime ingesting untrusted documents, Rust's biggest win is not "no bugs" — it's that the **failure
mode changes from silent heap corruption / potential RCE to a clean panic/abort.** C1, H1, H2, H3 are
exploitable in C; in Rust they're at worst a crash. That closes the security-relevant half of this audit.
But a full rewrite of 2.4M lines is not the takeaway, and Rust wouldn't have stopped C3 unless the
generational-arena discipline had been chosen — which we can choose in C++ now.

**The pragmatic conclusion driving Part 3:** reproduce Rust's *compile-time guarantees for the safe subset*
as zero-cost C+ wrappers (slices, checked alloc, regions, handles), and get the rest from sanitizers +
fuzzing at test time. This is also exactly the philosophy the existing two templates already commit to.

---

## Part 3 — Structural Fix Proposal

### 3.0 Design constraints (non-negotiable, from the C+ convention)

Every facility below must:

- **Be zero-cost** — compile to the same machine code as the raw pointer/length it wraps.
- **Decay to a raw pointer/length at the `extern "C"` / MIR-JIT boundary** — MIR only speaks C; the wrapper
  lives on the C++ side and `.get()`s down at the edge. (Same rule the existing `ItemOf<Tag>` / `BorrowedPtr`
  obey: `sizeof(wrapper) == sizeof(raw)`, trivially convertible.)
- **Use no exceptions, no STL containers, no `new`/`delete`, no vtables.** Failure is a return value
  (`bool` / sentinel / `[[nodiscard]]`), matching the convention's error model.
- **Be header-only under `lib/`** and gated by a `make check-*` lint, mirroring `make check-int-cast` /
  `make check-item-cast` that already exist.

### 3.1 The master table — Rust idiom → C+ pattern → finding it fixes

| Rust idiom | C+ realization (this proposal) | Already in repo? | Findings fixed |
|---|---|---|---|
| `&[T]` / bounds-checked slice indexing | **`Span<T>` / `ByteCursor`** (`lib/span.hpp`, new) | partial — `StrView` is non-owning but unchecked | **C1, H1, H5**, M4, all parser OOB |
| `try_reserve` → `Result` (fallible alloc) | **`[[nodiscard]] checked_alloc` / `try_*`** (`lib/checked_alloc.hpp`, §3.3) | only `ItemOrError` sketched | **H2, H3, H4, M5** |
| Infallible alloc (abort-on-OOM, always a value) | **`NonNull<T>` from the arena + Memory-Context OOM loop** (§3.7) | factory landed; OOM loop = Stage 2 | **H2, H4, M5** (structural) |
| `checked_mul` / overflow-is-an-error | **`checked_mul` / `checked_add`** (`lib/checked_math.hpp`, new) | no | **H3, H4, M2, M3**, arraylist/grid/pdf |
| Bounded recursion / stack probes | **`RecursionGuard` RAII** (`lib/recursion_guard.hpp`, new) | no | **C2**, CSS parser |
| Lifetimes / regions (`'a` outlives) | **`PersistentField` + `DomainOutlives`** | ✅ landed ([Template](./Memory_Safety_Template.md) §1) | **C3** (compile-time half) |
| `slotmap` / generational arena (`Weak`) | **`Handle<T>` = {index, generation}** (extend `ownership.hpp`, new) | no | **C3** (runtime half) |
| `Option<T>` / `NonNull<T>` | sentinels (`ItemNull`) + `BorrowedPtr::operator bool`; `NonNull<T>` wrapper (§3.7) | ✅ partial | M1, H2 |
| Tagged-union exhaustive `match` | **`ItemOf<Tag>` / `visit()`** | ✅ landed ([Template2](./Memory_Safety_Template2.md)) | (type-confusion class; latent here) |
| Newtype (`struct Bytes(usize)`) | strong-typedef `ByteLen` / `CharIdx` | no | latent off-by-one (M4) |

The next four sections specify the four new facilities; §3.6 covers applying the existing ownership templates to C3.

### 3.2 New facility A — bounded spans & input cursor (`lib/span.hpp`)

**Closes patterns B and C — the largest, most security-relevant class (C1, H1, H5).** The existing templates
have *nothing* for bounds; this is the biggest gap. The idea is Rust's slice: a pointer that *carries its end*,
so no callee can trust a foreign length.

```cpp
// lib/span.hpp — non-owning bounded view. sizeof == 2 pointers; zero-cost.
namespace lam {

template<class T>
struct Span {
    T* data_ = nullptr;
    size_t len_ = 0;

    constexpr size_t size() const { return len_; }
    constexpr bool   empty() const { return len_ == 0; }

    // checked element access: aborts (debug) / returns sentinel ref (release) on OOB.
    T& operator[](size_t i) const {
        SPAN_BOUNDS_CHECK(i < len_, "span_oob");   // log_error + abort in debug
        return data_[i];
    }
    bool get(size_t i, T* out) const { if (i >= len_) return false; *out = data_[i]; return true; }

    Span subspan(size_t off, size_t n) const {     // clamped, never over-reads
        if (off > len_) off = len_;
        if (n > len_ - off) n = len_ - off;
        return Span{ data_ + off, n };
    }
    T* raw() const { return data_; }               // explicit decay at the ABI edge
};

// Byte cursor for recursive-descent parsers: the end is part of the type, so
// "advance past the buffer" is structurally impossible.
struct ByteCursor {
    const uint8_t* p_;
    const uint8_t* end_;

    size_t remaining() const { return (size_t)(end_ - p_); }
    bool   has(size_t n) const { return remaining() >= n; }
    // peek returns 0 past the end instead of reading OOB — the H1/C1 class vanishes
    uint8_t peek(size_t i = 0) const { return (p_ + i < end_) ? p_[i] : 0; }
    bool   advance(size_t n) { if (!has(n)) return false; p_ += n; return true; }
    bool   take(uint8_t* out) { if (p_ >= end_) return false; *out = *p_++; return true; }
};

} // namespace lam
```

Fixes, concretely:

- **H1 / KV** — `lambda/input/input-mark.cpp:58` becomes `if (!cur.has(4)) { /* error */ } else { ...; cur.advance(4); }`.
  Because the cursor knows `end_`, there is no "trust 4 bytes exist."
- **C1** — `url_normalize_path` takes a `Span<char> out` instead of a bare `char*` with a literal `2047`; every
  write goes through `out[i]` / `out.subspan(...)`, bounded by the caller's *actual* size. The "callee bound ≠
  caller buffer" mismatch cannot be expressed.
- **H5** — the `href_buf` reconstruction uses a `Span<char>` write cursor that clamps instead of `pos += snprintf-return`.

This also folds in a hardening of the existing `StrView`: add the same checked `operator[]`/`get()` so the
two converge (`StrView` = `Span<const char>` with string helpers).

### 3.3 New facility B — checked allocation (`lib/checked_alloc.hpp`)

**Closes pattern A (H2, H3, H4, M5).** Rust makes you handle the allocation `Result`; C++17 gives us
`[[nodiscard]]` to make ignoring the null a compiler warning, and a combined overflow+null wrapper so the
two failure modes are handled in one place.

```cpp
// lib/checked_alloc.hpp
namespace lam {

// n*sz with overflow check, then pool_calloc; returns nullptr on overflow OR OOM.
// [[nodiscard]] => the caller is warned if it derefs without checking.
template<class T>
[[nodiscard]] inline T* checked_pool_array(Pool* p, size_t n, size_t sz = sizeof(T)) {
    size_t total;
    if (!checked_mul(n, sz, &total)) return nullptr;   // §3.4
    return (T*)pool_calloc(p, total);
}

// realloc that never leaks the original on failure and never returns an unchecked buffer.
template<class T>
[[nodiscard]] inline bool checked_realloc(T** slot, size_t n, size_t sz = sizeof(T)) {
    size_t total;
    if (!checked_mul(n, sz, &total)) return false;
    T* fresh = (T*)realloc(*slot, total);
    if (!fresh) return false;                          // *slot still valid → caller can bail
    *slot = fresh;
    return true;
}

} // namespace lam
```

Fixes: **H2** (`heap_strcpy` returns/branches on null instead of `memcpy`ing through it), **H3**
(`dl_alloc_item` / grid / flex use `checked_realloc` and bail on false before the indexed write),
**H4/M5** (every `length*elem_size` allocation routes through `checked_pool_array`). Enforced by a new
`make check-alloc` lint that flags a raw `pool_calloc`/`malloc`/`realloc` result dereferenced before a null
test (same mechanism as `make check-int-cast`).

> **`checked_alloc` is only *one half* of the allocation-result story — the *fallible* half.** The other
> half is the **infallible arena**, whose result is non-null by construction and needs no per-call check.
> Both halves, and the Memory-Context-driven OOM strategy that backs the infallible guarantee, are
> specified together in **§3.7**.

### 3.4 New facility C — checked integer arithmetic (`lib/checked_math.hpp`)

**Closes pattern D (H3, H4, M2, M3 + the arraylist/grid/pdf latents).** Rust's `checked_mul`/`checked_add`
return `None` on overflow; the C+ form returns `bool` and writes through an out-param, using compiler builtins
so it's branch-cheap.

```cpp
// lib/checked_math.hpp
namespace lam {
inline bool checked_mul(size_t a, size_t b, size_t* out) {
    return !__builtin_mul_overflow(a, b, out);     // gcc/clang; MSVC: _umul128 path
}
inline bool checked_add(size_t a, size_t b, size_t* out) {
    return !__builtin_add_overflow(a, b, out);
}
// narrowing guard for the int64 -> int / capacity-doubling cases (M2, arraylist.c:85)
template<class To, class From>
inline bool checked_narrow(From v, To* out) {
    *out = (To)v;
    return (From)*out == v && (v < 0) == (*out < 0);
}
} // namespace lam
```

Paired with a mechanical rule: **size/count/capacity fields that feed an allocation must be `size_t`, not
`int`.** `arraylist.c`'s `_alloced`/`length`, the grid track counts, and the display-list `new_cap` migrate to
`size_t` + `checked_mul`. `checked_narrow` covers the unavoidable `int64 length → int` sites (M2, `mark_builder.cpp:852`).

### 3.5 New facility D — recursion guard (`lib/recursion_guard.hpp`)

**Closes C2 and the CSS-parser gap.** RAII is explicitly sanctioned by the convention (§1). One stack-scoped
guard per recursive-descent / AST-walk entry, returning an error past a limit — the same discipline JSON/XML/
Mark/TOML already hand-roll, factored into one reusable type.

```cpp
// lib/recursion_guard.hpp
namespace lam {
struct RecursionGuard {
    int* depth_;
    bool ok_;
    RecursionGuard(int* depth, int limit) : depth_(depth), ok_(++(*depth) <= limit) {}
    ~RecursionGuard() { --(*depth_); }
    explicit operator bool() const { return ok_; }   // false => too deep, bail with an error
};
} // namespace lam
// usage in build_expr / css_parse_block:
//   RecursionGuard g(&ctx->depth, MAX_PARSE_DEPTH);
//   if (!g) return parse_error(ctx, ERR_RECURSION_LIMIT);
```

For **C2** specifically, this is the *root-cause* fix (cap the recursion). It should be paired with the
*defense-in-depth* fix of arming `_lambda_recovery_point` before AST build, not only before JIT execution, so
a runaway recursion is caught even if a depth guard is missed.

### 3.6 Closing C3 — apply the *existing* ownership templates (+ a generational handle)

C3 needs no new ownership theory — [`Memory_Safety_Template.md`](./Memory_Safety_Template.md) §1 already
built the tools. Two complementary layers:

**Compile-time half (already in the tree):** make `AnimationInstance::target` a `PersistentField`, not a raw
`void*`. The animation scheduler is long-lived (`PoolDomain`); a `View` is `LayoutSessionDomain`. Since
`DomainOutlives<LayoutSessionDomain, PoolDomain>` is undefined → `FalseType`, the `set()` overload is
`= delete`, so **storing the raw view pointer becomes a compile error**:

```cpp
struct AnimationInstance {
    lam::PersistentField<View, lam::PoolDomain> target;   // was: void* target
};
// anim->target.set(view_borrow);   // ❌ compile error: session-domain View can't outlive a pool-domain field
```

**Runtime half (the new bit the animation feature actually needs):** a transition legitimately *wants* to
reference a view that gets rebuilt each relayout. The Rust answer is a generational handle / `Weak`. Extend
`ownership.hpp` with a `Handle<T>` keyed by pool slot index + generation; the scheduler stores the handle, and
each tick resolves it — getting `nullptr` (not a dangling pointer) if the view was freed or rebuilt:

```cpp
// lib/ownership.hpp (extension)
template<class T>
struct Handle { uint32_t index; uint32_t generation; };   // 8 bytes, trivially copyable

// view_pool bumps the slot's generation on free/reuse; resolve fails closed.
template<class T> T* view_pool_resolve(ViewPool* pool, Handle<T> h);   // nullptr if stale
```

`AnimationInstance` stores `Handle<View>`; the tick does `View* v = view_pool_resolve(pool, anim->target);
if (!v) { retire_animation(anim); continue; }`. This is the structural fix for the *entire* class C3 belongs
to (cursor, drag state, `state_map` all hold raw `View*` across relayout). The existing per-site pointer-scrubbing
at `event.cpp:3606` becomes unnecessary for any subsystem migrated to handles.

### Per-finding fix map

| Finding | Facility | Action |
|---|---|---|
| C1 | `Span`/`ByteCursor` (3.2) | `url_normalize_path(Span<char> out, ...)`; bound every write to `out.size()` |
| C2 | `RecursionGuard` (3.5) + recovery-point reorder | depth-cap `build_expr`; arm `_lambda_recovery_point` before AST build |
| C3 | `PersistentField` (3.6) + `Handle<T>` | `AnimationInstance::target` → `PersistentField`/`Handle<View>`; resolve-or-retire each tick |
| H1 / KV | `ByteCursor` (3.2) | replace unconditional `(*mark)+=4` with `has(4)` guard (shared `parse_unicode_escape`) |
| H2 | `checked_alloc` (3.3) | `heap_strcpy` checks the alloc, returns sentinel on failure |
| H3 | `checked_realloc` (3.3) | `dl_alloc_item`/grid/flex bail on `false` before the write |
| H4 | `checked_alloc` + `checked_mul` (3.3/3.4) | `str_repeat`/`array_num_new`/`expand_list` route size math through the guard |
| H5 | `Span` write cursor (3.2) | `href_buf` reconstruction clamps instead of `pos += snprintf` |
| M1 | `Option`/`T&` discipline | guard `block->parent && block->parent->is_block()` (match the rest of the file) |
| M2 | `checked_narrow` (3.4) | `mark_builder.cpp:852` uses int64 loop counter / narrows safely |
| M3 | `checked_mul` (3.4) | grid auto-repeat track count overflow-checked before `mem_calloc` |
| M4 | `Span` + clamp | clamp `col_index` at assignment; size `col_*` arrays via `Span` |
| M5 | `checked_alloc` (3.3) | mmap-chunk `malloc` + datetime `pool_calloc` null-checked |
| arraylist/pdf/CSS latents | `checked_math` + `RecursionGuard` | `size_t` capacity; CSS depth cap |

---

### 3.7 OOM handling and allocation-result types: `NonNull` (infallible arena) vs `[[nodiscard]]` (fallible pool)

> Cross-reference: [`Memory_Context.md`](./Memory_Context.md) — Stage 1 (allocator factory, landed) and
> Stage 2 (§15, centralized page allocation + memory-pressure handling, not started). This section defines
> the **type-level contract** that sits on top of that runtime machinery.

Pattern A ("alloc *can* return NULL, but the hot path writes through it") is not best fixed by sprinkling
null checks at thousands of call sites — that is exactly the "tedious and unstructured" approach the codebase
wants to avoid. The structural fix is to recognize that Lambda/Radiant has **two fundamentally different
allocators**, give each a **distinct return type**, and enforce the failure-handling obligation through the
type rather than through discipline.

#### 3.7.1 Two allocator types → two return types

| | **Type 1 — fallible** | **Type 2 — infallible** |
|---|---|---|
| Allocators | `Pool` (rpmalloc/mmap), explicit large/mmap requests, `realloc` growth | `Arena` (bump), unit allocations within a chunk |
| Failure mode | returns `NULL` to the caller | **never returns to the caller on failure** — it reclaims, retries, then parks or aborts (§3.7.3) |
| Return type | `[[nodiscard]] T*` / `MaybeNull<T>` | `NonNull<T>` |
| Caller obligation | **must** null-check (enforced by `[[nodiscard]]` + `make check-alloc`) | **none** — the value cannot be null |
| Backed by | the caller handling the `NULL` | the Memory Context's chunk-level OOM loop |
| Implementation | §3.3 `checked_alloc.hpp` | this section |

This mirrors Rust precisely: the global allocator is *infallible at the call site* (allocation failure aborts,
so `Box::new` always yields a value), while `try_reserve` is *fallible* (returns `Result`). The split is not a
workaround — it is the honest description of how the two allocators already behave, made visible in the type.

#### 3.7.2 Why `NonNull<T>`, not a bare C++ reference

A non-null **reference** (`T&`) expresses "never null" in the language, but it is the wrong vehicle here:

1. **The dominant arena shape is `sizeof(Header)+payload` cast from `void*`** (flexible-array structs —
   `arena_alloc(a, sizeof(String)+len+1)` at `mark_builder.cpp:128`). A `String&` cannot describe header+payload.
2. **References can't be stored in fields or reseated.** The codebase keeps allocation results in struct
   fields everywhere (`Item* items`, `char* chars`); a `T&` member makes a struct non-assignable.
3. **Arrays / raw byte buffers** have no single `T` to reference.
4. **The C/MIR ABI cannot see references.** `arena_alloc` lives in `arena.h` (C header, MIR-visible) and must
   stay `void*`.

A `NonNull<T>` wrapper sidesteps all four: pointer-sized, storable, array-capable, and it **decays to a raw
`T*` at the ABI edge** — while still making "never null" a compile-time, type-level fact. A reference is kept
only as *call-site sugar* (`arena_emplace<T>`) for the plain single-object case, built on the wrapper.

```cpp
// lib/ownership.hpp (extension) — pointer-sized, trivially convertible to T*, zero-cost.
template<class T>
struct NonNull {
    T* p_;
    explicit NonNull(T* p) : p_(p) { assert(p && "NonNull constructed from null"); } // debug-only
    T* get() const { return p_; }            // explicit decay at the C/MIR boundary
    T* operator->() const { return p_; }
    T& operator*()  const { return *p_; }
    operator BorrowedPtr<T, PoolDomain>() const { return BorrowedPtr<T, PoolDomain>(p_); }
};

// Type 2 — infallible arena. Declared in a .hpp; the C ABI `void* arena_alloc(...)` is untouched.
template<class T> NonNull<T> arena_new(Arena& a) {                       // single object
    return NonNull<T>((T*)arena_calloc(&a, sizeof(T)));
}
template<class T> NonNull<T> arena_new_sized(Arena& a, size_t extra) {   // header + payload (FAM)
    return NonNull<T>((T*)arena_alloc(&a, sizeof(T) + extra));
}
inline NonNull<void> arena_bytes(Arena& a, size_t n) { return NonNull<void>(arena_alloc(&a, n)); }
template<class T> T& arena_emplace(Arena& a) { return *arena_new<T>(a).get(); }  // reference sugar

// Type 1 — fallible pool. Caller must check (== §3.3 checked_alloc).
template<class T> [[nodiscard]] T* pool_try_new(Pool* p) { return (T*)pool_calloc(p, sizeof(T)); }
```

Call sites collapse to their essential logic — the `String` allocation at `mark_builder.cpp:128` no longer
needs (and structurally cannot forget) a null guard:

```cpp
NonNull<String> s = arena_new_sized<String>(*arena_, len + 1);   // cannot be null, by type
memcpy(s->chars, src, len);
```

#### 3.7.3 The OOM handling strategy — *why* the infallible type is honest

`NonNull<T>` is only truthful because the arena **upholds the promise at one place — chunk acquisition —
instead of at every `arena_alloc`.** Unit allocations within a live chunk are pure pointer bumps that cannot
fail; the *only* failure point is acquiring a new chunk from the backing pool. That single choke point is
where the Memory Context's OOM policy lives. This is the structural reason per-call checks are unnecessary:
**failure is impossible to observe below the chunk boundary.**

The escalation ladder at the chunk boundary (Memory_Context.md §15.4–15.6, Stage 2):

```
arena needs a new chunk
        │
        ▼
  pool_alloc(backing, chunk_size)
        │  success → return chunk ───────────────► unit allocations proceed (NonNull honored)
        │
        │  NULL (backing pool / OS refused)
        ▼
  mem_context_reclaim(ctx, needed_bytes)        // §15.3 reclaimer registry, cost-ordered
        │   ├─ drop evictable caches (font glyph, image pixels, ThorVG paint)   [Level 1]
        │   ├─ compact / reset idle scratch arenas                              [Level 2]
        │   ├─ force a GC cycle (nursery + gc_heap)                             [Level 3]
        │   └─ retire finished per-document sub-contexts                        [Level 4]
        │
        ▼  retry pool_alloc
        │  success → return chunk ───────────────► proceed
        │
        │  still NULL after full reclamation → genuine memory pressure
        ▼
  per-thread back-pressure policy (§15.6):
        ├─ PARK   : block this thread until a watermark frees up, then retry    (default for request threads)
        ├─ FAIL   : convert to the fallible path — surface OOM as a Lambda error up the call stack
        └─ ABORT  : abort the requesting thread/process (last resort; logged with a snapshot)
```

Key properties this buys, tied back to the user's design intent:

- **Unit allocations assume success** — and that assumption is *enforced*, not hoped for: the only code that
  can ever see a NULL chunk is `mem_context_reclaim`'s caller, never application code.
- **"Unreasonable amount of memory" is handled here too**, not at the call site. A request that no amount of
  reclamation can satisfy hits the FAIL/ABORT rung — a single, centralized, *observable* decision (it logs a
  `--mem-dump` snapshot so the offending allocator/document is attributable via the doc-URL registry, §10.4).
- **The policy is per-thread and uniform.** A request/worker thread parks; a batch tool may prefer FAIL so the
  CLI exits cleanly; an internal invariant violation aborts. One place sets this, not 40 call sites.

#### 3.7.4 Incremental path — make the type honest *today*, upgrade the policy *later*

The `NonNull` return type must not lie. Before Stage 2 exists, a "Type 2" arena could still return NULL on
chunk failure — so the first, cheapest step is to make the infallible arena **truly infallible now** with a
single abort, then upgrade *only that one function body* when Stage 2 lands. **No `NonNull`-returning
signature ever changes.**

```c
// lib/arena.c — Stage 1 (now): the non-null promise becomes real at one site.
static void* arena_acquire_chunk(Arena* a, size_t size) {
    void* chunk = pool_alloc(a->pool, size);
    if (!chunk) {
        log_error("ARENA-OOM: chunk alloc failed (%zu bytes) for arena '%s' — aborting",
                  size, arena_label(a));
        abort();                       // Stage 1: honest, blunt.
    }
    return chunk;
}
// Stage 2 replaces the body of the `if (!chunk)` branch with:
//   chunk = mem_context_reclaim_retry(arena_context(a), size);   // §15.5 loop
//   if (!chunk) mem_context_apply_oom_policy(current_thread());  // PARK / FAIL / ABORT
```

This sequencing means the type-safety work (3.7.1–3.7.2) and the policy work (Stage 2) are **decoupled**: the
type contract can land and be relied upon immediately; the sophistication behind it grows underneath without
touching a single consumer.

#### 3.7.5 Honest caveats

- **The non-null type is a promise the allocator keeps.** It is honest only after `arena_acquire_chunk`
  aborts-or-reclaims (3.7.4). A "Type 2" allocator that can still silently return NULL makes `NonNull` lying —
  so the abort lands *first*, before any consumer relies on the type.
- **Non-null ≠ non-dangling.** `NonNull<T>` says "this pointer was not null at construction." It says nothing
  about lifetime — `arena_reset` invalidates outstanding `NonNull`s exactly like raw pointers. Lifetime is the
  domain/`Handle` axis's job (§3.6, §4.1), orthogonal to this one.
- **Decays to raw at the C/MIR boundary.** The guarantee holds C++-side only; an `extern "C"` signature still
  passes a bare `void*`/`T*`. `arena_realloc` and `arena_alloc_aligned` need the same wrapper treatment.
- **FAIL-policy reintroduces the fallible path deliberately.** When a thread's policy is FAIL, the OOM surfaces
  as a Lambda error (`ItemError`) up the normal error channel — i.e. the *infallible* arena can, by explicit
  per-thread policy, behave fallibly at the *chunk* boundary while unit allocations below it still never see
  NULL. The two-type model is about where the check lives, not about forbidding failure to ever propagate.

---

## Part 4 — Template Extensions & New Designs

### 4.1 Extensions to the existing templates

1. **`ownership.hpp` → add `Handle<T>` + `ViewPool` generation counters** (§3.6). This is the runtime
   complement to the existing compile-time `PersistentField`: `PersistentField` *forbids* the dangling store;
   `Handle` *provides the safe alternative* the forbidden code actually needed. Both reference
   `DomainOutlives`, so the rule still lives in one place. Resolves Open Question Q4/Q5 from the Radiant
   template for the registry case.

2. **`span.hpp` is the missing fourth pillar.** The two existing docs cover *ownership* (who frees) and *tag*
   (what type); they have no *bounds* (how big) pillar. `Span<T>` / `ByteCursor` fills it and should be cited
   as a peer facility in both template docs. `StrView` becomes `Span<const char>` + string helpers.

3. **Promote `ItemOrError` from "future work" to landed.** Template2 §8.3 and the 2026-05-28 "Next Phase" note
   already plan a `[[nodiscard]]` result wrapper around `RetMap`/`RetArray`. Combined with `checked_alloc`'s
   `[[nodiscard]]`, this gives Lambda a uniform "you must handle the failure" story at every fallible boundary
   — the C+ analogue of `Result<T,E>` + `?`.

3a. **`ownership.hpp` → add `NonNull<T>` + infallible-arena helpers** (§3.7). The dual of extension #3: where
   `ItemOrError`/`checked_alloc` cover the *fallible* allocators, `NonNull<T>` covers the *infallible* arena,
   so allocation-failure handling is expressed entirely in return types. Its honesty depends on the
   Memory-Context OOM loop ([`Memory_Context.md`](./Memory_Context.md) Stage 2); the abort-on-chunk-failure
   step (§3.7.4) lands it incrementally before Stage 2.

4. **Extend the `make check-*` lint family.** Today: `check-int-cast`, `check-item-cast`, `check-unsafe-casts`
   (proposed). Add `check-alloc` (unchecked alloc deref), `check-span` (raw `ptr+len` indexing outside `lib/`
   in `lambda/input/`), and `check-recursion` (recursive-descent entry lacking a `RecursionGuard`). The
   precedent and tooling already exist.

### 4.2 New template designs proposed

5. **Strong-typedef newtypes for unit-bearing integers** (`ByteLen`, `CharIdx`, `ColIndex`). A 4-line wrapper
   struct prevents mixing byte-lengths with char-counts and column-indices with column-counts (the M4 family),
   the same way Rust newtypes do. Zero-cost; rejects the wrong unit at compile time.

6. **`Bounded<T, N>` fixed-capacity buffer** — a stack array that knows its capacity and refuses overflowing
   writes (returns `bool`). Replaces the bare `char buf[256]` / `char decoded[8]` patterns
   (html5 entity buffer, tag-name buffers) with a type that makes the HTML-entity latent overflow a
   compile-time `static_assert` on `N` plus a runtime guard.

7. **`Reader<T>` / typed `ByteCursor` adapters per format** — a thin per-parser wrapper over `ByteCursor` that
   exposes format-aware bounded primitives (`read_hex4`, `read_until`, `expect`). Centralizes the
   audited-once unicode/escape/number decode so the H1 class can't recur in a new parser (the bug existed
   precisely because the safe pattern was copy-paste, not shared).

8. **Fuzz + sanitizer harness as a *standing* facility, not a one-off.** Templates give compile-time
   guarantees for the *wrapped* subset; everything still-raw needs runtime coverage. Add an ASan+UBSan build
   variant over the existing GTest + `test/lambda/*.ls` + `make layout` corpus, plus a libFuzzer target per
   `lambda/input/` format. This is what catches the bugs in the code that *hasn't* been migrated yet, and it
   converts this audit from a snapshot into continuous coverage. (C1, H1, H2, C3 would all surface immediately.)

### 4.3 What the templates still cannot do (honest limits)

- These are **opt-in lints, not a borrow checker** — they protect exactly the call sites migrated. The
  still-raw majority needs §4.2.8 (fuzz+ASan).
- **They stop at the ABI/MIR edge.** A `Span`, `Handle`, or `PersistentField` decays to a raw pointer in any
  `extern "C"` signature; safety holds on the C++ side only. This is inherent to the MIR-only-speaks-C constraint.
- **`Handle` resolution is a runtime check**, so it trades a UAF for a `nullptr` branch (and a per-pool
  generation counter). That is the intended trade — fail-closed beats use-after-free — but it is not free, and
  it relies on `view_pool` correctly bumping generations on every free/reuse.
- The **domain lattice is hand-curated**; an unmodeled domain pair fails closed (over-rejects), which can tempt
  `unsafe_borrow_raw`. Keep `check-unsafe-casts` honest.

---

## Part 5 — Phasing

Ordered by **leverage per unit effort** (highest first), independent of the in-flight Radiant/Lambda template rollout:

| Phase | Work | Closes | Risk |
|---|---|---|---|
| **0** ✅ | Land `lib/span.hpp`, `lib/checked_alloc.hpp`, `lib/checked_math.hpp`, `lib/recursion_guard.hpp`, `NonNull<T>`+arena helpers in `ownership.hpp` + GTests. Header-only, no production changes. | — | None |
| **1** | Wire `ByteCursor` through `lambda/input/` `\u`/escape paths and `url_parser.c`. | **C1, H1, H5** | Low — localized |
| **2** | Route allocation + size math in `lambda-mem.cpp`, `lambda-data*.cpp`, `display_list_storage.cpp`, `layout_grid/flex.cpp` through `checked_alloc`/`checked_mul`. Add `make check-alloc`. | **H2, H3, H4, M2, M3, M5** | Low–Med |
| **3** | `RecursionGuard` in `build_expr` + CSS parser; reorder `_lambda_recovery_point` arming. | **C2** | Low |
| **4** | Add `Handle<T>` + view-pool generations; migrate `AnimationInstance::target` (and cursor/drag/`state_map`) off raw `View*`; make the field a `PersistentField`. | **C3** | Med — touches relayout |
| **5** | Sweep M1/M4 null-guards + clamps; `size_t` capacity migration in `arraylist.c`. | M1, M4, latents | Low |
| **6** | Stand up ASan+UBSan CI variant + per-format libFuzzer over `lambda/input/`. | regression net | Low |

Phases 0–3 close every Critical and High finding with low blast radius and no ABI change. Phase 4 reuses
machinery already validated on the Lambda side. Phase 6 is the durable backstop for everything not yet wrapped.

### Implementation Status (2026-06-04)

**Phase 0 — landed and verified.** Header-only, no production call sites changed.

- `lib/checked_math.hpp` — `checked_mul` / `checked_add` / `checked_mul_add` (builtin-overflow), `checked_narrow`.
- `lib/span.hpp` — `Span<T>` (bounds-checked `operator[]`, non-aborting `get()`, clamped `subspan`, range-for, `unchecked()`); `ByteCursor` (`has`/`peek`/`advance`/`take`, peek-past-end returns 0).
- `lib/checked_alloc.hpp` — `[[nodiscard]]` `checked_pool_array` / `checked_pool_sized` / `checked_realloc` (preserves `*slot` on failure) / `checked_malloc`.
- `lib/recursion_guard.hpp` — `RecursionGuard` RAII (balanced depth counter on every exit).
- `lib/ownership.hpp` (extended) — `NonNull<T>` (debug null-assert; `operator*`/borrow-conversion as member templates so `NonNull<void>` stays well-formed) + `arena_new` / `arena_new_sized` / `arena_bytes` / `arena_emplace` / `pool_try_new`.
- Tests: `test/test_span_gtest.cpp` (9, incl. OOB-abort death test + truncated-`\u` H1 regression), `test/test_checked_alloc_gtest.cpp` (9, incl. null-`NonNull` death test), `test/test_recursion_guard_gtest.cpp` (3); registered in `build_lambda_config.json`. **21/21 pass.** Existing `test_own_tagged` (6/6) and `test_lambda_typed` (9/9) still pass; `lambda` debug target builds clean.

**Phase 1 — landed and verified.** Root-cause fixes at the untrusted-input boundary (no `Span`/`ByteCursor`
retrofit into the NUL-terminated `.c`/`.cpp` parsers — the equivalent bounds discipline was applied directly,
matching each parser's existing idiom).

- **H1 (Mark `\u`)** — `lambda/input/input-mark.cpp`: rewrote the `\u` handler to *peek-validate-then-advance*
  with short-circuit hex checks, so a truncated `"\u"`/`"\uAB"` at end-of-input neither reads nor advances
  past the buffer. Also corrects a latent off-by-one that consumed 7 chars for a 6-char escape (dropped the
  following character / could overshoot the NUL).
- **H1 (KV trailing `\`)** — `lambda/input/input-utils.hpp`: added an entry guard to the shared
  `parse_escape_char` (`if (!c) return 0;`), so a trailing backslash at end-of-input can no longer hit the
  `default` branch that appended the NUL and advanced past it. Fixes the `input-kv.cpp` caller and hardens all
  others at one point.
- **C1 (URL path overflow)** — `lib/url_parser.c` + `lib/url.h`: `url_normalize_path` now takes a `size_t cap`
  and bounds every write to it (the collapse-to-root case no longer does `strncpy(path,"/",2047)`); both call
  sites pass their real buffer size.
- **H5 (href snprintf cursor)** — `lib/url_parser.c`: replaced the repeated `pos += snprintf(...)` pattern
  with a clamping `href_appendf` helper that keeps `pos` within `[0, cap)` on truncation, so `href_buf+pos`
  and `cap-pos` can never go out of bounds.
- Regression tests: `test/lib/test_url_extra_gtest.cpp` +3 (`UrlSafetyRegression` — buffer-canary on C1
  collapse + capacity, H5 long-path clamp). Verified: `test_url_gtest` 39/39, `test_url_extra_gtest` 30/30,
  `test_mark_reader_gtest` 41/41, `test_input_roundtrip_gtest` 38/38; **Lambda baseline 2942/2942**.

**Phase 2 — landed and verified.** Allocation + size-math hardening across core runtime, lib, and radiant.
Every fix changes only the *failure* path (overflow / OOM); the success path is byte-identical, so no
behavior change for valid inputs.

- **H2 (`heap_strcpy`)** — `lambda/lambda-mem.cpp`: guard negative length + size overflow of `heap_alloc`'s
  `int` param; null-check the result instead of `memcpy`ing through NULL.
- **H4 (`str_repeat`)** — `lambda/lambda-eval.cpp`: `checked_mul` on `str_len*times`, size bound, null-check.
- **H4 (array factories)** — `lambda/lambda-data-runtime.cpp`: `array_fill`, `array_int_fill`,
  `array_int64_fill`, `array_float_fill`, `array_num_new` now `checked_mul` the element-count size and only
  set length/capacity + fill when the allocation succeeds (array stays empty on overflow/OOM; bounds-checked
  getters return null).
- **H4/H5 (`expand_list`)** — `lambda/lambda-data.cpp`: `checked_mul` the new buffer size, null-check
  `heap_data_alloc`, and revert the capacity doubling on failure so the list never publishes a NULL/short
  buffer.
- **M2 (deep-copy length)** — `lambda/mark_builder.cpp`: `int64_t` length + loop counter (`arr->length` is
  `int64_t`; `ArrayReader::get` takes `int64_t`) — no truncation.
- **M3 (grid auto-repeat)** — `radiant/layout_grid.cpp` (cols + rows): compute the expanded track count in
  64-bit and bail if it doesn't fit `int`, closing the overflow→undersized-`mem_calloc`→OOB-copy vector.
- **H3 (display list)** — `radiant/display_list_storage.cpp` `dl_alloc_item`: detect `int` capacity-doubling
  overflow, `checked_mul` the realloc size, and null-check the realloc (leaving the old buffer intact on OOM).
- **M5 (lib)** — `lib/mempool.c`: null-check the mmap-chunk `malloc` and `munmap` the mapping on failure;
  `lib/datetime.c`: null-check the two `pool_calloc`+`strncpy` datetime-literal sites.
- New header consumers: `checked_math.hpp` now included by `lambda-eval.cpp`, `lambda-data.cpp`,
  `lambda-data-runtime.cpp`, `display_list_storage.cpp`.
- Verified: **Lambda baseline 2942/2942**; deterministic radiant suites `test_ui_automation` 230,
  `test_page_load` 104, `test_radiant_view` 19, `test_fuzzy_crash` 24 — all 0 failures.

**Deferred from Phase 2 (follow-up):** the remaining *realloc-null-checks* in `grid_item_array_realloc`
(`layout_grid.cpp`) and `ensure_flex_items_capacity` / line-array growth (`layout_flex.cpp`). These are
OOM-only (not overflow→corruption) and need per-caller bail/clamp logic; deferred because the Radiant
baseline is pre-existingly flaky (see Phase 1 note), making clean verification of caller-contract changes
harder. The overflow→heap-overflow vectors and the clean display-list site are done.

**Not yet started:** Phase 3 (`RecursionGuard` wiring + recovery-point reorder), Phases 4–6, and the
`make check-alloc` / `check-span` / `check-recursion` lint targets.

---

## Appendix — Relationship to the existing two templates

| Concern | Owner doc |
|---|---|
| Temporal ownership / domains / `PersistentField` / promotion | [`Memory_Safety_Template.md`](./Memory_Safety_Template.md) §1 |
| Typed ownership lists (`ArrayOwnedList`/`PersistentList`) | [`Memory_Safety_Template.md`](./Memory_Safety_Template.md) §2 |
| Tag-confusion / `ItemOf<Tag>` / `visit()` / group traits | [`Memory_Safety_Template2.md`](./Memory_Safety_Template2.md) |
| **Bounds / spans / cursors** | **this doc §3.2** (new pillar) |
| **Allocation-failure safety (fallible pool)** | **this doc §3.3** (new) |
| **Allocation-result types + OOM handling (`NonNull` vs `[[nodiscard]]`)** | **this doc §3.7** (new); OOM loop = [`Memory_Context.md`](./Memory_Context.md) Stage 2 |
| **Integer-overflow safety** | **this doc §3.4** (new) |
| **Recursion-depth safety** | **this doc §3.5** (new) |
| Runtime UAF handles (`Handle<T>`) — complements `PersistentField` | **this doc §3.6 / §4.1** (extends Template §1) |

The three documents together cover the axes of memory safety: **who frees it** (ownership templates),
**what type it is** (tag templates), **how big it is** (spans, this doc), **does it still exist**
(domains + handles, shared), and **does allocation succeed** (`NonNull` vs `[[nodiscard]]` + the
Memory-Context OOM loop, this doc §3.7). The existing work is mature on the first two; this proposal supplies
the rest. The allocation-success axis is unique in that its *type contract* lives here while its *runtime
enforcement* lives in [`Memory_Context.md`](./Memory_Context.md) — the two docs meet at the arena chunk boundary.
