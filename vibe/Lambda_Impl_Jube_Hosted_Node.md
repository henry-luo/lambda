# Jube Hosted Node — Detailed Implementation Plan

> **Status:** plan, drafted 2026-07-26; implementation not started
>
> **Design authority:** `vibe/Lambda_Design_Jube_Node_Hosting.md` (JN1–JN14,
> stages N0–N7) as governed by the architecture ADRs in
> `vibe/Lambda_Design_Jube_Architecture.md` (JA1–JA16). Where the two disagree,
> the ADRs win; the two supersessions that affect this plan (WebCrypto →
> `web-crypto` module; `stream/web` → `web-streams` re-export) are folded into
> the stages below rather than treated as amendments.
>
> **Template:** `vibe/Lambda_Impl_Hosted_Python.md` (H0–H10). This plan reuses
> its stage anatomy (Goal / Tasks / Exit gate), its checkpoint discipline
> (static first, behavioral parity, then dynamic), and its verification matrix
> shape. The runtime relationship is inverted — the engine calls the module —
> so the compiler-service stages have no analog here; the service-table stage
> (N2) replaces them.
>
> **Anchors:** all `file:line` references were re-verified against master on
> **2026-07-26**. They will drift; each stage re-verifies its own set at stage
> start (this is a standing task in every stage, not repeated below).

## 1. Purpose and end state

Migrate the built-in Node.js compatibility layer (≈55.4k lines across 21
`lambda/js/js_*.cpp` builtins plus ≈3.7k lines embedded in core files) out of
the monolithic host into Jube native modules, discovered and verified exactly
like `lang-python`, while the JS engine itself stays in the host binary.

```text
lambda.exe                      (host: Lambda + JS engine + Jube registry/loader + event loop)
modules/
  node-core/          module.json + node-core.dylib      (events, stream, buffer, util, path, …)
  node-zlib/          module.json + node-zlib.dylib      (zlib)                       [first dynamic]
  node-fs/            module.json + node-fs.dylib        (fs, fs/promises)
  node-net/           module.json + node-net.dylib       (net, dns)
  node-child-process/ module.json + …                    (child_process)
  node-http/          module.json + …                    (http, https)
  node-tls/           module.json + …                    (tls)
  node-crypto/        module.json + …                    (node:crypto)
  web-crypto/         module.json + …                    (globalThis.crypto)          [JA1/JA3-2a]
  web-streams/        module.json + …                    (ReadableStream, …)          [JA1/JA3-2a]
```

The migration is behavior-preserving: `make node-baseline` is the arbiter at
every stage, and the standard bundle (host + `node-core` + manifest-only leaf
descriptors) is observably identical to today's monolith. Only the minimal
profile — host with no node modules — changes what scripts can see, and that
is its purpose.

## 2. Non-negotiable implementation constraints

These are release gates, not preferences:

1. **Node observable behavior does not change.** `make node-baseline` (locked
   set in `test/node/official_baseline.txt`; 1492/3517 at last ledger) passes
   at every stage; the locked set may only grow. The in-tree corpus
   (`test/node/*.js`, 110 scripts + goldens) stays green inside
   `test-lambda-baseline`.
2. **Lambda/JS hot paths gain nothing.** No language/module lookup, capability
   check, or trampoline on existing evaluation, call, property, allocation,
   GC, or JIT-generated paths. MIR emission for existing scripts is unchanged
   — the `test/mir` ratchet (`test/mir/mir_budgets.json`) stays green with no
   budget lifts attributable to this work (JN13).
3. **Absent module = zero cost + Node-shaped failure.** A bundle without a
   node module pays no initialization, mapping, or allocation for it;
   `require('x')` produces `Cannot find module 'x'` with
   `code: 'MODULE_NOT_FOUND'` plus one host `log_*` line naming the missing
   Jube module. Never a link failure. A host with no node modules still runs
   non-Node JS (JN5, P4).
4. `make test-lambda-baseline` and `make test262-baseline` finish with zero
   failures (Test262 additionally zero retries) after every shared-runtime
   change.
5. **Static first, dynamic second, behaviorally identical** (P7). Every module
   lands as a statically registered `JubeModuleDef`
   (`jube_register_static_module`, `lambda/jube/jube_registry.h:9`), proves
   parity on its test slice, then flips to an external dylib. Chain/list
   deletions happen only after the registry path serves the affected
   specifiers.
6. **Modules see only `jube.h`** (P3, JA5). No `js_runtime.h`,
   `js_runtime_state.hpp`, `Context` layout, GC internals — and no libuv:
   `uv_*` is on the banned-symbol list (JN8, JA7). The architecture checker
   (§16) enforces this in CI from N2 onward.
7. **Errors return, exceptions pend** (P6). No C++ unwinding across the
   boundary; `throw_value`/`check_exception`
   (`JubeHostScriptAPI`, `lambda/jube/jube.h:423`) and status codes only.
