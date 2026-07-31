# Lambda Const Pool & the Pointer-Free MIR Image — Design

**Date:** 2026-07-31
**Status:** DRAFT (rev 1) — const-pool mechanism proposed; the baked-pointer census
(§6) is verified against the emitters as of 2026-07-31. The type-graph section
(census C) is explicitly deferred to its own design.
**Scope:** literal/constant emission in the MIR Direct transpilers
(`lambda/runtime/transpile-mir.cpp`, `lambda/js/js_mir_*.cpp`), the future
`.const` / `.source` sections of the cache container, and the per-module const
GOT in module state.
**Relation to prior docs:** extends `vibe/Lambda_Design_Name_Identity.md`
(NI1–NI14) — the section/GOT/registry mechanism is reused, not redesigned here;
feeds the MIR cache L2+ serialization direction
(`vibe/Lambda_Design_MIR_Cache.md`, `_L3.md`, container layout in Name Identity
§7); container-literal semantics tie to `vibe/Lambda_Design_COW.md` (C4
`is_shared`). The end-state rule for emitters is CP7.

---

## 1. Goal

> **Emitted MIR contains no host addresses.** Every constant reference is a
> value immediate or a small integer (GOT index / registry id) resolved through
> per-isolate module state at load-link. The serialized image holds only
> values, offsets, and ids — never pointers — so it can be mmapped read-only,
> shared across processes, and reused across sessions with a single load-link
> pass and zero fixup writes into the image.

Non-goals:
- The Type/TypeMap graph serialization (census **C**) — reserved a ref-kind
  here, designed elsewhere (it is the container problem plus type-system
  invariants; do not fold it into this doc).
- IC/feedback-cell design details (census **B**) — the *slot* principle is
  stated (CP11) because pointer-freedom requires it; the cell format belongs to
  the IC/cache work.
- Implementing the lambda-ELF container. Section layouts here define *format
  direction* for L2+, consistent with Name Identity §7.

## 2. Decisions

| ID | Decision |
|----|----------|
| **CP1** | **Scalar split.** Value immediates: `null`, `bool`, `int`, `int64`, `double` (self-tagged inline doubles), and `datetime` iff it packs into the 64-bit Item payload. Pooled records: `string`, `symbol`, `binary`, `decimal`, non-packable `datetime`. Nothing else is a scalar const. |
| **CP2** | **`.const` section** = immutable byte image of static records, same discipline as name sections (NI3): blittable/mmap, shared read-only, record layouts are a versioned ABI. **Value strings carry no identity semantics** — their `String*` may point straight into the mapping, zero-copy, no registry. Only strings used as *property keys* are names and route through the name GOT/registry (NI6/NI7). Within-module dedup of identical literals at transpile time. |
| **CP3** | **Const GOT** (`modstate->consts[k]`): dense per-module table, exactly parallel to the name GOT (NI7). MIR bakes the dense index; load-link fills each entry from a serialized const-table section (§5). Zero-copy kinds resolve to mapping addresses; materialized kinds (decimal, containers) hold the materialized root. One hoistable load at use sites. |
| **CP4** | **Containers: materialized spine + mapped leaves. In-place relocation is rejected.** Decisive reason: relocated pointers include per-isolate values (map keys = canonical NameRefs, type fields = isolate-materialized `Type*`) which a shared mapping cannot hold — relocation degenerates into a private image copy per isolate. Supporting: rewriting dirties COW pages exactly where the data is biggest; it breaks the immutable-image invariant (NI3); and container literals are clone/COW *templates*, so a runtime spine exists regardless. |
| **CP5** | **Template format is offset-linked and pointer-free** (§4): internal references are tagged `(kind, payload)` pairs — value immediates, `.const` offsets (leaves), name-table indices (keys), template offsets (nesting), type-table indices (reserved for the type-section design). Materialization is one linear post-order walk at load-link. |
| **CP6** | **Materialized container templates are permanent COW sources**: marked `is_shared` (C4); literal evaluation returns a COW reference; first write clones. JS object literals keep the literal-shape path (shape template + per-instance storage) — shapes belong to the type-section story, not the const pool. |
| **CP7** | **The emitter whitelist** (lintable end-state): emitted MIR operands may be — value immediates; dense GOT indices (name GOT, const GOT, feedback slots); registry ids (builtin_id, ctor_id, sys-fn id); MIR-internal symbolic refs (`MIR_new_import`, `MIR_new_ref_op`). **Nothing else.** Any `(int64_t)(uintptr_t)expr` in a lowering file outside these forms is a defect. |
| **CP8** | **Sys functions by registry id** (census E): replace the baked `info->func_ptr` with the sys-function registry id; resolve at load or first use through the same registry that `to_sys_fn_named` consults today. |
| **CP9** | **Diagnostics strings become const refs** (census G): profile labels, error-message literals, side-stack markers are baked rodata addresses today — harmless in-process, dangling in any serialized image. They move to `.const` (or are dropped from release lowering where they are debug-only). |
| **CP10** | **Source section** (census F): function source slices and eval identity become `(offset, len)` into a `.source` section; `fn.toString()` and eval-cache keying resolve through it. |
| **CP11** | **Cache cells become feedback slots** (census B): ICs, shape caches, and ctor-prop caches move from transpiler-allocated structs baked by address into per-isolate module-state arrays; code bakes the slot index and computes `&modstate->feedback[k]`. Required for pointer-freedom and for sharing one compiled module across isolates; detailed cell format out of scope here. |
| **CP12** | **Const-section offsets are 32-bit.** The 64KB/16-bit constraint is a *NameId* property (it lives inside the 32-bit `[slot][offset]` split) and does not apply here: const references in code are GOT indices, and offsets appear only inside const-table entries and template links, which use `u32`. Big literals (long strings, typed-array init data) must not force section spillover. |

