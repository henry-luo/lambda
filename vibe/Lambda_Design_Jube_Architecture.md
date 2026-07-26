# Jube & Lambda Runtime — Architecture ADRs

> **Status:** proposal, drafted 2026-07-26 from review direction; records
> architecture-level decisions (ADRs) spanning the Lambda/JS core runtime and
> the Jube module system. Detailed mechanics live in the per-area designs;
> this document is the top-level contract they must agree with. Where an
> older per-area doc conflicts, **this document wins** and the superseded
> spot is marked in place (see §Supersessions).
>
> **Jube** is the native-module system of the unified runtime — serving both
> the Lambda/JS runtime **and the Radiant engine** (JA14): the way the one
> host executable is extended — with hosted languages, IO/platform APIs,
> engine bridges, layout/text extensions, and data-heavy packs — without
> recompiling it.
>
> **Related designs:**
> - `vibe/Lambda_Design_Native_Module.md` — module ABI, capability tables, VMap projections
> - `vibe/Lambda_Design_Jube_Lang_Hosting.md` — hosted-language architecture (Python first)
> - `vibe/Lambda_Impl_Hosted_Python.md` — staged carve-out execution (H0–H10)
> - `vibe/Lambda_Design_Jube_Node_Hosting.md` — Node compat as Jube modules (JN1–JN14, N0–N7)
> - `vibe/radiant/Radiant_Design_Concurrency.md` — pages as Lambda isolates (JA15 context)
> - `vibe/radiant/Radiant_Design_State_Management.md` — Lambda-page regeneration model (JA15 context)
> - `vibe/Lambda_Design_Concurrency.md` — §11 K21–K27 shared stream core (JA1 file/stream line)
> - `vibe/Lambda_Design_Pipeline.md` — text/data/binary pipelines over the K27 core (JA1 file/stream line)
> - `vibe/Lambda_Design_Static_Modules.md` — static-library layering of the host itself
> - `vibe/Lambda_Design_MIR_Cache_L3.md` — compiled-script cache (JA13 adjunct)
> - `doc/Lambda_Jube_Runtime.md` — user-facing runtime/bundle description

## 0. Decision index

| ID | Decision | Status |
|---|---|---|
| JA1 | Core runtime scope = ECMA-262 + engine substrate; all platform/IO surface goes to Jube modules (V8 : Node/Deno reference split) | adopted; scope criteria refined in review 2026-07-26 |
| JA2 | Jube modules are built and shipped as DSOs (dylib/so/dll); static registration is a dev/migration mechanism with mandated parity | adopted direction |
| JA3 | Four module kinds: hosted languages; native IO/runtime modules; Radiant DOM API modules; data-pack extensions | adopted direction |
| JA4 | One host executable; bundles differ only by the module set beside it; hosts are byte-identical across bundles | adopted (Python carve-out precedent) |
| JA5 | One strict host-API tier for all modules: `jube.h` only, versioned additive-only, status codes + pending exceptions, no unwinding | adopted (landed ABI) |
| JA6 | Value/memory boundary: `Item` is the only value currency; native structs cross as VMap projections; system resources as integer rids; precise rooting via host APIs only | adopted direction |
| JA7 | Async boundary: modules are shielded from the async substrate — micro-kernel bridge + host-owned socket/process/signal ops; no loop escape hatch, ever | adopted direction (2026-07-26 review) |
| JA8 | Dependency direction: core never links modules; core→module calls exist only as registered hooks with absent defaults; inter-module deps declared in manifests | adopted direction |
| JA9 | Conformance mapping: every module kind names its gate — core=Test262, web modules=WPT slices, node modules=node-baseline, languages=their corpus; no module lands without its gate | **adopted** (review 2026-07-26) |
| JA10 | Trust tiers: system modules are in-process native, verified by static/binary conformance checks; **third-party native modules never share the runtime process** — they run out-of-process, and IO isolation is enforced by an OS sandbox on that process plus a brokered IO channel (the process boundary alone gives memory isolation, not IO isolation) | **revised twice** in review 2026-07-26 |
| JA16 | One central IO API over `./lib` and OS services: it is the **sole** IO door for Lambda, Radiant, and all Jube modules, and it carries IO policy (safety, privacy, realm/domain isolation) as well as IO mechanism | adopted direction (review 2026-07-26) |
| JA11 | Lifecycle & evolution: process-lifetime load, transactional registration, `runtime_reset`/`heap_cleanup` hooks; additive-only struct evolution behind `struct_size` gates | adopted (landed ABI) |
| JA12 | Data-pack extensions: payloads ride the manifest `resources` mechanism — hash-verified, lazily loaded/mapped, never staged through the GC heap | directional (needs own design before first adopter) |
| JA13 | Compiler/IR boundary: MIR is private in-memory IR, never distributed; hosted languages compile via the opaque cursor API; no module links `mir.h` | adopted (prior decisions) |
| JA14 | Jube serves both host domains — the Lambda/JS runtime and the Radiant engine — behind one registry/loader; host APIs are domain-specific, architecture rules are shared | adopted direction (review 2026-07-26) |
| JA15 | Lambda ↔ Radiant is two explicit contracts: Radiant embeds Lambda (in-process embed interface, Jube-aligned in spirit); Lambda reaches Radiant only through the `radiant-dom` Jube module | adopted direction (review 2026-07-26) |

---

## JA1. The core/module split: ECMA-262 in, platform out

**Decision.** The lambda/js core runtime keeps exactly:

1. **The language** — parser, AST, MIR JIT, and the built-ins and semantics of
   **ECMA-262** (plus ECMA-402 to the extent implemented);
2. **The engine substrate** — everything tightly bound to the machine and to
   memory/execution management: value model, **stack/GC/memory management**,
   the JIT, **SIMD and CPU-tight facilities** (typed-array vectorization and
   kin; GPU-tight facilities live on the Radiant side, JA14), **thread and
   process management primitives** (the work pool, worker machinery, spawn/
   reap mechanics that back the JA7 services), **core file I/O and the
   shared stream core** (the file/stream line below), the module-loading
   host hooks 262 defines, job/microtask queues, the event loop and its
   drain ordering, and the Jube registry/loader itself;
