# Lambda Name Identity — Detailed Implementation Plan

**Status:** Draft implementation plan  
**Design authority:** [Lambda_Design_Name_Identity.md](../Lambda_Design_Name_Identity.md)  
**Scope:** NamePool evolution, generated global `NameId` catalogs, and migration of
Input/markup/Radiant/Lambda/JavaScript name handling  
**Cache status:** Persistent MIR-cache and module-section reconciliation is deferred
as specified by the design.

---

## 1. Objective

Implement one coherent name-identity system while evolving the existing
`NamePool`:

1. every String returned by a NamePool has the prefix-backed `NameRecord` ABI;
2. predefined names have a generated, compile-time `NameId`;
3. native markup, CSS, SVG, and Radiant code compares predefined names directly
   by `NameId`;
4. Lambda and JavaScript runtime property operations compare
   `PropertyKeyRef`s by pointer;
5. STRING, SYMBOL, and PRIVATE property keys use the same pointer-sized
   `PropertyKeyRef` representation while retaining different creation rules;
6. existing Input/Mark parsers continue to use per-Input NamePools and retain
   their present ownership and content-comparison behavior;
7. no new persistent MIR-cache format is introduced in this work.

The work is split into three gated phases. Each phase must leave the repository
buildable and the relevant baseline suites passing.

---

## 2. Decisions that implementations must preserve

These are implementation invariants, not optional alternatives.

### 2.1 Identity domains

- `NameId` is a generated predefined-name identity:
  `[16-bit global-pool number][16-bit ordinal]`.
- `NAME_ID_NONE == 0`.
- Pool 0 is markup/presentation, pool 1 is Lambda, and pool 2 is JavaScript/DOM.
- Generated IDs are immutable constants within one generated engine build.
  During beta, a source-list change regenerates all affected IDs and enums and
  requires a full engine rebuild.
- This plan does not require tombstones, append-only allocation, or numeric
  stability between Lambda releases.
- `SectionNameId` is a serialized module-local record location. It is never an
  equality identity. Persistent use of it is deferred to the MIR-cache
  reconciliation.
- `NameRef` is the embedded `String*` of an ordinary STRING NameRecord.
- `PropertyKeyRef` has the same physical representation—an embedded
  `String*`—for STRING, SYMBOL, and PRIVATE records.
- `PropertyKeyRef` equality is pointer equality. Only STRING keys are
  canonicalized by bytes.
- A `NameId` is not a CSS dense dispatch index, an HTML category code, a hash,
  or a module-section offset.

### 2.2 `NameRecord` and String ABI

- `NameRecord` is `[16-byte NameMeta][current String ABI][characters][padding]`.
- `NameRef` and `PropertyKeyRef` point to the `String`, not `NameMeta`.
- Add `String.is_pooled` in both ABI declarations:
  `lib/string.h` and `lambda/lambda.h`.
- `is_pooled` permits fixed-negative-offset recovery of `NameMeta`.
- `is_buffer` retains its current meaning for GC heap string-buffer recovery.
- `is_buffer` and `is_pooled` are mutually exclusive.
- Plain Strings have neither flag and must never be passed to NameMeta accessors.
- All NamePool creation paths—including long names and current symbol-name
  helpers—must produce prefix-backed pooled records. There must not be a
  “sometimes pooled” NamePool API.

### 2.3 Global-name use

- Native HTML/CSS/SVG/Input/Radiant code carries and compares `NameId` directly.
- Those hot paths must not call `well_known_name_ref()` merely to compare a
  predefined name.
- `well_known_name_ref()` and `well_known_key_ref()` are reserved for a
  boundary that actually needs a `String*`/`PropertyKeyRef`, principally
  Lambda/JS runtime linking and centralized object construction.
- Byte output uses `well_known_name_view()`.
- Dynamic/custom document names retain byte views and `NAME_ID_NONE`.

### 2.4 Input compatibility

- `Input::create()` continues to construct its per-Input NamePool with
  caller-visible `parent == NULL`.
- The pinned global fallback is internal NamePool infrastructure and must not
  populate or expose the `parent` field.
- Mark/Input names may return a process-pinned global record on a global hit.
- Other document names remain owned by the Input NamePool.
- Input ShapeEntries keep their copied byte views and `key_ref == NULL`.
- Input does not allocate dynamic global `NameId`s.

### 2.5 Runtime and cache boundary

- The legacy C2MIR path is frozen. New lowering work is MIR Direct only.
- Phase 3 may introduce an in-memory module property-key specification table
  and runtime link table.
- Phase 3 must not choose or imply the persistent serialization of
  `SectionNameId`, local name sections, cache catalog fingerprints, or
  relocation records.
- The design note about future reconciliation with
  `Lambda_Design_MIR_Cache.md` and `Lambda_Design_MIR_Cache_L3.md` remains the
  authority for that later work.

---

## 3. Delivery strategy

### 3.1 Phase dependency

```text
Phase 1: NameRecord + evolved NamePool + safe Shape/Input seam
    |
    +-- gate: all Input parsers and Lambda baseline still work
    |
Phase 2: generated pools 0/1/2 + markup/CSS/SVG/Radiant NameId migration
    |
    +-- gate: generated files clean; HTML/CSS/SVG/Radiant baselines pass
    |
Phase 3: Lambda/JS PropertyKeyRef migration + transient module link table
    |
    +-- gate: Lambda, Test262, Node, DOM, and runtime ownership tests pass
```

### 3.2 Change discipline

- Keep compatibility adapters only long enough to make each intermediate
  change buildable. Remove them before the owning phase exits.
- Do not mix spelling, hash, dense-code, record-offset, and identity fields
  under names such as `name_id`.
- At each structure migration, initialize every new field in all constructors,
  clones, transitions, deserializers, and test fixtures before enabling reads.
- Use the repository's `Str`, `StrView`, `ArrayList`, `HashMap`, pools, and
  arenas. Do not introduce standard-library collection or string ownership.
- Add root-cause or invariant comments at ownership, ABI, and canonicalization
  enforcement points.
- Do not edit generated Tree-sitter `parser.c`, generated Lua build files, or
  `log.conf`.

### 3.3 Required pre-change baseline

Before Phase 1 changes:

1. build the current debug targets used by the affected unit tests;
2. run `test_name_pool_gtest`;
3. run the Input baseline and record any known pre-existing failures;
4. run `make test-lambda-baseline`;
5. record current HTML/CSS/Radiant and JS baseline results before their phases;
6. run a source census and save the categories, not merely a raw match count:
   current `ShapeEntry.name_id`, `HTM_TAG_*`, `CSS_PROPERTY_*`, semantic
   `strcmp`/`strncmp`, `__sym_`, `__private_`, and baked member-name pointers.

The census is a migration checklist. Raw syntax recognition—such as HTML
tokenizer sentinels, grammar keywords before interning, and output spelling—is
classified separately so it is not incorrectly converted to identity logic.

---

## 4. Phase 1 — Extend NamePool and implement the identity mechanism

### 4.1 Phase 1 result

At the end of Phase 1:

- all NamePool-returned names use `NameRecord`;
- ordinary names can expose cached hash, array-index classification, key kind,
  flags, and predefined ID;
- unique SYMBOL and PRIVATE key records can be created;
- a NamePool can wrap immutable generated data later, although the generated
  catalogs are populated in Phase 2;
- `ShapeEntry.name_id` has been correctly split into hash, predefined identity,
  and optional address identity;
- Input/Mark continues to parse all supported formats with document
  `key_ref == NULL`;
- no HTML/Radiant/JS bulk migration is required yet.

### 4.2 P1.1 — Introduce the core types and ABI

Primary files:

- `lib/string.h`
- `lib/string.c`
- `lambda/lambda.h`
- `lambda/lambda-data.hpp`
- new `lambda/core/name_identity.h`
- new `lambda/core/name_identity.cpp`, if non-inline helpers are required

