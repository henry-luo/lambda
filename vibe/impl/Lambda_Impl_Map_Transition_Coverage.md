# Map Transition Coverage — Implementation Plan

**Date:** 2026-10-06
**Status:** PROPOSAL — the analysis is done; the rulings in §6 are open; nothing is implemented.
**Design:** [Lambda_Design_Shape_Transitions.md](../Lambda_Design_Shape_Transitions.md) §2–§5 and Appendix B (the 2026-10-06 survey of paths outside the tree). **Rulings touched:** D3.4.3v4 → v5 (a plain map rejoins the runtime tree by replay), D3.4.4v2 → v3 (a string key's identity is its spelling). D3.4.5 is unchanged: an incompatible write still rebuilds and repacks; only where the rebuilt type comes from changes. **Closes:** the D3.4.3v4 residue recorded in [LR03-38](<../Lambda_Issue_Ledger (fixed).md#lr03-38>) and Shape_Transitions §8.
**Scope:** the four cases the user listed on 2026-10-06. JavaScript objects, parser-side bounds, `js_meta` roots and VMap/Velmt are out of scope except where one change serves both sides at no extra cost (noted per phase).

## 1. Where we start

A plain map shares its type only when it starts empty or already sits on a runtime-tree node (D3.4.3v4). Every other start takes the private path in `map_extend_open_shape`, which copies the whole field list into the context pool on **each** add and never rejoins the tree. Debug build, peak RSS, 16 builds of a 1,024-key map (`temp/residue/*.ls`):

| Start | Peak RSS | Path |
|---|---|---|
| `var r = {}` | 34 MB | runtime tree |
| `var r = {seed: 0}` | 1.43 GB | private copy per add |
| `var r = parse("{\"seed\": 0}", 'json')^` | 1.43 GB | private copy per add |
| `let r = {*:base, extra: rep}` (1,024-key `base`) | 1.43 GB | spread adds Symbol keys; private from the first |

The jq benchmark VM pays the second and third rows on every `obj_with` over a parsed object, which is most of what `jq_mix` and `jq_records` do.

Three facts the plan rests on, each checked in the code on 2026-10-06:
- **Direct JIT field reads on an inferred literal shape are shape-guarded.** `em_guard_map_shape` compares the receiver's `TypeMap` pointer with the one the site compiled against before any direct read (transpile-mir T20-1a), and trusted records are guarded by `mir_emit_packed_record_shape_guard`. Moving a map onto a tree node changes its type pointer, so every such site takes its slow arm. No direct read assumes an offset without the guard.
- **A string key is read by its spelling, whatever pool it came from.** `fn_map_set` matches a `STRING`-kind entry with `shape_field_name_equals`, `_map_get_keyed` with `typemap_shape_name_equals_hash`, and `map_get_by_name_id_keyed` walks the chain by bytes. Only edges split by pooling (§2.2).
- **Lambda `Symbol` keys are string keys.** S8.2.1v4: a `NameKey` has string and symbol spellings normalized by exact contents. A Lambda `Symbol*` is an arena value, never a `NameRecord` (NI13), so `fn_map_set` already treats one by bytes as `STRING`-kind. The `NAME_KEY_SYMBOL` and `NAME_KEY_PRIVATE` kinds exist only for JavaScript `Symbol()` and `#private` names, which are pooled with a `NameId` each.

## 2. The four cases

### 2.1 Symbol and private keys join the tree (case 1)

**Lambda symbols.** `map_extend_via_runtime_tree` refuses any key whose Item type is not `LMD_TYPE_STRING`, so `m['k] = v`, and every spread (`map_literal_spread` passes the `Symbol*`s that `item_keys` returns), go private. Fix: build the edge key from the symbol's bytes as a `STRING`-kind key. `Symbol` and `String` have different headers (16 vs 8 bytes), so `TransitionKey` gets a byte-view constructor (`transition_key_of_bytes(chars, len)`) and `transition_target_for_key` mints the entry from bytes (§3, P0). `type_tree_add_map_field` takes the view instead of a `String*`.

**JavaScript identity keys.** `transition_target_for_key` already matches an edge by `(name_id, key_kind)` when the edge has an id, and the shape tables already hold `SYMBOL`/`PRIVATE` entries by identity (`typemap_shape_entries_equal`, `typemap_hash_lookup_by_name_id`). The only thing keeping them out is the `property_key_requires_identity` gate at four sites: `type_tree_add_map_field` (input.cpp:706), `map_put_with_data_growth` (:773, :796), `elmt_put_tree` (:1076) and `type_tree_follow` (:1126). The gate dates from the name-identity implementation (30e3df9b0), when edges were matched by bytes only; D3.4.4v2's "Symbol and private entries never enter that byte fallback" is a rule about lookups, and an identity edge is never matched by bytes. Lifting the gate is one line per site. The Lambda side needs only the first; the other three are JavaScript and parser paths, gated by test262 in P0 (§6, Q3).

**Array-index keys: no.** They exist only on JavaScript array companion maps (`MAP_KIND_ARRAY_PROPS`, `MAP_KIND_ARRAY_SPARSE`, `has_array_index_shape`): a sparse write `a[1000] = x` lands its numeric key there. Index sets are per array and unbounded, so sequences rarely repeat, each new index is a new edge, and the 16-edge fan-out would be spent on one array; the nodes it minted would hold one map each. V8 keeps element storage out of hidden classes for the same reason. Lambda has no such keys — `m["0"] = v` on a plain map is an ordinary string key and already joins. Keep them private.

### 2.2 Unpooled spellings share edges (case 2)

`transition_key_of_string` keys an edge by `name_id` when the key is pooled, else by bytes with `name_id == NONE`; the match requires the same form on both sides. NameIds are per pool (`pool_number << 16 | ordinal`, name_pool.cpp:109), so the same spelling has a different id in the compiler's pool, a parse `Input`'s pool and the runtime pool, and no id when a parser used `createString` (CSV headers, markup and graph attributes) or the key was computed (`"k" ++ string(i)`). Result: a map built with pooled keys and one built with unpooled keys of the same spelling never share a node, and neither shares with a parsed one.

**Rule (D3.4.4v3, Q1):** a `STRING`-kind name's identity is its spelling. Equal non-`NONE` ids prove it; otherwise key kind, length and bytes confirm it. `SYMBOL` and `PRIVATE` names are identified by `NameId` alone. This is what every read already does; the change aligns edges and the entry-equality helpers with it.

Implementation:
- `TypeMapTransition` keeps `name`/`name_len` for every `STRING` edge (today only id-less ones) and gains `name_hash` (the entry's FNV hash, routing only). Matching: kind first; identity kinds by id; `STRING` by id when both ids are set and equal, else hash, length, bytes. The walk is bounded (16 or 256 edges) and the hash rejects almost every non-match before a `memcmp`.
- `typemap_shape_entries_equal` and `typemap_hash_holds_equal`: two `STRING` entries with different set ids and the same bytes are equal. Without this a shared table could take a second entry for one spelling, which breaks the D3.4.3v3 invariant once edges unify.
- `map_existing_shape_entry` (input.cpp:722): a pooled `STRING` key falls back to a byte lookup when the id lookup misses, so `map_put` never appends a duplicate of a field minted under another pool's id. Today this cannot happen because edges split; after unification it could.
- A minted entry keeps whatever id its first key had. Lookups do not depend on it (§1).

### 2.3 The growth copy path goes through the tree (case 3)

**Mechanism: replay.** On the first add to a plain map whose type is not a runtime-tree node, `map_extend_via_runtime_tree` replays the map's fields from the runtime root — one edge per existing field, keyed by the field's spelling (or identity) and the current value's `TypeId` — then follows or mints the edge for the new field, allocates a data buffer for the target node (doubling, as today), stores every value at the node's offsets and installs the type. The map lands on the node a map built from `{}` by the same adds would reach; from then on each add is one edge. Cost: one O(fields) relayout per map, the same order as today's first copy, then O(1) per add instead of O(fields).

Which starting types replay (the private cases of Shape_Transitions B.2):
- a map literal's type (`{seed: 0}`, flagged shared but not a tree node);
- a node of another `Input`'s tree (a parsed document; `type_tree_owns` is false);
- a trusted contract type after `lambda_map_set_checked_impl` admitted the add (growth already clears `is_trusted_contract`);
- a private type of any origin (an earlier copy, a detached clone, `map_rebuild_for_type_change`'s result);
- a `has_spread` literal type: the nameless link slots are flattened through `map_collect_flat_keys` and `_map_get_keyed`, which is the `for (k, v in m)` view (first position, last value).

Not replayed, left as today: nominal instances and elements unless Q2 includes them; non-`PLAIN` map kinds; types with `js_meta`, fixed slots, flags, accessors or virtual entries (JavaScript); any map once the tree declines (fan-out or budget).

**Details that matter.**
- **Field contracts degrade to the value's kind.** A literal or contract entry may carry `int?`, a union or `any`, stored as a typed lane; the replayed node's entry is the value's `TypeId`, as a runtime-grown map's already is. Reads are unchanged. A later write of another kind takes the retype path (§2.4) instead of the lane. The `any` lane (LR03-37) is lost for that map; a field that flips kinds will replay per flip (see §2.4's note).
- **Names cross `Input`s by copy.** `shape_entry_copy_as` shares the `StrView` pointer, which is only safe within one `Input`. Replay from a foreign entry mints through the byte-view constructor, so `alloc_shape_entry_in` copies the bytes into the runtime tree's arena, exactly as a `String*` key is copied today.
- **Rooting.** Values are read into a `RootSpan` first (reading a wide scalar may box), the path is followed (tree allocations are arena, no GC), the buffer is allocated last and the data pointer re-fetched after it, then the stores run. This is the order `map_rebuild_for_type_change` already uses.
- **COW.** `clone_mutable_map` copies the type pointer and the data, so `var r = o; r.k = v` detaches `r` before the add and only `r` moves; `o` keeps its type. Unchanged.
- **Trusted-contract guards.** After growth the map's type is a tree node, so `emit_checked_boundary` and `mir_emit_typed_path_store` miss and take their checked arms, as they do for today's private copy (LR07-44 is about the un-grown case).

### 2.4 Layout-changing writes go through the tree (case 4)

`map_rebuild_for_type_change` (D3.4.5) rebuilds a fresh pool type per incompatible write and copies `is_private_clone` from the old one. On a runtime-tree node this leaves the tree; the next add then replays (§2.3), but every retype still allocates a chain.

**Mechanism: replay with one step changed.** In `fn_map_set`, before the rebuild, a plain Lambda map (no `js_meta`, no fixed slots, no flags, accessors or virtual fields) replays its fields from the runtime root with the changed field's `TypeId` replaced. The result is a sibling path; once it exists, a retype allocates nothing but the data buffer when the width grows. The editor already does this for documents (`rebuild_steps` + `type_tree_follow` with `like` steps and a new `type_id`), so the runtime reuses `type_tree_follow` with byte-view steps. The in-place paths stay in front: the `any` lane, a null placeholder on a shared type, native lanes, same type, `FLOAT`←`INT`, and the same-width retags that `shape_entry_retag_is_safe` allows.

Note: a field that alternates between two kinds replays on every flip, O(fields) each, which is today's cost without the allocation. If a workload shows it, the follow-up is an `any`-typed edge for a field retyped more than once on one map; not in this plan.

## 3. Phases

Each phase lands on its own and is green on its gates before the next starts. All numbers are debug unless marked release.

### P0 — Keys: byte-view edges, identity keys admitted, spelling identity (cases 1 and 2)

- `input.cpp`: `TransitionKey` gains `transition_key_of_bytes(chars, len, name_id, key_kind, hash)`; `transition_key_of_string` and `transition_key_of_entry` become wrappers. `transition_target_for_key` mints from the view (`alloc_shape_entry_in` split into a bytes core and the `String*` wrapper). `TypeMapTransition` gains `name_hash`; `STRING` edges always record their spelling; the match rule of §2.2.
- `type_tree_add_map_field(Input*, TypeMap* parent, const TransitionKey*, TypeId, ShapeEntry**)`; the four `property_key_requires_identity` gates lifted (the three JavaScript/parser ones only if Q3 says so).
- `lambda-data.hpp`: `typemap_shape_entries_equal` and `typemap_hash_holds_equal` per §2.2; `map_existing_shape_entry` byte fallback.
- `lambda-eval.cpp`: `map_extend_via_runtime_tree` accepts `LMD_TYPE_SYMBOL` keys through the byte view.
- Tests: `TransitionTreeKeyIdentityTest` in `test/test_mark_builder_gtest.cpp` — pooled and unpooled spellings of one key reach one node; two pools' ids reach one node; a JS `Symbol()` edge is never matched by its description bytes; `map_put` of a pooled key onto an unpooled entry updates, never duplicates. Fixture `test/lambda/proc/map_symbol_key_tree.ls` (symbol subscripts and `{*:base}` share with `{}` builds; `len`, order and values unchanged), in `kExtraLambdaScripts`.
- Gates: Lambda baseline; `test_mir_gc_stress_gtest`; `test_mir_ratchet_gtest`; input suites (parsers mint through the same function); test262 0 regressions if the JS gates are lifted. Measurement: the spread row of §1 at the `{}` figure.

### P1 — Replay on first add (case 3)

- `lambda-eval.cpp`: `map_replay_onto_runtime_tree(Map*, Input* tree, const Item* values, int count, ...)` — the two-pass walk of §2.3 (flattening `has_spread` through `map_collect_flat_keys`/`_map_get_keyed`), used by `map_extend_via_runtime_tree` when the type is neither empty-plain nor a node of the tree and the map qualifies. The private path remains for everything that does not qualify and for a declined tree.
- Per-nominal roots and element tag roots in the runtime tree if Q2 includes them: a `TypeNominal*`-keyed root table on `Input` after the `element_roots` pattern, children inheriting `nominal`/`is_nominal`/`struct_name`; elements through `elmt_tree_root(tree, tag, ns)` with the tag resolved from `name_id` or interned once.
- Tests: fixture `test/lambda/proc/map_replay_tree.ls` — grow from a literal, a parsed object, a contract record, a retyped map and a spread literal; `is`, `len`, iteration order, `format(_, 'json')`, a COW sibling unchanged; run on all three tiers (a `LambdaTierParityTests` entry) and in `kExtraLambdaScripts`. A GTest pins that the literal-start build shares the `{}`-start node (`map->type` pointer equality after the same adds).
- Gates: Lambda baseline; GC stress; JIT golden sweep (`LAMBDA_EXEC_BACKEND=jit ./test/test_lambda_gtest.exe`); MIR ratchet (replay is runtime, so no emission change is expected — a change is a finding). Measurement: rows 2 and 3 of §1 within 10% of row 1; the jq rows (`run_benchmarks.py -s text -b jq_ -e c2mir --typed`, release) re-timed and the proposal table updated.

### P2 — Retype through the tree (case 4)

- `lambda-eval.cpp`: in `fn_map_set`, the §2.4 branch before `map_rebuild_for_type_change`; it shares P1's replay core with a `changed_entry`/`new_type_id` pair. `map_rebuild_for_type_change` stays for JavaScript shapes and anything the branch declines.
- Tests: extend `map_runtime_shape_tree.ls` (the "retype" block) to assert that two maps retyped the same way share a type, and that a retyped sibling's reads are unchanged; a forced-GC run of the same.
- Gates: Lambda baseline; GC stress; JIT sweep; MIR ratchet; `test_js_gtest` and test262 unchanged (the branch must be unreachable for JS maps).

### P3 — Documents and ledger

- `doc/Lambda_Formal_Design.md`: D3.4.3v5 and D3.4.4v3 in place, Appendix A footnotes with the P0–P2 numbers, spec version bump.
- `vibe/Lambda_Design_Shape_Transitions.md`: §2 (where a type comes from), §3 (the runtime tree: replay), §4 (retype through the tree), §8 (the residue bullet closes; what remains is listed), Appendix B updated to what still bypasses.
- Ledger: LR03-38's residue line closes; a new fixed entry for the four cases.

## 4. Issues with expanding the tree

- **Budget and fan-out saturation is the remaining cliff.** Past 65,536 runtime nodes, or 16 edges from one node, an add is declined and the map goes private with the old O(n²) copies; replay makes more maps reach the tree, so the budget is spent sooner. Two pressures: dictionary-shaped objects whose keys arrive in varying orders mint a path per order, and retypes mint sibling paths. Measure on the jq rows (2,048-key objects) in P1 before deciding whether the budget moves; §6 Q4 asks how a declined map should degrade.
- **Edge identity across pools is a ruling change.** D3.4.4v2 confines the byte seam to id-less `Input` fields; §2.2 widens it to every `STRING` name. The reads already behave this way, so this is a doc-and-edge alignment, but it needs the ruling first (Q1).
- **Contracts degrade on replay.** A grown map's entries become `TypeId`-typed (§2.3). This is already true of every runtime-grown map, and the contract is re-established only by re-admission, but it is worth stating in D3.4.3v5 so it is not read as a bug.
- **`shape_entry_copy_as` shares names.** Any replay that reaches it with a foreign entry dangles once the source `Input` goes; P0's byte view exists so that P1 never calls it across `Input`s. A lint-style check (the entry's `name` must lie in the tree's arena or be minted from bytes) belongs in the GTest.
- **The identity-key gate on the JavaScript paths** was kept across two refactors; lifting it is justified by the edge format, but the test262 run is the only proof. If Q3 defers it, P0 lifts only the Lambda gate.
- **Thread model.** `context` is thread-local and the runtime tree is a capsule of that context, so tasks on other threads never touch it. No change.
- **Elements and nominal instances** need roots the runtime tree does not have (Q2). Without them they stay on the private path, which the appendix will keep listing.

## 5. Out of scope, recorded for later

JavaScript `map_put_heap` fallbacks beyond the identity-key gate (array-index shapes, non-plain kinds, descriptor clones, bounds); the `js_meta` root mismatch for an `Input` created before JS initializes; parser bounds (256/16/1,024); VMap, Velmt and VArray (no `TypeMap`); regex, `io.grep`, group-by and join result types; `map_put_undefined_unique_absent_bulk*`; the `any`-typed edge for flip-flopping fields.

## 6. Open questions for the user

1. **D3.4.4v3 — string identity is spelling.** Confirm the rule in §2.2: a `STRING`-kind name is identified by its spelling (ids are a fast path), `SYMBOL`/`PRIVATE` by `NameId`. Without it, case 2 cannot be done consistently with the reads.
2. **Scope of replay (D3.4.3v5).** Plain structural maps only (my recommendation for P1), or also **nominal instances** (a per-nominal root table; closes `var p: Point = …; p.extra = 1` growth) and **elements** (tag roots in the runtime tree; closes `e.attr = v` growth and `elmt_literal_begin`)? Both are a day each on top of P1 and reuse the element-root pattern.
3. **Identity-key gates on the JavaScript and parser paths.** Lift them in P0 under a test262 gate, or defer to the JavaScript round? Lambda needs only the `type_tree_add_map_field` gate.
4. **Degradation past the bounds.** Keep today's behaviour (a declined map goes private and copies per add), or should a plain map that the tree declines because of fan-out switch to an in-place private chain? The second needs the ownership mark LR03-38 rejected, so my recommendation is to keep the fallback, measure the jq rows in P1, and revisit only with numbers.
5. **Retype cost.** Accept O(fields) per retype without allocation (P2), with the `any`-typed edge as a later follow-up?
