# Lambda MIR Cache Level 3 — Code-Image Cache Detailed Design

**Status:** Proposal — elaborates MC6 (Route B) of `vibe/Lambda_Design_MIR_Cache.md` §5.3 into an implementable design
**Date:** 2026-07-24
**Scope:** Lambda modules and main scripts under MIR Direct. The container format, relocation journal, loader, and verifier are designed **language-neutral** so LambdaJS and other Jube guests can adopt them later (§10); only the Lambda de-pointering plan (§6) is language-specific.
**Companions:** `Lambda_Design_MIR_Cache.md` (levels & prior decisions MC1–MC8; L1 is implemented per `Lambda_Impl_MIR_Cache_L1 (done).md`), `Lambda_Desing_Native_Module.md` §7.4/P14 (cache-not-distribution), `Lambda_Design_JS_Cache.md` (JS in-process cache; supplies the fail-closed state discipline reused here), `Lambda_Design_MIR_Emission_Test.md` (dump/verifier infra reused for the determinism gate).

---

## 0. The two framing questions, answered up front

**Q1 — de-pointer the MIR lowering (opt 1), or keep the lowering and patch pointers at image save/load (opt 2)?**

**Opt 1, decisively — reaffirming MC4 — with one important refinement: de-pointering is necessary but not sufficient.** The investigation (§2) shows the two options are not symmetric alternatives; they address different halves of one problem:

- The addresses our *lowering* bakes (P1–P6, §2.2) are emitted as **anonymous 64-bit integer immediates**. By the time machine code exists, nothing distinguishes them from user integer constants; MIR-gen keeps **no relocation records at all** (§2.1), its constant folding can merge or transform the values (`get_ref_value` feeds `get_int_const`, so a baked pointer can survive only as `ptr+k`), and on aarch64 each 64-bit immediate is shredded into four `movz`/`movk` chunks. Every save-time patching variant of opt 2 is therefore either **unsound** (classify immediates by value → collides with folding, GVN, and coincidental equal integers) or collapses into opt 1 (annotate pointer operands at emission time — which *is* touching the lowering, with extra machinery and none of opt 1's side benefits). Opt 2 is rejected as the primary mechanism (L3-1).
- After de-pointering, process-specific addresses still enter the code — but only through **MIR's own item-address channels** (import addresses, function/thunk addresses, data-item addresses, switch tables, lrefs — §2.1). Those are enumerable, carry item identity at the emission point, and are captured soundly by a handful of gen-time hooks (§5.4). That residue is what the relocation journal covers. *De-pointering's real purpose is to shrink the address problem to exactly the channels MIR knows about.*
- Opt 2's one sound trick — compile twice at different addresses and diff the images — is **salvaged as the write-time verifier** (§5.5): every cache entry is born verified, and any byte the journal cannot explain fails the write closed (the module simply isn't cached). This converts the parent doc's "journal completeness cliff" from a silent-corruption risk into a deterministic write-time error.

**Q2 — how to serialize the const data companion to the MIR image?**

**Stage 1: don't.** The decisive fact (§2.3): `const_list` and `type_list` are populated during **AST building**, not transpile, and their entries point into the script's own compile pool (`&ft->double_val` literally points into an AST type node; `ShapeEntry::default_value` is an `AstNode*`). A faithful serializer must drag half the AST along. Instead, the recommended load model (**Model A**, L3-2) re-runs the cheap front-end phases on a cache hit — parse → AST → transpile-to-MIR — which *regenerates* `const_list`/`type_list`/name-pool contents with fresh, valid pointers and (by the determinism gate, §7) identical indices; only `MIR_gen`, the measured dominant cost, is replaced by image load + journal patch. The sidecar then carries **no const data at all** — just code bytes, the journal, and a per-function table.
**Stage 2 (deferred, additive):** if profiling shows front-end cost matters for some workload, a **self-contained image** (Model B) adds sidecar sections that serialize const values by kind and a flattened type graph; the concrete record design and its honest costs are specced in §9 so the decision is pre-made, but it is not v1.

---

## 1. Problem, goals, non-goals

**Goal:** production cold start pays neither transpile nor `MIR_gen` for unchanged code. Level 1 (landed) already de-duplicates compilation *within* a process; Level 3 carries generated machine code *across* processes as a local, regenerable cache.

**Non-goals:** distribution of compiled artifacts (P14/MC7 — scripts ship as source; the cache is keyed by build ID and silently invalidates on any runtime change); caching JS or other guests in v1 (§10 defines their entry criteria); caching partially-generated (lazy) modules (§11); sandbox-grade integrity against a hostile local user (§8.6).

**Success criteria:** (a) cache-hit cold start replaces `mir_gen` phase time with image load ≤ ~1ms/module; (b) `make test-lambda-baseline` 100% with the cache forced on and forced off; (c) a corrupted/stale/truncated cache file can never crash or mis-execute — worst case is a full recompile; (d) zero behavior change when the cache directory is absent.

