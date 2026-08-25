# Radiant Font Cache and Handle Ownership

**Status:** implemented  
**Authority:** `doc/Lambda_Formal_Design.md` D4.5.1v3

This note defines how Radiant keeps loaded native font faces alive while
preserving a bounded, replaceable face cache. It applies D4.5.1v3's Radiant
seam contracts—**pin, gen-check, copy-as-value**—to `FontHandle` ownership.
It does not replace the formal design ruling.

## Goal

`FontContext` owns the font-cache policy. A persistent Radiant style does not
own or manually release a native font handle: it holds a cache-managed alias
lease. The lease keeps a handle valid if its cache key is replaced, evicted,
or removed during a document reset.

This solves the stale-handle failure mode where a `FontBox`, line-layout state,
or copied `FontProp` continued to use a handle after another style resolution
released or replaced the cache mapping.

## Terms

| Term | Meaning |
| --- | --- |
| `FontHandle` | Ref-counted opened, sized native face plus cached metrics and glyph state. |
| Face-cache mapping | The `family:weight:slant:size` key mapped to a `FontHandle`; the mapping owns one reference. |
| Resolver reference | The one reference returned by `font_resolve()`; a short-lived direct caller releases it, or `setup_font()` transfers it into an alias lease. |
| Alias lease (pin) | A reference held through the font-cache API for one persistent `FontProp`; counted by `FontHandle::cache_alias_count`. |
| `FontProp` | The persistent CSS/style object that aliases a handle through an alias lease. |
| `FontBox` | Transient layout state. It points to a `FontProp` and derives its handle from that style; it never owns or duplicates a handle. |
| Direct client | A caller such as a resolver/fallback path that needs a temporary handle outside persistent style state. |

## Ownership model

```text
                         cache-map reference
face-cache key  ------------------------------> FontHandle
                                                    ^
                                                    |
                                  cache-managed alias lease
                                                    |
FontBox ----> FontProp ----------------------------+
                 ^
                 |
       copied FontProp has its own alias lease
```

The font cache is the policy owner for persistent aliases. Native reference
counting still has distinct, explicit holders:

- A face-cache mapping retains its `FontHandle`.
- Each persistent `FontProp` has one cache-managed alias lease.
- A direct resolver client temporarily owns the reference returned by
  `font_resolve()`.

No `FontProp`, `FontBox`, or line-layout field performs direct
`font_handle_retain()` / `font_handle_release()` ownership management.

## Invariants

- A native face remains usable while any cache mapping, alias lease, or direct
  resolver reference exists.
- A persistent `FontProp` either has no handle or exactly one alias lease for
  `font_handle`.
- Copying a `FontProp` creates another lease; byte-copying a raw handle without
  a lease is invalid.
- `FontBox::style->font_handle` is the sole current handle path for a
  `FontBox`; `FontBox` has no independent raw-handle snapshot.
- A cache-key replacement or deletion releases only that mapping's reference.
  It never invalidates handles still leased by active styles.
- LRU eviction may remove only entries with `cache_alias_count == 0`.
- The face-cache capacity is a target while all eviction candidates are pinned:
  correctness takes precedence over evicting a live entry.

## Cache-managed alias API

The ownership boundary is `lib/font/font.h`.

| API | Contract |
| --- | --- |
| `font_cache_adopt_handle_alias(handle)` | Consumes the resolver reference already returned by `font_resolve()` and records it as one persistent alias lease. It does not retain again. |
| `font_cache_pin_handle(handle)` | Retains `handle` and records a new alias lease. Use when a `FontProp` copy starts aliasing an existing handle. |
| `font_cache_unpin_handle(handle)` | Removes one alias lease and releases its retained reference. It diagnoses an underflow. |

`font_prop_release_handle()` is the sole persistent-style release path: it
unpins the current handle through the cache API and then clears the pointer.

## Resolution and binding

`setup_font()` is a binding operation, not a font-cache owner. Its sequence is:

1. Point the transient `FontBox` at the supplied `FontProp`.
2. Reuse the existing alias when its resolved family, used size, weight, and
   slant still match.
