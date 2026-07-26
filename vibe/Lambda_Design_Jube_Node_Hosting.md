# Jube Hosted Node Architecture

> **Status:** design proposal (2026-07-25; §3.1 + §9/JN7/JN8 revised in
> review 2026-07-26 — modules fully shielded from the async substrate)
>
> **What:** migrate the built-in Node.js compatibility layer (`lambda/js/js_fs.cpp`,
> `js_http.cpp`, `js_crypto.cpp`, …) out of the monolithic host into Jube native
> modules, packaged and distributed like `lang-python`
>
> **First adopters:** `path` (static pipe-cleaner), `node-zlib` (first dynamic
> module), `node-fs`
>
> **Related designs:**
> - `vibe/Lambda_Design_Native_Module.md` — the approved native-module design;
>   its **POC 2** ("node-* — Node compat as modules") is the direct ancestor of
>   this document
> - `vibe/Lambda_Design_Jube_Lang_Hosting.md` — the hosted-language architecture
>   (Python); this document reuses its principles and delivery machinery but is
>   **not** a language hosting
> - `vibe/Lambda_Impl_Hosted_Python.md` — the staged migration template (H0–H10)
> - `doc/Lambda_Jube_Runtime.md` — user-facing runtime/bundle description

All file:line references were verified against master on 2026-07-25 and will
drift; treat them as anchors, not contracts.

## 1. Decision summary

Node support becomes a set of Jube native modules beside the host, discovered
and verified exactly like `modules/lang-python`, while the JavaScript engine
itself stays in the host binary:

```text
lambda.exe                       (host: Lambda + JS engine + Jube registry/loader + event loop)
modules/
  node-core/    module.json + node-core.dylib      (events, stream, buffer, util, path, …)
  node-fs/      module.json + node-fs.dylib        (fs, fs/promises)
  node-net/     module.json + node-net.dylib       (net, dns)
  node-http/    module.json + node-http.dylib      (http, https)
  node-tls/     module.json + node-tls.dylib       (tls; mbedTLS)
  node-crypto/  module.json + node-crypto.dylib    (node:crypto; mbedTLS)
  node-zlib/    module.json + node-zlib.dylib      (zlib)
  node-child-process/ …                            (child_process)
```

| ID | Decision (proposed) |
|---|---|
| JN1 | Node compat migrates to Jube **native modules** using the `namespaces`/`types` capabilities of `JubeModuleDef`. It is not a hosted language: `JubeLanguageDef` is unused, and the JS engine remains in-host. The service direction is the inverse of Python's — the engine calls into the module. |
| JN2 | Granularity: one `node-core` module (the dependency hub: events/stream/buffer/util/path/… + process extensions + internalBinding) plus **leaf modules keyed by external dependency and libuv cluster** (`node-fs`, `node-net`, `node-child-process`, `node-http`, `node-tls`, `node-crypto`, `node-zlib`). |
| JN3 | Manifest schema gains a non-language module kind: `"kind": "runtime-library"`, `"engine": "js"`, and a `"provides"` specifier list. Integrity/ABI fields (`base_abi_version`, `hosted_api_version`, `host_build_id`, `sha256_*`, `entry_symbol`) are reused unchanged. Builtin-ness must be decidable from manifests alone — **no dlopen at compile time**. |
| JN4 | Resolution: a registry **specifier index** replaces `js_module_get_builtin`'s memcmp chain and all four duplicated builtin-name lists. `require`/`import` fall back to the registry, which dlopen-loads the owning module lazily on first touch. `node:`-prefix and `.js`-suffix aliasing is normalized once in the loader. |
| JN5 | Absent module ⇒ the Node-shaped `Cannot find module 'x'` error (`code: 'MODULE_NOT_FOUND'`) plus a host `log_*` diagnostic naming the missing Jube module. Never a link failure. A host with no Node modules at all must still run non-Node JS. |
| JN6 | The host keeps: the libuv loop and JS event-loop layer, microtask/nextTick queues, global timers, console core, the process-object core, the JS module cache, the npm resolver (initially), and `node:vm` (initially). `node-core` owns: process extensions, `internalBinding`, the stub namespaces, `timers/promises`, and the Node error-code helpers. |
| JN7 | The host API grows **one additive `JubeHostNodeAPI` parent** composing four versioned service tables — async request/resource operations, binary (Buffer/typed arrays), promise, and Node-error services — the Node analog of Python's compiler API (`JubeGuestExecutionAPI`), with v1 extracted empirically from what `fs`/`net`/`child_process` actually use (§9). JS value mechanics stay on the existing script/value/data tables (the `js_native_api.h` vs `node_api.h` split, §3.1). |
| JN8 | **Modules are shielded from the async substrate entirely**: no libuv type, symbol, or loop pointer crosses the module boundary (`uv_*` is on the module checker's banned-symbol list). Modules submit requests to host-owned async services and hold opaque integer resource ids; completions arrive on the JS thread with host-owned drain ordering. libuv remains a host implementation detail, swappable without touching any module. *(Revised 2026-07-26 from an earlier shared-libuv-substrate proposal; minimized 2026-07-26b to a two-tier surface — a ~15-entry micro-kernel (scheduling, thread-safe completion post, blocking work pool, resource table, shutdown) plus dedicated ops only for sockets/process/signals. fs, dns, zlib, and crypto ride the generic work op with plain syscalls, which is how libuv implements them internally anyway.)* |
| JN9 | GC across the boundary uses `JubeHostGcAPI` root frames/roots and persistent roots **only**. Direct `heap_register_gc_root*` externs, `RootFrame`/`Rooted`, `js_heap_epoch`, and `Context` field access are banned by the module architecture checker. The conversion is also the fix vehicle for the known unrooted-native-`Item`-locals class. |
| JN10 | The core→builtin **backward calls invert into init-time hooks** with absent-module defaults: console formatter, shutdown participants, the IPC socket-handoff, and globals installation (including the `Buffer` global). Active-handle enumeration needs no hook — it reads the §9.1 resource table. |
| JN11 | Lifecycle: namespace objects stay cached in the existing JS module cache per runtime epoch; modules implement the already-present `runtime_reset` (batch-mode global recreation) and `heap_cleanup` (per-heap teardown) descriptor hooks. |
| JN12 | Distribution: the **standard bundle ships `node-core`** (observable JS behavior is unchanged) with the heavy leaves optional; the full bundle ships all node modules; an embedded/minimal profile ships none. All bundles carry the byte-identical host binary, extending the existing `release-standard`/`release-jube` identity rule. |
| JN13 | Migration proceeds through stages N0–N7 (§11); `make node-baseline` (1492/3517 today) is an every-stage no-regress gate, and MIR emission for non-Node scripts must stay within existing `test/mir` budgets. |
| JN14 | `js_crypto.cpp` splits first. *(Superseded in part 2026-07-26 by `Lambda_Design_Jube_Architecture.md` JA1/JA3: WebCrypto no longer stays host-side — it becomes its own `web-crypto` Jube module. The split is therefore three-way: `node-crypto` module, `web-crypto` module, and a shared mbedTLS primitive layer both link.)* |

## 2. Goals and non-goals

### 2.1 Primary goals

1. **One host, different bundles.** The same `lambda.exe` serves a minimal
   embedded profile, the standard bundle, and the full bundle; only the modules
   beside it differ (mirrors the hosted-Python product rule).
2. **Kill the monolith's worst maintainability debt.** One registry replaces
   the hand-written memcmp dispatch chain and its three duplicated name lists.
3. **Make heavy dependencies optional.** A deployment that needs only `path`
   and `fs` should not carry mbedTLS-bound `crypto`/`tls` (~11.8k lines) or
   `http` (~6.9k).
4. **Open the door to out-of-tree Node-API-shaped modules** without touching
   the tree: the same descriptor a built-in uses is the third-party surface.
5. **Force an honest engine API.** Every service the builtins consume becomes
   an explicit, versioned host-API entry instead of an `extern` into engine
   internals — the same discipline the Python carve-out is imposing on the
   compiler side.
6. **Fix rooting by construction.** Migrating each builtin behind
   `JubeHostGcAPI` retires its ad-hoc `heap_register_gc_root` externs and
   unrooted `Item` locals (the forced-GC divergence class).

### 2.2 Non-goals

- Changing Node compatibility semantics or coverage (node-baseline must hold;
  growing it is separate work).
- Hosting a second JS engine or making the JS engine itself loadable.
- Sandboxing: node modules remain trusted native code (same trust model as all
  Jube modules).
- Hot unload; modules load for process lifetime (existing Jube rule).
- Migrating the browser/DOM/Web surface (`js_dom*`, `js_fetch`, `js_canvas`, …
  ≈27k lines) — that is POC 1 (`radiant-dom`) territory, a separate track.
- Moving the event loop out of the host.

## 3. What "like Python" means here — and where Node differs

The delivery machinery is identical; the runtime relationship is inverted.

| Aspect | `lang-python` (hosted language) | `node-*` (runtime library) |
|---|---|---|
| Module kind | `JubeLanguageDef` (sessions, run, load_module) | `namespaces` + `types` capability tables |
| Who calls whom | Module calls host services (compiler, execution, module graph) | Host engine calls module (require, method dispatch, hooks) |
| Host API centerpiece | `JubeGuestExecutionAPI` — opaque MIR compiler surface | New JS runtime service tables (async/binary/promise/error, §9) |
| Discovery trigger | CLI language/extension dispatch (`jube_discover_hosted_language`, `lambda/jube/jube_registry.cpp:3564`) | `require`/`import` builtin-specifier fallback (new, §8) |
| Absent-module behavior | generic hosted-language diagnostic | Node-shaped `MODULE_NOT_FOUND` error (JN5) |
| Packaging, manifest, integrity, negotiation, loader, static/dynamic symmetry, byte-identical host | **identical — reused as-is** | **identical — reused as-is** |

The review ladder from the hosted-language design applies verbatim to every
facility a node module wants from the engine: (1) use an existing common
facility; (2) promote/generalize it into the unified runtime; (3) expose it
through a versioned extension API; (4) keep an adapter inside the module;
(5) reject the reuse. No `node`-specific branch may be added ad hoc to core
files.

### 3.1 Prior art: how Node and Deno couple engine, runtime, and loop

**V8 embedding (both engines).** V8 hands the embedder an Isolate (one heap +
GC), Contexts (realms), stack-scoped `Local<>` handles inside `HandleScope`s,
rooted `Global`/`Persistent` handles, a pending-exception `TryCatch` model, and
a `v8::Platform` interface the embedder implements to supply task runners. The
**embedder owns the event loop** and decides when microtasks drain; Node runs
one `uv_loop` per `Environment` and drains nextTick + microtasks after each
C++→JS callback batch (`InternalCallbackScope`/`MakeCallback`). That
"completion, then drain" chokepoint is exactly what our host already implements
(`js_event_loop.cpp` + the `lambda_uv_set_microtask_drain` hooks) —
independent validation, nothing to change.

**Node core is the cautionary tale, not the model.** Node's own builtins (fs,
net, http, …) are C++ files bound *directly* to V8 and libuv, registered
through an `internalBinding` registry, with a JS layer (`lib/*.js` over
"primordials") on top; libuv handle lifetime is managed by `HandleWrap`/
`ReqWrap` wrapper classes woven into the GC. Structurally that is the same
monolith we are migrating away from — and its consequence is well documented:
native addons that coupled to those internals broke on every major release
(the NAN recompile era) until Node introduced a deliberate boundary.