Tasks:

1. Define fixed-width public types:

   - `NameId`;
   - `SectionNameId`;
   - `NameRef`;
   - `PropertyKeyRef`;
   - `NameKeyKind` with STRING, SYMBOL, and PRIVATE;
   - `NAME_ID_NONE`;
   - ordinary array-index “not an index” sentinel.

2. Define the 16-byte `NameMeta` exactly once in the core header:

   - cached 32-bit hash;
   - cached 32-bit ordinary array index or sentinel;
   - 16-bit name flags;
   - 8-bit key kind;
   - 8-bit reserved field;
   - 32-bit generated `NameId`.

3. Add compile-time checks for:

   - `sizeof(NameMeta) == 16`;
   - `sizeof(NameId) == 4`;
   - `sizeof(PropertyKeyRef) == sizeof(void*)`;
   - the existing String size and character offset in both ABI declarations;
   - identical String flag layout between `lib/string.h` and `lambda/lambda.h`;
   - alignment required by generated/mapped records.

4. Allocate a String flag bit to `is_pooled` in both declarations. Preserve the
   remaining bits and current `is_ascii`/`is_buffer` meaning.

5. Add checked helpers:

   - `string_is_pooled(const String*)`;
   - `name_ref_meta(NameRef)`;
   - `property_key_meta(PropertyKeyRef)`;
   - `name_ref_id(NameRef)`;
   - `property_key_id(PropertyKeyRef)`;
   - `property_key_kind(PropertyKeyRef)`;
   - `property_key_hash(PropertyKeyRef)`;
   - `property_key_array_index(PropertyKeyRef)`;
   - `property_key_equal(a, b)`, implemented as pointer equality.

6. Make metadata recovery reject or assert on a null or unpooled String in
   debug builds. Release code must not silently subtract a prefix from a plain
   String.

7. Audit every current `is_buffer` write and `String` bitfield copy. Ensure no
   allocation or conversion accidentally preserves `is_pooled` while dropping
   the prefix, or sets both ownership flags.

8. Keep general content Strings unchanged. `string_from_strview()` remains a
   plain-String allocator unless it is deliberately called through a new
   NameRecord allocation helper.

Acceptance tests:

- compile-time ABI assertions pass in C and C++ translation units;
- plain, heap-buffer, and pooled Strings are correctly distinguished;
- metadata access on a valid pooled record returns the exact prefix;
- copying pooled bytes into a plain String does not copy `is_pooled`;
- no GC path calls `gc_get_header()` for a pooled record.

### 4.3 P1.2 — Implement one shared ordinary-name classifier

Primary files:

- `lambda/core/name_identity.h`
- `lambda/core/name_identity.cpp`
- existing FNV/hash helper headers in `lib/`

Tasks:

1. Add one classifier used by:

   - dynamic NamePool insertion;
   - generated-record creation in Phase 2;
   - Lambda/JS static-key collection in Phase 3.

2. In one byte pass, compute:

   - exact-byte FNV hash compatible with existing ShapeEntry lookup;
   - ASCII flag;
   - ordinary array index according to the runtime's property-index rules;
   - ordinary name flags needed by runtime routing.

3. Keep typed-array canonical-numeric-index rules separate. The cached ordinary
   array index must not be used as a substitute for TypedArray semantics.

4. For SYMBOL and PRIVATE records:

   - assign identity hashes independently of diagnostic spelling;
   - set array index to the sentinel;
   - never infer kind from a byte prefix.

5. Add a collision test proving that identical hashes do not establish STRING
   equality without canonical pointer equality or the existing length+bytes
   fallback at an id-less document seam.

Acceptance tests:

- generator-side and runtime-side classification produce identical metadata;
- boundary cases for `"0"`, leading zeroes, maximum valid index, overflow,
  empty name, non-ASCII, and embedded NUL are covered;
- two unique keys with the same diagnostic bytes receive distinct references
  and valid identity hashes.

### 4.4 P1.3 — Evolve NamePool allocation and lifetime

Primary files:

- `lambda/core/name_pool.hpp`
- `lambda/core/name_pool.cpp`
- `lambda/core/mem_factory_core.cpp`
- `lib/ref_counted_pool.hpp`, only if the generic finalization helper needs a
  compatible owner hook
- affected runtime/Input pool factories and teardown sites

This work package has a mandatory ownership checkpoint. The present
`NamePool.ref_count` does not by itself keep its backing `Pool` alive; callers
can destroy the backing allocation independently. Runtime canonical adoption
must not begin until a retained NamePool also keeps every adopted record's
backing owner alive.

Implement an explicit backing contract:

1. Add a NamePool backing descriptor containing:

   - backing kind: allocation Pool, generated rodata, mapped section, or dynamic
     section segments;
   - backing base/descriptor as appropriate;
   - retain and release operations for the owner;
   - whether records from the backing may be adopted by an isolate registry;
   - immutable/pinned flags.

2. Extend `NamePoolEntry` with the backing owner of its canonical record.
   A registry NamePool can contain records adopted from several backings, so
   the root pool's own allocator descriptor is not sufficient provenance.

3. Give the isolate registry an adopted-backing set keyed by owner identity.
   Retain an owner on its first adopted entry and release it once at isolate
   teardown, independent of the number of names drawn from that owner.

4. Separate the mutable NamePool control block lifetime from record backing
   lifetime. The control block must remain valid until final
   `name_pool_release()`, and final release must release the backing after the
   hashmap and parent references are finished.

5. Preserve `name_pool_create(Pool*, parent)` for current Input callers, but
   define its ownership mode explicitly. Input-backed records are not eligible
   for isolate canonical adoption.

6. Add factories for:

   - an owned allocation-backed NamePool;
   - a pinned immutable `NamePoolData` wrapper;
   - an isolate dynamic NamePool whose normal records use 64KB
     section-format segments;
   - a runtime registry/root NamePool with internal global fallback.

7. Keep process-global generated pools pinned. Their retain/release operations
   are no-ops after process initialization.

8. For adoptable non-global backings, the isolate registry retains the backing
   owner the first time it accepts a canonical record from that backing.
   Repeated names from the same backing must not add unbounded retain entries.

9. For non-adoptable candidates, copy/classify them into the isolate dynamic
   NamePool before publishing a canonical pointer. Never publish a pointer from
   transient Input or scratch memory.

10. Preserve currently accepted long-name behavior. Before enabling segmented
   allocation, define and test the single-record-over-64KB path. The safe
   initial policy is a dedicated prefix-backed, non-sectionable dynamic
   allocation with `NAME_ID_NONE`; it is not eligible for future
   SectionNameId serialization until the cache design defines that case.

11. Keep the existing `parent` API for explicit hierarchical NamePools. Add a
   separate internal global-catalog fallback path so an Input NamePool still
   reports `parent == NULL`.

12. Preserve the existing distinction between lookup and create order:

   - lookup continues to search the current pool before its explicit parent;
   - create continues to honor an existing explicit-parent canonical before
     allocating locally;
   - every allocation-backed create consults the registered global catalog
     before it allocates a new local record;
   - process initialization must register global pools before an Input,
     compiler, or runtime NamePool can create names, so a predefined spelling
     cannot acquire an earlier local canonical;
   - isolate registry behavior is used only by runtime-canonical operations.

13. Split APIs by semantics:

    - content-interned ordinary name creation;
    - lookup without insertion;
    - isolate canonicalization/adoption;
    - unique SYMBOL creation;
    - unique PRIVATE creation;
    - immutable-data wrapping and ID resolution.

14. Preserve the existing `name_pool_create_symbol*()` content-pooling
    behavior for Lambda symbol spellings, including its length policy, but make
    every returned String a NameRecord. Do not repurpose this API for
    ECMAScript Symbol identity. Add separately named unique SYMBOL/PRIVATE
    property-key operations. Lambda language `Symbol*` values that are not
    property keys remain outside this mechanism.

