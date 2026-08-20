# Lambda Design — Type Boundary: Field Storage, Contract Shapes, and Adoption

- **Date:** 2026-08-20
- **Status:** DESIGN — rulings TB1/TB2 given by the user; gap analysis verified in code; implementation not started
- **Scope:** how a declared record contract's fields are STORED in packed map data, which shapes compiled fast-lane code may address by fixed offset, and when a literal may adopt a declared contract's `TypeMap`
- **Related:** `vibe/Lambda_Impl_Tune19.md` §9–§11 (the O(n²) recursive-type investigation, the reverted adoption slice, and the malformed-contract-shape finding this doc resolves); future normative home per Tune19 §10.3: ruling in `doc/Lambda_Formal_Design.md` (D3.2.2v2 family), engine detail in `doc/dev/lambda/LR_03` and `LR_13`
- **Formal authority touched:** D3.2.2* (validator as runtime enforcer), D2.2.2 (same facts same code), D3.2.1 (one subtype model)

## 1. The rulings — normative home: `Lambda_Design_Compiling_Lane.md` §10

⚠ 2026-08-20: the storage-lane rulings below are DEFINED in
[`Lambda_Design_Compiling_Lane.md`](Lambda_Design_Compiling_Lane.md) §10 ("Map
field storage lanes and the TypedItem slot"), where the native-lane discipline
lives — including the complete TypedItem inventory (§10.2) and the gap ledger
(§10.4b). This doc keeps the boundary-side consequences: the storage-valid
adoption gate (§3), the implementation slices (§4), and the open questions
(§5). The summaries below are non-normative.

**TB1 — `T[]` fields are pointer slots, never `ANY`/TypedItem.** `Array` and
`ArrayNum` are both `Container*` whose pointee self-describes (`type_id` is the
first byte of every container). The field's semantic contract stays `int[]` on
the `ShapeEntry` — it is only the STORAGE classification that must say
"pointer, 8 bytes, null = 0", exactly as `map?`/named-map fields already do.

**TB2 — a union field is packed by its ACTUAL member type, and compiled code
never addresses it by fixed offset.** `int | string` both carry in 8 bytes on
this implementation (int lane = i64; `String*` = pointer — the widths are 8,
not 4, but the principle is width-compatibility, not the number). `bool |
string` (1 byte vs 8) shows why the union itself cannot own a slot format:
packing follows the actual stored member; the runtime shape records which
member is current; switching members is a shape transition (the machinery
`map_rebuild_for_type_change` already is). Compiled code for a union field
calls the generic accessor (`map_get`/`fn_member`) and lets the CURRENT shape
resolve offset and format. The union type lives in the ADMISSION layer only.

## 2. What the code does today — verified, with the gaps

| # | site | today | under TB1/TB2 |
|---|---|---|---|
| G1 | `type_field_storage_type_id` (`lambda-data.hpp` ~1030): `LMD_TYPE_TYPE && kind != SIMPLE → LMD_TYPE_ANY` | `int[]` (occurrence contract, KIND_UNARY REPEAT) classified **ANY → 9-byte TypedItem** | pointer slot, 8B. The plumbing exists — the OPTIONAL branch already returns pointer lanes via `lambda_type_id_has_pointer_lane`, but an occurrence's own type_id is `LMD_TYPE_TYPE`, so neither `int[]` nor `int[]?` reaches it |
| G2 | same classifier | `int \| string` (no null arm) → ANY/TypedItem, uniformly | admission-only; storage shape records the actual member (which is what INFERRED shapes already do — the literal probe showed `choice` stored as `STRING`, 8B) |
| G3 | AST contract-shape builder | offsets/`byte_size` computed while fields were type-VALUED (8B `Type*` slots), then consumed under storage classification (9B) — the malformed shape of Tune19 §11.3; its own `shape_entry_storage_fits_data(choice)` = false | a contract TypeMap is either recomputed under the storage rule or never used as a storage shape (see §3) |
| G4 | `map_fill` → `set_fields_items` → `set_field_value` | honors ANY storage: writes 9-byte TypedItems at the shape's recorded offsets — on a G3 shape this is the byte-24 tag clobber and the byte-32 overflow | with G1+G2, declared fields are never ANY-stored; TypedItem remains only for genuinely `any`-typed dynamic fields and full-width scalars (`int64`/`uint64` cannot pack into an 8B Item) |

**Already compliant — important:** `is_direct_access_type(LMD_TYPE_ANY)` is
**false** (`transpile_shared.cpp:96`), so the MIR direct-read and direct-write
gates already refuse ANY-classified fields. TB2's "no fixed-offset fast lane
for unions" is honored at the emitter today; the corruption was reachable only
through the (reverted) adoption slice handing real data a G3 shape whose
runtime writers then honored the 9-byte format. Likewise, inferred literal
shapes already implement TB2's storage model — actual member type on the
entry, rebuild on member switch. The gap is confined to DECLARED contract
shapes and the `T[]` classification.

## 3. The consequence that narrows everything: adoption gates on "storage-valid"

Tune19 §11 proved literal adoption is the fix for recursive types (O(n²) →
O(n), 38 s → 0.14 s) and reverted it on the G3/G4 corruption. Two gate attempts
failed because they compared the LITERAL's layout to the contract's. TB1+TB2
give the correct gate, and it is a property of the CONTRACT alone:

> A contract TypeMap is **storage-valid** iff every field's storage
> classification is a concrete carrier: native scalars, nullable native lanes,
> and pointer-carrier containers (including `T[]` after G1). No field
> classifies ANY.

⚠ **LANDED 2026-08-20** (Tune19 §11.5): the gate below is implemented as
`mir_map_contract_storage_valid`, and it needed no offset recompute — refusing
ANY-bearing contracts is sufficient, because the AST shape's 8-byte strides
already fit every concrete storage class.

- **Storage-valid contracts** (`SplayNode` = float + three pointers) may be
  adopted, with offsets computed under `type_field_storage_type_id` — this is
  exactly the recursive-type case, so the O(n) column re-lands without
  touching unions at all.
- **Union-bearing contracts** (`Person`, via `choice`) are admission-only.
  Their values keep the inferred actual-typed shape; conformance is certified
  by the `(shape, contract)` memo of Tune19 §9.3, never by pointer identity —
  which also means the §9.4 layer-0 identity check is only ever expected to
  fire for storage-valid contracts, and that is fine.
- `Person`'s `scores` field stops being a problem the moment G1 lands (pointer
  slot); only `choice` keeps it off the adoption path.