3. **The `./lib` utility tier** — simple, dependency-light utilities stay
   core library substrate: base64 and friends, simple URL parsing/encoding,
   string/scanner primitives. Being core here means host implementation
   substrate, not necessarily script-visible surface. **Complicated codecs**
   (image formats, full charset-conversion suites, media) do *not* qualify —
   they go to Jube modules (often as data packs, JA12);
4. **A small closed allowlist of universal globals** that every JS
   environment ships regardless of profile — **confirmed in review
   2026-07-26**: `console` (core, with module-refinable formatting), timers
   (`setTimeout` family), `queueMicrotask`.

Everything else — IO, protocols, platform APIs, host bindings — lives in Jube
modules. **fetch/http/net are expected to be Jube modules** (per review
direction and the node-hosting plan). **WebCrypto and Web Streams are the
canonical examples**: complete, self-contained specs, outside ECMA-262, with
their own conformance suite (WPT), needed by both browser-shaped and
Node-shaped surfaces — exactly the profile of a Jube IO module.

**Litmus test.** *Is it specified by ECMA-262, or required to implement 262's
host hooks? → core. Is it tightly bound to CPU/GPU, memory/stack/GC, or
thread/process management? → core substrate. Is it core file/stream I/O on
the hot path of language features (pipes, `input()`/`format()`)? → core
substrate. Is it a simple dependency-light utility? → `./lib`, core.
Otherwise → module, unless it is on the named globals allowlist above.* The
allowlist is closed; extending it is an ADR-level change to this document,
not a convenience call.

**File and stream I/O — where the line sits (review 2026-07-26).** This is
the hardest cut, so it is drawn explicitly. **Core file I/O and core stream
I/O stay in core, for performance** — Lambda's language-level pipe support
and `input()`/`format()` sit directly on them, and the shared stream core
(the K21–K27 substrate of the concurrency design, on which the pipeline
design builds its text/data/binary pipelines) is engine substrate in the
same sense the GC is. This also matches the host's own layering charter
(file/network IO consolidating into `lambda-io` per the static-modules
design). What goes to Jube is the layers **above** the substrate: exotic
file/stream features (fs-event watching machinery, platform-specific
extensions) and **spec-surface stream APIs** — the WHATWG Web Streams
classes (`web-streams` module) and Node's fs/stream API shapes (node-*
modules) are surfaces over the core substrate, not the substrate itself.
The `web-streams` decision below is unchanged by this: the module owns the
spec classes and their semantics; the plumbing they ride is core.

**Reference model.** V8 : Node/Deno :: lambda/js core : Jube modules. V8
ships the language and zero IO; Node and Deno supply the platform surface
around it. One deliberate difference: V8 has no event loop (each embedder
brings one), whereas our core *includes* the loop substrate — because 262's
job queues need a pump, the loop is shared by every module kind, and JA7
makes it the host's exclusive machinery. So: the *platform API surface*
follows Node/Deno out of the core; the *loop* stays in.

**Why.** Isolation in both directions. Core work (GC, JIT, IC, value model)
must not be destabilized by platform-API churn, and platform work must not
require touching — or re-testing — the engine. The core knows modules only
through the registry; modules know the core only through `jube.h` (JA5).
Each side's blast radius ends at that boundary. Secondary wins: the minimal
profile gets honest (a language runtime with no IO at all), heavy
dependencies leave the core link (mbedTLS, zlib, libcurl), and third parties
get a real extension surface.

**Consequences.**
- `web-crypto` and `web-streams` become standalone Jube modules of the
  web-platform flavor (JA3 kind 2a). This **supersedes** the placement in
  `Lambda_Design_Jube_Node_Hosting.md` JN14, where WebCrypto stayed
  host-side: the `js_crypto.cpp` split becomes three-way — `node-crypto`
  (module), `web-crypto` (module), and a shared mbedTLS primitive layer both
  link. Likewise `js_stream.cpp`'s WHATWG half leaves `node-core` scope:
  Node's `stream/web` re-exports the `web-streams` module, mirroring how
  Node itself layers over the WHATWG spec.
- `fetch`/http/net are expected Jube modules: browser `fetch`
  (`js_fetch.cpp` — takes libcurl out of the core link) alongside the
  node-hosting plan's `node-net`/`node-http`. Whether an HTTP module rides
  libcurl or the host socket services is a **module-level implementation
  decision, deliberately not made at the architecture level** — noted only
  so the current split (fetch on libcurl, `node:http` on host sockets)
  converges by conscious module design rather than by accident. Simple
  URL/encoding utilities stay in `./lib` (core) per the scope criteria; only
  spec-complete platform surfaces above them are module territory.
- `globalThis` composition varies by bundle. Accepted: the standard bundle
  ships the common module set so observable behavior is unchanged; only the
  minimal profile drops platform globals (that is its purpose).

## JA2. Modules are DSOs

**Decision.** A Jube module is built and distributed as a platform dynamic
library (`.dylib`/`.so`/`.dll`) beside the host, described by a `module.json`
manifest, hash-verified per platform, ABI/hosted-API/build-ID negotiated
before its code runs, and loaded lazily at first use. The `lang-python`
bundle (`modules/lang-python/module.json` + library, entry symbol
`jube_module`) is the reference implementation of the whole chain.

**Static registration is not a second product form.** In-tree modules may
register statically (`jube_register_static`) during development and staged
migration, with **mandated behavioral parity**: same `JubeModuleDef`, same
registry, same lifecycle — the loader path is the only difference. Every
static module carries a plan to flip to DSO (Python landed; node-* staged in
its N-plan; radiant-dom per POC 1).