15. Implement temporal first-definer behavior without rebinding:

    - once a dynamic canonical has entered runtime shapes, a later section
      record with the same STRING bytes must resolve to that canonical;
    - generated globals are registered before runtime interning and therefore
      always win for predefined names;
    - SYMBOL and PRIVATE operations never use first-definer/content behavior.

16. Keep isolate registry, dynamic-segment, and link-table mutation on the
    isolate's existing single-writer path. Do not add locks to property hot
    paths. Synchronization is limited to rare backing
    register/retain/release bookkeeping.

17. Update memory-context registration and statistics so:

    - control blocks and record backing are attributed correctly;
    - a retained backing is not reported as freed;
    - teardown does not unregister a node twice;
    - pinned rodata is not reported as mutable heap consumption.

Ownership tests:

- retaining a NamePool keeps an adoptable record valid after its original
  module owner releases its reference;
- a non-adoptable Input record is copied before runtime publication;
- parent and backing references release exactly once;
- a pinned global wrapper survives isolate teardown;
- destroying an Input does not invalidate global hits;
- isolate teardown releases every adopted non-global backing;
- repeated canonicalization from one backing does not leak retains.

### 4.5 P1.4 — Allocate every NamePool name as a NameRecord

Primary files:

- `lambda/core/name_pool.cpp`
- `lambda/core/name_identity.cpp`
- NamePool tests

Tasks:

1. Add one internal allocation routine that:

   - reserves prefix, current String image, characters, terminator if required
     by the current ABI, and alignment padding;
   - classifies the name;
   - writes `NameMeta`;
   - writes the current String ABI;
   - sets `is_pooled = 1` and `is_buffer = 0`;
   - returns the embedded `String*`.

2. Route all ordinary NamePool creation overloads through it.

3. Route all current `name_pool_create_symbol*()` paths through a prefix-backed
   allocation. Preserve their current content/length semantics, including
   non-interned long spellings, but do not let a long spelling fall back to a
   plain String. These helpers create Lambda symbol spellings, not unique
   ECMAScript SYMBOL property keys.

4. Preserve byte-length-aware interning, including embedded NUL if supported by
   the current APIs. Do not replace `StrView` keys with C-string-only keys.

5. Preserve ordinary STRING content interning:

   - same pool + same bytes returns the same reference;
   - global fallback returns the generated global reference;
   - explicit parents continue to share their canonical;
   - the new unique SYMBOL/PRIVATE property-key APIs bypass the spelling
     hashmap and are always unique, except well-known Symbol singleton records
     supplied by pool 2 later;
   - the legacy Lambda symbol-spelling helper retains its documented
     content-pooling behavior.

6. Add a debug verifier that walks NamePool entries and checks prefix, flags,
   stored view, cached hash, and key kind. Use it in tests and optional debug
   assertions, not in production hot paths.

### 4.6 P1.5 — Correct the ShapeEntry identity model

Primary files:

- `lambda/lambda-data.hpp`
- `lambda/core/shape_pool.cpp`
- `lambda/input/input.cpp`
- `lambda/runtime/lambda-eval.cpp`
- `lambda/runtime/build_ast.cpp`
- `lambda/ts/ts_type_builder.cpp`
- `lambda/runtime/re2_wrapper.cpp`
- `lambda/js/js_runtime.cpp`
- `lambda/js/js_property_attrs.cpp`
- every other allocation, clone, transition, and fixture found by `rg`

Tasks:

1. Rename the current FNV field:

   - `ShapeEntry.name_id` → `ShapeEntry.name_hash`;
   - `typemap_name_id()` → `typemap_name_hash()`;
   - `typemap_shape_entry_name_id()` →
     `typemap_shape_entry_name_hash()`;
   - rename local variables and comments that currently call the hash an ID.

2. Add:

   - `NameId predefined_id`;
   - optional `PropertyKeyRef key_ref`.

3. Define equality routing:

   - if both predefined IDs are non-zero, compare IDs;
   - for canonical JS/Lambda shapes, compare `key_ref`;
   - for Input/id-less STRING shapes, use hash + length + bytes;
   - never compare hash alone;
   - never derive SYMBOL/PRIVATE equality from bytes.

4. Update every constructor and clone before switching any reader:

   - initialize `predefined_id` from a pooled source name when available;
   - initialize `key_ref` only for runtime-canonical shapes;
   - Input/Mark explicitly sets `key_ref = NULL`;
   - clone/transition/copy operations preserve both fields.

5. Keep byte views in ShapeEntry for formatting, dynamic field names, and
   Input compatibility.

6. Add a temporary debug invariant on runtime-canonical JS shapes requiring a
   non-null `key_ref`, except at explicitly documented conversion boundaries.
   Remove broad fallback behavior rather than letting partially migrated shapes
   silently compare by bytes.

7. Complete an `rg` zero-check before the work package closes:

   - no `ShapeEntry.name_id`;
   - no helper whose `name_id` result is actually a hash;
   - no aggregate initializer omitting the new fields.

Tests:

- predefined-id equality;
- id-less Input hash/length/bytes equality;
- FNV collision fallback;
- clone and shape-transition field preservation;
- canonical SYMBOL/PRIVATE fields with identical display bytes remain distinct;
- mixed predefined/non-predefined lookup follows an explicitly tested boundary
  path rather than comparing `0 == 0`.

### 4.7 P1.6 — Preserve and expose the Input/Mark seam

Primary files:

- `lambda/input/input.cpp`
- `lambda/input/input.hpp`
- `lambda/io/mark_builder.hpp`
- `lambda/core/mark_reader.hpp`
- `lambda/lambda-data.hpp`
- Input/Mark tests

Tasks:

1. Keep `Input::create()` using a per-Input NamePool with visible
   `parent == NULL`.

2. In Input ShapeEntry allocation:

   - continue to copy the key bytes into Input-owned shape storage;
   - copy `name_ref_id(key)` to `predefined_id`;
   - copy the cached NameMeta hash to `name_hash`;
   - set `key_ref = NULL`.

3. Extend `TypeElmt` with `NameId name_id` while retaining `StrView name`.

4. Extend `ElementReader` with:

   - `tagId()`;
   - `hasTag(NameId)`;
   - `findChildElement(NameId)`;
   - ID-aware attribute/field helpers where a ShapeEntry is already available.

5. Retain the existing byte/string overloads for arbitrary document data and
   external callers.

6. Add centralized Mark construction overloads that Phase 2 can use:

   - `MarkBuilder::createName(NameId)`;
   - `MarkBuilder::element(NameId)`;
   - `ElementBuilder::attr(NameId, ...)`;
   - `MapBuilder::put(NameId, ...)`.

   These overloads may resolve a generated name internally. Callers must not
   materialize a NameRef only to compare it.

7. Ensure content strings, values, text nodes, binary data, and Lambda
   `Symbol*` values retain their current arena/GC ownership and do not become
   NameRecords accidentally.

8. Verify all input adapters that depend on NamePool:

   - JSON and JSON-like maps;
   - XML/HTML;
   - CSS;
   - Markdown/CommonMark;
   - YAML;
   - TOML/CSV and other tabular inputs;
   - LaTeX/ASCII math;
   - PDF or any parser that creates Mark names indirectly.

### 4.8 Phase 1 tests and exit gate

Required unit coverage:

- existing NamePool interning, hierarchy, lookup, empty/null, and count tests;
- NameRecord prefix layout and flags;
- ordinary classifier and cached hash;
- unique key construction;
- backing-retain/adoption lifecycle;
- ShapeEntry rename and three equality routes;
- MarkBuilder/ElementBuilder name creation;
- TypeElmt and ElementReader ID/byte coexistence;
- Input destruction and global-fallback lifetime.

Required commands, adjusted to the repository's actual built target names:

```bash
make build-test
./test/test_name_pool_gtest.exe
./test/test_input_roundtrip_gtest.exe
./test/test_html_gtest.exe
./test/test_html_css_gtest.exe
make test-input-baseline
make test-lambda-baseline
make check-module-boundary
```

Phase 1 exits only when:

- every NamePool-returned String is verified pooled;
- `is_buffer && is_pooled` is impossible;
- no current FNV hash is named `NameId`;
- every Input parser baseline is unchanged or an independently documented
  pre-existing failure;
- Input ShapeEntries have `key_ref == NULL`;
- no isolate registry can retain a pointer without retaining or owning its
  backing;
- no persistent MIR-cache or SectionNameId representation has been introduced.

---

## 5. Phase 2 — Generate global pools and migrate markup/Radiant

### 5.1 Phase 2 result

At the end of Phase 2:

- the repository has one hand-maintained Python name catalog and one generator;
- pools 0, 1, and 2 are generated and registered;
- generated enums are compile-time `NameId` constants;
- HTML/CSS/SVG parsing recognizes a predefined name once and carries the ID;
- Radiant compares tag, attribute, CSS, and presentation names directly by ID;
- dynamic custom elements, attributes, CSS properties, and syntax remain
  supported by bytes with `NAME_ID_NONE`;
- the handwritten HTML tag identity table is removed;
- CSS's dense property dispatch code is explicitly separate from `NameId`.

### 5.2 P2.1 — Define the catalog source schema and census names

New file:

- `utils/well_known_names_data.py`

Tasks:

1. Make the Python data module the only hand-maintained spelling list.

2. Define records with enough information to generate:

   - owning pool;
   - symbolic enum name;
   - canonical byte spelling;
   - key kind;
   - cross-pool aliases;
   - input aliases/case-normalized spellings where format rules require them;
   - optional categories used to generate HTML/SVG predicates;
   - optional CSS dense dispatch-code mapping;
   - optional well-known Symbol designation.

3. Populate initial pool domains:

   - pool 0: HTML tags/attributes, CSS properties/keywords/at-rules needed by
     semantic routing, SVG tags/attributes and case fixups, XML/MathML and
     other presentation names used by Input/Radiant;
   - pool 1: Lambda type names, system/builtin function names, and predefined
     structural field names;
   - pool 2: JavaScript globals, constructors, prototype methods, special
     properties, DOM API member names, and well-known Symbols.

4. Resolve duplicates deliberately:

   - choose one owning record, with default physical ownership precedence
     pool 0, then pool 1, then pool 2;
   - emit aliases in later domain headers with the same numeric NameId;
   - fail generation if duplicate bytes are declared without an ownership or
     alias decision;
   - do not allocate two physical STRING records for the same global spelling.

5. Treat the current behavior tables as behavior authorities, not additional
   spelling authorities:

   - CSS property definitions retain parsing/cascade metadata;
   - Lambda system-function definitions retain signatures and function IDs;
   - JS builtin catalogs retain builtin IDs, arity, flags, and native entry
     points;
   - their name columns are changed to generated enum references.

6. Classify census matches:

   - semantic predefined name: add to catalog and migrate;
   - dynamic/custom name: keep bytes and `NAME_ID_NONE`;
   - raw syntax token: keep token comparison;
   - output spelling only: use view/materialization API;
   - diagnostic-only text: no identity conversion.

### 5.3 P2.2 — Implement deterministic generation

New file:

- `utils/generate_well_known_names.py`

Generated files:

- `lambda/core/well_known_markup_names.h`
- `lambda/core/well_known_markup_names.c`
- `lambda/core/well_known_lambda_names.h`
- `lambda/core/well_known_lambda_names.c`
- `lambda/js/js_well_known_names.h`
- `lambda/js/js_well_known_names.c`

Tasks:

1. Validate pool numbers are exactly 0, 1, and 2.

2. Assign ordinals deterministically from declared source order, beginning at
   1. A catalog edit may regenerate later IDs during beta.

3. Emit C-compatible enum headers with explicit values:

   - `MarkupNameId` / `MARKUP_NAME_*`;
   - `LambdaNameId` / `LAMBDA_NAME_*`;
   - `JsNameId` / `JS_NAME_*` and `JS_SYMBOL_*`.

4. Emit 8-byte-aligned immutable NameRecord byte images.

5. Emit per-pool ordinal-to-record-offset tables and versioned `NamePoolData`
   descriptors.

6. Emit a generated catalog identity/fingerprint used to reject incompatible
   generated or cached artifacts. Persistent cache wiring remains future work.

7. Generate lookup data for `well_known_name_id(StrView)` without runtime
   mutable initialization. Validate the chosen lookup representation for:

   - exact bytes and length;
   - duplicate/alias handling;
   - no `strcmp` dependence on a terminator;
   - deterministic output on all build platforms.

8. Generate optional category/mapping tables instead of relying on enum ranges:

   - HTML parser scope/category predicates;
   - SVG spelling fixup mapping;
   - CSS `NameId` ↔ dense `CssPropertyCode`.

9. Add `--check` mode that generates in memory or in `./temp/`, compares
   checked-in outputs, and exits non-zero on drift. Do not write to `/tmp`.

10. Add `make generate-names` and a CI/check target through
    `build_lambda_config.json` or the proper hand-maintained Make entry. Do not
    edit generated Lua files manually.

Generator tests:

- all enum IDs decode to their expected pool and ordinal;
- each ordinal table resolves to an aligned embedded String;
- every record has `is_pooled`, correct metadata, and correct characters;
- aliases resolve to the owner's exact NameId and record pointer;
- duplicate enum names, duplicate owners, invalid UTF-8 policy, overflow, and
  more than 65,535 names fail clearly;
- two generator runs are byte-identical;
- `--check` detects a deliberately stale fixture.

### 5.4 P2.3 — Register pools and expose lookup/materialization APIs

Primary files:

- `lambda/core/name_pool.hpp`
- `lambda/core/name_pool.cpp`
- `lambda/core/name_identity.h`
- generated pool headers/data
- process/runtime initialization and shutdown entry points

Tasks:

1. Wrap all three `NamePoolData` descriptors in pinned immutable NamePools once
   during process initialization.

2. Establish the global lookup hierarchy:

   - pool 2;
   - pool 1;
   - pool 0.

   This lookup hierarchy does not change the generator's pool-0 → pool-1 →
   pool-2 default physical ownership precedence. Alias resolution must return
   the one owning physical record.

3. Implement:

   - `well_known_name_id(StrView)`;
   - `well_known_name_view(NameId)`;
   - `well_known_name_ref(NameId)`;
   - `well_known_key_ref(NameId)`;
   - `name_ref_id(NameRef)`;
   - `property_key_id(PropertyKeyRef)`.

4. Validate pool and ordinal on every non-hot public decode. Invalid IDs must
   return a safe failure (`NONE`/empty/null as appropriate) and log a distinct
   diagnostic where corruption is actionable.

5. Ensure direct enum equality remains header-only integer comparison. Do not
   hide equality behind a function.

6. Make ordinary NamePool creation consult generated data first. A global hit
   returns the pinned record and does not allocate in the caller's pool.

7. Test multi-isolate use. Global records are shared; dynamic registry records
   are not shared between isolates.

### 5.5 P2.4 — Migrate HTML and SVG parsing

Primary areas:

- `lambda/input/html5/html5_token.h`
- `lambda/input/html5/html5_tokenizer.cpp`
- `lambda/input/html5/html5_parser.cpp`
- `lambda/input/html5/html5_tree_builder.cpp`
- HTML/SVG tests and fixtures

Tasks:

1. Add `NameId tag_id` to the HTML token representation while retaining
   `String* tag_name` or a byte view where tokenizer output/diagnostics need it.

2. At the tokenizer's tag-name commit boundary:

   - finish HTML case normalization;
   - intern through NamePool;
   - copy the generated ID once;
   - carry both ID and bytes only when both are needed.

