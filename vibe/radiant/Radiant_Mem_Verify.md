# Radiant Memory Verification — Bounded VeriFast Pilot

**Status:** proposed design and implementation plan; no VeriFast proof has been run.
**Date:** 2026-09-16.
**Source baseline:** `fd9987591`; the observations below were checked against the working tree.
**Scope:** display-list payload copying, selected list storage/lifecycle operations, and a bounded retained-copy/scratch-reset composition. This is not a whole-Radiant memory-safety claim.
**Document organization:** overall design first, detailed implementation plan second, in this file as requested. This plan does not change or ratify runtime semantics.

## Part I — Overall design

### 1. Objective and meaning of success

Establish a repeatable way to verify **actual production Radiant functions** with VeriFast comment annotations. The first useful result should cover allocation failure, buffer bounds, ownership transfer, and invalidation of scratch storage. It must identify exactly which implementation bodies were checked and which dependency contracts remain trusted.

The intended result is a conditional theorem:

> For every execution represented by the pinned VeriFast target model, the listed function bodies satisfy their memory-safety contracts when their entry preconditions and explicitly listed dependency contracts hold. The listed composition proofs establish those preconditions for the selected call sequences.

“Bounded pilot” describes the amount of source code and the number of call paths admitted to the project. The proof of an admitted loop should use an invariant covering arbitrary supported lengths, rather than testing or unrolling a fixed number of iterations.

Success does **not** establish that all Radiant callers satisfy these preconditions, that third-party libraries are memory-safe, or that every platform/build configuration behaves identically. Function proofs, caller proofs, native tests, and reviewed assumptions must be reported separately.

### 2. Formal-spec linkage

The formal design is authoritative. The relevant rulings and their application are:

| Ruling | Existing requirement | Application to this pilot |
|---|---|---|
| **D4.1.4v4** | Four allocation mechanisms; arena batch lifetime; pool individual lifetime; one writer with published readers. | Give scratch and pool allocations different lifetime predicates. Keep the pilot single-threaded; no concurrent mutation or render-worker theorem. |
| **D4.2.1v3**, **D4.2.3** | `MemContext` owns allocators and tears them down in dependency order. | Factory/registration contracts must preserve owner and backing relationships, including scratch re-registration after clear. |
| **D4.2.2v2** | Allocation pressure/reclamation precedes a possible `NULL` result. | Allocation failure remains a real branch. Review callbacks for effects and reentrancy before trusting an allocation contract. |
| **D4.2.4** | Shared-owner lifetime and exclusive batch invalidation. | Reset requires recovered ownership of the invalidated region and its borrowers. An observed scope count alone is insufficient. |
| **D4.2.5v2** | Tracking is diagnostic, not the ownership mechanism. | Do not derive a proof of liveness from memtrack registration or a clean leak report. |
| **D4.5.1v3** | Radiant uses regions, pools, generation handles and RAII; seam contracts are pin, generation-check, copy-as-value. | Prove copying into the destination lifetime; explicitly exclude unsupported borrowed-resource families. |
| **D5.3.3** | GC-facing native helpers use precise roots/handles. | Lambda/JS/GC crossings are outside this pilot. A later extension must model these contracts; it cannot introduce native-stack scanning. |

References: [formal design](../../doc/Lambda_Formal_Design.md), [documentation convention](../../doc/Doc_Convention.md), [C+ convention](../../doc/dev/C_Plus_Convention.md).

Several memory rulings carry implementation-status marks. In particular, the current `lib/arena.c` still contains `arena_free()` and free-list machinery, while D4.1.4v4 specifies the target batch-only arena policy. The pilot must record the implementation/conformance gap; it must neither assume the target migration has already happened nor turn the current implementation into a new ruling. Proving or migrating the allocator is outside this pilot. If a required fix needs a disputed ownership policy, stop that dependent phase for resolution under the documentation convention.

### 3. Why VeriFast, and the compatibility gate

VeriFast checks functions against preconditions/postconditions using separation logic. Ownership predicates describe the memory a function may access; loop invariants and proof lemmas make traversal and lifetime arguments explicit. Annotations use comments such as `//@ ...` and `/*@ ... @*/`, so successful integration need not add runtime checking overhead.

Its C++ frontend is described upstream as supporting simple C++ programs. Radiant's C+ convention helps, but does not guarantee acceptance of the actual translation units. `DisplayList` inherits from `lam::ArrayList<DisplayItem>`; the latter uses templates, placement construction, destructors, move operations and dependent types. `DisplayItem` is a tagged union. `render.hpp` pulls in a substantial dependency graph.