**Consequences.** Compile-time consumers may rely only on what manifests
declare (no dlopen to answer "does X exist" — the node-hosting JN3 rule
generalizes to all kinds). The transitional `-undefined,dynamic_lookup`
symbol laxity is debt to close: the end state is modules importing host
symbols solely through the `jube.h` service tables (JA5), enforced by the
module architecture checkers.

## JA3. Four module kinds

**Decision.** A Jube module is one (or a composition) of:

1. **Hosted language** — a language front-end and its runtime helpers
   (`JubeLanguageDef`: sessions, run, module load), compiling through the
   opaque host compiler API (JA13). Exemplar: `lang-python`; future: Ruby,
   Bash.
2. **Native IO / runtime module** — script-visible namespaces, functions,
   and types (`JubeNamespaceDef` + `JubeTypeDef`/`JubeFuncDef`), in two
   flavors that share mechanics but differ in spec authority and gate:
   - **2a web-platform modules** — implement a W3C/WHATWG spec: `web-crypto`,
     `web-streams` (exemplars); candidates: fetch, URL, encoding.
   - **2b node-compat modules** — implement Node's API surface: `node-core`
     and the dependency-keyed leaves (`node-fs`, `node-net`, `node-http`,
     `node-tls`, `node-crypto`, `node-zlib`, `node-child-process`).
3. **Radiant DOM API module** — the bridge that surfaces the in-process
   Radiant engine to scripts: DOM/CSSOM types as VMap projections with
   `interface_decl` + binding-table dispatch (the DOM3 mechanism).
   Exemplar: `radiant-dom` (native-module design POC 1). Radiant itself
   stays in-process; the module is the interface.
4. **Data-pack extension** — native code plus large data payloads:
   spell-check (hunspell + dictionaries), font/text shaping (HarfBuzz +
   font tables), hyphenation patterns, and similar. Code follows kind-2
   mechanics; the payload follows JA12. These exemplars are
   **Radiant-domain modules** (JA14): HarfBuzz extends Radiant's text
   pipeline, not the Lambda language surface.

**Kinds are orthogonal to host domain (JA14).** A module declares which
domain(s) it serves — the Lambda/JS runtime, the Radiant engine, or both —
and receives the corresponding host API. Kind 3 is inherently dual-domain
(Radiant surfaces projected to scripts); kind 4 is most often Radiant-domain;
kinds 1 and 2 are Lambda-domain.

| Kind | Descriptor mechanics | Discovery trigger | Conformance gate (JA9) | Exemplar |
|---|---|---|---|---|
| 1 hosted language | `JubeLanguageDef` + runtime-import catalog | CLI lang/extension dispatch; import bridge | language's own corpus (e.g. the Python suite) | `lang-python` |
| 2a web-platform | namespaces/types; installs globals lazily | global/namespace touch; `import` | **WPT slice imported with the module** | `web-crypto`, `web-streams` |
| 2b node-compat | namespaces/types; specifier index | `require`/`import` fallback | `make node-baseline` | `node-core`, `node-fs` |
| 3 radiant DOM | `interface_decl` + type bindings over VMaps | document/page bring-up | editor + UI-automation suites; WPT DOM infra | `radiant-dom` |
| 4 data pack | kind-2 code + manifest `resources` | first API use; lazy pack load | pack-specific corpus + integrity checks | spellcheck (planned) |

**Why four and not one.** The kinds differ in the *host services they
consume* (compiler API vs runtime services vs an in-process engine bridge vs
bulk data) and in *what proves them correct*. Naming the kinds keeps both
the API growth (JA5) and the test obligations (JA9) honest per kind, while
one registry, one manifest format, and one loader serve all of them.

## JA4. One host executable; bundles are packaging

**Decision.** There is exactly one host: `lambda.exe`. Distributions —
minimal (host only), standard (host + common modules), full (host +
everything) — differ solely in the modules beside the binary, and the host
binaries are **byte-identical** across all bundles (verified in packaging,
as `release-standard`/`release-jube` already are). No preprocessor product
splits in core files; composition lives in manifests and packaging scripts.
`lambda-jube.exe` remains a compatibility symlink, never a separate build.

## JA5. One strict host-API tier

**Decision.** Modules — including in-tree ones — see exactly one header:
`jube.h` (languages additionally `jube_language.h`). The contract, already
landed and exercised by Python, DOM3, and the demo modules:

- versioned, additive-only service tables, each carrying
  `api_version` + `struct_size`, size-gated on both sides;
- capability bits negotiated before module code runs; incompatibility is a
  clean refusal, not a partial load;
- **errors are return values**; exceptions *pend* (`throw_value` /
  `check_exception`); nothing unwinds across the boundary;
- no module imports host-internal symbols; the per-kind architecture
  checkers enforce the boundary (the hosted-Python checker is the template;
  node-* adds `uv_*` to the banned list per JA7);
- host build-ID pinning applies only to build-coupled surfaces (the hosted
  compiler API), not to the stable base ABI.

There is deliberately **no second, softer tier** for "trusted in-tree"
modules — in-tree modules are the constant test of the same contract
third parties get.

## JA6. The value and memory boundary

**Decision.**

- **`Item` is the only value currency** across the boundary, constructed and
  inspected through the host value/script/data tables. No engine-internal
  layouts (`js_runtime.h` IC/shape details, `Context` internals) cross.
- **Native structs cross as VMap projections** (`JubeTypeDef`): brand =
  vtable identity; finalization = sweep-time `destroy`; raw pointers are
  never script-visible.
- **System/kernel resources cross as integer rids** in the host resource
  table (JA7) — the fd model, not the pointer model.
- **Rooting is precise and host-mediated**: `JubeHostGcAPI` root frames,
  roots, weak refs, and persistent session roots only. No module declares
  `heap_register_gc_root` externs or engine `RootFrame`/`Rooted` types.
  (Consistent with the engine-wide rule: conservative stack scanning is
  retired and stays retired.)
- Marking-path vtable ops and finalizers are allocation-free and never
  re-enter script.

## JA7. The async/concurrency boundary

**Decision** (fixed in the 2026-07-26 node-hosting review; recorded here as
the architecture-wide rule for *all* module kinds):

