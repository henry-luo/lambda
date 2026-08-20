# Jube Hosted Node — Detailed Implementation Plan

> **Status:** implementation underway. N0 architecture tooling and the N1
> registry/catalog foundation are landed; N2 has the additive requirements and
> session lifecycle boundary plus complete static `node-core/path` and
> `node-core/string_decoder` and `node-core/querystring` moves. The
> focused locked Node path slice (13/13) and forced-GC smoke now pass through
> the Jube boundary, including `matchesGlob`, Win32 parsing, and namespace
> cache lifetime. `string_decoder` now consumes only Jube value, binary, root,
> and session services; its bare/node aliases and forced-GC namespace-cache
> lifetime probe pass. `querystring` now relies on opaque string byte access,
> host coercion/object creation, and session roots; normal and forced-GC
> parsing/stringifying coverage passes through the registry.
> `node-core/os` is also now statically registered through the same boundary,
> including its explicit compatibility global and session-owned namespace
> cache; its focused normal and forced-GC gates pass. Its locked upstream slice
> still exposes the legacy native-function string-coercion incompatibility
> (the old `js_os_set_method` used the identical constructor), so that baseline
> issue remains isolated rather than being attributed to the Jube move.
> `node-core/url` now uses only opaque Jube value/script/root services,
> session-owned persistent Blob URL roots, and registry-mediated Buffer
> resolution; its focused normal and forced-GC tests pass. Legacy `Url`
> construction and `url.parse(..., true)` now build the Node-compatible fixed
> field shape and null-prototype query dictionary entirely through those
> services; legacy rooted/non-rooted relative path resolution is also covered
> by the forced-GC leaf gate. The locked URL slice still contains
> runtime-semantics gaps (for
> example, a Symbol argument reaches native-call ABI as a number and NaN
> strict-assert behavior), tracked separately from the move.
> `node-core/events` is now also registry-owned and uses explicit Jube event
> services for async-local context, domains, process warnings, closures,
> prototypes, and promise capabilities; normal registry coverage and selected
> upstream EventEmitter contracts pass. Its listener-registration and callback
> path now pass the forced-GC/poisoned-freed sweep; the move also fixed the
> host direct-array append invariant that had left a relocated destination
> array live across growth. Buffer migration preparation is also landed: raw
> transport-to-Buffer construction now belongs to the host typed-array layer,
> and the additive binary service exposes Buffer allocation, writable byte
> access, and Buffer identity without making host transports depend on the
> Buffer namespace. The Buffer namespace and its coercion/ArrayBuffer/BigInt
> implementation remain host-resident until that full Jube conversion is
> complete. The service-table changes bumped the exact-build contract to
> `lambda-hosted-lang-20260726-h9b24` and
> regenerated the dynamic `lang-python` manifest so stale module images are
> rejected before initialization. The resolver-only `node-core/punycode`
> compatibility stub is now also a rooted Jube namespace with bare, `.js`,
> and `node:` resolution covered under normal and forced-GC execution.
> `node:console` and `node:process` now resolve through explicit node-core
> descriptors to their existing session globals, preserving object identity
> while removing their legacy dispatcher branches. The Buffer service now also
> has a validated opaque byte-view descriptor for ArrayBuffer, typed-array,
> and DataView inputs plus a shared-storage Buffer-view constructor; detached
> and out-of-bounds states remain host-validated. This is preparation for the
> Buffer implementation move, not a claim that the namespace is migrated.
> The N3.3 process inventory is now committed under
> `test/benchmark/hosted_node/`: host-core retains startup, queue, lifecycle,
> signal, IPC, and resource-table surfaces, while `node-core` installs the
> self-contained `memoryUsage`, `cpuUsage`, memory-capacity, `umask`,
> `setSourceMapsEnabled`, `cwd`/`chdir`/`uptime`/`hrtime`/`abort`/`kill`, POSIX credential methods, active resource/handle queries, capture-callback state, and read-only POSIX identity
> platform queries during
> `runtime_attach`. These moved extensions no longer exist in `js_globals.cpp`;
> the live process object is rooted while their names and callback wrappers are
> allocated. `_getActiveHandles` now reads the session-owned transitional
> resource table: node-net has moved its socket/server GC roots and inventory
> out of raw global arrays into session-owned, slot-and-generation `uint32_t`
> rids. Server and Socket construction is now rooted across forced collections
> until that table owns the JS edge. Tier-2 stream operations and the actual
> node-net module extraction remain the next N5b extension.
> The first node-net ownership slice is now registered as its own Jube module:
> `net`, `internal/net`, `internal/js_stream_socket`, `dns`, and
> `dns/promises` no longer belong to node-core's descriptor table. The module
> presently delegates namespace implementation to the host while stream and
> DNS work services are extracted; static and isolated dynamic parity cover
> that boundary. `net.isIP`, `net.isIPv4`, and `net.isIPv6` are the first
> concrete node-net exports: they now parse through the module's platform-only
> implementation and opaque Jube value/script/root services under normal and
> forced-GC static/dynamic gates. `net` now also owns the public default
> auto-select-family policy getters/setters; a narrow host network-policy
> service keeps the connection scheduler's state host-side while the module
> validates Node's boolean and positive-timeout contract. The pure
> `internal/net` namespace and `net._normalizeArgs` now build in node-net as
> well, preserving the host parser's normalized-args marker. `dns.lookupSync`
> is the first DNS operation with a module-local platform implementation
> (`getaddrinfo` + `inet_ntop`); its host namespace registration is gone and
> normal/forced-GC static/dynamic coverage exercises the moved paths.
> The network service now also owns the permission decision and non-throwing
> `ERR_ACCESS_DENIED` construction needed by the forthcoming `work_submit`
> DNS lifecycle, so a module resolver cannot bypass policy through libc.
> `node:timers` and `node:timers/promises` are now rooted node-core leaves as
> well: a narrow additive timer service leaves event-loop ownership in the host
> while the Jube facades supply classic timer/cancellation methods, promise
> timers, scheduler helpers, default identity, and the
> `require('timers').promises` bridge. Their regular and forced-GC gate passes.
> Node-core registration is deliberately performed by the full static
> executable after it selects the module profile, rather than by the shared
> registry library, so source-closure validation DSOs do not gain an invalid
> dependency on optional node-core objects.
> `node:constants` is now a rooted Jube leaf too. It builds the public,
> null-prototype frozen constant object through opaque value services, resolving
> only the still-host-resident `fs` source through the explicit host-namespace
> bridge while it awaits the fs extraction. Bare/node identity, exported
> constants, immutability, and forced-GC behavior are covered.
> `node:v8` is also a rooted Jube leaf, including its compatibility
> `promiseHooks`, heap-statistics, startup-snapshot, and default surfaces.
> The bounded `node:perf_hooks` compatibility namespace now follows it through
> the same registry path: it exposes the host-owned global `performance` and
> `PerformanceObserver` only through opaque Jube services, while keeping its
> Node-only histogram/entry constructors module-owned. Bare/node identity and
> static, forced-GC, and isolated dynamic-image probes are covered.
> `node:worker_threads` is likewise now a rooted node-core leaf. Its namespace
> and constructor exports are module-owned; host-owned MessagePort queues and
> transfer invariants cross the boundary only through the additive worker
> service table. Normal MessagePort lifecycle plus static/dynamic forced-GC
> namespace probes pass. This table tail advances the exact host build
> contract to `lambda-hosted-lang-20260727-h9b25`.
> `node:tty` is now a rooted node-core leaf too: its exports and constructors
> are module-owned, while its temporary `net.Socket` inheritance uses the
> explicit host-namespace bridge until node-net is extracted. Its regular and
> forced-GC static/dynamic registry probes pass. That probe also repaired the
> host net cache's constructor-graph rooting invariant, so `net.Socket` and
> both tty inheritance links now survive poisoned forced collection pending
> the N5 ownership move.
> The runtime-owned `node:module`, `node:vm`, `node:async_hooks`, and
> `node:trace_events` public specifiers now also route through explicit
> node-core namespace definitions. Their implementations remain host services
> where they own require/cache lifecycle, execution contexts, async-resource
> state, or runtime-global tracing. Static and isolated dynamic-image probes
> cover bare/node identity and forced-GC construction. The async-hooks probe
> additionally fixed constructor-graph rooting: both exported class/prototype
> graphs remain rooted until the namespace owns them.
> Host-runtime hubs with process/event-loop ownership now also use the same
> explicit node-core adapter pattern for `node:domain`, `node:cluster`,
> `node:readline`, `node:readline/promises`, and prefix-only `node:test`.
> The registry deliberately preserves `node:test` as prefix-only, so an
> unrelated bare `test` package is never promoted to a builtin. Each adapter
> has static/dynamic bare-or-node identity coverage and a poisoned forced-GC
> probe.
> The current reduced-node/minimal package verifier is green on a release
> build: the reduced profile resolves node-core's `path`, while the same host
> binary in the minimal profile runs plain JS and reports `MODULE_NOT_FOUND`
> for `path`.
> The isolated dynamic node-core parity gate now stages the separately owned
> `node-fs` and `node-net` images as well: host-namespace parity exercises
> `fs`, `net`, and DNS, and allowing those lookups to fall back to static
> providers would hide a broken dynamic dependency closure. The standard
> package now ships node-net and verifies net/DNS loading; the reduced profile
> omits it and proves `MODULE_NOT_FOUND`.
> The static executable target now declares the complete node-core source set
> explicitly. This keeps the separately generated test/release launcher from
> losing moved providers that remain referenced by host JS and Radiant code.
> `Buffer` has now joined `os` as an explicit node-core global definition:
> the active compatibility profile observes the same `Buffer` identity as
> `require('buffer').Buffer`, while an empty module-set forced through
> `JUBE_MODULE_PATH` exposes neither `Buffer`, `path`, nor `os`.
> The `buffer` and `node:buffer` specifiers now route through the same
> node-core descriptor (with the typed-array implementation still intentionally
> host-resident for this staged move), and static/dynamic poisoned-GC probes
> cover the bare/node/global identity invariant.
> `util`, its legacy `sys` alias, `util/types`, and the Browserify-era
> `inherits` entry point now take the explicit node-core descriptor path as
> well. Their forced-GC probe found and repaired a host Date-construction
> invariant: the new object, constructor, and prototype must remain rooted
> while property-key allocation and prototype installation compact the heap.
> `assert` and `assert/strict` now use the same descriptor path. Their
> forced-GC test repaired the strict-instance construction invariant: the
> namespace is permanently rooted, and fresh strict method wrappers remain
> rooted until the instance publishes them.
> The public `stream`, `stream/promises`, `stream/web`, `stream/consumers`,
> and `stream/iter` specifiers now also enter through explicit node-core
> descriptors while the large Node/WHATWG implementation split remains open.
> Their static and isolated dynamic probes uncovered and fixed the shared
> stream namespace cache invariant: each cached namespace must be registered
> as a GC root before a sibling surface can allocate.
> The host-owned `repl` implementation is likewise now reached only through
> a node-core descriptor; its regular and forced-GC static/dynamic probes
> cover its constructor, modes, and default namespace identity.
> `diagnostics_channel` is now registry-owned as well. Its forced-GC probe
> found the channel-construction invariant: both a new channel and its name
> must stay rooted while property keys and callbacks are allocated before the
> persistent channel cache publishes them.
> N4's first dynamic delivery proof is now present as `node-zlib`: its own
> Jube image, dependency on dynamically activated `node-core`,
> and isolated normal/forced-GC gzip round-trip gate are all verified. The
> raw gzip/gunzip, deflate/inflate, raw-deflate/raw-inflate, and auto-detect
> unzip codecs now remain in the static host as one provider; the node-zlib
> image owns the namespace, all seven synchronous and callback wrappers, and
> the stateful stream facade through the Jube binary, codec, and next-tick
> services. The image links neither zlib nor a raw host symbol; only the
> versioned codec/state provider and generic Transform lifecycle remain in the
> host.
> The Node module build contract is now generated from one JSON template with
> explicit `node-core` and `node-zlib` instances, avoiding per-module linker
> flag drift. The shared loader-negative harness also covers `node-zlib`'s
> missing-image, tampered-image, and wrong-base-ABI cases, including the
> registry diagnostic naming the unavailable module.
> A two-artifact zlib parity gate now compares the host-resident static
> checkpoint against copied dynamic `node-core` + `node-zlib` images under
> normal and poisoned forced-GC execution. It establishes the comparison
> harness required for the later source-removal flip; it does not claim that
> host source has already been removed.
> The reduced-node and minimal package profiles now ship only the `node-zlib`
> manifest (never its image), so both produce `MODULE_NOT_FOUND` instead of
> reviving the legacy host implementation. The compatibility package remains
> the profile that carries the verified dynamic image.
> N0.5's link audit confirms zlib must remain in the host independently of the
> Node surface: PDF post-processing/decompression, npm tarball extraction, and
> font decompression include it. mbedTLS likewise remains host-linked for the
> serve TLS handler and the existing JS TLS/crypto and digest paths. N4/N6 may
> remove those libraries from a Node module's implementation boundary, but
> cannot claim that either dependency drops from the host binary.
> `node-zlib` now owns the complete observable namespace: `crc32` and the seven
> synchronous/callback compression operations validate Node inputs in the
> dynamic image, while stateful constructors use the host's opaque codec and
> generic Transform services. It reads strings and binary views only through
> Jube value/binary tables, preserves unsigned seed validation through the
> Node error table, and caches namespace/prototype roots per session. The
> static checkpoint and hosted image use the same host primitive, so there is
> no divergent duplicate implementation.
> The `crc32` boundary now also covers DataView under poisoned forced-GC in
> both the static checkpoint and dynamic image. That probe repaired the shared
> runtime ownership invariant: a native DataView must trace its backing
> ArrayBuffer, and a typed-array view must root its backing Item and ArrayNum
> descriptor until its wrapper map owns those edges. Generic property reads
> likewise root a receiver while an inherited accessor can allocate. The prior
> host build stamp `lambda-hosted-lang-20260727-h9b35` covered the added opaque
> string-byte, coded-range-error, Node-compatible zlib-error, and next-tick
> callback-post service tails, decimal-to-BigInt construction for the
> node-core `hrtime.bigint` migration, Node-shaped platform syscall errors,
> generic coded compatibility errors, and session-scoped active-resource/handle inventory.
> N3.4 now has its first host hook inversion: the cluster-worker disconnect
> path invokes a typed, absent-default shutdown registry instead of importing
> node-net's server-close symbol; net registers the participant only while its
> namespace is live and clears it on reset. Static and isolated dynamic
> node-core gates cover the transition.
> The same absent-default registry now owns process-IPC TCP-handle acceptance:
> `js_globals.cpp` no longer imports net directly, while host net registers its
> opaque pipe adapter until the node-child-process/rid migration replaces it.
> Cluster's online-event queue now follows the same inversion: runtime cluster
> code emits through an absent-default child-process hook, and `js_cp_fork`
> installs that hook before returning the child so `worker.on('online')` keeps
> its queued next-turn behavior. Reset clears the hook; static and isolated
> dynamic node-core fork probes cover the ordering.
> Console formatting is now the third N3.4 absent-default seam: node-core
> activation resolves the host util namespace only to register its formatter,
> so active Node profiles preserve `%` substitution while a profile-less
> console deliberately uses generic rendering. Reset clears the hook; static,
> dynamic-after-`require('console')`, and minimal-profile probes cover both
> sides without activating a dynamic module merely because `console` exists.
> The active-resource inventory now follows the same absent-default pattern:
> the registry no longer imports node-net directly, and host net registers its
> resource/handle callbacks only while its namespace is live. The empty default
> preserves process inventory behavior before net activation while the rid
> table supersedes net's current rooted arrays.
> The legacy Node builtin dispatcher is now catalog-first for every node-core
> surface: fs, child-process, crypto, DNS, net/TLS/HTTP, and the internal
> compatibility shims are explicit node-core descriptors whose implementations
> remain behind one typed host-namespace table. `module.builtinModules`,
> `module.isBuiltin`, and static-literal module lowering now consult that same
> normalized Jube index; their duplicated name lists are gone. Static and
> isolated dynamic-image normal/forced-GC probes cover every transitional
> descriptor. The sole temporary dispatcher fallback is `zlib`, retained only
> for N4's intentionally catalog-free static parity checkpoint; N4.4 removes
> it together with the host zlib source.
> The v8 static and isolated dynamic-module probes run under forced GC. Those
> probes exposed and fixed three host ownership invariants shared by all Node
> leaves: `Object.keys` roots both collection arrays across key allocation,
> typed-array construction roots its backing ArrayBuffer and ArrayNum view
> until the owning map is installed, and every platform's `os.cpus()` graph is
> assembled through rooted Jube values. Buffer construction, querystring,
> legacy URL parsing, and the node-core leaf suite now pass the poisoned
> forced-GC gate rather than relying on stale pointer luck.
> The current static node-core descriptor also builds as a separately verified
> `modules/node-core/node-core.dylib`: an isolated no-module-set root activates
> that image through its manifest under normal and forced-GC execution, while
> its unresolved import table has no direct JS-runtime or static-registration
> symbols. Late dynamic activation now attaches the image to the already-live
> runtime session and publishes its explicit globals through rooted registry
> helpers, rather than accidentally falling back to the static descriptor.
> This is the static-to-dynamic parity foundation; packaging/profile ownership
> remains to be completed before the final N7 flip.
> Reduced/minimal release-profile smokes also prove the same
> host binary activates `node-core` only when its module set is present; `vm`
> is now an explicit active-profile global alongside `Buffer` and `os`, so
> the minimal profile exposes none of those Node globals. N2's
> async work-table and the fs callback pilot's session-bound persistent roots
> are now landed. `fs.readFile` and `fs.writeFile` now run their native
> open/stat/read-or-write sequence through that session-owned work table,
> suppressing late completion after detach; the remaining fs operations and
> general rid table remain open.
> `node-fs` now owns the public `fs` and `fs/promises` descriptors. Its
> callback `readFile`/`writeFile`/`appendFile` plus
> `watch`/`watchFile`/`unwatchFile`/`utimes`, their `fs/promises`
> counterparts, and the text `readFileSync`/write/append/exists/unlink/mkdir/
> rmdir/rename/readdir plus descriptor `fchown` plus `utimesSync`/`opendirSync`/
> `_toUnixTimestamp`, `exists` custom promisify behavior, and module-built
> `fs.constants` retain Node policy and namespace shaping in module code. The
> migrated file-content, `copyFile`, `statfs`, and simple path-operation routes (`access`,
> `chmod`, `truncate`, `rm`, `link`, `symlink`, `mkdir`, `rename`, `unlink`, `rmdir`,
> `realpath`, `mkdtemp`, `readdir`, `readlink`, `chown`, `lchown`, `lchmod`, `open`,
> `close`, `fchmod`, `fchown`, `read`, `write`, `readv`, `writev`, `stat`, `lstat`, and `fstat`) use the host filesystem provider;
> the remaining operations use only Jube permission, domain, precise-root, async-work,
> value, and promise/closure services; the promise entry points adapt that same
> completion path. Static and isolated dynamic normal/poisoned-GC probes cover
> both APIs. `fs/promises.open` now returns the module-owned branded
> `FileHandle` with `fd`, `close`, `read`, and `readFile`, plus branded, full-shape
> `Stats` for synchronous, callback, and promise stat calls. The four stream
> constructors are module-owned wrappers over the host stream-factory service
> while generic Node streams await their N3.2 extraction. The copied-image loader
> negative matrix now covers missing,
> tampered, and ABI-incompatible `node-fs` images. The permission/domain and
> branded-native-object service tails advance the exact host build contract to
> `lambda-hosted-lang-20260727-h9b40`.
> The async/fs pilot, remaining module migrations, dynamic delivery, profiles,
> and web tail remain open.
>
> **Design authority:** `vibe/Lambda_Design_Jube_Node_Hosting.md` (JN1–JN14,
> stages N0–N7) as governed by the architecture ADRs in
> `vibe/Lambda_Design_Jube_Architecture.md` (JA1–JA16). Where the two disagree,
> the ADRs win; the two supersessions that affect this plan (WebCrypto →
> `web-crypto` module; `stream/web` → `web-streams` re-export) are folded into
> the stages below rather than treated as amendments.
> N8 is the formerly deferrable web-platform tail split out of Node closure so
> the WPT dependency cannot make N7 internally contradictory.
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
every stage, and the **standard compatibility bundle** contains every
extracted module needed to remain observably identical to today's monolith.
Two explicitly reduced profiles are additional products, not substitutes for
the compatibility bundle: **reduced-node** contains `node-core` plus
manifest-only leaf descriptors, and **minimal** contains the host only. The
full Jube bundle contains the compatibility set plus optional hosted-language
and other Jube modules.

