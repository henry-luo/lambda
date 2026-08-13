# Lambda Jube DOM — Stage 4 (DOM4): Compile-Time Member Ordinals — Proposal

> **Status**: PROPOSAL (2026-08-13). Not started.
> **Parent design**: [Lambda_Jube_DOM3.md](./Lambda_Jube_DOM3.md) — table-driven property
> dispatch (`JubeMemberRecord` tables, Lambda-type-syntax interface declarations, binding
> tables). DOM3 phases 0–5 are implemented and green; `dom_node` has 142 declared members
> routed through record dispatch with `host_ops = NULL`.
> **Predecessors**: [Lambda_Jube_DOM.md](./Lambda_Jube_DOM.md) (DOM1),
> [Lambda_Jube_DOM2.md](./Lambda_Jube_DOM2.md) (DOM2).
> **Spec anchors**: **D7.4.4** (single host-object protocol — this doc's charter ruling,
> minted from D4k/D4j), D3.4.7 / D7.4.1–D7.4.3 (host-family metadata, single VMap/Jube bridge),
> D6.2.2v2 (observable Get-then-`[[Call]]` for method calls), D5.4.3/D5.4.4 (no realm state
> baked into shared MIR), D8 (compilation). LC1 applies to Lambda script only; LJS keeps its
> caches — but DOM4 is *static resolution*, not an inline cache.

## 0. The DOM1→DOM4 arc

- **DOM1 — the carrier.** DOM wrappers became **branded native VMaps** (`host_type` =
  interface identity, `host_data` = Radiant `DomNode*`), with wrapper-identity caching
  so `el === el`. Behavioral pin still in force: projected members are non-configurable
  and unshadowable (writes swallowed, no expando shadowing) — a deliberate divergence
  from WebIDL.
- **DOM2 — the protocol.** A generic host-object protocol over the carrier:
  `JubeHostObjectOps`, a real versioned host API, descriptor-driven registration, and
  Lambda-side projections. Dispatch inside the ops was still hand-written strcmp chains
  (~1,150 sites, name lists hand-synced across up to 4 copies per type).
- **DOM3 — the tables.** Interfaces became **declared data instead of code**: Lambda
  type syntax as the IDL (D0a), parsed at registration into member records behind a
  SipHash index dual-keyed snake/camel; behavior reduced to binding tables;
  has/keys/descriptors/prototypes derived from the table; method reads return real
  cached function objects (D0d). Per-tag differences via guard chains; open-name
  surfaces via record-owned `named_get`/`named_set` hooks. strcmp chains largely
  deleted; `host_ops = NULL` for converted families, `legacy_ops` left as a
  transitional fall-through.
- **DOM4 — compile time, and one protocol** (this doc): the declaration becomes the
  compiler's input (member ordinals, typed lattice), guard chains become subtypes, and
  the fallback tier is deleted outright — the declared tables end up the fastest path
  and the only path.

## 1. Concept

> **Declared interfaces + record-owned hooks are the ONLY host-object protocol — one way
> to be a host object, no fallback tier.** (Now formal: **D7.4.4**.) DOM4's two thrusts
> serve this single statement: *forward*, compile-time member ordinals make the declared
> tables the fastest path as well as the only path (D4a–D4f); *backward*, the
> `host_ops`/`legacy_ops` fallback tier and its key-materialization shim are deleted
> outright (D4k, P0.6). When DOM4 exits, a host type's entire behavior is legible from
> its interface declaration and binding table — nothing routes around them.

DOM3 replaced ~1,150 strcmp dispatch chains with per-type member-record tables resolved by
a content-hashed (SipHash) HashMap, dual-keyed on snake_case and camelCase. That fixed the
*shape* of dispatch but every DOM property access still pays, at runtime:

1. `jube_record_for_type` — **linear scan** over `s_type_records[]` (jube_interface.cpp:145)
   to map `vmap->host_type` → `JubeTypeRecord*`;
2. key-character extraction from the key Item (and on some paths a transient
   `heap_strcpy` key materialization — DOM3 Part 1 scan facts, vmap.cpp string_key_item);
3. SipHash over the key bytes + HashMap probe + memcmp confirm (`jube_resolve_member`,
   jube_interface.cpp:172);
4. guard-chain walk + binding call.

**DOM4 moves steps 1–3 to compile time and deletes step 4 from the kernel entirely**
(user ruling 2026-08-13: routing must not pay for per-tag behavior — see D4h/D4i). The member tables are built from interface
declaration text that is compiled into the binary (`radiant_dom_interface_decl` and peers)
and parsed deterministically at registration — so the table layout is knowable while the
JS→MIR transpiler is running in the same process. When the transpiler can prove a
receiver's Jube type, it resolves the member **ordinal** (index into the type's stable
`members[]` array, which is already flat and in declaration order —
`JubeTypeRecord.members` / `member_count`) and emits:

```c
jube_member_get_by_ordinal(receiver, type_slot, ordinal, &out)   // guarded fast path
```

instead of the generic `js_property_get` → `jube_member_get` → hash-lookup pipeline. At
runtime the fast entry does a brand check (one pointer compare) and an array index — no
key bytes, no hash, no memcmp, no linear type scan. On any guard failure it returns 0 and
the site falls back to the generic path, so mispredictions stay correct.

This is the DOM analogue of what the builtin catalog already does for `Math.*`
(compile-time lowering to direct calls, js_mir_expression_lowering) and what `builtin_id`
int-switch dispatch does for builtins — the same "resolve names at compile time, ship
integers" move, applied to the Jube interface tables.

## 2. Decisions

Continuing the DOM-stage decision ledger (DOM3 used D0a–D0d):

- **D4a (v2) — Ordinal = declaration index, resolving to exactly ONE member record; the
  dispatch kernel carries no guard chains.** A member's identity across the compile/runtime
  boundary is its index in the owning type's declared member array, and `members[ordinal]`
  is the whole answer — no `next_same_name` walk, no per-row predicate in the kernel.
  Per-tag behavior differences are handled by D4h (per-kind host subtypes) and D4i
  (handler-internal branching), never by routing. Invariant: registration (and
  `jube_interface_runtime_reset`) must never reorder or renumber `members[]` for a given
  interface text; the array is rebuilt only from the same static text, so ordinals are
  stable for the life of a binary.

- **D4b — Brand guard, not trust.** Every by-ordinal entry validates before touching the
  table: `get_type_id(receiver) == LMD_TYPE_VMAP`, then the brand check — exact
  (`trec->type == receiver.vmap->host_type`) when the lattice proved a leaf type, family
  membership (D4h) when it proved the base — then the DOM3 neutered-husk
  check (`host_data == NULL` → undefined, same semantics as today). Any failure returns 0
  and the emitted code falls back to the generic property pipeline. Compiler typing is
  therefore *optimistic*: a wrong prediction (rebound `document`, null `parentNode`, a
  non-DOM value flowing in) costs one compare and lands on today's exact behavior.

- **D4c — Type slots kill the linear type scan for everyone.** Registration assigns each
  `JubeTypeRecord` a stable small-integer slot (its index in `s_type_records[]`, which is
  append-only per D4a). The compiler bakes the slot; the runtime does
  `s_type_records[slot]` + brand compare — O(1). Independently, `jube_record_for_type`
  gets a pointer-keyed lookup (≤64 types; open-addressed table or sorted array) so the
  **slow** path stops linear-scanning too. This piece lands even if nothing else does.

