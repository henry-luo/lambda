# Lambda Name Identity & Name Pool Sections — Design

**Date:** 2026-07-31
**Status:** PROPOSAL — design settled in review discussion (rev 5); not implemented.
The routing cleanups in §9 that need only integer ids (W1, W2) can start
independently of the section/GOT infrastructure.
**Scope:** `lambda/core/name_pool.*`, the mirrored String ABI definitions
(`lib/string.h`, `lambda/lambda.h`) and allocation helpers, LJS runtime + MIR
lowering (`lambda/js/`), generated markup/Lambda/JS/DOM name catalogs and their
consumers, per-module runtime state, and the boundary that must later be
reconciled with the MIR-cache design.
**Relation to prior docs:** builds on `vibe/Lambda_Name_Pool.md` (hierarchical
name pool, symbol ≤32 pooling — implemented Nov 2025); complements
`vibe/Lambda_Design_MIR_Cache.md` / `_L3.md` (future reconciliation required,
§8); per-isolate ownership follows `vibe/Lambda_Js_Thread.md`
(JT1 context-thread rule) and the `vibe/Lambda_Design_Runtime_Globals.md`
invariant that no synchronization lands on repeated execution paths. Motivated
by the 2026-07-28 LJS string-dispatch census (§10).

---

## 1. Goal

> **One address per semantic property key.** Ordinary string names have one
> canonical address per spelling per isolate, assigned once. ECMAScript Symbols
> and private names receive unique addresses according to their semantic
> identity, never according to their display spelling. Every runtime routing
> comparison — property find, method find, builtin/sys-call routing,
> constructor dispatch — becomes a pointer or integer compare.

Content comparison survives only at true string-name boundaries: the first
intern of dynamic bytes, and the seam with id-less data (Mark/Input documents,
raw C-ABI keys). It is never used to compare Symbol or private-name keys.
For predefined names, native parsers, formatters, and Radiant carry the
generated NameId constant and compare it directly. NameRef/PropertyKeyRef
comparison is used where the JavaScript/Lambda runtime must also represent
dynamic strings, Symbols, or private names in one address-sized key domain.

Non-goals:
- No JS semantics change. Lookup order, proxies, exotic receivers unaffected.
- Pointer identity is an internal property-key routing mechanism. It does not
  replace content equality for observable Lambda or JavaScript string values.
- Mark/Input **does not allocate NameIds for document data** (NI13). It may
  reuse a predefined ID, but a JSON with a million distinct keys must not
  expand the global catalog or other session-lived structures.
- Persistent MIR-cache/container integration is not part of this proposal; it
  requires the follow-up reconciliation in §8.

## 2. Terminology and core decisions

