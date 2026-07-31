# Lambda Const Pool, MarkPack Binary Data & the Pointer-Free MIR Image — Design

**Date:** 2026-07-31
**Status:** DRAFT (rev 4) — rev 1: bespoke const templates; rev 2: **MarkPack**,
one MessagePack-inspired encoding for script consts and whole documents; rev 3:
skippability, blocks, two-tier validation, trailer, interchange formats; rev 4:
**four-section layering (names / types / data / code), two stream variants
(simple: length-free flow for temporal data; advanced: trailing skip table for
storage, with append-only sealing), and first-class fragments** (self-contained
except name/type references — resolved by slot-preserving envelopes). The
baked-pointer census (§6) is verified against the emitters as of 2026-07-31.
Full Type-graph serialization (census C) remains deferred, but §2-types is now
its designated format home.
**Scope:** literal/constant emission in the MIR Direct transpilers, the cache
container sections, the per-module const GOT, the binary document cache path,
the MarkPack interchange format (`input-markpack` / `format-markpack`), and
fragment extraction.
**Relation to prior docs:** extends `vibe/Lambda_Design_Name_Identity.md`
(NI1–NI14; refines its §7); container semantics tie to
`vibe/Lambda_Design_COW.md` (C4 `is_shared`); feeds MIR cache L2+; the
simple-variant/messaging note ties to `vibe/Lambda_Js_Thread.md` (isolates
communicate by message passing); streamed query & zone maps tie to the
DataFrame/data-processing direction.

---

## 1. Goal

> **One binary encoding for Lambda data, four consumers.** (1) Pointer-free MIR
> constants. (2) Documents that serialize once and re-open by mmap. (3) A
> first-class interchange format, scannable/queryable in place. (4) A transient
> wire form for streams and cross-isolate messages. The container file is
> **§1 names, §2 types, §3 data, §4 code** — a data file has no §4; a fragment
> or message may be as small as §1(+§2)+§3.

Non-goals: full Type/TypeMap graph serialization (census **C** — §2-types is
the home, the content design is separate); IC/feedback-cell format (census
**B**); the lazy VMap-projection engine (format-supported, designed
separately).

## 2. Decisions