- Modules are **fully shielded from the async substrate**. No libuv type,
  symbol, or loop pointer crosses the boundary; there is **no escape hatch**
  (Node-API's `napi_get_uv_event_loop` is the named anti-pattern).
- The uniform surface is a **micro-kernel of five concepts** (~15 entries):
  scheduling (nextTick/microtask/timers), thread-safe completion post
  (`napi_threadsafe_function` pattern), a blocking-work pool
  (`napi_create_async_work` pattern), the rid resource table (Deno
  `ResourceTable` pattern), and shutdown registration.
- Dedicated ops exist **only where the host must own the machinery**:
  socket/pipe streams (readiness-vs-IOCP portability), process spawn/reap
  (single SIGCHLD owner), signal subscription. Blocking-syscall domains
  (fs, dns, zlib, crypto CPU work) ride the generic work pool — which is
  libuv's own internal strategy for them.
- Completions are delivered **only on the JS thread, in loop order**, with
  the host draining nextTick/microtasks after each batch. Modules never
  pump, poll, or drain.

**Why at the architecture level.** This is not a node-* implementation
detail: web-streams needs the same scheduling bridge, data-pack extensions
need the same work pool (shaping, dictionary loads), and the radiant module
needs the same completion discipline. One async model, host-owned, for every
kind — and the substrate (libuv today) stays swappable without touching any
module.

## JA8. Dependency direction and layering

**Decision.**

- **Core never links modules.** Where core behavior is refined by a module
  (console formatting, globals installation, IPC accept, shutdown
  participation), the mechanism is a hook the module registers at init, and
  the host has a defined absent-module default. Link-time `extern`s from
  core into module code are defects to burn down (the node-hosting §10
  inversion is the template).
- **Modules depend only on `jube.h`** (JA5) — never on each other's symbols.
  Inter-module needs are declared in the manifest (`dependencies`) and
  resolved by the registry at activation, or expressed as script-level
  re-export (Node's `stream/web` over `web-streams`).
- The host's own internal layering (`lambda-lib` → `lambda-data` →
  `lambda-rt` → radiant, with IO consolidating per the static-modules
  design) is orthogonal to Jube but must stay consistent with it: what
  leaves the core for a module also leaves the corresponding static
  library's charter.

## JA9. Conformance mapping — every module kind names its gate

**Decision.** A surface is gated by the suite that owns its spec, and **a
module may not land without its gate wired into CI**:

| Surface | Spec authority | Gate |
|---|---|---|
| core runtime | ECMA-262 (+402 as implemented) | Test262 via `test/js262` |
| web-platform modules (2a) | W3C / WHATWG | **imported WPT slice** (e.g. `WebCryptoAPI/`, `streams/`), shipped/updated with the module |
| node-compat modules (2b) | Node.js documented behavior | `make node-baseline` (no-regress) |
| hosted languages (1) | the language's own spec/tests | the language corpus (e.g. the Python suite + goldens) |
| radiant DOM (3) | DOM/CSSOM specs | editor + UI-automation baselines; WPT DOM infra |
| data packs (4) | pack-specific | pack corpus + integrity/loading tests |

**Why.** The review finding that motivated this: WebCrypto and Web Streams
sit outside ECMA-262, so Test262 structurally cannot cover them, and the
in-tree Node suite barely touches them (`crypto_random.js` and one BYOB
test) — the exact surfaces JA1 sends to modules were the ones with no net.
Under JA9 the module and its suite arrive together, so extraction *adds*
coverage instead of losing it. Precedent: Node vendors WPT slices into its
own test tree for the same reason.

## JA10. Trust tiers, integrity, and the limits of enforcement

**The load-bearing fact.** An in-process native DSO has **full ambient
authority over the process**. It can issue raw syscall instructions (no
libc, no symbol, nothing to whitelist), `dlopen`/`dlsym` by computed name,
write directly to the host's policy tables or function pointers, and
`mmap`+`mprotect` its own code. Therefore **no userspace API — including
JA16's — can contain native code that does not wish to be contained**, and
no static verification of a native binary can prove otherwise (declared
imports are the only thing a symbol checker sees; one level of indirection
defeats it). This is not a gap to be closed by better checking; it is a
property of loading native code into your address space. Every system that
needed real containment answered it the same way: a different execution tier
(process boundary + OS sandbox, or a capability-secure VM), never binary
inspection.

**Decision — three tiers, mechanism matched to trust:**

| Tier | Who | Mechanism | IO enforcement |
|---|---|---|---|
| **T1 system** | first-party in-tree modules; signed partner modules | in-process native DSO (JA2), admitted by **static + binary conformance verification** (below) | **by discipline**: JA16 API + checkers + review. Catches careless and buggy IO, not adversarial IO |
| **T2 confined** | third-party native modules | **separate process** (never the runtime process) + **OS sandbox on that process** + **brokered IO** over the Jube IPC channel | **enforced by the OS** — all three parts required |
| **T3 sandboxed** | untrusted / freely distributed extensions — **the recommended default** for untrusted code (portability argument below) | WASM in-process: no syscall instruction exists in the ISA, memory is bounded, declared imports are the only egress | **enforced by construction** |

### T1 verification — what the static check does and does not prove

For system modules the check is **conformance verification, not safety
proof**, and that is the correct job: first-party code is not adversarial, so
the checks have essentially no false negatives against the failure mode that
actually occurs — someone reaching for `open()`/`uv_fs_*`/`socket()` because
it was convenient. Two layers, both cheap, both already precedented in-tree
by the hosted-Python architecture checker (which validates source *and*
binary, including `--require-module-binary`):

- **Source-level**: no forbidden includes (engine internals, libuv, raw
  POSIX IO headers); IO reached only through the JA16 API.
- **Binary-level**: the dynamic import table contains only allowlisted
  symbols; no `dlopen`/`dlsym`; a disassembly scan finds no raw `syscall`/
  `svc` instructions and no directly linked IO from a statically bundled
  dependency.

Both are **defeatable by intent** and neither upgrades T1 into a security
boundary — a T1 module is trusted exactly as much as code linked into the
host. What they buy is a ratchet: the architecture cannot erode silently,
and accidental IO becomes a build failure rather than a code-review miss.

### T1 under worker-thread isolation (added 2026-07-26 — concurrency K31)

Tier-2 workers may run as **thread isolates in the runtime process**
(`start worker(spec, isolation: 'thread')`, `Lambda_Design_Concurrency.md`
§10.3). This does not change the trust tiers, but it does change what T1
admission has to check, in two ways:

- **A T1 module's globals become shared concurrent state.** One DSO is
  loaded once per *process*, not once per isolate, so every thread isolate
  in the process shares its statics, caches, and lazily-initialized tables.
  A module that was correct under one context can be wrong under several.
  Therefore T1 admission gains a **thread-isolation-safety declaration** in
  the module's FFI contract (alongside the blocking/await-safety and
  async markers); an undeclared module is **process-isolation-only**, and
  loading it into a thread isolate is a load-time error rather than a race.