8. **Rooting is precise and host-mediated** (JN9, CLAUDE rule 15). Every
   converted file passes the forced-GC gate
   (`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, the
   `test-gc-rooting-python` pattern at `Makefile:1096`) before it may flip to
   dynamic.
9. **Release performance protocol** (§18): no statistically significant
   Lambda/JS/Node regression; measurements on `make release` builds only
   (CLAUDE rule 10).
10. **C2MIR stays frozen** (CLAUDE rule 14). No `transpile.cpp` work; MIR
    Direct only.
11. **One host, byte-identical across bundles** (JA4). `verify-jube-package`'s
    `cmp -s` identity check (`Makefile:810`) extends to the node bundles.
12. **Finalizer rules** (P8, native-module design §6.3): `heap_cleanup`, vmap
    `destroy`, and anything on the marking path never allocate and never
    re-enter script.
13. **No cross-module symbol imports** (JA8). Inter-module needs are manifest
    `dependencies` (already parsed, `jube_registry.cpp:3414`) or script-level
    re-export; never a dylib-to-dylib symbol.
14. **Additive-only ABI evolution** (JA11): new tables/fields append behind
    `struct_size` gates, following the `JubeHostAPI.data` tail precedent
    (`jube.h:976`).
15. House rules apply throughout: `./lib` containers not `std::` (rule 3),
    `log_*` not printf (rule 4), no `/tmp` (rule 2), root-cause comments at
    fix points (rule 12), no code duplication — the per-module build targets
    and manifests are generated from one template, not copy-edited (rule 13).

## 3. Scope and non-goals

### 3.1 In scope

- Manifest `kind`/`engine`/`provides` support and the registry **specifier
  index** replacing the builtin memcmp chain and all four name lists.
- The `JubeHostNodeAPI` composed service tables (async micro-kernel + socket/
  process/signal tier, binary, promise, Node-error) and their host
  implementations (rid resource table, work pool, thread-safe completion
  post).
- Migration of the 21 builtins plus the embedded core pieces (process
  extensions, `internalBinding`, stubs, error helpers) into the module set of
  §1, static → dynamic each.
- The §10 hook inversions (console formatter, shutdown, IPC handoff, globals
  installation including lazy installation for dynamic modules).
- Rooting conversion of every migrated file to Jube GC services, gated by
  forced-GC sweeps.
- The `web-crypto` and `web-streams` modules to the extent the JN14/JA1
  supersessions require (three-way crypto split; `stream/web` re-export) —
  with an explicitly legal transitional state if their WPT gates are not yet
  wired (§13.4).
- Packaging: standard = host + `node-core` (+ manifest-only leaf
  descriptors); full = all; minimal = host only.
- The `check_node_module_architecture.py` checker family with synthetic
  self-tests.

### 3.2 Out of scope

- Changing Node compatibility semantics or growing coverage (separate work;
  the baseline may grow but this plan doesn't grow it).
- The browser/DOM/Web surface (`js_dom*`, `js_fetch`, `js_canvas`, ≈27k
  lines) — POC 1 / `radiant-dom` territory. `js_fetch`'s future as a Jube
  module (JA1 consequences) is its own plan.
- Moving the event loop, global timers, microtask/nextTick machinery,
  `console` core, `process` core, the JS module cache, the npm resolver
  (revisit after N5), or `node:vm` out of the host (JN6).
- The JA16 central IO API design (open item 8 there). This plan's banned
  `uv_*` checker list is JA16's first enforcement instance, and the §14 op
  families are input to that design, but the IO-policy layer is not built
  here.
- Sandboxing/trust-tier work (JA10 T2/T3), hot unload, Windows CI bring-up
  beyond keeping the export surface auditable (§20 risk 7).
- The WPT harness *decision* (JA9 / architecture open item 3) — N6c/N6d
  depend on it; a default is proposed (§13.4) but deciding it is its own
  review.

## 4. Verified starting point (2026-07-26)

### 4.1 Infrastructure reused as-is

| Facility | Anchor | Notes |
|---|---|---|
| `JubeModuleDef` (namespaces/types/functions, `init`, `shutdown`; additive tail: `interface_decl`, `type_bindings`, `runtime_reset`, `heap_cleanup`, `language`) | `lambda/jube/jube.h:1120`; `JUBE_MODULE_DEF_V1_SIZE` | node modules use `namespaces` + `types` + the two lifecycle hooks; `language` stays NULL (JN1) |
| `JubeNamespaceDef` = `specifiers[]` + `specifier_count` + `Item (*build)(void)` + optional `JubeFuncDef` table | `jube.h:277` | exactly the shape a converted `js_get_<x>_namespace()` adapts to |
| `JubeTypeBinding`/`JubeMemberBind` + `interface_decl` (DOM3 dispatch) | `jube.h:296/:310` | for class-shaped surfaces (Socket, Server, Hash, FileHandle, …) |
| `JubeHostAPI` (`hosted_language`, `gc`, `value`, `script`, `dom`, `runtime_catalog`; additive tail `data`) | `jube.h:976` | the new `node` parent appends after `data`, size-gated |
| `JubeHostGcAPI` (`register_root`, `unregister_root`, `root_frame_begin/take_slot/end`, additive `register_weak`/`unregister_weak`) | `jube.h:355` | JN9 target for conversions |
| `JubeHostRootAPI` (opaque `JubeRootFrame`, `persistent_root_register(session, slot)`) | `jube.h:~370` | hosted-language tail; N2 decides which frame flavor node modules standardize on (§15) |
| `JubeHostScriptAPI` (`new_function`, `throw_value`, `check_exception`, `call_function`, `new_error_with_name`, `global_this`, …) | `jube.h:423` | value mechanics stay here (the `js_native_api.h` half) |
| Static registration + enumeration | `jube_register_static_module` `jube_registry.cpp:2957`; registration site `:3611` (radiant, hostobj_demo); `jube_static_module_count/_at` `:3619/:3623` | in-tree static module sources live under `lambda/module/<name>/` (`radiant_module.cpp:1534`, `hostobj_demo_module.cpp:266`) — node modules follow this convention |
| Manifest scan + verification | scan `jube_registry.cpp:3217–3250`, `:3516–3546`; per-OS `library_*`/`sha256_*` keys `:3303–3319`; SHA-256 verify `:3323`; fields parsed today: `language`/`aliases`/`extensions` (`:3296–3300`), `entry_symbol`, `host_build_id`, `base_abi_version`, `hosted_api_version` (`:3392–3403`), **`dependencies` (`:3414`)**, `resources` (`:3423`) | `dependencies` and the loading-path cycle/depth guard (`jube_manifest_loading_paths`, `:54`) already exist — JN2's inter-module deps need no new machinery |
| Loader + rollback | `jube_load_dynamic_module_checked` `:2963` (entry symbol default `"jube_module"`, `:2973`) | transactional registration reused unchanged |
| Module root override | `JUBE_MODULE_PATH` env (`:3573`) | used by integrity/parity tests |
| Scan-on-first-need pattern | `jube_discover_hosted_language` `:3564` | the specifier index copies this trigger discipline |
| Build-target shape | `build_lambda_config.json:305` (`lang-python`: `"link": "dynamic"`, `"pic": true`, `"target_dir": "modules/lang-python"`, `link_options_macos: ["-Wl,-undefined,dynamic_lookup"]`, explicit `source_files`) | cloned per node module (via one generator, rule 13) |
| Build/package targets | `build-lang-python` `Makefile:772`, `release-lang-python` `:783`, `package-standard` `:795` (manifest-only descriptor precedent), `package-jube` `:803`, `verify-jube-package` `:810` (shasum + `cmp -s` + absent/full smokes), `release-jube` `:849` | node analogs cloned in N4/N7 |
| Manifest stamping | `utils/update_jube_manifest_integrity.py <module dir>` | reused verbatim |
| Loader negative/integrity/dispatch matrices | `test-jube-module-loader-negative` `Makefile:836` (`utils/test_jube_module_loader_negative.py`), `test-jube-module-integrity` `:822` | parameterized/extended for node modules |
| Architecture-checker methodology | `utils/check_hosted_python_architecture.py` (path-anchored source rules + binary `nm` mode via `--require-module-binary`; "checks added only after their owning stage lands" philosophy), self-test `utils/test_hosted_python_architecture_checker.py`, `check-hosted-python-architecture` `Makefile:2362`, coupling-inventory target `:2367` | template for §16 |
| node-baseline harness | `node-baseline` `Makefile:1217` → `./test/test_node_gtest.exe --baseline-only` over `ref/node/test/parallel/` with shims from `lambda/js/test_shim/` (`node-shim` `:1154`); locked set `test/node/official_baseline.txt`; report `test/node/node_official_report.py` (`:1237`); adjacent `node-regression-gate`/`node-full`/`node-update-baseline` | the continuous gate |
| Forced-GC gate mechanics | `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` + golden diff (`test-gc-rooting-python`, `Makefile:1096`); corpus sweep exe precedent `test/test_mir_gc_stress_gtest.exe` (`test-mir-gc-stress` `:1091`) | §17.3 |
| Async substrate + precedents | loop accessor `lambda_uv_loop()` and drain hooks `lambda_uv_set_microtask_drain`/`lambda_uv_set_task_drain` (`lib/uv_loop.h`); in-tree `uv_queue_work` pools: `lambda/network/network_thread_pool.cpp`, `lambda/serve/worker_pool.cpp`, `js_fetch.cpp:546`; `uv_async_init` post: `lambda/runtime/concurrency.cpp:580` | the §14 micro-kernel is assembled from these, host-side |

### 4.2 What does not exist yet (built by this plan)

- Manifest `kind`, `engine`, `provides` parsing (confirmed absent from
  `jube_registry.cpp`) and the two-index registry (language index + specifier
  index) — N1.
- Any specifier→module mapping: today nothing connects `require("x")` to the
  Jube registry — N1.
- Dynamic-module namespace/global installation: `js_install_jube_global_namespaces`
  (`lambda/js/js_globals.cpp:~74`) iterates **static modules only** and
  installs **eagerly** (`ns->build()` at global setup). Lazy accessor
  installation and dynamic-module coverage — N3.
- `JubeHostNodeAPI` and all four child tables, the rid resource table, the
  shared blocking-work pool as a host API, thread-safe completion post as a
  module-facing service — N2.
- `utils/check_node_module_architecture.py` + self-test — N0.
- A node coupling-inventory generator (the hosted-python one at
  `Makefile:2367` is Python-specific) — N0.
- Require-latency / namespace-build microbenches (none exist) — N0.
- WPT harness for module slices (JA9) — prerequisite for N6c/N6d only.

### 4.3 The migration population

The authoritative inventory is design §4 (sizes, per-file coupling, backward
calls); this plan does not duplicate it. Key anchors re-verified today, with
drift corrections against the 2026-07-25 design pass:

- Builtin chain `js_module_get_builtin` — `lambda/js/js_runtime.cpp:38891`
  (was :38863). ESM/cache `js_module_get` `:39842`; `js_module_is_builtin`
  `:39856`; `builtinModules` for `node:module` `:39237–39261`.
- Lowering-time skip list `builtin_names[]` —
  `js_mir_module_batch_lowering.cpp:1931`; `node:` special cases nearby and in
  `js_mir_entrypoints_require.cpp:1581`; the builtin extern consumed by
  require at `js_mir_entrypoints_require.cpp:42`.
- `internalBinding` → `js_internal_binding` `js_runtime.cpp:38513`
  (`'uv'` constants `:38517`, `'config'` `:38600`, `internal/test/binding`
  `:39728`).
- `ERR_*` helpers (`js_throw_type_error_code` family) `js_runtime.cpp:23307+`.
- First adopters: `js_path.cpp` 669 lines, `js_get_path_namespace` `:613`;
  `js_zlib.cpp` 1,429 lines, `js_get_zlib_namespace` `:1285`;
  `js_crypto.cpp` 8,725 lines, `js_get_crypto_namespace` `:8584`.
- Globals install cluster (process/Buffer/os/vm, `ReadableStream` at
  `js_globals.cpp:16143`) — `js_globals.cpp:16009+`.
- Error-code header users: assert, dns, buffer, child_process, crypto, fs,
  globals … all include `lambda/js/js_error_codes.h`.

## 5. Stage map

| Stage | Deliverable | Primary gate | Rough size |
|---|---|---|---|
| N0 | evidence baseline, coupling inventory, checker + self-test, microbenches | tooling green; inventory fully classified | small (tooling only) |
| N1 | manifest `kind`/`provides`, specifier index, compile-time consumers rewired, `path` static module | node-baseline + MIR budgets unchanged; `path` registry-served | small–medium |
| N2 | `JubeHostNodeAPI` v1 (4 tables), rid table, work pool, completion post; fs pilot conversion in place; checker in CI | pilot green under forced GC; negative descriptor tests | medium |
| N3 | `node-core` static; process split; hook inversions; chain + 4 lists deleted | node-baseline; chains gone; checker green on `lambda/module/node_core` | large (~30k lines move) |
| N4 | `node-zlib` first dynamic module; delivery chain cloned; absent-module negatives | dynamic load macOS+Linux; static/dynamic parity | small |
| N5 | `node-fs` → `node-net` → `node-child-process`, static→dynamic each | per-module: baseline, forced-GC, zero `uv_*`, negatives | large |
| N6 | `node-http` → `node-tls` → crypto three-way split (+`web-crypto`) → `web-streams` flip | as N5 + crypto/tls/http optional at runtime | large |
| N7 | `node-core` dynamic; packaging matrix; docs; allowlist burn-down; perf closeout | byte-identical host ×3 bundles; minimal profile green; perf vs N0 | medium |

```text
N0 ──► N1 ──► N2 ──► N3 ──► N4 ──► N5a fs ──► N5b net ──► N5c child_process
                                        └───────────────────────► N6a http ──► N6b tls ──► N6c crypto split ──► N7
                                                                     (N6d web-streams joins N6c; both WPT-gated, deferrable past N7)
```

Stages are independently shippable checkpoints; each holds constraints §2 in
full. N5/N6 sub-stages are strictly sequential per module (each is a rooting
conversion with its own forced-GC gate) but the *next* module's source
reorganization may start while the previous one's dynamic flip soaks.

## 6. Stage N0 — Freeze evidence and tooling

### Goal

Reproducible baselines, a mechanical coupling ledger, and a checker that can
gate all later stages — before any shared code moves.

### Tasks

- [ ] **N0.1** Record baselines into a dated evidence directory
  `test/benchmark/hosted_node/` (checked in, mirroring
  `test/benchmark/hosted_python/`): `make node-baseline` ledger,
  `make test-lambda-baseline`, `make test262-baseline`, release host binary
  size, `otool -L`/`ldd` mapped-library list (zlib/mbedTLS presence),
  node-baseline wall time. Record revision, compiler, platform, commands
  (template §20 method).
- [ ] **N0.2** Write the require/namespace microbench: a JS script timing
  (a) first `require` of each builtin (cold namespace build) and (b) 10⁵
  cached requires, run via the harness with 7+ repetitions; store runner +
  results under `test/benchmark/hosted_node/`. This is the perf yardstick for
  the registry rewiring (N1) and the N7 closeout.
- [ ] **N0.3** Write `utils/hosted_node_coupling_inventory.py` + make target
  `hosted-node-coupling-inventory` (pattern: `Makefile:2367`). Deterministic
  JSON records over the §4.3 population: every `uv_*` call site;
  `heap_register_gc_root(_range)` externs; `RootFrame`/`Rooted` uses;
  `js_heap_epoch` reads; `js_runtime_state.hpp`/`js_runtime.h` includers;
  JS-queue externs (`js_next_tick_enqueue`, `js_setTimeout`, …); the §10
  backward-call edges; inter-builtin externs. Each record carries a
  classification — `microkernel-op` / `tier2-op` / `hook` / `rooting` /
  `moves-with-file` / `stays-host` / `delete` — and an owning stage. Zero
  unclassified records is the gate (template H0.4/H0.5 discipline).
- [ ] **N0.4** Write `utils/check_node_module_architecture.py` per §16 with
  its synthetic self-test `utils/test_node_module_architecture_checker.py`
  (the checker must prove it rejects injected violations, not merely pass on
  a clean tree — `Makefile:846` precedent). Make targets
  `check-node-module-architecture` / `test-node-module-architecture-checker`.
  Until N2 it runs in report mode against the future module dirs (empty) and
  the inventory; from N2 it enforces (§16.3).
- [ ] **N0.5** Confirm and record the two audit questions that shape N4/N6:
  who else links zlib (host link stays if e.g. PNG/PDF paths need it) and who
  else links mbedTLS (`lambda-lib` per the native-module doc). The answers
  decide whether N4/N6 can drop them from the host link or only from the
  Node surface.

### Exit gate

- Evidence directory populated; inventory has zero unclassified entries;
  checker self-test green; all baselines green and archived.
- No product behavior change of any kind.

## 7. Stage N1 — Registry specifier index + `path` pipe-cleaner

### Goal

The resolution spine: manifests can declare runtime-library modules, one
specifier index answers "is this a builtin and who owns it", and the smallest
zero-dependency builtin (`path`) proves the whole static path end to end.

### Tasks

- [ ] **N1.1** Manifest schema (JN3): parse `"kind"`
  (`"runtime-library"` | absent=language), `"engine"` (`"js"`), and
  `"provides"` (string array; reuse `jube_manifest_string_array`, the
  `dependencies` parser at `jube_registry.cpp:3414` is the model). Scan-time
  validation: a manifest with neither `language` nor `provides` is rejected
  with a logged reason. Builtin-ness must be decidable from manifests alone —
  no dlopen at scan (JN3).
- [ ] **N1.2** Specifier index: a registry-owned `HashMap` (`lib/hashmap.h`)
  from normalized specifier → module slot, populated from (a) every scanned
  manifest's `provides` and (b) every static module's
  `JubeNamespaceDef.specifiers`. Normalization at insert *and* lookup: strip
  `node:` prefix except for prefix-only names (`node:test`, …, enumerated
  from the chain during this stage); strip a trailing `.js`. New API surface
  (registry header, host-internal): `jube_specifier_lookup(name)` (may
  trigger lazy dlopen of the owning module, JN5 semantics on failure),
  `jube_specifier_is_builtin(name)` (manifest-index query, never loads),
  `jube_specifier_index_names(cb)` (for `builtinModules`). Index construction
  is lazy on first miss, copying the `jube_discover_hosted_language` trigger
  discipline (`:3564`) so non-Node runs never scan.
- [ ] **N1.3** Route `js_module_get_builtin` (`js_runtime.cpp:38891`)
  **registry-first**: normalized lookup → registered namespace `build()` (or
  cached Item); on miss, fall through to the existing chain unchanged.
  Namespace Items keep flowing into the existing `js_modules[]` cache — the
  registry supplies namespaces, the engine owns caching (JN6).
- [ ] **N1.4** Rewire the three compile-time/introspection consumers to ask
  the index first, list second (the lists shrink stage by stage until N3
  deletes them): the lowering skip list
  (`js_mir_module_batch_lowering.cpp:1931`), `js_module_is_builtin`
  (`js_runtime.cpp:39856`), and `builtinModules`
  (`:39237` — becomes index-derived ∪ residual list so reflection matches
  reality per bundle from day one).
- [ ] **N1.5** Convert `path`: move `lambda/js/js_path.cpp` →
  `lambda/module/node_core/js_path.cpp` (it ends up in `node-core`; the
  directory starts existing now), add
  `lambda/module/node_core/node_core_module.cpp` with a static
  `JubeModuleDef` whose first `JubeNamespaceDef` wraps
  `js_get_path_namespace` (`build()` callback), specifiers = exactly the
  set the chain serves today (`path`, `path/posix`, `path/win32`, plus alias
  forms — enumerate from the chain before deleting). Register beside radiant
  at `jube_registry.cpp:3611`. Keep the file compiling into the host target
  (static stage) via `build_lambda_config.json` source-list update.
- [ ] **N1.6** Delete `path`'s rows from the chain and all remaining lists.
  From this stage on, a specifier must live in exactly one place.
- [ ] **N1.7** Tests: registry unit tests (normalization incl. `node:path`
  and `path.js`, unknown specifier, prefix-only names, static-provides
  lookup, index enumeration); `require('path')`/`import` smoke through both
  CJS and ESM paths; N0.2 microbench re-run (require latency must not
  regress beyond noise — one hash lookup replaces ≤3 memcmps per chain
  entry).

### Exit gate

- node-baseline unchanged; `test/mir` ratchet green (resolution rewiring must
  not change emission — the literal-`require` intercept at
  `js_mir_expression_lowering.cpp:6862` still emits the same call, JN13).
- `path` served via registry; its list rows gone; the four lists still
  mutually consistent for everything else.
- Lambda + Test262 baselines green.

## 8. Stage N2 — `JubeHostNodeAPI` v1 and the async micro-kernel

### Goal

The runtime-services half of the ABI (the `node_api.h` analog): one additive
parent on `JubeHostAPI` composing async, binary, promise, and error tables —
extracted from real call sites, exercised by a real conversion, enforced by
the checker.

### Tasks

- [ ] **N2.1** Freeze the v1 op inventory from N0.3's ledger: map every
  `uv_*`/JS-queue call site in fs/net/child_process/dns (plus the §20-risk-8
  edges: `fs.watch`, tty raw mode, IPC descriptor passing, `uv_tcp_open`-style
  fd adoption) onto §14's micro-kernel or Tier-2 ops. Every op cites ≥1 call
  site (P5); every call site maps to exactly one op or to `moves-with-file`
  (plain syscalls a module may make directly, e.g. tty `termios`). The
  worksheet lands in this document's §14 as the signed-off v1 surface.
- [ ] **N2.2** ABI: append `const JubeHostNodeAPI* node;` after `data` on
  `JubeHostAPI` (`jube.h:976`), size-gated; define the parent + four child
  tables in `jube.h` per §14, each with `api_version`/`struct_size` first
  (the `JubeHostRootAPI`/`JubeHostDataAPI` precedent). Table names are
  engine-generic (`JubeHostAsyncAPI`, `JubeHostBinaryAPI`,
  `JubeHostPromiseAPI`, `JubeHostNodeErrorAPI`) because JA7 makes the async
  micro-kernel architecture-wide; only the error table and the parent are
  Node-flavored.
- [ ] **N2.3** Host implementation, new files:
  - `lambda/js/js_async_services.cpp` — micro-kernel over the existing
    substrate: `work_submit` on `uv_queue_work(lambda_uv_loop(), …)`
    (pool precedents: `network_thread_pool.cpp`, `serve/worker_pool.cpp`);
    `post_completion` on a host-owned `uv_async_t` + locked queue
    (`concurrency.cpp:580` precedent), draining nextTick/microtasks after
    each completion batch through the same path
    `lambda_uv_set_microtask_drain` already services (`js_event_loop.cpp:1720`)
    — the MakeCallback discipline, host-owned;
    scheduling entries delegate to `js_next_tick_enqueue`
    (`js_event_loop.cpp:163`), the microtask queue, and the existing timer
    wheel; `register_shutdown` keeps an ordered host list.
  - `lambda/js/js_resource_table.cpp` — the rid table: slot array +
    generation bits packed into `uint32_t` rid; entries carry kind tag, an
    opaque host pointer, `close_fn`, and a ref flag; `rid_ref`/`rid_unref`
    forward to `uv_ref`/`uv_unref` on the backing handle (or a loop-liveness
    counter for non-handle resources) so "active handles keep the process
    alive" (`lambda_uv_run` semantics, `lib/uv_loop.h`) is preserved;
    `rid_close` cancels in-flight requests with canceled-status completions
    (§14 contract); host-side enumeration API for `_getActiveHandles` (JN10).
  - `lambda/jube/jube_node_host.cpp` — table assembly + version plumbing.
- [ ] **N2.4** Tier-2 ops (stream/process/signal families, §14) implemented
  host-side over the existing libuv usage patterns lifted from
  js_net/js_child_process — implementation moves behind the API now, the
  *callers* convert in N5. Payload copy-on-submit for writes (v1 contract);
  op-specific completion structs defined in `jube.h` (valid only during the
  callback).
- [ ] **N2.5** Pilot conversion (the design's "fs picked next"): convert
  `js_fs.cpp`'s async `uv_fs_*` sites (`:717–750` region) **in place** to
  `work_submit` + completion, and its promise construction to the promise
  table. This proves the tables against a real consumer before any module
  move; the converted file rides into N5a unchanged.
- [ ] **N2.6** Rooting conversion guide: land §15 of this document as the
  reviewed pattern reference; convert the fs pilot's rooting as its first
  application (`js_fs.cpp:1989` in-function extern, `:2529` RootFrame).
- [ ] **N2.7** Negative descriptor tests: extend the loader-negative matrix
  with undersized/missing `node` tables, wrong `api_version`, and a module
  requesting the node capability from a host without it (clean refusal
  before init — `Makefile:836` matrix style).
- [ ] **N2.8** Checker goes to enforcement for `lambda/module/node_*/`
  sources (currently just `node_core` with `js_path.cpp` + the module def) —
  `uv_*`, engine internals, GC externs on the banned list; wire
  `check-node-module-architecture` into `build-test` CI.
- [ ] **N2.9** Decide and record in §14: which rooting frame flavor node
  modules use (recommendation: `JubeHostGcAPI` frames per design §9.5, with
  `JubeHostRootAPI`'s opaque frames as the documented alternative if layout
  hiding proves necessary); where the `ERR_*` implementation lives
  (recommendation: host-side behind `JubeHostNodeErrorAPI` so host compat
  paths share it — design §9.4 leaves this to N2).

### Exit gate

- fs pilot: full fs slice of node-baseline + `test/node` corpus green, and
  green under the forced-GC gate (§17.3).
- Table unit tests + negative descriptor tests green; checker enforcing in
  CI; MIR budgets untouched.
- v1 op worksheet (§14) updated to match what landed — no unused ops, no
  call site left mapping to a banned symbol.

## 9. Stage N3 — `node-core` (static)

### Goal

The dependency hub becomes one statically registered module; the engine's
builtin name knowledge is deleted; core→builtin edges become hooks.

### Tasks

- [ ] **N3.1** Move the §6.1 core set into `lambda/module/node_core/`:
  events, buffer, util, url, querystring, string_decoder, os, assert
  (+`node:test`), readline, permission glue, `timers/promises`, the stub
  namespaces, `internalBinding` (+ constant tables, incl. the test-only
  `internal/test/binding`, `js_runtime.cpp:39728`), and the Node error-code
  helpers' *registration* (implementation per N2.9). Move order within the
  stage: leaves first (querystring, string_decoder, os, url), hub files last
  (events → buffer → util → assert), stream last of all (N3.2). One review
  unit per file or coherent pair (§22).
- [ ] **N3.2** Split `js_stream.cpp` (10,111 lines): Node half →
  `lambda/module/node_core/js_stream_node.cpp`; the WHATWG half
  (ReadableStream/WritableStream class implementations — re-enumerate their
  exact extent at stage start) stays host-side in a new
  `lambda/js/js_web_streams.cpp` pending N6d. `stream/web` (and
  `stream/consumers` where it touches web streams) is served from
  `node-core` as a re-export over a transitional host hook. Sequencing rule
  from design §13.1: if a stream-coverage campaign is active, land stream
  last within N3 or after it.
- [ ] **N3.3** Process split (design §6.4 rule: engine-called stays, only
  Node-observable moves): enumerate the `js_globals.cpp:2700–4470` members
  into host-core vs node-core lists (the enumeration is a committed artifact
  in `test/benchmark/hosted_node/`); `node-core` extends the host `process`
  at init (memoryUsage, signals, `_getActiveHandles` via the rid-table
  enumeration, nextTick *binding* stays host). Risk 3 applies: test harnesses
  reading Node-only members (e.g. `process.memoryUsage` in the shims) are
  part of the enumeration.
- [ ] **N3.4** Invert the §10 backward calls as init-time hook registrations
  with absent defaults, via a small host-side hook registry
  (`lambda/js/js_host_hooks.h/.cpp`, typed setters, one absent-default each):
  console formatter (`js_globals.cpp:5990` edge; absent default = minimal
  engine formatter — write it now, test it in the minimal profile);
  shutdown participants (replaces the `js_net_close_all_active_servers`
  call at `js_runtime.cpp:35790`; servers now close via rid-table shutdown);
  IPC accept handoff (rid-based, replaces the `uv_pipe_t*` extern at
  `js_globals.cpp:138` — lands with node-net/child-process in N5 but the hook
  point is created here); cluster-online queue (`js_runtime.cpp:62`) moves
  wholesale to node-child-process's hook (N5c).
- [ ] **N3.5** Globals: extend the `JubeNamespaceDef` install path
  (`js_install_jube_global_namespaces`, `js_globals.cpp:~74`) to (a) cover
  registry (dynamic) modules, and (b) install **lazy accessors** — a global
  slot that builds on first touch and memoizes — replacing today's eager
  static-only walk. `Buffer`, `process` extensions, and the `os`/`vm`
  module-side globals ride this; the unconditional install block
  (`js_globals.cpp:16009+`) shrinks to host-core globals only. Absent
  `node-core` ⇒ `typeof Buffer === 'undefined'` (the minimal profile's
  defining observable).
- [ ] **N3.6** Delete the chain (`js_runtime.cpp:38891`, ~950 lines with its
  inline externs) and the two remaining lists (`js_module_is_builtin` body,
  the batch-lowering `builtin_names[]`); `builtinModules` becomes purely
  index-derived. Delete the `node:` special cases in
  `js_mir_entrypoints_require.cpp:1581` and
  `js_mir_module_batch_lowering.cpp` in favor of index normalization.
- [ ] **N3.7** Per-epoch namespace caching moves from ad-hoc `js_heap_epoch`
  checks inside getters to the descriptor's `runtime_reset` + `heap_cleanup`
  hooks (JN11) — the registry already invokes these for interface caches
  (`jube_interface_runtime_reset`).
- [ ] **N3.8** Rooting conversion for every moved file per §15, forced-GC
  gate per file batch (§17.3).

### Exit gate

- All four name lists and the chain are gone; grep proves no
  `js_module_get_builtin` chain remnant.
- node-baseline unchanged; `test/node` corpus green; forced-GC slice green
  on converted files; checker green over `lambda/module/node_core/`
  (allowlist entries counted and burned down to the transitional set).
- Minimal-profile smoke (host built without node_core registration in a test
  configuration): non-Node JS runs; `require('fs')` produces the JN5 error;
  console works via the absent-default formatter.
- Lambda + Test262 + MIR budgets green.

## 10. Stage N4 — `node-zlib`: first dynamic module

### Goal

Prove the full delivery chain (build target → manifest → hash → lazy dlopen →
absent negatives) on the smallest external-dep leaf before any big module
flips.

### Tasks

- [ ] **N4.1** Sources: `lambda/js/js_zlib.cpp` →
  `lambda/module/node_zlib/js_zlib.cpp` + `node_zlib_module.cpp`
  (`JubeModuleDef`, specifiers `zlib`/`node:zlib` normalized, buffer coupling
  through `JubeHostBinaryAPI` only, async surface through `work_submit`).
  Static checkpoint first (P7).
- [ ] **N4.2** Build target: add a module-target generator step
  (`utils/generate_premake.py` input) so each node module's
  `build_lambda_config.json` entry is emitted from one template (name,
  `link: dynamic`, `pic`, `target_dir: modules/node-zlib`,
  `-Wl,-undefined,dynamic_lookup` on macOS + the linux equivalent, explicit
  `source_files`) — clone of `build_lambda_config.json:305`, not copy-paste
  ×8 (rule 13). Make targets `build-node-zlib` / `release-node-zlib`
  mirroring `Makefile:772/:783` incl. the
  `update_jube_manifest_integrity.py modules/node-zlib` stamping step.
- [ ] **N4.3** `modules/node-zlib/module.json` per design §6.2 (kind
  `runtime-library`, engine `js`, `provides`, `dependencies: ["node-core"]`,
  per-OS library + sha256, `entry_symbol: "jube_module"`). Dependency
  activation exercises the existing `dependencies` machinery
  (`jube_registry.cpp:3414`) — add a test that requiring `zlib` with
  `node-core` present-but-unloaded activates `node-core` first.
- [ ] **N4.4** Flip dynamic: remove `node_zlib` sources from the host target;
  zlib leaves the host link **iff** the N0.5 audit cleared it (otherwise the
  host keeps its own zlib user and the module still links its own copy —
  record which). Static/dynamic parity: run the zlib slice (in-tree +
  official) in both modes via `JUBE_MODULE_PATH` bundle copies
  (`Makefile:822` isolation pattern) and diff.
- [ ] **N4.5** Absent-module negatives: bundle copy without the dylib →
  `require('zlib')` yields `MODULE_NOT_FOUND` + host log naming `node-zlib`
  (manifest-only descriptor upgrades the log to "known, not installed" —
  `package-standard` precedent at `Makefile:795`); tampered-library and
  wrong-ABI rows added to the loader-negative matrix (parameterize
  `utils/test_jube_module_loader_negative.py` by module rather than cloning
  it).

### Exit gate

- Dynamic load green on macOS + Linux; parity diff empty; node-baseline
  unchanged; negatives green; host byte-identity across packagings
  maintained.

## 11. Stage N5 — libuv leaves: fs, net, child_process

Each sub-stage follows one shared recipe (a checklist committed once in
`lambda/module/README.md` and referenced, not restated — rule 13):
move sources → convert every `uv_*`/JS-queue call to §14 ops → rooting per
§15 → static parity → checker zero-`uv_*` → manifest/build target from the
N4 generator → dynamic flip → absent negatives → inventory ledger rows
burned to zero for that file set.

- [ ] **N5a — `node-fs`** (3.9k lines; fs, fs/promises, FileHandle as
  `JubeTypeBinding`). The N2.5 pilot already converted the async core;
  remaining: sync surface (direct syscalls, module-local — allowed),
  `fs.watch` per the N2.1 decision (Tier-2 `fs_event` ops or documented
  deferral if baseline-cold), promises via the promise table,
  `js_next_tick_enqueue` extern (`js_fs.cpp:3127`) → scheduling ops,
  permission glue via node-core.
- [ ] **N5b — `node-net`** (7.9k lines; net + dns + cares_wrap shim). First
  real consumer of the Tier-2 stream ops (connect/listen/accept/read/write/
  shutdown/opts/fd-adoption → rids); the raw socket/server pool arrays with
  their `heap_register_gc_root` registration (`js_net.cpp:34/:120`) become
  rid-table entries + persistent roots; dns rides `work_submit`
  (getaddrinfo) exactly as libuv does internally; `_getActiveHandles`
  switches to rid-table enumeration (hook point from N3.4);
  direct `Context` access (`js_net.cpp:4591`) eliminated.
- [ ] **N5c — `node-child-process`** (3.8k lines). Tier-2 process ops
  (spawn/kill/exit-completion; single SIGCHLD owner stays host); stdio spec
  references rids; the IPC handoff hook (N3.4) lands for real — the
  `uv_pipe_t*` extern is deleted; cluster-online glue moves in
  (`js_runtime.cpp:62` edge gone).

### Exit gate (per module, then for the stage)

- node-baseline unchanged; forced-GC gate green on the module's slice;
  checker: zero `uv_*`, zero engine-internal references from
  `lambda/module/node_*/`; absent negatives green; static/dynamic parity
  diffed; `js_runtime_state.hpp` includer count for migrated files = 0.

## 12. Stage N6 — protocol leaves and the JA1 web modules

- [ ] **N6a — `node-http`** (6.9k lines; http + https; depends node-core +
  node-net). The HTTP/1.1 parser is module-local; raw libuv accept
  (`js_http.cpp:3357` region) converts to stream-op accept→rid; the 10-root
  `RootFrame` at `:5955` converts per §15. Profile against N0's throughput
  numbers before/after — this is the copy-on-submit stress case (§20 risk 8);
  if profiles demand it, spec the pinned zero-copy additive entry then, not
  preemptively.
- [ ] **N6b — `node-tls`** (3.1k lines; mbedTLS + the OpenSSL dlopen
  soft-dep for PFX stays module-side; depends node-net).
- [ ] **N6c — the three-way crypto split** (JN14 as superseded by JA1).
  Sub-task order: (1) extract a shared mbedTLS primitive layer
  (`lambda/module/crypto_primitives/`, compiled *into each* consumer —
  source-level sharing, no cross-module symbols per JA8; it may also serve
  the host's existing `lib/digest.h` users — audit); (2) `node-crypto`
  module (the node:crypto surface, ≈6–7k lines after the split); (3)
  `web-crypto` module (kind 2a) owning `globalThis.crypto` via the lazy
  globals path, WPT-slice gated (§13.4); `node:crypto`'s `webcrypto`
  property re-exports it. Re-enumerate the WebCrypto extent inside
  `js_crypto.cpp`/`js_globals.cpp` at sub-stage start (budget it as surgery,
  not a file move — design §13.2). mbedTLS drops from the minimal host link
  iff the N0.5 audit clears `lambda-lib`'s use.
- [ ] **N6d — `web-streams`** (kind 2a): `lambda/js/js_web_streams.cpp`
  (created in N3.2) becomes the module; the transitional host hook flips to
  a `node-core` script-level re-export of the module (`stream/web` mirrors
  how Node layers over WHATWG); `ReadableStream` globals
  (`js_globals.cpp:16143`) move to the module's lazy globals. WPT streams
  slice gated (§13.4).

### Exit gate

- node-baseline unchanged; `crypto`/`tls`/`http` individually absent at
  runtime produce clean JN5 errors (the native-module doc's POC 2 exit
  criterion); minimal-host link audit results recorded (zlib/mbedTLS);
  N6c/N6d WPT slices green *or* the deferral is recorded per §13.4 with the
  transitional state intact.

### §13.4 WPT gating and the legal transitional state

JA9 forbids landing a web-platform module without its WPT slice. The harness
choice (reuse Radiant WPT infra vs a js262-style runner; architecture open
item 3) is not this plan's decision. Therefore: **N6c(3) and N6d may trail
the rest of N6 indefinitely** — the transitional state (WebCrypto host-side
as today; WHATWG streams host-side in `js_web_streams.cpp` with the
`node-core` re-export hook) is explicitly legal and observable-behavior-
neutral. What is *not* legal is shipping `web-crypto`/`web-streams` modules
without their slices. When the harness decision lands, the default proposal
is a js262-style runner over vendored `WebCryptoAPI/` and `streams/` slices,
shipped and updated with the module (JA9 table).

## 13. Stage N7 — Closure

### Tasks

- [ ] **N7.1** `node-core` flips dynamic (the largest module; every earlier
  stage's parity machinery exists by now). The static registration path for
  node modules is then removed entirely (template H10 discipline: static was
  a checkpoint, not a product form — JA2).
- [ ] **N7.2** Packaging: `package-standard` adds `node-core` (module, not
  just manifest) + manifest-only descriptors for the leaves; `package-jube`
  (full) adds all node + web modules; a new minimal-profile packaging target
  ships host only. `verify-jube-package` (`Makefile:810`) extends to: host
  byte-identity across all three bundles; standard-bundle smoke (Node script
  using fs/path via node-core… noting leaves absent → JN5 errors); full
  smoke; minimal smoke (`typeof Buffer === 'undefined'`, non-Node JS green).
- [ ] **N7.3** Docs: `doc/Lambda_Jube_Runtime.md` gains the node-module
  bundle section; `vibe/Lambda_Design_Jube_Node_Hosting.md` status flips to
  implemented-with-deltas; the native-module doc's POC 2 marked delivered;
  memory/ledger docs updated.
- [ ] **N7.4** Checker closure: transitional allowlist burned to the
  permanent set (each survivor named and justified in the checker source);
  `--require-module-binary` mode (nm import audit) mandatory for all node
  module dylibs in CI — do not inherit Python's open H8 laxity (design §4.5
  caveat; shared burn-down with JA open item 5).
- [ ] **N7.5** Perf closeout vs N0 evidence (§18): require microbench,
  node-baseline wall time, host + bundle sizes, minimal-profile startup,
  http/fs throughput spot-checks. Any significant regression stops closure
  (template §20.3 rule).

### Exit gate — definition of done (superset of design §11 exit criteria)

- memcmp chains and all four lists deleted; resolution is registry-only.
- Every node module loadable dynamically; `crypto`/`tls`/`http` optional at
  runtime with clean errors; minimal host runs non-Node JS suites.
- node-baseline ≥ N0 ledger; Lambda/Test262/MIR budgets green; forced-GC
  gates green per module; checker green with final allowlist.
- Byte-identical host across minimal/standard/full; release perf within
  noise of N0.

## 14. The service surface — v1 worksheet (JN7/JN8)

Signed off in N2.1; this section then becomes the record of the landed v1.
House rules: `api_version` + `struct_size` first on every table; status-code
returns; pending exceptions; borrowed Items rooted before allocating ops;
completion payloads are op-specific C structs valid only during the callback;
Node semantics (fs.Stats shaping, Buffer construction, error objects) stay
module-side.

```c
struct JubeHostNodeAPI {
    uint32_t api_version; uint32_t struct_size; uint64_t capabilities;
    const JubeHostAsyncAPI*     async_ops;   // Tier 1 + Tier 2
    const JubeHostBinaryAPI*    binary;
    const JubeHostPromiseAPI*   promise;
    const JubeHostNodeErrorAPI* error;
};
```

**Tier 1 — micro-kernel (~15 entries, five concepts; sufficient for
node-zlib, node-fs (minus watch), dns, node-crypto):**

| Concept | Entries | Host backing (verified precedent) |
|---|---|---|
| scheduling | `next_tick`, `enqueue_microtask`, `timer_start`, `timer_clear` | `js_next_tick_enqueue` (`js_event_loop.cpp:163`), microtask queue, existing timer wheel |
| completion post | `post_completion(cb, user)` — any thread → JS thread, loop order, host drains nextTick/microtasks per batch | `uv_async_t` (`concurrency.cpp:580` pattern) + the `lambda_uv_set_microtask_drain` drain path (`js_event_loop.cpp:1720`) |
| blocking work | `work_submit(fn, complete, user)`, `work_cancel` | `uv_queue_work` (`network_thread_pool.cpp`, `js_fetch.cpp:546` patterns); pool sizing policy + long-op hygiene rule documented here at N2 (risk 8) |
| resource table | `rid_add(ptr, close_fn)`, `rid_get`, `rid_close`, `rid_ref`, `rid_unref` | new `js_resource_table.cpp`; slot+generation uint32; ref/unref → `uv_ref`/`uv_unref` liveness |
| lifecycle | `register_shutdown` | ordered host list; replaces the shutdown backward call |

**Tier 2 — dedicated ops, host-owned machinery only (~15–18 entries):**

| Family | Ops (sketch) | Why not the work pool |
|---|---|---|
| stream (tcp+pipe) | `stream_connect` / `listen` / `accept` → rid; `read_start` / `read_stop`; `write` (copy-on-submit); `shutdown`; `opt_set` (nodelay/keepalive); `fd_adopt`; `ipc_send_handle` / `ipc_receive_handle` → rid | readiness-vs-IOCP portability; per-connection blocking reads starve the pool |
| process | `proc_spawn(spec{argv,env,cwd,stdio rids,ipc}) → rid`; `proc_kill`; exit completion | single SIGCHLD owner |
| signal | `signal_start` / `signal_stop` → rid | single handler owner |
| fs events (pending N2.1 decision) | `fs_event_start` / `fs_event_stop` → rid | kqueue/FSEvents/inotify/RDCW machinery |

v1 contracts (design §9.1, restated as implementable rules): write payloads
copied at submit (pinned zero-copy = marked additive extension, adopted only
on N6a profile evidence); completion callbacks may submit but never block or
nest a drain; `rid_close` cancels in-flight requests → canceled-status
completions; no loop pointer, no `uv_*` type, no escape hatch — ever.

`JubeHostBinaryAPI` (buffer_alloc/from_copy/bytes, arraybuffer_alloc,
typed_array_view, is_buffer; marked extension: `buffer_adopt`),
`JubeHostPromiseAPI` (promise_new withResolvers-shape, resolve, reject,
is_promise), and `JubeHostNodeErrorAPI` (error_with_code, errno_error —
implementation host-side per N2.9, throw via `script->throw_value`) land as
sketched in design §9.2–9.4 with signatures frozen at N2.1.

## 15. Rooting conversion guide (JN9)

Landed as the reviewed reference in N2.6; every conversion is reviewed per
site — this population is exactly where unrooted `Item` locals across
allocating calls were found (107/244 scripts diverging under forced GC).

| Today | Becomes | Notes |
|---|---|---|
| file-local `extern heap_register_gc_root(_range)` (js_net.cpp:34, js_dns.cpp:28, js_os.cpp:21, js_tls.cpp:33, js_child_process.cpp:54, js_fs.cpp:1989 in-function) | `host->gc->register_root` per slot, or rid-table ownership when the value's lifetime is the resource's | delete the extern; range registration of raw arrays (js_net.cpp:120) is replaced by rid entries + per-slot roots |
| `RootFrame`/`Rooted` stack scopes (js_fs.cpp:2529, js_http.cpp:5955, js_child_process.cpp:3731) | `gc->root_frame_begin` / `root_frame_take_slot` / `root_frame_end` (flavor per N2.9) | slot counts audited against the live values, not copied |
| `js_heap_epoch`-keyed namespace caches (js_runtime.cpp:39204 pattern) | descriptor `runtime_reset` (drop caches) + `heap_cleanup` (per-heap teardown) | JN11; registry already drives these hooks |
| direct `Context`/heap field access (js_net.cpp:4591) | eliminate; needed values arrive via host APIs | checker-banned |
| `heap_calloc`/`heap_create_name`/`s2it` inline uses | allowed transitionally via the counted allowlist; burn down toward value/script/data tables | full elimination is N7.4's allowlist closure, not a per-stage blocker |

Review rule per converted function: every `Item` local that lives across any
allocating or re-entrant call is in a root slot; every completion callback
re-roots what it holds before building Items. Gate: the §17.3 forced-GC run
on the module's slice, plus goldens byte-identical.

## 16. Architecture checker spec (`check_node_module_architecture.py`)

Model: the hosted-Python checker (path-anchored source rules; binary `nm`
mode; self-test with synthetic violations; "checks land with their owning
stage" philosophy — see its header comment).

1. **Scope:** `lambda/module/node_*/**` and `lambda/module/web_*/**` sources
   and, in binary mode, the built dylibs.
2. **Banned (source + binary):** `uv.h`/`uv_*` symbols; `js_runtime.h`,
   `js_runtime_state.hpp`, `Context` layout access; `heap_register_gc_root*`
   externs; engine `RootFrame`/`Rooted` types; `js_heap_epoch`;
   `dlopen`/`dlsym`; direct POSIX socket/spawn calls in files whose ops are
   Tier-2-owned (per-module rule rows); cross-module symbol imports.
3. **Allowed:** `jube.h`; libc/platform calls in `moves-with-file` classes
   (tty ioctl, sync fs syscalls, mbedTLS in tls/crypto, zlib in node_zlib);
   module-local symbols; a **counted transitional allowlist** seeded from the
   N0.3 inventory, shrinking monotonically — a new entry fails review.
4. **Binary mode:** dynamic import table ⊆ allowlisted host exports (the
   Jube surface) + platform libs; runs with `--require-module-binary` from
   the first dynamic flip (N4) and is mandatory in CI at N7.4.
5. **Self-test:** injects a `uv_tcp_init` call, a `js_runtime_state.hpp`
   include, a `heap_register_gc_root` extern, and a fresh allowlist entry
   into scratch copies and must see all four rejected.

## 17. Test and verification matrix

### 17.1 Required on every stage

```sh
make build
make test-lambda-baseline        # includes test/node in-tree corpus + MIR ratchet
make test262-baseline            # zero failed, zero retries
make node-baseline               # locked official set, no regressions
make check-node-module-architecture
git diff --check
```

### 17.2 Per converted module (N3 onward)

- Its node-baseline slice + in-tree corpus slice in **static** mode, then
  **dynamic** mode (isolated bundle copy via `JUBE_MODULE_PATH`,
  `Makefile:822` pattern), diffed against each other and the pre-move run.
- Absent-module negatives (missing dylib, tampered library, wrong ABI/build
  id, missing dependency → rollback observed) through the parameterized
  loader-negative matrix.
- Descriptor negatives for any new table the module consumes (undersized,
  version-mismatch — clean refusal before `init`).

### 17.3 Forced-GC gate

Per converted file batch, before its dynamic flip:

```sh
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 ./lambda.exe <script> → diff golden
```

over (a) the module's in-tree `test/node` scripts and (b) a named official
subset (the full official suite under FORCE_EVERY=1 is impractically slow —
the subset is committed per module in `test/benchmark/hosted_node/`, chosen
from the module's baseline slice). New make target `node-gc-gate` wraps this
(`test-gc-rooting-python` at `Makefile:1096` is the template; corpus-sweep
precedent `test-mir-gc-stress` `:1091`).

### 17.4 MIR budgets

`test/mir` ratchet green at every stage; any diff in emission for existing
scripts is a defect of the rewiring (the resolution changes are runtime-side
by construction — the lowering intercepts still emit identical calls).

### 17.5 Sanitizers / stress

Reuse the project's sanitizer configuration for: rid-table
lifetime/generation reuse, completion-after-close, work-pool
submit/cancel races, repeated `runtime_reset`/`heap_cleanup` cycles,
shutdown with active rids, module init failure rollback. No leak,
use-after-free, stale rid acceptance, or unbalanced root frame.

## 18. Release performance protocol

Method verbatim from the Python plan §20.2 (release builds, ≥7 reps,
medians + dispersion, alternate before/after, archive raw samples in
`test/benchmark/hosted_node/`). Protected measurements:

- require-latency + namespace-build microbench (N0.2) — the registry path
  must beat or match the memcmp chain;
- node-baseline wall time;
- Lambda and non-Node JS startup (minimal profile) — must show zero node
  cost when absent;
- host and bundle sizes per stage (host shrinks as leaves detach — record,
  don't gate);
- N6a http and N5a fs throughput spot-checks vs N0 (the copy-on-submit
  decision input).

Acceptance: no statistically significant regression on any protected
measurement; a regression stops the stage pending root cause (never absorbed
as "migration overhead").

## 19. Review checkpoints requiring explicit attention

Implementation pauses for review if any of these becomes necessary:

1. any new op or table entry without a named call site (P5 violation);
2. a module needing a symbol outside `jube.h` + its platform allowance
   (the escape-hatch moment — JN8's named anti-pattern);
3. adding fields/branches to shared engine records or hot paths for Node;
4. a behavior difference surfacing between static and dynamic modes;
5. weakening a name-list deletion (re-adding engine builtin knowledge);
6. any completion delivered off the JS thread or outside loop order;
7. a rooting conversion that cannot be expressed with existing GC/Root
   tables (would imply a new GC API — separate design);
8. baseline growth work colliding with a module move (stream risk) —
   resequence rather than merge;
9. the WPT harness decision being pre-empted by module code (N6c/N6d wait);
10. any manifest/ABI field changing meaning rather than appending (JA11).

## 20. Risk register

| # | Risk (design §13 ↔ here) | Stage | Mitigation/gate |
|---|---|---|---|
| 1 | stream is huge and load-bearing; conflicts with coverage campaigns | N3.2 | move stream last within N3; resequence on conflict (checkpoint 8) |
| 2 | crypto/WebCrypto split is surgery in an 8.7k file | N6c | three sub-tasks with the shared-primitives layer first; re-enumeration at sub-stage start |
| 3 | process split hides engine deps on Node-only members | N3.3 | committed enumeration artifact; shim audit; minimal-profile smoke |
| 4 | console degradation without node-core | N3.4 | absent-default formatter written + tested in N3, not later |
| 5 | npm resolver placement | post-N5 | explicitly deferred; revisit note in N7.3 docs |
| 6 | `node:vm` stays host | — | documented exception (JN6); no work here |
| 7 | Windows loader parity | N4+ | keep export surface = Jube tables only (JN8 helps); Windows CI is out of scope but the export audit (§16.4) keeps the door open |
| 8 | request-API coverage/cost: fs.watch, IPC fd passing, fd adoption; pool starvation; copy-on-submit cost | N2.1/N5/N6a | edge inventory before v1 freeze; pool sizing + hygiene rule documented; zero-copy only on N6a profile evidence |
| 9 | worker_threads futures vs process-lifetime modules | — | note in N7.3; `runtime_reset`/`heap_cleanup` likely suffice (verify then) |
| 10 | Python H8 laxity inherited | N2.8/N7.4 | binary-mode checker mandatory from first dynamic flip; shared burn-down with JA open item 5 |
| 11 | zlib/mbedTLS host-link entanglement blocks "drops out of host" claims | N0.5/N4/N6 | audit first; claims scoped to audit results |
| 12 | official-suite forced-GC runtime | N3+ | committed per-module subsets (§17.3), not full-suite sweeps |
| 13 | eight hand-edited build targets/manifests drift | N4.2 | one generator template for module targets + manifests (rule 13) |

## 21. Recommended landing series (review units)

1. N0 tooling: evidence dir + inventory generator + checker + self-test;
2. N1 manifest fields + specifier index (no consumer);
3. N1 registry-first `js_module_get_builtin` + compile-time consumers;
4. N1 `path` module + row deletions;
5. N2 ABI tables in `jube.h` + host impls (async/rid/binary/promise/error),
   unit + negative tests;
6. N2 fs pilot conversion (+ rooting guide application);
7. N3 per-file moves (one unit per file/pair, leaves → hub → stream);
8. N3 hook inversions + lazy globals;
9. N3 chain/list deletion + `builtinModules` from index;
10. N4 module-target generator + node-zlib static;
11. N4 node-zlib dynamic + negatives;
12–14. N5a/N5b/N5c (each: convert → parity → flip);
15–17. N6a/N6b/N6c(1–2);
18. N6c(3)/N6d when WPT harness lands;
19. N7 node-core flip + packaging + verify extension;
20. N7 docs + allowlist closure + perf closeout.

Each unit carries its focused tests, checker delta, and the §17.1 gates;
units touching shared engine paths carry release perf evidence.

## Appendix A — `module.json` template (node-zlib shown)

```json
{
  "name": "node-zlib",
  "version": "0.1.0",
  "base_abi_version": 2,
  "hosted_api_version": 1,
  "host_build_id": "<stamped>",
  "kind": "runtime-library",
  "engine": "js",
  "provides": ["zlib"],
  "dependencies": ["node-core"],
  "resources": [],
  "library_macos": "node-zlib.dylib",
  "library_linux": "node-zlib.so",
  "library_windows": "node-zlib.dll",
  "entry_symbol": "jube_module",
  "sha256_macos": "<stamped>"
}
```

Notes: `provides` lists canonical (normalized) specifiers; prefixed-only
surfaces (`node:test`) are written prefixed. `host_build_id`/`sha256_*` are
stamped by `utils/update_jube_manifest_integrity.py`, never hand-edited.

## Appendix B — name-list deletion checklist (all gone by N3.6)

| List | Anchor (2026-07-26) | Deleted at |
|---|---|---|
| `js_module_get_builtin` memcmp chain + inline externs | `js_runtime.cpp:38891` | rows per module N1–N3; body N3.6 |
| `builtinModules` for `node:module` | `js_runtime.cpp:39237` | index-derived from N1.4; residual list gone N3.6 |
| `js_module_is_builtin` | `js_runtime.cpp:39856` | thin index query from N1.4; list gone N3.6 |
| batch-lowering `builtin_names[]` | `js_mir_module_batch_lowering.cpp:1931` | index-first from N1.4; list gone N3.6 |
| `node:` prefix special cases | `js_mir_entrypoints_require.cpp:1581`; `js_mir_module_batch_lowering.cpp` | N3.6 (normalization owns aliasing) |

## Appendix C — relationship to prior decisions

| Prior decision | Effect here |
|---|---|
| Design JN1–JN13, §11 N0–N7 | executed as written; this plan adds only sequencing detail, verified anchors, and tooling |
| JA1/JA3 supersessions (JN14, `stream/web`) | folded as N3.2 split + N6c three-way split + N6d, with the §13.4 transitional rule |
| JA9 gates | node modules: node-baseline; web modules: WPT slices (blocking N6c(3)/N6d only) |
| JA16 | the banned `uv_*` checker list is its first enforcement instance; §14 op families feed its future surface |
| CLAUDE rules 13/14/15 | one generator for targets/manifests; no C2MIR work; precise rooting only, forced-GC gated |
| Hosted-Python plan H0–H10 | stage anatomy, checkpoint discipline, checker/self-test pattern, packaging identity checks, perf protocol reused; compiler-service stages replaced by N2 |