| ID | Decision |
|----|----------|
| **CP1** | **Scalar split (script consts).** MIR immediates: `null`, `bool`, `int`, `int64`, `double`, packable `datetime` — never serialized from code. Pooled: `string`, `symbol`, `binary`, `decimal`, non-packable `datetime`. In document serialization every value encodes. |
| **CP2** | **The data section = dense value stream + 8-aligned leaf area.** Leaves (String/Binary records, arraynum payloads) are runtime-layout, static-flagged, zero-copy readable. Value strings carry no identity; only property-key/tag/attr names live in §1. |
| **CP3** | **Const GOT** (`modstate->consts[k]`): dense per-module table parallel to the name GOT (NI7); MIR bakes the index; §3's root table lists roots in GOT order. |
| **CP4** | **Containers: materialized spine + mapped leaves; in-place relocation rejected** (per-isolate NameRefs/`Type*` cannot live in shared pages; COW dirtying; templates are clone/COW sources anyway). §4c. |
| **CP5** | **MarkPack: three deviations from MessagePack.** (1) Map keys are NameId references, never inline strings. (2) The primary map form is **type + packed data**: a shape (names + field kinds) plus untagged packed fields — the wire mirror of `TypeMap` + packed data. (3) Skippability comes from a **trailing skip table** (CP20), not inline prefixes — the stream itself stays a pure MessagePack-style flow. |
| **CP6** | **Materialized templates are permanent COW sources** (`is_shared`, C4); evaluation returns a COW reference; first write clones. |
| **CP7** | **The emitter whitelist** (lintable): MIR operands may be value immediates; dense GOT indices (name/const/feedback); registry ids (builtin/ctor/sys-fn); MIR symbolic refs (`import`, `ref_op`). Nothing else. |
| **CP8** | **Sys functions by registry id** (census E; `transpile-mir.cpp:14256`). |
| **CP9** | **Diagnostics strings → const refs or debug-only** (census G). |
| **CP10** | **Source section** (census F) attached to §4-code. |
| **CP11** | **Cache cells → feedback slots** (census B). |
| **CP12** | **Reference narrowing.** Wire references are ≤32-bit: u32 section-relative offsets, u32 NameIds (`[slot16|offset16]`), varint type-pool indices (u16-friendly), varint counts. Only the section table uses u64 file offsets; a section caps at 4GB, files may exceed it. |
| **CP13** | **One encoding, four consumers** (consts / documents / interchange / messages). Compiled-document cache = §1+§2+§3; script cache adds §4; messages and fragments use the simple variant (CP20) with minimal envelopes (CP26). |
| **CP14** | **Maps are Type + packed data.** Shapes live in the §2 type pool (CP25) or **inline in the stream** for one-off shapes (`map-packed-inline`). A shape = field names (NameIds) + field kind tags; instances pack fields untagged per the spec; a varying field is declared `ANY` and carries a tagged value. Inline-keyed `map` remains the heterogeneous fallback. Each pooled shape materializes one shared TypeMap for all instances. |
| **CP15** | **Two name-resolution policies, one §1 format.** Scripts: isolate registry canonicalize (claims identity, NI6). Documents/interchange/messages: Input-pool parent-first borrow (NI13 preserved). |
| **CP16** | **Inline-vs-leaf threshold** for strings/binaries (initial 32B): below → inline in stream; at/above → aligned leaf record, zero-copy. |
| **CP17** | **Eager materialization in v1**; in-place scan, lazy paging, and random access are format-supported (CP20/CP21); the projection engine is a separate design. |
| **CP18** | **Two-tier validation.** Tier U (fs/web/interchange/messages): full structural validation before any mapping pointer is handed out; the skip table, when present, is **verified against the walk** (a lying table must not mis-drive scanners). Tier M (Lambda-managed cache): magic/version + content-hash verify; for large lazy-mapped files, hash the block index (which holds per-block hashes) so verification does not defeat laziness. Malformed input ⇒ error, never UB. |
| **CP19** | **`input-markpack` / `format-markpack` in scope** — MarkPack as a first-class convert format; round-trip equality over the input corpus is the gate. |
| **CP20** | **Two stream variants, one stream encoding.** Container tags carry **no inline byte-length**. **Simple variant** = the bare stream: single-pass, non-seekable writers, ideal for temporal data — pipes, spill, and cross-isolate messages (JT: isolates already share nothing). **Advanced variant** = the same stream bytes + a **trailing skip table** (varint subtree byte-lengths in container pre-order) + block index — for long-term storage, O(1) subtree skip, streamed query, fragment slicing. **Sealing**: simple → advanced by one linear walk that appends the tables; stream bytes are never rewritten (append-only upgrade, anchored by the CP22 trailer). `fix*` small forms are skipped by bounded walk in both variants. |
| **CP21** | **Block packing** (advanced variant): power-of-two blocks (default 4K = page size; per-file `block_size`), block-aligned sections, subtrees kept contiguous, records avoid straddling where reasonable. The block index entry carries `{first_value_off, container_ordinal, flags, xxh3}` — the ordinal anchors the skip-table cursor so scanners can enter at any block without walking from the start. Skipped ranges are never faulted in; blocks map to HTTP range requests. |
| **CP22** | **Trailer-anchored layout.** The file ends with a fixed trailer pointing at the section table (u64 offsets). All trailing structures — skip table, block index, appended sections — are located through it. This is also the affordance for future **incremental append** (new sections + new trailer supersede the root; old blocks stay valid — on-disk structural sharing in the C4 spirit; semantics KIV, OI-CP11). |
| **CP23** | **Integrity:** whole-file xxh3-class hash in the header (corruption detection & cache identity — not a security boundary); optional per-block hashes in the block index for lazy/partial verification. |
| **CP24** | **`.meta` provenance section**, self-encoded as MarkPack: source path/URL + content hash, original format, versions, creation time. Drives document-cache invalidation; optional for script caches. |
| **CP25** | **Four sections: names / types / data / code.** §2-types is the pool of shared shapes (kind-tagged entries, so the same pool later hosts full Type records when census C is designed — `typed`'s `type_idx` and `map-packed`'s `shape_idx` both index §2). §3-data may still define **inline types** for one-off shapes (CP14); the serializer promotes a shape to §2 once it is referenced more than once (heuristic, OI-CP8). |
| **CP26** | **Fragments are mini-documents.** A subtree's stream bytes are self-contained *except* NameIds (→§1), type refs (→§2), and leaf refs (→§3 leaf area). Extraction has two modes. **Fast mode** — zero rewrite: slice the subtree's stream bytes verbatim and include, wholesale, the referenced name *sections* (keeping their source slot numbers — the envelope slot table may be sparse, which is why `[slot16|offset16]` pays off here), the whole type pool, and the whole leaf area. **Compact mode** — linear rewrite: walk the subtree, build minimal name/type/leaf sections, remap NameIds, shape indices, and leaf offsets during the copy. The extractor picks by size heuristic. Either way the result is a valid MarkPack file (§1+§2+§3, root = the fragment); in the advanced variant the subtree's extent comes from the skip table, in the simple variant from a walk. |