- **T1's blast radius grows with isolate count.** A T1 fault already takes
  the process down; under thread isolation it now takes down every page or
  worker sharing it. This strengthens rather than changes the tiering rule:
  untrusted code is never T1, so untrusted code is never in a thread isolate
  — it is T2 (its own process) or T3 (WASM). "Untrusted ⇒ process
  isolation" falls straight out of the load-bearing fact above.

### T2 — what a separate process does and does not buy

**A separate process alone does *not* enforce IO isolation.** It has its own
ambient authority under the same user credentials: it can `open()` any file
that user can read, connect anywhere the network allows, and `exec` other
programs. What the process boundary *does* give — and this is substantial —
is **memory isolation**: the module cannot corrupt the runtime heap, patch
policy tables or function pointers, read secrets out of host memory, or take
the process down with it (crash isolation).

IO isolation needs **three parts together**:

1. **The process boundary** — memory isolation and a place to apply policy.
2. **An OS sandbox applied to that process** — this is what actually removes
   the ambient authority. Mechanisms are per-platform (§ below).
3. **A capability broker** — the sandboxed process starts with *no* file or
   network access and receives **pre-opened descriptors** over the IPC
   channel for exactly what policy grants. The host stays the only thing
   that ever resolves a path or opens a socket; the module holds handles it
   was given, never names it can ask for. This is why JA16 is a
   precondition, not a companion: the broker *is* the IO API.

This is Chrome's renderer model, and the honest lesson from it: **the IPC
surface becomes the new attack surface.** Sandbox escapes in practice are
broker/IPC bugs, not sandbox-policy bugs. So the Jube-over-IPC protocol must
be written to be memory-safe against a hostile peer — every message length,
index, and handle reference validated, no trust in peer-supplied offsets.

**Consequences that shape which modules can be T2:**

- **The in-process ABI does not survive the boundary.** JA5/JA6 assume a
  shared address space: `Item` values, VMap projections over native structs,
  and host root frames are all pointer-based. T2 therefore needs a
  *different interface shape* — a marshaled wire protocol, not the same
  descriptor tables. The JA7 design translates well (integer rids cross a
  process boundary natively; the request/completion model is already
  message-shaped); VMap projections and direct Item access do not. T2 is a
  second interface style, not a loading option for the same module.
- **Latency cost picks the tier.** A per-call process hop is fine for
  coarse-grained work (spell-check a document, decode an image) and
  unacceptable for fine-grained work — text shaping called per glyph run
  during layout is the sharpest example, which is precisely a JA12 data-pack
  case. Bulk data can ride shared memory (font tables, glyph buffers,
  decoded bitmaps), but chatty per-call interfaces cannot be rescued that
  way. **This is why T3 remains in the model**: WASM gives capability
  isolation at in-process call cost, so a latency-sensitive untrusted module
  belongs in T3, not T2.
- **Lifecycle grows**: process spawn cost, restart/recovery on crash, and a
  defined behavior when the module process dies mid-request (the JA7
  canceled-completion path generalizes).

### T2 platform backends — and why they argue for T3 as the default

| Platform | Mechanism | Signing required? |
|---|---|---|
| Linux | seccomp-bpf syscall filter + user namespaces / `pivot_root` (or Landlock for path rules) + `no_new_privs` + dropped capabilities | no |
| macOS | **Seatbelt**: `sandbox_init` / `sandbox_init_with_parameters` with an SBPL profile | **no** (see below) |
| Windows | AppContainer + restricted token + job object | no |

seccomp-bpf cannot dereference pointers, so it cannot filter by pathname —
which is precisely why part 3 (the broker) is structural rather than
optional.

**macOS without code signing — yes, via Seatbelt.** The entitlement-based
**App Sandbox** requires signing, but it is not the only mechanism.
`sandbox_init(3)` applies a profile to the *calling* process at runtime, with
no entitlement and no signature: either a built-in profile
(`kSBXProfilePureComputation` is close to what a confined compute module
wants — no file, no network) or a custom SBPL profile string. **Chrome's
macOS renderer sandbox works exactly this way** — Chromium is not App-
Sandboxed; it applies custom SBPL profiles via Seatbelt — which is the
existence proof that this is viable at production scale.

Caveats to accept knowingly: `sandbox_init` has been formally deprecated
since 10.8 while remaining the backbone of Apple's own service confinement
(the OS ships SBPL profiles for its daemons), so it works but carries
removal risk; SBPL itself is undocumented and its details shift between
releases. Keep the profile minimal (deny-by-default, allow the IPC channel
and nothing else) so the exposure to SBPL drift stays small. Note separately
that shipping any macOS app to users involves signing and notarization for
Gatekeeper anyway — what Seatbelt buys is not "never sign", it is "no App
Sandbox entitlement and no entitlement-gated frameworks", which keeps the
sandbox usable in development and in unsigned builds.