3. Attribute names are structural names, not content Strings. Change attribute
   commit from `createString` to `createName`; values remain content Strings.
   ShapeEntry then receives `predefined_id` automatically.

4. Replace semantic tag-name chains in the tree builder with:

   - direct `NameId` comparisons;
   - generated switch/table category predicates for “special,” “scoping,”
     “formatting,” table, foreign-content, and related tag sets.

5. Do not use numeric enum ranges for categories. Generated ordinal ordering
   has no semantic meaning.

6. Replace SVG tag/attribute adjustment tables with generated alias/canonical
   spelling data where practical. Recognition produces the canonical NameId;
   output retains the required canonical SVG byte spelling.

7. Keep raw syntax checks as bytes—for example tokenizer state delimiters,
   `CDATA[` recognition, and XML processing-instruction syntax.

8. Carry `TypeElmt.name_id` through the open-element stack, active-formatting
   list, scope checks, and current-node helpers.

9. Add custom-element and unknown-foreign-name tests proving
   `NAME_ID_NONE + bytes` remains functional.

### 5.6 P2.5 — Replace handwritten DOM/Radiant tag identity

Primary files:

- `lambda/input/css/dom_node.cpp`
- `lambda/input/css/dom_element.hpp`
- `lambda/input/css/dom_element.cpp`
- `radiant/view.hpp`
- Radiant layout, style, event, form, accessibility, and render sources found
  by the census

Tasks:

1. Change `DomElement::tag_id` from `uintptr_t`/handwritten tag code to
   `NameId`.

2. Retain tag bytes for:

   - custom elements;
   - unknown foreign elements;
   - serialization;
   - diagnostics and external APIs.

3. Remove the duplicated `HtmlElementInfo` spelling table and runtime tag
   hashmap after all consumers use generated lookup.

4. Remove `HTM_TAG_*` as an independent identity enum. During migration, a
   generated compatibility alias may keep a small buildable change set, but it
   must map exactly to `MARKUP_NAME_*` and be removed before Phase 2 exits.

5. Replace all Radiant semantic tag comparisons with direct
   `MARKUP_NAME_*` equality.

6. Replace every old tag-range test with a generated category predicate or an
   explicit switch. Never depend on generated enum ordering.

7. Add internal DOM attribute overloads:

   - `get_attribute(NameId)`;
   - `has_attribute(NameId)`;
   - `set_attribute(NameId, ...)`;
   - `remove_attribute(NameId)`.

8. Migrate predefined Radiant attribute checks such as `href`, `src`, `open`,
   form state, namespace, accessibility, and SVG attributes to those overloads.
   Retain byte overloads for dynamic and public APIs.

9. In SVG rendering, obtain the element/attribute ID once and use it for
   semantic branching. Use `well_known_name_view()` only where bytes are
   emitted.

10. Update selector matching caches to store/prefer `NameId` for predefined
    tags and attributes, with byte handling for dynamic names.

11. Run the Radiant integer-layout lint after touched layout files, even though
    this migration should not introduce dimensions or casts.

### 5.7 P2.6 — Separate CSS identity from dense dispatch

Primary files:

- `lambda/input/css/css_style.hpp`
- `lambda/input/css/css_properties.cpp`
- CSS parser/token/declaration/cascade files
- Radiant CSS resolution files

The existing sequential `CssPropertyId` is densely indexed by arrays and
switches. A generated `NameId` is sparse and must not replace it mechanically.

Tasks:

1. Rename the dense concept to `CssPropertyCode` and rename APIs/fields so code
   versus identity is unambiguous. Compatibility aliases may be staged but
   removed from public internal APIs before the phase exits.

2. Add `NameId name_id` to standard CSS property definitions and parsed
   declarations.

3. Generate the mapping:

   - standard CSS property NameId → dense property code;
   - dense property code → standard CSS property NameId.

4. At the property-name normalization boundary:

   - resolve the generated NameId once;
   - map a standard ID to dense code;
   - retain raw bytes for custom `--*`, unknown, vendor, or diagnostics;
   - use `NAME_ID_NONE` when the spelling is not catalogued.

5. Keep cascade arrays, style storage, and large dispatch switches keyed by
   dense `CssPropertyCode`.

6. Use `NameId` when code is asking “is this predefined property/name?”.
   Never expose the dense code as a universal identity.

7. Migrate semantic CSS keyword, pseudo, at-rule, and namespace comparisons
   where they are in the pool-0 catalog. Keep lexical token recognition and
   custom identifiers byte-based.

8. Preserve original spelling only where CSSOM, custom properties,
   serialization, error reporting, or source fidelity requires it.

9. Add exhaustive generated mapping tests:

   - every standard property maps both ways;
   - no two property IDs map to one dense code accidentally;
   - custom properties have `NAME_ID_NONE` and no standard code;
   - aliases/vendor spellings follow explicitly declared behavior.

### 5.8 P2.7 — Migrate native readers, builders, and formatters

Tasks:

1. Convert centralized construction of predefined elements/fields to
   `element(NameId)`, `attr(NameId, ...)`, and `put(NameId, ...)`.

2. Convert semantic native reads to `tagId()` and ShapeEntry
   `predefined_id`.

3. Keep dynamic string overloads for user-provided document keys.

4. For formatters:

   - serialize retained byte views when source/custom spelling matters;
   - use `well_known_name_view()` for generated predefined spelling;
   - do not call `well_known_name_ref()` merely to obtain output bytes.

5. Audit non-JS `well_known_name_ref()` calls. Each surviving call must be a
   centralized construction/API bridge that requires a `String*`, not an
   equality test.

Expected non-JS uses after this phase are limited to:

- NamePool/MarkBuilder central materialization;
- an ABI that explicitly accepts a NameRef;
- Lambda runtime linking in Phase 3.

### 5.9 Phase 2 tests and exit gate

Required checks:

```bash
python3 utils/generate_well_known_names.py --check
make build-test
./test/test_name_pool_gtest.exe
./test/test_html_gtest.exe
./test/test_html_css_gtest.exe
./test/test_input_roundtrip_gtest.exe
make test-input-baseline
make test-radiant-baseline
make test-lambda-baseline
make lint ARGS='--rule ^no-int-cast-radiant$'
make check-module-boundary
```

Add focused tests for:

- all generated records and aliases;
- HTML parser category behavior before/after conversion;
- SVG case-adjusted tag and attribute names;
- custom elements and unknown attributes;
- CSS standard property mapping and custom properties;
- DOM attribute ID overloads;
- Radiant behavior for representative HTML, SVG, form, link, table, and
  replaced-element paths;
- format round trips preserving required spelling.

Phase 2 exits only when:

- `--check` reports no generated drift;
- no handwritten `HTM_TAG_*` identity remains;
- no independent HTML tag spelling hashmap remains;
- standard CSS identity is `NameId` and dense CSS routing is explicitly
  `CssPropertyCode`;
- native predefined-name equality does not call
  `well_known_name_ref()` or use `strcmp`;
- all pool-0/1/2 enum constants are generated;
- pool 1 and pool 2 may be populated before Phase 3 even if their consumers are
  not yet fully migrated;
- markup/Radiant baselines pass.

---

## 6. Phase 3 — Migrate Lambda and JavaScript

### 6.1 Phase 3 result

At the end of Phase 3:

- Lambda and JavaScript ordinary property keys use canonical STRING
  `PropertyKeyRef`s;
- SYMBOL and PRIVATE keys use unique PropertyKeyRefs, not encoded spellings;
- generated pool-1 and pool-2 IDs identify predefined language/runtime names;
- MIR Direct sites load runtime-linked refs from a dense module property-key
  table instead of baking compiler-owned String pointers;
- builtin dispatch uses numeric builtin IDs after recognition;
- constructor classification uses `ctor_id`, not name chains;
- per-runtime well-known ref tables resolve generated IDs once;
- dynamic modules/eval/new Function use the same identity path;
- persistent MIR-cache serialization remains explicitly deferred.

