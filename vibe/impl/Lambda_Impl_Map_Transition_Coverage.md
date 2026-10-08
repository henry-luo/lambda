# Map Transition Coverage — Implementation Plan

**Date:** 2026-10-06 (rev 2, same day: tree at construction; replay dropped)
**Status:** IN PROGRESS — P0–P3 implemented 2026-10-06; P1.5 measured and decided the same day (every cap kept; see the phase notes); P4's spec revision to do. Q1–Q3 and Q5 ruled; Q4 open.
**Design:** [Lambda_Design_Shape_Transitions.md](../Lambda_Design_Shape_Transitions.md) §2–§5 and Appendix B (the 2026-10-06 survey of paths outside the tree). **Rulings touched:** D3.4.3v4 → v5 (external parents: every map type can be the parent of a runtime-tree edge, and the parent is never written), D3.4.4v2 → v3 (a string key's identity is its spelling). D3.4.5 is unchanged: an incompatible write still rebuilds and repacks; only where the rebuilt type comes from changes. **Closes:** the D3.4.3v4 residue recorded in [LR03-38](<../Lambda_Issue_Ledger (fixed).md#lr03-38>) and Shape_Transitions §8.
**Scope:** the four cases listed on 2026-10-06 — identity keys, spelling-keyed edges, the growth copy path, layout-changing writes — done by construction, so that no map is ever relaid out to join the tree. JavaScript objects, parser-side bounds and VMap/Velmt are out of scope except where one change serves both sides at no extra cost.

## 1. Where we start

A plain map shares its type only when it starts empty or already sits on a runtime-tree node (D3.4.3v4). Every other start takes the private path in `map_extend_open_shape`, which copies the whole field list into the context pool on **each** add. Debug build, peak RSS, 16 builds of a 1,024-key map (`temp/residue/*.ls`):

| Start | Peak RSS | Path |
|---|---|---|
| `var r = {}` | 34 MB | runtime tree |
| `var r = {seed: 0}` | 1.43 GB | private copy per add |
| `var r = parse("{\"seed\": 0}", 'json')^` | 1.43 GB | private copy per add |
| `let r = {*:base, extra: rep}` (1,024-key `base`) | 1.43 GB | spread adds Symbol keys; private from the first |

The jq benchmark VM pays rows 2 and 3 on every `obj_with` over a parsed object.

Facts the plan rests on, each checked in the code on 2026-10-06:
- **Why those maps are not on the tree.** A literal's or contract's `TypeMap` is a compile-time artifact of the module: immutable, read by concurrent satellite executions (D8.5.1v7), and baked into compiled code as a constant (the shape guards compare the receiver's type pointer against an immediate). A parsed document's types belong to its `Input`, whose lifetime is independent of any context (D4.2.6). A tree node takes edges on itself and lets a child extend its chain and hash table in place (D3.4.3v3), so neither kind of type can be a tree node without becoming mutable, shared state.
- **The runtime tree lives as long as one heap generation of the Runtime.** `context` is the thread-local `EvalContext*`; the `EvalContext` is the Runtime's canonical, retained one, re-bound for every evaluation. The tree is a capsule of it that owns its own pool (LR03-39). It has REALM lifetime and is dropped when the heap and name pool are replaced (LR03-43), because its entries carry that pool's NameIds. Tasks on other threads have contexts, and trees, of their own.
- **Direct JIT field reads on an inferred literal shape are shape-guarded** (`em_guard_map_shape`, T20-1a; `mir_emit_packed_record_shape_guard` for trusted records). A map that changes type pointer takes every such site's slow arm.
- **A string key is read by its spelling, whatever pool it came from** (`fn_map_set`, `_map_get_keyed`, `map_get_by_name_id_keyed`). Only edges split by pooling.
- **Lambda `Symbol` keys are string keys** (S8.2.1v4: a `NameKey` has string and symbol spellings normalized by exact contents). `NAME_KEY_SYMBOL`/`PRIVATE` exist only for JavaScript `Symbol()` and `#private` names, each pooled with a `NameId`.

## 2. Design: external parents

**The idea.** The tree does not need to own a type to grow from it. A map's current type — a literal's, a contract's, a nominal type's, a parsed document's node, a private type — becomes the *parent* of its first add, and the edge from it lives in the runtime tree, not on the parent. The child is a runtime-tree node, minted once per `(parent, key, value type)` per Runtime; it copies the parent's field list (a branch copy, which the tree already does when a parent's chain tail is taken) and from then on grows in place like any node. The parent is never written: no `transitions` entry, no chain-tail link, no table slot. Every map born from the same literal, the same contract, or the same parsed document node and grown the same way lands on the same child.

So "a literal resolves to a node per Runtime" is not a step at all. The literal *is* the parent; the first add from any map on it looks up `(literal, key, type)` in the tree's edge table — one hash lookup, the same cost as following any edge — and mints the child once. The second map born from that literal hits the cached edge. The cache is the edge table itself, keyed by the parent's pointer; there is no resolved type list, no per-site slot, and the literal-born map keeps the literal type until its first add, so every compiled guard keeps hitting while it is un-grown. Nothing in the transpiler changes.

### 2.1 The edge table

- One hash table per runtime tree, in the tree `Input` (`lib/hashmap.h`). Key: a **structural fingerprint** of the parent — everything a child copies from it (the record identity, and each entry's spelling, identity, contract, offset, flags, namespace and default) — mapping to an edge list in the tree's arena, walked like a node's own list. *Revised during P1:* the plan keyed by the parent's address, and the first stats run showed what that costs — 16 parsed documents of one shape minted 16 separate 1,023-node paths (16,368 mints, 0 hits); keyed by structure, they mint one (1,023 mints, 15,345 hits). A fingerprint collision only shares a list, since every child is still matched against the live parent in full.
- `transition_target_for_key` consults the parent's own edge list when the tree owns the parent (`type_tree_owns`), else the table. Parser `Input`s keep their edge lists and are untouched.
- A hit re-checks the child against the live parent **in full**, not with `map_transition_prefix_matches_parent`. Parents die — a module is freed, an `Input` released, and private types live in the heap's pool, which a batch resets between scripts — and a later type can take the address. The child copied pointers from its parent (`nominal`, `struct_name`, each entry's contract `Type*`, `ns`, `default_value`, the element's `content_list`), so a hit requires every one of them, and each entry's spelling, identity, offset and flags, to equal the live parent's (`external_child_matches_parent`). Then the child references only what the live parent references. The compare is O(fields), paid once per map at its first add; every later add follows an in-tree edge.
- Table edges keep the 16-edge fan-out cap when P1 lands, so P1 changes no bound. The cap exists to bound a *linear* edge walk. Table edges have one too: the table finds a parent's list by fingerprint, and the list is then scanned like any other. Whether table edges drop the cap, raise it or keep it was decided by measurement in P1.5 (Q5): they keep it. The 65,536-type budget bounds the tree in every case (§4).

### 2.2 What can be a parent

Any `LMD_TYPE_MAP` or `LMD_TYPE_ELEMENT` type whose container kind is `MAP_KIND_PLAIN` and that carries no JavaScript-only state (`js_meta`, fixed slots, flags, accessors, virtual entries) and no `has_spread` link slots. That admits literal types, trusted contracts, nominal instances, parsed document nodes from any `Input`, private types of any origin, and the tree's own nodes. The parent must be alive at the add, which it is: it is the map's current type. Excluded: non-plain kinds, JavaScript shapes, a `Map` borrowing an element's `TypeElmt` (`vmap_from_array`), and spread-slot types, which stay on the private path.

### 2.3 The child of an external parent

- **Entries are copied, names by bytes.** `clone_shape_entries_owned` copies the parent's entries, contracts and lanes intact, so the prefix has the parent's exact packing, and copies every entry name — and an element's tag spelling — into the tree's arena: a literal's names live in the module pool and a document's in its `Input` arena, and either can go while the Runtime lives. An external parent is never extended in place, so its chain tail, hash table and slot index are never shared. (The plan's lint for arena-owned names is not expressible statically; `TransitionTreeExternalParentTest` asserts it on every copied entry and reads a child after its parent's `Input` is released.)
- **Record identity is kept.** The child copies `nominal`, `is_nominal`, `struct_name`, `has_named_shape` and the element's `name`/`name_id`/`ns`, so a grown nominal instance is still an instance of its type (S2.1.4, as the private path already ensures). `is_trusted_contract` is false on the child, as today.
- **Cost.** One prefix copy of *n* entries per `(parent, key, type)` per Runtime, then O(1) per add. Today's path copies *n* entries per add per map.

### 2.4 Retype edges

`map_rebuild_for_type_change` (D3.4.5) mints a fresh private type per incompatible write. For a plain Lambda map it becomes a `retype(entry, type)` edge from the current type: the target has the same fields with that entry's lane replaced, minted once per Runtime and shared by every map retyped the same way; its children grow in place. The in-place cases stay in front — the `any` lane, a null placeholder on a shared type, native lanes, same type, `FLOAT`←`INT`, and the same-width retags `shape_entry_retag_is_safe` allows. JavaScript shapes keep `map_rebuild_for_type_change`.

A field that alternates between kinds alternates between two sibling targets, O(fields) to repack per flip as today but without allocation. If a workload shows it, the follow-up is an `any`-typed target for a field retyped more than once on one map.

### 2.5 Keys (cases 1 and 2)

- **Lambda `Symbol` keys** (`m['k'] = v`, every spread) are rejected today because `map_extend_via_runtime_tree` accepts only `LMD_TYPE_STRING` Items. The edge key is built from bytes (`transition_key_of_bytes`), and `Symbol`'s 16-byte header stops being an obstacle.
- **JavaScript identity keys** already match edges by `(name_id, key_kind)` and sit in shape tables by identity; the `property_key_requires_identity` gate at four sites (`type_tree_add_map_field`, `map_put_with_data_growth` ×2, `elmt_put_tree`, `type_tree_follow`) is residue from byte-only edges. All four are lifted in P0 (NI18; §6, Q3).
- **Array-index keys: no.** They exist only on JavaScript array companion maps (`MAP_KIND_ARRAY_PROPS`/`ARRAY_SPARSE`); index sets are per array and unbounded, so edges would never repeat. Lambda has no such keys. Keep them private.
- **Spelling identity (D3.4.4v3).** NameIds are per pool (`pool_number << 16 | ordinal`), so one spelling has a different id in the compiler's pool, each parse `Input`'s pool and the runtime pool, and none for `createString` keys or computed keys; edges split on that today. Rule: a `STRING`-kind name's identity is its spelling — equal non-`NONE` ids prove it, otherwise key kind, length and bytes confirm it; `SYMBOL`/`PRIVATE` by `NameId` alone. Applied to the edge table's key and compare, to the parser trees' edge lists (which then record spelling and hash for every `STRING` edge), to `typemap_shape_entries_equal`/`typemap_hash_holds_equal` (so a shared table never takes two entries for one spelling), and to `map_existing_shape_entry` (byte fallback after an id miss, so `map_put` never appends a duplicate).

### 2.6 Constructors that start on the tree

With external parents, construction rarely needs changing: a literal-born or document-born map is on the graph the moment it grows. The constructors that still mint a one-off type per value are moved onto the tree so they share:
- **Spread literals** `{*:base, x: 1}`: start on `EmptyMap` and add through `fn_map_set`; they join once `Symbol` keys are accepted. No other change.
- **Element literals with computed keys or spreads** (`elmt_literal_begin`): start on the literal's `TypeElmt`, which is now a valid external parent.
- **Regex match maps** (`runtime_result_shape`), **`io.grep` results**: built from the runtime root by following `(name, type)` edges (two or three lookups per value) instead of allocating a `TypeMap` and chain per match.
- **Group-by and join tuples**: start on the tree's `group` tag root (`elmt_tree_root(tree, …)`) and add attributes through `elmt_put_tree` on the runtime tree, instead of a fresh `TypeElmt` per group that `elmt_put` then clones.
- **Element edit transactions** (`write_set.cpp`): the same tag-root start.

### 2.7 The alternative, and why not

"Resolve each literal and contract to a per-Runtime node and attach that at `map()`" was considered first. It needs a per-Runtime resolved type list, every guard emitter to load the node pointer from a slot instead of an immediate, contract adoption to adopt the node, and edges keyed by the field *contract* rather than `TypeId` so that lane-typed literal fields keep their packing (a D3.4.2 change). External parents reach the same sharing with none of that, because the child copies the parent's entries verbatim and the literal keeps serving the compiled guards until the map grows. The one thing the alternative buys — `{a: 1}` and `{}`+`a = 1` landing on the *same* node — is not observable (type identity is not semantics) and costs one extra node per literal site.

### 2.8 What stays private, and why there is no replay

- **A tree decline by budget.** Past 65,536 types nothing can be minted; the map takes today's private copy. Replay could not help here: it would re-walk the same path and be declined at the same point. (Whether the fan-out cap stays on table edges is P1.5's measurement, §2.1.)
- **JavaScript shapes, non-plain kinds, spread-slot types, VMap/Velmt/VArray** (no `TypeMap`), as listed in Shape_Transitions Appendix B.
- **Foreign documents** — parsed under no Runtime, another Runtime, or a document whose own `Input` tree declined it — need no replay either: their types are external parents like any other, and the child copies the names. Replay is therefore not a phase of this plan.

### 2.9 JavaScript keys in the one name space (ruled 2026-10-06: NI18, D3.4.4v4)

S8.2.2v4 identifies a key by its resolved namespace and its normalized characters, and a namespace is a name resolved recursively to a root. JavaScript symbols join that space through this mapping, under a root namespace `js` (the host); proposed and accepted on 2026-10-06 as NI18 and D3.4.4v4:

| JavaScript key | Namespace | Characters | Identity it must reproduce |
|---|---|---|---|
| property name `"a"` | global | `a` | by spelling — already how Lambda and JS strings behave |
| `Symbol.for("k")` | `js.symbol.for` (the registry) | `k`, every byte | `Symbol.for("k") === Symbol.for("k")` process-wide |
| well-known `Symbol.iterator` | `js.symbol` | `iterator` | one singleton per well-known name |
| `Symbol("d")` | `js.symbol.unique.<n>`, one anonymous namespace per creation | `d` (the description, diagnostic) | each creation distinct; equal descriptions never equal |
| private `#x` | `js.private.<class>`, one anonymous namespace per class declaration | `x` | identity per declaration; the brand check is namespace membership |

The convention is a reading of the records the runtime already keeps, not a new representation: a registered or well-known symbol's `NameRecord` is interned per (kind, characters), so the record stands for (namespace, characters); a unique symbol's or private name's record is fresh per creation, so the record *is* its anonymous namespace. The edge key `(name_id, key_kind)` that `transition_target_for_key` already compares for identity kinds is therefore exactly `(resolved namespace, characters)` under this mapping, and the tree admits JavaScript keys with no change beyond lifting the `property_key_requires_identity` gates (§2.5). What the convention adds is a spelling for interop — how such a key prints and is written from Lambda (`js.symbol.for.k`, `js.symbol.iterator`) — which is a separate, smaller ruling once the mapping is accepted. D3.4.4v4 states the mapping; the interop spelling is the one open item.

## 3. Phases

Each phase lands on its own and is green on its gates before the next starts. Numbers are debug unless marked release.

### P0 — Keys: byte-view edges, identity keys admitted, spelling identity (§2.5)

- `input.cpp`: `transition_key_of_bytes`; `transition_target_for_key` mints from the view (`alloc_shape_entry_in` split into a bytes core and the `String*` wrapper); `TypeMapTransition` gains `name_hash`, every `STRING` edge records its spelling; the §2.5 match rule. `type_tree_add_map_field` takes the view; all four `property_key_requires_identity` gates lifted (NI18).
- `lambda-data.hpp`: `typemap_shape_entries_equal`, `typemap_hash_holds_equal`; `map_existing_shape_entry` byte fallback.
- `lambda-eval.cpp`: `map_extend_via_runtime_tree` accepts `LMD_TYPE_SYMBOL` keys.
- Tests: `TransitionTreeKeyIdentityTest` in `test/test_mark_builder_gtest.cpp` — pooled, unpooled and character-given spellings reach one node in either order; a JS `Symbol()` or private edge never matches by description bytes, and its record finds its edge again; entries of one spelling are equal across ids. (The planned "`map_put` onto an unpooled entry updates, never duplicates" was dropped: `map_put` appends a duplicate for an existing data key by design — JSON duplicate keys — and `map_existing_shape_entry` serves only the JS virtual-accessor case.) Fixture `test/lambda/proc/map_symbol_key_tree.ls` (symbol subscripts and `{*:base}` share with `{}` builds; `len`, order and values unchanged), in `kExtraLambdaScripts`.
- Gates: Lambda baseline; `test_mir_gc_stress_gtest`; `test_mir_ratchet_gtest`; input suites; test262 0 regressions if the JS gates are lifted. Measurement: the spread row of §1 at the `{}` figure.

**Done 2026-10-06.** Spread row of §1: 1.43 GB → 33 MB. Lambda baseline 6231/6232 (the one failure the environmental `pdf_svg_page_resources`); GC stress 300/300; test262 0 semantic regressions — three unicode-identifier cases reported "slow batch runtime ≥ 3000 ms", and a paired A/B against a pre-P0 binary on the same machine measured no difference (1.65–2.66 s both, varying with load). A namespaced Lambda symbol keeps the private path until keys carry their namespace (LR03-40).

### P1 — External parents (§2.1–§2.3)

- `input.cpp`: the edge table on `Input` (created with the runtime tree only); `transition_target_for_key` routes an unowned parent to it; the byte-copying `shape_entry_copy_as` variant for external parents; children copy `nominal`/`is_nominal`/`struct_name`/`has_named_shape` and the element identity fields. `type_tree_add_map_field` accepts any admissible parent (§2.2) and reports a decline distinctly from a refusal.
- `lambda-eval.cpp`: `map_extend_via_runtime_tree` drops the empty-plain/owned test for the §2.2 admissibility test; `map_extend_open_shape`'s private path remains for refusals and declines.
- Lint: a tree entry's `name` must be arena-owned (sibling of `no-raw-shape-chain-walk`).
- Tests: `TransitionTreeExternalParentTest` — two maps born from one literal and grown alike share a child, and the literal is unchanged (no `transitions`, chain tail unlinked, table untouched); the same for two maps from one parsed node, with the parse `Input` released after growth and the grown map still readable; a nominal instance keeps `is T` and its methods after growth; a stale parent pointer misses on the prefix check. Fixture `test/lambda/proc/map_external_parent.ls` — grow from a literal, a parsed object, a contract record, a nominal instance, a retyped map; `is`, `len`, iteration order, `format(_, 'json')`, a COW sibling unchanged; all three tiers (a `LambdaTierParityTests` entry) and `kExtraLambdaScripts`.
- Gates: Lambda baseline; GC stress; JIT golden sweep; MIR ratchet (no emission change expected — any change is a finding). Measurement: rows 2 and 3 of §1 within 10% of row 1; the jq rows re-timed (`run_benchmarks.py -s text -b jq_ -e c2mir --typed`, release) and the proposal table updated.

**Implemented 2026-10-06.** Rows 2 and 3 of §1: 1.43 GB → 33 MB and 39 MB, at the `{}` figure. `TransitionTreeExternalParentTest` (4) and the fixture `map_external_parent.ls` (JIT and auto, with the interpreter's index-assign rejection pinned, and in the forced-GC sweep). Writing the fixture found three pre-existing defects, all reproducing on master: LR12-39, fixed here (a COW copy of a parsed container kept `is_immortal`, so a later index write was lost); LR12-40 (a `var` bound to a member read aliases it) and LR12-41 (an index write on a parsed element copy is lost), ledgered open.

### P1.5 — Fan-out profiling (Q5)

**Instrumentation landed 2026-10-06** (with the structural-key revision of §2.1): process-wide counters split into the runtime tree and every other tree, written by `LAMBDA_SHAPE_TREE_STATS=<file>` as one line per class per process at exit, so batch runners aggregate across processes; `LAMBDA_SHAPE_TREE_FANOUT` (the runtime tree's table edges) and `LAMBDA_SHAPE_TREE_FANOUT_NODES` (its interior nodes) override the caps while stats are on. The plan's single override became two, since the jq objects grow mostly from in-tree nodes after their first add.

The 16-edge cap bounds a linear walk. Table edges have one as well, since the table only finds the parent's list. More sharing also spends the 65,536-type budget sooner, and a wide fan-out may mint paths nobody reuses. Neither effect was known in numbers, so the cap's fate was measured, not argued.

- **Instrumentation** (kept, behind `LAMBDA_SHAPE_TREE_STATS=1`, printed with `log_notice` at context teardown so release builds report it): per tree — the parser `Input`s, `js_input` and the runtime tree — the number of adds that hit an edge, minted one, or were declined by fan-out or by budget; a histogram of out-degree at the nodes that declined; the mean and maximum edge-list length walked per add; the number of maps that ended on a private type and the entries they copied; peak node count against the budget.
- **Settings**, as a measurement-only override `LAMBDA_SHAPE_TREE_FANOUT=<n>` applied to the runtime tree's table edges (0 = budget only); the constant is set once from the result and the override removed: 16 (today), 64, 256, and no cap.
- **Workloads**: `run_benchmarks.py -s text --typed` (the jq rows, with their 2,048-key objects, and the JSON rows) and `-s beng`; the Lambda baseline corpus through `lambda.exe test-batch`; the JavaScript benchmarks (`deltablue2`, `havlak`, `prettier_ast2`, `cd2`) and the test262 batch, since `js_input`'s edges are the same mechanism. Each setting records wall time, peak RSS, the decline counts and the private-map count; benchmarks as medians of five interleaved runs, release.
- **Decision rule**: the setting with the fewest fan-out declines that regresses neither time nor peak RSS beyond noise on every workload. If no cap wins on sharing but reaches the budget on the jq rows, raise the budget rather than keep the cap, and record both numbers. The outcome is written into §2.1, D3.4.3v5's footnote and Shape_Transitions §5.
- Gates: the instrumentation changes no emission (MIR ratchet) and no output (Lambda baseline); the override is removed before P2.

**Sweep status (2026-10-06):** the first sweep was stopped during the jq λ-VM rows, when `jq_bf_vm` read 181 s against the 122 s of the V1 runner table. Investigated before going on: it is the machine, not the code. A release build of `f5f18c058` (the V1 commit) measured 189 s the same hour, and interleaved runs of a shortened `jq_bf` input on both binaries gave equal user time (19.80 vs 20.01, 22.27 vs 22.48, 28.77 vs 29.90 s) and equal or lower peak RSS (349–371 vs 346 MB); load average was about 35 (two IDE indexers and other sessions' test runs), and one pair read 54 vs 110 s user for the same work. The stats counters cost nothing measurable (182.5 s without them). Counts are deterministic and can be gathered under load; the timing half of the decision needs a quiet machine, so the sweep is rerun when it is quiet.

**Result and decision (2026-10-06): every cap stays -- 256 at the root, 16 at interior nodes and on table edges.** Counts were gathered under load, since they are deterministic, and the sweep added a root override (`LAMBDA_SHAPE_TREE_FANOUT_ROOT`) once the counts showed where adds decline. A cap changes a decision only where an add reaches it, so phase A ran every workload once on the default caps, and only the workloads that declined ran the grid. Phase A covered 173 Lambda and 95 JavaScript benchmarks over nine suites, on the JIT, and the 1,119 Lambda test scripts through `test-batch`.
- **Below the root, nothing declines.** No workload declined an add at an interior node or a table edge; the longest walk outside the jq mix rows was 6 edges, and 8 in the corpus. The runtime tree's budget was never approached (peak 4,125 of 65,536 nodes).
- **JavaScript never reaches the runtime tree** (zero runtime-tree adds in all 95 scripts), so the decision cannot move a JavaScript workload. `js_input`'s own 16-edge declines (3.05 M in `jq_bf.js`, 2.31 M in the Octane runner) are that tree's question, outside this plan's scope.
- **The only decliners are `jq_mix`, `jq_mix2` and `jq_mix_vm`, all at the root.** `[range(2048) | {(tostring): .}]` builds 2,048 one-key objects with distinct keys, 3 times a round for 7 rounds, so 7 × 3 × (2,048 − 256) = 37,632 adds pass the root's 256 edges (51,996 in the VM row). Lifting the root cap removes them at the cost of longer walks, because each root add scans the list linearly and new edges go at its head:

| `jq_mix`, root cap | 256 | 512 | 1,024 | ≥ 2,048 |
|---|---|---|---|---|
| fan-out declines (private copies) | 37,632 | 32,256 | 21,504 | 0 |
| edges walked / mean per add | 10.4 M / 91 | 19.4 M / 169 | 33.1 M / 289 | 44.2 M / 385 |
| CPU vs 256, median of paired runs (`jq_mix` / `jq_mix2`) | -- | +0.4% / +0.2% | +4.6% / +3.2% | +9.2% / +10.5% |
| peak RSS vs 256 | -- | ±2 MB | -3 to -7 MB | -11 to -24 MB |

Timing used ABBA-ordered pairs of release runs measuring user+sys CPU (9 pairs; 7 for `jq_mix_vm`, which was neutral at +0.3%, within its ±5% spread). The no-cap pairs ran at load 4, and every pair fell within +8.4% to +11.4%; the 512 and 1,024 pairs ran as load rose again. The decision rule read literally picks 512, which declines 14% fewer adds at no measurable cost. The cap is kept at 256 anyway: the declines it avoids are throwaway one-key objects whose private copies cost no measurable memory (±2 MB of 750), and 1,024 and above cost CPU on every root add. Wide fan-out would pay only with an indexed edge list, which would need its own ruling. The overrides (`LAMBDA_SHAPE_TREE_FANOUT`, `_NODES`, `_ROOT`) are removed and `shape_tree_fanout_cap` is the constant pair again; the counters stay behind `LAMBDA_SHAPE_TREE_STATS`. A side finding: none of the corpus's 25,599 private copies (348,503 entries) came from fan-out or the budget. They are adds the tree does not admit, the residue listed in D3.4.3v5's footnote; they were not broken down by cause.

### P2 — Retype edges (§2.4)

**Implemented 2026-10-06.** `type_tree_retype_field` (input.cpp) mints the target as a copy of the parent's entries with the one field's lane replaced, so a `number` or union contract on another field survives (a replay through `type_tree_follow` would have reset every field to its TypeId's canonical type, which `map_rebuild_for_type_change` deliberately avoids); the target is keyed in the external table by the parent's fingerprint plus (field position, new type), repacked from offset 0 as the rebuild packs a private chain, and is an ordinary tree node afterwards. `map_rebuild_for_type_change` was split so its data move (`container_move_to_type`) serves both the private rebuild and the tree target. Contract admission's reification keeps the private rebuild, since it installs full field contracts. `TransitionTreeRetypeTest` (2) and the retype/flip cases added to `map_runtime_shape_tree.ls`, whose output matches the pre-P0 binary.

- `lambda-eval.cpp`: in `fn_map_set`, before `map_rebuild_for_type_change`, the retype edge for plain Lambda maps, sharing P1's child minting with the one entry's lane replaced.
- Tests: extend `map_runtime_shape_tree.ls` (the "retype" block) so two maps retyped alike share a type and a retyped sibling's reads are unchanged; forced-GC run of the same.
- Gates: Lambda baseline; GC stress; JIT sweep; MIR ratchet; `test_js_gtest` and test262 unchanged (the branch must be unreachable for JS maps).

**2026-10-08 correction (D3.4.3v5/D3.4.5):** shared plain Lambda shapes now take the runtime retype edge before constructor-specific JS detachment. Chart configuration spreads exposed the earlier ordering in the four-worker chart/PDF concurrency test: a type-changing override entered `js_typemap_transition_for_type` without an active JS realm. The shared `container_retype_field` helper serves both ordinary shape writes and this early dispatch; shared field metadata stays immutable. Fixed-slot recipes, active JS literal reservations, and compatible shared placeholder writes retain their existing paths. The existing concurrent chart/PDF test reproduces the crash before this correction and passes its full 20-second worker budgets afterwards.

### P3 — Constructors on the tree (§2.6)

**Implemented 2026-10-06.** `runtime_result_shape` (regex matches, `io.grep` records) follows one edge per field from the runtime root, so 6,000 match maps in a probe share two nodes (2 mints, 12,004 hits) where each had minted a `TypeMap` and chain. Group-by groups and join tuples start on the runtime tree's tag root (`runtime_shape_tree_element_root`, with `runtime_shape_tree` promoted out of lambda-eval.cpp) and add attributes through `fn_map_set`, so they grow by the tree with heap data and follow the setter's rules -- a repeated name updates its field, a list is stored as its array image (S2.5.6) -- where `elmt_put` had appended. Element edit transactions (`write_set.cpp`) were left as they are: their fresh empty `TypeElmt` per edit is already an external parent whose children are shared by structure (§2.1), so only that empty type per edit remains. `MatchResultsDoNotExtendCompilerTypeList` now also pins that two results, and a second call, share the tree node. P3 exposed [LR03-43](<../Lambda_Issue_Ledger (fixed).md#lr03-43>). The tree had outlived `runtime_reset_heap` and its name pool, so in a test-batch a group's `region` followed the previous script's `r` edge by a recycled NameId. The tree now ends with its heap generation (`runtime_shape_tree_release`). Gates: Lambda baseline 6,233/6,234 (the environmental `pdf_svg_page_resources`); the JIT-pinned golden sweep is clean apart from that same test; GC stress 301/301; the interpreter-pinned sweep fails only on its E501 unsupported constructs.

- Regex match maps and `io.grep` results from the runtime root; group-by, join and element edit transactions from a tag root; `elmt_literal_begin` unchanged (its literal is a parent after P1).
- Tests: existing goldens cover the values; a GTest pins that 1,000 matches of one pattern share one `TypeMap`.
- Gates: Lambda baseline; GC stress.

### P4 — Documents and ledger

- `doc/Lambda_Formal_Design.md`: D3.4.3v5 (external parents; the parent is never written; the edge table; no fan-out cap on table edges) and D3.4.4v3 in place, Appendix A footnotes with the P0–P3 numbers, spec version bump.
- `vibe/Lambda_Design_Shape_Transitions.md`: §2 (where a type comes from: every type is a possible parent), §3 (the edge table; children of external parents), §4 (retype edges), §8 (the residue bullet closes), Appendix B reduced to what still bypasses.
- Ledger: LR03-38's residue line closes; a fixed entry for the four cases.

## 4. Issues with the design

- **The budget is the remaining cliff.** More maps reach the tree, so 65,536 types are spent sooner, and past the budget growth is O(*n*²) again. Pressures: dictionary-shaped objects whose keys arrive in varying orders (a path per order), one-off private parents (a declined map's private type is a parent whose children nobody else uses), and retype siblings. P1 measures the jq rows (2,048-key objects) before the budget moves. The real answer for dictionary workloads is a hash-backed representation past the budget, which is a separate design (§6, Q4).
- **Spelling identity is a ruling change** (D3.4.4v3). The reads already behave this way; the edges and the equality helpers are aligned to them. It needs the ruling first (Q1).
- **External parents must be alive at the add.** They are, being the map's current type; a parent freed while a map still points at it is a use-after-free today, not a new hazard. The stale-pointer case is covered by the prefix check (§2.1).
- **Names are copied, not shared, for external parents.** Any path that reaches `shape_entry_copy_as`'s sharing variant with an external parent dangles once the owner goes; the lint and the P1 GTest (owner released after growth) guard it.
- **Contracts degrade only on the added field.** A child keeps the parent's lanes for the copied prefix; only new fields are `TypeId`-typed, as a runtime-grown map's already are.
- **The identity-key gate on the JavaScript paths** was kept across two refactors; lifting it is justified by the edge format, but test262 is the only proof (Q3).

## 5. Out of scope, recorded for later

JavaScript `map_put_heap` fallbacks beyond the identity-key gate (array-index shapes, non-plain kinds, descriptor clones, bounds); the `js_meta` root mismatch for an `Input` created before JS initializes; parser bounds (256/16/1,024); VMap, Velmt and VArray; `map_put_undefined_unique_absent_bulk*`; the `any`-typed retype target; a hash-backed map past the budget.

## 6. Open questions for the user

1. **D3.4.4v3 — string identity is spelling.** *Ruled 2026-10-06 (user), as S8.2.2v4 (Formal Semantics 58.0.0) and D3.4.4v3, since revised to v4 for JavaScript keys (Formal Design 26.0.0): one name space keyed by resolved namespace plus normalized characters; an interned id proves equality, never difference.* P0 implements it for the edges and the equality helpers. The namespace half has a separate gap at map writes and reads ([LR03-40](<../Lambda_Issue_Ledger.md#lr03-40>)), outside this plan.
2. **Scope of external parents (D3.4.3v5).** *Ruled 2026-10-06 (user): plain maps, nominal instances and elements together from P1.* The child copies the parent's identity fields either way; P1's fixture and GTests cover all three.
3. **Identity-key gates on the JavaScript and parser paths.** *Ruled 2026-10-06 (user): JavaScript symbols join the one name space under the §2.9 mapping (NI18, D3.4.4v4).* P0 lifts all four gates under a test262 gate.
4. **Degradation past the budget.** Keep today's private copies (my recommendation, with the jq measurement in P1), or open a design for a hash-backed plain map past the budget now?
5. **Fan-out.** *Ruled 2026-10-06 (user): decided by measurement, not by argument.* Whether the runtime tree's table edges drop the 16-edge cap, take a higher one, or keep it is P1.5's task: profile the benchmarks and the Lambda and JavaScript test corpora under each setting, then decide. Parser trees keep their caps regardless. *Measured and decided 2026-10-06 (P1.5): every cap stays. No workload reaches 16 below the root, and a wider root costs CPU in linear edge walks.*