| ID | Decision |
|----|----------|
| **NI1** | **NameRef = canonical ordinary-string `String*`. PropertyKeyRef = one address-sized opaque reference whose physical value is the embedded `String*` address.** A PropertyKeyRef has one of three metadata kinds: STRING, SYMBOL, PRIVATE. All three compare with the same definitive operation, pointer equality. Only STRING keys participate in spelling canonicalization. |
| **NI2** | **NameId = `[16-bit global pool][16-bit generated ordinal]`** — the statically generated identity of a predefined name in one global-catalog version. `NAME_ID_NONE == 0` means "not predefined." Published IDs are immutable constants within the generated engine build; direct equality of two non-zero NameIds from that catalog is definitive. During beta, changing the global-name source regenerates the IDs and enums and requires an engine recompile; cross-release numeric stability is explicitly deferred. A separate `SectionNameId = [16-bit local slot][16-bit byte offset]` is used only as a serialized location for module-local records and is never compared as identity. |
| **NI3** | **NameRecord = fixed `NameMeta` prefix + the current runtime `String` ABI (§3).** NameRef/PropertyKeyRef points at the `String`, not the prefix. `String.is_pooled` authorizes recovering the prefix at a fixed negative offset, analogous to `String.is_buffer` authorizing recovery of heap allocation metadata. Name pool sections are immutable byte images of these records: blittable/mmap-able and shareable read-only. |
| **NI4** | **Global pool numbering is fixed:** pool 0 = markup/presentation names, pool 1 = Lambda names, and pool 2 = JavaScript/DOM names (§4). The generator assigns the low 16-bit ordinal within each pool. Global pools are not constrained by the 64KB local-section limit; the generated ordinal→record-offset table resolves a NameId when a NameRef is actually needed. |
| **NI5** | **Module serialization distinguishes identities from locations.** A predefined name is written as its generated NameId. A non-predefined static name is written as a SectionNameId into a module-local section. An artifact containing NameIds is valid only for the matching generated global-catalog version; mismatch requires rejection and regeneration. Referencing an import's local section for byte dedup remains KIV because it couples the importer to the import's section layout. |
| **NI6** | **Evolve the existing NamePool; do not replace it.** Its current allocation-backed, hierarchical, content-interning behavior remains the default used by Input/Mark. Add isolate-canonical, immutable-section-backed, and unique-property-key operations to the same abstraction. The isolate registry is the existing NamePool hashmap evolved to store the canonical String NameRef and its backing owner; for STRING keys, **first definer wins** and adoption is zero-copy. |
| **NI7** | **Per-module property-key GOT** (`modstate->property_keys[]`): dense array of PropertyKeyRefs, filled at load-link for JS/Lambda runtime sites that require address identity. Entries may originate from a global NameId or a module-local SectionNameId. **MIR bakes the dense GOT index** for those sites; native markup/CSS/Radiant code carries and compares NameId directly and does not use the GOT. Private refs are created by class evaluation and stored with the runtime private environment. |
| **NI8** | **Lifetime = pool-level refcount.** Section images are immutable; the per-process `NamePool` control block (mutable, holds `ref_count` + mapping handle) governs when backing memory dies. The registry retains every pool it has drawn at least one canonical from, for isolate lifetime. A module unload drops only its own ref. |
| **NI9** | **NameMeta carries property-key metadata:** generated predefined NameId or NONE, key kind, cached lookup hash, name flags, and the parsed ordinary array index (or a not-index sentinel). STRING uses the exact-byte FNV hash; SYMBOL/PRIVATE use an identity hash assigned independently of their diagnostic bytes, preventing anonymous or same-description keys from collapsing into one hash bucket. `String.flags` keeps general String state (`is_ascii`, `is_buffer`) plus `is_pooled`; it is not overloaded with the full classifier. Exactly **one shared ordinary-name classifier** is used by the transpiler and dynamic intern path. Symbol/private kind is assigned by semantic creation, never inferred from prefixes such as `__sym_` or `__private_`. |
| **NI10** | **The current FNV `ShapeEntry.name_id` is not a NameId and must be renamed `name_hash`; ShapeEntry gains `NameId predefined_id` plus optional `PropertyKeyRef key_ref`.** Mark/Input shapes record a predefined ID when known and otherwise retain `NAME_ID_NONE`; their ordinary predefined comparisons use the ID directly. Canonical JS shapes also set `key_ref`, whose pointer equality remains definitive for the full STRING/SYMBOL/PRIVATE property-key domain. Hash + length + memcmp survives only for non-predefined, id-less STRING names. |
| **NI11** | **Dynamic pool alignment (§7):** all Strings returned by NamePool APIs use the same prefix-backed NameRecord layout and set `is_pooled`. The isolate's dynamic pool allocates in 64KB section-format segments. Dynamic ordinary names are id-less but content-interned; Symbol/private PropertyKeyRefs use unique allocation operations that bypass the spelling hashmap. |
| **NI12** | **eval / `new Function` / REPL** follow the same pipeline: runtime transpilation emits an in-memory name section in the standard format, registered as a pool (control block without file backing), GOT built the same way. One pipeline, no special cases. |
| **NI13** | **Documents excluded from isolate canonical identity, but kept on NamePool:** MarkBuilder/Input retains today's per-Input NamePool and content-hash seam. Its `createName()` results become prefix-backed pooled Strings transparently, while document ShapeEntries continue to copy/store byte views with `key_ref == NULL`. Content strings and Lambda `Symbol*` values remain arena-owned and are not NameRecords. |
| **NI14** | **Threading:** sections immutable-shared; registry, GOT, and dynamic pool are isolate-owned single-writer (no hot-path synchronization, per the Runtime_Globals invariant). The only locked operation is pool control-block bookkeeping (register/retain/release) — rare, load-time events. The generated global canonicals are process-shared; two isolates may pick different canonicals only for non-global spellings. This is harmless because dynamic NameRefs never cross isolates (JT: no shared JS objects). |
| **NI15** | **The three key kinds share representation, not management.** STRING is interned by exact bytes; each ordinary `Symbol()` and private-name creation receives a fresh record; `Symbol.for` and well-known Symbols explicitly reuse their registry/singleton record. A Symbol's description and a private name's source spelling are diagnostic bytes only and never determine identity. |
| **NI16** | **Global well-known pools are generated, never hand-maintained.** One Python data module is the source of truth; one Python generator emits three enum headers and three immutable pool-data files (§4). Generated enum values are compile-time NameId constants and are the comparison operand in native HTML/CSS/SVG, Input, formatter, and Radiant code. NameRef resolution is reserved for consumers whose ABI or semantics actually require a `String*`/PropertyKeyRef. |