3. Otherwise unpin the previous `FontProp` alias.
4. Resolve a handle through `font_resolve()`.
5. Store it in `FontProp` and transfer the returned resolver reference with
   `font_cache_adopt_handle_alias()`.
6. Populate derived metrics from the resolved handle.

The cache, resolver, and face selection own loading and lifetime decisions.
`setup_font()` must not add an independent raw release before resolving a new
face.

## Copy and teardown

`font_prop_copy(destination, source)` first unpins `destination`'s old handle,
copies the property value, then pins the copied handle. This implements
D4.5.1v3's copy-as-value requirement for the handle alias.

When a persistent property dies or is rebound,
`font_prop_release_handle()` unpins it and nulls the handle. A `FontBox` needs
no teardown because it owns no handle. Line layout keeps `FontProp*` for its
parent-font and previous-kerning state, then derives handles only at the point
of metric or shaping use.

## Replacement, LRU, and document reset

### Cache-key replacement

Inserting a new handle for an existing cache key drops the replaced mapping's
reference. Active `FontProp` leases retain the old handle, so an in-flight
layout or render remains valid. Future lookups use the new mapping; the old
handle closes only after its last lease/direct reference is released.

### Bounded LRU

The cache scans for the least-recently-used entry whose
`cache_alias_count == 0`. If every candidate is leased, it defers eviction
rather than freeing a live native face. The map may therefore temporarily
exceed its configured target; `font_cache_trim()` stops when no unpinned entry
can make progress. Once aliases drain, normal insertion or trimming can evict
the entry.

### Document boundary

`font_context_reset_document_fonts()` clears the previous document's
`@font-face` descriptors and codepoint/platform fallback state, then removes
all document-font mappings from the face cache. System-font mappings remain
available for reuse across batch documents.

The reset does not retain obsolete document mappings merely because a style is
still leased. The lease already protects the old handle, while removing the
mapping ensures a new document cannot resolve through a prior document's
`@font-face`. Once the old document's styles drain, their leases release the
native face normally.

## Face selection and cache keys

The face-cache key contains the requested family, weight, slant, and CSS-pixel
size. Size uses `%.9g`, preserving sufficient float precision for computed CSS
sizes such as percentage-derived values; coarse rounding would incorrectly
merge distinct sized faces and their metrics.

Before accepting a same-family system cache entry, resolution checks for a
matching document `@font-face`. A document face shadows an installed family.
If that face's sources fail, a cached global fallback may be reused for that
declared face; ordinary candidates in a multi-family CSS list must still
continue to the next candidate instead of accepting that fallback early.

## Direct callers

`font_resolve()` returns one reference to its caller. A caller that does not
store the result in a persistent `FontProp` must release that reference with
`font_handle_release()`. A caller that binds it to `FontProp` transfers the
reference using `font_cache_adopt_handle_alias()` instead.

This distinction is deliberate: the cache governs long-lived Radiant aliases,
but short-lived resolver/fallback operations retain normal explicit ownership.

## Scope and concurrency

The model governs face handles, not glyph bitmap-buffer validity. Glyph caches
have their own arena reset and generation contract. `FontContext` is currently
single-threaded; cache mutation and alias bind/unbind must occur on its owning
UI/layout thread.

## Verification

The ownership boundary is covered by:

- `RadiantViewTest.BatchDocumentFontFaceOverridesSystemCache`
- WPT layout cases `boundary-shaping-002` and `boundary-shaping-008`
- the baseline layout suite (`make layout suite=baseline`)

## Implementation map

| Area | Source |
| --- | --- |
| Public lifetime API | `lib/font/font.h` |
| Cache mapping, keys, LRU, alias lease operations, resolution | `lib/font/font_cache.c` |
| Per-document reset | `lib/font/font_context.c` |
| Persistent style binding | `radiant/font.cpp` |
| `FontProp` copy and `FontBox` handle derivation | `radiant/view.hpp` |
| Line-layout persistent aliases | `radiant/layout.hpp`, `radiant/layout_inline.cpp`, `radiant/layout_text.cpp` |