## 3. Scalar constants (the easy part)

| Type | Representation | Load-link work |
|------|----------------|----------------|
| null / bool / int / int64 | value immediate | none |
| double | value immediate (self-tagged inline; subnormal outliers constructed by value) | none |
| datetime (packable) | value immediate | none |
| string (value position) | `.const` static String record | GOT ← mapping address (zero-copy) |
| string (property-key position) | **name**, not a const — name table / name GOT | registry canonicalize (NI6) |
| symbol | name-pool record (symbols ≤32 pool today) via name sections | registry canonicalize |
| binary | `.const` byte record | GOT ← mapping address (zero-copy) |
| decimal | `.const` serialized coefficient/exponent bytes | materialize libmpdec object once, GOT ← object; static-flagged, immutable |
| datetime (non-packable) | `.const` packed record | GOT ← mapping address if readable in place, else materialize |

## 4. Container constants (the hard part)

Serialized template (in `.const`), all links offsets or indices:

```
template  := { u8 container_kind          // array | arraynum | list | map | element
             ; u8 flags                   // elem width for arraynum, etc.
             ; u16 reserved
             ; u32 count
             ; ref entries[count] }       // + kv pairs for maps/elements
ref       := { u8 kind; u8 pad[3]; u32 payload }        // or a packed u64 form
ref kinds := IMM64   (payload indexes an imm side-array of raw Item bits)
           | CSTR    (u32 .const offset — static String leaf, zero-copy)
           | CBIN    (u32 .const offset — bytes leaf, zero-copy)
           | NAME    (u32 name-table index → canonical NameRef at load)
           | TMPL    (u32 .const offset — nested template)
           | TYPE    (u32 type-table index — RESERVED for type-section design)
```

Materialization (one pass inside the §5 walk): post-order over `TMPL` links;
build the runtime container spine with the static Mark constructors; keys
resolve through the name GOT slice to canonical NameRefs; mark the root (and
nested containers) `is_shared`; store the root in `consts[k]`.

**Zero-copy leaves:** `CSTR`/`CBIN` payloads — including `arraynum` element
bytes — stay in the mapping; only the spine (headers, slots) is runtime memory.
A literal `[1.0, 2.0, …]` therefore shares its payload with the image until the
first write clones it via COW (see OI-CP3).

## 5. Const-table section and load-link pipeline

The blob gains a `.consttab` parallel to `.nametab`: an array in GOT order of
`{ u8 kind; u8 flags; u16 reserved; u32 offset_or_id }`.

Extending the Name Identity §4 pipeline (steps 1–5 unchanged):

```
3b. Walk .consttab in GOT order:
      IMM-kinds        → (not present; immediates never reach the table)
      CSTR/CBIN        → consts[k] = section_base + offset          (zero-copy)
      DECIMAL          → consts[k] = materialize(bytes)             (once, static)
      TMPL             → consts[k] = materialize_template(offset)   (§4 walk)
3c. Feedback slots: allocate modstate->feedback[] sized from the module header
      (cells zero-initialized; no serialized content).
```

## 6. Baked-pointer census — the road to a pointer-free image

Verified against the emitters 2026-07-31. Already symbolic/pointer-free:
runtime helper calls (named imports, `import_resolver` at `MIR_link` —
`lambda/runtime/mir.c:229`); sibling-function calls and function-object
creation (`MIR_new_ref_op(func_item)`); module variables
(`js_get_module_var(int)`); int/double immediates.

