# Transpile_Node4: Stream-Centric Architecture & the Path Past 41%

> **Status of this document.** This proposal supersedes the planning in `Transpile_Node3.md`. It re-measures the Node.js compatibility baseline against a **fresh `make release` build (2026-06-16)**, then refreshes key module claims against the current LambdaJS implementation with focused checks on 2026-06-18, 2026-06-20, and 2026-06-21. The locked full-suite baseline remains 1,462; the focused spot-checks below are drift indicators, not a replacement full release baseline. Every claim is grounded in the current `lambda/js/` code (`file:line` cited; line numbers drift -- confirm against the symbol).

---

## 1. Executive Summary

LambdaJS now passes **1,462 of 3,521** official Node.js parallel tests (**41.5%**), measured on a fresh release build and locked in as the new baseline on 2026-06-16. That is **+48 net** over Node3's 1,414 snapshot — but the headline number hides the real story:

- The previous baseline (`test/node/official_baseline.txt`, preserved as `*.stale-1414`) was **stale**: re-measurement showed **77 new passes and 28 regressions** against it, all from unrelated language-conformance work landing without a Node-regression gate. The first action of Node4 — re-baseline and gate against regressions — has been completed in tandem with this proposal.
- Node3's plan was executed **out of order**. The work that landed (crypto ciphers, Buffer, assert, a real TLS layer, module sub-path resolution, process/os) tracked Phases 6/8/9/10/13. **Phase 7 — the stream state-machine rebuild, identified in Node3 as the highest-reward item — was never started.** `js_stream.cpp` is still 840 LOC of the same map-based stubs.
- Because streams were skipped, `http`, `net`, `zlib`, and the crypto cipher all got built **standalone on libuv** instead of on a shared stream core. They do not compose (no `pipe`, no `for await`, no `createReadStream`). This is now the dominant structural blocker.
- There is a **stability problem**: the locked full-suite run had 26 hard crashers + 2 timeouts, and focused 2026-06-18 reruns show the pressure is still real. Track 0 has now cleared the focused `net` crash cluster (`net` is 94/148 with 0 crashers and 0 regressions), converted the focused `child_process` spawn-error crashers into clean behavior (`child_process` is 34/97 with 0 crashers and 0 regressions), and cleared the focused `tls` crash cluster (`tls` is 130/214 with 0 crashers and 0 regressions). The remaining crash risk is now broader full-suite drift rather than the focused net/child_process/tls gates.
- Several modules with **near-complete code score very low** (`assert` 2/14, `buffer` 27/67 in the current focused gate, `util` 7/27). Their gap is *message/semantics fidelity* (exact `ERR_*` codes and Node-format messages), not missing APIs.

**Node4 target: ~1,800 (51%)** via six tracks, with a stretch to 1,900+. The central thesis: **build the real stream core, re-platform the I/O subsystems onto it, then close the wiring / stability / fidelity gaps around it.**

### Current Baseline Snapshot (locked in 2026-06-16)

| Metric | Count |
|--------|------:|
| Total tests run | 3,521 |
| **Passing (locked baseline)** | **1,462 (41.5%)** |
| Failing | 2,058 |
| Crashed | 26 |
| Timed out | 1 |
| Improvements vs prior 1,414 baseline | +48 net (77 new − 28 lost − 1 flicker) |

> Per-module numbers in §3.1 were captured at 1,463 (single-test flicker `test-abortcontroller.js` between the measurement and lock-in runs); the difference is noise on a 3,521-test suite.

> 2026-06-18 focused drift checks against the current local LambdaJS build: `util` stayed **7/27** with 0 regressions and 2 improvements; `stream` stayed **50/213** with 0 crashes; `dns` is now **10/29** with 0 crashes, 0 regressions, and 10 improvements after the Track E.1 lookup/promises/internal-binding slice; `https` improved to **42/63** with 3 improvements; `net` is now **94/148** with 0 crashes, 0 regressions, and 12 improvements after the Track 0.3 socket-connect stabilization slice; `child_process` is now **34/97** with 0 crashes, 0 regressions, and 8 improvements after the Track 0.4 spawn/fork stabilization slice; `tls` is now **130/214** with 0 crashes, 0 regressions, and 78 improvements after the Track 0.5 TLS/node:test/closure-env stabilization slice. 2026-06-20 focused `buffer` check is **27/67** with 0 crashes, 0 regressions, and 2 current improvements; the C.3 iterator-surface fixture passed, while the official `test-buffer-iterator.js` remained in the locked baseline. The C.4 binding-constants fixture also passed on 2026-06-20, and the direct official `test-constants.js`, `test-binding-constants.js`, `test-uv-binding-constant.js`, and `test-process-binding-internalbinding-allowlist.js` probes exit 0. The C.5 common-shim fixture passed on 2026-06-20, direct WPT helper consumers `test-whatwg-events-event-constructors.js` and `test-whatwg-url-custom-setters.js` exit 0, and `common/internet` now supports env-gated graceful skip. The E.1 `common/dns` helper fixture also passed on 2026-06-20, covering DNS packet parse/write plus mocked lookup errors. The Track D `common/crypto` helper fixture passed on 2026-06-20, covering `modp2buf`, PEM regexes, OpenSSL-version predicates, and safe `opensslCli` probing; the Track D P3 random-fill fixture now covers `randomFillSync`, callback-form `randomFill`, and `getFips` against Buffer, TypedArray, DataView, and ArrayBuffer inputs. The Track D randomBytes/randomInt/WebCrypto/HKDF/generate-key/getCipherInfo/hash/randomUUIDv7/PBKDF2/HMAC/Argon2-unsupported/sec-level/Sign-Verify/cipher-compat/DH/ECDH fixtures now cover `randomBytes` callback/error fidelity and hidden aliases, `globalThis.crypto.getRandomValues()` integer typed-array filling, tightened `randomFill(Sync)` offset/size validation, `randomInt([min,] max[, cb])` overloads, cipher output-encoding validation, Cipheriv/Decipheriv constructor/prototype behavior, DES-EDE3-CBC, AES-KW, AES-ECB/null-IV validation, cipher input decoding, Hash stream-style `end()`/`read()`, Hash input decoding for base64/hex/latin1/ucs2, RFC 5869 output, ArrayBuffer returns, async callback ordering, minimal `createSecretKey()` input, AES/HMAC `generateKey(Sync)` export lengths, AES/DES/KW cipher metadata/NID lookup, one-shot hash output encodings, UUIDv7 timestamp/version/variant bits, integer `Date.now()`, PBKDF2 Buffer returns/error validation plus Latin-1 Buffer `toString()`/`from()` decoding, MD5/SHA-1/SHA-224 HMAC vectors, Hmac constructor/prototype behavior, stream-style HMAC `end()`/`read()`, explicit `ERR_CRYPTO_ARGON2_NOT_SUPPORTED` throws for `argon2()`/`argon2Sync()`, `internal/crypto/util.getOpenSSLSecLevel()`, the thin `createSign()`/`createVerify()` update surface used by `test-crypto-update-encoding.js`, DH/ECDH constructor metadata, finite-field DH key agreement, ECDH key agreement/point conversion, DH validation errors, DH ArrayBuffer-view generator overloads, AES padding finalization errors, and narrow PEM private-key `Sign.sign()`. The focused `crypto` gate is now **47/121** with 0 crashes, 0 regressions, and 32 current improvements (`test-crypto-argon2-unsupported.js`, `test-crypto-cipheriv-decipheriv.js`, `test-crypto-classes.js`, `test-crypto-dh-constructor.js`, `test-crypto-dh-curves.js`, `test-crypto-dh-errors.js`, `test-crypto-dh-modp2-views.js`, `test-crypto-dh-modp2.js`, `test-crypto-dh-odd-key.js`, `test-crypto-dh-shared.js`, `test-crypto-dh.js`, `test-crypto-ecb.js`, `test-crypto-ecdh-convert-key.js`, `test-crypto-ecdh-setpublickey-deprecation.js`, `test-crypto-encoding-validation-error.js`, `test-crypto-getcipherinfo.js`, `test-crypto-hkdf.js`, `test-crypto-hmac.js`, `test-crypto-key-objects-messageport.js`, `test-crypto-oneshot-hash.js`, `test-crypto-padding.js`, `test-crypto-pbkdf2.js`, `test-crypto-random.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-randomuuidv7.js`, `test-crypto-sec-level.js`, `test-crypto-secret-keygen.js`, `test-crypto-subtle-cross-realm.js`, `test-crypto-subtle-zero-length.js`, `test-crypto-update-encoding.js`, `test-crypto-verify-failure.js`, `test-crypto-worker-thread.js`). Re-run the full release methodology in §9 before changing the locked baseline.