### 6.2 P3.1 — Add a transient module property-key link table

Primary files:

- `lambda/runtime/transpiler.hpp`
- `lambda/runtime/transpile-mir.cpp`
- `lambda/lambda-data.hpp`
- `lambda/lambda.h`
- module layout/state/link/teardown sources
- equivalent JS MIR module structures

Introduce an in-memory, non-persistent representation:

1. `PropertyKeySpec` describes a compiler-known key as either:

   - generated predefined `NameId`; or
   - id-less ordinary STRING candidate bytes/NameRef plus an explicitly retained
     backing owner.

2. The compiler deduplicates specs by:

   - non-zero NameId for predefined names;
   - bytes for ordinary id-less STRING names.

3. `LambdaModuleLayout` records `property_key_count`.

4. `LambdaModuleState` owns `PropertyKeyRef* property_keys`.

5. At module activation/link for each runtime/isolate:

   - predefined spec → `well_known_key_ref(NameId)`;
   - ordinary spec → isolate NamePool canonicalization/adoption;
   - retain an adoptable backing or copy from a non-adoptable backing;
   - store the definitive ref in `property_keys[index]`.

6. MIR Direct bakes only the dense table index. It loads
   `modstate->property_keys[index]` at the property operation.

7. Module teardown releases its link table and owner references. The isolate
   registry may continue retaining adopted canonical backing until isolate
   teardown.

8. Re-link the table for each isolate and for any L1 compiled-module reuse.
   Never reuse PropertyKeyRef addresses across isolates for dynamic names.

9. Add a catalog identity field to the in-memory compiled unit if useful for
   assertions, but do not define a persistent cache record.

10. Add an explicit TODO at the module-key serialization boundary referring to
    the future MIR-cache reconciliation. Do not leave scattered TODOs on every
    call site.

Tests:

- predefined and dynamic specs deduplicate correctly;
- the same compiled module links independently in two isolates;
- an id-less compiler NamePool can be released after link without dangling
  runtime refs;
- L1 reuse rebuilds the table;
- no MIR constant contains a compiler-owned `String*` for migrated key sites.

### 6.3 P3.2 — Migrate Lambda compile-time name recognition

Primary files:

- `lambda/runtime/build_ast.cpp`
- Lambda scope/type/system-function registry files
- `lambda/runtime/sys_func_registry.h`
- `lambda/runtime/sys_func_registry.c`
- `lambda/runtime/transpile-mir.cpp`

Tasks:

1. Add generated `LambdaNameId` values to predefined type, builtin/system
   function, and structural-name records.

2. Keep behavior metadata in the existing registry. Replace duplicated
   spelling literals with generated enum references and obtain a view only
   where diagnostics or source lookup needs bytes.

3. Change system-function recognition:

   - raw parser name is interned once;
   - generated `NameId` selects a predefined registry entry;
   - dynamic Jube/external functions retain content lookup and
     `NAME_ID_NONE`.

4. Carry the existing numeric builtin/system-function ID through AST and MIR
   lowering. Do not convert ID → name → lookup again at runtime.

5. Migrate scope lookups to canonical NameRef/PropertyKeyRef equality after the
   parser boundary has interned the incoming name. Retain byte fallback only
   for an explicitly documented non-interned API boundary.

6. Add property-key specs for static member names and lower them through the
   module table.

7. Update Lambda member IC structures/helpers to accept PropertyKeyRef and use
   cached metadata:

   - pointer equality for canonical shapes;
   - cached hash for table routing;
   - cached array index where the operation needs it.

8. Do not migrate raw grammar/operator token recognition to NameId. Tokens are
   syntax until converted to semantic names.

9. Do not add support to `lambda/runtime/transpile.cpp`; it belongs to frozen
   C2MIR.

### 6.4 P3.3 — Establish per-runtime JS well-known refs

Primary files:

- `lambda/js/js_runtime_state.hpp`
- `lambda/js/js_runtime.cpp`
- `lambda/js/js_builtin_catalog.def`
- `lambda/js/js_builtin_catalog.hpp`
- generated pool-2 header/data

Tasks:

1. Add a per-runtime/realm `JsWellKnownRefs` table.

2. During JS runtime initialization, resolve the generated pool-2 IDs once and
   store PropertyKeyRefs for frequently used names:

   - `length`, `name`, `prototype`, `constructor`;
   - iterator and other well-known Symbol keys;
   - globals, standard methods, special internal/public properties;
   - DOM member names used by the JS bridge.

3. Runtime hot paths compare against `js_wk.<field>` by pointer. They do not
   repeatedly call a resolver or compare bytes.

4. Keep `js_builtin_catalog.def` as the behavior catalog, but replace visible
   spelling literals with generated `JS_NAME_*` references. Its generated/spec
   structures carry `NameId`, resolved ref, and existing builtin metadata.

5. Ensure no mutable process-global table stores isolate-specific dynamic refs.
   Generated global refs may be shared, but the runtime table remains attached
   to runtime state for consistent access and future realm-specific behavior.

6. Add initialization completeness checks so a missing generated catalog entry
   fails at startup/test time, not through a null hot-path comparison.

### 6.5 P3.4 — Convert ordinary JS property operations and shapes

Primary files:

- `lambda/js/js_runtime.h`
- `lambda/js/js_runtime.cpp`
- `lambda/js/js_property_attrs.cpp`
- `lambda/js/js_mir_internal.hpp`
- JS MIR lowering and IC files

Tasks:

1. Add internal PropertyKeyRef entry points for:

   - get;
   - set;
   - define;
   - delete;
   - has/own;
   - descriptor lookup;
   - prototype-chain lookup;
   - enumeration bookkeeping where a key identity is needed.

2. Keep Item/public entry points as conversion boundaries. They perform
   `ToPropertyKey` once, then call the ref-based operation.

3. Change `JsLoadIC` and `JsStoreIC`:

   - remove borrowed `const char*`/length identity;
   - store/use PropertyKeyRef;
   - use NameMeta cached hash/array index;
   - compare the definitive key by pointer.

4. Change JS member-ref/MIR structures to carry a module property-key table
   index, not a baked character pointer or compiler-owned Item.

5. Canonical JS ShapeEntries always set `key_ref`. Remove `memcmp` fallback from
   the canonical property path once all constructors and transitions comply.

6. Intern computed STRING property keys in the isolate NamePool once. Reuse the
   canonical ref across IC, shape, descriptor, proxy, and prototype operations.

7. Preserve observable property ordering:

   - ordinary array-index ordering uses cached `array_index`;
   - other strings preserve insertion order;
   - Symbols remain in their specified ordering domain;
   - PRIVATE keys are not exposed by reflection/enumeration.

8. Keep string materialization lazy/centralized where a public JS API requires
   a String value, such as `Reflect.ownKeys`, property descriptors, error
   messages, or proxy trap arguments.

9. Remove long `length + strncmp` predefined-property chains after equivalent
   `js_wk` dispatch is tested.

### 6.6 P3.5 — Make SYMBOL keys semantic

Primary files:

- JS Symbol registry/runtime files
- property conversion and reflection files
- JS tests

Tasks:

1. Retain the current observable JS Symbol Item representation initially if
   changing it is unnecessary.

2. Add a per-runtime Symbol registry mapping each Symbol identity to one
   SYMBOL-kind PropertyKeyRef.

3. Symbol creation allocates a unique record even when descriptions match or
   are empty.

4. `Symbol.for(key)` returns/reuses the registered Symbol and its same
   PropertyKeyRef.

5. Well-known Symbols use generated singleton pool-2 records.

6. `ToPropertyKey(Symbol)` retrieves the registry ref. It never synthesizes
   `__sym_N`.

7. Remove `__sym_` prefix generation, parsing, and routing after all property,
   proxy, descriptor, and reflection paths consume PropertyKeyRef.