## 3. Scalar encoding

| Type | Script-const path | Document path |
|------|-------------------|---------------|
| null / bool / int | MIR immediate (never serialized) | fixint / tag |
| int64 / double / datetime (packable) | MIR immediate | tag + 8B (datetime ext: 12B) |
| string | leaf record via const GOT (zero-copy) or inline per CP16 | same + `str-name` (pooled repeated value) |
| symbol | NameId → registry/pool per CP15 | same |
| binary | leaf record / inline | same |
| decimal | serialized bytes; materialize once (static, immutable) | same |

## 4. MarkPack

### 4a. Encoding (tag byte + payload; LE; varint = LEB128; no inline container lengths — CP20)

```
0x00–0x7f  fixint 0..127                 0xe0–0xff  fixint -32..-1
0x80–0x8f  fixmap-packed (shape_idx u16; ≤15 packed fields)
0x90–0x9f  fixarray (0–15 elems)
0xc0 nil   0xc2 false   0xc3 true
0xc4 int32(4B)      0xc5 int64(8B)       0xc6 float64(8B)
0xc7 decimal(varlen bytes)
0xc8 datetime(8B)   0xc9 datetime-ext(12B)
0xca symbol(NameId u32)
0xcb str-name(NameId u32)                 // value string pooled in §1
0xcc str-inline(varlen, bytes)            // < CP16 threshold
0xcd str-ref(u32 leaf offset)             // aligned String record, zero-copy
0xce bin-inline(varlen, bytes)            0xcf bin-ref(u32 leaf offset)
0xd0 array(varint n; n values)
0xd1 array-imm64(varint n; n × raw 8B Item bits)   // value-kind Items only; blit
0xd2 arraynum(u8 elem_type; varint n; u32 leaf offset)
0xd3 list(varint n; n values)
0xd4 map(varint n; n × (NameId u32, value))        // inline-keyed FALLBACK
0xd5 map-packed(varint shape_idx; untagged fields per §2 shape)   // PRIMARY
0xda map-packed-inline(shape-def; untagged fields)  // one-off inline type (CP25)
0xd6 element(NameId tag; attrs value (map form); varint n; n content values)
0xd7 range(value lo, value hi)
0xd8 array-indexed(varint n; n × u32 child offsets; n values)  // random access
0xd9 typed(varint type_idx; value)                 // reserved (census C → §2)

shape-def := varint field_count; field_count × { NameId(u32); u8 field_kind }
             // field_kind = a tag kind above, or ANY (tagged value inline);
             // non-ANY fields pack untagged at the width the kind implies
```

The three deviations (CP5) in one sentence each: **NameId keys** keep identity
across the wire — loaded shapes carry canonical NameRefs, so cross-module
lookups remain pointer compares; **typed shapes + packed rows** make a record
array cost one shape plus untagged fields per row, mirroring `TypeMap` +
packed data and landing directly on the shared-root-shape runtime win; the
**trailing skip table** gives O(1) subtree skip without polluting the stream —
so the same bytes serve streaming producers and seekable storage.

Functions, closures, VMaps, native handles do not serialize — encoder error.

### 4b. Data section layout (advanced variant)

```
.data header:  u32 version;  u32 block_size (power of 2, default 4096)
               u32 stream_off, stream_len       // dense value stream
               u32 leaf_off,   leaf_len         // 8-aligned static records
               u32 roots_off,  root_count       // u32 stream offsets
               u32 skiptab_off, skiptab_len     // 0 in simple variant
               u32 blockidx_off, blockidx_count // 0 if absent
skip table:    varint subtree byte-length per container, pre-order (CP20)
block index:   per block { u32 first_value_off; u32 container_ordinal;
                           u16 flags; u16 reserved; u64 xxh3 }   (CP21/CP23)
.types (§2):   varint entry_count; kind-tagged entries (SHAPE = shape-def;
               future kinds reserved for census C)
```

The simple variant is the same stream with `skiptab_off = blockidx_off = 0` —
and sealing appends the missing tables without touching a byte of the stream.

### 4c. Materialized spine (the load-time result)

The spine is the ordinary runtime containers, built once into a static arena
owned by module state (scripts) or the Input (documents) — the same
static-Mark-container world parser-built data occupies. GC never traces it;
teardown frees it wholesale.