**Containers and light container runtimes — not the mechanism for this.**
Docker/podman/containerd are the *same Linux primitives* listed above
(namespaces + cgroups + seccomp + capabilities + LSM), packaged with image
distribution and a daemon. They add no security model we would not already
be implementing, and they bring three disqualifying properties for in-app
module confinement:

- **Linux-only.** On macOS and Windows, Docker runs a Linux VM (Virtualization
  .framework / WSL2 / Hyper-V). "Sandbox a module with Docker" there means
  "ship a Linux VM inside a desktop app" — and a macOS-native module cannot
  run in a Linux container regardless. Apple's newer container tooling is
  likewise Linux-containers-in-a-VM, not a way to confine native macOS code.
- **Deployment-tier, not library-tier.** A daemon, image management, and
  root or a rootless setup are dependencies an embedded runtime cannot take
  on to sandbox one extension.
- **Not a stronger boundary.** A container shares the host kernel; kernel
  exploits escape it. The genuinely stronger options are microVMs
  (Firecracker, Kata) or user-space kernels (gVisor) — heavier still.

Where containers/microVMs *are* the right answer: **server-side, multi-tenant
deployment** — running untrusted third-party modules as a hosted service. If
that use case ever arrives it becomes a deployment tier above T2, not a
replacement for it.

**Consequence — T3 is the better default untrusted tier.** T2 needs three
OS-specific sandbox backends (one of them on a deprecated API), a capability
broker, a hostile-peer-safe IPC protocol, a marshaled interface distinct
from the in-process ABI, and per-call latency. T3 needs **one** embedded
WASM runtime that behaves identically on all three platforms, with no
signing, no VM, no OS-specific backend, and in-process call cost. WASM is
the only *portable* sandbox available to us. So the working recommendation
is: **T3 first for untrusted extensions; T2 as the escape hatch** for
modules that genuinely cannot be compiled to WASM (large existing native
dependencies, threads, platform APIs) and are coarse-grained enough to
absorb IPC latency. This inverts the earlier emphasis and should be
revisited with real requirements before either is built.

**T1 is not a security boundary, and the document must not pretend
otherwise.** Its integrity chain (manifest identity, per-platform SHA-256,
ABI/hosted-API/build-ID negotiation, transactional registration, clean
refusal on failure) answers *"is this the artifact we shipped?"* — a supply
chain and compatibility question. It does not answer *"can this artifact
misbehave?"* Loading a T1 module is a decision equivalent in trust to
linking it into the host.

**Third-party native modules therefore never enter the runtime process.**
Whitelisting declared imports is not verification, so the tier changes
instead of the checking: third-party native code goes to T2, whose
enforcement comes from the OS rather than from inspecting a binary. The
intuition that a Jube module should behave "like a freely compiled WASM
module" is exactly right — and where in-process call latency is required,
the way to get that property is to *actually run it as one* (T3), because
WASM's guarantee comes from its runtime, not from binary inspection.

**Process-level hardening applies to all tiers** as defense in depth: the
host may install an OS sandbox profile over itself, which constrains T1
modules too, regardless of their cooperation. Granularity is process-wide
(and path-level policy needs a broker), so it complements rather than
replaces the tiering.

**Status.** T1 exists today (its verification layers are staged work). T2 and
T3 are **directional** — neither is built, and neither is required until
there is a real third-party module story. What this ADR fixes now, while it
is cheap, are two rules: **third-party native code never loads into the
runtime process**, and **a process boundary is not by itself an IO
boundary** — T2 means process + OS sandbox + broker, or it means nothing. A
WASM-tier assessment already exists (`vibe/Lambda_KIV_WASM.md`) but studied
WASM→MIR *as a guest language*; sandboxing would instead embed an
off-the-shelf WASM runtime — a different proposition that the KIV verdict
does not settle.

**Permissions.** The runtime permission model (`--allow-fs`/`--allow-net`
style gates) is policy inside JA16 and the JA7 resource table. Against
scripts it is a real boundary (JA16); against T1 modules it is policy, not
containment; in T2/T3 the OS or the VM makes it binding.

## JA16. The central IO API

**Decision.** All IO in the unified runtime goes through **one host-owned IO
API**, and nothing else opens a file, a socket, or a device:

1. **Raw, lowest-level IO is isolated to `./lib` and the libraries the host
   links** (`lib/file.c`, `lib/url.c`, `lib/uv_loop.c`, libcurl, OS
   services). These are mechanism, not policy, and they are not called
   directly from anywhere above.
2. **An official IO API sits over that layer** — the single contract for
   files, streams/pipes, sockets, DNS, processes, and devices. It is the
   same host-owned surface JA7 already defines for async and the resource
   table: **one chokepoint, not two**. The `lambda-io` charter of the
   static-modules design is its natural home, with the refinement that
   event-loop-coupled language IO consumes the API rather than reimplementing
   it.
3. **Lambda, Radiant, and every Jube module call only this API.** No ad-hoc
   `open`/`socket`/`uv_*`/POSIX calls above the `./lib` layer. For modules
   this is the JA5 boundary already (and the node-hosting checker's banned
   `uv_*` list is the first instance of enforcing it); for core code it is a
   burn-down (`js_fs.cpp`'s direct `uv_fs_*`, `js_os.cpp`'s raw POSIX,
   `js_net.cpp`'s socket calls are today's violations).
4. **The API carries policy, not just mechanism**: safety (path and
   capability gates), privacy (what may be read, what may leave the
   machine), realm/domain isolation (per-page, per-realm, per-tenant
   scoping — the Radiant multi-document case), plus auditing, quotas, and
   diagnostics. Policy lives *at* the chokepoint because that is the only
   place it can be complete.

**What this genuinely enforces — and what it does not.**

| Actor | Ambient authority? | Is JA16 a boundary? |
|---|---|---|
| Lambda / JS / hosted-language **scripts** (incl. untrusted web content in Radiant) | none — they reach only what the runtime exposes | **Yes — a real security boundary.** Complete, enforceable, and the case that matters most for privacy and realm isolation |
| **T1 native modules** | full | **No — governance, not containment.** Prevents careless/accidental IO; a determined module bypasses it |
| **T2 / T3 extensions** | none (OS- or VM-constrained) | **Yes — enforced by the OS or the VM** |

