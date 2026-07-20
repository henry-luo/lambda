# JS array `extra` migration inventory

**Status:** complete (2026-07-15)  
**Parent plan:** `vibe/Lambda_Impl_Stack_Frame_JS2.md`

## Pre-migration census

The JP0 scan found 248 textual `->extra` references in ten `lambda/js/`
translation units. About 95 were companion-map reads, 12 were companion-map
writes, and the remainder were companion truth tests, array capacity/count
arithmetic, `JsTimerHandle.extra_args` fields, or `ArrayNumShape*` accesses.
There are no JS MIR-lowering accesses to `Array::extra`.

The companion representation appeared in these runtime files and was migrated
to `js_array_has_props`, `js_array_props`, and `js_array_set_props`:

- `js_runtime.cpp`, `js_globals.cpp`, `js_dom.cpp`, `js_property_attrs.cpp`,
  `js_props.cpp`, `js_runtime_state.cpp`, `js_util.cpp`, `js_assert.cpp`, and the
  generic-Array check in `js_typed_array.cpp`.
- All raw cast reads, pointer writes, and companion truth tests were included;
  sparse-array creation now uses the same setter as ordinary named properties.

## Post-migration residual scan

The same scan now returns 40 references, all classified:

- 29 in `js_event_loop.cpp` are fields named `extra_count` / `extra_args` on
  timer handles; they are unrelated to `Container::extra`.
- 9 in `js_runtime.cpp` are uniform tail-count capacity arithmetic. None casts
  the field to a pointer or uses it as companion-map presence.
- 2 in `js_typed_array.cpp` are the intentional `ArrayNum::extra` →
  `ArrayNumShape*` overload. `ArrayNum` stores unboxed numeric elements and is
  outside SF15-J.

There are 96 `js_array_has_props` and 95 `js_array_props` references under
`lambda/js/`, plus 12 `js_array_set_props` references. A repository-wide scan
finds no remaining generic-Array `extra` → `Map*` cast and no
`gc_mark_possible_item(extra)` probe.

Indexed JS paths outside `js_runtime.cpp` use `container_dense_capacity`; no
descriptor, enumeration, or own-property path treats physical `capacity` as
the indexed range. This is required because both wide scalar payloads and the
props Item live above that logical boundary.

The copy audit also found two `Array.from` clone fast paths that copied raw Item
words into a distinct array. They now read and push through the ordinary owned
store, which both preserves sparse/prototype reads and re-homes any wide scalar
payload. Remaining Item `memcpy`/`memmove` sites in `lambda/js/` either replace
the same array's buffer before `list_relocate_owned_tail`, move Items within the
same owning array, or operate on a temporary sort buffer.

## Buffer provenance and promotion decision

Promotion always moves only the `items` buffer; the `Array` header never moves.
The shared GC-aware `expand_list` is the allocation choke point:

- arena-owned input buffers remain arena-owned;
- GC/data-zone and Lambda fused buffers grow into GC data-zone storage;
- a JS `mem_alloc(MEM_CAT_JS_RUNTIME)` buffer may transition to data-zone
  storage. Its old buffer remains in the JS runtime-buffer registry until normal
  cleanup; registry cleanup checks the owner's current `items` pointer before
  neutering it, so it cannot invalidate the promoted buffer or pair allocators
  incorrectly.

`js_array_install_runtime_items` handles explicit JS buffer replacements. Both
growth paths call `list_relocate_owned_tail`, which moves the exact counted tail
to the new high end and rebases only logical scalar Items whose payload points
into the old buffer.

## GC and sparse-map audit

Array tracing scans `min(length, capacity - extra)` logical Items. With
`CONTAINER_FLAG_JS_PROPS`, it additionally marks exactly
`items[capacity - 1]`; scalar payload words are never interpreted as Items.
Compaction copies the whole buffer and rebases embedded scalar pointers only in
the logical range. The props Item is preserved by the tail move and needs no
pointer rebase because its `Map` lives outside the array buffer.

`SparseArrayMap` remains a base-first `Map`. Reaching it through the reserved
tagged Item invokes the existing Map trace and native sparse trace/finalizer, so
its shape data and `sparse_indices` contents retain their existing ownership and
reachability rules.

## Permanent regression matrix

- `test/js/regression_js_array_props_tail.js` covers props-first and wide-first
  ordering, identity, repeated growth, sparse indices, slice, concat,
  `Array.from` from a temporary source, and forced collections.
- `test/lambda/js_array_props_tail_bridge.ls` plus its JS module cover polyglot
  int64, DateTime, and out-of-band float values in both orders. A Lambda-created
  array is passed into JS, promoted by attaching props, grown, collected, and
  returned; mutations observed through Lambda's original reference prove that
  promotion did not replace the header.