8. Ensure the ordinary string `"__sym_1"` remains a normal STRING key and
   cannot alias any Symbol.

9. Materialize a Symbol description only for user-visible description/string
   operations; description bytes are not identity.

### 6.7 P3.6 — Make PRIVATE keys semantic

Primary files:

- JS parser/lowering private-name environment
- class evaluation/runtime helpers
- property/brand-check operations

Tasks:

1. Create a fresh PRIVATE PropertyKeyRef for each private-name binding during
   class evaluation, not merely during source parsing.

2. Store refs in the runtime private environment. Methods, fields, accessors,
   initializers, and brand checks for one evaluated class share those refs.

3. Re-evaluating the same class source creates fresh refs.

4. Pass PRIVATE refs through the same property operations and shapes as other
   PropertyKeyRefs, with private visibility rules enforced at the semantic
   boundary.

5. Remove `__private_<class-index>_` spelling generation, prefix parsing, and
   spelling-based brand checks.

6. PRIVATE display bytes are diagnostic only. They must never be exposed as a
   public property key or used for equality.

### 6.8 P3.7 — Complete builtin and constructor routing cleanups

Tasks:

1. Once a generated name/ref selects a JS builtin, carry the existing
   `JsBuiltinId` through MIR and runtime dispatch.

2. Eliminate builtin ID → string → catalog lookup round trips and any
   per-call temporary allocation.

3. Add or complete `ctor_id` on constructor/function metadata.

4. Stamp `ctor_id` when standard and user constructors are created.

5. Replace constructor-name chains with `ctor_id` dispatch. Function `.name`
   remains observable metadata and must not decide semantics.

6. Verify renamed/bound/proxied constructors follow the language's existing
   semantic rules without relying on display names.

### 6.9 P3.8 — Migrate the JS/DOM boundary

Primary files:

- JS DOM bridge sources
- `lambda/module/radiant/radiant_dom_bridge.cpp`
- `lambda/jube/jube_interface.cpp`
- DOM wrapper/member descriptor sources

Tasks:

1. Key JS-visible DOM member records by pool-2 PropertyKeyRef.

2. Resolve generated DOM member NameIds once into `js_wk`, then use pointer
   comparison and existing receiver/type dispatch.

3. When JS accesses native markup attributes/tags:

   - use PropertyKeyRef in the JS object/property layer;
   - map known markup spellings to pool-0 `NameId` at the centralized bridge;
   - use native `NameId` DOM overloads;
   - keep dynamic/custom attributes byte-based.

4. Public values such as `tagName`, `localName`, attribute names, and reflected
   property keys materialize Strings only at the JS-facing boundary.

5. Do not make Radiant compare pool-2 PropertyKeyRefs. Its native identity
   remains pool-0 `NameId`.

### 6.10 P3.9 — Cover eval, modules, dynamic compilation, and teardown

Tasks:

1. Route regular scripts, modules, `eval`, `new Function`, REPL compilation,
   and required/imported modules through the same property-key spec/link path.

2. Make deferred JS MIR compilation retain the compiler NamePool backing
   contract, not merely the NamePool control-block counter.

3. Audit existing paired retention of JS transpiler NamePool and AST Pool.
   Replace it with the explicit backing-owner contract or preserve the pair
   behind one safe owner abstraction.

4. Ensure runtime/module teardown order is:

   - stop execution;
   - release module key tables/module backing refs;
   - release module state;
   - release isolate registry/adopted backings;
   - release the runtime dynamic NamePool.

5. Verify error paths and partial compilation perform the same releases.

6. Rebuild property-key tables when compiled MIR is reused in a new runtime.

7. Add one clearly scoped future note:

   > Persistent encoding of non-global property-key specs, SectionNameId
   > relocation, global-catalog versioning, and cached MIR relinking will be
   > defined by the future reconciliation with the MIR-cache design.

### 6.11 Phase 3 tests and exit gate

Lambda coverage:

- predefined type and system-function recognition by generated ID;
- dynamic/Jube function names still work;
- scope canonicalization;
- static and computed member access;
- module/eval/reuse property-key linking;
- Lambda member IC pointer-key behavior.

JavaScript coverage:

- ordinary named and computed property get/set/delete/has/define;
- prototype lookup and shape transitions;
- proxy traps and property descriptor APIs;
- enumeration and `Reflect.ownKeys`;
- array index boundary/order behavior;
- TypedArray canonical-numeric behavior remains correct;
- two Symbols with the same description are unequal;
- `Symbol.for` is stable;
- well-known Symbols are singleton identities;
- string `"__sym_N"` never aliases a Symbol;
- same private spelling in different classes is distinct;
- repeated evaluation of the same class source gets fresh private keys;
- private brand checks and accessors;
- standard builtin dispatch;
- constructor dispatch independent of function display name;
- DOM named members and dynamic attributes;
- multiple runtimes/isolates and module reuse;
- failure/teardown ownership paths.

Required gates:

```bash
make build-test
make test-lambda-baseline
make test262-baseline
make node-baseline
make test-radiant-baseline
make check-module-boundary
python3 utils/generate_well_known_names.py --check
```

Run any repository-specific DOM/UI suites discovered during the census.

Performance validation must use a release build:

```bash
make release
```

Measure representative property-access, HTML/CSS parsing, and Radiant workloads.
The acceptance target is structural first:

- no repeated well-known-name resolution in hot loops;
- no predefined-property `memcmp`/`strncmp` chains;
- no temporary string allocation for builtin dispatch;
- no baked compiler-owned name pointers in MIR;
- no Symbol/private pseudo-name allocation;
- no material regression in release benchmarks.

Phase 3 exits only when:

- every internal JS property operation receives a PropertyKeyRef after one
  conversion boundary;
- canonical JS shapes require and compare `key_ref`;
- ordinary STRING keys canonicalize by bytes per isolate;
- SYMBOL and PRIVATE keys compare only by reference;
- builtins and constructors use numeric IDs after recognition;
- all dynamic compilation modes link their property-key table;
- teardown and cross-isolate tests show no dangling backing or leaked retain;
- the legacy C2MIR path is untouched;
- persistent MIR-cache reconciliation remains future work.

---

## 7. Proposed implementation sequence

Keep each numbered item independently reviewable and buildable where practical.

1. Add core identity types, NameMeta, String flag, and ABI assertions.
2. Add classifier and NameRecord allocation tests.
3. Convert every existing NamePool creation path to NameRecord.
4. Add explicit NamePool backing ownership and lifecycle tests.
5. Add unique SYMBOL/PRIVATE creation APIs.
6. Rename ShapeEntry hash fields and initialize the new fields everywhere.
7. Add TypeElmt/MarkBuilder/ElementReader ID-aware APIs.
8. Run the full Phase 1 Input gate.
9. Add Python source data and generator with fixture tests.
10. Generate and register pools 0/1/2; add global resolver tests.
11. Migrate HTML token names, attributes, and tree-builder category checks.
12. Migrate SVG normalization and semantic checks.
13. Change DomElement/Radiant to generated markup NameIds; remove old tag
    identity.
14. Split CSS `NameId` from dense `CssPropertyCode` and migrate CSS/Radiant.
15. Migrate remaining native builders/readers/formatters; run Phase 2 gate.
16. Add transient module PropertyKeySpec and runtime link table.
17. Migrate Lambda predefined names, scopes, members, and builtin-ID lowering.
18. Add per-runtime JS well-known refs and convert builtin catalogs.
19. Convert ordinary JS property operations, shapes, and ICs.
20. Convert Symbol registry/property keys and remove `__sym_` routing.
21. Convert private environments/brand checks and remove `__private_` routing.
22. Complete constructor-ID and builtin-ID cleanup.
23. Convert DOM bridge and all dynamic compilation/link/teardown paths.
24. Run Phase 3 correctness, ownership, cross-isolate, and release-performance
    gates.