## 4. Implementation slices (each separately gated)

1. **G1 — classify occurrence contracts as pointer slots.** One classifier
   case (`T[]`, `T[]?` → the pointer lane the OPTIONAL branch already uses).
   ⚠ This changes runtime storage format for every rebuilt shape holding a
   `T[]`-contracted field (TypedItem → pointer): within one process the rule
   is applied consistently by rebuild/fill/read alike, but the slice needs the
   full baseline plus a fixture asserting a `T[]` field round-trips through
   `map_rebuild_for_type_change`, and a mir-emission look at the ANY-slot
   readers that stop being reachable.
2. **Storage-valid predicate + offset recompute** for declared contracts (fix
   G3 for the shapes that pass; refuse adoption for the rest). Assert
   `shape_entry_storage_fits_data` over every entry as the malformation
   tripwire.
3. **Re-land the three adoption pieces of Tune19 §11.1** gated on slice 2's
   predicate. `proc_type_numeric_structural_admission` (union-bearing, must
   take the generic path and pass) and the recursive-splay scaling fixture are
   the regression pair.
4. **G2 cleanup** — nothing to build: assert that declared union fields never
   reach a fixed-offset emitter (already true via `is_direct_access_type`) and
   record TB2 in the D3.2.2v2 revision alongside §9.5's items.

## 5. Open questions

- **`int | float`**: both 8B but different carriers (i64 lane vs double).
  Under TB2 this is just another union — actual-member storage + generic
  access — but it is the case that tempts a "same width, share the slot"
  shortcut. The tag is what distinguishes them; the shortcut is unsound
  without it. Named here so nobody takes it.
- **Member-switch churn**: `bool | string` ping-ponging forces a rebuild per
  switch under TB2. Accepted for now — measured record workloads switch member
  types rarely; revisit only if a profile ever shows rebuild in a hot loop.
- **Full-width members** (`int64 | string`): the actual-member rule stores the
  int64 in its 8B lane slot; the entry tag disambiguates. TypedItem is not
  needed for these either once the entry records the member.