- **D4d — Direct method calls are semantics-preserving and allowed.** For a
  compile-time-resolved `JUBE_MEMBER_METHOD`, `receiver.method(args)` may lower to
  `jube_member_call_by_ordinal(receiver, slot, ordinal, args, argc, &out)`, skipping the
  cached-function-object read and the arity trampoline. Justification against D6.2.2v2
  (observable Get then `[[Call]]`): declared members **cannot be shadowed** — instance
  writes to readonly members are swallowed ("no expando shadowing of projected members",
  jube_member_get resolves records *before* expando and prototype fallthrough, descriptors
  report `configurable: false`, and delete refuses. A Get that can only ever produce the
  record's own cached function is unobservable; eliding it changes nothing a conforming
  script can see.
  **Provenance caveat: the unshadowable contract is a Lambda/Jube ruling (the DOM1→DOM3
  "projected non-configurable contract", Lambda_Jube_DOM3.md — no formal D# covers it),
  NOT the web platform spec.** WebIDL puts members on interface prototypes as
  `{writable: true, configurable: true}`; browsers permit both instance shadowing
  (`el.appendChild = x`) and prototype patching (polyfills), with only
  `[[LegacyUnforgeable]]` members (location) non-configurable. Our pin already makes such
  patching inert today, so D4d adds no *new* divergence — but baked direct calls harden
  the pin: adopting browser-accurate shadowing later (polyfill compat is the realistic
  pressure; Sizzle already forced DOM3 behavior flips) would require a deopt mechanism,
  e.g. a per-type patch epoch folded into the brand guard. Adopt D4d with that recorded. The direct entry must still: pass the family brand check, apply husk semantics,
  and feed the JS exception/error-lane state identically (`can_raise` members get the same
  `jm_emit_error_lane_propagate_check` any runtime call gets). A plain get of a method name
  (`f = el.appendChild`) still returns the cached function object — only the immediate-call
  form takes the direct entry.

- **D4e — Receiver typing is a flow lattice seeded and propagated by the interface
  declarations themselves.** No hand-maintained type tables on the JS side. Seeds:
  `document` / `window` globals (only when the module provably never rebinds them and the
  scope has no `with` / direct `eval` — detection helpers exist). Propagation: the declared
  interfaces carry Lambda-typed signatures (`clone_range: fn() range`,
  `first_element_child: dom_node`, `create_range: fn() range`), so the registry can answer
  "member M of type T yields type U" and one seed types whole chains:
  `document.createRange()` : range → `.cloneRange()` : range;
  `document.getElementById(x)` : dom_node → `.parentNode` : dom_node. Storage piggybacks
  on the existing known-class-instance machinery (the P7 `class_entry` precedent on
  module-const/var entries): add a `const JubeTypeDef*` to the var-entry and to
  expression-result typing in the lowering. Joins that disagree drop to unknown;
  reassignment with an unknown RHS clears the type. Nullable members (`parentNode`) keep
  their type — the D4b guard makes optimism safe (null → fallback → today's TypeError
  path).

- **D4f — Only declared members get ordinals; misses stay dynamic.** Computed keys
  (`el[k]`), names that don't resolve in the table at compile time (expandos, prototype
  patches, `named_get` open-name surfaces like CSS property names on style objects, and
  `indexed_get`) always take the generic path. A compile-time miss is *not* evidence of
  runtime absence — expandos are per-instance — so no "known-absent" fast path.

- **D4h — Per-kind host subtypes replace guard chains (USER RULING 2026-08-13).** The
  single flat `dom_node` interface splits into a family: `dom_node` (base) with subtypes
  for the kinds whose member sets or member behavior differ — at minimum
  `input_element`, `select_element`, `textarea_element`, `option_element` (the `value`
  overload family), plus `character_data` (text/comment) and `svg_element`, which today
  exist only as applicability guards (`guard_node` ×26, `guard_svg` ×8, `guard_text` ×6,
  `guard_character_data` ×4 — most guards in the census are applicability, not overloads,
  and all dissolve under subtyping). Mechanics:
  - **Wrap-time selection.** The wrapper's brand is chosen once at `js_dom_wrap_element`
    time from the node kind/tag (both immutable after creation) via a static tag→type
    table. No per-access cost.
  - **Prefix ("vtable") member layout.** A subtype's `members[]` is its parent's array as
    a prefix, own/overriding members after (overrides replace in place at the parent's
    ordinal). Registration flattens the declared inheritance (`type select_element <:
    dom_node` — Lambda object types already support inheritance per D0c) into this layout.
    Consequence: an ordinal resolved at compile time **against the base type** is valid on
    every family member — the receiver's own record supplies the (possibly overridden)
    row, which is exactly virtual dispatch.
  - **Family brand check.** The lattice usually proves only the base type
    (`querySelector` : dom_node), so the fast entry accepts any family member: resolve
    the receiver's own record O(1) (host_type→record map, D4c), then check family
    membership (stored family-root slot compare; parent chains are ≤2 deep). One extra
    load versus the exact-brand compare.
  - **Prototype identity.** Each subtype gets its own prototype whose `__proto__` is the
    parent's — moving instanceof/proto-chain behavior *closer* to browsers
    (HTMLSelectElement → HTMLElement → Node). This is an observable, deliberate golden
    change (DOM3 precedent: D0d/get_attribute flips). Exposing named constructors
    (`window.HTMLSelectElement`) is optional and deferred.

- **D4i — Residual per-tag logic lives inside the member's own handler, never in
  routing.** Where a behavior difference is too small to justify a subtype (single quirky
  member, e.g. the `href`/`src` reflection variants), the type declares ONE member row and
  its get/set binding branches on the tag internally (`radiant_dom_value_get` owning its
  own `is_tag` switch). The kernel's contract stays: ordinal → record → binding, no
  predicates. Rule of thumb: a guard cluster with several members (select: 12 rows,
  input: 18, option: 8, anchor: 8) becomes a subtype; a 1–2-row cluster becomes an
  internal branch. Behavior parity pin: a member absent from a kind (e.g. `div.value`)
  must keep today's guard-miss semantics — read undefined, write lands in the expando and
  reads back. Under subtyping that happens naturally (the name misses `dom_node`'s table
  → generic path → expando), so **`value` is NOT declared on the base type**; base-typed
  `el.value` access takes the generic path unless the lattice proves the subtype
  (see OQ6 for the virtual-base-row alternative).

- **D4g — The MIR disk cache must key on a registry digest.** Baked slots/ordinals are
  stable within a binary (static module list, static interface text ⇒ deterministic
  registration), but **not across builds**: editing an interface declaration renumbers
  ordinals while the JS source hash — the current L1 cache key — is unchanged. Before the
  fast path ships, the L1 (and future L3) MIR cache key must fold in a digest of the
  activated interface texts + module registration order. This is a correctness gate, not
  an optimization. (Realm note: slots and ordinals identify process-wide static registry
  entries, like `builtin_id`s — they are not realm state, so D5.4.3/D5.4.4 realm-in-MIR
  concerns do not apply; the per-realm NameId indirection stays as-is for ordinary maps.)

- **D4j — Key-carrier policy: Item at the ABI, one borrowed strview inside, integers as
  the end-state.** Ruling on the current inconsistency (Item at entry → repeated
  `jube_item_key_chars` extraction → `heap_strcpy` Item re-materialization at the vmap
  legacy boundary):
  - **ABI boundary stays Item** (jube.h hooks, member entries): property keys are
    string | symbol | number (indexed hooks legitimately receive ints), jube.h is the
    cross-language C ABI where Items are the currency, and the expando/prototype
    fallbacks are Item-keyed regardless — strview-only would just move the copy deeper.
  - **Internal resolution flow uses ONE borrowed extraction**: extract `(chars, len)`
    once at entry into a `JubeKey {Item item; const char* chars; uint32_t len;}` and
    thread it — today `jube_member_get` extracts three times (resolve, indexed digits,
    `__proto__`). Views borrow from the rooted key Item; never NUL-terminated, so
    binding code must take `(chars, len)` or Item, **never `char*`** — the `fn_to_cstr`
    assumption is what created the shim.
  - **Retiring the vmap `string_key_item`/`heap_strcpy` re-materialization COMPLETELY is
    an explicit DOM4 goal (USER RULING 2026-08-13)** — not "dies eventually with
    legacy". Subsumed by D4k, which retires the entire `host_ops`/`legacy_ops`
    mechanism the shim serves; the shim and its route are deleted in the D4k sweep.
    Do not align new code to it.
  - **End-state hot-path currency is integers**: ordinal sites carry no key; OQ8's
    id-keyed entry carries a NameId; the strview resolver remains only for computed
    keys.

- **D4k — Retire `host_ops` and `legacy_ops` ENTIRELY (USER RULING 2026-08-13).** DOM4
  exits with the type-level fallback dispatch mechanism gone, not merely unused:
  declared interfaces + record-owned hooks (`named_get`/`named_set`/`object_*`,
  `indexed_get`, `JubeTypeDef.destroy`) become the **only** host-object protocol.
  Inventory (2026-08-13) says this is cheap: `legacy_ops` already has zero users
  (all 13 binding rows NULL) and `host_ops` has two instances — one dead for property
  traffic (range/selection), one live (`velmt`). Exit criteria:
  1. Zero `host_ops`/`legacy_ops` instances in the tree (velmt migrated; range/selection
     stripped after the `invalidate` caller check).
  2. All consumer code deleted, not stubbed: the ~12 `jube_legacy_ops()` fall-through
     checks in jube_interface.cpp, vmap.cpp's host get/set/own-keys route incl.
     `string_key_item`, and the js-side fallback branches (js_globals.cpp
     has/delete/own_keys, js_runtime.cpp `host_ops->prototype`, the JS_EXOTIC host-ops
     hook resolution).
  3. Struct surface cleaned up: `JubeHostObjectOps` and the `host_ops`/`legacy_ops`
     fields removed from jube.h with a JUBE_ABI_VERSION bump (all modules are in-tree
     static — the bump is cheap now and removes the temptation permanently; mere
     registry rejection is the fallback only if an out-of-tree module appears before
     then), plus stale comments/docs referencing the fallback protocol
     (jube.h, hostobj_demo, JS_13) updated.
  P0.6 is the carrying phase; D4j's shim-retirement goal is subsumed by this ruling.

- **D4l — Three virtual carriers: varray, vmap, velmt (USER RULING 2026-08-13,
  direction).** Jube's virtual-object story mirrors Lambda's container taxonomy instead
  of forcing everything through the one existing carrier. Census of today's mismatches:
  map-shaped hosts are branded VMaps (fine); **array-shaped hosts are not virtual at
  all** — NodeList/HTMLCollection/options/cssRules are eagerly materialized real Arrays
  with a companion-map `namedItem`/`constructor` decoration, kept "live" by a
  4096-entry issued-collection cache re-walked on every DOM mutation
  (js_dom.cpp:1868, refresh sweeps) — copies on read *and* sweeps on write;
  **element-shaped hosts are flattened** — Radiant's `Velmt` memcpys the struct into a
  VMap payload and strcmp-projects `tag`/`attrs`/`children`/`text` through the map-only
  interface. The carriers:
  - **vmap** — virtual map (exists; `VMapVtable`).
  - **varray** — virtual array: `{get_at, count, trace, destroy}` vtable + host brand,
    `type()` = "array"; length/index/iteration route through the vtable. DOM collections
    become varrays over the parent's live child state — **live by reading**, deleting
    the materialization and the mutation-refresh machinery. Declared element type
    (`dom_node[]`) feeds the D4e lattice, resolving OQ3's loop-variable typing.
  - **velmt** — virtual element: Lambda `Element`'s dual nature virtualized — list face
    (children), map face (attrs/named members), tag. First client: Radiant `Velmt`
    custom-layout handles (the name already agrees); candidate: the Lambda/Mark
    projection of DOM nodes, where an element *is* the natural data shape.
  Orthogonality pin: declared interfaces / member records / ordinals are the
  **named-member** protocol and apply identically to all three carriers (D7.4.4); the
  carrier decides which **structural** faces exist virtually. **Scope (user ruling
  2026-08-13): direction only in DOM4.** The Lambda runtime is likely not ready for the
  new carriers; varray and velmt are future Jube work (their own stage). **DOM4 settles
  vmap first** — ordinals, subtypes, host_ops retirement, and the fast path all land on
  the existing VMap carrier; nothing in DOM4 depends on the new carriers existing.
  Collections stay materialized Arrays for now (the refresh-sweep cost is accepted,
  known, and quarantined behind D4l as the future fix); DOM-node carrier move = OQ9.

## 3. Mechanism

### 3.1 Registry: compile-time query surface (jube_interface.h additions)

```c
// compile-time (transpiler-side) queries — all pure lookups over registered records
const JubeTypeDef* jube_iface_type_by_name(const char* name, uint32_t len);
int  jube_iface_type_slot(const JubeTypeDef* type);              // -1 if undeclared
int  jube_member_ordinal(const JubeTypeDef* type,                // unique record ordinal
                         const char* name, uint32_t len);        // both spellings; -1 miss
uint8_t jube_member_kind_at(const JubeTypeDef* type, int ordinal); // JubeMemberKind
bool jube_member_can_raise_at(const JubeTypeDef* type, int ordinal);
int  jube_member_arity_at(const JubeTypeDef* type, int ordinal);
// D4e propagation: declared field type / method return type, as a registered type or NULL
const JubeTypeDef* jube_member_result_type_at(const JubeTypeDef* type, int ordinal);
```

Requires keeping the parsed signature's result-type name (or resolved `JubeTypeDef*`) on
`JubeMemberRecord` — parse output that DOM3 currently uses only for method-arity and
projection typing. No `JubeTypeDef` / module-ABI change: everything lives on the
Lambda-side records (`jube_interface.cpp`), consistent with DOM3's frozen-ABI stance.

### 3.2 Runtime: by-ordinal entries

```c
// return 1 = handled (result in *out); 0 = brand/guard mismatch, caller falls back
int jube_member_get_by_ordinal (Item receiver, int slot, uint32_t ordinal, Item* out);
int jube_member_set_by_ordinal (Item receiver, int slot, uint32_t ordinal, Item value, Item* out);
int jube_member_call_by_ordinal(Item receiver, int slot, uint32_t ordinal,
                                Item* args, int argc, Item* out);
```

Body of the get, in full:

```c
if (get_type_id(receiver) != LMD_TYPE_VMAP) return 0;
JubeTypeRecord* trec = jube_record_for_host_type(receiver.vmap->host_type); // O(1), D4c
if (!trec || trec->family_root_slot != slot) return 0;           // family brand check, D4h
if (!receiver.vmap->host_data) { *out = undefined; return 1; }   // husk, DOM3 semantics
JubeMemberRecord* rec = &trec->members[ordinal];   // receiver's OWN row: virtual dispatch
... CONST / METHOD / get-binding dispatch (shared kernel with jube_member_get) ...
```

No guard chain, no predicates — `members[ordinal]` on the receiver's own record is the
complete resolution (D4a v2). The prefix layout (D4h) is what makes indexing the
*receiver's* table with a *base-resolved* ordinal sound. The dispatch arm is extracted
from `jube_member_get` and shared (CLAUDE.md rule 13 — one kernel, two entries). Set
mirrors the existing write semantics including the readonly swallow; call invokes
`rec->bind->call` directly with the receiver (what the trampoline does today, minus the
function-object indirection). Exact-brand sites (the lattice proved a leaf subtype, e.g.
from a `createElement("select")` literal) may still emit the plain
`trec->type == host_type` compare.

### 3.3 Transpiler: lattice + emission

- `jm_build_reference` (js_mir_expression_lowering.cpp:1240) already special-cases named
  keys (NameId, IC index). Add: if the object expression's inferred Jube type is known and
  the key is a non-computed identifier that resolves via `jube_member_ordinal`, stamp
  `jube_slot`/`jube_ordinal` on the reference.
- Load/store emission: guarded call to the by-ordinal entry; on 0-return, branch to the
  existing generic sequence. Immediate-call form (`recv.m(...)` where the member kind is
  METHOD) emits `jube_member_call_by_ordinal` with the same fallback shape (D4d).
- Lattice: seeds + `jube_member_result_type_at` propagation through member gets and calls
  (D4e); merged conservatively at joins, cleared on unknown assignment, disabled entirely
  in scopes with `with`/direct-eval.
- Kill switch: `LAMBDA_JS_NO_JUBE_ORDINAL` env var reverts all sites to the generic path;
  `js_exec_profile` counters record fast-hit / fallback per site kind (JsOptTrace surface)
  so coverage is measurable rather than assumed.

### 3.4 What the fast path removes, per access

| today (record path, post-DOM3) | DOM4 fast path |
|---|---|
| VMap check | VMap check |
| `jube_record_for_type` linear scan over types | O(1) record map + family-slot compare |
| key-chars extraction (+ transient key on some routes) | — |
| SipHash + HashMap probe + memcmp confirm | `members[ordinal]` load |
| guard-chain walk (tag predicates per row) | — (subtyping D4h / handler-internal D4i) |
| binding call | binding call |
| methods: cached-fn read → trampoline → generic call entry | direct `bind->call` (D4d) |

The guard elimination also speeds the **generic** path: hash resolution lands on a single
record instead of walking a predicate chain, and the per-type tables shrink (the 5 `value`
rows become 1 per subtype).

The key-materialization and hashing elimination is likely worth more than the scan: keys
arrive as Items and some routes copy bytes per access just to probe the index.

## 4. Hazards

- **H1 — Stale ordinals via the MIR disk cache** (D4g). Interface-text edit + warm L1
  cache = silently wrong member. Registry digest in the cache key is a ship-blocker gate.
- **H2 — runtime_reset invariants.** `jube_interface_runtime_reset` recreates prototypes
  and method-fn roots per batch script; it must not free/rebuild `members[]` in a way that
  changes addresses mid-run while compiled MIR holds slot+ordinal (addresses aren't baked —
  only integers — so rebuild-from-same-text is fine; assert count/order equality in debug).
- **H3 — Seed soundness.** `document` is a *global property*, reachable as
  `window.document`, deletable/assignable in principle. Seeding must be conservative:
  any write to the name in the module, any `with`, any direct eval in scope disqualifies
  it. The brand guard bounds the damage of a wrong seed to a fallback, but a *stale
  wrapper of the right brand* would pass the guard — so seeds must only ever be values
  produced by the bridge (globals and declared-signature results), never heuristic
  ("variable named `el`") typing.
- **H4 — Prefix-layout integrity.** The D4h vtable property (base ordinal valid on every
  family member, overrides in place) is load-bearing for correctness, not just speed. A
  registration bug that shifts a subtype's inherited prefix silently redirects every
  base-resolved access on that subtype. Debug assert at registration: for each subtype,
  `members[i].snake_name == parent->members[i].snake_name` for all parent ordinals.
- **H4b — Brand-compare sweep.** ~31 direct `host_type ==` compares exist across
  js_dom.cpp / the bridge / vmap.cpp; each must become family-aware (or provably
  base-only) when the family splits. A `radiant_dom_is_node_family(host_type)` predicate
  replaces them in one sweep; any survivor comparing raw equality against the base type
  is a latent "select element isn't a DOM node" bug.
- **H4c — Guard-miss parity.** Members that used to guard-miss into the expando
  (`div.value = "x"` reads back `"x"`) must keep exactly that behavior when their rows
  move into subtypes (D4i pin). The parity harness must sweep *mismatched* kind × member
  pairs, not just declared ones.
- **H5 — Error-lane parity for `can_raise` members.** The direct-call entry must set the
  same JS exception state the trampoline path does, and lowering must emit the same
  propagate check; a missed check turns a DOM exception into silent-undefined.

## 5. Phased plan

Gates per phase follow the DOM3 pattern: `make build`, focused DOM GTests, full JS GTest,
`dom_module_props` diff-exact, UI-automation, `make test-lambda-baseline`, `make release`,
plus the DOM3 Phase-5 wrap-sweep benchmark (query_ms / walk_ms) and jq_find_repro as the
perf/behavior canaries.

- **P0 — Registry groundwork** (no JS changes). Type slots + O(1) `jube_record_for_type`
  (D4c, benefits the slow path immediately); compile-time query API (3.1); result-type
  retention on records; by-ordinal runtime entries (3.2) with the shared dispatch kernel.
  New gate: a **parity harness** GTest that, for every declared type, sweeps every member
  ordinal and diffs `*_by_ordinal` against the by-name path (get/set/call, husk receivers
  and mismatched kind × member pairs included).
- **P0.5 — Family split** (D4h/D4i; independent of the compiler work and de-risks it).
  Interface-inheritance flattening with prefix layout + registration assert (H4);
  wrap-time tag→type selection; migrate guard clusters into subtypes
  (character_data / svg first — pure applicability, no behavior forks — then the form
  controls), fold 1–2-row clusters into handler-internal branches; delete
  `guard`/`next_same_name` from the dispatch kernel; brand-compare sweep (H4b);
  convert `_is_tag` strcasecmp to `tag_name_id()` integer compares (§6).
  Gates: full DOM3 gate set with **deliberate, reviewed golden updates** for prototype
  identity (per-subtype prototypes are an intended behavior change toward browser
  semantics), plus H4c mismatched-pair parity.
- **P0.6 — host_ops/legacy_ops retirement** (carries D4k, which subsumes D4j's shim
  goal; can run parallel to P1). Inventory 2026-08-13: `legacy_ops` has **zero users** —
  all 13 binding rows in the tree are NULL (radiant ×10, node_fs ×2, hostobj_demo) — and
  `host_ops` has exactly two instances: `radiant_dom_node_host_ops` (stamped on
  range/selection, dead for all property traffic since records + prototype_seed answer
  first; only the `invalidate` slot needs a caller check — no `ops->invalidate` call
  site found) and `radiant_velmt_host_ops` (fully live: velmt has no binding row at
  all). Steps:
  1. Delete the ~12 dead `jube_legacy_ops()` fall-through checks in jube_interface.cpp —
     no migration needed, they guard nothing today (field removal = optional ABI bump).
  2. Migrate `velmt` to a declared interface + binding table (node_fs file_handle/stats
     rows are the OWNING_NATIVE template: records + `JubeTypeDef.destroy`, host_ops
     NULL).
  3. Verify/rehome `radiant_dom_host_invalidate` (husk protocol), then strip the dead
     pointer from range/selection.
  4. One sweep deletes the whole dead mechanism per D4k exit criteria: `string_key_item`
     + the host route in vmap.cpp, the js-side host_ops fallback branches (js_globals.cpp
     has/delete/own_keys, js_runtime.cpp prototype fallback, JS_EXOTIC hook resolution),
     then remove `JubeHostObjectOps` + the `host_ops`/`legacy_ops` fields from jube.h
     with the JUBE_ABI_VERSION bump and update stale fallback-protocol comments/docs
     (jube.h, hostobj_demo, JS_13).
  Distinction to preserve: `named_get`/`named_set`/`object_*` **record-owned hooks** on
  the binding (dom_node, document, style/CSSOM open-name surfaces) are the sanctioned
  D4f surface and stay — retirement targets the type-level `host_ops` fallback and
  `legacy_ops` only. Gates: grep-zero `string_key_item`, hostobj/velmt-touching suites,
  full DOM3 gate set.
- **P1 — Get fast path, seeded types only.** Lattice with `document`/`window` seeds; emit
  guarded by-ordinal gets at non-computed named sites; kill switch + profile counters.
  Gate: goldens unchanged with the flag on and off; counter report shows nonzero hits on
  dom-heavy suites.
- **P2 — Signature propagation + direct method calls.** `jube_member_result_type_at` flow
  (D4e) so `getElementById` / `createRange` / navigation chains type through;
  `jube_member_call_by_ordinal` for immediate calls (D4d) with error-lane parity tests
  (H5): a raising member (e.g. `compareBoundaryPoints` on a detached range) must produce
  the identical thrown error via both paths.
- **P3 — Stores + guard-heavy rows.** `set_by_ordinal` including readonly-swallow and
  reflected-attribute + live-state hooks; the select/input/option/textcontrol `value`
  guard family as the acceptance case.
- **P4 — Cache digest + adjacent scans.** Registry digest into the MIR L1 (and planned
  L3) cache key (H1 — must land before default-on if the cache is enabled anywhere the
  fast path is). Convert the DOM wrapper identity cache's linear chunk scan to a
  pointer-keyed hashmap while in the area — it is the other per-access linear scan DOM3
  left behind (JS_13 §2 known issue).
- **(Future, not DOM4) varray carrier + collection conversion** (D4l direction). When
  the runtime is ready: introduce `VArray` (vtable + host brand); convert DOM
  collections (childNodes/children, getElementsBy*/HTMLCollection, select.options,
  cssRules) from materialized decorated Arrays to live varrays over parent state; delete
  the issued-collection cache and the per-mutation refresh sweeps; declare element types
  (`dom_node[]`) so collection loops type through the lattice. `velmt` (the Radiant
  handle) migrates in P0.6 on declared records regardless — its move to the velmt
  *carrier* rides OQ9. Behavior gates when it runs: live-collection semantics (mutation
  during iteration, `namedItem`, `length` after insert) golden-diffed; snapshot
  semantics (`querySelectorAll`) must stay snapshots.

## 6. strcmp endgame — what DOM4 kills and what legitimately survives

Census 2026-08-13: js_dom.cpp has ~687 string-compare sites (+89 bridge). DOM4's claim is
scoped: it eliminates string comparison as a **dispatch mechanism**, not as data
semantics or parsing.

- **Killed (~300 prop-name chain sites):** the `strcmp(prop, ...)` behavior chains behind
  the record adapters. Not by the fast path itself but by the extraction D4h/D4i forces —
  per-subtype bindings become direct behavior fns (this completes the DOM3 Phase-4e
  engine sweep). After P0.5+P3, an inferred-site member access touches zero string bytes.
- **Killed as strings (~200 tag-check sites, 88× `_is_tag`):** routing-role tag checks
  become wrap-time type selection (D4h); D4i handler-internal branches convert to integer
  compares — `DomElement::tag_name_id()` already exists (dom_element.hpp:821), `_is_tag`'s
  per-check `strcasecmp` is legacy. Mechanical conversion, fold into P0.5.
- **Survives, correctly (~55 keyword-value sites):** attribute *value* semantics
  (`contenteditable="plaintext-only"`, `type="checkbox"`, dir/autocomplete keywords) —
  data comparison, not dispatch; enum-table cleanup case-by-case, out of scope.
- **Survives, bounded:** the generic path's hash + single memcmp confirm for computed
  keys and un-inferred sites (NameId is a content hash, not a unique symbol — the
  confirm cannot be dropped); open-world attribute names (`data-*`) on the miss path;
  and parsing proper (selector text, innerHTML fragments, CSS values) — string
  processing by nature.

**Under jube proper, post-DOM4 the runtime string work reduces to:** the by-name index
probe's memcmp confirm, the 9-byte `__proto__` check, `named_get` CSS-name resolution,
and the expando fallback's ordinary-map lookup (the JS object kernel's cost, not jube's).
OQ8 removes the first two for baked-literal names at un-inferred sites; after that, byte
comparison under jube exists only for genuinely computed keys (`el["val"+"ue"]` — hashing
bytes is unavoidable by construction) and the OQ4 CSS surface.

## 7. Open questions

- **OQ1 — Un-inferred sites.** Extend named-IC admission (`js_named_ic_receiver_map`) to
  VMap receivers so dynamic DOM sites get IC coverage too, or accept the generic path
  where the lattice can't see? Proposal: defer; measure P1/P2 counter fallback rates
  first — if inferred coverage on real DOM workloads is high, the IC work is dead weight.
- **OQ2 — Constant folding.** Instance constants (`range.START_TO_START`) could fold to
  an immediate behind the brand guard. Marginal; take only if free during P1.
- **OQ3 — Collection element types.** `querySelectorAll` / `childNodes` yield arrays of
  `dom_node`; typing elements through loop variables would unlock the fast path inside
  the hottest loops. Needs array-element type flow in the lattice — separate slice,
  likely the highest-value follow-on.
- **OQ4 — Open-name surfaces.** `style.backgroundColor` resolves via `named_get` and can
  never have a member ordinal — but CSS property names have their own static table
  (`css_prop_table.cpp`). A compile-time CSS-property-ID fast path over `named_get` is
  the same trick one layer down; out of scope for DOM4 proper.
- **OQ5 — Ordinal surface for Lambda-side callers.** The Lambda projection
  (`jube_member_projected_get`) could take the same by-ordinal shortcut once the Lambda
  transpiler grows receiver typing; nothing in the runtime design precludes it (entries
  are language-neutral). Not scheduled.
- **OQ6 — Virtual base rows for hot subtype members.** Under D4i, `el.value` on a
  base-typed receiver takes the generic path (the name isn't on `dom_node`). If profiling
  shows base-typed `value`/`checked`/`selected` access is hot, the alternative is
  declaring them on the base as *declining default* rows (subtypes override in place at
  the same ordinal; the base default signals "absent" and the kernel falls through to
  expando/proto). That buys the ordinal fast path at the cost of a decline signal in the
  kernel contract — measure before paying (js_exec_profile fallback counters, P1).
- **OQ8 — Id-keyed generic entry for baked names on un-inferred receivers.** A
  non-computed site whose receiver the lattice couldn't type still has a compile-time
  name literal. LJS already ships baked names as integers (per-module name table +
  execution-time NameId, the named-IC `named_key_index` machinery, realm-correct per
  D5.4.3/D5.4.4). Add `jube_member_get_by_name_id(receiver, name_id, chars, len, out)`
  and key the member index on NameId with integer compare (byte-confirm only on
  collision). Kills the memcmp confirm and the `__proto__` byte check for the dominant
  share of generic-path traffic; full elimination for computed keys needs unique interned
  name identity — the Name Identity design's open W1/W2, explicitly NOT absorbed here.
- **OQ7 — Subtype granularity beyond the initial set.** The guard census also shows
  anchor (8), form (4), details (2), img (2) clusters. Split them only when the cluster
  earns it under the D4i rule of thumb; each new subtype costs a tag-table row and a
  prototype, not a kernel change — the design is granularity-neutral.
- **OQ9 — velmt as the DOM-node carrier.** D4l establishes the velmt carrier (dual
  list+map faces + tag); whether DOM nodes themselves move onto it — making the
  Lambda/Mark projection of a DOM tree a real virtual Element tree (children as list
  face, attrs as map face) instead of a VMap with projection keys — is a DOM5-scale
  carrier switch (DOM1 was the last one). Radiant's custom-layout `Velmt` handles are
  the low-risk pilot: element-shaped, read-only, pass-scoped.