Avoid a single repository-wide mechanical replacement for steps 11–14 or
18–23. Each area needs semantic review to distinguish identity from parsing,
dispatch code, output spelling, and diagnostics.

---

## 8. File-level migration checklist

The exact census must be regenerated immediately before each phase, but this is
the minimum expected surface.

| Area | Expected files/directories | Required result |
|---|---|---|
| String ABI | `lib/string.h`, `lib/string.c`, `lambda/lambda.h` | `is_pooled`, preserved ABI, mutually exclusive ownership flags |
| Identity core | new `lambda/core/name_identity.*` | NameMeta, classifier, checked accessors, ID/ref types |
| NamePool | `lambda/core/name_pool.*`, `mem_factory_core.cpp` | prefix allocation, backing modes, global fallback, unique refs |
| Shapes | `lambda/lambda-data.hpp`, `lambda/core/shape_pool.cpp`, all ShapeEntry users | `name_hash`, `predefined_id`, `key_ref` |
| Input/Mark | `lambda/input/`, `lambda/io/mark_builder.hpp`, `lambda/core/mark_reader.hpp` | per-Input compatibility, ID-aware centralized APIs |
| Generator | `utils/well_known_names_data.py`, `generate_well_known_names.py` | deterministic pools, headers, mappings, fingerprint, `--check` |
| Generated pool 0 | `lambda/core/well_known_markup_names.*` | markup/CSS/SVG records and enum |
| Generated pool 1 | `lambda/core/well_known_lambda_names.*` | Lambda records and enum |
| Generated pool 2 | `lambda/js/js_well_known_names.*` | JS/DOM records, names, Symbols, enum |
| HTML/SVG | `lambda/input/html5/` | tag/attribute IDs carried from recognition |
| CSS | `lambda/input/css/` | sparse NameId identity + dense CssPropertyCode |
| Radiant | `radiant/` | direct NameId tag/attribute comparisons |
| Lambda | `lambda/runtime/build_ast.cpp`, `transpile-mir.cpp`, registries | generated IDs, key link table, builtin IDs |
| JS | `lambda/js/` | PropertyKeyRef operations, shapes, ICs, symbols/private |
| DOM bridge | JS/Radiant bridge and Jube interface files | pool-2 refs at JS boundary, pool-0 IDs natively |
| Build | `build_lambda_config.json`, hand-maintained Make entry | generated sources/targets without editing generated Lua |
| Tests | `test/`, `test/lambda/`, JS/DOM/layout suites | unit, baseline, ownership, and identity coverage |

---

## 9. Cross-phase invariants and audit queries

Use `rg` at each exit gate. Adjust patterns for renamed files, but verify the
meaning of every remaining match.

### Phase 1 audits

- `ShapeEntry.name_id` and hash helpers still named as IDs;
- NamePool paths calling a plain String allocator directly;
- `is_buffer`/`is_pooled` assignments and raw String copies;
- ShapeEntry aggregate initializers;
- independent destruction of a retained NamePool backing.

### Phase 2 audits

- `HTM_TAG_`;
- duplicated HTML/SVG tag-name tables;
- semantic tag/attribute `strcmp`/`strncmp`;
- uses of CSS dense codes named `*Id`;
- native equality sites calling `well_known_name_ref`;
- hand-maintained predefined spelling literals outside the Python data source.

### Phase 3 audits

- `__sym_` and `__private_` semantic routing;
- JS property ICs storing `char*` + length as identity;
- MIR constants containing compiler-owned member-name pointers;
- special-property length/`strncmp` chains;
- constructor behavior selected by function name;
- builtin ID → name → lookup paths;
- canonical JS ShapeEntries without `key_ref`;
- runtime code comparing SYMBOL/PRIVATE bytes.

Some matches may remain in tests that assert old spellings do not have special
meaning, in migration documentation, or in raw syntax/output code. Each
survivor should be classified explicitly.

---

## 10. Main risks and controls

### 10.1 Backing lifetime

**Risk:** a canonical ref outlives the Pool that contains its NameRecord.

**Control:** explicit retainable backing descriptors; non-adoptable candidates
are copied; ownership stress tests destroy module/Input owners immediately
after linking.

### 10.2 ABI drift

**Risk:** `lib/string.h`, `lambda/lambda.h`, generator layout, and runtime
metadata recovery disagree.

**Control:** shared constants, C/C++ compile-time assertions, generated-record
round-trip tests, and one classifier/layout implementation used by generator
fixtures and runtime.

### 10.3 Partial ShapeEntry migration

**Risk:** some shapes compare hash, some bytes, and some pointer, causing false
hits or misses.

**Control:** initialize all writers before readers, debug invariants by shape
domain, collision tests, and removal of ambiguously named `name_id` hash APIs.

### 10.4 Treating generated IDs as dense/orderable

**Risk:** HTML category ranges or CSS arrays accidentally rely on ordinal
layout.

**Control:** generated category predicates and explicit NameId↔dense-code
mappings; tests reorder a fixture catalog and verify semantics.

### 10.5 Breaking dynamic document names

**Risk:** custom elements, custom properties, unknown XML/SVG names, or
user-defined fields become unrepresentable.

**Control:** retain bytes and `NAME_ID_NONE`; preserve byte overloads; test
custom/unknown cases in every migrated parser.

### 10.6 Symbol/private spelling leakage

**Risk:** legacy pseudo-name strings continue to alias or leak into reflection.

**Control:** central ToPropertyKey conversion, unique semantic registries,
negative collision tests using literal `"__sym_N"`/`"__private_*"` strings,
and a zero-audit of prefix routing.

### 10.7 Cross-isolate pointer reuse

**Risk:** dynamic PropertyKeyRefs from one runtime are reused in another through
compiled-module caching.

**Control:** MIR stores dense indices, not pointers; each runtime links its own
table; global refs alone are process-shared.

### 10.8 Scope expansion into persistent cache

**Risk:** transient linking choices accidentally become an undocumented cache
format.

**Control:** keep `PropertyKeySpec` explicitly in-memory, place one future-work
marker at the serialization boundary, and defer SectionNameId/cache layout to a
separate reconciled design.

---

## 11. Definition of done

The name identity design is implemented when all of the following are true:

1. NamePool is still the name-management abstraction used by Input, compilers,
   and runtimes.
2. Every NamePool-returned String has a valid NameMeta prefix and
   `is_pooled == 1`.
3. `is_pooled` and `is_buffer` are never both set.
4. A retained/adopted reference cannot outlive its backing owner.
5. The old ShapeEntry FNV value is named `name_hash`; `predefined_id` and
   `key_ref` have distinct, documented roles.
6. Input parsers pass unchanged behavior tests and document shapes keep
   `key_ref == NULL`.
7. One Python source module generates all three enum headers and pool-data
   files; `--check` is clean.
8. Pool numbering is 0 markup, 1 Lambda, 2 JS/DOM.
9. Native markup/CSS/SVG/Radiant code compares predefined names directly by
   generated `NameId`.
10. CSS dense routing is a separate `CssPropertyCode`.
11. Handwritten HTML tag identity and duplicated spelling tables are removed.
12. Lambda and JS static property sites link through
    `modstate->property_keys[]`; MIR does not bake transient String pointers.
13. JavaScript ordinary STRING keys are isolate-canonical; SYMBOL and PRIVATE
    keys are unique/singleton according to semantics.
14. Symbol/private pseudo-name encodings no longer participate in property
    identity.
15. JS well-known refs are resolved once per runtime and hot paths compare
    pointers.
16. Builtin and constructor runtime routing no longer round-trips through
    display names.
17. All relevant baseline, DOM, layout, ownership, and cross-isolate suites
    pass.
18. Release measurements show no material regression and confirm removal of
    repeated name comparisons/materializations on the targeted hot paths.
19. No legacy C2MIR implementation was added.
20. Persistent MIR-cache/SectionNameId reconciliation remains explicitly
    recorded as future work, without an accidental interim disk ABI.