---

## 2. Investigation facts (verified 2026-07-24)

File:line references are to the current tree (`lambda/runtime/…` after the static-modules re-layout; the parent doc's `lambda/…` citations are the same files pre-move).

### 2.1 How process-specific addresses enter MIR-generated artifacts

Verified in `mac-deps/mir/` (which already carries small local modifications, e.g. the post-simplify NULL-label cleanup in `MIR_link`, `mir.c:2067-2085` — precedent for guarded local patches).

**Central negative fact: MIR-gen retains no relocation information.** The per-function fixup lists (`abs_address_locs`, `const_refs`, `label_refs`, `call_refs`) are truncated per translate and discarded (`mir-gen-x86_64.c:2856-2862`, `mir-gen-aarch64.c:2474-2476`). `MIR_write`/`MIR_read` serialize **IR by name** — no machine code (`mir.c:4929+`) — which is why MC3 (bitcode cache) caches the wrong phase.

**Address channels** (the generator reads addresses from `item->addr` via `get_ref_value`, `mir-gen.c:773-779`):

| # | Channel | x86_64 form | aarch64 form | Tracked by MIR? |
|---|---------|-------------|--------------|-----------------|
| C1 | Import call target (`item->addr` = resolver result) | abs64 word in per-function **constant pool appended after the code**, `call *disp32(rip)` (`mir-gen-x86_64.c:1847`, `2705-2708`, pool written `2889`) | `movz/movk` ×4 + `blr` (`mir-gen-aarch64.c:256-263`, `2303`) | no |
| C2 | Same-module call (`item->addr` = **thunk**) | `change_calls` → `rel32` to thunk if in range (`2988-2994`); at link-finish `target_change_to_direct_calls` rewrites to `rel32` **direct to callee `machine_code`** unless `MIR_set_func_redef_permission(TRUE)` (`3013-3058`) | `movz/movk` of thunk + `blr` — **always via thunk** (`target_change_to_direct_calls` is a no-op, `mir-gen-aarch64.c:2533`) | no |
| C3 | Data-item address into a register (BSS/`data`/`ref_data`/float-literal `.lc` items; `MIR_OP_REF`) | `movabs reg, imm64` (`mir-gen-x86_64.c:1586`, `2657-2659`) — **not recorded anywhere** | `movz/movk` (`mir-gen-aarch64.c:2297-2309`) | **no** |
| C4 | Address-of-function as a value | same encoders; value = the **thunk** | same | no |
| C5 | `MIR_SWITCH` jump table: 8-byte **absolute label addresses in the code stream** | slots recorded transiently in `abs_address_locs`, rebased `base+disp` via `_MIR_update_code_arr` (`mir-gen-x86_64.c:2807-2872`, `2998-3008`) | identical scheme (`mir-gen-aarch64.c:2426-2529`) | transient |
| C6 | Thunk bodies (13B x86 / 16B a64) | `jmp rel32` or `movabs r11; jmp *r11` (`mir-x86_64.c:155-192`) | `b rel26` or `ldr x9,#8; br x9; .quad addr` (`mir-aarch64.c:203-226`) | n/a — re-pointable |
| C7 | `ref_data`/`expr_data` slots in **data memory** | absolute pointer written at link (`mir.c:2033-2058`) | same | recomputed each `MIR_link` |
| C8 | Single-label `lref_data` slot = `func_code + disp` written into data at **gen** time (`gen_setup_lrefs`, `mir-gen.c:781-789`) | same | no (two-label lref is a PI difference) |

**Levers that already exist:** every function item's `item->addr` is a stable thunk created at `MIR_load_module` (`mir.c:1928-1934`) and re-pointable via `_MIR_redirect_thunk`; `_MIR_publish_code` installs arbitrary bytes into executable memory and `_MIR_set_code`/`_MIR_update_code_arr` apply caller-supplied reloc lists with W^X + icache handled (`mir.c:4426-4512`; macOS uses `MAP_JIT` + `pthread_jit_write_protect_np` + `sys_icache_invalidate`, `mir-code-alloc-default.c:33-61`). `generate_func_code` itself is just "publish + set `machine_code`/`call_addr` + redirect thunk" (`mir-gen.c:9504-9529`) — i.e. **installing cached code without running the generator is a supported composition of existing primitives**.

**The folding hazard (kills value-based patching):** a data-item `REF` can be consumed by constant folding (`get_int_const` → `get_ref_value`, `mir-gen.c:5601-5602`), so the byte pattern in code may be `item_addr + k`, not `item_addr`. Any scheme that identifies pointers in finished code *by value* is unsound. Capture must happen where item identity is still in hand.

### 2.2 The lowering's baked-pointer inventory (current sweep)

`emit_load_const` already routes literal values relocatably: index into `const_list` through the pinned per-function `consts_reg`, loaded once per function from BSS `_mod_consts_ptr` (`transpile-mir.cpp:2119-2139`, BSS created `:15275-15281`, patched with the live pointer at `:15744-15758`). The exceptions — raw host pointers emitted as immediates — are:

| Cat | Sites (`lambda/runtime/transpile-mir.cpp`) | Points into | Path heat |
|-----|--------------------------------------------|-------------|-----------|
| P1 interned name strings | `emit_load_string_literal :1483` (single shared helper, ~20 callers); member-access field name baked as a fully-tagged STRING Item `:7462` | per-script name pool (`script->pool`) | **hot** (member access) |
| P2 module `type_list` pointer | `emit_load_module_type_list :2129` (raw), though BSS `_mod_type_list_ptr` already exists and is used at `:13284`, `:14731`, `:14908`, `:15332` | per-script list object | warm |
| P3 `TypeMap*` descriptors | `:6841` (map construction) | compile pool type nodes | warm |
| P4 binary globals: `&LIT_TYPE_DATE/TIME/LIST/NUMBER/INTEGER` `:10896-10930`, sized-type singletons `:10954`, static strings `" "` `:8452` and `"side-stack"` `:704` | exe rodata/data (ASLR-shifted per launch) | prologue + type ops |
| P5 sys-function native addresses | `info->func_ptr :12263` | exe code (ASLR-shifted) | setup |
| P6 static literal containers | `static_const_array/map_from_node :2329/:2361` fold literal collections into `script_pool`-owned `Array*`/`Map*` (`is_static`/`is_immortal`) whose tagged Item is then emitted as an immediate | per-script pool | literal-heavy code |

(The MIR-emission-test doc §2.7 counts "12 sites"; this sweep is the authoritative refresh. All are `MIR_new_int_op` immediates — channel-invisible to MIR, hence §0-Q1.)

### 2.3 Const/type side tables are AST-build products with pool-internal pointers

- `const_list` entries are appended **during AST building** with indices assigned in append order: symbols `build_ast.cpp:1868`, int64 (`&it->int64_val` — a pointer *into the AST node*) `:1884`, doubles (`&ft->double_val`) `:1903`/`:2764`, binaries `:2914`, strings `:3174`. `type_list` likewise: `:3933`, `:4158`, `:4192`, `:5387`, `:5723`, `:5774`.
- The type graph is deep and self-referential: `TypeMap` carries a `ShapeEntry` chain whose entries hold `StrView* name`, `Type*`, `Target* ns`, **`AstNode* default_value`**, plus rebuildable caches (`field_index[32]`, `slot_entries`, `transitions`) (`lambda-data.hpp:244-330`). `TypeMap::type_index` (`:266`) is the stable ordinal into `type_list`.
- Each script owns its compile-time pools: `transpile_script` creates a fresh `Input` base supplying `tp->pool/arena/name_pool/type_list` (`runner.cpp:573` area; confirmed by the L1 doc §2.1). The name pool is content-addressed (`name_pool.cpp:19`, `HASHMAP_DEFINE_LENSTRKEY`), arena-backed, re-internable at any time.
- Pub-symbol registration for importers reads the dep's **live `ast_root` + `jit_context`** (`register_module_pub_fns`, `transpile-mir.cpp:15507-15600`) — so any load model must leave a cached module with an AST and a linked MIR context (Model A does; Model B must serialize an interface summary instead, §9.3).

### 2.4 Determinism status

Registers, labels, and proto/import names are deterministic per fresh compile (emission-test doc §2.7); MIR emission is byte-identical across debug and release hosts (MT-harness finding). The only emission-order dependencies on hashmap iteration found are `async_restore_vars` (`transpile-mir.cpp:1864`) and `callsite_info` walks (`:14484/:14498`) — both content-keyed with fixed seeds, hence cross-process stable. Residual risk is pointer-*value* dependence, which de-pointering removes and the §7 gate enforces.

### 2.5 Infrastructure already available

Binary file IO (`write_binary_file`/`read_binary_file`, `lib/file.h:21/:65`; atomic text write precedent `:74`), SHA-256 + streaming digests (`lib/digest.h:33`), xxhash3/FNV (`lib/hash.h`), zstd + zlib already linked (`build_lambda_config.json:199-202`, `:460`), mmap arena precedent (`pack.cpp:166-173`). Profile phases `parse|ast|transpile|jit_init|file_write|c2mir|mir_gen` land in `temp/phase_profile.txt` (`runner.cpp:113-243`) — the measurement harness for §13 gates. A manually-bumped ABI stamp exists (`JUBE_HOST_BUILD_ID`, `lambda/jube/jube.h:23`) but **no automatic build ID is embedded in `lambda.exe`** — a real build stamp is a prerequisite work item (D0, §13).

---

## 3. Option analysis in full (Q1)

### 3.1 Opt 2 variants and why each fails as the primary mechanism

| Variant | Mechanism | Fatal flaw |
|---------|-----------|------------|
| 2a value scan | At save, enumerate live pointer registries (name pool, type nodes, `LIT_TYPE_*`, sys-fn table); scan code bytes for their 64-bit encodings; journal matches | Unsound: folding may store `ptr+k` (§2.1); aarch64 splits values across `movz/movk`; a user integer literal equal to a live address patches wrongly — silent corruption, undetectable at load |
| 2b differential diff | Compile twice at different addresses, diff images, journal differing bytes | Sound only for *discovery*, not *meaning*: a differing byte tells you where, not what to re-point it to; classifying the "what" reduces to 2a. Retained as the **write-time completeness verifier** (§5.5), where "unexplained differing byte" simply vetoes caching |
| 2c emission-time annotation | Record `(insn, operand) → semantic` in a side table at each P-site, then hook the generator's immediate encoders to journal annotated operands | Is opt 1 with extra steps: it touches every lowering site *and* adds per-target encoder hooks for anonymous immediates, keeps the in-process pool-address fragility, forfeits byte-golden dumps, and still fights GVN/folding (annotation must survive `simplify`/`combine` operand rewriting) |
| 2d patch MIR bitcode instead of machine code | `MIR_write` image with IR-level pointer rewriting | Caches the wrong phase — re-runs `MIR_gen` on every load (MC3, already dropped) |

### 3.2 What opt 1 does and does not buy

De-pointering (§6) eliminates categories P1–P6 so that generated code contains only **names, indices, and values**. It does *not* make images copy-in verbatim: channels C1–C8 (§2.1) still embed process addresses — but each carries **item identity at its emission point**, so a bounded set of gen-time hooks captures them soundly (§5.4). Independent side benefits, worth having even if L3 slipped: removes the last "works until a pool moves" hazards from the in-process L1 cache; makes script-level MIR dumps pointer-free, lifting the emission-test harness's ban on full-file byte goldens (MT §2.7); and simplifies cross-process determinism to a checkable dump-equality property (§7).

### 3.3 Verdict

**L3-1: opt 1 (de-pointering, = MC4) is the prerequisite; the relocation journal covers the residual MIR-owned channels; differential diffing is demoted to the write-time verifier.** No variant of opt 2 survives as the primary mechanism.

---

## 4. Load models (Q2): what a cache hit actually skips

**Model A — regenerate the front-end, load the back-end (chosen for v1).**
On a hit, run parse → AST build → transpile-to-MIR → `MIR_load_module` → `MIR_link` exactly as today (fresh `MIR_context_t`, fresh thunks, fresh BSS/data sections, imports re-resolved by name through `import_resolver` — `lambda/runtime/mir.c:227-254`), but **replace the per-function `MIR_gen` with: publish cached bytes + apply journal fixups + redirect thunk**. Everything downstream is byte-for-byte today's path: `_mod_consts_ptr`/`_mod_type_list_ptr` wiring (`transpile-mir.cpp:15744-15758`), `register_module_pub_fns`, `register_bss_gc_roots` (zero-then-root per L1 discipline, `mir.c:491-527`), per-run `main_func` execution, debug-table build.
*Why this is right for v1:* the profile says `mir_gen` dominates (parent §1); the front-end regeneration hands us — for free — valid `const_list`/`type_list`/name-pool/AST/pub-interface state whose serialization is the single most expensive and risky part of the alternative (the `ShapeEntry::default_value → AstNode*` fact alone, §2.3). The sidecar shrinks to code + journal.
*What it requires:* deterministic emission (§7) so cached code matches regenerated IR structure, and the same-build guarantee from the cache key (any binary change misses).

**Model B — self-contained image (deferred Stage 2, §9).**
Skips the front-end too: sidecar sections carry const values, a flattened type graph, the pub interface, and BSS layout. Strictly additive on the same container (§5.1): new section kinds plus an AST-free loader path. Entry criterion: `LAMBDA_PROFILE` evidence that parse+ast+transpile is a material share of cold start for a real workload (large scripts, deep import cones). Until then it is specced but not built.

**L3-2: Stage v1 = Model A. Model B is an additive later stage, entry-gated on front-end profile share.**

---

## 5. Stage-A architecture

### 5.1 Container format (one file per module per key)

`<cache_dir>/<key[0:2]>/<key>.lmci` — little-endian, native word size (the key includes arch; images are never cross-arch):

| Section | Contents |
|---------|----------|
| Header | magic `LMCI`, format version, flags, arch/os tag, total length, SHA-256 of the remainder, cache-key echo (full hash), producer build stamp |
| Module meta | module canonical path (diagnostic only), MIR module name, opt level, gen-policy flags (redef permission, trace off), section directory |
| Func table | per generated function: MIR item name, offset in code blob, length, original base address (for layout preservation §5.3), `call_addr`≠`machine_code` flag |
| Code blob | concatenated function code **including each function's trailing x86 constant pool**, page-aligned |
| Journal | fixup records (§5.2), sorted by offset |
| Data-init | for each non-BSS `data` item: name + bytes (only if Model B ever needs it; v1 relies on re-link recreating sections — kept as a reserved section kind) |
| Lang sections | reserved, tagged by guest language (§10); empty for Lambda v1 |

Compression: zstd on code+journal is available (already linked) but off by default until measured — images are small and mmap-friendliness matters more.

### 5.2 The relocation journal

Each record: `{u32 kind, u32 site_form, u64 offset, u32 sym_len, char sym[]}` where `offset` is into the code blob and `sym` names a MIR item in the regenerated module (or an import name). Kinds and forms, mapped to §2.1 channels:

| Kind | Channel | Fixup at load |
|------|---------|----------------|
| `IMPORT_ADDR` | C1 | resolve `sym` via the fresh context's `import_resolver` result (`item->addr` of the import item); write per `site_form` |
| `FUNC_ADDR` | C2 residue, C4 | fresh `item->addr` (thunk) or the callee's installed code base for direct-call sites — `sym` is the func item name |
| `DATA_ADDR` | C3, C7-referenced | fresh `item->addr` of the named BSS/`data`/`.lc` item (sections recreated by `MIR_load_module` in the fresh context) |
| `IMAGE_LOCAL` | C5 switch slots, C8 lref | new value = `new_image_base + stored_disp`; for lref, the write target is a named data slot, not a code offset |

`site_form` ∈ {`ABS64` (pool word, switch slot, data qword), `X64_REL32` (patch `rel32` at `offset` to reach target), `A64_MOVSEQ` (re-encode 4×16-bit chunks at `offset`)}. The applier is a thin wrapper over `_MIR_update_code_arr`/`_MIR_set_code` for `ABS64`, plus two tiny target-specific encoders for the other forms; W^X and icache handling ride the existing primitives.

### 5.3 Layout preservation minimizes the journal

The loader places every function at its **original offset relative to the image base** (func table records original bases; the blob preserves gaps up to a waste cap). Consequence: every *self-relative* reference inside the image — intra-function branches, RIP-relative pool loads, and x86 `rel32` calls that `target_change_to_direct_calls` rewrote to point at another cached function's `machine_code` — is correct **with zero fixups**. The journal then only carries: absolute words (pool entries, switch slots, `movabs`/`movk` materializations), `rel32` sites whose target is *outside* the image (thunks, if any survive the direct-call rewrite), and lref slots. aarch64 has no intra-image relative calls (always `movz/movk`+`blr`), so its journal is denser but uniform. This is the concrete answer to parent OQ3: *partially* image-base-relative — relative sites free, absolute sites journaled per-site.

Policy: cache-writing compiles keep today's defaults (redef permission off, so the direct-call optimization stays; no trace wrappers). Forcing all-thunk calls (`MIR_set_func_redef_permission(TRUE)`) was considered to simplify C2 and rejected: it costs a thunk hop on every intra-module call at steady state, and layout preservation already gets the same journal reduction for free.

### 5.4 Save path (cache-writing compile)

Runs only on a miss, eager at the session's opt level (parent decision: cache writers are eager; lazy stays for uncached dev runs, §11).

1. **Gen-time capture hooks** in vendored `mir-gen` target code, guarded by a nullable callback on `gen_ctx` (upstream behavior unchanged when null; the vendored tree already carries local patches, §2.1). Hook points per target — each has the `MIR_item_t` in hand, so records are identity-sound:
   - x86_64: `setup_imm_addr` call sites for call targets (pool entries), the `J`-path 64-bit immediate emission when the operand is a resolved item reference, `change_calls`/`target_change_to_direct_calls` (to update or retire earlier records when a call form is rewritten), `target_rebase`'s `abs_address_locs` walk (switch slots), `gen_setup_lrefs`.
   - aarch64: the `movz/movk` materialization site for `ref` values (`mir-gen-aarch64.c:2297-2309`), `target_rebase`, `gen_setup_lrefs`.
2. After each function's gen, append `(name, base, len, code bytes incl. pool)` to the func table and blob.
3. **Differential write verifier (mandatory, default-on):** in a second `MIR_context_t` in the same process (first context kept alive so allocations cannot coincide), re-load + re-link + re-gen the same module; byte-diff the two images function-by-function. Every differing byte range must be covered by a journal record whose recomputed value explains it; any unexplained delta → log + **do not cache this module** (fail-open to normal JIT). This is the completeness oracle: unknown address channels can never ship in an image. Cost = one extra `MIR_gen` on cache misses only; revisit sampling once mature.
4. Assemble container, SHA-256, write to `<key>.lmci.tmp`, `rename()` into place (atomic; concurrent writers produce identical content by §7, last rename wins harmlessly).

### 5.5 Load path (cache hit)

1. Key computed (§8.1); file present, header/version/arch match, SHA-256 verifies, build stamp matches — else miss.
2. Front-end regeneration: parse → AST → transpile-to-MIR (today's code, unchanged).
3. `MIR_load_module` (creates thunks, allocates BSS/data sections) + `MIR_link` with imports resolved as today but **no gen interface** — either `MIR_link(ctx, NULL, resolver)` if the vendored MIR accepts it, or link with the lazy interface and rely on step 4 re-pointing every thunk before any call (verify which; OQ-L3-2).
4. Map/copy the code blob into executable memory via `_MIR_publish_code` (one block, layout-preserving §5.3); apply journal fixups; for each function set `machine_code`/`call_addr` and `_MIR_redirect_thunk(item->addr → installed code)` — exactly the tail of `generate_func_code` (`mir-gen.c:9504-9529`).
5. Continue today's path verbatim: consts/type-list BSS wiring, `register_module_pub_fns` for importers, zero-then-root `_gvar_*`, debug-info table rebuild (fresh addresses), per-run `main_func` execution.
6. **Any anomaly at any step → discard entry (unlink + poison in-memory) and fall through to full compile.** Never crash on a bad image; the image executes only after full validation.

### 5.6 Platform notes

macOS is first: `MAP_JIT` + `pthread_jit_write_protect_np` + `sys_icache_invalidate` are already exercised by MIR's own allocator; the loader reuses those paths by writing through `_MIR_publish_code`/`_MIR_set_code` rather than mapping pages itself. Linux second (plain `mprotect` W^X). Windows last (`VirtualAlloc`/`VirtualProtect`; the loader has no dlopen dependence so the Jube loader's Windows gap is irrelevant here).

---

## 6. De-pointering work plan (MC4 realized, per category)

| Cat | Fix | Emitted shape after | Cost note |
|-----|-----|---------------------|-----------|
| P1 `emit_load_string_literal` | intern into `const_list` at AST/transpile time; emit `emit_load_const(idx)` — one shared helper fixes ~20 callers | `[consts_reg + 8*idx]` load (consts_reg is prologue-pinned) | +1 L1-cached load per literal use |
| P1 member-name Item `:7462` | const_list entry holding the **pre-tagged** STRING Item so the load needs no re-tagging | 1 load | hot path — benchmark before/after (awfy + `Lambda_Benchmark_*` suites) |
| P2 `emit_load_module_type_list :2129` | use the existing `_mod_type_list_ptr` BSS (already loaded elsewhere: `:13284` etc.) | BSS load | trivial |
| P3 `TypeMap*` `:6841` | `type_list[type_index]` via `_mod_type_list_ptr` (`TypeMap::type_index` already exists, `lambda-data.hpp:266`) | 2 loads | warm |
| P4 `LIT_TYPE_*`, static `" "`/`"side-stack"` | named **data imports** resolved by `import_resolver` (new registry section alongside `sys_func_defs[]`); lowering emits `MIR_OP_REF` to the import item | C3 channel → journaled | none at steady state |
| P5 `info->func_ptr :12263` | `MIR_OP_REF` to the function's existing named import item | C1/C4 channel → journaled | none |
| P6 static containers `:2329/:2361` | append folded Item to `const_list`; emit indexed load | 1 load | literal-heavy code only |

Exit gate: baseline 100%, MIR dumps contain **zero** raw-pointer immediates (extend the mir-check ratchet with a `forbid`-pattern sweep), benchmark suite within noise. This lands **before** any journal/loader work and is independently shippable (L3-3).

## 7. Determinism gate

Model A assumes: same source + same binary ⇒ byte-identical MIR emission across processes. Post-de-pointering this becomes mechanically checkable: CI job compiles a probe set (reuse the `test/mir/` fixtures + picked `test/lambda` scripts) **in two separate processes** and diffs the canonical dumps byte-for-byte (the dump infrastructure and `--transpile-only` path exist; MT2). Byte-golden full-file dumps — newly legal per §3.2 — pin it permanently. The two hashmap-order emission sites (§2.4) are covered by the same diff. Gen-level nondeterminism is separately covered by the §5.4 differential verifier (which diffs *machine code* in-process). (L3-4)

## 8. Cache key & management

### 8.1 Key
`SHA-256(build stamp ‖ target arch/os ‖ MIR opt level + gen-policy flags ‖ format version ‖ module source hash ‖ transitive import source hashes in canonical import order)`. Transitive **source** hashes (not interface summaries) for v1: coarser invalidation, but sound with zero new machinery — Model A recompiles importers cheaply anyway; interface-summary keying is a Model-B refinement. Same discipline as the JS cache doc §6.2: the key gates lookup, but the entry's SHA-256 is verified on load — a hash collision must never execute wrong code.

### 8.2 Build stamp (new prerequisite, D0)
No automatic build ID exists today (§2.5); `JUBE_HOST_BUILD_ID` is manual. Add a generated `build_stamp.h` (git commit + dirty flag + config hash + compiler id) injected by `utils/generate_premake.py`; expose in `--version`; reuse it to auto-derive `JUBE_HOST_BUILD_ID`'s successor. Keyed-by-build-stamp is what makes the cache "memoization, not a format" (Native-Module §7.4).

### 8.3 Location (decides parent OQ5)
`~/.lambda/cache/mir/` per Native-Module §7.4 (tree-sitter precedent), override via `LAMBDA_CACHE_DIR`; tests point it at `./temp/` to honor the no-`/tmp` rule and keep CI hermetic. `LAMBDA_DISABLE_MIR_L3=1` kill switch mirroring the L1 flag.

### 8.4 Eviction
Size-capped LRU by file mtime (touch on hit), default 512MB, pruned opportunistically after writes. Entries are independent files; eviction can never strand a dependent (importers embed no dependency on the *cache entry* of their imports — only on live link-time state).

### 8.5 States & concurrency
Reuse the JS cache doc's fail-closed discipline: an entry is *usable* only after full validation; validation or execution anomalies **poison** (in-memory set + best-effort unlink) so one bad image can't be retried in a loop. Readers never lock; writers use tmp+rename (§5.4.4).

### 8.6 Security posture
The cache executes native code from a user-writable directory. Threat model: a same-user attacker can already modify `lambda.exe` or `~/.zshrc`; the SHA-256 is an *integrity* check against corruption, not authentication. Per P14 the cache is never shared or distributed; do not add signing complexity. Document `chmod 700` on the cache dir at creation. (L3-7)

## 9. Stage-B self-contained sidecar (deferred spec)

Recorded now so Model B is a bolt-on, not a redesign. New sections on the same container:

- **§9.1 Const pool section.** One record per `const_list` index, tagged by kind: `SYMBOL`/`STRING`/`BINARY` = content bytes (loader re-interns into the module's load pool → identity-correct `String*`); `INT64`/`DOUBLE` = raw 8 bytes into loader-owned slots (today's entries point into AST nodes, §2.3 — Model B must own the storage); `DECIMAL` = canonical string re-parsed via libmpdec; `DATETIME` = packed value. Index order = record order.
- **§9.2 Type-graph section.** Types flattened by `type_list` ordinal; intra-graph `Type*` → ordinals; `ShapeEntry` chains as arrays `{name → string-blob offset (re-interned), type ordinal, byte_offset, flags, name_id}`; `field_index`/`slot_entries`/`transitions` **rebuilt at load**, never serialized. The honest blocker: `ShapeEntry::default_value` is an `AstNode*` — Model B must either serialize a mini-expression form or pre-evaluate defaults at write; this is the main reason Model B is deferred (L3-2).
- **§9.3 Pub-interface section.** What `register_module_pub_fns` walks the AST for today: pub fn `{name, mangled name, TypeFunc signature by type-ordinal}` + pub var `{name, type ordinal, BSS item name}` — enough to register `dynamic_import_map` entries and drive importer inference without an AST.
- **§9.4 BSS layout section.** `{item name, size}` list so the loader can create MIR items without transpile.

Loader gains an AST-free path: build a skeletal MIR module from §9.4 + func table, link, install, wire consts/types from §9.1–9.2. Cache key upgrades from transitive source hashes to interface-summary hashes at the same time.

## 10. Language generality (the Jube image contract)

The container, journal kinds, applier, verifier, key discipline, and cache manager are guest-agnostic. A guest language qualifies for L3 when it satisfies:

- **G1 — De-pointered emission:** no anonymous pointer immediates; every process address flows through MIR item channels (C1–C8). Lambda: §6. JS: fails today — property names, literals, and **mutable IC/shape-cache cells** are baked (parent §2.1) — and its cells are *per-instantiation mutable state*, not constants.
- **G2 — Reconstructible compile-time state:** either deterministic front-end regeneration (Model A) or full sidecar sections (Model B). JS's two-layer template/instance split (`Lambda_Design_JS_Cache.md` §6.1) maps cleanly onto Model A.
- **G3 — Runtime-cell journal class:** guests with per-instantiation mutable cells (JS ICs, shape caches) must allocate them as **named MIR BSS/data items**, so they become `DATA_ADDR` journal targets recreated-and-reset per load — the parent doc's "journal them as BSS-like allocations" made concrete. This doubles as the JS de-pointering design's target shape.
- **G4 — Own key axes:** each guest appends its language tag + transpiler-policy flags to the §8.1 key and owns its lang-section contents. The JS cache's key ingredients (mode, preamble ABI, resolution base) slot in here unchanged.

Python/Ruby/Bash currently do `jit_init`→`MIR_finish` per unit and can adopt the whole stack once they satisfy G1–G2. Nothing in Stage A hard-codes Lambda beyond §6.

## 11. Interaction with Levels 1–2

- **L1 (landed):** unchanged. An L3 hit materializes a `Script` with live `jit_context` exactly like a fresh compile, so it enters the in-process registry and serves L1 hits for the rest of the batch. L3 lookup slots in at the L1 *miss* path, before `transpile_script`'s `compile_script_as_mir_direct` step — front-end phases run either way under Model A.
- **L2 (approved experiment, not started — `mir.c:401` still eager):** unchanged decision from the parent: cache-*writing* compiles are eager at the session opt level; lazy gen is for uncached dev/test runs. A lazily-generated module (thunks, partial code) is never serialized. If L2 lands first, the L3 writer simply forces eager for the modules it persists.

## 12. Decision ledger

| ID | Decision |
|----|----------|
| L3-1 | Opt 1 (de-pointer the lowering) is the mechanism; every save/load-patching variant of opt 2 rejected as primary (§3.1); differential diffing kept as the write-time verifier only |
| L3-2 | v1 load model = **Model A** (regenerate front-end, load back-end); **no const/type serialization in v1**; Model B specced (§9) and entry-gated on front-end profile share |
| L3-3 | De-pointering P1–P6 lands first as an independent milestone (baseline green, pointer-free dumps enforced by mir-check `forbid` ratchet, benchmarks within noise) |
| L3-4 | Cross-process determinism enforced by a dump byte-equality CI gate + full-file byte goldens (newly legal post-L3-3) |
| L3-5 | Journal captured by nullable gen-time hooks in vendored `mir-gen` target code at item-address materialization points (§5.4.1); kinds/forms per §5.2; layout-preserving image placement makes intra-image relative references fixup-free (§5.3; answers parent OQ3) |
| L3-6 | Differential write verifier is mandatory and fail-closed: an unexplained image byte vetoes caching that module |
| L3-7 | Key = build stamp + arch + opt/policy + format version + transitive source hashes (§8.1); location `~/.lambda/cache/mir/` with `LAMBDA_CACHE_DIR` override (decides parent OQ5); tmp+rename atomicity; SHA-256 validated on load; size-capped LRU; poison-on-anomaly; no signing (P14 posture) |
| L3-8 | Automatic build stamp (git+config hash) is a prerequisite (D0); `JUBE_HOST_BUILD_ID` successor derives from it |
| L3-9 | Guest contract G1–G4 (§10); JS enablement = its de-pointering + IC/shape cells re-homed as named MIR data items (G3); JS work tracked in its own design, not here |
| L3-10 | Cache-writing compiles keep default gen policy (direct-call rewrite on, redef permission off); all-thunk indirection rejected on steady-state cost |

## 13. Sequencing & exit gates

- **D0 — Build stamp + measurement.** Generated build ID (§8.2). `LAMBDA_PROFILE` cold-start baseline on representative scripts/import cones → records `mir_gen` vs front-end share (feeds the L3-2/Model-B gate).
- **D1 — De-pointering (L3-3).** §6 table, one category per PR; exit = baseline 100% + `forbid` ratchet + benchmark parity.
- **D2 — Determinism gate (L3-4).** Two-process dump diff in CI; byte goldens for the probe set.
- **D3 — Capture hooks + differential verifier (no file format yet).** Vendored-MIR hooks behind null-guarded callbacks; verifier runs in-process and reports journal coverage on canary modules. Exit = 100% explained bytes on the probe corpus, both targets (x86_64 + aarch64).
- **D4 — Container writer + loader, macOS.** §5.1–5.6; wired at the L1-miss seam. Exit = baseline 100% with L3 forced on; measured hit-load time; kill switch verified.
- **D5 — Linux, then Windows loader.**
- **D6 — Model B sidecar** only if D0/D4 measurements show front-end share matters (§4).

## 14. Open questions

- **OQ-L3-1:** Does anything in Lambda emission produce `expr_data` or single-label `lref_data` today (C7/C8)? If not, those journal kinds start dormant — confirm and assert at write time rather than carrying untested code.
- **OQ-L3-2:** Does the vendored `MIR_link` accept a NULL `set_interface` (link-without-gen), or does the loader use the lazy-interface-then-redirect trick (§5.5.3)? Verify; upstream comment suggests NULL is legal.
- **OQ-L3-3:** x86 residue after `target_change_to_direct_calls` — do any `rel32`-to-thunk sites survive in practice (they would journal as `FUNC_ADDR`/`X64_REL32`)? The D3 verifier answers this empirically.
- **OQ-L3-4:** Hit-path front-end cost under Model A on the largest real import cones — the concrete number that decides whether D6 ever runs.
- **OQ-L3-5:** Should main scripts (not just modules) be cached in v1? Nothing in the design precludes it (the key covers the main source + cone); decide by measurement — REPL/watch workflows recompile mains constantly, batch runs don't.
- **OQ-L3-6:** Per-function granularity within an image (load only called functions' pages) — composes naturally with layout preservation via `mmap` laziness; defer until image sizes justify it.