**Why build it anyway, given T1's limit.** Because containment was never its
main job:
- it is the **only** way the policy layer (privacy, realm isolation,
  permissions) can be implemented completely rather than per-call-site;
- it makes **untrusted scripts** — the actual multi-tenant threat, including
  remote web content — genuinely confinable;
- it is the audit, logging, quota, and diagnostics chokepoint;
- it makes IO **testable and mockable** (deterministic tests, fault
  injection);
- it keeps the substrate swappable, exactly as JA7 does for async;
- it converts "did someone sneak in an `open()`?" from an unanswerable
  question into a checker rule;
- and it is the precondition for T2/T3 ever working: brokered capabilities
  need a broker, and this is it.

**Consequences and open work.** The API surface must be extracted
empirically (JA5's rule) from the real call sites — `lib/file*`, the
resource/network stack, and the JA7 op families — rather than designed
speculatively; the core burn-down above is staged work with its own plan;
and the policy model (what gates exist, how realms are named, how
capabilities are granted and revoked) is a design of its own, listed as
open item 8.

## JA11. Lifecycle and ABI evolution

**Decision.**

- Modules load for **process lifetime**; no hot unload (`shutdown` is
  best-effort at teardown). Hot *reload* of script-level code is a separate,
  existing workstream and does not go through Jube.
- Registration is **transactional**: a failed `init`, interface compile, or
  descriptor validation rolls back to the pre-load state.
- Per-runtime lifecycle rides two descriptor hooks: `runtime_reset` (batch
  reruns recreate globals; module caches must drop) and `heap_cleanup`
  (per-heap teardown before the heap dies; also where resource leak
  accounting settles).
- Evolution is **additive-only**: new fields append behind `struct_size`
  gates (the `JUBE_MODULE_DEF_V1_SIZE` precedent); new services append as
  table pointers; behavior never changes under an existing entry —
  replacements get new entries plus capability bits.

## JA12. Data-pack extensions (directional)

**Decision (direction; needs its own design before the first adopter).**
Large payloads — dictionaries, font tables, shaping data — ride the existing
manifest `resources` mechanism, extended with:

- per-resource integrity hashes alongside the library hash;
- lazy loading with a preference for **read-only mapping** (mmap-style)
  over heap staging: pack data must never be copied through the GC heap;
- packs versioned *with* their module (one manifest, one compatibility
  negotiation) while remaining separately downloadable in bundles — subset
  packaging (per-locale dictionaries, per-script font data) is the expected
  shape;
- pack absence degrades the feature, never the load: a module whose optional
  pack is missing registers with reduced capability and reports it, mirroring
  JN5's clean-failure rule.

Exemplars queued behind this: spell-check (hunspell-class engine +
dictionaries), text shaping (HarfBuzz + font data), hyphenation — all
**Radiant-domain** modules (JA14), so the pack design lands together with
the Radiant host-API extraction. The first adopter writes the detailed
design; this ADR fixes only the constraints above.

## JA13. The compiler/IR boundary

**Decision (recorded from prior designs; unchanged).** MIR is a **private,
in-memory IR** — never a distribution or interchange format; source remains
the specification level for script packages. Hosted-language modules compile
through the **opaque cursor compiler API** (contexts, modules, functions,
labels, instructions as host-owned handles; no `MIR_*` types or `mir.h`
inclusion module-side — the hosted-Python H7 boundary). Compiled-script
caching is a local, derived cache keyed by host build ID + sources + flags
(the MIR-cache design), invisible to the module contract.

## JA14. Jube serves the whole unified runtime — Lambda and Radiant alike

**Decision.** Jube is the module system of the **unified runtime**, not of
the Lambda/JS engine alone. Behind the one registry, loader, manifest
format, and verification chain sit **two host service domains**:

- the **Lambda/JS runtime domain** — value model, script namespaces, the
  hosted-compiler API, async services: everything JA5–JA7 describe;
- the **Radiant engine domain** — services for layout/text/render
  extensions: text shaping (HarfBuzz + font data), hyphenation, spell-check
  for editing, image/media codecs, and eventually pluggable render/paint
  backends.

A module's manifest declares which domain(s) it serves; the host API it
receives at init is **domain-specific** and the two APIs may be very
different — the Radiant surface is extracted from real usage on its own
schedule (empirically, as the Lambda tables were), not force-fitted into the
Lambda service shapes. What is **shared and non-negotiable across domains**
is the architecture: DSO form + manifest + integrity + negotiation (JA2),
the single strict tier and additive versioning (JA5, JA11), value/rooting
rules wherever Items are involved (JA6), shielded async and work-pool access
(JA7), dependency direction (JA8), a named conformance gate (JA9), and the
trust model (JA10).

**Why.** Radiant has the same extension pressures Lambda has — heavy
optional dependencies (shaping engines, codecs), large data payloads, and
third-party extension interest — and inventing a second module system for
it would duplicate the loader, the integrity story, the versioning
discipline, and the packaging, while inevitably diverging on the rules that
matter. One architecture, two service domains.

**Consequences.** HarfBuzz/text-shaping is a Radiant-domain module with its
font-data pack (JA12) — not a Lambda module; spell-check likewise (serving
the Radiant editor first). Bundle composition (JA4) extends naturally:
minimal Radiant profiles become expressible the same way minimal JS
profiles are. The Radiant host-API extraction is its own follow-on design
(open item 6).

## JA15. The Lambda ↔ Radiant boundary

**Decision.** The relationship between the two engines is bidirectional,
with **two different, explicit contracts** — one per direction:

- **Radiant embeds Lambda** (the embed direction). Radiant drives script
  execution — pages as Lambda isolates, document scripting, state
  regeneration — through an in-process embedding interface on the runtime.
  This is **not a Jube interface in exact shape**: it is statically layered
  (`lambda-lib` → `lambda-data` → `lambda-rt` → radiant, per the
  static-modules design), with no manifest, dlopen, or hash step. But it
  must **align with Jube in spirit**: a versioned, explicit contract surface
  with the same additive/size-gating discipline, errors as return values,
  precise rooting at every boundary crossing, no reach into engine internals
  beyond the contract, and capability-style negotiation where the surface
  evolves. The analogy: Radiant is to Lambda what an embedder is to V8 —
  isolates, contexts, and handles across a deliberate interface — with the
  difference that the event loop stays in the runtime core (JA1) and is
  shared, not embedder-owned.
- **Lambda reaches Radiant only through Jube** (the module direction).
  Scripts touch DOM/CSSOM via the `radiant-dom` module (JA3 kind 3, the
  native-module design's POC 1). `lambda-rt` never links radiant; the static
  layering stays acyclic, and the reverse edge exists only as surfaces the
  module registers.

**Why two contracts instead of one.** The directions differ in nature.
Embedding is engine-to-engine composition — in-process,
performance-critical, lifecycle-entangled (documents, isolates, frames,
navigation) — while script-visible DOM is a platform API surface exactly
like every other module namespace. Forcing both through the Jube mechanism
would either bloat the module ABI with embedding concerns or under-specify
the embed; forcing both into ad-hoc internal linkage is the monolith this
architecture exists to end. Aligning them in spirit keeps both sides
auditable by the same principles (JA5/JA6/JA8) without pretending they are
the same mechanism.

**Consequences.** The embed contract gets formalized as its own follow-on
design (open item 7), building on the decisions already made in
`Radiant_Design_Concurrency.md` (pages as Lambda isolates, same-thread
script+layout) and `Radiant_Design_State_Management.md` (persistence by
regeneration). The `radiant-dom` module remains the sole script-side door
(its migration plan is POC 1's).

---

## Supersessions

| Where | What changes |
|---|---|
| `Lambda_Design_Jube_Node_Hosting.md` JN14 | WebCrypto no longer "stays host-side": it becomes the `web-crypto` Jube module (JA1/JA3-2a). The `js_crypto.cpp` split is three-way — `node-crypto` module, `web-crypto` module, shared mbedTLS primitive layer. Marked in place. |
| `Lambda_Design_Jube_Node_Hosting.md` §6.1 (`node-core` contents) | `stream/web` leaves `node-core`: it becomes a re-export of the `web-streams` module (JA1). Marked in place. |
| `Lambda_Design_Native_Module.md` §5.2 sketch | The landed `jube.h` (module def at `lambda/jube/jube.h:1120`, entry symbol `jube_module`) is authoritative over the proposal-era struct sketches; the doc's decisions stand, its code sketches do not. |

## Open items

1. **Allowlist conformance audit (JA1)**: the allowlist itself is confirmed
   (console/timers/queueMicrotask, review 2026-07-26); what remains is the
   mechanical audit of `js_globals.cpp` global installation against it —
   anything installed beyond the allowlist and the 262 surface moves to a
   module kind.
2. **fetch/http/net module shaping (JA1)**: direction is decided (Jube);
   open is only the module grouping. The libcurl-vs-host-sockets choice is a
   module-level decision per review 2026-07-26 — carried as a note in JA1's
   consequences, not as an architecture question.
3. **WPT harness**: pick the import/update mechanism for module WPT slices
   (JA9) — reuse the Radiant-side WPT infrastructure vs a JS-runner harness
   like the js262 one; decide before `web-streams`/`web-crypto` land.
4. **Data-pack design (JA12)**: full design doc gated on the first adopter;
   lands together with the Radiant host-API extraction (JA14, item 6).
5. **Symbol-isolation closure (JA2/JA5)**: retire `dynamic_lookup` laxity —
   shared burn-down with hosted-Python H8.
6. **Radiant-domain host API (JA14)**: extract the Radiant service surface
   empirically from the first Radiant-domain adopters (text shaping,
   spell-check) — its own design doc.
7. **Lambda ↔ Radiant embed contract (JA15)**: formalize the embedding
   interface (isolates/documents/lifecycle, versioned headers, rooting
   rules) as its own design doc, building on the concurrency and
   state-management decisions.
8. **IO API + policy model (JA16)**: its own design doc — surface extracted
   empirically from `lib/file*`, the resource/network stack, and the JA7 op
   families; the policy model (gate taxonomy, realm naming, capability grant
   and revocation, audit format); and the staged burn-down of core's direct
   `uv_*`/POSIX/socket calls. The node-* checker's banned `uv_*` list is the
   first enforcement instance.
9. **Confined/sandboxed tiers (JA10 T2/T3)**: not needed until a real
   third-party module story exists, but the evaluation should happen before
   one arrives — out-of-process Jube-over-IPC with a capability broker vs an
   embedded WASM runtime (note: `Lambda_KIV_WASM.md` assessed WASM→MIR as a
   *guest language*; sandboxing is a different proposition its verdict does
   not settle). Working recommendation recorded: **T3-first, T2 as escape
   hatch** — WASM is the only portable sandbox, while T2 needs three OS
   backends + broker + wire protocol. Sub-items when it starts: the
   **marshaled T2 interface shape** (the in-process Item/VMap ABI cannot
   cross a process boundary; JA7's rid model can), per-platform sandbox
   profiles (macOS = Seatbelt SBPL, no signing needed, accept the
   `sandbox_init` deprecation risk), the hostile-peer-safe IPC protocol,
   shared-memory paths for bulk data, and module-process lifecycle/recovery.
   Containers/microVMs are assessed as a *server-side deployment* tier, not
   an in-app confinement mechanism.
10. **T1 verification layers (JA10)**: implement the source + binary
    conformance checks (allowlisted dynamic imports, no `dlopen`/`dlsym`,
    disassembly scan for raw syscall instructions) as an extension of the
    existing architecture-checker family; wire into `make build-test` per
    module kind.