This profile definition corrects an ambiguity in JN12: manifest-only
descriptors improve diagnostics but cannot preserve the behavior of a
previously built-in `fs`, `net`, `http`, or crypto implementation. N0 updates
the governing design document with this clarification before product changes
start.

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
3. **Absent module = no module activation cost + Node-shaped failure.** A
   bundle without a node module pays no initialization, mapping, module-state
   allocation, or global-accessor installation for it; the generic registry
   may perform its one catalog-presence check on the first module request;
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
   (§17) enforces this in CI from N2 onward.
7. **Errors return, exceptions pend** (P6). No C++ unwinding across the
   boundary; `throw_value`/`check_exception`
   (`JubeHostScriptAPI`, `lambda/jube/jube.h:423`) and status codes only.
8. **Rooting is precise and host-mediated** (JN9, CLAUDE rule 15). Every
   converted file passes the forced-GC gate
   (`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, the
   `test-gc-rooting-python` pattern at `Makefile:1096`) before it may flip to
   dynamic.
9. **Release performance protocol** (§19): no statistically significant
   Lambda/JS/Node regression; measurements on `make release` builds only
   (CLAUDE rule 10).
10. **C2MIR stays frozen** (CLAUDE rule 14). No `transpile.cpp` work; MIR
    Direct only.
11. **One host, byte-identical across bundles** (JA4). `verify-jube-package`'s
    `cmp -s` identity check (`Makefile:810`) extends to the node bundles.
12. **Finalizer rules** (P8, native-module design §6.3): `heap_cleanup`, vmap
    `destroy`, and anything on the marking path never allocate and never
    re-enter script.
13. **No cross-module symbol imports** (JA8). Manifest `dependencies` establish
    activation order only. Inter-module behavior uses the host-mediated
    namespace resolver or a named host operation; never a dylib-to-dylib
    symbol.
14. **Static host dependency ownership.** Lambda/Radiant retain their built-in
   filesystem, zlib, HTTP, TLS, and crypto libraries as static host links. A
   Node image may not link, embed, or import libz, libuv, curl, mbedTLS,
   OpenSSL, or another host dependency. It owns Node-facing semantics only and
   reaches the implementation through a size-gated Jube provider table. The
   checker and generated-build validation enforce this from N4 onward.
15. **Additive-only ABI evolution** (JA11): new tables/fields append behind
   `struct_size` gates, following the `JubeHostAPI.data` tail precedent
   (`jube.h:976`).
16. House rules apply throughout: `./lib` containers not `std::` (rule 3),
    `log_*` not printf (rule 4), no `/tmp` (rule 2), root-cause comments at
    fix points (rule 12), no code duplication — the per-module build targets
    and manifests are generated from one template, not copy-edited (rule 13).

## 3. Scope and non-goals

### 3.1 In scope

- Manifest `kind`/`engine`/`provides` support, a two-phase catalog/activation
  registry, and the **specifier index** replacing the builtin memcmp chain and
  all four name lists.
- A separate `JubeGlobalDef` descriptor and generic bundle activation list;
  namespace specifiers never implicitly become global properties.
- The `JubeHostNodeAPI` composed service tables (runtime/session, opaque roots,
  async micro-kernel + socket/process/signal tier, binary, promise,
  Node-error) and their host implementations (rid resource table, work pool,
  thread-safe completion post).
- Pre-init capability/size negotiation through an additive
  `JubeModuleRequirements` descriptor.
- Migration of the 21 builtins plus the embedded core pieces (process
  extensions, `internalBinding`, stubs, error helpers) into the module set of
  §1, static → dynamic each.
- The §10 hook inversions (console formatter, shutdown, IPC handoff, and
  profile-activated explicit globals whose objects build lazily).
- Rooting conversion of every migrated file to Jube GC services, gated by
  forced-GC sweeps.
- The `web-crypto` and `web-streams` modules to the extent the JN14/JA1
  supersessions require (three-way crypto split; `stream/web` re-export) —
  with an explicitly legal transitional state if their WPT gates are not yet
  wired (N8).
- Packaging: standard compatibility = host + every extracted replacement for
  today's monolith; full Jube = compatibility + optional Jube modules;
  reduced-node = host + `node-core` + manifest-only leaf descriptors;
  minimal = host only.
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
  `uv_*` checker list is JA16's first enforcement instance, and the §15 op
  families are input to that design, but the IO-policy layer is not built
  here.
- Sandboxing/trust-tier work (JA10 T2/T3), hot unload, Windows CI bring-up
  beyond keeping the export surface auditable (§21 risk 7).
- The WPT harness *decision* (JA9 / architecture open item 3) — N8 depends on
  it; a default is proposed there but deciding it is its own
  review.

## 4. Verified starting point (2026-07-26)

### 4.1 Infrastructure reused as-is

| Facility | Anchor | Notes |
|---|---|---|
| `JubeModuleDef` (namespaces/types/functions, `init`, `shutdown`; additive tail: `interface_decl`, `type_bindings`, `runtime_reset`, `heap_cleanup`, `language`) | `lambda/jube/jube.h:1120`; `JUBE_MODULE_DEF_V1_SIZE` | N2 appends requirements, explicit globals, and session lifecycle without changing this released prefix; `language` stays NULL (JN1) |
| `JubeNamespaceDef` = `specifiers[]` + `specifier_count` + `Item (*build)(void)` + optional `JubeFuncDef` table | `jube.h:277` | suitable only for specifiers that return the same namespace object; distinct results such as `path/win32` require distinct definitions |
| `JubeTypeBinding`/`JubeMemberBind` + `interface_decl` (DOM3 dispatch) | `jube.h:296/:310` | for class-shaped surfaces (Socket, Server, Hash, FileHandle, …) |
| `JubeHostAPI` (`hosted_language`, `gc`, `value`, `script`, `dom`, `runtime_catalog`; additive tail `data`) | `jube.h:976` | the new `node` parent appends after `data`, size-gated |
| `JubeHostGcAPI` (`register_root`, `unregister_root`, legacy concrete root frames, weak roots) | `jube.h:355` | retained for ABI compatibility; node modules do not use its concrete `LambdaRootFrame` surface |
| `JubeHostRootAPI` (opaque `JubeRootFrame`, session-bound persistent roots) | `jube.h:~370` | N2 extends it additively with unregister and exposes it through the node parent |
| `JubeHostScriptAPI` (`new_function`, `throw_value`, `check_exception`, `call_function`, `new_error_with_name`, `global_this`, …) | `jube.h:423` | value mechanics stay here (the `js_native_api.h` half) |
| Static registration + enumeration | `jube_register_static_module` `jube_registry.cpp:2957`; registration site `:3611` (radiant, hostobj_demo); `jube_static_module_count/_at` `:3619/:3623` | in-tree static module sources live under `lambda/module/<name>/` (`radiant_module.cpp:1534`, `hostobj_demo_module.cpp:266`) — node modules follow this convention |
| Manifest scan + verification | scan `jube_registry.cpp:3217–3250`, `:3516–3546`; per-OS `library_*`/`sha256_*` keys `:3303–3319`; SHA-256 verify `:3323`; fields parsed today: `language`/`aliases`/`extensions` (`:3296–3300`), `entry_symbol`, `host_build_id`, `base_abi_version`, `hosted_api_version` (`:3392–3403`), **`dependencies` (`:3414`)**, `resources` (`:3423`) | dependency loading/rollback is reused, but N1 must split today's select-and-load scan into a read-only catalog phase and a later activation phase |
| Loader + rollback | `jube_load_dynamic_module_checked` `:2963` (entry symbol default `"jube_module"`, `:2973`) | transactional registration reused unchanged |
| Module root override | `JUBE_MODULE_PATH` env (`:3573`) | used by integrity/parity tests |
| Scan-on-first-need pattern | `jube_discover_hosted_language` `:3564` | the specifier index copies this trigger discipline |
| Build-target shape | `build_lambda_config.json:305` (`lang-python`: `"link": "dynamic"`, `"pic": true`, `"target_dir": "modules/lang-python"`, `link_options_macos: ["-Wl,-undefined,dynamic_lookup"]`, explicit `source_files`) | cloned per node module (via one generator, rule 13) |
| Build/package targets | `build-lang-python` `Makefile:772`, `release-lang-python` `:783`, `package-standard` `:795` (manifest-only descriptor precedent), `package-jube` `:803`, `verify-jube-package` `:810` (shasum + `cmp -s` + absent/full smokes), `release-jube` `:849` | node analogs cloned in N4/N7 |
| Manifest stamping | `utils/update_jube_manifest_integrity.py <module dir>` | reused verbatim |
| Loader negative/integrity/dispatch matrices | `test-jube-module-loader-negative` `Makefile:836` (`utils/test_jube_module_loader_negative.py`), `test-jube-module-integrity` `:822` | parameterized/extended for node modules |
| Architecture-checker methodology | `utils/check_hosted_python_architecture.py` (path-anchored source rules + binary `nm` mode via `--require-module-binary`; "checks added only after their owning stage lands" philosophy), self-test `utils/test_hosted_python_architecture_checker.py`, `check-hosted-python-architecture` `Makefile:2362`, coupling-inventory target `:2367` | template for §17 |
| node-baseline harness | `node-baseline` `Makefile:1217` → `./test/test_node_gtest.exe --baseline-only` over `ref/node/test/parallel/` with shims from `lambda/js/test_shim/` (`node-shim` `:1154`); locked set `test/node/official_baseline.txt`; report `test/node/node_official_report.py` (`:1237`); adjacent `node-regression-gate`/`node-full`/`node-update-baseline` | the continuous gate |
| Forced-GC gate mechanics | `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` + golden diff (`test-gc-rooting-python`, `Makefile:1096`); corpus sweep exe precedent `test/test_mir_gc_stress_gtest.exe` (`test-mir-gc-stress` `:1091`) | §18.3 |
| Async substrate + precedents | loop accessor `lambda_uv_loop()` and drain hooks `lambda_uv_set_microtask_drain`/`lambda_uv_set_task_drain` (`lib/uv_loop.h`); in-tree `uv_queue_work` pools: `lambda/network/network_thread_pool.cpp`, `lambda/serve/worker_pool.cpp`, `js_fetch.cpp:546`; `uv_async_init` post: `lambda/runtime/concurrency.cpp:580` | the §15 micro-kernel is assembled from these, host-side |

### 4.2 What does not exist yet (built by this plan)

- Manifest `kind`, `engine`, `provides` parsing (confirmed absent from
  `jube_registry.cpp`), the two-index registry (language index + specifier
  index), and the catalog/activation split — N1.
- Any specifier→module mapping: today nothing connects `require("x")` to the
  Jube registry — N1.
- A distinct global descriptor: `js_install_jube_global_namespaces`
  (`lambda/js/js_globals.cpp:~74`) currently installs every namespace's first
  specifier eagerly, which would incorrectly create globals such as `path`.
  `JubeGlobalDef`, profile activation, and lazy object accessors — N2/N3.
- A per-JS-runtime session/attach lifecycle. `JubeModuleDef.init` is
  process-level and may run before a JS heap/global/process object exists; it
  cannot perform the process extension described by the original N3 draft —
  N2.
- Pre-init module requirement negotiation. The current registry invokes
  `module->init(&jube_host_api)` directly after base descriptor validation —
  N2.
- `JubeHostNodeAPI` and its six child tables, the rid resource table, the
  shared blocking-work pool as a host API, thread-safe completion post as a
  module-facing service — N2.
- `utils/check_node_module_architecture.py` + self-test — N0.
- A node coupling-inventory generator (the hosted-python one at
  `Makefile:2367` is Python-specific) — N0.
- Require-latency / namespace-build microbenches (none exist) — N0.
- WPT harness for module slices (JA9) — prerequisite for N8 only.

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
  `node_zlib_module.cpp` owns the Node-facing zlib namespace and stream
  facade; `jube_node_zlib_codec.cpp` remains the host provider;
  `js_crypto.cpp` 8,725 lines, `js_get_crypto_namespace` `:8584`.
- Globals install cluster (process/Buffer/os/vm, `ReadableStream` at
  `js_globals.cpp:16143`) — `js_globals.cpp:16009+`.
- Error-code header users: assert, dns, buffer, child_process, crypto, fs,
  globals … all include `lambda/js/js_error_codes.h`.

## 5. Stage map

| Stage | Deliverable | Primary gate | Rough size |
|---|---|---|---|
| N0 | evidence baseline, coupling inventory, checker + self-test, microbenches | tooling green; inventory fully classified | small (tooling only) |
| N1 | manifest catalog/activation split, `kind`/`provides`, specifier index in shadow mode, compile-time consumers index-first | node-baseline + MIR budgets unchanged; no dlopen from catalog queries | medium |
| N2 | runtime/session + requirement contracts; completed `jube.h` value-boundary inventory; `path` as a true static Jube module; Node async/service tables and fs pilot | `path` has no forbidden imports; pilot green under forced GC; negative descriptor tests | medium–large |
| N3 | `node-core` static; process split; hook inversions; chain + 4 lists deleted | node-baseline; chains gone; checker green on `lambda/module/node_core` | large (~30k lines move) |
| N4 | `node-zlib` first dynamic module over a host-owned codec provider; delivery chain cloned; absent-module negatives | dynamic load macOS+Linux; static/dynamic parity; no host dependency imports | small |
| N5 | `node-fs` → `node-net` → `node-child-process`, static→dynamic each | per-module: baseline, forced-GC, zero `uv_*`, negatives | large |
| N6 | `node-http` → `node-tls` → shared crypto primitives + `node-crypto`; web implementations remain behind transitional host providers | as N5 + crypto/tls/http optional at runtime | large |
| N7 | Node closure: `node-core` dynamic; four-profile packaging; docs; allowlist burn-down; perf closeout | byte-identical host ×4 bundles; compatibility and minimal profiles green; perf vs N0 | medium |
| N8 | WPT-gated `web-crypto` and `web-streams` extraction and compatibility-package update | WPT slices green; standard behavior unchanged | medium, independently gated |

```text
N0 ──► N1 catalog ──► N2 ABI/session/path ──► N3 node-core ──► N4 zlib
      └──────────────────────────────────────────────────────────► N5a fs ──► N5b net ──► N5c child_process
                                                                   └────────► N6a http ──► N6b tls ──► N6c node-crypto ──► N7 Node closure
                                                                                                                   └──────► N8 web modules
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
  (a) first `require` of each builtin in a fresh process/bundle (separating
  catalog scan, integrity/dependency activation, dynamic load, and namespace
  build where instrumentation permits) and (b) 10⁵ cached requires, run via
  the harness with 7+ repetitions; store runner + results under
  `test/benchmark/hosted_node/`. This is the perf yardstick for the registry
  rewiring (N1) and the N7 closeout.
- [ ] **N0.3** Write `utils/hosted_node_coupling_inventory.py` + make target
  `hosted-node-coupling-inventory` (pattern: `Makefile:2367`). Deterministic
  JSON records over the §4.3 population: every non-system include and every
  undefined external symbol from a per-file object build, not only the
  previously known couplings; every `uv_*` call site;
  `heap_register_gc_root(_range)` extern; `RootFrame`/`Rooted` use;
  `js_heap_epoch` read; `js_runtime_state.hpp`/`js_runtime.h` includer;
  JS-queue extern (`js_next_tick_enqueue`, `js_setTimeout`, …); the §10
  backward-call edges; and every inter-builtin extern. Each record carries a
  classification — `existing-jube-api` / `new-additive-api` /
  `microkernel-op` / `tier2-op` / `host-namespace-resolve` / `hook` /
  `rooting` / `moves-with-file` / `stays-host` / `delete` — and an owning
  stage. The generated report includes a per-module future `nm -u` allowlist.
  Zero unclassified includes or symbols is the gate (template H0.4/H0.5
  discipline).
- [ ] **N0.4** Write `utils/check_node_module_architecture.py` per §17 with
  its synthetic self-test `utils/test_node_module_architecture_checker.py`
  (the checker must prove it rejects injected violations, not merely pass on
  a clean tree — `Makefile:846` precedent). Make targets
  `check-node-module-architecture` / `test-node-module-architecture-checker`.
  Until N2 it runs in report mode against the future module dirs (empty) and
  the inventory; from N2 it enforces the allowed-import rules in item 3 of
  §17.
- [x] **N0.5** Confirm and record the two audit questions that shape N4/N6:
  who else links zlib (host link stays if e.g. PNG/PDF paths need it) and who
  else links mbedTLS (`lambda-lib` per the native-module doc). The answers
  decide whether N4/N6 can drop them from the host link or only from the
  Node surface.
- [ ] **N0.6** Amend the JN12 profile table in
  `vibe/Lambda_Design_Jube_Node_Hosting.md`: compatibility/standard contains
  every module replacing a currently built-in observable; reduced-node is the
  intentionally smaller `node-core` profile; minimal is host-only; full Jube
  adds optional Jube modules. Record that a manifest-only descriptor is a
  diagnostic aid, never a behavior-preserving replacement.

### Exit gate

- Evidence directory populated; include/symbol inventory has zero unclassified
  entries; generated future-import allowlists are complete; checker self-test
  green; profile clarification landed; all baselines green and archived.
- No product behavior change of any kind.

## 7. Stage N1 — Two-phase registry and specifier index

### Goal

The resolution spine lands without moving a builtin yet: manifests can
declare runtime-library modules, a read-only catalog answers "is this a
builtin and who owns it" without loading code, and activation remains a
separate transactional operation. Existing builtin rows remain the behavioral
fallback until N2's `path` boundary proof.

### Tasks

- [ ] **N1.1** Split today's selected-manifest path into two explicit phases:
  **catalog** reads and validates manifest metadata without checking/loading
  the library; **activation** verifies the library hash, activates
  dependencies, loads the image, negotiates requirements, registers the
  descriptor, and rolls back transactionally. The catalog records all
  manifests in all roots in stable order; it does not stop at the first
  selector match or reuse a one-bit “paths scanned” state that can starve the
  language or specifier index. Compile-time queries call only catalog
  functions. Tests prove that catalog construction executes neither `dlopen`
  nor module `init`.
- [ ] **N1.2** Manifest schema (JN3): parse `"kind"`
  (`"runtime-library"` | absent=language), `"engine"` (`"js"`), and
  `"provides"` (string array; reuse `jube_manifest_string_array`, the
  `dependencies` parser at `jube_registry.cpp:3414` is the model). Scan-time
  validation: a manifest with neither `language` nor `provides` is rejected
  with a logged reason. Builtin-ness must be decidable from manifests alone —
  no dlopen at scan (JN3). `provides` is owner metadata, not an instruction to
  create globals.
- [ ] **N1.3** Specifier catalog: a registry-owned `HashMap`
  (`lib/hashmap.h`) from normalized specifier → stable owner record. An owner
  record stores module identity, manifest path, catalog state
  (`known-unavailable` / `cataloged` / `activating` / `active` / `failed`),
  and the active descriptor slot when available. Populate it from every
  scanned manifest's `provides` and every static module's
  `JubeNamespaceDef.specifiers`. Normalization at insert *and* lookup: strip
  `node:` except for prefix-only names (`node:test`, …, enumerated from the
  chain during this stage); strip the same trailing `.js` aliases the current
  chain accepts.
- [ ] **N1.4** Define collision and attestation rules: different owners
  providing the same normalized specifier are a deterministic hard error;
  a static descriptor plus its same-name manifest reconcile to one owner;
  immediately after the dynamic entry returns—and before requirements
  negotiation or `init`—the canonical union of descriptor namespace
  specifiers must equal the manifest's `provides` set; every provided
  specifier must map to exactly one namespace builder. A mismatch closes the
  image without executing module callbacks.
- [ ] **N1.5** New host-internal API:
  `jube_specifier_catalog_contains(name)` (never loads),
  `jube_specifier_resolve(name, out_namespace)` (structured status; may
  activate), and `jube_specifier_index_names(cb)`. Structured statuses
  distinguish unknown, known-but-unavailable, activation failure, and success.
  The resolver owns the single top-level JN5 log line; lower loader errors are
  attached detail rather than duplicate user-facing missing-module logs.
- [ ] **N1.6** Make catalog construction thread-safe and immutable for
  parallel lowering readers: a host-owned once/lock builds it before batch
  workers query it; activation state changes remain JS-thread-owned. Relative
  and absolute source imports bypass the catalog entirely; its first scan is
  triggered only by a bare/`node:` module query or a present
  `module-set.json`, so ordinary non-module JS does not scan. Add concurrent
  catalog-query stress under the project's thread sanitizer.
- [ ] **N1.7** Route `js_module_get_builtin` (`js_runtime.cpp:38891`)
  registry-first only for already active/static providers; otherwise fall
  through to the existing chain unchanged. Namespace Items keep flowing into
  the existing `js_modules[]` cache — the registry supplies namespaces, the
  engine owns caching (JN6).
- [ ] **N1.8** Rewire the three compile-time/introspection consumers to ask
  the index first, list second (the lists shrink stage by stage until N3
  deletes them): the lowering skip list
  (`js_mir_module_batch_lowering.cpp:1931`), `js_module_is_builtin`
  (`js_runtime.cpp:39856`), and `builtinModules`
  (`:39237` — becomes index-derived ∪ residual list so reflection matches
  reality per bundle from day one).
- [ ] **N1.9** Tests: catalog/activation separation; collision and
  manifest/descriptor mismatch; normalization including `node:path`,
  `path.js`, unknown specifiers and prefix-only names; stable enumeration;
  known-but-unavailable status; concurrent read-only queries; hosted-language
  discovery before Node lookup and the reverse order. Re-run N0.2 for the
  active/static registry path.

### Exit gate

- node-baseline unchanged; `test/mir` ratchet green (resolution rewiring must
  not change emission — the literal-`require` intercept at
  `js_mir_expression_lowering.cpp:6862` still emits the same call, JN13).
- No builtin row has moved yet; the chain and four lists remain behaviorally
  authoritative fallbacks and mutually consistent.
- Catalog queries never load code; activation errors are structured and
  provider collisions cannot depend on directory iteration order.
- Lambda + Test262 baselines green.

## 8. Stage N2 — Runtime boundary, `path`, and `JubeHostNodeAPI` v1

### Goal

Make the module boundary real before the large move: add pre-init requirement
negotiation and a per-JS-runtime session lifecycle, close the generic
value/script API gaps found by N0, convert `path` so its object has no
forbidden host imports, then land the async/binary/promise/error services and
exercise them through the fs pilot.

### Tasks

- [ ] **N2.1** Freeze two inventories from N0.3's ledger:
  - the generic value/script operations required to convert `path` and the
    later namespace builders (`string_from_utf8_n`, current `this`,
    undefined, property attributes/prototypes, exception clear/take,
    closures/environments, type/string access, and any other empirically
    named gap);
  - the Node op set: map every
  `uv_*`/JS-queue call site in fs/net/child_process/dns (plus the §21-risk-8
  edges: `fs.watch`, tty raw mode, IPC descriptor passing, `uv_tcp_open`-style
  fd adoption) onto §15's micro-kernel or Tier-2 ops. Every op cites ≥1 call
  site (P5); every call site maps to exactly one op or to `moves-with-file`
  (plain syscalls a module may make directly, e.g. tty `termios`). The
  worksheet lands in this document's §15 as the signed-off v1 surface. No API
  entry lands without a ledger call site.
- [ ] **N2.2** Additive lifecycle/requirements ABI:
  - append `const JubeModuleRequirements* requirements`,
    `const JubeGlobalDef* globals` + count, and
    `runtime_attach(session)` / `runtime_reset_session(session)` /
    `runtime_detach(session)` to the `JubeModuleDef` tail, all
    `struct_size`-gated; legacy no-argument `runtime_reset` remains for
    existing modules;
  - `JubeModuleRequirements` declares required host/node capability bits and
    minimum parent/child table versions and sizes. The registry validates it
    before `module->init`; failure executes no module callback;
  - `module->init` is process-lifetime only: validate/store tables and create
    no `Item`, root, global, process extension, rid, timer, or JS callback;
  - `runtime_attach` runs with an opaque live JS session after the global and
    host-core `process` objects exist; `runtime_detach` runs before that
    session's heap is destroyed and must not allocate or re-enter script.
- [ ] **N2.3** Append `const JubeHostNodeAPI* node;` after `data` on
  `JubeHostAPI` (`jube.h:976`), size-gated. The parent composes six versioned
  children: `runtime`, `roots`, `async_ops`, `binary`, `promise`, and `error`.
  `JubeHostRuntimeAPI` exposes current/valid session, current global/process,
  `resolve_namespace(session, specifier, out)` for module-to-module
  script-level composition, and ledger-backed `resolve_host_namespace` for
  thin node-core adapters whose implementations deliberately remain in the
  host (`node:vm`, module-loader core, process core). These are the only legal
  namespace bridges. Re-expose the opaque `JubeHostRootAPI`
  through the parent and append session-bound
  `persistent_root_unregister`; node modules never use the concrete
  `LambdaRootFrame` entries of legacy `JubeHostGcAPI`.
- [ ] **N2.4** Separate globals from namespaces. `JubeGlobalDef` contains a
  global name, flags, and `build(session)` callback. Only globals from an
  **active** descriptor receive lazy object-building accessors; namespace
  specifiers are never installed on `globalThis`. A generic
  `modules/module-set.json` lists modules that a bundle activates before JS
  global construction. Reduced-node lists `node-core`; compatibility lists
  every module that owns a formerly eager global; minimal has no activation
  file and performs no module scan or accessor allocation during JS startup.
- [ ] **N2.5** Convert `path` completely:
  move `lambda/js/js_path.cpp` to `lambda/module/node_core/`, replace its
  engine includes and direct `js_*`/heap/error calls with stored `jube.h`
  table calls, and make its cache session-owned and precisely rooted.
  Register two namespace equivalence classes: the default/POSIX result
  (`path`, `path/posix`, accepted aliases) and the distinct Win32 result
  (`path/win32`, accepted aliases). Declare no `JubeGlobalDef` for `path`.
  Delete its chain/list rows only after CJS and ESM resolve through the
  registry. Compile its object independently and require its undefined-symbol
  set to be `jube.h` callbacks plus approved platform/lib symbols only.
- [ ] **N2.6** Host async implementation, new files:
  - `lambda/js/js_async_services.cpp` — micro-kernel over the existing
    substrate: `work_submit` on `uv_queue_work(lambda_uv_loop(), …)`
    (pool precedents: `network_thread_pool.cpp`, `serve/worker_pool.cpp`);
    `post_completion` on a host-owned `uv_async_t` + locked queue
    (`concurrency.cpp:580` precedent), running the existing
    nextTick/microtask checkpoint after **each completion callback** through
    the same path
    `lambda_uv_set_microtask_drain` already services (`js_event_loop.cpp:1720`)
    — not once per arbitrary batch;
    scheduling entries delegate to `js_next_tick_enqueue`
    (`js_event_loop.cpp:163`), the microtask queue, and the existing timer
    wheel; `register_shutdown(session, …)` keeps an ordered per-session host
    list removed during detach.
  - `lambda/js/js_resource_table.cpp` — the rid table: slot array +
    generation bits packed into `uint32_t` rid; entries carry their owning
    JS session, kind tag, opaque host pointer, `close_fn`, pending-request
    links, and a ref flag; `rid_ref`/`rid_unref`
    forward to `uv_ref`/`uv_unref` on the backing handle (or a loop-liveness
    counter for non-handle resources) so "active handles keep the process
    alive" (`lambda_uv_run` semantics, `lib/uv_loop.h`) is preserved;
    `rid_close` cancels its linked in-flight requests with canceled-status
    completions while the session is live (§15 contract); runtime detach
    closes/cancels every rid/request owned by that session without delivering
    script callbacks; host-side enumeration for `_getActiveHandles` filters by
    the calling session (JN10).
  - `lambda/jube/jube_node_host.cpp` — table assembly + version plumbing.
- [ ] **N2.7** Tier-2 ops (stream/process/signal families, §15) implemented
  host-side over the existing libuv usage patterns lifted from
  js_net/js_child_process — implementation moves behind the API now, the
  *callers* convert in N5. Payload copy-on-submit for writes (v1 contract);
  op-specific completion structs defined in `jube.h` (valid only during the
  callback). Every submit/start entry takes an owning session; completion is
  suppressed once detach starts.
- [ ] **N2.8** Pilot conversion (the design's "fs picked next"): convert
  `js_fs.cpp`'s async `uv_fs_*` sites (`:717–750` region) **in place** to
  `work_submit` + completion, and its promise construction to the promise
  table. This proves the tables against a real consumer before any module
  move; the converted file rides into N5a unchanged. Preserve its existing
  callback and microtask ordering exactly; semantic improvements are separate
  work.
- [ ] **N2.9** Rooting conversion guide: land §16 of this document as the
  reviewed pattern reference; convert the fs pilot's rooting as its first
  application (`js_fs.cpp:1989` in-function extern, `:2529` RootFrame).
- [ ] **N2.10** Negative descriptor tests: extend the loader-negative matrix
  with undersized/missing `node` tables, wrong `api_version`, and a module
  requesting the node capability from a host without it (clean refusal
  before init — a synthetic init callback sets a sentinel and the test proves
  it remains untouched). Add attach-before-global, cross-session root/rid,
  completion-after-detach, and double-detach negatives.
- [ ] **N2.11** Checker goes to enforcement for
  `lambda/module/node_*/` sources (currently `node_core` with the converted
  `js_path.cpp` + module def): `uv_*`, engine internals, GC externs and every
  N0-unclassified/forbidden host symbol on the banned list. Wire
  `check-node-module-architecture` into `build-test` CI.
- [ ] **N2.12** Node modules standardize on the opaque, session-bound root
  table. The `ERR_*` implementation stays host-side behind
  `JubeHostNodeErrorAPI` so host compat paths share it; node-core registers
  only the Node-visible constructors/properties that use those services.

### Exit gate

- `path`, `path/posix`, and `path/win32` return the correct distinct objects;
  no `globalThis.path` is introduced; the path object has no forbidden
  imports and its legacy rows are gone.
- fs pilot: full fs slice of node-baseline + `test/node` corpus green, and
  green under the forced-GC gate (§18.3).
- Requirement checks run before init; runtime/session/root/rid tests and
  negative descriptor tests are green; checker enforcing in CI; MIR budgets
  untouched.
- v1 op worksheet (§15) updated to match what landed — no unused ops, no
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
  helpers' *registration* (implementation per N2.12). Move order within the
  stage: leaves first (querystring, string_decoder, os, url), hub files last
  (events → buffer → util → assert), stream last of all (N3.2). One review
  unit per file or coherent pair (§22). A move is complete only when every
  N0 include/symbol row for that unit is converted to `jube.h`, module-local
  code, the host namespace resolver, or an approved platform library; source
  relocation alone is not a checkpoint.
  Public specifiers whose implementation deliberately stays in the host
  (`node:vm`, module-loader/process core, and any other N0-classified survivor)
  still receive thin node-core namespace definitions that call the named
  `resolve_host_namespace` provider. Thus deleting the chain does not expose
  them in minimal and does not require a direct host symbol import.
- [ ] **N3.2** Split `js_stream.cpp` (10,111 lines): Node half →
  `lambda/module/node_core/js_stream_node.cpp`; the WHATWG half
  (ReadableStream/WritableStream class implementations — re-enumerate their
  exact extent at stage start) stays host-side in a new
  `lambda/js/js_web_streams.cpp` pending N8. `stream/web` (and
  `stream/consumers` where it touches web streams) is served from
  `node-core` as a re-export over a transitional host namespace provider.
  N8 replaces that provider with
  `runtime->resolve_namespace("web-streams")`. Sequencing rule
  from design §13.1: if a stream-coverage campaign is active, land stream
  last within N3 or after it.
- [ ] **N3.3** Process split (design §6.4 rule: engine-called stays, only
  Node-observable moves): enumerate the `js_globals.cpp:2700–4470` members
  into host-core vs node-core lists (the enumeration is a committed artifact
  in `test/benchmark/hosted_node/`); `node-core` extends the host `process`
  during `runtime_attach` (memoryUsage, signals, `_getActiveHandles` via the
  rid-table enumeration; nextTick *binding* stays host). Risk 3 applies: test harnesses
  reading Node-only members (e.g. `process.memoryUsage` in the shims) are
  part of the enumeration. The extension occurs in `runtime_attach`, never
  process-level module `init`; detach drops every session-owned cached member
  without allocating.
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
- [ ] **N3.5** Globals: delete the implicit
  `JubeNamespaceDef.specifiers[0] → globalThis` behavior. During
  `runtime_attach`, install lazy object-building accessors only for the active
  module's explicit `JubeGlobalDef` rows. `Buffer` and the intentionally
  retained `os`/`vm` compatibility globals use explicit rows; process
  extensions attach to the existing host-core object and are not separate
  globals. The unconditional install block
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
  checks inside getters to session-keyed module cache records.
  `runtime_attach` creates/registers them; `runtime_reset_session` drops
  borrowed cache values for that session; `runtime_detach` unregisters
  persistent roots and destroys the record before `heap_cleanup`. No module
  reads a heap pointer or epoch.
- [ ] **N3.8** Rooting conversion for every moved file per §16, forced-GC
  gate per file batch (§18.3).

### Exit gate

- All four name lists and the chain are gone; grep proves no
  `js_module_get_builtin` chain remnant.
- node-baseline unchanged; `test/node` corpus green; forced-GC slice green
  on converted files; checker green over `lambda/module/node_core/`
  (no direct engine-symbol allowance; platform imports match the N0 ledger).
- Minimal-profile smoke (host built without node_core registration in a test
  configuration): non-Node JS runs; `require('fs')` produces the JN5 error;
  console works via the absent-default formatter; no `path`, `Buffer`, `os`,
  or `vm` property is accidentally installed from namespace metadata.
- Lambda + Test262 + MIR budgets green.

## 10. Stage N4 — `node-zlib`: first dynamic module

### Goal

Prove the full delivery chain (build target → manifest → lazy dlopen → absent
negatives) on the smallest dependency-backed leaf without making that
dependency a DSO. `node-zlib` is the reference inversion: the host owns the
statically linked codec, while the image owns the Node API and calls a
versioned host codec provider.

### Tasks

- [x] **N4.1** Add `JubeHostNodeZlibAPI` to the `JubeHostNodeAPI` tail. Its
  plain-byte input/output contract supplies CRC32 and the seven one-shot codec
  modes; allocation/free and zlib status stay host-owned. Move the raw codec
  implementation to a host provider source and replace every direct codec
  call in `node_zlib_module.cpp` with the table. The module retains input
  validation, Buffer conversion, error shaping, namespace construction, and
  callback scheduling. Preserve today's synchronous-plus-nextTick behavior;
  work-pool conversion is separate. Static checkpoint first (P7).
- [x] **N4.2** Build target: add a module-target generator step
  by extending `build_lambda_config.json` with one node-module template plus
  instance data consumed by `utils/generate_premake.py` (the JSON remains the
  source of truth; no generated `.lua` is edited). Each instance supplies name,
  `link: dynamic`, `pic`, `target_dir: modules/node-zlib`,
  `-Wl,-undefined,dynamic_lookup` on macOS + the linux equivalent, explicit
  `source_files`) — clone of `build_lambda_config.json:305`, not copy-paste
  ×8 (rule 13). Make targets `build-node-zlib` / `release-node-zlib`
  mirroring `Makefile:772/:783` incl. the
  `update_jube_manifest_integrity.py modules/node-zlib` metadata-cleanup step.
  The node-zlib instance has no `libraries` entry and no codec source: the
  generated image must have no host-dependency imports.
- [x] **N4.3** `modules/node-zlib/module.json` per design §6.2 (kind
  `runtime-library`, engine `js`, `provides`, `dependencies: ["node-core"]`,
  per-OS library, `entry_symbol: "jube_module"`). Build-local hash and host
  id fields remain absent; negative fixtures may supply a digest explicitly.
  Dependency
  activation exercises the existing `dependencies` machinery
  (`jube_registry.cpp:3414`) — add a test that requiring `zlib` with
  `node-core` present-but-unloaded activates `node-core` first.
- [x] **N4.4** Flip dynamic: remove the Node zlib namespace implementation
  from the host target, but keep the host codec provider and zlib static link.
  The module now owns the namespace, validation, callbacks, constructors, and
  stream facade; the host retains only the versioned byte/stateful codec
  provider. Static/dynamic parity uses two explicit test artifacts:
  a disposable static-checkpoint host and a production-shaped host with the
  source absent plus the dylib. `JUBE_MODULE_PATH` isolates the latter's
  bundle; it is not claimed to switch a statically linked module off. Run the
  zlib slice (in-tree + official) in both and diff.
- [x] **N4.5** Absent-module negatives: bundle copy without the dylib →
  `require('zlib')` yields `MODULE_NOT_FOUND` + host log naming `node-zlib`
  (manifest-only descriptor upgrades the log to "known, not installed" —
  `package-standard` precedent at `Makefile:795`); tampered-library and
  wrong-ABI rows added to the loader-negative matrix (parameterize
  `utils/test_jube_module_loader_negative.py` by module rather than cloning
  it).

### Exit gate

- Dynamic load green on macOS + Linux; parity diff empty; node-baseline
  unchanged; negatives green; node-zlib import audit contains neither libz nor
  host runtime symbols; host byte-identity across packagings maintained.

## 11. Stage N5 — libuv leaves: fs, net, child_process

Each sub-stage follows one shared recipe (a checklist committed once in
`lambda/module/README.md` and referenced, not restated — rule 13):
move sources → burn every N0 include/symbol row to a named `jube.h` service,
module-local implementation, namespace resolution, or approved platform
library → rooting per §16 → static parity → checker/import audit →
manifest/build target from the N4 generator → dynamic flip → absent negatives.

- [x] **N5a — `node-fs`** (3.9k lines; fs, fs/promises, FileHandle as
  `JubeTypeBinding`). Every platform operation now crosses the size-gated
  `JubeHostFilesystemAPI`: file-content work, path mutation, directory/string
  results, descriptor open/close/chmod/chown/read/write, normalized
  stat/lstat/fstat metadata, and `statfs`. The module owns only Node argument
  validation, permissions, callbacks/promises, branded `FileHandle`/`Stats`,
  and error shaping. `fs.watch`/`watchFile` preserve the existing observable
  stub contract; no unimplemented event backend was silently dropped. Static
  and isolated dynamic normal/poisoned-GC, absence, integrity, and import
  gates pass with no filesystem syscall or platform-I/O header in the DSO.
- [ ] **N5b — `node-net`** (7.9k lines; net + dns + cares_wrap shim). First
  real consumer of the Tier-2 stream ops (connect/listen/accept/read/write/
  shutdown/opts/fd-adoption → rids). The transitional host table already
  uses session-owned, generation-checked rid entries and persistent roots;
  this stage moves those entries behind the stream service and module boundary.
  The existing IP-literal / `dns.lookupSync` provider probe is explicitly
  **not** this completion: it still delegates `net`, `dns`, and
  `internal/js_stream_socket` namespaces to the legacy host. Complete N5b in
  this order:

  1. Extract a host-owned `JubeHostStreamAPI` with opaque, session-owned rids
     for TCP/pipe create, connect, bind/listen, accept, read, write, shutdown,
     close, ref/unref, socket options, and fd adoption. Its completion records
     carry only status, byte counts, peer/address metadata, and new rids; no
     `uv_*` value or pointer crosses the ABI.
     The initial host bootstrap now supplies TCP create/bind/address/fd-
     adoption/close/ref/unref/live rid ownership through the existing
     generation-checked resource table. `node-net` has a first module-owned
     `BoundSocket` vertical slice over those rids, while legacy `Server` and
     `Socket` consume adopted descriptors during the transition. It is
     deliberately not exposed as a Node namespace and does not yet make
     `node-net` complete. Add the remaining verbs as real host operations,
     with completion records, before moving their Node-facing callers.
  2. Route the *legacy* `js_net.cpp` through that provider first and prove its
     normal and forced-GC baselines. This isolates transport lifetime and
     drain ordering without changing Node-visible state at the same time.
  3. Move the Node state machine (socket/server objects, stream events,
     auto-select-family policy, IPC handoff, and Node errors) into
     `node-net`; remove every `resolve_host_namespace("net"|"dns"|
     "internal/js_stream_socket")` delegation. The module may retain the
     existing IP/DNS validation helpers, but its namespace must be constructed
     locally.
  4. Add host DNS operations: synchronous literal/lookup and asynchronous
     `getaddrinfo` completion via the host work service. `_getActiveHandles`
     then reads the host rid table, and direct `Context` access
     (`js_net.cpp:4591`) is eliminated.

  The final node-net DSO neither links libuv nor performs a direct socket
  operation, and it cannot fall through to the legacy host namespace.
- [ ] **N5c — `node-child-process`** (3.8k lines). Tier-2 process ops
  (spawn/kill/exit-completion; single SIGCHLD owner stays host); stdio spec
  references rids; the IPC handoff hook (N3.4) lands for real — the
  `uv_pipe_t*` extern is deleted; cluster-online glue moves in
  (`js_runtime.cpp:62` edge gone). Like N5b, do this in two passes: first
  make the legacy implementation consume a host process provider and prove
  lifecycle parity; only then move child-process JS policy, stdio objects,
  IPC framing, and callback/promise settlement into the module. A module that
  merely returns the legacy `child_process` namespace is not a completed N5c
  migration.

### Exit gate (per module, then for the stage)

- node-baseline unchanged; forced-GC gate green on the module's slice;
  checker: zero `uv_*`, zero engine-internal references from
  `lambda/module/node_*/`; absent negatives green; static/dynamic parity
  diffed; `js_runtime_state.hpp` includer count for migrated files = 0.

## 12. Stage N6 — protocol leaves and Node crypto

- [ ] **N6a — `node-http`** (6.9k lines; http + https; depends node-core +
  node-net). The HTTP/1.1 parser is module-local; host stream/TLS provider
  operations replace raw libuv accept
  (`js_http.cpp:3357` region) converts to stream-op accept→rid; the 10-root
  `RootFrame` at `:5955` converts per §16. Profile against N0's throughput
  numbers before/after — this is the copy-on-submit stress case (§21 risk 8);
  if profiles demand it, spec the pinned zero-copy additive entry then, not
  preemptively.
- [ ] **N6b — `node-tls`** (3.1k lines; host TLS provider including the
  existing PFX/PKCS12 support; depends node-net). Neither mbedTLS nor the
  OpenSSL soft dependency may appear in the image import table.
- [ ] **N6c — prepare the three-way crypto split and extract Node crypto**
  (JN14 as superseded by JA1).
  Sub-task order: (1) extract one host-owned, versioned crypto-provider table
  over the existing static mbedTLS/digest implementation; (2) `node-crypto`
  module (the node:crypto surface, ≈6–7k lines after the split); (3) leave
  WebCrypto host-side behind the transitional namespace provider.
  `node:crypto.webcrypto` resolves that provider through the host runtime
  table. Re-enumerate the WebCrypto extent inside
  `js_crypto.cpp`/`js_globals.cpp` at sub-stage start (budget it as surgery,
  not a file move — design §13.2). mbedTLS drops from the minimal host link
  iff the N0.5 audit clears `lambda-lib`'s use. Actual `web-crypto`
  extraction is N8 and cannot block Node closure.

### Exit gate

- node-baseline unchanged; `crypto`/`tls`/`http` individually absent at
  runtime produce clean JN5 errors (the native-module doc's POC 2 exit
  criterion); minimal-host link audit results recorded (zlib/mbedTLS);
  WebCrypto and WHATWG streams remain behaviorally neutral behind
  transitional host providers pending N8.

### WPT gating and the legal transitional state

JA9 forbids landing a web-platform module without its WPT slice. The harness
choice (reuse Radiant WPT infra vs a js262-style runner; architecture open
item 3) is not this plan's decision. Therefore N7 closes Node hosting with
WebCrypto host-side as today and WHATWG streams host-side in
`js_web_streams.cpp`; node modules use host-mediated namespace providers.
N8 waits indefinitely if necessary. What is not legal is shipping either web
module without its slice.

## 13. Stage N7 — Node closure

### Tasks

- [x] **N7.1** `node-core` flips dynamic (the largest module; every earlier
  stage's parity machinery exists by now). The static registration path for
  node modules is removed entirely (template H10 discipline: static was a
  checkpoint, not a product form — JA2). `URL`/`URLSearchParams` and
  `EventEmitter` remain host-owned shared primitives because the global URL
  surface and readline use the same identities; node-core resolves `url` and
  `events` through `JubeHostRuntimeAPI`, and the binary gate rejects direct
  imports of their lifecycle symbols.
- [ ] **N7.2** Packaging produces four bundles from one host:
  - compatibility/standard: all extracted Node modules, preserving today's
    Node-visible behavior;
  - full Jube: the compatibility set plus optional Jube modules such as
    `lang-python`;
  - reduced-node: `node-core` plus manifest-only leaf descriptors, with
    deliberate JN5 failures for `fs`/`net`/…;
  - minimal: host only.
  `modules/module-set.json` activates only eager-global owners. Extend
  `verify-jube-package` (`Makefile:810`) with host byte-identity across all
  four; a packaged node-baseline smoke against compatibility; expected
  reduced failures; and minimal smoke (`typeof Buffer === 'undefined'`,
  non-Node JS green).
- [ ] **N7.3** Docs: `doc/Lambda_Jube_Runtime.md` gains the node-module
  bundle section; `vibe/Lambda_Design_Jube_Node_Hosting.md` status flips to
  implemented-with-deltas; the native-module doc's POC 2 marked delivered;
  memory/ledger docs updated.
- [ ] **N7.4** Checker closure: the platform-library allowlist is burned down
  to the permanent set (each survivor named and justified in checker source);
  `--require-module-binary` mode (nm import audit) mandatory for all node
  module dylibs in CI — do not inherit Python's open H8 laxity (design §4.5
  caveat; shared burn-down with JA open item 5).
- [ ] **N7.5** Perf closeout vs N0 evidence (§19): require microbench,
  node-baseline wall time, host + bundle sizes, minimal-profile startup,
  http/fs throughput spot-checks. Any significant regression stops closure
  (template §20.3 rule).

### Exit gate — definition of done (superset of design §11 exit criteria)

- memcmp chains and all four lists deleted; resolution is registry-only.
- Every node module loadable dynamically; `crypto`/`tls`/`http` optional at
  runtime with clean errors; minimal host runs non-Node JS suites.
- node-baseline ≥ N0 ledger; Lambda/Test262/MIR budgets green; forced-GC
  gates green per module; checker green with final allowlist.
- Byte-identical host across minimal/reduced-node/compatibility/full; release perf within
  noise of N0.

## 14. Stage N8 — WPT-gated web modules

### Goal and tasks

- [ ] **N8.1** Land the selected WPT harness and vendored, version-recorded
  `WebCryptoAPI/` and `streams/` slices before moving either implementation.
- [ ] **N8.2** Extract `web-crypto` with explicit `JubeGlobalDef` rows for
  `crypto`; `node:crypto.webcrypto` resolves its namespace through
  `JubeHostRuntimeAPI`. Run its WPT slice, Test262, Node crypto slice,
  forced-GC and static/dynamic parity gates.
- [ ] **N8.3** Extract `web-streams` from `lambda/js/js_web_streams.cpp` with
  explicit global rows for the WHATWG constructors. Replace node-core's
  transitional provider with host-mediated namespace resolution for
  `stream/web` and related consumers. Run its WPT slice plus Node stream,
  forced-GC and parity gates.
- [ ] **N8.4** Add both modules to compatibility/standard and full Jube
  packaging because they replace globals visible in today's monolith. Do not
  add them to reduced-node or minimal. Re-run host byte-identity and profile
  global-surface tests.

### Exit gate

- Both WPT slices are green; compatibility/standard remains observably
  equivalent to the pre-extraction global and Node surfaces; reduced and
  minimal profiles expose only their documented globals.

## 15. The service surface — v1 worksheet (JN7/JN8)

Signed off in N2.1; this section then becomes the record of the landed v1.
House rules: `api_version` + `struct_size` first on every table; status-code
returns; pending exceptions; borrowed Items rooted before allocating ops;
completion payloads are op-specific C structs valid only during the callback;
Node semantics (fs.Stats shaping, Buffer construction, error objects) stay
module-side.

```c
struct JubeHostNodeAPI {
    uint32_t api_version; uint32_t struct_size; uint64_t capabilities;
    const JubeHostRuntimeAPI*   runtime;
    const JubeHostRootAPI*      roots;
    const JubeHostAsyncAPI*     async_ops;   // Tier 1 + Tier 2
    const JubeHostBinaryAPI*    binary;
    const JubeHostPromiseAPI*   promise;
    const JubeHostNodeErrorAPI* error;
};
```

`JubeHostRuntimeAPI` defines the JS session boundary:
`current_session`, `session_is_live`, `global_this`, `process_object`, and
`resolve_namespace`. Namespace resolution returns a structured status and a
borrowed `Item`; callers root it before any allocating/re-entrant call.
`JubeHostRootAPI` uses only opaque `JubeRootFrame` storage and session-bound
persistent register/unregister. A root registered for session A cannot be
used or removed through session B.

`JubeModuleRequirements` is descriptor metadata inspected before `init`; it
contains required capability bits and minimum size/version records for every
table the module consumes. `JubeGlobalDef` is independent of
`JubeNamespaceDef`; globals are installed only for an already active module
during runtime attach.

**Tier 1 — micro-kernel (~15 entries, five concepts; sufficient for
node-zlib, node-fs (minus watch), dns, node-crypto):**

| Concept | Entries | Host backing (verified precedent) |
|---|---|---|
| scheduling | `next_tick`, `enqueue_microtask`, `timer_start`, `timer_clear` | `js_next_tick_enqueue` (`js_event_loop.cpp:163`), microtask queue, existing timer wheel |
| completion post | `post_completion(session, cb, user)` — any thread → owning JS thread, loop order, host runs the existing nextTick/microtask checkpoint after each callback | `uv_async_t` (`concurrency.cpp:580` pattern) + the `lambda_uv_set_microtask_drain` drain path (`js_event_loop.cpp:1720`) |
| blocking work | `work_submit(session, fn, complete, user)`, `work_cancel` | `uv_queue_work` (`network_thread_pool.cpp`, `js_fetch.cpp:546` patterns); pool sizing policy + long-op hygiene rule documented here at N2 (risk 8) |
| resource table | `rid_add(session, ptr, close_fn)`, `rid_get`, `rid_close`, `rid_ref`, `rid_unref` | new `js_resource_table.cpp`; slot+generation uint32; owner-session validation; ref/unref → `uv_ref`/`uv_unref` liveness |
| lifecycle | `register_shutdown(session, cb, user)` / unregister | ordered per-session host list; detach unregisters it; replaces the shutdown backward call |

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
completions while the session is live; detach cancels all session-owned
requests without script delivery and prevents later callbacks from entering a
dead runtime; no loop pointer, no `uv_*` type, no escape hatch — ever.
`work_submit` copies or takes explicit ownership of native/POD inputs before
returning; its worker callback cannot access an `Item`, root, JS session, or
any Jube host service. Only the JS-thread completion callback may turn the
native result back into rooted Items.

`JubeHostBinaryAPI` (buffer_alloc/from_copy/bytes, arraybuffer_alloc,
typed_array_view, is_buffer; marked extension: `buffer_adopt`),
`JubeHostPromiseAPI` (promise_new withResolvers-shape, resolve, reject,
is_promise), and `JubeHostNodeErrorAPI` (error_with_code, errno_error —
implementation host-side per N2.12, throw via `script->throw_value`) land as
sketched in design §9.2–9.4 with signatures frozen at N2.1.

## 16. Rooting conversion guide (JN9)

Landed as the reviewed reference in N2.9; every conversion is reviewed per
site — this population is exactly where unrooted `Item` locals across
allocating calls were found (107/244 scripts diverging under forced GC).

| Today | Becomes | Notes |
|---|---|---|
| file-local `extern heap_register_gc_root(_range)` (js_net.cpp:34, js_dns.cpp:28, js_os.cpp:21, js_tls.cpp:33, js_child_process.cpp:54, js_fs.cpp:1989 in-function) | `host->node->roots->persistent_root_register(session, slot)` or rid-table ownership when the value's lifetime is the resource's | delete the extern; range registration of raw arrays (js_net.cpp:120) is replaced by rid entries + per-slot roots; unregister during detach |
| `RootFrame`/`Rooted` stack scopes (js_fs.cpp:2529, js_http.cpp:5955, js_child_process.cpp:3731) | opaque `roots->root_frame_begin` / `root_frame_take_slot` / `root_frame_end` | slot counts audited against the live values; never use `LambdaRootFrame` |
| `js_heap_epoch`-keyed namespace caches (js_runtime.cpp:39204 pattern) | session-keyed cache created at `runtime_attach`, cleared/reset for that session, unrooted at `runtime_detach` | no heap/epoch identity leaks into a module |
| direct `Context`/heap field access (js_net.cpp:4591) | eliminate; needed values arrive via host APIs | checker-banned |
| `heap_calloc`/`heap_create_name`/`s2it` inline uses | additive value/script/data table calls named by the N0 ledger | elimination is required before the file counts as moved into a module; no dynamic checkpoint may import them |

Review rule per converted function: every `Item` local that lives across any
allocating or re-entrant call is in a root slot; every completion callback
re-roots what it holds before building Items. Gate: the §18.3 forced-GC run
on the module's slice, plus goldens byte-identical.

## 17. Architecture checker spec (`check_node_module_architecture.py`)

Model: the hosted-Python checker (path-anchored source rules; binary `nm`
mode; self-test with synthetic violations; "checks land with their owning
stage" philosophy — see its header comment).

1. **Scope:** `lambda/module/node_*/**` and `lambda/module/web_*/**` sources
   and, in binary mode, the built dylibs.
2. **Banned (source + binary):** `uv.h`/`uv_*` symbols; `js_runtime.h`,
   `js_runtime_state.hpp`, `Context` layout access; `heap_register_gc_root*`
   externs; engine `RootFrame`/`Rooted` types; `js_heap_epoch`;
   `dlopen`/`dlsym`; direct POSIX socket/spawn calls in files whose ops are
   Tier-2-owned (per-module rule rows); cross-module symbol imports; and every
   direct host symbol not classified in the N0 ledger. Namespace builders
   cannot appear in `JubeGlobalDef` unless the global is explicitly intended.
3. **Allowed:** `jube.h`, libc, and platform calls that do not duplicate a
   host-owned dependency. Filesystem, socket/process, zlib, HTTP, TLS, and
   crypto capability calls are provider-table operations once their module has
   crossed its static checkpoint. Module-local symbols remain allowed. Engine
   symbol exceptions are not transitional module allowances: an unconverted
   file remains outside the module directory until its rows are burned down.
4. **Binary mode:** dynamic import table ⊆ the module entry point/runtime
   loader allowance + approved platform libraries, and its linked-library set
   excludes libz, libuv, curl, mbedTLS, OpenSSL, and every configured static
   host dependency. Calls into the host occur through stored Jube table
   pointers, not undefined engine imports. Run `--require-module-binary` from
   the first dynamic flip (N4); it is mandatory in CI at N7.4.
5. **Self-test:** injects a `uv_tcp_init` call, a `js_runtime_state.hpp`
   include, a `heap_register_gc_root` extern, and a fresh allowlist entry
   into scratch copies under `./temp/` and must see all four rejected.

## 18. Test and verification matrix

### 18.1 Required on every stage

```sh
make build
make test-lambda-baseline        # includes test/node in-tree corpus + MIR ratchet
make test262-baseline            # zero failed, zero retries
make node-baseline               # locked official set, no regressions
make check-node-module-architecture
git diff --check
```

### 18.2 Per converted module (N2 onward)

- Its node-baseline slice + in-tree corpus slice in a disposable
  **static-checkpoint host**, then a production-shaped **dynamic host** whose
  target excludes the module sources (isolated bundle copy via
  `JUBE_MODULE_PATH`, `Makefile:822` pattern), diffed against each other and
  the pre-move run. A path override alone is never treated as switching off a
  statically linked copy.
- Absent-module negatives (missing dylib, tampered library, wrong ABI/build
  id, missing dependency → rollback observed) through the parameterized
  loader-negative matrix.
- Descriptor negatives for any new table the module consumes (undersized,
  version-mismatch — clean refusal before `init`).
- Runtime lifecycle negatives: attach only after global/process creation;
  cross-session root/rid rejection; reset/detach idempotence;
  completion-after-detach suppression; no roots/rids/requests surviving heap
  cleanup.
- A host-ownership audit: generated node-module link inputs contain no
  configured host dependency, and the resulting image has neither a matching
  library dependency nor a forbidden dependency symbol. The host executable
  continues to link its static provider. This applies even when the Node
  namespace is package-optional.
- Global-surface snapshots (`Reflect.ownKeys(globalThis)` plus property
  descriptors): compatibility equals the pre-move snapshot; reduced/minimal
  differ only by their documented explicit-global rows; namespace-only
  providers such as `path` never appear.

### 18.3 Forced-GC gate

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

### 18.4 MIR budgets

`test/mir` ratchet green at every stage; any diff in emission for existing
scripts is a defect of the rewiring (the resolution changes are runtime-side
by construction — the lowering intercepts still emit identical calls).

### 18.5 Sanitizers / stress

Reuse the project's sanitizer configuration for: rid-table
lifetime/generation reuse, completion-after-close, work-pool
submit/cancel races, repeated attach/reset-session/detach/heap-cleanup cycles,
shutdown with active rids, module init failure rollback. No leak,
use-after-free, stale rid acceptance, or unbalanced root frame.

## 19. Release performance protocol

Method verbatim from the Python plan §20.2 (release builds, ≥7 reps,
medians + dispersion, alternate before/after, archive raw samples in
`test/benchmark/hosted_node/`). Protected measurements:

- cached require-latency + namespace-build microbench (N0.2) — steady-state
  cached resolution must not regress significantly;
- cold catalog scan, integrity verification, dependency activation and
  `dlopen` reported separately against an explicit activation budget; cold
  dynamic activation is not required to equal an in-binary memcmp dispatch;
- node-baseline wall time;
- Lambda and non-Node JS startup (minimal profile) — must show zero node
  cost when absent;
- host and bundle sizes per stage (host shrinks as leaves detach — record,
  don't gate);
- N6a http and N5a fs throughput spot-checks vs N0 (the copy-on-submit
  decision input).

Acceptance: no statistically significant regression on steady-state protected
measurements; cold activation stays within its recorded budget. A regression
stops the stage pending root cause (never absorbed as "migration overhead").

## 20. Review checkpoints requiring explicit attention

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
9. the WPT harness decision being pre-empted by module code (N8 waits);
10. any manifest/ABI field changing meaning rather than appending (JA11).
11. any attempt to use a manifest dependency as though it were a callable
    interface rather than activation ordering;
12. any namespace becoming a global without an explicit `JubeGlobalDef`;
13. any module `init` allocating or touching a JS runtime object.

## 21. Risk register

| # | Risk (design §13 ↔ here) | Stage | Mitigation/gate |
|---|---|---|---|
| 1 | stream is huge and load-bearing; conflicts with coverage campaigns | N3.2 | move stream last within N3; resequence on conflict (checkpoint 8) |
| 2 | crypto/WebCrypto split is surgery in an 8.7k file | N6c | three sub-tasks with the shared-primitives layer first; re-enumeration at sub-stage start |
| 3 | process split hides engine deps on Node-only members | N3.3 | committed enumeration artifact; shim audit; minimal-profile smoke |
| 4 | console degradation without node-core | N3.4 | absent-default formatter written + tested in N3, not later |
| 5 | npm resolver placement | post-N5 | explicitly deferred; revisit note in N7.3 docs |
| 6 | `node:vm` stays host | — | documented exception (JN6); no work here |
| 7 | Windows loader parity | N4+ | keep export surface = Jube tables only (JN8 helps); Windows CI is out of scope but the export audit (§17.4) keeps the door open |
| 8 | request-API coverage/cost: fs.watch, IPC fd passing, fd adoption; pool starvation; copy-on-submit cost | N2.1/N5/N6a | edge inventory before v1 freeze; pool sizing + hygiene rule documented; zero-copy only on N6a profile evidence |
| 9 | worker_threads futures vs process-lifetime modules | — | note in N7.3; every future isolate needs its own session attach/reset/detach and rid/root ownership audit |
| 10 | Python H8 laxity inherited | N2.11/N7.4 | binary-mode checker mandatory from first dynamic flip; shared burn-down with JA open item 5 |
| 11 | zlib/mbedTLS host-link entanglement blocks "drops out of host" claims | N0.5/N4/N6 | audit first; claims scoped to audit results |
| 12 | official-suite forced-GC runtime | N3+ | committed per-module subsets (§18.3), not full-suite sweeps |
| 13 | eight hand-edited build targets/manifests drift | N4.2 | one generator template for module targets + manifests (rule 13) |
| 14 | namespace/global conflation leaks globals (`path`) or builds the wrong sub-namespace (`path/win32`) | N2/N3 | separate descriptors; equivalence-class mapping tests; global-surface snapshots |
| 15 | process-level module init runs before a JS heap/global exists | N2/N3 | process-only init + session attach/detach contract; negative lifecycle tests |
| 16 | async completion or persistent roots outlive their JS runtime | N2+ | owner session on every request/rid/root; cancel/unregister at detach; sanitizer stress |
| 17 | manifest dependency is mistaken for an inter-module ABI | N0/N2+ | complete symbol ledger + host-mediated namespace resolver; binary import rejection |
| 18 | reduced-node packaging is misrepresented as behavior preserving | N0/N7 | four named profiles; packaged compatibility node-baseline; explicit reduced negatives |

## 22. Recommended landing series (review units)

1. N0 evidence + complete include/undefined-symbol inventory;
2. N0 checker/self-test + governing profile clarification;
3. N1 read-only manifest catalog and structured owner records;
4. N1 collision/attestation rules + index-first compile-time consumers;
5. N2 requirements, runtime session, opaque roots, and explicit global
   descriptor ABI;
6. N2 generic value/script API gap closure + fully converted static `path`;
7. N2 async/rid/binary/promise/error host services + lifecycle negatives;
8. N2 fs pilot conversion (+ rooting guide application);
9. N3 per-file node-core moves (one unit per file/pair, leaves → hub → stream);
10. N3 process attach, hook inversions, and explicit globals;
11. N3 chain/list deletion + `builtinModules` from index;
12. N4 JSON-backed module-target generator + node-zlib static;
13. N4 production-shaped node-zlib dynamic artifact + negatives;
14–16. N5a/N5b/N5c (each: convert → two-artifact parity → flip);
17–19. N6a/N6b/N6c Node modules;
20. N7 node-core flip + four-profile packaging/verification;
21. N7 docs + import-allowlist closure + perf closeout;
22–23. N8 web-crypto/web-streams only after their WPT harness/slices land.

Each unit carries its focused tests, checker delta, and the §18.1 gates;
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
`dependencies` controls activation order only. At activation the registry
attests that the descriptor's canonical namespace-specifier union equals
`provides`; it does not infer globals or a callable native dependency ABI.

## Appendix B — bundle activation template

Only modules owning formerly eager globals appear here; ordinary builtin
providers remain lazy until `require`/`import`. Example reduced-node profile:

```json
{
  "profile": "reduced-node",
  "activate": ["node-core"]
}
```

The compatibility profile adds `web-crypto` and `web-streams` after N8
because they replace currently eager globals. Leaf Node modules need to be
present in compatibility packaging but do not need eager activation. Minimal
ships no `module-set.json`; full Jube extends compatibility with optional
non-compatibility modules.

## Appendix C — name-list deletion checklist (all gone by N3.6)

| List | Anchor (2026-07-26) | Deleted at |
|---|---|---|
| `js_module_get_builtin` memcmp chain + inline externs | `js_runtime.cpp:38891` | `path` rows at N2.5; remaining rows N3; body N3.6 |
| `builtinModules` for `node:module` | `js_runtime.cpp:39237` | index-derived + residual from N1.8; residual list gone N3.6 |
| `js_module_is_builtin` | `js_runtime.cpp:39856` | index-first from N1.8; residual list gone N3.6 |
| batch-lowering `builtin_names[]` | `js_mir_module_batch_lowering.cpp:1931` | index-first from N1.8; residual list gone N3.6 |
| `node:` prefix special cases | `js_mir_entrypoints_require.cpp:1581`; `js_mir_module_batch_lowering.cpp` | N3.6 (normalization owns aliasing) |

## Appendix D — relationship to prior decisions

| Prior decision | Effect here |
|---|---|
| Design JN1–JN13, §11 N0–N7 | implemented with the explicit lifecycle, namespace/global, registry-attestation, profile, and N8 amendments recorded in this plan and synchronized back to the design at N0 |
| JA1/JA3 supersessions (JN14, `stream/web`) | folded as the N3.2 transitional split, N6c Node-crypto extraction, and independent WPT-gated N8 web modules |
| JA9 gates | node modules: node-baseline; N8 web modules: mandatory WPT slices |
| JA16 | the banned `uv_*` checker list is its first enforcement instance; §15 op families feed its future surface |
| JN12 profile ambiguity | clarified before implementation: compatibility/standard contains all extracted replacements; reduced-node is the intentionally smaller node-core bundle |
| CLAUDE rules 13/14/15 | one generator for targets/manifests; no C2MIR work; precise rooting only, forced-GC gated |
| Hosted-Python plan H0–H10 | stage anatomy, checkpoint discipline, checker/self-test pattern, packaging identity checks, perf protocol reused; compiler-service stages replaced by N2 |