**Node-API is the boundary Node learned to build.** The supported addon ABI
(N-API) is: opaque `napi_env`/`napi_value` handles, status-code returns with a
pending-exception model (`napi_throw` / `napi_is_exception_pending` — no
unwinding), handle scopes, `napi_ref` for persistence, versioned additive-only
evolution — and, critically, its supported async story **hides libuv**:
`napi_create_async_work`/`napi_queue_async_work` run blocking work on the
runtime's pool and complete on the JS thread, and `napi_threadsafe_function`
posts calls onto the JS thread from anywhere. A raw `napi_get_uv_event_loop`
escape hatch exists and is the acknowledged wart — addons that reach for it
are the ones that break in workers and embedded hosts. The header split is
equally instructive: `js_native_api.h` (pure engine/value API, engine-portable)
vs `node_api.h` (runtime services). Our existing script/value/data tables are
already the first half; JN7 adds the second half without reproducing the
escape hatch.

**Deno never exposed the loop at all.** `deno_core` reaches V8 from Rust and
gives extension code exactly one bridge: **ops** (`#[op2]`-generated glue;
eligible sync ops ride V8's Fast API calls) plus a **ResourceTable** — kernel
objects (files, sockets, child processes) live host-side keyed by opaque
`u32` resource ids; JS and extension code hold only rids; async ops are Rust
futures the runtime schedules on tokio, and completion resolves the op's
promise; `close(rid)` drops the resource and cancels its pending ops. Deno
also learned to keep generic serialization off the boundary (the serde_v8 cost
drove op2's direct value access) — for us: keep Item-native signatures, never
add a marshaling layer.

Two structural facts about Deno matter when borrowing its shape. First,
`deno_core` itself ships almost no I/O: each extension (`deno_fs`,
`deno_net`, …) implements its own ops directly on tokio/std, and the runtime
standardizes only the generic stream verbs (`op_read`/`op_write`/`op_close`/
`op_shutdown` over the `Resource` trait) — creation ops are per-extension.
Second, that "extensions just use tokio" freedom exists because Deno has **no
stable native-module ABI at all**: extensions compile into the binary in one
Rust build (FFI is a user-code mechanism, not how builtins are made), so
nothing crosses a versioned boundary. Across our C-ABI dylib boundary, the
translation of Deno's shape is: a tiny uniform bridge (scheduling +
thread-safe completion + work pool + resource table) plus uniform stream
verbs — which is what §9.1 specifies.

Lessons adopted in this design: (1) the value-API vs runtime-services split
(JN7); (2) opaque handles + status codes + pending exceptions — already house
rules, here independently validated; (3) **resource-id tables instead of
handle pointers** (JN8), which also yields central shutdown, active-handle
enumeration, leak accounting, and a permission-audit chokepoint for free;
(4) completions delivered on the JS thread with host-owned drain ordering;
(5) a generic blocking-work op instead of any module-visible threading
primitive; (6) no loop escape hatch — `napi_get_uv_event_loop` is the
anti-pattern this design deliberately omits.

## 4. Verified starting point (2026-07-25)

### 4.1 Inventory

21 dedicated builtin files in `lambda/js/`, **≈55.4k lines** (.cpp only), each
exposing a `js_get_<name>_namespace()` entry:

| File | Lines | File | Lines |
|---|---|---|---|
| `js_stream.cpp` (stream + stream/web/promises/consumers/iter) | 10,111 | `js_dns.cpp` | 1,991 |
| `js_crypto.cpp` (node:crypto **mixed with WebCrypto**) | 8,725 | `js_readline.cpp` | 1,878 |
| `js_assert.cpp` (assert + node:test) | 6,240 | `js_zlib.cpp` | 1,429 |
| `js_http.cpp` (own HTTP/1.1 parser, raw libuv) | 6,175 | `js_url_module.cpp` | 870 |
| `js_net.cpp` | 5,883 | `js_events.cpp` | 846 |
| `js_fs.cpp` | 3,919 | `js_os.cpp` | 803 |
| `js_child_process.cpp` | 3,755 | `js_https.cpp` | 686 |
| `js_buffer.cpp` | 3,152 | `js_path.cpp` | 669 |
| `js_tls.cpp` | 3,076 | `js_querystring.cpp` | 462 |
| `js_util.cpp` | 2,943 | `js_permission.cpp` | 402 |
| | | `js_string_decoder.cpp` | 139 |

Node code embedded in core files (**≈3.7k lines**): the `process` object
(`js_globals.cpp:2700–4470`, ≈1,770), the builtin dispatcher itself plus inline
stub namespaces (`js_runtime.cpp:38863–39812`, ≈950), `internalBinding` and its
constant tables (`js_runtime.cpp:38328–38594`), `node:vm`
(`js_runtime.cpp:33777+`), and the `ERR_*` error-code helpers
(`js_runtime.cpp:23279–23450`). Adjacent: `lambda/npm/` (3,056 lines, bare
specifier resolution/install) and `lib/uv_loop.c` (138 lines, the process-global
loop). Rough split of `lambda/js/`: Node-specific ≈26%, core engine ≈57%,
browser/DOM ≈12%.

### 4.2 Resolution paths today

- `js_module_get_builtin(Item)` at `js_runtime.cpp:38863–39812`: a hand-written
  if/else chain doing three `memcmp`s per module (bare, `.js`, `node:`), with
  getters re-declared `extern` inline at each call site.
- Call sites: CJS `js_require` (`lambda/js/js_mir_entrypoints_require.cpp:1858`,
  builtin check at `:1868` before any disk lookup); ESM/cache `js_module_get`
  (`js_runtime.cpp:39814`); MIR lowering intercepts literal `require("…")`
  (`js_mir_expression_lowering.cpp:6862–6896`) and `import` lowers to
  `js_module_get` (`js_mir_statement_lowering.cpp:6110`).
- **Four independently maintained name lists**: the chain itself;
  `builtinModules` for `node:module` (`js_runtime.cpp:39209`);
  `js_module_is_builtin` (`js_runtime.cpp:39839`); and the lowering-time
  skip-list `builtin_names[]` (`js_mir_module_batch_lowering.cpp:1930`), plus
  `node:`-prefix special cases in path resolution
  (`js_mir_entrypoints_require.cpp:1581`,
  `js_mir_module_batch_lowering.cpp:1927/:1983`).

### 4.3 Coupling inventory

- **Internal headers:** `js_runtime.h` (1,164 lines: IC layouts, tag-encoding
  asserts, map-kind inlines) is the de-facto API; `js_runtime_state.hpp` (the
  mutable runtime-state capsule) is included by 8 builtins (net, fs,
  child_process, buffer, dns, events, assert, querystring).
- **GC internals:** locally re-declared `heap_register_gc_root(_range)` externs
  (`js_net.cpp:34` registering raw socket arrays at `:120`,
  `js_child_process.cpp:54`, `js_dns.cpp:28`, `js_os.cpp:21`, `js_tls.cpp:33`,
  and `js_fs.cpp:1989` *inside a function body*); direct `RootFrame`/`Rooted`
  use (`js_fs.cpp:2529`, `js_http.cpp:5955` with 10 roots,
  `js_child_process.cpp:3731`); direct `Context`/heap access
  (`js_net.cpp:4591`); namespace caches keyed on the collector generation
  `js_heap_epoch` (`js_runtime.cpp:39204`); raw `heap_calloc`/`heap_create_name`
  + `s2it` inlined thousands of times.
- **Event loop:** builtins call libuv directly — `uv_spawn`
  (`js_child_process.cpp:542`), `uv_tcp_init`/`uv_write`/`uv_read_start`
  (`js_net.cpp:2175/:1038/:1939`), `uv_accept` (`js_http.cpp:3357`), `uv_fs_*`
  (`js_fs.cpp:717–750`), `uv_timer_*` (`js_dns.cpp:405`) — on the process loop
  from `lib/uv_loop.c`; and call JS-level queues ~40× (`js_setTimeout` from
  `js_child_process.cpp:1013`, `js_next_tick_enqueue` from `js_fs.cpp:1080`),
  several through locally re-declared prototypes (`js_child_process.cpp:41`,
  `js_fs.cpp:3127`).
- **Backward calls (core → builtins):** `console.log` → `js_util_format`
  (`js_globals.cpp:5990`); `process._getActiveHandles` → js_net
  (`js_globals.cpp:3714`); shutdown → `js_net_close_all_active_servers`
  (`js_runtime.cpp:35790`); cluster-online queueing (`js_runtime.cpp:62`); a raw
  `uv_pipe_t*` IPC handoff in a header-free extern (`js_globals.cpp:138`);
  `js_buffer_from_bytes` (`js_globals.cpp:1256`, `js_runtime.cpp:33793`); and
  unconditional installation of `process`/`Buffer`/`os`/`vm` onto `globalThis`
  (`js_globals.cpp:16009–16096`).
- **Inter-builtin:** tls→net (`js_tls.cpp:29/:3027`), stream→events
  (`js_stream.cpp:9922`), https→http, fs/net/dns/http→`js_permission.*`,
  nearly all→`js_error_codes.h` + the `ERR_*` throwers.

### 4.4 External dependencies

Only **libuv**, **zlib** (`js_zlib.cpp` only), and **mbedTLS** (`js_tls.cpp`,
`js_crypto.cpp`), plus **OpenSSL as an existing runtime `dlopen` soft-dep** for
PFX/PKCS12 (`js_tls.cpp:1190–1206`). There is **no libcurl** (curl is
`js_fetch.cpp` — browser surface) and **no c-ares** (`cares_wrap` is a shim
over libuv getaddrinfo, `js_runtime.cpp:38524`). Raw POSIX/BSD/mach calls in
os/net/dns/child_process. No `#ifdef` or runtime flag disables Node today;
`js_permission.*` gates what builtins may do, not whether they exist.

### 4.5 Existing Jube infrastructure reused as-is

- **ABI** — `JubeModuleDef` (`lambda/jube/jube.h:1120`) already carries the
  needed capabilities: `namespaces` (`JubeNamespaceDef` with specifier tables +
  lazy `build()`, `jube.h:277`), `types` + DOM3 `interface_decl`/
  `type_bindings` for class-shaped surfaces, `runtime_reset`, `heap_cleanup`.
  Entry symbol `jube_module`; static/dynamic registration symmetry
  (`jube_register_static` path used by `radiant`/`hostobj_demo`).
- **Loader** — manifest discovery, SHA-256 verification, ABI/hosted-API/build-ID
  negotiation, transactional registration with rollback
  (`jube_load_dynamic_module_checked`, `lambda/jube/jube_registry.cpp:2963`;
  manifest scan `:3217–3250`).
- **Host services** — `JubeHostAPI` (`jube.h:976`) with `gc` (roots, frames,
  weak refs), `value` (objects/arrays/properties), `script` (functions,
  prototypes, `throw_value`/`check_exception` pending-exception model,
  `call_function`, error construction), `data` (neutral names/maps/floats).
  The `dom` table is the working proof that a large domain-specific service
  surface can live behind `JubeHostAPI`.
- **Delivery** — the `lang-python` build-target shape
  (`build_lambda_config.json:304`: dynamic, PIC, `target_dir modules/…`,
  `-Wl,-undefined,dynamic_lookup`), `make build-/release-lang-python`
  (`Makefile:772/:783`), manifest stamping
  (`utils/update_jube_manifest_integrity.py`), standard/full package split with
  the byte-identical-host check, the loader-negative test matrix
  (`utils/test_jube_module_loader_negative.py`), and the architecture-checker
  methodology (`utils/check_hosted_python_architecture.py`,
  `utils/check_module_boundary.py`).

Known caveat: the Python module itself still imports internal host/library
symbols (H8 is not closed). Node modules must not inherit that laxity; §10's
checker is a required gate from stage N1, and tightening it also serves the
Python closure work.

## 5. Governing principles

### P1. One host, different bundles
Same as hosted-language P1. No preprocessor Node switch is added to core files;
composition is packaging.

### P2. The engine consumes modules through one registry
Lambda, JS, and future front-ends resolve module-provided namespaces through
the Jube registry. The JS engine holds no builtin name knowledge of its own
once migration completes.

### P3. Modules see only `jube.h`
No `js_runtime.h`, `js_runtime_state.hpp`, `lambda-data.hpp` internals, GC
internals, or `Context` layout — and no libuv either. After the JN8 revision
there is no deliberate widening at all: the async substrate is reached only
through the §9.1 request API, and `uv_*` symbols sit on the module checker's
banned list alongside the engine internals.

### P4. No cost when absent, no behavior change when present
A bundle without a node module pays nothing for it and fails cleanly (JN5).
A bundle with the module must be observably identical to today's monolith —
node-baseline is the arbiter.

### P5. Services are extracted from real usage
Each new host-API entry (§9) is justified by a named call site in the migrating
builtin, mirroring how the hosted-language API v1 was derived. No speculative
surface.

### P6. Errors return, exceptions pend
No C++ unwinding across the module boundary. Handlers return status codes and
use the pending-exception model already established by the DOM3 binding tables
(`jube.h` `JubeMemberBind` contract) and `JubeHostScriptAPI.throw_value`/
`check_exception`.

### P7. Static first, dynamic second, behaviorally identical
Every module lands first as a statically registered `JubeModuleDef` (same
descriptor, same registry, no dlopen), then flips to an external dylib — the
same checkpoint discipline as hosted Python (P9 there, H3→H8 here).

### P8. The finalizer rules apply
`heap_cleanup`, vmap `destroy`, and anything on the marking path must not
allocate or re-enter script (native-module design §6.3 rule).

## 6. Target system

### 6.1 Module granularity (JN2)

Grouping is by dependency cluster and external library, so that optionality
falls on the heavy edges:

| Module | Contents (today's files) | ≈Lines | External deps | Depends on |
|---|---|---|---|---|
| `node-core` | events, stream (+promises/consumers/iter; `stream/web` re-exports the `web-streams` module per `Lambda_Design_Jube_Architecture.md` JA1), buffer, util (+types/inspect), path (+posix/win32), url, querystring, string_decoder, os, assert (+node:test), readline, permission glue; process extensions, `internalBinding` + constant tables, stub namespaces (cluster/worker_threads/v8/tty/…), `timers/promises`, Node error-code helpers | ≈30k | — | host APIs only; `web-streams` module |
| `node-zlib` | zlib | 1.4k | zlib | node-core |
| `node-fs` | fs, fs/promises, FileHandle | 3.9k | libuv | node-core |
| `node-net` | net, dns (+cares_wrap shim) | 7.9k | libuv | node-core |
| `node-child-process` | child_process (+cluster-online glue) | 3.8k | libuv | node-core, node-net (IPC) |
| `node-http` | http, https | 6.9k | libuv | node-core, node-net |
| `node-tls` | tls | 3.1k | mbedTLS (+dlopen'd OpenSSL) | node-core, node-net |
| `node-crypto` | node:crypto after the WebCrypto split (JN14) | ≈6–7k | mbedTLS | node-core |

Rationale for a fat `node-core` rather than 21 micro-modules: events/stream/
buffer/util are the dependency hub of everything else (stream→events,
everything→buffer, assert→util-inspect), they have no external libraries, and
per-piece optionality among them buys nothing — nobody ships `stream` without
`events`. The optionality wins live at the leaves (mbedTLS, zlib, sockets,
subprocesses). A single all-in-one `node` module was considered and rejected:
it would forfeit the "minimal host without crypto/http" goal that motivates
POC 2. Splitting `node-net` further (dns separate) is possible later; the
manifest `dependencies` field already expresses inter-module needs.

### 6.2 Manifest schema (JN3)

`module.json` gains a runtime-library kind alongside the language kind; all
verification fields are shared:

```json
{
  "name": "node-fs",
  "version": "0.1.0",
  "base_abi_version": 2,
  "hosted_api_version": 1,
  "host_build_id": "lambda-hosted-lang-20260724-h7e38",
  "kind": "runtime-library",
  "engine": "js",
  "provides": ["fs", "fs/promises", "internal/fs/promises", "internal/fs/utils"],
  "dependencies": ["node-core"],
  "resources": [],
  "library_macos": "node-fs.dylib",
  "library_linux": "node-fs.so",
  "library_windows": "node-fs.dll",
  "entry_symbol": "jube_module",
  "sha256_macos": "…"
}
```

- `provides` lists **canonical** specifiers; the loader derives the `node:`
  and `.js` alias forms by rule (JN4). Specifiers that exist only in prefixed
  form (`node:test`) are written prefixed and not de-aliased.
- `kind`/`engine` let the registry maintain two indices over one manifest set:
  the existing language/alias/extension index and the new specifier index.
  A manifest with neither `language` nor `provides` is rejected at scan time.
- `dependencies` is enforced at load: activating `node-http` activates
  `node-net` and `node-core` first (the loader's existing dependency-path
  machinery, `jube_manifest_loading_paths`, covers cycles/depth).
- The standard bundle may ship **manifest-only descriptors** for absent leaves
  (as hosted Python does), which upgrades JN5's diagnostic from "unknown
  module" to "known module, not installed" in the host log — the script-visible
  error stays the Node-shaped one.

### 6.3 Discovery and loading (JN4, JN5)

Manifests under `modules/` (and the existing host-module-root override) are
scanned once, lazily, on the first specifier miss — the same
scan-on-first-need pattern as `jube_discover_hosted_language`
(`jube_registry.cpp:3571`). Scanning parses manifests only; **dlopen happens on
first `require`/`import` of a provided specifier** (or first touch of a
registered global/hook the module declares). Compile-time consumers (§8) query
the manifest-built specifier index and never trigger a load.

Load failure (hash mismatch, ABI/build-ID mismatch, missing library, failed
`init`) is a handled discovery result: the specifier resolves to the
Node-shaped `MODULE_NOT_FOUND` error with a one-line host diagnostic naming
the module and reason. Registration remains transactional (existing rollback).

### 6.4 What stays in the host (JN6)

- **The event loop**: `lib/uv_loop.c` + `lambda/js/js_event_loop.cpp`
  (microtasks, nextTick, timer wheel, rAF, virtual clock, bounded drains).
  It serves browser surfaces (fetch, timers, rAF) and Lambda concurrency
  regardless of Node's presence. (Its eventual home per the static-modules
  layering — SM12 puts IO in `lambda-io` — is orthogonal to this design.)
- **Global timers** (`setTimeout` et al. as globals) and the microtask/nextTick
  machinery — Web-platform surface, not Node-only. `timers/promises` (the
  namespace) moves to `node-core`.
- **`process` core**: identity, `argv`/`env`, exit/exception plumbing that the
  engine itself needs even in browser-profile runs. `node-core` extends it at
  init (nextTick binding, `memoryUsage`, signals, `_getActiveHandles`
  providers, …). The exact split line is enumerated during N3 from the
  `js_globals.cpp:2700–4470` inventory; the rule: anything the engine calls on
  its own paths stays, anything only Node scripts observe moves.
- **`console` core** with a formatter hook (§10) — degraded but functional
  formatting without `node-core`.
- **The JS module cache** (`js_modules[]`) and CJS/ESM semantics — engine
  behavior. The registry only supplies namespace objects to cache.
- **npm resolver** (`lambda/npm/`) — initially host (it is wired into
  compile-time module-batch resolution). Revisit after N5; it is only
  meaningful with Node semantics, so `node-core` is its natural later home.
- **`node:vm`** — implemented against engine internals
  (`js_runtime.cpp:33777+`); moving it requires compiler-service exposure that
  nothing else needs. Stays host, revisit last.
- **WebCrypto** (`globalThis.crypto`) — *(superseded)* originally retained
  host-side; per `Lambda_Design_Jube_Architecture.md` JA1/JA3 it becomes the
  `web-crypto` Jube module (WPT-gated), and `node:crypto`'s webcrypto surface
  re-exports it (JN14).

## 7. Architecture layers

Mirroring the hosted-language layer stack:

- **Layer A — Jube base ABI** (reused unchanged): `JubeModuleDef`, capability
  tables, negotiation, transactional registration, static/dynamic symmetry.
- **Layer B — unified runtime services** (reused): `JubeHostGcAPI` (roots,
  frames, weak), `JubeHostValueAPI`, `JubeHostScriptAPI` (functions,
  prototypes, pending exceptions, calls), `JubeHostDataAPI` (neutral data).
- **Layer C — JS runtime services for native modules** (**new**, §9): async/
  event-loop, binary, promise, and Node-error tables. This is the counterpart
  of Python's Layer C: where a hosted language needed the *compiler* as a
  service, a runtime library needs the *running engine* as a service.
- **Layer D — the node-* modules**: namespace factories, class bindings via
  `interface_decl` + `JubeTypeBinding` where surfaces are class-shaped
  (Socket, Server, ChildProcess, Hash, FileHandle …), plain namespace `build()`
  functions where they are bags of functions (path, querystring, os).

## 8. Resolution rewiring (JN4)

Replace the four name lists with one specifier index:

1. **Registry index.** At manifest scan, each `provides` entry (plus statically
   registered modules' `JubeNamespaceDef.specifiers`) is inserted into a
   hashmap `specifier → module slot`. Alias normalization (strip `node:`, strip
   `.js`) happens at insert and lookup, so each module lists each surface once.
2. **`js_module_get_builtin` becomes a thin registry query**: look up the
   normalized specifier; if the owning module is not yet initialized, dlopen +
   init it (lazy, JN5 on failure); call the namespace `build()` (or return the
   cached Item). The ~950-line chain and its inline `extern` re-declarations
   are deleted at the end of N3.
3. **Compile-time consumers query the same index without loading**:
   `jm_resolve_module_path`'s skip-list (`js_mir_module_batch_lowering.cpp:1930`)
   and `js_module_is_builtin` become `jube_specifier_is_builtin()` over the
   manifest index. `builtinModules` (`node:module`) is generated from the index
   so reflection matches reality per bundle.
4. **Lowering is untouched semantically**: the literal-`require` intercept
   (`js_mir_expression_lowering.cpp:6862`) still emits the same `js_require`
   call; only `js_require`'s builtin branch changes internals. Emission for
   existing scripts is unchanged, keeping `test/mir` budgets neutral (JN13).
5. **Caching**: the returned namespace Items keep flowing into the existing
   `js_modules[]` cache; per-epoch invalidation moves from ad-hoc
   `js_heap_epoch` checks inside getters to the descriptor's `runtime_reset` +
   `heap_cleanup` hooks (JN11), which the registry already invokes for the
   DOM3/interface caches (`jube_interface_runtime_reset`).

Cost: one hash lookup per first-require of a specifier, then cache hits —
strictly better than today's linear memcmp chain. Method calls on namespace
members are unchanged (plain function Items). Class-shaped members dispatch
through the compiled interface records (`jube_member_call`), the mechanism
already carrying DOM at cost parity.

## 9. The JS runtime service surface (JN7, JN8)

One additive pointer on `JubeHostAPI` — a composed `JubeHostNodeAPI` parent —
carries everything a node module needs beyond the existing value/script/data
tables, mirroring how `hosted_language` composes its service tables:

```c
struct JubeHostNodeAPI {
    uint32_t api_version; uint32_t struct_size; uint64_t capabilities;
    const JubeHostAsyncAPI*     async_ops;  // §9.1 — requests, resources, scheduling
    const JubeHostBinaryAPI*    binary;     // §9.2
    const JubeHostPromiseAPI*   promise;    // §9.3
    const JubeHostNodeErrorAPI* error;      // §9.4
};
```

This is the `js_native_api.h` vs `node_api.h` split (§3.1) applied to our ABI:
JS value mechanics stay on the existing tables; runtime services live here.
Sketches are directional; v1 signatures are extracted in N2 from the actual
call sites in fs/net/child_process/dns (P5). House rules throughout:
`api_version` + `struct_size` first, status-code returns, pending exceptions,
borrowed Items rooted before allocating operations.

### 9.1 Async services: micro-kernel + socket tier (JN8)

Modules are fully shielded from the async substrate. No libuv type, symbol,
or loop pointer crosses the module boundary; `uv_*` joins the checker's
banned-symbol list. Revised again in review (2026-07-26b) toward minimality:
the uniform surface is a **micro-kernel of five concepts**, and dedicated I/O
ops exist **only where the host must own the machinery**. Everything else
rides the generic blocking-work op — which is exactly how libuv itself
implements those areas internally.

**Tier 1 — the micro-kernel bridge (~15 entries, 5 concepts).** Sufficient on
its own for most modules:

| Concept | Entries | Precedent |
|---|---|---|
| scheduling | `next_tick`, `enqueue_microtask`, `timer_start`, `timer_clear` | Node `MakeCallback` drain discipline; existing host queues |
| thread-safe completion post | `post_completion(cb, user_data)` — deliver a callback onto the JS thread from any thread, in loop order, with the host draining nextTick/microtasks after each batch | `napi_threadsafe_function` |
| blocking work | `work_submit(fn, complete, user_data)`, `work_cancel` — run `fn` on the host pool, `complete` on the JS thread | `napi_create_async_work`; libuv's own threadpool |
| resource table | `rid_add(ptr, close_fn)`, `rid_get`, `rid_close`, `rid_ref`, `rid_unref` — opaque uint32 ids for host-tracked long-lived objects | Deno `ResourceTable`; the fd model |
| lifecycle | `register_shutdown` | napi cleanup hooks |

**Modules that need no other ops at all**: `node-fs` (minus watch), `node-dns`
side of node-net, `node-zlib`, `node-crypto` — a blocking syscall or CPU job
on `work_submit` with completion on the JS thread **is** libuv's own internal
strategy for these (`uv_fs_*` and `uv_getaddrinfo` are threadpool + blocking
call, nothing more). Wrapping each verb as a host op would add ABI without
adding capability. tty raw mode is a synchronous `termios`/ioctl call the
module makes directly — no async ABI involved. `fs.watch` is the one fs edge
that genuinely needs platform event machinery (kqueue/FSEvents/inotify/
ReadDirectoryChangesW): host-implemented behind two ops, or deferred if
node-baseline shows it cold.

**Tier 2 — dedicated ops only where the host must own the machinery
(~15–18 entries):**

| Family | Ops (sketch) | Why it cannot ride the work pool |
|---|---|---|
| stream (tcp + pipe unified) | connect / listen / accept → rid; read_start / read_stop; write; shutdown; opts (nodelay, keepalive); fd adoption; IPC handle send / receive → rid | readiness (kqueue/epoll) vs completion (IOCP) portability is the reason libuv exists; per-connection blocking reads would starve the pool |
| process | spawn (argv/env/cwd/stdio spec referencing rids + ipc) → rid; kill; exit completion | child reaping needs exactly one SIGCHLD owner in the process — the host |
| signal | signal_start / signal_stop → rid | same single-owner argument (host already owns handlers) |

This is also precisely Deno's split: `deno_core` standardizes only the
generic stream verbs (`op_read`/`op_write`/`op_close`/`op_shutdown`
dispatching through the `Resource` trait), while resource *creation*
(open/connect/listen) lives in each extension. Creation stays per-domain,
verbs stay uniform, and nothing else is runtime API.

v1 contracts (deliberate simplifications, each with a marked extension
point):

- **Write payloads are copied at submit.** No cross-ABI buffer pinning or
  lifetime contract in v1; a pinned zero-copy variant is a later additive
  entry, adopted only if profiles demand it (§13 risk 8).
- Completion callbacks may submit new requests but must not block or nest a
  drain.
- `close(rid)` implicitly cancels that resource's in-flight requests —
  completions fire with a canceled status (Deno's close semantics);
  request-level cancel exists where the substrate supports it.
- Completion payloads are op-specific C structs valid only during the
  callback; the module builds Items from them (fs.Stats, Buffers) via the
  value/binary tables — Node semantics stay module-side.

**Rejected further step — the pure micro-kernel** (Tier 1 only, ~15 entries,
the Node-API addon model where modules run their own socket poller threads
and post back via `post_completion`): rejected because it relocates
poller/threading/handle-lifecycle complexity into `node-net`/`node-http` —
the exact bug classes this migration evicts — duplicates a poller per
module, makes Windows IOCP a module problem, and splits SIGCHLD ownership.
The micro-kernel remains sufficient for every module that isn't
socket/process-shaped, which is most of them.

Total: ≈30 entries built from five concepts — down from the earlier ~50-op
draft, half the size of the in-tree `JubeHostDomAPI`, and the module-facing
conceptual load matches Deno's bridge (ops + rids + scheduling). The
decoupling claim is unchanged: the host can swap libuv for direct
kqueue/io_uring or another scheduler without touching any module.

### 9.2 Binary data

```c
struct JubeHostBinaryAPI {
    uint32_t api_version; uint32_t struct_size;
    Item (*buffer_alloc)(size_t len, uint8_t** out_bytes);   // Node Buffer
    Item (*buffer_from_copy)(const uint8_t* bytes, size_t len);
    uint8_t* (*buffer_bytes)(Item buf, size_t* out_len);
    Item (*arraybuffer_alloc)(size_t len, void** out_data);
    Item (*typed_array_view)(int kind, Item arraybuffer, size_t offset, size_t len);
    int  (*is_buffer)(Item v);
};
```

Replaces the backward `js_buffer_from_bytes` externs in core once `Buffer`
itself lives in `node-core`: core code that needs byte objects (e.g. fetch)
uses engine-native ArrayBuffer paths; Node-shaped `Buffer` construction is
module-side over this table. A zero-copy `buffer_adopt` (host adopts
module-provided bytes with a free callback) is the marked extension point if
read-path copies show up in N5/N6 profiles.

### 9.3 Promises

```c
struct JubeHostPromiseAPI {
    uint32_t api_version; uint32_t struct_size;
    Item (*promise_new)(Item* out_resolve, Item* out_reject); // withResolvers shape
    int  (*resolve)(Item resolve_fn, Item value);
    int  (*reject)(Item reject_fn, Item error);
    int  (*is_promise)(Item v);
};
```

Backs every `*/promises` namespace and async fs/dns/stream operations; the
module resolves from uv callbacks via `next_tick` scheduling rules identical to
today's in-tree code.

### 9.4 Node errors

```c
struct JubeHostNodeErrorAPI {
    uint32_t api_version; uint32_t struct_size;
    Item (*error_with_code)(const char* code, const char* ctor,  // "ERR_INVALID_ARG_TYPE", "TypeError"
                            Item message);
    Item (*errno_error)(int uv_errno, const char* syscall, const char* path);
    // throw via JubeHostScriptAPI.throw_value; check via check_exception
};
```

The `ERR_*` helper bodies (`js_runtime.cpp:23279–23450`) become the
implementation behind this table (owned by `node-core` conceptually but hosted
as API so every leaf module — and the host's own compatibility paths — shares
one implementation; exact placement decided in N2).

### 9.5 GC and rooting (JN9)

No new table: `JubeHostGcAPI` (register/unregister root, root frames, weak
refs) plus `persistent_root_register` cover all observed patterns —
`heap_register_gc_root` externs → `register_root`; the socket/server pool
arrays (`js_net.cpp:120`) → `register_root` per slot or persistent roots;
`RootFrame`/`Rooted` stack scopes → `root_frame_begin`/`take_slot`/`end`.
Conversion is mechanical but must be reviewed per site: this is exactly the
population where unrooted `Item` locals across allocating calls were found
(107/244 scripts diverging under forced GC), and each converted file gets a
forced-GC sweep run (§12) before it may flip to dynamic.

## 10. Inverting the backward calls (JN10)

Every core→builtin edge becomes an init-time hook registration with a defined
absent default:

| Today (core calls…) | Becomes | Absent-module default |
|---|---|---|
| `js_util_format` for `console.log` (`js_globals.cpp:5990`) | console formatter hook | engine-native minimal formatter (JSON-ish, no colors/depth options) |
| `js_net_get_active_handles` / `_getActiveHandles` (`js_globals.cpp:3714`) | host-side enumeration of the §9.1 resource table — no hook needed | empty array (empty table) |
| `js_net_close_all_active_servers` at shutdown (`js_runtime.cpp:35790`) | `register_shutdown` participants | no-op |
| `js_child_process_emit_or_queue_cluster_online` (`js_runtime.cpp:62`) | owned entirely by `node-child-process` via its IPC channel hooks | n/a |
| `uv_pipe_t*` IPC accept handoff (`js_globals.cpp:138`) | the host accepts the handle into the §9.1 resource table and delivers a rid through the IPC hook registered by `node-net`/`node-child-process` | reject with `MODULE_NOT_FOUND`-shaped error |
| unconditional `process`/`Buffer`/`os`/`vm` global install (`js_globals.cpp:16009–16096`) | host installs its own core globals; module namespaces/globals install via the existing `JubeNamespaceDef` globals path (`js_globals.cpp:78–88`) extended with non-eager (lazy accessor) installation | globals simply absent (`typeof Buffer === 'undefined'`) |

The `Buffer` global is the observable crux: with `node-core` bundled in the
standard distribution (JN12), scripts see no difference; only the minimal
profile drops it, which is that profile's purpose.

## 11. Migration plan

Stages are independently shippable checkpoints; each holds `make
test-lambda-baseline` and `make node-baseline` (1492/3517) exactly, keeps the
standard host byte-identical across bundle packaging, and lands static-first
(P7). Line references in §4 are re-verified at each stage start.

- **N0 — Freeze evidence.** Regenerate the coupling inventory for the §4 file
  set (the hosted-Python inventory tooling pattern); record node-baseline,
  host binary size, require-latency and namespace-build microbenchmarks;
  write `utils/check_node_module_architecture.py` (allowed-symbol model:
  `jube.h` + libuv + libc/platform + module-local) with a synthetic self-test.
  *Gate:* inventory + baselines archived; checker red on the current tree
  (expected) with a counted allowlist.
- **N1 — Registry plumbing + pipe-cleaner.** Add the specifier index and the
  manifest `kind`/`provides` support; route `js_module_get_builtin` through
  the registry **first**, falling back to the chain; convert `path`
  (`js_path.cpp`, 669 lines, zero deps) to a statically registered
  `JubeModuleDef` with a namespace table and delete its chain entries + list
  rows. *Gate:* node-baseline unchanged; `path` served via registry; the four
  lists still consistent for everything else; MIR budgets unchanged.
- **N2 — Service tables.** Introduce `JubeHostNodeAPI` v1 (async
  request/resource ops + binary/promise/error), the op set extracted from the
  §4.3 `uv_*` call inventory across fs/net/child_process/dns (P5) — including
  the §13-risk-8 edge inventory (fs.watch, tty raw mode, IPC descriptor
  passing, fd adoption) so v1 needs no later escape hatch; land the checker
  in CI for converted files with `uv_*` on the banned list; write the rooting
  conversion guide (JN9 patterns). *Gate:* tables exercised by at least one
  real conversion (fs picked next); negative descriptor tests (undersized/
  missing tables) extend the existing POSIX matrix.
- **N3 — `node-core` (static).** Move the §6.1 core set including process
  extensions, `internalBinding`, stubs, and error helpers; invert the §10
  hooks; delete `js_module_get_builtin`'s chain and the three other lists;
  `builtinModules` becomes registry-derived. *Gate:* chains deleted;
  node-baseline; forced-GC sweep green on converted files; checker green for
  `node-core` sources.
- **N4 — First dynamic module: `node-zlib`.** Smallest external-dep leaf
  (1.4k lines, zlib, buffer-only coupling) proves the full delivery chain:
  build target cloned from `lang-python`'s shape, `module.json` + hash
  stamping, `make build-node-zlib`, loader-negative matrix entries,
  absent-module smoke (`require('zlib')` → MODULE_NOT_FOUND + host log).
  zlib stops linking into the standard host. *Gate:* dynamic load green on
  macOS + Linux; identical behavior static vs dynamic (P7 diffing).
- **N5 — libuv leaves.** `node-fs`, then `node-net` (net+dns), then
  `node-child-process`, static→dynamic each, converting rooting per JN9 and
  every direct `uv_*`/JS-queue call to the §9.1 request API (JN8). *Gate per
  module:* node-baseline; forced-GC sweep; checker (zero `uv_*` references);
  absent-module negatives.
- **N6 — Protocol leaves.** `node-http` (http+https), `node-tls`; then the
  JN14 three-way crypto split (`web-crypto` module + `node-crypto` module +
  shared mbedTLS primitives, per the architecture ADRs) and `node-crypto`.
  mbedTLS drops
  out of the minimal host link if nothing else needs it (audit — mbedtls is
  also linked via `lambda-lib`). *Gate:* as N5, plus `crypto` optional at
  runtime (the native-module doc's exit criterion).
- **N7 — Closure.** `node-core` flips dynamic; packaging: standard bundle =
  host + `node-core` (+ manifest-only leaf descriptors), full bundle = all,
  minimal profile = host only; `make verify-jube-package` extended with
  node-module hash + absent/full smoke; docs (`doc/Lambda_Jube_Runtime.md`
  section); final allowlist reduction in the checker. *Gate:* byte-identical
  host across all three bundles; minimal profile runs the non-Node JS suites;
  release perf evidence (require microbench, node-baseline wall time) within
  noise of N0 numbers.

Exit criteria overall (superset of the native-module doc's POC 2 list):
memcmp chains deleted; per-module specifier registration; node-baseline
unchanged; `zlib` + `fs` + the rest loadable dynamically; `crypto`/`tls`/
`http` optional at runtime with clean errors; minimal host runs without any
node module.

## 12. Testing and verification

- **node-baseline** (`make node-baseline`) after every stage — the continuous
  regression contract (1492/3517 at N0; the number may only grow).
- **Absent-module negatives** per module: require → Node-shaped error, host
  diagnostic logged, no crash, no partial registration (rollback verified) —
  extending `utils/test_jube_module_loader_negative.py`.
- **Static/dynamic equivalence**: each module runs its slice of test/node in
  both registration modes before the static path is removed (P7).
- **Forced-GC sweep** (the existing P3 harness) over each converted module's
  test slice — the JN9 conversion is only done when this is green.
- **Architecture checker** (`check_node_module_architecture.py`) in
  `make build-test` CI from N2: module objects may reference only `jube.h`
  services, libuv, platform libc, and module-local symbols.
- **MIR budgets**: `test/mir` ratchet stays green — resolution rewiring must
  not change emission for existing scripts (JN13).
- **Perf**: require-latency and namespace-build microbench vs N0; node-baseline
  wall time; standard/minimal host binary sizes recorded per stage.

## 13. Open questions and risks

1. **`stream` is both huge and load-bearing** (10.1k lines, the compat
   linchpin for baseline growth). Migrating it into `node-core` while other
   work improves stream coverage invites conflicts — sequence N3 against any
   active stream campaign, or land stream last within N3.
2. **The crypto/WebCrypto split** (JN14) is real surgery inside an 8.7k-line
   file with shared mbedTLS helper code; budget it as its own sub-task with a
   shared-primitives header, not a file move.
3. **`process` split line** (host core vs `node-core` extensions) needs the
   N3 enumeration; risk of hidden engine dependencies on Node-only members
   (e.g. test harnesses reading `process.memoryUsage`).
4. **util/console degradation**: the absent-`node-core` console formatter must
   be good enough for engine diagnostics without re-growing an inspect clone
   host-side.
5. **npm resolver placement** (host now, `node-core` later?) — coupled to
   compile-time batch lowering; revisit after N5.
6. **`node:vm`** stays host-side indefinitely unless a compiler-service
   exposure need arises elsewhere; document it as a deliberate exception to
   "all Node surface is module-provided".
7. **Windows**: `LoadLibrary` parity and the `-undefined,dynamic_lookup`
   equivalent (host import library / delay-bound imports) need CI from N4 —
   same open as the native-module design's risk 4. The JN8 revision helps:
   with libuv fully host-side, modules import only the Jube API surface, so
   the Windows export set stays small and auditable.
8. **Request-API coverage and cost (JN8)**: with the two-tier split the
   coverage risk concentrates in Tier 2 — `fs.watch` (`uv_fs_event`), IPC
   descriptor passing, and `uv_tcp_open`-style fd adoption (cluster/stdio)
   are the edges N2 must inventory before freezing v1 so no module ever
   needs an escape hatch (the `napi_get_uv_event_loop` mistake, §3.1).
   Tier 1 adds a new sizing concern: fs/dns/zlib/crypto all ride the shared
   work pool, so pool starvation under mixed load needs a host sizing policy
   and a long-op hygiene rule (libuv's fixed-4-thread default is the
   cautionary precedent). And v1's copy-on-submit write contract is a
   deliberate simplification: profile http/fs throughput against the N0
   baselines before deciding whether the pinned zero-copy variant is
   warranted.
9. **Worker threads/cluster futures**: if real `worker_threads` support
   arrives (per `vibe/Lambda_Js_Thread.md`), per-worker module instances and
   the "modules are process-lifetime singletons" rule will need
   reconciliation (likely per-runtime `heap_cleanup`/`runtime_reset` already
   suffice — verify then).
10. **Python H8 debt**: the module symbol-isolation checker tightening that
    node modules require is the same work hosted Python still owes; doing it
    once for both is the efficient path, but sequencing against the Python
    closure plan must be explicit.

## Appendix A — relationship to prior decisions

| Prior decision | Effect here |
|---|---|
| Native-module design POC 2 (`vibe/Lambda_Design_Native_Module.md` §8) | This document is its detailed successor; the sketch's `JubeNamespaceDef` capability is now the landed `jube.h:277` struct, and the suggested order (path first, crypto last) survives with zlib inserted as the dynamic pipe-cleaner. |
| Hosted-language architecture P1–P10 (`vibe/Lambda_Design_Jube_Lang_Hosting.md`) | P1/P3/P5/P7/P9/P10 carried over directly (§5); the compiler-API layers are replaced by JS runtime service tables (§9). |
| Errors-as-return-values across the C ABI (Jube ledger, `vibe/Lambda_Semantics_Features.md`) | §5 P6, §9 contract. |
| Precise rooting only, no conservative scanning (CLAUDE.md rule 15) | JN9; forced-GC sweep gates each conversion. |
| Static-modules layering SM10/SM12 (`vibe/Lambda_Design_Static_Modules.md`) | Moving Node IO out of `lambda-rt` is consistent with the lambda-io charter; `lib/uv_loop.c`'s eventual home is decided there, not here. |
| C2MIR frozen (CLAUDE.md rule 14) | No node-module work touches the legacy transpiler; MIR-emission neutrality is a stated gate (JN13). |
| node-baseline ledger (`make node-baseline`, 1492/3517) | The continuous no-regress gate for every stage (JN13). |
| Review 2026-07-26 (user) | JN8 inverted: modules shielded behind a host request/resource API (rid table) instead of calling libuv directly; JN7 consolidated into one composed `JubeHostNodeAPI`; prior-art survey added (§3.1). |
| `Lambda_Design_Jube_Architecture.md` (2026-07-26) | Top-level architecture ADRs JA1–JA13 now govern; supersedes JN14's WebCrypto placement (→ `web-crypto` module) and moves `stream/web` out of `node-core` (→ `web-streams` module re-export). |