> Measurement method is documented in [§9](#9-appendix-measurement-methodology) so these numbers are reproducible.

---

## 2. Reconciliation with Transpile_Node3

Node3 proposed Phases 6–14. Here is what the code actually shows today.

| Node3 Phase | Planned | Actual status now | Evidence |
|------|---------|-------------------|----------|
| **6 — Error codes** | +80 | 🟡 **Infra done, migration ~15%** | `js_error_codes.h` exists (30 `JS_ERR_*` constants); helpers `js_throw_invalid_arg_type/_out_of_range/_type_error_code` at `js_runtime.cpp:21483–21570`. But only ~105 coded call sites vs ~700+ plain. `js_typed_array.cpp` alone has 65 plain + 45 coded; `js_runtime.cpp` has 401 plain. |
| **7 — Stream internals** | +100 | 🔴 **NOT STARTED** | `js_stream.cpp` still 840 LOC. State is JS-map properties (`__flowing__`, `__buffer__`), only option storage for `highWaterMark` rather than backpressure, `_write` gets a noop callback, and there is no `Symbol.asyncIterator`. **This is the critical miss.** |
| **8 — Crypto** | +60 | 🟢 **Symmetric done, DH/ECDH partial** | `createCipheriv/Decipheriv` (AES-CBC/CTR/GCM) `js_crypto.cpp:908–938`; `pbkdf2(Sync)` `:945`; `scrypt(Sync)` `:1112`; `subtle.digest/encrypt/decrypt` `:1225–1440`; `getCiphers/getCipherInfo/getHashes/timingSafeEqual`; `hash()`, `randomBytes()` callback/alias validation, `globalThis.crypto.getRandomValues()`, `randomInt([min,] max[, cb])`, `randomUUIDv7()`, PBKDF2 Buffer/error fidelity, HMAC vectors/constructor/read surface, explicit `argon2()`/`argon2Sync()` unsupported errors, `internal/crypto/util.getOpenSSLSecLevel()`, `randomFillSync`, callback-form `randomFill`, `getFips`, `hkdf(Sync)`, minimal `createSecretKey()`, AES/HMAC `generateKey(Sync)`, a thin `createSign()`/`createVerify()` update surface, finite-field DH key agreement, ECDH key agreement/`convertKey()`/point formats, narrow PEM private-key `Sign.sign()`, and DH/ECDH constructor metadata/getters/validation are now present. Missing: full RSA/ECDSA verify, sign options/PSS/KeyObject inputs, stateless KeyObject `diffieHellman()`, asymmetric keygen, full KeyObject, X509. |
| **9 — Module resolution** | +50 | 🟢 **Mostly done** | `stream/promises` is now a real namespace (`js_runtime.cpp:31691`), plus `fs/promises`, `dns/promises`, `timers/promises`, `util/types`, `path/posix|win32`, `assert/strict`, `node:test`, `vm`, `perf_hooks`, and `constants`. `internal/util` and `internal/test/binding` are recognized as engine built-ins; `internalBinding('cares_wrap')` exists for DNS test hooks, `internalBinding('uv')` now exposes read-only `UV_*` constants, and `internalBinding('constants')` returns the Node-shaped null-prototype constants tree. Remaining internal work is broader private-binding module fidelity beyond this covered surface. |
| **10 — Process & OS** | +40 | 🟢 **Done** | `process.binding`, `allowedNodeEnvironmentFlags`, `report`, `setuid/setgid` all at `js_globals.cpp:3270–3330`. `os.cpus/networkInterfaces/userInfo/constants` complete (`js_os.cpp`). |
| **11 — child_process & REPL** | +40 | 🟡 **exec/spawn improved, fork/IPC skeletal** | `js_child_process.cpp` is now 1,338 LOC. Track 0.4 added guarded `uv_spawn` failures, async `error`/`spawn` events, stream `data/end/close`, multiple listeners, LambdaJS self-spawn routing through `lambda.exe js`, numeric arg stringification, `process.execArgv`, string exit codes, stdio `inherit`/`ipc` basics, and a thin `fork()` bridge. Remaining: real IPC messaging, full stdio validation/options, `spawn-shell` warning/common fidelity. |
| **12 — zlib streaming** | +35 | 🔴 **Blocked on Phase 7** | `js_zlib.cpp` (463 LOC): sync only (`gzipSync` etc.). No `createGzip/createDeflate` Transform classes, no async-callback form, brotli stubbed (`:320` returns error), no zstd. |
| **13 — Buffer & util** | +30 | 🟢 **Buffer done; util partial** | `js_buffer.cpp` now 2,558 LOC (isAscii/isUtf8/copyBytesFrom/of/SlowBuffer/poolSize/BigInt64 plus explicit Buffer prototype `keys`/`values`/`entries`/`[Symbol.iterator]` all present). `assert` 1,176 LOC (throws/rejects/match/AssertionError/mock.fn). `util.promisify` has a callback-last Promise wrapper, `util.promisify.custom`, `internal/util.customPromisifyArgs`, Node-compatible first-value/default multi-result handling, and `DEP0174` warning emission for promise-returning originals; `util.callbackify` has a Promise-to-callback wrapper and official `test-util-callbackify.js` now passes (Node4 C.2 partials, 2026-06-17). Remaining: official `test-util-promisify.js` mustCall/promise-drain fidelity and `util.inspect` depth/colors/showHidden. |
| **14 — Test infra** | +25 | 🟢 **core common shims done** | External skip-list (26 entries) + GTest loading done. `common/{index,tmpdir,fixtures,internet,wpt,dns,crypto}` shimmed (`lambda/js/test_shim/`), and `make node-shim` installs the runner-facing copies under `ref/node/test/common/`. Remaining test-infra work is broader private helper fidelity, not missing core common shims. |

**New since Node3, not in the plan:** a **real mbedTLS-backed `tls`** module — `tls.connect`, `tls.createServer`, `createSecureContext`, `TLSSocket` with an actual handshake (`js_tls.cpp:302` `tls_handshake(...)`, 623 LOC). This is significant and is the reason https *could* be made real cheaply (see Track C).

---

## 3. Current State (measured per-module)

### 3.1 Per-module pass rates (priority modules)

Rows for `https`, `net`, `child_process`, `tls`, `stream`, `util`, and `dns` were refreshed with focused 2026-06-18 runs against the current local build; `buffer` was refreshed on 2026-06-20 after the C.3 iterator-surface slice; `crypto` was refreshed on 2026-06-21 after the Track D ECDH/PEM-sign slice. The remaining rows are from the 2026-06-16 release-baseline probe and should be refreshed before using their exact counts in a release decision.

| Module | Total | Pass | Rate | Crash | Read on the gap |
|--------|------:|-----:|-----:|------:|-----------------|
| http | 388 | 277 | 71% | 0 | Best-supported; gains now need streaming bodies + chunked encoding |
| fs | 251 | 166 | 66% | 0 | async defers correctly (libuv); missing `FileHandle`, `fs.watch`, `createReadStream` |
| https | 63 | 42 | 67% | 0 | Still no true TLS wiring: `createServer()` delegates to HTTP despite the TLS-aware file header; 3 current improvements |
| net | 148 | 94 | 64% | 0 | Track 0.3 landed real connect argument normalization, hostname lookup events, pending-connect destroy safety, and `uv_tcp_getsockname()` addresses; still not a Duplex |
| os | (7)* | 3 | — | 0 | small prefix sample; mostly complete |
| process | 93 | 36 | 39% | 1 | message fidelity + a few APIs |
| events | 36 | 13 | 36% | 0 | missing `events.on()` async-iterator, functional captureRejections |
| buffer | 67 | 27 | 40% | 0 | C.3 explicit iterator surface is landed; remaining gap is mostly message/format/API fidelity |
| vm | 95 | 32 | 34% | 0 | sandbox isolation, `vm.Script` timeout (a test times out) |
| timers | 58 | 20 | 34% | 0 | ordering correctness |
| repl | 105 | 34 | 32% | 2 | mostly out of scope; 2 crashers |
| url | 16 | 5 | 31% | 0 | WHATWG URL edge cases |
| child_process | 97 | 34 | 35% | 0 | Track 0.4 fixed the spawn failure crash path and added a thin fork/self-spawn/stdio/IPC foundation; full IPC and `spawn-shell` warning fidelity remain |
| tls | 214 | 130 | 61% | 0 | Track 0.5 cleared the focused crash cluster; remaining failures are TLS API fidelity, stream semantics, certificate verification, and unsupported options |
| stream | 213 | 50 | 23% | 0 | **the linchpin — 163 failing** |
| util | 27 | 7 | 26% | 0 | promisify promise/mustCall drain + inspect gaps; callbackify official pass |
| assert | 14 | 2 | 14% | 0 | **code complete — gap is AssertionError message fidelity** |
| crypto | 121 | 47 | 39% | 0 | randomBytes callback/alias fidelity, global WebCrypto `getRandomValues`, randomInt overloads, cipher output/input encoding validation, DES-EDE3-CBC/AES-KW/AES-ECB constructor and IV fidelity, Cipheriv/Decipheriv padding finalization, Hash stream/input-encoding fidelity, randomFillSync/randomFill/getFips, HKDF, HMAC digest/constructor/read surface, AES/HMAC generateKey, AES/DES/KW getCipherInfo metadata, one-shot hash, UUIDv7, PBKDF2 Buffer/error fidelity, explicit Argon2 unsupported errors, internal OpenSSL security-level helper, thin Sign/Verify update handling, finite-field DH key agreement, ECDH key agreement/point conversion, and narrow PEM private-key `Sign.sign()` now landed; asymmetric gap remains (full verify/sign options/asymmetric keygen/full KeyObject/stateless DH) |
| zlib | 61 | 8 | 13% | 0 | **streaming gap — blocked on stream core** |
| worker | 139 | 28 | 20% | 0 | stub (single-threaded); out of scope beyond stub passes |
| **dns** | 29 | **10** | **34%** | 0 | **lookup/promises now cover the core option/error path; remaining gap is resolver record coverage, the Resolver class, and common/dns fidelity** |

<sup>*os prefix matched a small sample in the probe; the module is otherwise mature.</sup>

### 3.2 Crash inventory (locked 26 crashes + 2 timeouts; spot-check drift noted)

| Cluster | Count | Tests | Likely root cause |
|---------|------:|-------|-------------------|
| **net** | **0 in the post-Track 0.3 focused run** | Was 15 in the earlier 2026-06-18 focused run: autoselectfamily, better-error-messages-{path,port-hostname}, connect-{after-destroy,immediate-finish,no-arg,options-invalid,options-port}, dns-{custom-lookup,error}, localerror, options-lookup, pipe-connect-errors, socket-connect-invalid-autoselectfamily(×2) | Fixed by `Socket.connect`/`net.connect` rest-arg normalization, Node-shaped validation errors, deferred close while DNS/connect is pending, `lookup` error emission, and method receiver fixes. Remaining net failures are stream/Duplex semantics rather than crashers. |
| **child_process** | **0 in the post-Track 0.4 focused run** | Was 3: spawn-error, spawn-shell, spawn-typeerror | `spawn-error` and `spawn-typeerror` now pass directly; the focused module gate is 34/97 with 0 crashers and 0 regressions. `spawn-shell` still fails as a clean assertion on warning/common fidelity, not a crash. |
| **tls** | **0 in the post-Track 0.5 focused run** | Was 11: client-abort, client-allow-partial-trust-chain, client-default-ciphers, connect-allow-half-open-option, connect-hints-option, destroy-whilst-write, ip-servername-forbidden, socket-allow-half-open-option, socket-constructor-alpn-options-parsing, tlswrap-segfault-2, wrap-event-emmiter | Fixed by TLS require/extension resolution, inline PEM parsing, TLSSocket/listener/failed-connect lifetime guards, `node:test` beforeEach retention, and remapped async scope-env slot allocation for reused parent closure envs. Remaining TLS failures are clean failures. |
| **console** | 2 | async-write-error, sync-write-error | Write-error EPIPE path |
| **repl** | 2 | array-prototype-tempering, unsafe-array-iteration | Prototype tampering crashes the iterator fast-path |
| **signal/process** | 3 | signal-args (130), signal-handler (143), process-kill-pid (143) | Signal delivery into the event loop |
| misc | 2 | stdio-pipe-redirect, permission-net-uds | — |
| **timeouts** | 2 | fs-readdir-stack-overflow, vm-timeout | `vm` has no timeout enforcement; recursion guard missing |

> Crashes block more than their own test — a segfault can abort a worker batch. **Track 0 fixes these first.**

### 3.3 Regression inventory

The 2026-06-16 re-baseline audit found 28 failures relative to the stale 1,414 baseline, clustered in `snapshot-dns` (4), `https` (3), `http` (3), `vm` (2), `tls` (2), `domain` (2), plus singletons. Those were historical stale-baseline deltas, not necessarily current regressions after `test/node/official_baseline.txt` was relocked at 1,462.

Current 2026-06-18 focused module checks show active regressions against the locked 1,462 baseline in:
- `net`: none after the Track 0.3 focused rerun (`./test/test_node_gtest.exe --modules=net --timeout=15000 --gtest_brief=1`)
- `child_process`: none after the Track 0.4 focused rerun (`./test/test_node_gtest.exe --modules=child_process --timeout=15000 --gtest_brief=1`)
- `tls`: none after the Track 0.5 focused rerun (`./test/test_node_gtest.exe --modules=tls --timeout=15000 --gtest_brief=1`)
- `dns`: none after the Track E.1 focused rerun (`./test/test_node_gtest.exe --modules=dns --timeout=15000 --gtest_brief=1`)

Historical sampled stale-baseline root causes (real at the time, but recheck before treating as current):
- `test-next-tick.js` → `AssertionError: deepStrictEqual values are not equal` — **nextTick ordering regressed**.
- `test-process-hrtime-bigint.js`, `test-buffer-swap-fast.js` → `Uncaught SyntaxError: Invalid eval source` — a **shared engine-level regression** from recent language/eval work, surfacing in the `*-fast` internal-binding tests.

These confirm the baseline drifted because of unrelated language-conformance work (es2024/sparse-array/editing commits) landing without a Node-regression gate.

### 3.4 The "complete code, low pass-rate" anomaly

`buffer` (40%), `assert` (14%), and `util` (26%) have substantially complete implementations (2,558 / 1,176 / 1,455 LOC) yet score low. Their failures are not mostly missing methods — they are:
- exact `AssertionError` / `TypeError` **message strings** and `.code` properties,
- `util.inspect` output format (depth, colors, circular markers),
- remaining `util.promisify` promise/mustCall drain behavior, while `util.inspect` still lacks Node's formatting fidelity.

**Implication:** for these modules, a "fidelity sweep" (Track C/F) yields more than new features.

---

## 4. Root-Cause Re-Analysis

Six structural causes account for the bulk of the locked 2,058 failures plus the current crash pressure.

### RC1 — No real stream state machine (the linchpin)

`js_stream.cpp` models streams as JS objects with marker properties and a synchronous, unbounded buffer:
- `push()`/`write()` mutate `__buffer__` directly; there is **no `highWaterMark`, no backpressure** — `write()` never returns `false`, `drain` is emitted unconditionally (`:452`).
- the user `_write(chunk, enc, cb)` callback is **not invoked with a working callback** (`:438–443`); `_final`/`_flush` are best-effort.
- events fire **synchronously** inside `push`/`emit` rather than via the microtask queue, so ordering (`data` before `readable`, `end` after drain) is wrong.
- **no `Symbol.asyncIterator`** — `for await (const c of readable)` fails outright.
- the static surface is `Readable/Writable/Duplex/Transform/PassThrough/pipeline/finished` + `Readable.from` (`:740,:828`). Missing: `compose`, `addAbortSignal`, and a correct promise-returning `stream/promises.pipeline/finished`.

163 of 213 stream tests fail directly on this, and it blocks everything in RC2.

### RC2 — I/O subsystems are built standalone on libuv, not on streams

Because there was no stream core to build on, each I/O module reimplemented its own ad-hoc data flow directly over libuv:
- **http** (`js_http.cpp`, 1,329 LOC): `IncomingMessage.body` is a **string**, not a Readable; `ServerResponse` accumulates body in memory; no chunked transfer-encoding; `Agent` pooling is fake.
- **net** (`js_net.cpp`, 1,094 LOC): `Socket` is **not a Duplex**; `setKeepAlive/setNoDelay/ref/unref` are no-ops. `connect()` now validates options and uses libuv DNS/TCP with safer lifetime handling, but the stream contract is still ad-hoc.
- **zlib** (`js_zlib.cpp`): sync only; the streaming `createGzip()` family doesn't exist.
- **crypto cipher**: `Cipher/Decipher` are not Transform streams (only `update`/`final`).

Consequence: nothing composes (`req.pipe(gunzip).pipe(res)` is impossible), and each module independently re-derives buffering/flow bugs. **One real stream core lets all four re-platform.**

### RC3 — Wiring gaps: real implementations not connected

Two high-value capabilities exist but aren't wired:
- **https has no TLS.** `js_https.cpp:39–43` delegates to `js_http_createServer()` on port 443; the comment itself says *"In a full implementation, this would pipe TLS sockets into HTTP parsing."* Meanwhile `js_tls.cpp` is a **real** mbedTLS handshake. Connecting them is wiring, not new crypto.
- **`util.promisify` is partially real.** Node4 C.2 slices replaced the literal `return fn_item` stub with a callback-last Promise wrapper, `util.promisify.custom`, `internal/util.customPromisifyArgs`, Node default first-value resolution, named multi-result objects, and `DEP0174` warning emission. Local module coverage now passes, but the official `test-util-promisify.js` still fails on broader promise/mustCall drain behavior (direct run now reaches one anonymous `mustCall` instead of zero). `callbackify` now has a Promise-to-callback wrapper and official `test-util-callbackify.js` passes, so the remaining utility gap is promisify harness/promise fidelity plus `util.inspect`.

### RC4 — Stability: crashers on error, abort, and invalid-option paths

The locked full-suite crashers and the 2026-06-18 focused `net`/`tls` reruns were overwhelmingly on **error / abort / invalid-argument / option-validation paths**. The focused `net`, `child_process`, and `tls` crash clusters have now been converted into clean passes/failures by fixing connect/spawn/TLS option normalization, pending-handle lifetime, method receiver handling, test-hook retention, and async closure scope-env slot reuse. Full-suite crash risk still needs a fresh release run, but the focused Track 0 crash gates are clean.

### RC5 — Message & semantics fidelity

~85% of throw sites still use plain `js_throw_type_error(msg)` without a Node `ERR_*` code. Tests assert `{ code, name, message }` exactly. This caps `buffer`, `assert`, `process`, `path`, and the error-prefixed misc tests regardless of feature completeness.

### RC6 — Total-failure / stub modules

- **dns 10/29**: Track E.1 landed variadic `dns.lookup(host, [options], cb)`, option validation for `family`/`hints`/`all`/`verbatim`/`order`, callback/promise result shaping, IP-literal short-circuiting, a real `dns.promises.lookup`, `require('dns/promises')`, a mutable `internalBinding('cares_wrap').getaddrinfo` hook for official memory-error tests, and delayed `internal/test/binding` warnings. Direct `test-dns-lookup.js` now exits 0 and the focused gate reports 10 improvements / 0 regressions. Remaining work is resolver record fidelity, the `Resolver` class, and `common/dns`; historical `snapshot-dns` stale-baseline deltas should be rechecked against this partial.
- **async_hooks / AsyncLocalStorage**: registered but stubbed (no context propagation) — already cost one regression (`test-async-local-storage-contexts.js`) and block async-context-dependent http/timer tests.

---

## 5. The Plan — Six Tracks

Tracks 0 and C are independent and can start immediately. Track A is the long pole; Track B depends on it. D/E/F are independent of A.

```
Track 0 (stabilize + re-baseline) ─ independent, do first
Track C (wiring & quick wins)     ─ independent
Track A (stream core) ──→ Track B (re-platform http/net/zlib/crypto on streams)
Track D (crypto asymmetric)       ─ independent
Track E (dns + AsyncLocalStorage) ─ independent
Track F (child fork/IPC + fidelity sweep) ─ independent
```

---

### Track 0 — Stabilize & Re-baseline (P0, Low effort, **~+30**)

**Goal:** stop the silent rot, eliminate crashes, and make regressions visible.

0.1 **Re-baseline.** Run `make node-update-baseline` to record the current passing set. Without this, every future change is measured against a wrong reference and the 77 real improvements stay invisible. *(Locked in at 1,462 on 2026-06-16; the stale 1,414 file is saved as `test/node/official_baseline.txt.stale-1414`.)*

0.2 **Add a Node-regression gate.** Wire the node-official runner into the same CI guard used for `test-lambda-baseline` so language commits (es2024/eval/sparse-array) can't silently regress Node tests as they did here (28 regressions).

0.3 **Kill the net crash cluster (done, 2026-06-18).** Focused net rerun is now **94/148**, **0 crashed**, **0 timed out**, **0 regressions**, **12 improvements**. Landed: normalized `(port, host, cb)` and object options for `net.connect`/`createConnection`/`Socket.connect`, Node-shaped validation and DNS errors, `'lookup'` event emission, pending-DNS/connect destroy safety, receiver fixes for Socket/Server methods, `resume()` chaining, and `address()` from `uv_tcp_getsockname`. Remaining net work belongs under Track A/B because `Socket` still is not a real Duplex.

0.4 **Fix child_process spawn crashers (done, 2026-06-18).** Focused child_process rerun is now **34/97**, **0 crashed**, **0 timed out**, **0 regressions**, **8 improvements**. Landed: guarded `uv_spawn` failure path with async `'error'` emission, Node-shaped spawn error fields, async `'spawn'` ordering, stream `data/end/close` events, multiple listeners per event, numeric arg stringification, LambdaJS self-spawn routing through `lambda.exe js ... --no-log`, string `process.exit()` codes, `process.execArgv`, stdio `inherit`/`ipc` basics, child `process.send` no-op for IPC-spawned children, and a thin `fork()` bridge. Direct `spawn-error` and `spawn-typeerror` pass; `spawn-shell` is now a clean assertion failure on warning/common fidelity (`Unexpected extra warning received: [object Object]`), not a crash.

0.5 **Fix tls abort/options/wrap crashers (done, 2026-06-18).** Focused TLS rerun is now **130/214**, **0 crashed**, **0 timed out**, **0 regressions**, **78 improvements**. Landed: runtime `require()` `.js`/directory fallback for fixture modules, inline PEM cert/key/CA parsing, TLSSocket/listener/failed-connect lifetime guards, TLS server `address()`/`listen()`/`listening` compatibility, `events.once()` closure-backed Promise handlers, `node:test` beforeEach retention with hook rooting, and async/generator scope-env allocation that preserves remapped parent slots when reused closure envs are sparse. Direct `test-tls-client-allow-partial-trust-chain.js` now exits 0; it still reports memtrack leaks, so leak cleanup remains separate from crash stabilization.

0.6 **Fix active regressions, and re-check the historical stale-baseline fossils.** Current focused regressions are now clear for `net`, `child_process`, and `tls`; the previous `net` focused regressions (`test-net-connect-after-destroy.js`, `test-net-dns-error.js`) pass in the Track 0.3 rerun, and the previous `tls` focused regressions (`test-tls-connect-hints-option.js`, `test-tls-destroy-whilst-write.js`, `test-tls-socket-constructor-alpn-options-parsing.js`, `test-tls-tlswrap-segfault-2.js`) are clean in the Track 0.5 rerun. The old `nextTick` and `Invalid eval source` samples came from the stale-baseline audit and should be revalidated before they are treated as current blockers.

0.7 **Runner: per-module reporting + crash auto-quarantine.** Print the §3.1 table automatically and auto-tag crashers (already half-built — `write_crashers()` exists).

> Net effect: convert the locked full-suite crashers, plus the currently visible `net`/`tls` crash drift, into clean failures or passes. Conservative **+30**, plus restored signal quality.

---

### Track A — Stream Core Rebuild (P0, High effort, **~+70 direct**)

**Goal:** replace `js_stream.cpp` with a faithful Node `Readable`/`Writable`/`Duplex`/`Transform` state machine. This is the same Phase-7 work Node3 identified; it remains the single highest-reward item and is now also the prerequisite for Track B.

A.1 **State structs**, not marker properties — `ReadableState`/`WritableState` carrying `highWaterMark`, buffered `length`, `flowing` (null/true/false), `reading`, `ended`, `endEmitted`, `destroyed`, `needDrain`, `objectMode`, plus the buffered-chunk queue.

A.2 **Correct event sequencing via the microtask queue** (`js_event_loop.cpp` already provides `js_microtask_enqueue`/`js_next_tick_enqueue`). `data`/`readable`/`end`/`drain`/`finish`/`close` must fire in Node order, asynchronously.

A.3 **Backpressure**: `write()` returns `false` at/over `highWaterMark`; `drain` fires when it falls below. `push()` returns `false` likewise. Invoke the user `_read`/`_write(chunk,enc,cb)`/`_transform`/`_flush`/`_final` with **working callbacks**.

A.4 **Flow-mode transitions**: adding a `data` listener or calling `resume()` → flowing; `pause()` → paused-buffering; `pipe()`/`unpipe()` wired through backpressure.

A.5 **`Symbol.asyncIterator`** on Readable (async generator reading to end/error) so `for await` works.

A.6 **Static surface**: real `stream.pipeline` (multi-stage, error-propagating, cleanup), `stream.finished`, `stream.addAbortSignal`, `stream.compose`, and a correct promise-returning `stream/promises.pipeline/finished`.

A.7 **Re-platform `process.stdin/stdout/stderr`** and `fs.createReadStream/createWriteStream` onto the new core (small, unlocks `stdio`/`pipe` tests).

Estimated ~2,000–2,500 LOC. **stream 50 → ~120 (+70)**, and it unblocks Track B.

---

### Track B — Re-platform I/O on the Stream Core (P1, depends on A, **~+75**)

Once A lands, each subsystem becomes a thin adapter over the shared core instead of a bespoke libuv buffer.

| Sub-track | Change | Est. |
|-----------|--------|-----:|
| **B1 zlib** | `createGzip/Gunzip/Deflate/Inflate/DeflateRaw/InflateRaw` as Transform streams wrapping the existing zlib calls; add async-callback wrappers (`zlib.gzip(buf, cb)` via `process.nextTick`); freeze `zlib.codes`. (Brotli/zstd optional.) | +25 |
| **B2 http** | `IncomingMessage` extends Readable (emit `data`/`end`), `ServerResponse`/`ClientRequest` extend Writable; add chunked transfer-encoding and request-body streaming. | +30 |
| **B3 crypto cipher** | `Cipher`/`Decipher` (and optionally `Hash`/`Hmac`) become Transform streams so `createCipheriv(...).pipe()` works. | +10 |
| **B4 net** | `Socket` becomes a Duplex over the core; real `setTimeout` enforcement. | +10 |

---

### Track C — Wiring & Quick Wins (P1, Low effort, independent, **~+55**)

High return for low effort; none depend on the stream rewrite.

C.1 **Wire https → TLS (+10–15).** Replace the `js_https.cpp:39` delegation with `js_tls_createServer` for the server and `tls.connect` for the client, feeding decrypted bytes into the existing HTTP parser. The TLS layer already works (`js_tls.cpp:302`); this is plumbing. The 2026-06-18 focused `https` run is already 42/63 with 3 improvements and 0 regressions, but the implementation still delegates server creation to plain HTTP, so the strategic gap remains.

C.2 **Real `util.promisify` + `callbackify` (+15).** Implement the Node algorithm (last-arg callback `(err, value)` → resolve/reject; honor `util.promisify.custom`). `JsPromise` is fully available. Widely depended upon across fs/dns/stream/timers tests.

**2026-06-17 partial landing:** `js_util.cpp` gained the first real callback-last Promise wrapper, preserves the caller `this`, rejects on callback errors or synchronous throws, and exposes `util.promisify.custom = Symbol.for('nodejs.util.promisify.custom')`. Added `test/node/util_promisify.js` + `.txt`. Verification: `make build-test`; `./lambda.exe js test/node/util_promisify.js --no-log`; `./test/test_node_module_gtest.exe '--gtest_filter=NodeModuleTests/NodeFileTest.Run/util_basic:NodeModuleTests/NodeFileTest.Run/util_extended:NodeModuleTests/NodeFileTest.Run/util_promisify'`; `./test/test_js_gtest.exe --gtest_brief=1`; `./test/test_node_gtest.exe --modules=util --timeout=15000 --gtest_brief=1` → util official subset **6/27 pass, 21 fail, 0 regressions, 1 improvement**.

**2026-06-17 callbackify landing:** `js_util.cpp` now has a real `util.callbackify` wrapper: validates `original` and trailing callback, preserves `this`, routes fulfilled values to `(null, value)`, routes rejected promises to `(err)`, wraps falsy rejections as `ERR_FALSY_VALUE_REJECTION` with `.reason`, and routes synchronous throws through the same asynchronous rejection path. Added `test/node/util_callbackify.js` + `.txt`. Verification: `make build-test`; `./lambda.exe js test/node/util_callbackify.js --no-log`; `./lambda.exe js test/node/util_promisify.js --no-log`; `./test/test_node_module_gtest.exe '--gtest_filter=NodeModuleTests/NodeFileTest.Run/util_basic:NodeModuleTests/NodeFileTest.Run/util_extended:NodeModuleTests/NodeFileTest.Run/util_promisify:NodeModuleTests/NodeFileTest.Run/util_callbackify'`; `./test/test_node_gtest.exe --modules=util --timeout=15000 --gtest_brief=1` → util official subset **7/27 pass, 20 fail, 0 regressions, 2 improvements** (`test-util-callbackify.js`, `test-util-stripvtcontrolcharacters.js`). Remaining C.2 work: full `test-util-promisify.js` fidelity.

**2026-06-17 promisify fidelity landing:** `util.promisify` now matches Node's result-shaping rules for the covered cases: default multi-value callbacks resolve to the first success value, while `fn[require('internal/util').customPromisifyArgs] = ['a', 'b']` resolves to an object mapping names to callback values. `internal/util` is now treated as an engine-provided built-in during static `require()` resolution (including the `.js` resolver fallback), and `process.off`/`removeListener` now actually removes registered listeners so the official warning tests can detach `mustNotCall` handlers. Promise-returning originals emit the Node `DEP0174` warning object. Updated `test/node/util_promisify.js` + `.txt`. Verification: `make build`; `./lambda.exe js test/node/util_promisify.js --no-log`; `./lambda.exe js test/node/util_callbackify.js --no-log`; `make build-test`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/util_promisify:NodeModuleTests/NodeFileTest.Run/util_callbackify --gtest_brief=1` → **2/2 pass**; `./test/test_node_gtest.exe --modules=util --timeout=15000 --gtest_brief=1` → util official subset remains **7/27 pass, 20 fail, 0 regressions, 2 improvements**. Direct `test-util-promisify.js` now reaches one anonymous `mustCall` before exit (previously zero), but still fails the remaining `75 vs 1` promise/mustCall drain accounting.

C.3 **`Buffer.prototype` iterator surface + `keys/values/entries` (+3, done 2026-06-20).** `test-buffer-iterator.js` was already in the locked baseline because Buffer objects fall through to the generic typed-array iterator path, but `js_buffer.cpp` did not explicitly expose the Node Buffer prototype iterator family. The C.3 slice added Buffer-local iterator objects with `next()` and `[Symbol.iterator]()` identity behavior, installed `Buffer.prototype.keys`, `values`, `entries`, and `[Symbol.iterator] = values`, and added `test/node/buffer_iterator.js` + `.txt` to cover prototype presence, method identity, `for...of`, and direct `.next()` behavior. Verification: `make build`; `./lambda.exe js test/node/buffer_iterator.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-buffer-iterator.js --no-log`; `make build-test`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/buffer_iterator --gtest_brief=1` → **1/1 pass**; `./test/test_node_gtest.exe --modules=buffer --timeout=15000 --gtest_brief=1` → **27/67 pass, 40 fail, 0 crashed, 0 timed out, 0 regressions, 2 improvements** (`test-buffer-backing-arraybuffer.js`, `test-buffer-write-fast.js`). The two official improvements are current focused-run drift, not evidence that C.3 unlocked those exact tests.

C.4 **`internalBinding` / `internal/test/binding` constants (+15, done 2026-06-20).** The earlier DNS slice recognized the `internal/test/binding` specifier and added `internalBinding('cares_wrap').getaddrinfo`. This slice completed the covered constants surface: `internalBinding('uv')` now marks `UV_*` entries non-writable/non-configurable while preserving `errname(code)`, `internalBinding('constants')` returns the Node-shaped null-prototype tree (`crypto`, `fs`, `internal`, `os`, `trace`, `zlib`) with `fs` dirent constants and `os.{UV_UDP_REUSEADDR,dlopen,errno,priority,signals}`, `process.binding('uv'|'constants'|'cares_wrap')` now delegates to the same binding factory, and `require('constants')`/`require('node:constants')` now expose the frozen public constants view needed by `test-constants.js`. Added `test/node/internal_binding_constants.js` + `.txt` to guard null prototypes, public-module flattening, process-binding parity, and read-only UV assignment. Verification: `make build`; `./lambda.exe js test/node/internal_binding_constants.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-constants.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-binding-constants.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-uv-binding-constant.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-process-binding-internalbinding-allowlist.js --no-log`; `make build-test`; `./test/test_node_module_gtest.exe '--gtest_filter=*internal_binding_constants*' --gtest_brief=1` → **1/1 pass**. The official Node gtest wrapper did not honor a narrowed C.4 filter in this run and started the full 3,521-test sweep, so it was interrupted instead of counted as a focused gate.

C.5 **`common/internet` + `common/wpt` shims (+10–30, done 2026-06-20).** `lambda/js/test_shim/internet.js` is now a Lambda-compatible helper with Node's `addresses` names, `NODE_TEST_*` address overrides, `hasInternet`, `skip()`, and `skipIfNoInternet()`; `LAMBDA_NODE_SKIP_INTERNET=1`, `NODE_TEST_SKIP_INTERNET=1`, or `NODE_SKIP_INTERNET=1` turns network tests into a clean common-skip instead of a hard network dependency. `lambda/js/test_shim/wpt.js` now exposes the assertion harness surface used by the active official WPT consumers (`test`, `promise_test`, `assert_equals`, `assert_array_equals`, `assert_throws*`, comparison helpers, and `assert_unreached`) on top of Lambda's `assert`, plus a small `ResourceLoader` subset. `make node-shim` now backs up and installs those tracked helper files into the ignored `ref/node/test/common/` checkout alongside `index`, `tmpdir`, and `fixtures`, and `test/node/common_shims.js` + `.txt` guards the tracked shim source.

Verification: `./lambda.exe js test/node/common_shims.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-whatwg-events-event-constructors.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-whatwg-url-custom-setters.js --no-log` (clean skip for missing Intl); `env LAMBDA_NODE_SKIP_INTERNET=1 ./lambda.exe js ref/node/test/parallel/test-net-connect-immediate-finish.js --no-log` (clean skip). Remaining direct consumer failures are downstream runtime gaps, not shim-load failures: `test-whatwg-events-eventtarget-this-of-listener.js` still fails on EventTarget listener `this`, `test-whatwg-url-custom-searchparams-sort.js` still fails on URLSearchParams iteration, `test-dns-setservers-type-check.js` still fails on missing `dns.promises.Resolver`, and the normal `test-net-connect-immediate-finish.js` path still fails deeper in net behavior.

---

### Track D — Crypto Asymmetric (P1, High effort, independent, **~+40**)

Complete the crypto surface on the existing mbedTLS backend (`js_crypto.cpp`).

| Priority | API | mbedTLS backend |
|----------|-----|-----------------|
| P1 | `createSign`/`createVerify` (RSA, ECDSA) | `mbedtls_pk_sign`/`_verify` |
| P1 | `generateKeyPair(Sync)` | `mbedtls_pk_*` + `mbedtls_rsa_gen_key` |
| P2 | `createPublicKey`/`createPrivateKey` + full KeyObject metadata | `mbedtls_pk_parse_*` |
| P2 | `createDiffieHellman`/`ECDH` | `mbedtls_dhm_*`/`mbedtls_ecdh_*` |
| P3 | `X509Certificate` | `mbedtls_x509_crt_*` |
| P3 | `subtle.{sign,verify,importKey,exportKey,generateKey}` | — |

Reuse the cipher null-guard discipline from Track 0 to avoid new crashers.

**2026-06-20 `common/crypto` helper landing:** `lambda/js/test_shim/crypto.js` now provides the active Node test helper surface used by official crypto/TLS tests: `modp2buf`, `assertApproximateSize`, PEM regex helpers (`pkcs1*`, `spkiExp`, `pkcs8*`, `sec1*`), `testEncryptDecrypt`, `testSignVerify`, `hasOpenSSL()`, `hasOpenSSL3`, and lazy `opensslCli` detection. The helper intentionally does not claim asymmetric crypto is implemented; it lets official tests load the shared helper and then fail or skip on the underlying runtime feature. `make node-shim` now backs up/installs `crypto.js` into `ref/node/test/common/`. Added `test/node/common_crypto.js` + `.txt` to guard the static helper surface and safe OpenSSL probing. Verification: `./lambda.exe js test/node/common_crypto.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/common_crypto --gtest_brief=1`; `git diff --check`.

**2026-06-20 P3 random-fill landing:** `js_crypto.cpp` now exposes the existing OS entropy source through `crypto.randomFillSync(buf[, offset[, size]])`, callback-form `crypto.randomFill(buf[, offset[, size]], cb)`, and `crypto.getFips()`. Fill targets cover Buffer/TypedArray views through the typed-array current-data path, DataView through its backing buffer, and ArrayBuffer/SharedArrayBuffer through a small exported `js_get_arraybuffer_ptr_item()` helper so upgraded ArrayBuffer maps use the same backing-store lookup as the rest of the runtime. The same slice fixed the adjacent `Buffer.from(ArrayBuffer, byteOffset, length)` view path so official random-fill regression coverage observes ArrayBuffer-backed Buffers correctly. Added `test/node/crypto_random_fill.js` + `.txt` to guard in-place return identity, offset/size boundaries, callback arguments, and the no-FIPS runtime contract. Verification: `./lambda.exe js test/node/crypto_random_fill.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-randomfillsync-regression.js --no-log`; `make build-test`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_random_fill --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **18/120 pass, 102 fail, 0 crashed, 0 timed out, 0 regressions, 3 improvements**; `./test/test_node_gtest.exe --modules=buffer --timeout=15000 --gtest_brief=1` → **27/67 pass, 40 fail, 0 crashed, 0 timed out, 0 regressions, 2 improvements**; `git diff --check`.

**2026-06-20 HKDF landing:** `js_crypto.cpp` now implements RFC 5869 `crypto.hkdfSync()` and callback-form `crypto.hkdf()` on the existing digest/HMAC facade, returning ArrayBuffer results and preserving Node's async callback ordering. The same slice added a minimal symmetric `createSecretKey()` wrapper accepted by HKDF, made crypto byte extraction honor typed-array byte views, fixed `getHashes()`/`getCiphers()` array construction so iteration does not see leading `undefined` entries, and marks the mbedTLS backend as BoringSSL-like for Node's legacy-provider digest gate without enabling OpenSSL 3-only tests. Added `test/node/crypto_hkdf.js` + `.txt` to guard the RFC 5869 vector, secret-key input, ArrayBuffer return type, and async callback shape. Verification: `./lambda.exe js test/node/crypto_hkdf.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-hkdf.js --no-log`; `make build-test`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_basic:NodeModuleTests/NodeFileTest.Run/crypto_cipher:NodeModuleTests/NodeFileTest.Run/common_crypto:NodeModuleTests/NodeFileTest.Run/crypto_random_fill:NodeModuleTests/NodeFileTest.Run/crypto_hkdf --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **19/120 pass, 101 fail, 0 crashed, 0 timed out, 0 regressions, 4 improvements** (`test-crypto-ecb.js`, `test-crypto-hkdf.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-verify-failure.js`); `git diff --check`.

**2026-06-20 symmetric generateKey landing:** `js_crypto.cpp` now exposes `crypto.generateKeySync()` and callback-form `crypto.generateKey()` for AES and HMAC secret keys, backed by the existing OS entropy source. Minimal secret-key objects now share an `export()` method with `createSecretKey()`, so official keygen coverage can verify exported byte lengths without claiming full public/private KeyObject support. Added `test/node/crypto_generate_key.js` + `.txt` to guard AES/HMAC byte sizing, invalid AES length errors, and async callback ordering. Verification: `./lambda.exe js test/node/crypto_generate_key.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-secret-keygen.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_generate_key --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **20/120 pass, 100 fail, 0 crashed, 0 timed out, 0 regressions, 5 improvements** (`test-crypto-ecb.js`, `test-crypto-hkdf.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-secret-keygen.js`, `test-crypto-verify-failure.js`); `git diff --check`.

**2026-06-20 getCipherInfo landing:** `js_crypto.cpp` now exposes `crypto.getCipherInfo(nameOrNid[, options])` for the AES-CBC/CTR/GCM ciphers currently advertised by `getCiphers()`, including OpenSSL-compatible NID lookup, block/IV/key length metadata, mode strings, and option filters for `keyLength`/`ivLength`. Added `test/node/crypto_cipher_info.js` + `.txt` to guard name lookup, NID lookup, option acceptance/rejection, unknown-cipher `undefined`, and invalid argument errors. Verification: `./lambda.exe js test/node/crypto_cipher_info.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-getcipherinfo.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_cipher_info --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **21/120 pass, 99 fail, 0 crashed, 0 timed out, 0 regressions, 6 improvements** (`test-crypto-ecb.js`, `test-crypto-getcipherinfo.js`, `test-crypto-hkdf.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-secret-keygen.js`, `test-crypto-verify-failure.js`); `git diff --check`.

**2026-06-20 one-shot hash landing:** `js_crypto.cpp` now exposes `crypto.hash(algorithm, data[, outputEncoding])` as a one-shot wrapper over the existing digest facade, with Buffer/TypedArray/DataView/ArrayBuffer/string byte extraction, default hex output, `hex`/`base64`/`buffer` outputs, and Node-coded invalid argument/value errors. Added `test/node/crypto_hash_oneshot.js` + `.txt` to guard output parity with `createHash()` and argument validation. Verification: `./lambda.exe js test/node/crypto_hash_oneshot.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-oneshot-hash.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_hash_oneshot --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **22/120 pass, 98 fail, 0 crashed, 0 timed out, 0 regressions, 7 improvements** (`test-crypto-ecb.js`, `test-crypto-getcipherinfo.js`, `test-crypto-hkdf.js`, `test-crypto-oneshot-hash.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-secret-keygen.js`, `test-crypto-verify-failure.js`); `git diff --check`.

**2026-06-20 randomUUIDv7 landing:** `js_crypto.cpp` now exposes `crypto.randomUUIDv7([options])`, generating RFC 9562-style UUIDv7 strings with epoch-millisecond timestamp bits, version/variant bits, entropy-backed uniqueness, and the same `disableEntropyCache` option validation as `randomUUID()`. The official UUIDv7 timestamp assertion also surfaced a real runtime root cause: `Date.now()` returned fractional milliseconds, so `js_date_now()` now returns the clipped integer millisecond value expected by JavaScript and Node. Added `test/node/crypto_randomuuidv7.js` + `.txt` to guard UUID format, timestamp extraction, option validation, and uniqueness. Verification: `./lambda.exe js test/node/crypto_randomuuidv7.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-randomuuidv7.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_randomuuidv7 --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **23/120 pass, 97 fail, 0 crashed, 0 timed out, 0 regressions, 8 improvements** (`test-crypto-ecb.js`, `test-crypto-getcipherinfo.js`, `test-crypto-hkdf.js`, `test-crypto-oneshot-hash.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-randomuuidv7.js`, `test-crypto-secret-keygen.js`, `test-crypto-verify-failure.js`); `git diff --check`.

**2026-06-20 Argon2 unsupported landing:** `js_crypto.cpp` now exposes `crypto.argon2()` and `crypto.argon2Sync()` as explicit unsupported entry points that throw `ERR_CRYPTO_ARGON2_NOT_SUPPORTED` on the current non-OpenSSL-3.2 backend. This matches the official unsupported-backend test without pretending that the runtime has a real Argon2 implementation. Added `test/node/crypto_argon2_unsupported.js` + `.txt` to guard both sync and async-named entry points. Verification: `./lambda.exe js test/node/crypto_argon2_unsupported.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-argon2-unsupported.js --no-log`; `make build-test`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_argon2_unsupported --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **24/120 pass, 96 fail, 0 crashed, 0 timed out, 0 regressions, 9 improvements** (`test-crypto-argon2-unsupported.js`, `test-crypto-ecb.js`, `test-crypto-getcipherinfo.js`, `test-crypto-hkdf.js`, `test-crypto-oneshot-hash.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-randomuuidv7.js`, `test-crypto-secret-keygen.js`, `test-crypto-verify-failure.js`); `git diff --check`.

**2026-06-20 OpenSSL security-level helper landing:** `js_runtime.cpp` now resolves `require('internal/crypto/util')` with `getOpenSSLSecLevel()` returning `0`, giving official crypto tests the Node internal helper they use to adapt security-level expectations while preserving LambdaJS's mbedTLS-backed runtime model. Added `test/node/crypto_sec_level.js` + `.txt` to guard the internal module and numeric range. Verification: `./lambda.exe js test/node/crypto_sec_level.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-sec-level.js --no-log`; `make build-test`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_sec_level --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **25/120 pass, 95 fail, 0 crashed, 0 timed out, 0 regressions, 10 improvements** (`test-crypto-argon2-unsupported.js`, `test-crypto-ecb.js`, `test-crypto-getcipherinfo.js`, `test-crypto-hkdf.js`, `test-crypto-oneshot-hash.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-randomuuidv7.js`, `test-crypto-sec-level.js`, `test-crypto-secret-keygen.js`, `test-crypto-verify-failure.js`); `git diff --check`.

**2026-06-20 PBKDF2 + Buffer Latin-1 landing:** `js_crypto.cpp` now returns Buffer-marked `Uint8Array` results from `crypto.pbkdf2Sync()` and callback-form `crypto.pbkdf2()`, validates password/salt/iterations/keylen/digest with Node-coded errors, handles the digest/callback overload, and throws `ERR_CRYPTO_INVALID_DIGEST` for unsupported digest names. The official PBKDF2 vector also exposed a shared Buffer root cause: `Buffer.prototype.toString('latin1'|'binary')` now returns a valid UTF-8 Lambda string for Latin-1 code points, while `ascii` masks high bits like Node. Added `test/node/crypto_pbkdf2.js` + `.txt` to guard Buffer return shape, Latin-1 output, async result bytes, invalid digest, missing-digest overload, and non-finite keylen messages. Verification: `./lambda.exe js test/node/crypto_pbkdf2.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-pbkdf2.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_pbkdf2 --gtest_brief=1`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/buffer_basic:NodeModuleTests/NodeFileTest.Run/buffer_advanced --gtest_brief=1`; `make build-test`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **26/120 pass, 94 fail, 0 crashed, 0 timed out, 0 regressions, 11 improvements** (`test-crypto-argon2-unsupported.js`, `test-crypto-ecb.js`, `test-crypto-getcipherinfo.js`, `test-crypto-hkdf.js`, `test-crypto-oneshot-hash.js`, `test-crypto-pbkdf2.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-randomuuidv7.js`, `test-crypto-sec-level.js`, `test-crypto-secret-keygen.js`, `test-crypto-verify-failure.js`); `git diff --check`.

**2026-06-20 HMAC + Buffer Latin-1-from landing:** `lib/digest` now exposes MD5 through the existing mbedTLS digest facade, and `js_crypto.cpp` now validates HMAC algorithm/key inputs before allocating runtime state, accepts string/Buffer/TypedArray/DataView/ArrayBuffer/secret-key keys, links returned objects to `crypto.Hmac.prototype`, supports MD5/SHA-1/SHA-224/SHA-256/SHA-384/SHA-512 HMAC output, returns Buffer-marked raw digests, handles `hex`/`base64`/`latin1`/`ucs2` encodings, returns empty output after finalization, and provides the minimal `end()`/`read()` stream surface used by the official HMAC vector test. The same official test exposed the shared inverse Latin-1 root cause: `Buffer.from(str, 'latin1'|'binary')` now decodes UTF-8 Lambda strings by code point and writes the low byte, while `ascii` masks to 7 bits. Added `test/node/crypto_hmac.js` + `.txt` to guard constructor/prototype behavior, thrown encoding coercion, invalid key/digest errors, MD5/SHA-1/SHA-224 vectors, Buffer/Latin-1 digests, stream-style `end()`/`read()`, and secret-key input parity. Verification: `./lambda.exe js test/node/crypto_hmac.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-hmac.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_hmac --gtest_brief=1`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_basic:NodeModuleTests/NodeFileTest.Run/crypto_pbkdf2:NodeModuleTests/NodeFileTest.Run/buffer_basic:NodeModuleTests/NodeFileTest.Run/buffer_advanced --gtest_brief=1`; `make build-test`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **27/120 pass, 93 fail, 0 crashed, 0 timed out, 0 regressions, 12 improvements** (`test-crypto-argon2-unsupported.js`, `test-crypto-ecb.js`, `test-crypto-getcipherinfo.js`, `test-crypto-hkdf.js`, `test-crypto-hmac.js`, `test-crypto-oneshot-hash.js`, `test-crypto-pbkdf2.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-randomuuidv7.js`, `test-crypto-sec-level.js`, `test-crypto-secret-keygen.js`, `test-crypto-verify-failure.js`); `git diff --check`.

**2026-06-21 randomBytes/randomInt/WebCrypto random landing:** `js_crypto.cpp` now validates `crypto.randomBytes(size[, callback])` with Node-coded type/range errors, supports callback delivery as `(null, Buffer)`, and exposes hidden `pseudoRandomBytes`/`prng`/`rng` aliases; `pseudoRandomBytes` emits the expected one-shot `DEP0115` warning. `globalThis.crypto` now points at the crypto namespace and `crypto.getRandomValues()` fills integer typed-array views in place with the 65,536-byte quota, while `randomFill(Sync)` rejects invalid `offset`/`size` values without string/boolean coercion and preserves Node's range ordering. `crypto.randomInt([min,] max[, cb])` now supports the sync/callback overload set with safe-integer and range validation. The same slice normalized `common.expectWarning()` through an uninitialized local to avoid the Lambda parameter-reassignment lowering trap in the compatibility shim, and added `test/node/crypto_random.js` + `.txt` to cover the landed behavior. Verification: `./lambda.exe js test/node/crypto_random.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-random.js --no-log`; `make build-test`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_random --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **31/120 pass, 89 fail, 0 crashed, 0 timed out, 0 regressions, 16 improvements** (`test-crypto-argon2-unsupported.js`, `test-crypto-ecb.js`, `test-crypto-getcipherinfo.js`, `test-crypto-hkdf.js`, `test-crypto-hmac.js`, `test-crypto-key-objects-messageport.js`, `test-crypto-oneshot-hash.js`, `test-crypto-pbkdf2.js`, `test-crypto-randomfillsync-regression.js`, `test-crypto-randomuuidv7.js`, `test-crypto-sec-level.js`, `test-crypto-secret-keygen.js`, `test-crypto-subtle-cross-realm.js`, `test-crypto-subtle-zero-length.js`, `test-crypto-verify-failure.js`, `test-crypto-worker-thread.js`); `git diff --check`.

**2026-06-21 skip-list promotion:** `test/node/skip_list.txt` no longer excludes `test-crypto-random.js`; the direct upstream probe still exits 0, and the focused official crypto gate now counts it as a new pass. Verification: `./lambda.exe js ref/node/test/parallel/test-crypto-random.js --no-log`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **32/121 pass, 89 fail, 0 crashed, 0 timed out, 0 regressions, 17 improvements** including `test-crypto-random.js`.

**2026-06-21 cipher/Hash encoding landing:** `js_crypto.cpp` now tracks Cipher output encoding across `update()`/`final()`, throws `Cannot change encoding` when a stream switches valid output encodings, and throws `ERR_UNKNOWN_ENCODING` before state changes for invalid output encodings. The same slice added Hash stream-style `write()`/`end()`/`read()`, finalized-state errors, MD5 allowance on the Hash path to match existing HMAC/PBKDF2 support, and string input decoding for `Hash.update(data, encoding)` so base64/hex/latin1/ascii/ucs2 inputs feed the digest as bytes rather than raw UTF-8. Added `test/node/crypto_encoding.js` + `.txt` to guard the cipher encoding lock, unknown encoding, Hash base64/latin1 input, and Hash `end()`/`read()` behavior. Verification: `make build-test`; `./lambda.exe js test/node/crypto_encoding.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-encoding-validation-error.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-from-binary.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_encoding --gtest_brief=1`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_cipher:NodeModuleTests/NodeFileTest.Run/crypto_hash_oneshot:NodeModuleTests/NodeFileTest.Run/crypto_hmac --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **33/121 pass, 88 fail, 0 crashed, 0 timed out, 0 regressions, 18 improvements** including `test-crypto-encoding-validation-error.js`; `git diff --check`.

**2026-06-21 Sign/Verify update-surface landing:** `js_crypto.cpp` now exposes `crypto.createSign()` and `crypto.createVerify()` constructors with `update()`/`write()`/`end()` methods that share Hash's string-input decoding and correctly ignore input encodings for Buffer/TypedArray inputs. `Verify.verify()` validates the provided key/signature inputs and returns `false` on the current unsupported asymmetric backend instead of throwing on malformed signatures, while `Sign.sign()` remains explicitly unsupported until RSA/ECDSA key parsing/signing lands. Added `test/node/crypto_sign_verify_update.js` + `.txt` to guard Buffer update encoding and the conservative verify-false path. Verification: `make build-test`; `./lambda.exe js test/node/crypto_sign_verify_update.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-update-encoding.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-verify-failure.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_sign_verify_update --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **34/121 pass, 87 fail, 0 crashed, 0 timed out, 0 regressions, 19 improvements** including `test-crypto-update-encoding.js`; `git diff --check`.

**2026-06-21 Cipheriv/Decipheriv compatibility landing:** `js_mir_expression_lowering.cpp` now expands spread arguments for computed member calls (`obj[key](...args)`), which unblocked the upstream crypto class loop from passing the argument array as a single value. `js_crypto.cpp` now exports `Cipheriv`/`Decipheriv`, links their prototypes, supports `des-ede3-cbc`, `id-aes128-wrap`, and `aes-*-ecb`, decodes string input encodings in `Cipher.update()`, adds the stream-style `end()`/`read()` shim used by cipher tests, and validates cipher name/key/IV ordering with Node-coded errors. `js_buffer.cpp` now rejects oversized `Buffer.allocUnsafeSlow()` requests instead of silently truncating them to the runtime's 1 MiB cap. Added `test/js/spread_member_call.js` + `.txt` and `test/node/crypto_cipher_node_compat.js` + `.txt` to guard the shared lowering and crypto behavior. Verification: `make build`; `./lambda.exe js test/js/spread_member_call.js --no-log`; `./lambda.exe js test/node/crypto_cipher_node_compat.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-cipheriv-decipheriv.js --no-log`; `./test/test_node_module_gtest.exe '--gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_cipher_node_compat' --gtest_brief=1`; `./test/test_js_gtest.exe '--gtest_filter=JavaScriptTests/JsFileTest.Run/spread_member_call' --gtest_brief=1` (passed, with unrelated `lib_ramda.js` timeout/ASan cleanup noise from batch setup); `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **35/121 pass, 86 fail, 0 crashed, 0 timed out, 0 regressions, 20 improvements** including `test-crypto-cipheriv-decipheriv.js`.

**2026-06-21 DH/ECDH constructor/error landing:** `js_crypto.cpp` now exports `createDiffieHellman()`, `createDiffieHellmanGroup()`, `getDiffieHellman()`, `createECDH()`, `getCurves()`, and the `DiffieHellman`/`DiffieHellmanGroup`/`ECDH` constructors. Size-based DH construction generates prime bytes with mbedTLS MPI, named groups expose standard MODP2/MODP5 parameters, `getPrime()`/`getGenerator()` honor output encodings, and validation now matches LambdaJS's advertised OpenSSL 1.1.1 compatibility mode for DH size/generator/unknown-group errors. The ECDH surface is intentionally constructor metadata only; real ECDH key agreement remains a P2 backend item. Added `test/node/crypto_dh_constructor.js` + `.txt` to guard constructor/prototype linkage, DH parameter getters, MODP group construction, and `getCurves()`. Verification: `make build-test`; `make build`; `./lambda.exe js test/node/crypto_dh_constructor.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-classes.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-dh-constructor.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-dh-errors.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_dh_constructor --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **38/121 pass, 83 fail, 0 crashed, 0 timed out, 0 regressions, 23 improvements** including `test-crypto-classes.js`, `test-crypto-dh-constructor.js`, and `test-crypto-dh-errors.js`.

**2026-06-21 finite-field DH + padding landing:** `js_crypto.cpp` now implements finite-field `DiffieHellman.generateKeys()`, `computeSecret()`, `getPublicKey()`, `getPrivateKey()`, `setPublicKey()`, and `setPrivateKey()` on the existing mbedTLS MPI backend. Private keys are generated in the valid DH range, public keys and shared secrets are exported at the prime byte width, and `computeSecret()` rejects empty/small or oversized peer keys with Node-shaped crypto errors. `createDiffieHellman()` also now distinguishes the Node overload `createDiffieHellman(BufferPrime, '02', 'hex')` from the legacy two-argument Buffer-prime encoding form, which unlocks ArrayBuffer/DataView/TypedArray prime inputs in the MODP2 view test. The same slice added `Cipheriv`/`Decipheriv.setAutoPadding()`, OpenSSL-1.1.1-compatible `Cipher functions` error objects for wrong final block length / bad decrypt / no-padding block-length failures, and the empty padded ECB/CBC decrypt error expected by DH and padding tests. `test/node/crypto_dh_constructor.js` now guards secret parity, private-key public restore, encoded-generator overloads, and the padding error surfaces. Verification: `make build`; `./lambda.exe js ref/node/test/parallel/test-crypto-dh.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-dh-modp2.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-dh-modp2-views.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-padding.js --no-log`; `./lambda.exe js test/node/crypto_dh_constructor.js --no-log`; `make build-test`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/crypto_dh_constructor --gtest_brief=1`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` → **44/121 pass, 77 fail, 0 crashed, 0 timed out, 0 regressions, 29 improvements** including the new DH/padding passes `test-crypto-dh.js`, `test-crypto-dh-modp2.js`, `test-crypto-dh-modp2-views.js`, `test-crypto-dh-shared.js`, `test-crypto-dh-odd-key.js`, and `test-crypto-padding.js`.

**2026-06-21 ECDH + PEM Sign.sign landing:** `js_crypto.cpp` now implements ECDH key generation, shared-secret computation, key getters/setters, compressed/uncompressed/hybrid point export, `ECDH.convertKey()`, `secp256k1` curve support, `DEP0031` warning emission for `ecdh.setPublicKey()`, invalid public/private key validation, and key-pair mismatch detection on the mbedTLS ECP backend. The same slice added a narrow real `Sign.sign()` path for PEM private keys via `mbedtls_pk_parse_key()`/`mbedtls_pk_sign()` so ECDH tests can verify invalid ECDH operations do not poison later signing. `js_assert.cpp` now matches `assert.throws(fn, /re/)` against `String(thrown)` like Node, which fixed the upstream uninitialized ECDH getter regex assertions. Verification: `make build-test`; `./lambda.exe js ref/node/test/parallel/test-crypto-dh-curves.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-ecdh-convert-key.js --no-log`; `./lambda.exe js ref/node/test/parallel/test-crypto-ecdh-setpublickey-deprecation.js --no-log`; `./test/test_node_gtest.exe --modules=crypto --timeout=15000 --gtest_brief=1` -> **47/121 pass, 74 fail, 0 crashed, 0 timed out, 0 regressions, 32 improvements** including the new ECDH passes; `./test/test_node_gtest.exe --modules=assert --timeout=15000 --gtest_brief=1` -> **2/14 pass, 0 regressions**.

---

### Track E — dns Rebuild + AsyncLocalStorage (P2, independent, **~+35**)

E.1 **Fix dns (0 → ~20; partial 10/29 landed 2026-06-18).** The first slice completed `dns.lookup(host, [opts], cb)` option/error semantics for the official lookup suite, implemented real Promise shaping for `dns.promises.lookup`, wired `require('dns/promises')` to the Promise namespace, added IP-literal short-circuiting, and added the `internalBinding('cares_wrap').getaddrinfo` hook used by memory-error tests. The direct official `test-dns-lookup.js` now exits 0. The focused module gate is now **10/29**, **0 crashed**, **0 timed out**, **0 regressions**, **10 improvements**: `test-dns-default-order-ipv4.js`, `test-dns-default-order-ipv6.js`, `test-dns-default-order-verbatim.js`, `test-dns-lookup-promises-options-deprecated.js`, `test-dns-lookup.js`, `test-dns-lookupService.js`, `test-dns-memory-error.js`, `test-dns-promises-exists.js`, `test-dns-resolve-promises.js`, and `test-dns-set-default-order.js`.

The slice also fixed a shared assertion root cause: `assert.rejects()` now captures expected error/message state per Promise handler instead of using process-global statics, so concurrent rejection assertions no longer overwrite each other.

**2026-06-20 `common/dns` helper landing:** `lambda/js/test_shim/dns.js` now provides the active Node test helper surface: DNS packet `parseDNSPacket`/`writeDNSPacket` for A/AAAA/TXT/MX/NS/CNAME/PTR/SOA/SRV/CAA records, `errorLookupMock()`, `mockedErrorCode`, `mockedSysCall`, and `createMockedLookup()`. The implementation uses explicit Buffer big-endian reads/writes instead of the upstream helper's typed-array endian path, and `make node-shim` now backs up/installs `dns.js` into `ref/node/test/common/`. Added `test/node/common_dns.js` + `.txt` to guard packet round-tripping and mocked lookup behavior. Verification: `./lambda.exe js test/node/common_dns.js --no-log`; `./test/test_node_module_gtest.exe --gtest_filter=NodeModuleTests/NodeFileTest.Run/common_dns --gtest_brief=1`; `git diff --check`. Remaining E.1 work: implement real `dns.resolve{4,6,Mx,Txt,Srv,...}` record handling instead of the current lookup shortcut, add the `Resolver` class, and recheck the historical `snapshot-dns` stale-baseline deltas.

E.2 **Real AsyncLocalStorage (+15).** Propagate a context store across `nextTick`/microtask/timer/libuv callbacks (a context stack saved/restored around each scheduled callback in `js_event_loop.cpp`). Recovers `test-async-local-storage-*` and unblocks async-context http tests. Full `async_hooks` ID tracking remains out of scope.

---

### Track F — child_process fork/IPC + Fidelity Sweep (P2, independent, **~+35**)

F.1 **`fork()` + IPC (+20).** Since the binary is `lambda.exe`, `fork(mod)` spawns `lambda.exe js <mod>` with an IPC pipe; implement `child.send(msg)` / `child.on('message')` (JSON framing over the pipe), configurable `stdio`, and signal handling.

F.2 **Error-code & message fidelity sweep (+15).** Migrate the highest-traffic plain throw sites to coded helpers — `js_typed_array.cpp` (65 plain), `js_globals.cpp`, `js_runtime.cpp` — and align `AssertionError`/`TypeError` message strings to Node format. Directly lifts the "complete-code, low-pass" modules (`assert`, `buffer`, `process`, `path`).

---

## 6. Roadmap & Projected Results

### Priority matrix

| Track | Area | Est. | Effort | Depends | Priority |
|-------|------|-----:|--------|---------|----------|
| **0** | Stabilize + re-baseline | +30 | Low | — | **P0** |
| **A** | Stream core rebuild | +70 | High | — | **P0** |
| **C** | Wiring & quick wins | +55 | Low | — | **P1** |
| **B** | Re-platform I/O on streams | +75 | High | A | **P1** |
| **D** | Crypto asymmetric | +40 | High | — | **P1** |
| **E** | dns + AsyncLocalStorage | +35 | Medium | — | **P2** |
| **F** | child fork/IPC + fidelity | +35 | Medium | — | **P2** |

### Projected cumulative (discounted for overlap)

| Milestone | Passes | Rate |
|-----------|-------:|-----:|
| Current (locked baseline) | 1,462 | 41.5% |
| + Track 0 | 1,492 | 42.4% |
| + Track C | 1,544 | 43.9% |
| + Track A | 1,609 | 45.7% |
| + Track B | 1,679 | 47.7% |
| + Track D | 1,714 | 48.7% |
| + Track E | 1,744 | 49.5% |
| + Track F | 1,774 | 50.4% |
| **Stretch** | **1,850+** | **52%+** |

Estimates are deliberately discounted from the per-track raw sums (which total ~+340) because of inter-track overlap and the usual fidelity friction.

---

## 7. Non-Goals (unchanged from Node3, re-confirmed)

Disabled in the runner today: `http2` (268), `dgram` (75), `wasi`. Out of scope: full `worker_threads` threading (138 — single-threaded engine; stub passes counted), `cluster` multi-process (83), full interactive `repl`, native addons / N-API, full `fs.watch`, full `async_hooks` ID graph, full `inspector` protocol. These are ~825 tests of architectural mismatch, not incremental API gaps.

---

## 8. Success Criteria

| Metric | Target |
|--------|--------|
| Baseline passes | ≥ 1,775 (50%+) |
| **Zero crashes** | locked 26 → 0, and current `net`/`tls` spot-check crashers → 0 (Track 0 is a hard gate) |
| Zero regressions | Node-regression gate in CI (Track 0.2) |
| stream rate | ≥ 55% (50 → ≥117) |
| crypto rate | ≥ 45% (47 → ≥54) |
| zlib rate | ≥ 50% (8 → ≥30) |
| dns rate | ≥ 65% (0 → ≥20) |
| https | TLS-real; 0 regressions |
| Coded-error coverage | ≥ 60% of throw sites carry `ERR_*` |

---

## 9. Appendix: Measurement Methodology

So the numbers above are reproducible:

1. **Build:** `make release` (produces a fresh `lambda.exe`; note it *deletes* the test exe).
2. **Rebuild the runner:** `cd build/premake && make -f test_node_gtest.make config=debug_native`.
3. **Full run:** `./test/test_node_gtest.exe` → prints the Total/Passed/Failed/Crashed/Regressions/Improvements box; writes crashers to `temp/_node_official_crashers.txt`.
4. **Per-module:** `./test/test_node_gtest.exe --modules=<prefix> --timeout=15000`.
5. **Re-baseline:** `make node-update-baseline` (or `./test/test_node_gtest.exe --update-baseline`), which only writes if there are no regressions vs the current baseline.

Tests come from `ref/node/test/parallel/` (3,938 files); the runner filters by the enabled-module table in `test/test_node_gtest.cpp:110+` (dgram/http2/wasi disabled) and the external `test/node/skip_list.txt` (26 entries). PASS = exit 0 and no `Uncaught` in output.

### Failure samples

```
$ ./lambda.exe js ref/node/test/parallel/test-dns-lookup.js --no-log
# exits 0 after the 2026-06-18 Track E.1 lookup/promises slice

$ ./test/test_node_gtest.exe --modules=dns --timeout=15000 --gtest_brief=1
# Total 29, Passed 10, Failed 19, Crashed 0, Timed out 0,
# Regressions 0, Improvements 10. Remaining failures are outside
# the core test-dns-lookup.js path.

# Historical stale-baseline samples from the 2026-06-16 audit; recheck before
# treating them as current locked-baseline regressions:
$ lambda.exe js test-next-tick.js
Uncaught AssertionError: deepStrictEqual ...    # nextTick ordering had regressed

$ lambda.exe js test-buffer-swap-fast.js
Uncaught SyntaxError: Invalid eval source       # shared engine regression sample (also hrtime-bigint)

# js_https.cpp:39 — comment in source:
#   "In a full implementation, this would pipe TLS sockets into HTTP parsing"
#   → https currently delegates to plain HTTP on :443

# js_util.cpp — util.promisify / callbackify:
#   Node4 C.2 partial: callback-last Promise wrapper exists, default
#   multi-value results now resolve to the first value, customPromisifyArgs
#   named results work, and DEP0174 warnings emit. Official
#   test-util-promisify.js still fails on promise/mustCall drain accounting.
#   callbackify wrapper exists and official test-util-callbackify.js passes.
```