- **array / list** — header + `Item[]` slots; imm values paste as Item bits;
  `str-ref`/`bin-ref` box pointers into the mapping; `array-imm64` blits.
- **arraynum** — header in arena, payload pointer into the leaf area
  (zero-copy), subject to OI-CP3 (copy until C4 covers payload buffers).
- **map** — `Map` + `TypeMap` + packed data via existing static Mark
  constructors. **No census-C dependency**: `map-packed` kinds come from the
  shape; inline-keyed maps infer from values (self-typed). One shared TypeMap
  per pooled shape; `ShapeEntry.name` stamped with the resolved NameRef —
  which is what makes baked lookups hit by pointer equality; FNV `name_id`
  serves its demoted hash-only role.
- **element** — `TypeElmt` with tag/attr names as resolved NameRefs.

Every spine node is `is_shared` (CP6); the spine is never written.

### 4d. Resolution policies (CP15)

| | Scripts (§1–§4) | Documents/interchange/messages (§1–§3) |
|---|---|---|
| NameId → | isolate registry canonicalize | Input pool parent-first borrow |
| Spine home | module const arena, isolate lifetime | Input arena, document lifetime |
| Roots | const GOT via `roots[]` | single document root |
| Validation | Tier M | Tier U unless cache-managed |

### 4e. In-place scan and streamed query (advanced variant)

A scanner over a mapped or range-fetched §3 evaluates path-style queries
touching only bytes on the query path: resolve the *query's* names once
against §1, then element tags and map keys compare as u32 NameIds;
`map-packed` locates fields at shape-computed positions; non-matching subtrees
skip via the skip table (block-index ordinals let a scanner enter at any
block); skipped pages never fault in. Substrate for streamed search, VMap lazy
projection, and the DataFrame direction (per-block zone maps = OI-CP12).

### 4f. Fragments and messages

Fragment extraction per CP26 (fast = verbatim slices + wholesale sections,
enabled by sparse slot tables; compact = minimal sections + linear remap).
Cross-isolate messages are the degenerate fragment: simple-variant stream +
minimal envelope; the receiving isolate interns names through its own
registry/pool on decode — which is exactly the CP15 document policy, so
messaging needs no new mechanism.

## 5. Container file and pipelines

```
cache/interchange file:
   header     magic, format version, build id, String-ABI version, content hash
   §1 names   name pool section(s)      (NI3: static String records; sparse
                                         slot table permitted — CP26)
   §2 types   shared shape/type pool    (CP25)
   §3 data    MarkPack stream + leaves  (§4b; block-packed in advanced variant)
   §4 code    MIR image + name table (GOT order) + .source + feedback count
              — scripts only
   .meta      provenance (CP24)
   trailer    u64 section-table offset  (CP22; also anchors sealed tables)
```

**Script load-link** (extends Name Identity §4): map → Tier-M verify →
register pools → name table → name GOT (registry) → §2 shapes → shared
TypeMaps → `roots[]` → const GOT (zero-copy kinds ⇒ mapping addresses;
containers ⇒ §4c; decimals ⇒ one-time) → feedback slots → link/execute. Zero
writes into the image.

**Document load**: map → validate (tier per source) → intern §1 via Input pool
→ materialize shapes + root into the Input arena. Cache-miss: parse source,
then serialize (names → §1, shapes → §2, data → §3, provenance → `.meta`),
sealed (advanced variant).

**Interchange** (CP19): `format-markpack` emits advanced-variant files;
`input-markpack` loads any variant at Tier U. **Messages/streams** use the
simple variant; sealing upgrades a captured stream to storage form in one
append pass.

## 6. Baked-pointer census — the road to a pointer-free image

Verified against the emitters 2026-07-31. Already symbolic/pointer-free:
runtime helper calls (named imports, `import_resolver` at `MIR_link` —
`lambda/runtime/mir.c:229`); sibling-function calls and function-object
creation (`MIR_new_ref_op(func_item)`); module variables
(`js_get_module_var(int)`); int/double immediates.