The first gate is therefore a **production-source compatibility experiment**, before investing in a large predicate library. A standalone C example establishes that the tool runs; it does not establish that Radiant can be verified. Pin the tool revision and target configuration. Do not treat the historical C++ feature report as a complete current support matrix.

Primary references, consulted 2026-09-16:

- [VeriFast C introduction](https://github.com/verifast/verifast/blob/master/intro-c.md): modular verification, annotations and memory permissions.
- [Current C++ frontend](https://github.com/verifast/verifast/tree/master/src/cxx_frontend): frontend scope and Clang AST translation.
- [C++ test corpus](https://github.com/verifast/verifast/tree/master/tests/cxx): useful compatibility probes, including templates and object lifetimes.
- [Verification of C++ Programs with VeriFast](https://arxiv.org/abs/2212.13754): historical description of the C++ proof model, dated 2022.

### 4. Scope boundaries

The pilot is one dependency chain, delivered in independently reviewable phases. Phase 0 freezes an explicit symbol/dependency manifest; it cannot silently expand to all of rendering.

| Layer | Included proof targets | Boundary |
|---|---|---|
| Payload copying | `dl_copy_stops()`, `dl_copy_dashes()` and any single shared production helper needed by a root-cause fix. | Valid source spans; arbitrary supported positive counts; null/empty input and allocation failure branches. |
| List storage/lifecycle | `dl_alloc_item()`, `DisplayList::init/clear/destroy()`, their used wrappers, and `dl_item_free_owned_payload()`. | `lam::ArrayList<DisplayItem>` only; required operations are either proved or explicitly trusted. Other instantiations are excluded. |
| Selected callers | Stroke, linear-gradient and radial-gradient recorders; retained stop/dash/path-copy helpers. | Bounds/math/backend routines may have audited contracts. Prove the admitted callers' memory operations and failure cleanup, not rendering correctness. |
| Retained composition | `retained_dl_copy_range()`, its rollback and payload-clone paths for the admitted operation set. | Separate source/destination lists and backing arenas. No borrowed images, glyphs, media, surfaces, pictures or polygon clips. |
| Reset composition | The scratch-lifetime part of `ViewTree::reset_retained()` and a checked sequence using the real reset body. | DOM/property teardown and cache clearing are contracted dependencies. Full layout/event entry paths remain outside the proof. |

The retained operation set is fixed initially to `DL_FILL_RECT`, `DL_BEGIN_ELEMENT`, `DL_END_ELEMENT`, `DL_FILL_PATH`, `DL_STROKE_PATH`, `DL_FILL_LINEAR_GRADIENT`, and `DL_FILL_RADIAL_GRADIENT`. This covers scalar-only payloads, marker index adjustment, owned paths, copied stops and copied dashes without importing all resource managers.

Restricting the theorem to this set does not require removing or rejecting other operations in production. The real tagged-union switch remains intact. Its contract and the coverage report state the admitted input domain. Tests or harnesses restricted to this domain do not prove that the unrestricted production cache always supplies such inputs.

Outside scope: flex/grid/table layout algorithms; whole DOM ownership; parsing; paint correctness; whole-cache eviction/map correctness; all display opcodes; asynchronous events; worker concurrency/data races; GC/JIT/JS; third-party implementations; allocator internals/VM; global leak freedom; stack exhaustion; performance guarantees.

### 5. Current implementation anchors

Line numbers are survey anchors and must be refreshed during implementation; symbols are the stable reference.

| Source | Observation | Consequence |
|---|---|---|
| `radiant/display_list_storage.cpp:43`, `:51` — copy helpers | `scratch_alloc()` is followed immediately by `memcpy()` without a null check. | A faithful failure-permitting contract will not prove these paths as written. |
| `lib/scratch_arena.c:46` — `scratch_alloc()` | Rejects invalid/zero/oversized requests, checks alignment arithmetic, and may fail backing allocation. | The model must preserve null results, the header-size/alignment limit, and successful allocation freshness. |
| `radiant/render.hpp:708` — `DisplayList` | Item storage comes from `ArrayList`; variable payloads use `ScratchArena`. | The two storage lifetimes are distinct. Reallocation can invalidate `DisplayItem*` while payload storage stays live. |
| `lib/arraylist.hpp` — `reallocate()`, `insert()` | Growth allocates new storage, constructs copies, destroys old objects, then frees the old storage. | Model object lifetime and alias invalidation; this is not simply a `realloc()` contract. |
| `radiant/display_list_storage.cpp:177` — `DisplayList::clear()` | Frees owned payloads, clears items, releases scratch, then re-registers scratch on the backing arena. | A cleared list is reusable but still has an active scratch scope. Clear alone does not permit backing-arena reset. |
| `radiant/display_list_storage.cpp:194` — `DisplayList::destroy()` | Clears, releases array storage, then releases the re-created scratch scope. | Prove both releases compose correctly and that no stale payload permission survives. |
| `radiant/retained_display_list.cpp:153` — `retained_dl_copy_range()` | Appends a shallow item copy, clones payloads, rolls back on failure. | Track partial initialization and ownership field-by-field, including failure after path cloning but before copying an array. |
| `radiant/view_pool.cpp:975` — `ViewTree::reset_retained()` | Advances the layout generation, clears cached state, checks scopes, then resets scratch. | Separate layout generation from proof lifetime identity; prove that scopes account for every admitted borrow. |
| `radiant/retained_display_list.cpp:404` — generation validation | Reads `surface->generation` through a supplied owner pointer. | Generation comparison itself needs a live owner. It is not a substitute for proving owner lifetime; excluded from the pilot. |

The older [Radiant memory proposal](Radiant_Design_Memory.md) supplies history, not a substitute for these current observations. In particular, use the current `prop_pool`, `canonical_prop_arena`, and `scratch_arena` fields in `ViewTree`, not the older `pool`/`arena` sketch.

### 6. Ownership model

#### 6.1 Logical resources

Use reusable predicates, with concrete VeriFast syntax established in Phase 1. The names below describe a proposed model, not annotations already checked by VeriFast.

| Logical resource | Information and permissions represented |
|---|---|
| Arena owner | Live arena identity, current logical lifetime, registered scratch scopes, and outstanding allocation/borrow obligations. |
| Scratch scope | Live backing owner, scope identity, registration state, allocation inventory, and owned scratch metadata. |
| Allocation | Exact base, readable/writable extent, alignment, initialization state, owning scope and current logical lifetime. |
| Read borrow | A fraction of the permission for a live span; borrowing does not transfer destruction rights. |
| Item storage | `0 <= count <= capacity`, initialized items in the prefix, raw spare storage, and the current storage identity. |
| Item payload | Active opcode and the appropriate scalar, scratch-owned, externally owned, or borrowed fields. |
| Owned path | A backend ownership token returned by cloning and consumed exactly once by path release. |
| Display list | Item storage plus scratch scope plus payload ownership for every initialized item. |

Do not conflate the ghost lifetime identity with `uint32_t layout_generation`. The runtime counter wraps and skips zero. A logical epoch/token must be fresh across invalidations without deriving unbounded freshness from a finite counter. The pilot does not prove protection against generation wraparound in other APIs.

#### 6.2 Borrowing and invalidation

Copying borrows the source span read-only and returns those permissions to the caller. On success, the destination owns a fresh initialized span; the source and destination do not overlap. The destination allocation belongs to the destination scratch scope, even if its contents originated in another arena.

Reset/release cannot be specified as returning an empty arena while arbitrary allocation predicates remain framed out. Before invalidating bytes, the caller must recover the relevant permissions and allocation inventory. The ghost model must make reset unprovable while a read borrow or owned payload from that region remains outstanding.

The same rule applies when the allocator keeps physical pages for reuse. Address readability at the machine level does not preserve the old object's logical lifetime. Scope counters and runtime assertions are supporting implementation facts, not axioms that erase outstanding borrows.

For the reset composition, every backing-arena scratch scope must be destroyed/released. `dl_clear()` immediately starts another scope; use the actual destroy/release sequence required by the current implementation before calling reset.

#### 6.3 Copy and allocation failure contracts

The copy-helper contract should express:

1. Existing null/nonpositive-count branches return null without reading the source.
2. When source copying is attempted, the caller owns a readable span of the required length and a live destination scratch scope.
3. Count-to-byte conversion and multiplication cannot wrap into an undersized allocation. Use existing checked-size helpers where a fix is required; account for the allocator's alignment/header limit too.
4. Allocation failure returns null without calling `memcpy()` or altering the source. Existing destination allocations stay valid.
5. Success returns a fresh, initialized, properly aligned destination span with the same element values/bytes required by the type's copy semantics.
6. Both outcomes preserve the list's other allocations and the backing-owner relationship.

A positive count paired with a null source currently returns null. Each admitted caller must interpret this consistently; it must not publish a positive-count payload with no readable storage. A guard alone does not establish that an arbitrary non-null source points to `count` elements: that is an explicit caller obligation.

Allocation contracts must not manufacture typed objects solely from a non-null byte pointer. Check the selected C++ frontend's treatment of raw storage, alignment, initialization, struct copies and placement construction. If the existing code violates the supported language/object-lifetime model, report the issue rather than disabling the check globally.

#### 6.4 Partial construction and rollback

An appended, zero-initialized item is initially an uncommitted slot. Its final opcode, pointers, counts and ownership tokens must agree before a recorder publishes it as usable. Allocation or backend-clone failure must leave a valid list and release any acquired external ownership.

For retained copies, the initial shallow assignment copies pointer values, not ownership. Represent the intermediate state explicitly. A clone that fails must not leave a source-owned path in a destination field that rollback will free. A later failure must release already acquired destination paths exactly once and preserve the source's tokens.

Rollback restores the previous logical item prefix. It need not reclaim every scratch byte immediately if those bytes remain tracked by the destination scope and are reclaimed by its later release. State this retained-allocation behavior in the postcondition; do not claim unchanged allocation counts or bounded memory consumption without a separate proof.

#### 6.5 Storage growth and indices

The `ArrayList<DisplayItem>` contract must distinguish live item pointers from copied values. Successful growth invalidates pointers into the old item buffer; existing payload pointers retain their separate owner. Failure preserves the old buffer and its initialized prefix.

For the retained copy loop, require distinct source/destination lists and backing arenas. Keep the source borrow stable while appending to the destination. Prove range checks, loop bounds, marker-index adjustment and destination-count arithmetic. `item_count()` saturates at `INT_MAX`; saturation is not a proof that subsequent additions are safe. Establish an explicit safe count bound at admitted entry points and preserve it through append, or fix the arithmetic after identifying the root cause.

### 7. Trust boundary and claim levels

Maintain a machine-readable manifest with these categories:

- **Verified body:** the exact production definition has passed VeriFast under a recorded contract and input domain.
- **Verified composition:** a checked caller or harness invokes those definitions and discharges their preconditions for a named sequence.
- **Trusted dependency:** only its contract is used. Record implementation location, rationale, reviewed failure/effect behavior, and why its body is outside scope.
- **Not covered:** no proof is claimed. A native test or a source review does not upgrade this status.

Initial trusted dependencies may include the memory factory, arena/scratch implementation, memtrack allocation, logging, C memory primitives supplied by VeriFast, opaque path clone/free, and the DOM/cache dependencies reached by reset. Every such assumption must be specific. “All render helpers are safe” is not an acceptable entry.

`ArrayList<DisplayItem>` is provisionally a trusted dependency during the leaf-copy phase. Phase 3 attempts to verify the actually used specialization/methods. If that is blocked, publish the remaining conditional client proof and label array growth itself unverified; do not claim the storage layer complete.

Review external callbacks for reentrancy, collection, arena reset and changes to source spans. “Single-threaded” does not exclude reentrant reclamation. If a necessary preservation contract is false, fix the actual ownership/lifetime bug or leave that proof failed.

No unconditional `assume(false)`, leaked ownership predicates, verification-only success allocators, disabled memory checks, or replacement no-op cleanup implementations may close an in-scope obligation. Ordinary narrow preconditions are allowed only when recorded, non-vacuous, and either established by admitted callers or visibly left as boundary obligations.

### 8. Proof integration architecture

Place function contracts with production declarations/definitions and shared abstract predicates in ghost headers. Use a small verification driver to select pinned targets and collect evidence. No independent reimplementation of Radiant's copy/list/reset algorithms is permitted.

If a large translation unit cannot be parsed because of unrelated code, a normal production module split may be appropriate. Both the normal build and VeriFast must consume the same implementation and type declarations. Do not hide difficult memory operations behind verification macros or maintain hand-copied substitute layouts. If the split does not solve an actual dependency-boundary problem, report the frontend blocker.

A harness is useful for proving a lifecycle sequence. It must call the real verified definitions through their checked interfaces. It is not evidence that all application entry paths follow the same sequence.

Some retained helpers are currently `static`. If a separate harness/module needs one, expose the existing helper through an appropriate internal module header and keep one production definition, as required by the repository's deduplication rule. Do not copy the helper into a test or verify a rewritten algorithm. Record any such visibility/module change in the production build configuration when necessary.

The first target configuration is one explicitly pinned 64-bit ABI supported by the chosen VeriFast distribution and Radiant build. Record pointer/`size_t` widths, endianness, alignment assumptions, C++ mode, compiler defines, include paths and enabled diagnostics. Any later platform is a separate configuration gate, not automatically covered by the initial pass.

## Part II — Detailed implementation plan

### 9. Deliverables and proposed file layout

Paths in this section are proposed additions; none exists merely because it is listed here. Search for an existing equivalent before implementing one.

| Artifact | Proposed location | Purpose |
|---|---|---|
| Pilot manifest | `test/verification/radiant/manifest.json` | Tool identity, configuration, exact symbols/domains, dependencies, assumptions and expected results. |
| Ownership predicates | `test/verification/radiant/contracts/*.gh` | Shared arena/scratch/list/payload predicates and checked lemmas. |
| Composition harnesses | `test/verification/radiant/*.cpp` | Real copy/clear/destroy/reset compositions and ownership entry examples. |
| Negative verification fixtures | `test/verification/radiant/negative/` | Small intentionally invalid clients that must fail for a specified proof obligation. |
| Driver | `utils/verifast/run_radiant.py` | Resolve pinned tool, invoke checks, fail on missing coverage, write structured results. |
| Local generated artifacts | `temp/verifast-radiant/` | Downloads, logs, temporary translation artifacts and result JSON; never `/tmp`. |
| Production annotations/fixes | Existing `radiant/` and narrowly required `lib/` files | Actual contracts and root-cause corrections. |
| Native regression tests | Existing display-list/retained-list and `test/lib/` suites | Confirm concrete failure handling, ownership and reuse behavior. |
| Build/CI entry | `Makefile` plus CI configuration chosen after Phase 0 | Proposed `verify-radiant-memory` target and pinned verification job. |

The driver must not depend on unpinned “latest” downloads. Keep the tool outside runtime dependencies. Use an explicit `VERIFAST` executable path or a repository-local pinned tool directory, with a verified artifact checksum.

Any production source-list or sanitizer-profile change goes through `build_lambda_config.json` followed by `make`; never edit generated Lua build files. No vendored dependency edits are part of this plan.

### 10. Phase 0 — Establish feasibility and freeze coverage

**Inputs:** source baseline, this plan, upstream binary/source distribution.

**Work:**

1. Record the current source commit and relevant local modifications. Confirm applicable `AGENTS.md` instructions. Refresh the symbol anchors in §5.
2. Pin a VeriFast revision/distribution with C++ support. Record artifact hash, platform, frontend/solver versions and the exact invocation. Verify the available flags from that executable rather than copying hypothetical command syntax into CI.
3. Run a small positive and negative upstream-style annotation example to validate installation and error handling.
4. Probe the actual header/declaration graph for the copy helpers, then `DisplayList`, then the reachable `ArrayList<DisplayItem>` operations. Include union fields, raw allocation, placement construction, destructors, offset-based payload access and required macros in the probe matrix.
5. Attempt the real copy-helper body with a provisional, explicitly trusted allocator contract. Separate parse/type support failures from unsatisfied memory obligations. The existing null-allocation path is an expected proof failure, not an installation failure.
6. Inventory the exact transitive calls needed by later phases. Freeze production entry points, supported opcodes, target configuration and the trusted dependency list in the manifest.
7. Review allocator failure callbacks and the current arena implementation against the required contracts and D4.1.4v4/D4.2.4. Record conformance gaps without assuming proposed mechanisms are implemented.

**Deliverables:** reproducible compatibility report; pinned configuration; manifest skeleton; list of real blockers and candidate production module splits, if any.

**Exit gate:** the frontend accepts at least the intended leaf-copy production code and faithful type declarations; the next storage/lifecycle target has an explicit supported or blocked status. A toy-only pass does not satisfy the gate.

**Stop rule:** if progress requires a second implementation, unsound stubs, a frontend extension or a broad architecture rewrite, report a blocked phase and its smallest reproducer. Do not silently expand the pilot into tool development.

### 11. Phase 1 — Define and validate the ownership contracts

**Depends on:** Phase 0's supported source boundary.

**Work:**

1. Define the owner, scope, allocation, borrow and payload predicates from §6. Keep physical allocation identity separate from logical region lifetime and finite runtime generations.
2. Write audited contracts for only the allocator/factory/backend calls reached by the manifest. Specify success, failure, alignment, initialization, source preservation, callback effects and owner invalidation. Document partial initialization of factory-managed objects where applicable.
3. Model outstanding allocations so release/reset requires their permissions back. Support multiple scopes sharing one backing arena without assuming that releasing one scope invalidates another's allocations.
4. Model `memcpy` with source readability, destination writability, extent and non-overlap. Decide whether the accepted frontend can prove the typed copies directly; do not silently replace C++ object-lifetime obligations with byte arithmetic.
5. Define separate predicates for an uncommitted item, an initialized admitted item, and partially cloned payloads. Avoid one predicate that asserts complete ownership before acquisition has succeeded.
6. Write checked construction/borrowing examples and negative clients: use after scope release, reset with an outstanding borrow, double ownership of a path, and wrong-owner release.
7. Review contracts against implementation rather than tests alone. Tests can check examples of an assumed contract, not prove its full truth.

**Deliverables:** ghost headers, dependency-contract inventory, positive ownership examples and negative tests.

**Exit gate:** the examples are satisfiable and the intended invalid clients fail at memory/ownership obligations. No new axiom should grant a client permissions the real API cannot provide. Unknown dependency effects remain blockers or visible restrictions.

### 12. Phase 2 — Verify copying and the selected callers

**Depends on:** accepted frontend path and allocator contracts.

**Production targets:** `dl_copy_stops()`, `dl_copy_dashes()`, selected retained wrappers, and the memory-manipulating paths of `dl_stroke_path()`, `dl_fill_linear_gradient()`, `dl_fill_radial_gradient()`.

**Work:**

1. Add pre/postconditions to both copy helpers and obtain the initial failed proof for the unchecked allocation path. Preserve the diagnostic as before-fix evidence.
2. Search for existing checked multiplication and scratch-copy helpers. Fix the confirmed allocation/size root cause in production. If a third equivalent typed-copy variant is needed, extract the shared shape first; do not create a verification-only copy routine.
3. Cover null inputs, nonpositive count, oversized/alignment-rejected allocation, ordinary allocation failure and success. Do not add a precondition that allocation always succeeds.
4. Inspect every direct call site. For the admitted recorders, handle `dl_alloc_item()` failure before dereferencing the slot, and handle path-clone/copy failure before exposing an inconsistent item. Preserve source ownership and release any destination path already acquired.
5. Establish the source-span preconditions for admitted internal callers. For public recorder entry points, document caller-provided span validity as a remaining boundary requirement rather than attempting to validate arbitrary pointers at runtime.
6. Keep bounds/transform helper contracts explicit. This phase does not prove floating-point geometry; it must still establish the pointer validity and object fields those helpers access.
7. Use the existing test infrastructure for deterministic failure injection if available. Otherwise add the smallest test-only allocation/clone failure mechanism at the relevant dependency boundary; do not change runtime allocation policy merely to make the tests easy.
8. Add native regressions for successful copies, source preservation, failure before slot allocation, failure after acquiring a path, and subsequent list reuse. Avoid passing a short source array with a fabricated huge count to tests that might actually copy from it.

**Exit gate:** helper bodies and admitted caller memory paths verify with a real failure branch; runtime failure tests pass; the dependency manifest states any still-trusted list/backend helpers. OOM return behavior is reviewed at callers, not merely hidden behind a helper null check.

### 13. Phase 3 — Verify list storage and lifecycle

**Depends on:** predicates and the Phase 0 C++ result. This phase can advance independently of additional recorder annotations once its dependencies exist.

**Work:**

1. Verify `lib_grow_capacity()` and the required `ArrayList<DisplayItem>` method bodies reachable from reserve, append, back/data/size/capacity, remove-range, clear and release. Include their actual constructor/destructor and private growth/reallocation dependencies. Do not attempt unrelated generic methods or instantiations.
2. Prove arithmetic bounds, initialized-prefix preservation, object construction/destruction, and failure preservation. Establish that reallocation retires old item-buffer borrows. Treat item copies containing payload pointers as ownership relocation, not duplication of free rights.
3. Prove `dl_alloc_item()` returns either null with a valid list or a writable newly appended slot. State which previous item pointers cease to be usable on success.
4. Prove opcode/descriptor agreement for admitted payload cleanup. Offset-based field access must match the actual active union member and object layout; do not assume every descriptor offset is valid without checking the admitted table rows.
5. Add the clear-loop invariant: the processed prefix has surrendered owned external payloads; the remaining suffix still owns them; scratch payload allocations remain tracked until scope release.
6. Verify `init()`, `clear()` and `destroy()` against constructed/initialized/reusable states. Cover explicit destroy followed by C++ base destruction, repeated lifecycle calls allowed by the real API, and factory registration effects.
7. Verify wrapper null cases and demonstrate that `clear()` retains an active scratch scope whereas `destroy()` releases it. Add native coverage for repeated clear/reuse, growth, destroy and initialization with failing dependencies where supported by the contract.

**Exit gate:** the named storage/lifecycle bodies verify and their construction/teardown compositions pass. If a required template/object-lifetime feature is unsupported, retain the conditional client results but mark this phase blocked; growth is not reported as verified.

### 14. Phase 4 — Verify retained copy and reset composition

**Depends on:** copying and lifecycle contracts; successful earlier proofs for every body claimed here.

**Work:**

1. Annotate the retained clone/rollback/copy functions for exactly the opcode set in §4. Keep source and destination list/arena identities distinct. Include range and destination-capacity limits necessary for the integer marker/count APIs.
2. Prove the copy-loop invariant: the source remains borrowed and unchanged; the destination prefix before `dest_start` remains owned; each new committed item has independent destination ownership; the current partial item has explicitly tracked acquisition state.
3. Prove every clone/copy failure branch and rollback. Verify that source paths are never freed, destination paths are freed once, destination count is restored, and abandoned scratch bytes remain owned by the destination scope until release.
4. Annotate the scratch-lifetime part of the real `ViewTree::reset_retained()` body. Provide explicit contracts for tree teardown and measurement-cache helpers, covering the permissions they consume/preserve. These bodies remain listed as trusted, not disguised as verified DOM ownership.
5. Build a composition harness using real production definitions: construct a source list on the view scratch arena; copy admitted items to a retained destination on a separate arena; end all source/list borrows; destroy the source list and other admitted scratch scopes; reset the source view tree; read the retained copied payloads; destroy the retained list before its arena.
6. Include both a minimal tree and a contracted nonempty-tree case if the dependency predicates can represent it faithfully. Clearly state which tree internals are assumed in the latter.
7. Add a negative counterpart that calls reset after `dl_clear()` but before scope destruction, and another that retains a source scratch borrow across reset. Both must fail proof. Test surviving destination payloads through real post-reset reads in the positive case.
8. Audit reset call sites in `radiant/layout.cpp` and `radiant/event.cpp` for the pilot's preconditions. List the remaining caller obligations; this audit does not convert either whole function into a verified caller.
9. Extend the existing retained-display-list native suite for source-reset independence and clone/copy rollback. Keep image/glyph/video/picture/polygon cases outside the formal claim even if their existing tests pass.

**Exit gate:** the admitted retained-copy bodies and lifecycle composition verify, the reset misuse examples fail for the intended reason, and the report explicitly identifies unproved application entry paths and contracted tree/backend internals.

### 15. Phase 5 — Reproducible checks and handoff

**Work:**

1. Implement the manifest-driven runner. It must fail if the verifier is missing, a target is silently skipped, a required symbol disappears, a dependency classification changes without review, or an unexpected diagnostic/timeout occurs.
2. Separate positive proof results from expected negative failures. A negative test passes only for the intended memory/ownership failure; a parse error, missing include or crash is a failing test infrastructure result.
3. Record source/configuration hashes, tool/frontend/solver identity, commands, checked bodies, restrictions, trusted contracts, negative-test outcomes, elapsed time and proof failures under `temp/verifast-radiant/`. Hash included contracts and relevant headers as well as `.cpp` files.
4. Add the proposed `make verify-radiant-memory` target. Keep the target independent of building the full GUI where possible. Expose configuration errors rather than treating absent tools as a successful skip.
5. Add one pinned CI configuration after the local gates pass. Changes to admitted sources, their headers/contracts, the manifest or runner must rerun it. Expand platform coverage only with an explicit additional configuration.
6. Run native regressions and required repository checks after production changes. Update this document with measured proof coverage and any blocked phases; do not replace the scope-qualified theorem with a percentage of “Radiant verified.”

**Exit gate:** a clean checkout with the pinned tool can reproduce the proof and expected-failure results. Every required target is accounted for; no diagnostic suppression or trusted-contract expansion silently changes the claimed guarantee.

### 16. Verification and regression matrix

| Obligation | Formal evidence | Native evidence |
|---|---|---|
| No copy through null allocation | Success/failure branches of copy helpers and admitted callers | Deterministic allocation failure, including failure after path acquisition |
| No undersized destination | Checked byte-count arithmetic, allocator size/alignment contract | Boundary cases without constructing invalid readable-source spans |
| Source survives copy/rollback | Source permissions returned unchanged | Source values/path ownership inspected after success and partial failure |
| List growth is safe | Verified specialization or an explicitly incomplete storage phase | Growth preserves items; stale pointers are never dereferenced by the test |
| Cleanup releases external payload once | Cleanup and rollback ownership invariants | Instrumented clone/free counts with the existing backend test boundary |
| Clear differs from destroy | Scope-registration postconditions | Clear/reuse remains registered; destroy releases the scope |
| Scratch cannot escape reset | Outstanding-borrow reset failures and positive composition | Reset followed by valid retained destination reads |
| Source/destination lifetimes are independent | Distinct owner predicates in copy composition | Separate arenas; destroy/reset source first |
| Annotations match shipped code | Same production definitions and manifest source hashes | Ordinary C++ build and existing regressions |

Use the existing suites `test/test_display_list_gtest.cpp`, `test/test_retained_display_list_gtest.cpp`, `test/lib/test_arena_gtest.cpp`, and `test/lib/test_scratch_arena_gtest.cpp`. Read their current stubs carefully: mock backend success establishes client behavior under that mock, not a backend proof. Do not weaken tests to accommodate a proof failure.

After implementing production changes, the baseline check sequence includes:

```sh
make build-test
./test/test_display_list_gtest.exe
./test/test_retained_display_list_gtest.exe
make test-radiant-baseline
make lint ARGS='--rule ^no-int-cast-radiant$'
```

Resolve the current arena/scratch test executable names from the build configuration and run them when their contracts or implementations change. Run the Lambda baseline if shared engine behavior is changed. Add the proposed verification target only once Phase 5 implements it.

ASan is complementary evidence. The current `make build-debug-asan` builds a separate `lambda-debug-asan.exe`; it does **not** automatically make the native test executables sanitizer-enabled. Select/configure the relevant test profile through `build_lambda_config.json` and record which binaries are instrumented. A clean sanitizer run cannot establish safety after logical arena invalidation when pages remain allocated, so the borrow/reset proof remains necessary. No performance claim is part of this pilot; any later benchmark uses `make release` per repository rules.

### 17. Risks, scope controls and completion checklist

| Risk | Required response |
|---|---|
| Unsupported C++/headers/object lifetime | Isolate a reproducible Phase 0 failure. Use a justified production module boundary or leave the phase blocked. |
| Allocator contract disagrees with implementation or formal design | Record the exact gap; fix a proven implementation defect within authorized scope or resolve the ruling before dependent work. Never strengthen the assumed API for convenience. |
| Proof grows into all rendering/DOM | Freeze the manifest and opcode set. Keep additional bodies as explicit dependencies or future work. |
| Allocation failure leaves a partial item | Repair the production construction/rollback sequence and prove both outcomes. |
| Scope count fails to account for a borrow | Fix the ownership model and the responsible lifecycle code; do not assert the borrow disappeared. |
| Generation check is used as a liveness proof | Require live-owner permission first; leave borrowed resource families outside the pilot. |
| Proof-only source diverges | Verify shared production definitions and declarations; reject copied implementation/model layouts. |
| Tests pass using backend stubs | Preserve the backend contract in the trusted inventory and avoid upgrading the claim. |

The bounded pilot is complete only when:

- [ ] The pinned tool accepts the actual admitted production source and target configuration.
- [ ] The predicate library has positive construction examples and meaningful negative clients.
- [ ] Copying and admitted callers handle allocator/clone failure without unsafe accesses or source ownership loss.
- [ ] The manifest records every checked body, restricted domain and trusted dependency.
- [ ] The required list storage/lifecycle proofs pass; any missing specialization proof remains visibly incomplete.
- [ ] The retained-copy/reset composition passes using real production definitions.
- [ ] Use-after-release, reset-with-borrow, reset-after-clear and double-ownership negative cases fail for their intended obligations.
- [ ] Native regressions, Radiant baseline and applicable lint checks pass after code changes.
- [ ] CI reproduces the result without hidden skips or unpinned dependencies.
- [ ] The final report distinguishes module proofs, caller proofs, assumptions and uncovered application paths.

All boxes are initially unchecked. Partial verified results remain useful, but a blocked phase is not a completed pilot. Subsequent work may reduce the trusted allocator/backend boundary, admit more payload families, or verify application reset callers; each is a separate scope decision supported by the measured pilot results.