Remaining baked-pointer categories and their fixes:

| # | Category | Example anchors | Fix | Owner |
|---|----------|-----------------|-----|-------|
| A | Name/string chars: identifier & property names, `emit_load_string_literal`, boxed string/symbol Items, regex pattern+flags, literal-shape name tables, class-metadata name arrays | `js_mir_expression_lowering.cpp:1429, 13500, 11742, 725`; `transpile-mir.cpp:1906, 8793, 13492` | name sections + name GOT | Name Identity (W4) |
| B | Mutable cache cells: `JsLoadIC*`/`JsStoreIC*`, `shape_cache_ptr`, `ctor_shape_cache_ptr`, `ctor_prop_ptrs[16]` | `js_mir_expression_lowering.cpp:1124, 11525`; `js_mir_context.hpp:220–324`; `js_mir_statement_lowering.cpp:3169` | feedback slots (CP11) | IC/cache work |
| C | Type graph: boundary `expected` types, literal map shapes, `full_type` roots, `type_list`, `LIT_TYPE_*` singletons | `transpile-mir.cpp:2319, 8158, 12889, 2807, 12693` | type section, own design; `TYPE` ref reserved (CP5) | **deferred — separate doc** |
| D | Transpiler side tables: class-field metadata arrays, bulk import indices/keys | `js_mir_expression_lowering.cpp:725`; `js_mir_module_batch_lowering.cpp:6328` | `.const` records (CP2/CP5) | this doc |
| E | Bare sys-fn references baked as raw `info->func_ptr` | `transpile-mir.cpp:14256` | registry id (CP8) | this doc |
| F | Source text slices + eval source identity | `js_mir_expression_lowering.cpp:48, 12248` | `.source` section (CP10) | this doc |
| G | Diagnostics strings: profile labels, error literals, side-stack markers | `js_mir_expression_lowering.cpp:303, 1840`; `transpile-mir.cpp:1087` | `.const` refs or debug-only elision (CP9) | this doc |

End state: with A (names), B (slots), C (type section), D–G (this doc), the
image satisfies CP7 and is serializable/shareable by construction.

## 7. Open issues

- **OI-CP1 — decimal sharing scope.** Materialized decimals are immutable and
  static-flagged; per-process sharing is safe iff nothing mutates libmpdec
  objects post-construction. Verify against `lambda-decimal.cpp` before
  promoting from per-isolate to per-process.
- **OI-CP2 — datetime packing.** Decide whether the datetime payload fits the
  Item immediate form in all calendars/precisions; otherwise it is a `.const`
  record kind. (Parser-static datetimes are Input-owned today; MIR consts need
  the section home either way.)
- **OI-CP3 — COW coverage for `arraynum` payloads.** Zero-copy element bytes
  require first-write clone of the *payload*, not just the header. Confirm C4
  Stage-1 covers ArrayNum buffers; if not, `arraynum` templates materialize
  with copied payloads until it does (correct, loses zero-copy only).
- **OI-CP4 — cross-module literal dedup.** Same-spelling/same-value literals in
  different modules duplicate `.const` bytes. KIV like NI5 (import-slot dedup):
  needs cache-dependency tracking; not worth coupling for bytes.
- **OI-CP5 — template ABI versioning.** `ref` encoding and record layouts are
  serialization ABI; version with the section header alongside the String ABI
  (NI3).
- **OI-CP6 — large-literal eviction.** A huge literal (multi-MB string/blob)
  pins its pages via the mapping for module lifetime — same retention as the
  name design, but bigger; acceptable, noted for the L2 cache size budget.

## 8. Phasing and gates

- **CP-P0 (mechanical, no format work):** E — sys-fn registry ids; G — move
  diagnostics strings behind debug-only lowering or interned pool refs. Can
  land with Name Identity P0 (W1/W2).
- **CP-P1:** `.const` scalars + const GOT + `.consttab` walk — lands with the
  name-section/GOT infrastructure (Name Identity P2) since the pipeline is
  shared.
- **CP-P2:** container templates + COW-source semantics (needs C4 `is_shared`
  in the literal path); D side-tables fold in here.
- **CP-P3:** B — feedback-slot migration (with the IC/cache workstream).
- **CP-P4:** C — type section, separate design doc, unblocks full CP7.
- **Gates:** lambda + js262 + node baselines no-regress; MIR emission-budget
  ratchet; Result-series re-run at each phase; a **CP7 lint** (grep-class rule
  over `(int64_t)(uintptr_t)` in lowering files, allowlist shrinking per phase)
  so the whitelist is enforced, not aspirational.