| # | Category | Example anchors | Fix | Owner |
|---|----------|-----------------|-----|-------|
| A | Name/string chars: identifier & property names, `emit_load_string_literal`, boxed string/symbol Items, regex pattern+flags, literal-shape name tables, class-metadata name arrays | `js_mir_expression_lowering.cpp:1429, 13500, 11742, 725`; `transpile-mir.cpp:1906, 8793, 13492` | name sections + name GOT | Name Identity (W4) |
| B | Mutable cache cells: `JsLoadIC*`/`JsStoreIC*`, `shape_cache_ptr`, `ctor_shape_cache_ptr`, `ctor_prop_ptrs[16]` | `js_mir_expression_lowering.cpp:1124, 11525`; `js_mir_context.hpp:220–324`; `js_mir_statement_lowering.cpp:3169` | feedback slots (CP11) | IC/cache work |
| C | Type graph: boundary `expected` types, literal map shapes, `full_type` roots, `type_list`, `LIT_TYPE_*` singletons | `transpile-mir.cpp:2319, 8158, 12889, 2807, 12693` | §2-types section; `typed` tag reserved | **deferred — separate doc** |
| D | Transpiler side tables: class-field metadata arrays, bulk import indices/keys | `js_mir_expression_lowering.cpp:725`; `js_mir_module_batch_lowering.cpp:6328` | §3 records / name-GOT slices | this doc |
| E | Bare sys-fn references baked as raw `info->func_ptr` | `transpile-mir.cpp:14256` | registry id (CP8) | this doc |
| F | Source text slices + eval source identity | `js_mir_expression_lowering.cpp:48, 12248` | `.source` (CP10) | this doc |
| G | Diagnostics strings: profile labels, error literals, side-stack markers | `js_mir_expression_lowering.cpp:303, 1840`; `transpile-mir.cpp:1087` | const refs / debug-only (CP9) | this doc |

## 7. Open issues

- **OI-CP1 — decimal sharing scope** (verify immutability in `lambda-decimal.cpp`).
- **OI-CP2 — datetime packing** (8B coverage vs 12B ext).
- **OI-CP3 — COW coverage for arraynum payloads** (copy until C4 covers buffers).
- **OI-CP4 — cross-module literal dedup** (KIV, as NI5).
- **OI-CP5 — encoding ABI versioning** (tags, shape entries, skip table, block
  index — versioned with the section header alongside the String ABI).
- **OI-CP6 — large-literal/document retention** (mapped leaves pin pages).
- **OI-CP7 — eager-materialization benchmark** vs input parsers (gate for the
  document path; urgency signal for lazy projection).
- **OI-CP8 — serializer heuristics:** CP16 threshold; inline-shape → §2
  promotion (reuse count); `str-name` pooling; block straddling/padding; fast
  vs compact fragment extraction cutover.
- **OI-CP9 — per-block compression** (KIV; flag space reserved).
- **OI-CP10 — shape-pool blowup** on heterogeneous data (fallback heuristics).
- **OI-CP11 — incremental append semantics** (multi-root supersession, garbage
  blocks, compaction — future design over the CP22 affordance).
- **OI-CP12 — zone maps** for query pushdown (with the DataFrame design).
- **OI-CP13 — skip-table encoding details:** varint pre-order lengths +
  block-ordinal anchors is the proposal; confirm scanner-entry cost at block
  granularity and the seal-pass cost on large files.
- **OI-CP14 — sealing tool surface:** where sealing lives (`lambda convert`,
  cache manager, or both) and whether the cache auto-seals simple-variant
  spills.

## 8. Phasing and gates

- **CP-P0 (mechanical):** E — sys-fn registry ids; G — diagnostics strings.
  Lands with Name Identity P0 (W1/W2).
- **CP-P1:** stream encoder/decoder (simple variant first — it is the smaller
  core) + Tier-U validator + §2-types/§3 layout; scalar consts + const GOT.
  Lands with the name-section/GOT infrastructure (Name Identity P2).
- **CP-P2:** container materialization (§4c) + COW-source semantics; D
  side-tables; sealing pass (skip table + block index + hashes).
- **CP-P2b:** document cache path (serialize Input, `.meta`, source-hash
  invalidation; OI-CP7 benchmark).
- **CP-P2c:** `input-markpack` / `format-markpack` (CP19) + fragment
  extraction (CP26); round-trip and fragment-reload gates.
- **CP-P3:** B — feedback-slot migration.
- **CP-P4:** C — census-C type records join §2-types (separate doc); `typed`
  activates; full CP7.
- **KIV:** lazy VMap projection, incremental append (OI-CP11), zone maps,
  per-block compression, auto-sealing policy (OI-CP14).
- **Gates:** lambda + js262 + node baselines no-regress; MIR emission-budget
  ratchet; Result-series re-run per phase; parse-vs-decode structural equality
  over the input corpus (documents, interchange, fragments); the **CP7 lint**
  (shrinking `(int64_t)(uintptr_t)` allowlist).