**Terminology note:** NameId is generated predefined identity ("which
catalogued name this is") within one global-catalog version. SectionNameId is
serialized position ("where a module-local spelling is written down"). NameRef
is canonical ordinary-string address identity, and PropertyKeyRef is semantic
property-key address identity across STRING/SYMBOL/PRIVATE. Comparing NameIds
is correct when both are non-zero and come from the same catalog version;
comparing SectionNameIds or Symbol/private display bytes as identity is a bug.

## 3. NameRecord ABI and section format

A name pool section is a pure byte image of 8-byte-aligned records:

```
section:  [NameMeta][String image][pad]...          (≤ 64KB, byte offsets 16-bit)

NameMeta (fixed 16-byte prefix, versioned with the section):
          u32 hash
          u32 array_index       // UINT32_MAX = not an ordinary array index
          u16 name_flags
          u8  key_kind          // STRING, SYMBOL, PRIVATE
          u8  reserved8
          u32 name_id           // generated predefined NameId, or NAME_ID_NONE

String:   current runtime ABI, unchanged:
          u32 len
          u8  flags             // is_ascii, is_buffer, is_pooled, ...
          char chars[len]       // begins at byte 5 of String
          '\0'
```

- A SectionNameId's offset addresses the embedded `String` header, so local
  decode remains `section_base + offset`. A global NameId instead indexes its
  generated pool's ordinal→record-offset table. In both cases the NameMeta is
  found at one fixed negative offset, and NameRef/PropertyKeyRef is a valid
  `String*`, not a wrapper pointer.
- Add `uint8_t is_pooled:1` to both mirrored String definitions. This consumes
  a spare bit only: `sizeof(String) == 8` and `offsetof(String, chars) == 5`
  remain unchanged and remain ABI assertions.
- `String.is_pooled == 1` is the authority to recover NameMeta. This follows the
  existing heap string-buffer mechanism: `String.is_buffer` tells
  `string_is_owned_buffer()` that the allocation metadata behind the String is
  valid before it calls `gc_get_header()`. Here `is_pooled` means only
  "this String is preceded by a valid NameMeta and its lifetime is governed by
  a NamePool/backing." It does **not** mean canonical, static, or
  content-interned.
- Pooled records are immutable, so `is_buffer` is always false. Plain static
  strings, content strings, and DomText strings have `is_pooled == 0`; no code
  may subtract NameMeta from them. `is_static` is deliberately rejected as the
  flag name because existing static Strings do not necessarily have a prefix,
  while dynamic and Input-owned NamePool Strings do.

The provenance flags are mutually exclusive:

| `is_buffer` | `is_pooled` | Meaning |
|-------------|-------------|---------|
| 0 | 0 | Plain String; neither GC-buffer nor NameMeta prefix recovery is legal |
| 1 | 0 | Mutable, exclusively owned GC string buffer; `gc_get_header()` is legal |
| 0 | 1 | Immutable NameRecord String; fixed negative-offset NameMeta recovery is legal |
| 1 | 1 | Invalid state |

- Existing String consumers (`len`, `is_ascii`, `chars`) work unchanged.
  `s2it()` may materialize only a STRING-kind PropertyKeyRef as a JS string;
  SYMBOL and PRIVATE records are String-layout-compatible storage but are not
  semantically string values.
- NameRecords are immutable, outside GC, and never individually ref-counted
  (the invariant `name_pool.hpp` documents). Allocation-backed records die with
  their NamePool; mapped/rodata records die with their backing pool. Nothing
  ever writes a mapped section page, so read-only sharing is real sharing.
- NameMeta holds data specific to property-key routing. In particular, the
  classifier stores the parsed array index, not merely an `is_array_index`
  bit; array lookup can therefore retrieve both the classification and numeric
  value without reparsing. TypedArray `CanonicalNumericIndexString` remains a
  separate semantic classifier because it accepts spellings such as `"-0"`,
  `"NaN"`, and `"Infinity"` that are not ordinary array indices.
- `name_id` is non-zero only for generated predefined records. Dynamic,
  Input-local, and module-local records use `NAME_ID_NONE`, unless
  canonicalization replaces them with the matching global record. Code reads
  the ID once when parsing/building a name-bearing structure and carries it
  forward for direct comparisons.
- For STRING, `hash` is the existing exact-byte FNV fingerprint. For a dynamic
  SYMBOL or PRIVATE record, it is a mixed per-isolate identity sequence (or an
  equivalent identity hash), not a hash of `chars`; well-known Symbols receive
  generated identity hashes. Hash remains only a bucket selector, while
  PropertyKeyRef pointer equality is definitive.
- **Budget:** 16B NameMeta + the current `sizeof(String)` allocation convention
  + avg ~10B chars + NUL + alignment ≈ 40B/name → roughly 1.5–1.6K names per
  64KB local section. Big module-local bundles spill into additional local
  slots. Global generated pools use 16-bit ordinals plus an offset table
  and may therefore exceed 64KB, up to 65,535 catalog entries per pool.

The three global well-known pools are generated and shipped as rodata in the
runtime binary as specified in §4. They are registered before any dynamic
runtime names, so their ordinary STRING records are deterministic canonicals
and their well-known SYMBOL records are singleton PropertyKeyRefs. Only
MIR-baked references go through a module GOT; native runtime code uses the
generated well-known-name API.

Ordinary `Symbol(description)` records and PRIVATE records use the same
NameRecord layout but are dynamically allocated with unique-key NamePool
operations. Their `chars` are diagnostic only. `Symbol.for(key)` reuses the
PropertyKeyRef stored in its semantic Symbol registry; repeated class
evaluation creates fresh PRIVATE refs even when source spelling is identical.

## 4. Global well-known name management

### 4.1 Fixed global pools

The runtime predefines exactly three global, immutable NamePools:

| Pool | Generated enum | Contents |
|------|----------------|----------|
| **0 — MARKUP** | `MarkupNameId`, `MARKUP_NAME_*` | HTML, CSS, SVG, XML/MathML, and other markup/presentation-format tag, attribute, property, keyword, and namespace names |
| **1 — LAMBDA** | `LambdaNameId`, `LAMBDA_NAME_*` | Lambda language/runtime type names, builtin/system-function names, and predefined structural field names |
| **2 — JS/DOM** | `JsNameId`, `JS_NAME_*` / `JS_SYMBOL_*` | JavaScript globals, constructors, methods, special properties, DOM API member names, and well-known Symbol records |

These are NameId pool numbers, not module-section slots. Within each pool,
ordinal 0 is reserved and every real name receives a generated ordinal in
`1..65535`. Module-local sections have their own SectionNameId slot namespace,
so global pool entries do not consume module slots. Each semantic predefined
name has exactly one non-zero NameId in a generated catalog; multiple generated
enum labels for the same ordinary spelling are aliases of that one constant
(§4.3).

At process initialization, NamePool wraps the three generated rodata pool
images and pins their backing for process lifetime. The global lookup hierarchy
is pool 2 → pool 1 → pool 0, and the isolate's dynamic runtime NamePool has pool
2 as its parent. Registration completes before a dynamic name can be interned.

Input/Mark NamePools retain their current caller-visible `parent == NULL`
behavior, but NamePool lookup gains the pinned global hierarchy as a read-only
fallback before allocating a new local record (§7.2). Thus a parser-created
predefined `"div"` can reuse `MARKUP_NAME_DIV`, while an arbitrary document key
remains Input-owned. This fallback is NamePool infrastructure, not an isolate
registry or an Input-parent lifetime relationship.

### 4.2 Generated headers and data

Use a separate declarative Python data module as the single source of truth,
keeping data review independent from generator implementation:

- `utils/well_known_names_data.py` — the only hand-maintained list of symbolic
  enum name, UTF-8 spelling/description, owning pool, key kind, and any
  generated flags or well-known-Symbol identity. Its deterministic declaration
  order supplies the generator's ordinal assignment.
- `utils/generate_well_known_names.py` — imports that data module, validates it,
  builds NameRecords, and writes all generated artifacts.

The generator emits exactly one public enum header and one compiled data file
per pool:

| Pool | Generated header | Generated data |
|------|------------------|----------------|
| 0 | `lambda/core/well_known_markup_names.h` | `lambda/core/well_known_markup_names.c` |
| 1 | `lambda/core/well_known_lambda_names.h` | `lambda/core/well_known_lambda_names.c` |
| 2 | `lambda/js/js_well_known_names.h` | `lambda/js/js_well_known_names.c` |

Each header contains a C-compatible enum whose values are the complete
`[pool:16][generated-ordinal:16]` NameIds, plus the matching external pool-data
descriptor. For example, conceptually:

```c
typedef enum JsNameId {
    JS_NAME_LENGTH = NAME_ID_LITERAL(2, 1),
    JS_NAME_PROTOTYPE = NAME_ID_LITERAL(2, 2),
    JS_SYMBOL_ITERATOR = NAME_ID_LITERAL(2, 3),
} JsNameId;
```

The concrete ordinals above are illustrative; generated output is
authoritative. The `.c` files contain 8-byte-aligned immutable NameRecord byte
images, an ordinal→record-offset table, and a `NamePoolData` descriptor with
pool number, byte length, record count, format version, global-catalog
fingerprint, offsets, and data pointer. Generated files carry a `DO NOT EDIT`
banner and the exact
regeneration command. They are checked into source control so ordinary builds
do not depend on regeneration, while
`python3 utils/generate_well_known_names.py --check` fails CI when checked-in
output is stale. Build integration exposes `make generate-names`; generated
source registration is made through `build_lambda_config.json`, never by
editing generated `.lua` files.

Generation is deterministic: no timestamps, locale-dependent ordering, or
host addresses. For a generated engine build, the emitted enum values and
`NameMeta.name_id` fields are published immutable constants: runtime code
never assigns, mutates, or recycles them.

During the beta stage, the numeric assignment is not a cross-release ABI.
Adding, removing, renaming, or reordering global names updates the Python data
source, regenerates the enum headers and pool data together, and requires a
full engine recompile. Deleted names are removed; no tombstones or append-only
ordinal policy is required. The generator may therefore renumber remaining
names in a later build. Whether a future Lambda release preserves Global
NameIds across releases is a separate compatibility decision, deliberately
left for future design. Numeric NameIds must never be copied outside the
generated headers or treated as durable external values.

The generator emits a deterministic global-catalog version/fingerprint. Any
compiled or cached artifact that embeds a NameId must carry that identity and
must be rejected and regenerated when it does not match the engine. The exact
persistent-cache field and invalidation mechanism remain part of the §8
MIR-cache reconciliation.

For every generated record, the generator computes the exact UTF-8 length,
ASCII bit, FNV hash or Symbol identity hash, array-index metadata, record
padding, generated ordinal, enum NameId, ordinal→offset entry, and catalog
fingerprint. Generation fails on duplicate symbolic enum names, embedded NULs,
invalid key kinds, misaligned/out-of-bounds record offsets, or more than
65,535 names in a pool. A generated conformance manifest is checked by tests
against the runtime's shared ordinary-name classifier so the Python emitter
cannot silently disagree with dynamic interning.

### 4.3 Cross-pool duplicates and aliases

Ordinary STRING names are globally deduplicated by exact UTF-8 bytes. When the
same spelling is declared by more than one domain—for example, a markup name
also exposed as a DOM property—the generator emits one physical record and
makes every later enum an alias of its owning NameId. Default physical
ownership follows pool precedence 0, then 1, then 2; the generator reports
cross-pool aliases for review.

Deduplication keys include `key_kind`. A STRING spelling never aliases a
well-known SYMBOL record, and two distinct well-known Symbols never alias
even if their descriptions match. Thus `JS_NAME_ITERATOR` and
`JS_SYMBOL_ITERATOR` designate different records and different
PropertyKeyRefs.

The no-duplicate-record rule makes a predefined enum resolve directly to its
process-global canonical record; it does not depend on temporal
first-definer behavior. The generated header for a domain may therefore
contain an enum whose NameId points into an earlier owning pool.

### 4.4 Direct NameId comparison and materialization API

NameId is the preferred native comparison form for predefined names. A
name-bearing parser/DOM/style structure carries both:

- `NameId name_id` — non-zero for a predefined name, otherwise
  `NAME_ID_NONE`; and
- its existing byte/String representation when arbitrary names, round-trip
  formatting, diagnostics, or public APIs require the spelling.

Concretely, HTML/SVG tokens carry NameId after name recognition;
`TypeElmt`/`ElementReader` expose the element-name ID; `ShapeEntry` carries the
field/attribute predefined ID; `DomElement::tag_id` uses NameId; and standard
CSS declarations carry the property NameId (plus a separate dense code only if
needed for table dispatch).

Hot native comparisons are plain integer operations:

```c
if (tag_id == MARKUP_NAME_DIV) { ... }
if (property_id == MARKUP_NAME_CSS_DISPLAY) { ... }
if (attr_id == MARKUP_NAME_HREF) { ... }
```

No NamePool lookup, address resolution, function call, or content comparison is
allowed on those paths. Cross-domain enum aliases carry the same NameId, so
direct equality remains correct. NameId ordering has no semantic meaning:
existing range checks such as `tag >= HTM_TAG_H1 && tag <= HTM_TAG_H6` migrate
to direct switches or generated category predicates/bitsets rather than
depending on ordinal adjacency.

Common boundary helpers provide:

```c
NameId name_ref_id(NameRef ref);                     // NameMeta.name_id or NONE
NameId property_key_id(PropertyKeyRef ref);          // any key kind, or NONE
NameId well_known_name_id(StrView bytes);            // raw-input catalog lookup
StrView well_known_name_view(NameId id);             // spelling/formatting
NameRef well_known_name_ref(NameId id);              // STRING materialization
PropertyKeyRef well_known_key_ref(NameId id);        // JS/Lambda key materialization
```

`well_known_name_id()` is used once at an id-less input boundary, normally
during tokenization, interning, or node/style construction; the resulting ID
is stored and carried forward. Parsers with generated keyword/tag/property
tables should emit the NameId directly and bypass even this lookup. A NamePool
hit on a generated record reads the same ID from NameMeta. Format-specific case
folding and namespace normalization remain parser responsibilities before
catalog lookup.

`well_known_name_view()` uses the generated pool/ordinal offset table when
code needs bytes for parsing diagnostics, formatting, serialization, or a
legacy `const char*` API. It is not an equality helper.

`well_known_name_ref()` / `well_known_key_ref()` perform the ordinal→record
resolution and are deliberately absent from HTML/CSS/SVG parser and Radiant
layout comparison paths. JavaScript and Lambda property runtimes may resolve
their generated NameIds into a per-runtime `js_wk`/equivalent table once, then
compare PropertyKeyRefs without resolving again.

### 4.5 Non-JS NameRef usage audit

The current native subsystems already point toward the intended design:

- Radiant `DomElement::tag_id` and `HTM_TAG_*` perform direct integer tag
  comparisons throughout layout.
- CSS stores `CssPropertyId` / `CSS_PROPERTY_*` and routes declarations by
  integer ID.
- `DomElement::tag_name`, Element/TypeElmt names, ShapeEntry byte views, and
  CSS property spelling tables exist for arbitrary-name preservation,
  formatting, selectors, diagnostics, and public DOM data—not because equality
  requires a NameRef.

These existing ID systems should converge on generated NameId constants.
`DomElement::tag_id` becomes `NameId`; generated compatibility aliases may map
`HTM_TAG_*` to `MARKUP_NAME_*` during migration, after which the handwritten
HTML tag enum and tag-name hashmap are removed. Standard CSS property identity
uses the corresponding `MARKUP_NAME_CSS_*` NameId. If CSS needs a separate
dense dispatch/table index, call it `CssPropertyCode` and generate the
NameId↔code mapping; never use that dense code as universal name identity.
Internal DOM attribute operations gain `get_attribute(NameId)`,
`has_attribute(NameId)`, `set_attribute(NameId, ...)`, and
`remove_attribute(NameId)` overloads so Radiant checks such as `href`, `open`,
`controls`, and `type` remain direct-ID operations. String overloads remain for
dynamic/public DOM names.

Outside the JavaScript and Lambda script property runtimes, no ordinary
comparison site needs to call `well_known_name_ref()`. The only remaining
non-JS uses are centralized construction/materialization bridges whose current
ABI explicitly requires `String*`, such as creating a Mark Element/map field
from an enum when no source spelling is already present. Prefer new
`MarkBuilder::element(NameId)`, `attr(NameId, ...)`, and `put(NameId, ...)`
overloads; those helpers may resolve internally once, or use the generated
view when the destination stores bytes. Radiant synthetic-node creation and
formatters need only `well_known_name_view()`, not NameRef.

### 4.6 Migration of existing definitions and comparisons

Migration is catalog-first and incremental:

1. Inventory predefined literals in markup/CSS/SVG tables, Lambda builtin/type
   tables, `js_builtin_catalog.def`, JS well-known-Symbol tables, DOM member
   tables, special-property routing, and constructor/method dispatch.
2. Add every semantic predefined name to `well_known_names_data.py`; generate
   all six artifacts and register the three immutable pools through NamePool.
3. Replace local `#define` IDs, duplicated `const char*` tables, manual
   `"name"` + length pairs, and structural `strcmp`/`strncmp` chains. Stored
   IDs and equality sites use the generated enum directly; only
   spelling/materialization sites use `well_known_name_view()` or a centralized
   NameRef bridge.
4. Change existing semantic catalogs to carry a generated name enum rather
   than owning another spelling. For example, `js_builtin_catalog.def` remains
   authoritative for builtin behavior/dispatch IDs, but refers to
   `JS_NAME_*` for the property spelling.
5. Delete a legacy spelling table only after all its construction, lookup,
   formatting, and diagnostic consumers have moved to the generated enum/view;
   do not introduce a parallel compatibility table.

The migration targets comparisons of predefined semantic names, not arbitrary
user text or parser keyword recognition before interning. A repository audit
maintained by the generator/CI reports remaining literal comparisons in
in-scope routing files, with a narrow allowlist for genuine raw-text or
diagnostic cases.

Required tests decode every generated enum and verify pool, generated ordinal,
ordinal→record offset, alignment, `is_pooled`, NameMeta name_id, key kind,
spelling, hash/classifier metadata, cross-pool aliases, well-known-Symbol
non-aliasing, deterministic regeneration, catalog fingerprint consistency,
and the 65,535-name bound.
Existing NamePool, Input/Mark parser, Lambda baseline, JS, DOM, CSS, HTML, and
SVG suites remain regression gates throughout the migration.

## 5. Load-link pipeline

```
1. Obtain section images        — mmap from cache blob, or in-memory from a fresh
                                  transpile (eval: NI12). Zero fixup: images are final.
2. Verify global catalog        — require the artifact's generated catalog
                                  fingerprint to match this engine before
                                  interpreting any GLOBAL_NAME_ID.
3. Register pools               — create control blocks; build the module's
                                  slot → section-base table (link-time only).
4. Walk the module name table   — tagged entries in property-key GOT order:
     for k, entry:
        if entry.kind == GLOBAL_NAME_ID:
            keyref = well_known_key_ref(entry.name_id) // resolve once for JS/Lambda
        else:
            candidate = slot_base[entry.section_id.slot] +
                        entry.section_id.offset         // embedded String*
            meta = name_ref_meta(candidate)             // validate prefix + bounds
            require meta.kind == STRING                 // private is runtime-created
            keyref = registry.canonicalize(candidate)   // retains backing on adoption
        got[k] = keyref
5. Drop the local slot table    — nothing at runtime decodes SectionNameIds.
                                  (Keep behind a debug flag for dump tooling.)
6. Execute                      — code sites load got[k]; helpers receive PropertyKeyRefs.
```

Static shape templates (object literals, classes) serialized in future blobs
reference predefined names by NameId and other static names by SectionNameId.
They are materialized in the same pass, stamping `ShapeEntry.predefined_id`
when non-zero and `ShapeEntry.key_ref` when the JS/Lambda runtime needs address
identity. `ShapeEntry.name` remains the byte view used by
formatters/debugging. Module A's canonical JS shapes are thereby matchable by
module B's lookups with a pointer compare; native predefined-name consumers
use NameId; arbitrary Mark fields retain the byte-comparison seam.

## 6. Runtime effect, structure by structure

| Site | Today | After |
|------|-------|-------|
| Named load/store IC key match | baked rodata `char*` ptr-compare, memcmp fallback ([js_runtime.cpp:7755](../lambda/js/js_runtime.cpp)) | PropertyKeyRef compare, no fallback |
| IC hit | shape ptr compare (unchanged) | unchanged |
| Typemap lookup confirm | FNV `name_id` reject → ptr try → **memcmp confirm** ([lambda-data.hpp:477](../lambda/lambda-data.hpp)) | predefined native name: `predefined_id` compare; canonical JS key: `key_ref` compare; hash/length/memcmp only for non-predefined STRING seams |
| `js_property_get` special names | ~dozens of length-guarded strncmp per access (js_runtime.cpp:4334–5590) | `key == js_wk.length`, where `js_wk` resolves generated NameIds once at runtime initialization |
| String/Number/Array/Math method routing | name-string chains, ~45 branches (`js_string_method` js_runtime.cpp:22877); id path round-trips id→name→chain +alloc (js_runtime.cpp:10556) | `switch (builtin_id)` jump table end-to-end (W1) |
| Dynamic `new ctor(...)` | ~60-arm name chain + `"bound "` strip (js_runtime.cpp:2216) | `fn->ctor_id` int dispatch (W2) |
| Symbol keys | Symbol Item converted to user-spellable `__sym_N` String | semantic Symbol registry returns a unique/singleton SYMBOL PropertyKeyRef; `"__sym_N"` remains an unrelated STRING key |
| Private keys | `__private_<class-index>_` spelling + prefix parsing/brand checks | runtime private environment supplies a unique PRIVATE PropertyKeyRef; repeated class evaluation receives fresh refs |
| Array-index key classification | per-access numeric-string parse | NameMeta returns cached `array_index` or sentinel; TypedArray canonical-numeric classification stays separate |
| HTML/SVG tag routing in Radiant | handwritten `HTM_TAG_*` enum plus duplicated tag-name table/hashmap | generated `MARKUP_NAME_*` stored in `DomElement::tag_id`; direct NameId compare |
| CSS property routing | handwritten `CSS_PROPERTY_*` identity plus spelling table | generated markup NameId is property identity; optional generated dense `CssPropertyCode` remains dispatch-only |
| DOM property access | `fn_to_cstr` + strcmp chains + SipHash record map (radiant_dom_bridge.cpp:3873, jube_interface.cpp:533) | VMap IC receiver kind + records keyed by PropertyKeyRef (W5) |
| Module vars | `js_get_module_var(int)` (already index-based) | unchanged — the GOT follows its pattern |

## 7. NamePool evolution, Input compatibility, and open issues

### 7.1 Evolve NamePool in place

`NamePool` remains the public and ownership abstraction. The initial
implementation evolves it as follows:

- `name_pool_create_len()` / `_strview()` keep their signatures and pointer
  interning behavior, but allocate `[NameMeta][String]`, set `is_pooled`, run
  the shared ordinary-name classifier, set `name_id = NAME_ID_NONE` for a new
  local record, and return the embedded `String*`. A generated global hit
  returns its process-pinned record and generated NameId.
- `NamePoolEntry` keeps its length-aware byte view and canonical `String*`, and
  gains backing-owner information for an adopted section record. A registry
  retains each adopted backing pool once, not once per adopted name.
- An immutable-section factory creates a NamePool control block around mapped
  or rodata records. Its records are not copied into an allocation pool.
- NamePool owns registration and lookup of the three pinned global pools
  (§4). Allocation-backed pools consult that read-only catalog before creating
  a local STRING record, without changing their explicit `parent` pointer.
- Isolate-canonical operation adopts STRING candidates into the root NamePool's
  existing spelling hashmap with first-definer semantics.
- New unique-key operations allocate SYMBOL or PRIVATE records without
  inserting them into the spelling hashmap.
- Existing hierarchy, lookup, retain/release, mem-context registration, and
  statistics APIs remain available. Pool mode/backend differences stay behind
  NamePool APIs.

The existing `name_pool_create_symbol*()` API means content-based Lambda
symbol-spelling pooling and has tests that depend on that behavior. It must not
be repurposed for ECMAScript `Symbol()` identity. The new JS operation must
have an explicitly unique name such as
`name_pool_create_unique_property_key(pool, kind, display, len)`.

### 7.2 Input and Mark compatibility

The existing parser path is deliberately preserved:

1. `Input::create()` obtains `input->name_pool` through
   `mem_name_pool_create()`.
2. `MarkBuilder::createName()` calls `name_pool_create_len()`.
3. Input parsers overwhelmingly use MarkBuilder/ElementBuilder/MapBuilder and
   do not access NamePool representation.
4. Input shape construction copies the returned String's bytes and length into
   its own `StrView` and copies `NameMeta.name_id` into
   `ShapeEntry.predefined_id`; document ShapeEntries keep `key_ref == NULL`.
5. `MarkBuilder::createString()`, DomText strings, and Lambda `Symbol*` values
   remain arena-owned plain values with `is_pooled == 0`.

Thus the prefix is transparent to input parsers: the returned pointer is still
a current-ABI `String*`, and same-name calls within that Input still return the
same pointer. Arbitrary document names retain current Input-pool ownership;
catalogued global names may return their process-pinned record. Input data is
not inserted into the isolate registry and therefore cannot bloat
session-lived canonical state. Parser and Radiant code carry the copied
predefined NameId for equality and do not repeatedly recover metadata or
resolve a NameRef.

Although `Input::create()` accepts a parent `Input`, it currently constructs
the Input NamePool with `parent == NULL`; this proposal preserves that
per-Input behavior. The global well-known fallback in §4.1 is internal to
NamePool and does not populate the `parent` field. Wiring `parent->name_pool`
remains a separate semantic/lifetime change, not an incidental part of
NameRecord migration.

### 7.3 Dynamic alignment

- Static sections, isolate-dynamic ordinary names, unique Symbol/private keys,
  and Input names share the NameRecord physical layout and `is_pooled`
  invariant. Their NamePool modes and identity policies differ.
- Only generated global records own non-zero NameIds. Other records use
  `NAME_ID_NONE`; SectionNameId remains external serialized location metadata.
- Dynamic segments use the 64KB section format, so a future snapshot or
  hot-eval cache can promote an eligible segment without reformatting.
- Only ordinary STRING records are content-canonicalized. Temporal
  first-definer behavior never applies to Symbol or private identity.

Open issues (accepted or KIV):

- **OI-NI1 (accepted): canonical is temporal, not preferential.** If a computed
  key interns `"foo"` before a lazily-loaded module with a static `"foo"`
  arrives, the dynamic record stays canonical and the section record's bytes go
  unused. Correctness is unaffected; well-known names never hit this (boot
  registers them first). Rebinding canonicals later is impossible by design
  (identity has already spread into shapes) — do not attempt.
- **OI-NI2 (pre-existing, KIV): unbounded dynamic intern growth.** Adversarial
  workloads minting unbounded unique computed keys grow the dynamic pool for
  isolate lifetime — exactly today's `heap_create_name` behavior, no
  regression. Unique Symbol/private key records are also isolate-lifetime in
  the initial design because shapes may retain their addresses. Weak
  interning/eviction is inapplicable to unique semantic keys; revisit lifetime
  compaction only with precise ownership evidence.
- **OI-NI3 (implementation audit): pooled-flag discipline.** Every NamePool
  allocation path, including legacy uninterned long-symbol paths, must either
  produce the prefix and set `is_pooled`, or return a plain String that is
  never accepted as NameRef/PropertyKeyRef. Prefer the first invariant:
  everything returned by NamePool has NameMeta.
- **OI-NI4 (KIV): import-slot byte dedup** (NI5) — enable only with cache
  dependency tracking keyed on import section layout hashes.
- **OI-NI5: GOT rebuild on the existing in-memory L1 cache hit.** L1 reuses a
  linked module within a process; the GOT must be (re)filled per isolate that
  adopts the module, mirroring how per-isolate module state is already
  instantiated. Cheap (one registry walk), but it must not be skipped.
- **OI-NI6: SectionNameId offset headroom.** Raw byte offsets cap each
  module-local section at 64KB; storing `offset>>3` would stretch it to 512KB
  per slot if spillover ever proves annoying. This does not limit global
  NameId pools, which use generated ordinals plus offset tables.
- **OI-NI7: mapped backing granularity.** Retaining one canonical must retain
  the mapping that contains its record without accidentally pinning unrelated
  code/const mappings. Resolve with page/allocation-granularity-aligned name
  sections or an explicit shared blob owner before persistent mmap loading.
- **OI-NI8: private-environment lowering.** PRIVATE PropertyKeyRefs must be
  created per class evaluation and made available to every method/brand
  operation for that evaluation. Static private spelling may be compile
  metadata, but a SectionNameId or spelling NameRef must never become its
  semantic identity.

## 8. Future reconciliation with the MIR-cache design

MIR-cache reconciliation is explicitly deferred. This proposal defines the
runtime identity model, global NameId, local SectionNameId, NameRecord image,
and load-link inputs, but it does not select or revise the persistent cache
container, cache levels, invalidation keys, or import-dependency policy.

Before persistent module-local name sections are implemented, a follow-up
design pass must reconcile this document with `Lambda_Design_MIR_Cache.md` and
`Lambda_Design_MIR_Cache_L3.md`. That work must decide:

- where immutable module-local name-section images and the module's tagged
  NameId/SectionNameId table live (the three compiled global pools in §4 are
  already settled);
- how the cache versions the `String` ABI and `NameMeta` layout;
- where the generated global-catalog fingerprint is stored and how a mismatch
  rejects/invalidates artifacts containing build-relative NameIds;
- how mappings are owned and retained after a canonical record is adopted;
- when per-isolate property-key GOTs are built or rebuilt; and
- how import-section dependencies participate in cache invalidation.

Until that reconciliation is complete, §3–§5 describe the required logical
interfaces, not an approved lambda-ELF/cache layout.

## 9. Routing cleanups that ride on this (workstreams + phasing)

- **W1 — builtin method dispatch by id end-to-end.** Convert the
  `js_string_method` / `js_number_method` / `js_array_method` tail /
  `js_math_method` chains to `switch (builtin_id)` bodies; make
  `js_dispatch_*_builtin` call by id (deleting the id→name→`heap_create_name`
  round-trip and its per-call allocation); MIR lowering emits builtin ids for
  typed receivers. Independent of §2–§5 — pure integer scheme. **P0.**
- **W2 — `ctor_id` stamped on JsFunction** at `js_create_constructor` (the
  `JS_CTOR_*` id is already in hand); dynamic-new dispatches on it; `bind`
  copies it; fold `special_ctor_kind` name-sniffing into it. **P0.**
- **W3 — generated global well-known pools (§4).** Add the Python data module
  and generator, emit/register pools 0/1/2, then migrate markup, Lambda,
  JS/DOM, and well-known-Symbol catalogs. Replace `HTM_TAG_*` and standard CSS
  name identity with generated NameId constants, carrying IDs through parser,
  Mark, DOM, style, and layout structures. **P2.**
- **W4 — this design's core**, deliberately staged:

  1. add `String.is_pooled` and NameMeta allocation to **every existing
     NamePool creation path**, preserving the APIs, hierarchy, and interning
     semantics used by Input/Mark;
  2. introduce PropertyKeyRef, unique SYMBOL/PRIVATE creation, the generated
     global-pool registration/resolver used by W3, and
     `ShapeEntry.predefined_id` / `key_ref`, with dual `(chars,len)` helper
     entries during migration; then
  3. add immutable **module-local** sections, the isolate registry, and
     property-key GOT after the MIR-cache reconciliation in §8.

  **P2.**
- **W5 — DOM**: VMap IC receiver kind; finish converting legacy strcmp arms to
  member records; key records by PropertyKeyRef. **P3.**
- **W6 — Node specifier index** per `Lambda_Design_Jube_Node_Hosting.md` (cold
  path; part of the JN stages, listed for completeness). **P4.**

**Gates:** existing NamePool unit tests (including interning, hierarchy, empty
names, and legacy symbol pooling) remain green after W4.1; representative
JSON/XML/HTML/CSS/Markdown and other MarkBuilder-based input parsers retain
their output and lifetimes; lambda-baseline remains 100% for every
`lambda/core` touch; `generate_well_known_names.py --check` passes. For routing
stages: js262 + node-baseline + DOM suites no-regress; MIR emission-budget
ratchet (`test/mir/mir_budgets.json`); Result-series benchmark re-run per phase;
exec-profile counters (existing `js_exec_profile` infra) on each converted path
proving the chains stop being hit.

## 10. Appendix — 2026-07-28 string-dispatch census (motivation)

Where LJS routes by string comparison today (anchors verified 2026-07-28):

- `js_string_method` ~45-branch strncmp chain — `lambda/js/js_runtime.cpp:22877`
  (chain from :22936); number/array/math analogues.
- id→name round-trip + per-call `heap_create_name` in
  `js_dispatch_string_builtin` / `js_dispatch_math_builtin` —
  `lambda/js/js_runtime.cpp:10556–10605`.
- `js_property_get` special-name strncmp scatter — `lambda/js/js_runtime.cpp:4334–5590`.
- Dynamic `new` ctor-by-name chain (~60 arms, `"bound "` strip) —
  `lambda/js/js_runtime.cpp:2216`; memoized name-sniffing
  `js_function_special_ctor_kind` — `lambda/js/js_function.hpp:118`.
- `internalBinding`/require builtin-module memcmp chains —
  `lambda/js/js_runtime.cpp:39633`, `lambda/js/js_mir_entrypoints_require.cpp`.
- DOM: per-access `fn_to_cstr` + strcmp chains + SipHash member map —
  `lambda/module/radiant/radiant_dom_bridge.cpp:3179,3873`,
  `lambda/jube/jube_interface.cpp:533`, `lambda/js/js_dom.cpp:8940`
  (607 str/memcmps in file).
- Already id/IC-based (kept): named ICs (`js_runtime.cpp:7777`, receivers
  limited to MAP/ARRAY_PROPS at :7652), typemap FNV+ptr+memcmp
  (`lambda/lambda-data.hpp:397–590`), `js_get_module_var(int)`
  (`lambda/js/js_runtime_state.cpp:768`), `js_dispatch_builtin` int switch
  (`js_runtime.cpp:10596`), compile-time Math lowering
  (`lambda/js/js_mir_expression_lowering.cpp:5879`).
