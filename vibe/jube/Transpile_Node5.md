# Transpile Node Tune5 Proposal

Date: 2026-06-29
Status: Track A in progress
Scope: structural LambdaJS Node.js compatibility work against official Node.js
parallel tests.

## Goal

Raise LambdaJS Node.js official-test support by fixing structural runtime gaps,
not by adding single-test workarounds. Tune5 is intentionally not a continuation
of the Tune4 leftovers. It starts from the current official Node baseline,
current failing-test shape, and the current LambdaJS Node implementation.

The target is a boring but important step change:

- make the official Node harness a reliable regression signal;
- convert broad missing-function and fidelity failures into passing clusters;
- reduce crash/timeout quarantine pressure;
- move Node compatibility from per-module scaffolding toward shared Node
  runtime primitives.

## Current Evidence

Source artifacts inspected:

- `test/node/official_baseline.txt`
- `test/node/official_skip_list.txt`
- `test/node/official_slow_list.txt`
- `test/test_node_gtest.cpp`
- `lambda/js/js_*.cpp`
- `doc/dev/js/JS_14_Node_Compat.md`
- `doc/dev/js/JS_16_Testing.md`
- focused harness sample:
- `ref/node` updated from `320b450cd13` to `92b72d4f601` on 2026-06-29 to
  resolve baseline filenames missing from the local Node checkout;

```bash
./test/test_node_gtest.exe \
  --gtest_filter='*test_stream_base_typechecking*:*test_http_abort_before_end*:*test_tls_0_dns_altname*:*test_zlib_brotli_dictionary*:*test_async_hooks_asyncresource_constructor*:*test_crypto_async_sign_verify*:*test_worker_arraybuffer_zerofill*:*test_diagnostics_channel_bind_store*:*test_assert_checktag*:*test_buffer_arraybuffer*' \
  --timeout=15000 \
  --gtest_brief=1
```

The focused sample required a network-capable run because the harness does a
local socket preflight for `http` / `https` / `net` / `tls` tests.

## Baseline Snapshot

`test/node/official_baseline.txt` currently records:

| Metric | Count |
| --- | ---: |
| Passing baseline | 1744 |
| Total tests in recorded run | 3554 |
| Failed | 1810 |
| Missing | 0 |
| Timed out | 0 |
| Crashed | 17 |
| Previous baseline | 1730 |
| Regressions | 0 |
| Improvements | 14 |
| Slow list | 32 tests |
| Skip list | 76 tests |

The local `ref/node` checkout was refreshed to upstream `92b72d4f601` after the
initial Tune5 analysis. That resolved the earlier local drift where 75 passing
baseline filenames were absent from `ref/node/test/parallel`. The current tree
now contains all 1744 passing baseline filenames. Because the baseline header
still records the older `ref/node` commit `4d3198c6646`, Tune5 Track 0 should
still decide whether to refresh `official_baseline.txt` against `92b72d4f601`
before making acceptance claims.

The checked-in `Transpile_Node4.md` is now stale on the headline number: it
still describes the full-suite baseline as 1730 passing, while the current
baseline artifact records 1744.

## Failure Shape

Using the current checkout's runner prefix rules and updated `ref/node`
`92b72d4f601`, the current non-skip/non-slow official tree has 3561 enabled
tests. Of those, 1743 are in the current passing baseline and 1818 are not; the
remaining one passing baseline test is listed in `official_slow_list.txt`.

The largest active failing clusters are:

| Prefix | Pass | Fail | Total | Read |
| --- | ---: | ---: | ---: | --- |
| `http` | 121 | 259 | 380 | request/response lifecycle and stream-body semantics |
| `tls` | 46 | 165 | 211 | certificate/options/session fidelity and async lexical/event ordering |
| `worker` | 30 | 109 | 139 | mostly stubbed worker model |
| `crypto` | 62 | 69 | 131 | asymmetric crypto, KeyObject, async sign/verify |
| `cluster` | 15 | 67 | 82 | fork/IPC/process supervision semantics |
| `vm` | 30 | 63 | 93 | context isolation and timeout semantics |
| `diagnostics` | 6 | 62 | 68 | diagnostics_channel and async context binding |
| `repl` | 45 | 60 | 105 | lower priority, CLI/debuggability surface |
| `child-process` | 43 | 53 | 96 | IPC/fork/stdio fidelity |
| `stream` | 162 | 50 | 212 | remaining state-machine and validation gaps |
| `process` | 38 | 50 | 88 | process binding/env/signal/message fidelity |
| `whatwg` | 14 | 49 | 63 | TextEncoder/Decoder, web platform shims |
| `zlib` | 18 | 45 | 63 | brotli/zstd plus stream transform fidelity |
| `permission` | 15 | 44 | 59 | Node permission model and CLI flags |
| `https` | 21 | 40 | 61 | HTTPS agent/session/TLS-over-HTTP composition |
| `buffer` | 30 | 38 | 68 | ArrayBuffer/exotic/fidelity gaps |
| `fs` | 215 | 37 | 252 | near-strong, but async/watch/FileHandle gaps remain |

This ranking is not just "stream leftovers." Streams remain important, but the
current failing surface says Tune5 also needs async context, diagnostics,
worker/cluster process modeling, crypto, VM isolation, and fidelity work.

## Representative Failure Samples

The focused sample ran 10 selected failing official tests with 0 crashes and 0
timeouts. All failed as expected failures, which is useful: these are structural
semantics gaps, not harness instability.

| Test | Result | Symptom |
| --- | --- | --- |
| `test-assert-checktag.js` | fail | `AssertionError` message does not match Node |
| `test-async-hooks-asyncresource-constructor.js` | fail | missing expected exception |
| `test-buffer-arraybuffer.js` | fail | ArrayBuffer/Buffer strict equality mismatch |
| `test-crypto-async-sign-verify.js` | fail | async sign/verify result mismatch |
| `test-diagnostics-channel-bind-store.js` | fail | missing function in diagnostics-channel path |
| `test-http-abort-before-end.js` | fail | missing function in HTTP abort/lifecycle path |
| `test-stream-base-typechecking.js` | fail | missing expected exception |
| `test-tls-0-dns-altname.js` | fail | `Cannot access 'server' before initialization` |
| `test-worker-arraybuffer-zerofill.js` | fail | missing function in worker path |
| `test-zlib-brotli-dictionary.js` | fail | missing function in brotli path |

The three slowest representative failures were `http`, `stream`, and `tls`,
each around 5.1s. That is another sign that the remaining I/O tests do real
event-loop work before failing.

## Current Implementation Read

### Harness

`test/test_node_gtest.cpp` runs each official Node test in its own
`lambda.exe js` subprocess under `temp/node_test/<filename>/`, with a local
`test` symlink into `ref/node/test`. It loads:

- `test/node/official_baseline.txt`
- `test/node/official_skip_list.txt`
- `test/node/official_slow_list.txt`
- `test/node/official_serial_list.txt`

The harness already has the right safety shape: baseline tests are regressions
when they fail; non-baseline failures are skipped as expected failures; slow
tests are excluded by default; socket tests are preflighted to avoid false EPERM
failures.

The missing structural piece is reporting. The runner can write timings and
crasher manifests, but it does not produce a committed per-prefix pass/fail
inventory or failure-reason clustering. Tune5 should make that inventory a
first-class artifact.

### Stream and I/O

`lambda/js/js_stream.cpp` is now large and featureful, but it still stores
stream state in ordinary JS maps and marker properties such as `__flowing__`,
`__buffer__`, `_readableState`, and `_writableState`. It has many targeted
fixes, but it is not yet a shared C-level Readable/Writable/Duplex state
machine.

That matters because several other modules are still carrying their own I/O
models:

- `js_http.cpp`: HTTP parsing and lifecycle are native and libuv-backed, but
  request/response streams are still not ordinary shared stream-core instances.
- `js_net.cpp`: sockets are libuv-backed and high coverage now, but the official
  failures that remain are mostly composition/fidelity rather than raw TCP.
- `js_https.cpp`: has grown beyond the old thin comment, but still has a custom
  HTTP-over-TLS adapter surface and agent/session handling is incomplete.
- `js_zlib.cpp`: sync operations and some Transform constructors exist, but
  brotli/zstd families and full transform lifecycle are missing.

### Async Context and Diagnostics

`async_hooks` is routed to `js_get_async_hooks_namespace()`, and there are
selected `internal/async_hooks` and `internal/async_context_frame` namespaces.
However, official failures remain broad:

- `async` prefix: 30 failures out of 48 in the current tree;
- `diagnostics` prefix: 61 failures out of 67;
- representative diagnostics failure: `TypeError: is not a function`;
- representative AsyncResource failure: missing expected exception.

This points to a shared async-resource/context propagation layer rather than
isolated fixes in `stream.finished()`, timers, or diagnostics_channel.

### Crypto

`js_crypto.cpp` is substantial and includes symmetric crypto, hashing, HMAC,
PBKDF2/HKDF, random APIs, some DH/ECDH/sign/verify work, and WebCrypto slices.
The remaining official shape is no longer "crypto missing"; it is asymmetric
and object-model fidelity:

- `test-crypto-async-sign-verify.js` still fails;
- 62 current crypto-prefix failures remain;
- the likely cluster is KeyObject, Sign/Verify async paths, options/PSS, X509,
  and precise Node error surfaces.

### Worker, Cluster, Child Process

The worker and cluster prefixes are large failure pools:

- `worker`: 109 failures out of 139;
- `cluster`: 67 failures out of 82;
- `child-process`: 53 failures out of 95.

The runtime has useful `child_process` work and some fork/IPC surface, but
workers and cluster remain mostly compatibility stubs. Tune5 should decide
explicitly whether to implement a single-process worker emulation layer, a real
subprocess Lambda worker model, or keep most worker/cluster tests out of scope.
Leaving them half-enabled creates noisy failure volume.

### Fidelity Modules

Some modules have mature code but low official score because Node asserts exact
surface details:

- `assert`: 3 / 14 in the current tree; sample fails on exact `message`.
- `buffer`: 29 / 67; sample fails on ArrayBuffer/Buffer equality behavior.
- `util`: 11 / 26; `util.inspect` is slow-listed and `promisify` now passes the
  current baseline, but deeper inspect/deprecate/format fidelity remains.
- `path`: 7 / 13 plus several path crash skips.

These are not good candidates for broad rewrites. They need a fidelity harness:
expected error `{ name, code, message }`, descriptors, prototype chains, and
format strings.

## Tune5 Thesis

The highest-value structural work is not one module. It is four shared layers:

1. **Node official inventory and drift control** so every slice is measured
   against a pinned/current Node tree.
2. **Shared stream/I/O lifecycle core** so `http`, `https`, `net`, `zlib`,
   crypto transforms, stdio, and fs streams compose through one state machine.
3. **Shared async-resource/context propagation** so `async_hooks`,
   `AsyncLocalStorage`, diagnostics_channel, timers, streams, and HTTP callbacks
   stop needing local context hacks.
4. **Node fidelity kernel** for error codes/messages, descriptors, Buffer
   exotic behavior, and inspect/format output.

Worker/cluster and crypto asymmetric work are the two largest feature tracks
outside those shared layers.

## Proposed Tracks

### Track 0: Measurement and Harness Discipline

Goal: make the official Node suite actionable before implementing more runtime.

Work:

- Pin or refresh `ref/node` so `official_baseline.txt` and the checked-out Node
  tests agree.
- Add a generated per-prefix report from the harness:
  - total, passed, failed, skipped, slow, crashed, timed out;
  - top failing prefixes by absolute failures;
  - top slow expected failures;
  - active baseline names missing from the current `ref/node` tree.
- Preserve `temp/node_official_failures.log` and
  `temp/node_official_times.tsv` content into a checked-in summary only when
  explicitly updating a proposal or baseline report.
- Add a `--classify-failures` mode or postprocessor that buckets failure output
  by first error line: missing function, missing exception, message mismatch,
  TDZ/reference error, timeout, crash.

Acceptance:

- A fresh node baseline update can say which Node commit it used and whether
  any baseline-pass filenames are absent.
- `Transpile_Node5.md` can be updated from a single report command rather
  than manual ad hoc counting.
- No runtime change is accepted without a focused module run and a baseline-only
  regression gate.

### Track A: Stream and I/O Core

Goal: replace per-module I/O behavior with a shared Node stream lifecycle.

Work:

- Introduce C-level `JsReadableState`, `JsWritableState`, and `JsDuplexState`
  attached to stream objects, while preserving JS-visible `_readableState` and
  `_writableState` views.
- Centralize queueing, high-water-mark accounting, `needDrain`, `readable`,
  `data`, `end`, `finish`, `close`, and `error` ordering.
- Route lifecycle callbacks through `process.nextTick` / microtask boundaries
  consistently instead of firing synchronously from every module.
- Re-platform:
  - `net.Socket` as a real Duplex;
  - `http.IncomingMessage`, `ClientRequest`, and `ServerResponse`;
  - `https` over `tls.TLSSocket` plus HTTP parsing;
  - `zlib` transform constructors;
  - crypto `Cipher`, `Decipher`, `Hash`, and `Hmac` transform behavior where
    official tests require it.

Focused gates:

- `./test/test_node_gtest.exe --modules=stream --timeout=20000 --gtest_brief=1`
- `./test/test_node_gtest.exe --modules=http,https,net,tls --timeout=20000 --gtest_brief=1`
- selected zlib transform official tests.

Acceptance:

- Stream focused failures fall materially below the current 50 remaining
  current-tree failures.
- HTTP/HTTPS failures that are purely stream-lifecycle failures improve without
  adding HTTP-specific workarounds.
- No new crash or timeout entries are added to `official_skip_list.txt`.

### Track B: Async Hooks, AsyncLocalStorage, and Diagnostics

Goal: make async context a runtime primitive instead of a set of per-module
special cases.

Work:

- Define a `JsAsyncResource` model with async id, trigger id, type, and captured
  context.
- Add context capture/restore wrappers used by:
  - timers and `setImmediate`;
  - Promise reactions;
  - libuv callbacks;
  - stream callbacks and events;
  - HTTP/TLS/net callbacks;
  - diagnostics_channel bind/run helpers.
- Make `AsyncResource` validation/error behavior Node-shaped, including the
  constructor errors exposed by `test-async-hooks-asyncresource-constructor.js`.
- Expand diagnostics_channel from importable surface to working channel,
  subscribe/unsubscribe, bindStore/runStores, and tracing-channel semantics.

Focused gates:

- async-hooks official subset by filter/prefix;
- `diagnostics` prefix;
- existing local ALS tests such as `test/node/timers_async_local_storage.js`
  and `test/node/stream_finished_async_local_storage.js`.

Acceptance:

- Diagnostics failures no longer mostly report "is not a function."
- ALS context survives timers, stream callbacks, and representative HTTP/TLS
  callbacks.
- Existing stream/timer ALS local fixtures still pass.

### Track C: Fidelity Kernel

Goal: lift low-scoring but implemented modules by matching Node's observable
details.

Work:

- Build a shared Node error factory:
  - `name`, `code`, `message`;
  - correct prototype;
  - predictable stack first line;
  - reusable formatting for `ERR_INVALID_ARG_TYPE`, `ERR_INVALID_ARG_VALUE`,
    `ERR_OUT_OF_RANGE`, stream errors, crypto errors, and assertion errors.
- Expand descriptor/prototype helpers so native modules can easily set
  enumerability, writability, symbols, and constructor/prototype chains.
- Add a Buffer/ArrayBuffer fidelity pass:
  - shared backing details;
  - `Buffer.from(arrayBuffer, offset, length)`;
  - slice/subarray aliasing;
  - detached/resizable edge behavior where LambdaJS supports it.
- Add util/assert formatting pass:
  - `AssertionError` messages;
  - `assert.deepStrictEqual` diagnostics;
  - `util.inspect` depth, circular markers, colors, hidden fields, and
    constructor tags;
  - `util.format` and deprecation warning details.

Focused gates:

- `--modules=assert,buffer,util,path,process`
- representative samples: `test-assert-checktag.js`,
  `test-buffer-arraybuffer.js`, `test-util-inspect.js` with `--include-slow`
  when necessary.

Acceptance:

- Assert/buffer/util improvements come from shared helpers, not patching one
  official filename.
- Error-code migration reduces plain uncoded throws in touched modules.

### Track D: Crypto Asymmetric and KeyObject

Goal: close the next crypto cluster without disturbing already-passing symmetric
crypto.

Work:

- Finish `KeyObject` and key import/export surfaces needed by official tests.
- Implement complete `Sign` / `Verify` update/sign/verify options, including
  RSA/ECDSA/PSS shapes that mbedTLS can support.
- Implement async callback forms via the event loop, not synchronous callback
  invocation.
- Add `X509Certificate` and certificate helpers only to the level official
  tests require.

Focused gates:

- `--modules=crypto --timeout=20000 --gtest_brief=1`
- local `test/node/crypto_*` fixtures.

Acceptance:

- `test-crypto-async-sign-verify.js` moves from result mismatch to pass or to a
  narrower unsupported-crypto failure.
- No regression in symmetric cipher/hash/HMAC/PBKDF2/HKDF local fixtures.

### Track E: Worker, Cluster, and Process Model Decision

Goal: stop worker/cluster from being a large ambiguous failure bucket.

Work:

- Decide scope explicitly:
  - real subprocess workers using `lambda.exe js`;
  - single-process compatibility workers with message queues;
  - or mark selected worker/cluster classes as intentionally unsupported.
- If implementing:
  - `Worker` constructor, `postMessage`, `MessagePort`, transfer lists, and
    lifecycle events;
  - cluster primary/worker process modeling on top of the child-process IPC
    foundation;
  - structured clone / ArrayBuffer transfer semantics;
  - resource cleanup so worker tests do not orphan subprocesses.
- If not implementing:
  - move unavailable worker/cluster tests into documented skip/quarantine
    categories instead of leaving them as noisy expected failures.

Focused gates:

- `--modules=worker,cluster,child_process --timeout=20000 --gtest_brief=1`
- selected child-process IPC fixtures.

Acceptance:

- Worker/cluster failure counts either fall materially or are intentionally
  reduced by documented scope decisions.
- No orphan worker/cluster processes remain after focused runs.

### Track F: VM and Module Isolation

Goal: address `vm`, `module`, `require`, and debugger-adjacent failures that
come from missing realm/context boundaries.

Work:

- Give `vm.createContext` and `runInContext` a real global-object boundary.
- Implement enough `vm.Script` caching/options/timeout semantics to move the
  ordinary official `vm` subset.
- Strengthen CommonJS/module metadata:
  - `Module` children/cache behavior;
  - `require.cache`;
  - package main/wrapper edge cases;
  - built-in module mutation boundaries.

Focused gates:

- `--modules=vm,module`
- `require` and `internal` prefix filters where relevant.

Acceptance:

- VM failures stop being broad sandbox/isolation mismatches.
- Module/require fixes do not destabilize npm package resolution.

## Proposed Order

1. Track 0: refresh/pin evidence and generate a current official failure
   inventory.
2. Track C first small slice: shared Node error factory applied to
   `assert`/`buffer` samples. This is low-risk and improves test signal.
3. Track B first slice: AsyncResource constructor + AsyncLocalStorage context
   propagation for timers and diagnostics_channel bind/run.
4. Track A first slice: formal stream state structs under the existing JS
   visible state, then migrate one no-network stream cluster.
5. Track A/B integration: route stream callbacks through async resources.
6. Track D crypto asymmetric slice.
7. Track E scope decision for worker/cluster.
8. Track F VM/module isolation if worker/cluster is deferred.

## Implementation Status

### 2026-06-29 Track 0 slice: generated official inventory report

Landed a first Track 0 measurement slice:

- Added `test/node/node_official_report.py`, a postprocessor that reads the
  official Node harness configuration, `official_baseline.txt`,
  `official_skip_list.txt`, `official_slow_list.txt`, the current
  `ref/node/test/parallel` tree, and any latest `temp/` run artifacts.
- Added `make node-official-report`, which writes
  `temp/node_official_report.md` by default.
- The report now provides the per-prefix pass/fail/skip/slow inventory, active
  baseline filenames missing from `ref/node`, latest timing status counts,
  slowest non-passing timing rows, crash/timeout manifest rows, and a
  first-error-line failure classifier.

Verification:

```bash
python3 -B test/node/node_official_report.py
make node-official-report
git diff --check
```

Current generated report from the checked-in baseline plus latest temp run
artifacts is `temp/node_official_report.md`. Runtime compatibility has not been
changed in this slice; this is intentionally a harness/reporting foundation for
the next Node5 implementation slice.

### 2026-06-29 Track C slice: assert and Buffer fidelity

Landed the first runtime fidelity slice for the two representative official
failures from this proposal:

- `Buffer.from(arrayBuffer, offset, length)` and deprecated `Buffer(...)` now
  preserve shared `ArrayBuffer` backing, expose Buffer `.parent`, forward the
  third constructor argument, and use Node-shaped
  `ERR_BUFFER_OUT_OF_BOUNDS` offset/length errors.
- ArrayBuffer offset conversion now distinguishes omitted `length` from
  nonnumeric `length`: omitted means remaining bytes, while nonnumeric/`NaN`
  means zero bytes.
- Buffer invalid-input diagnostics now preserve constructor names such as
  `AB` in the `Received an instance of ...` suffix.
- `assert.deepStrictEqual()` now emits the Node-shaped Date/fake-Date
  check-tag message used by `test-assert-checktag.js`.
- `globalThis` and `process` now expose no-allocation identity predicates, and
  `util.isDeepStrictEqual()` rejects host singleton objects compared against
  structural copies. This fixes the shared root cause behind fake
  global/process equality without changing the runtime class/prototype behavior
  of those objects.

Verification:

```bash
make build
./lambda.exe js ref/node/test/parallel/test-buffer-arraybuffer.js --no-log
./lambda.exe js ref/node/test/parallel/test-assert-checktag.js --no-log
./lambda.exe js test/node/assert_basic.js --no-log
./lambda.exe js test/node/buffer_advanced.js --no-log
./test/test_node_gtest.exe '--gtest_filter=*test_assert_checktag*:*test_buffer_arraybuffer*' --gtest_brief=1
./test/test_node_gtest.exe --modules=assert,buffer --gtest_brief=1
```

Results:

- Direct official `test-buffer-arraybuffer.js`: pass.
- Direct official `test-assert-checktag.js`: pass.
- Focused official gtest filter: 2/2 passed, 0 regressions,
  `NEW_PASS test-assert-checktag.js`,
  `NEW_PASS test-buffer-arraybuffer.js`.
- Assert/buffer module sweep: 82 selected tests, 35 passed, 47 expected
  failures, 0 regressions, same two improvements.
- Local assert and Buffer fixtures still pass.
- The full baseline-only gate currently reports unrelated baseline drift in
  several non-assert/non-buffer tests. The stable reproduced subset remains
  failing after reversing this Node5 patch and rebuilding, so those failures are
  not introduced by this slice.

### 2026-06-29 Track B slice: AsyncResource and diagnostics stores

Landed the first async-context runtime slice for two Track B representative
official failures:

- `AsyncResource` constructor now validates public Node-shaped arguments:
  missing/non-string `type` throws `ERR_INVALID_ARG_TYPE`, empty string `type`
  throws `ERR_ASYNC_TYPE`, and invalid numeric `triggerAsyncId` throws
  `ERR_INVALID_ASYNC_ID`.
- `AsyncResource` preserves valid numeric/object `triggerAsyncId` options and
  existing `requireManualDestroy` behavior.
- `diagnostics_channel` channels now expose real `bindStore`, `unbindStore`,
  and `runStores` behavior backed by the existing AsyncLocalStorage context
  helper.
- Diagnostics store transforms now run before subscriber/callback execution,
  subscribers observe the bound stores during `runStores`, nested contexts
  restore correctly, and transform failures are contained and emitted through
  `uncaughtException` after `runStores` returns.
- The diagnostics failure also exposed a root-cause event-name bug in this
  path: deferred `uncaughtException` emission must use the real 17-byte event
  name, otherwise it misses listeners stored under the JS-created event key.
  The same 17-byte length is now used by process listener bookkeeping for
  `uncaughtException`.

Verification:

```bash
make build
./lambda.exe js ref/node/test/parallel/test-async-hooks-asyncresource-constructor.js --no-log
./lambda.exe js ref/node/test/parallel/test-diagnostics-channel-bind-store.js --no-log
./lambda.exe js test/node/timers_async_local_storage.js --no-log
./lambda.exe js test/node/stream_finished_async_local_storage.js --no-log
./test/test_node_gtest.exe '--gtest_filter=*test_async_hooks_asyncresource_constructor*:*test_diagnostics_channel_bind_store*' --gtest_brief=1
./test/test_node_gtest.exe '--gtest_filter=*test_async_hooks_run_in_async_scope_this_arg*:*test_async_hooks_prevent_double_destroy*:*test_async_hooks_disable_gc_tracking*:*test_async_hooks_asyncresource_constructor*' --gtest_brief=1
./test/test_node_gtest.exe '--gtest_filter=*test_diagnostics_channel_memory_leak*:*test_diagnostics_channel_module_import_error*:*test_diagnostics_channel_sync_unsubscribe*:*test_diagnostics_channel_tracing_channel_promise_early_exit*:*test_diagnostics_channel_web_locks*:*test_diagnostics_channel_worker_threads*:*test_diagnostics_channel_bind_store*' --gtest_brief=1
```

Results:

- Direct official `test-async-hooks-asyncresource-constructor.js`: pass.
- Direct official `test-diagnostics-channel-bind-store.js`: pass.
- Local timer and stream AsyncLocalStorage fixtures still pass.
- Focused official gtest filter: 2/2 passed, 0 regressions,
  `NEW_PASS test-async-hooks-asyncresource-constructor.js`,
  `NEW_PASS test-diagnostics-channel-bind-store.js`.
- Neighboring AsyncResource focused filter: 4/4 passed, 0 regressions.
- Diagnostics baseline guard plus bind-store: 7/7 passed, 0 regressions,
  `NEW_PASS test-diagnostics-channel-bind-store.js`.

### 2026-06-29 Track A slice: stream construct lifecycle

Landed the first no-network stream lifecycle slice:

- Stream constructor options now propagate `construct` to `_construct` across
  `Readable`, `Writable`, `Duplex`, and `Transform`.
- Each stream instance invokes `_construct(callback)` after its state, listener
  table, and public methods are installed.
- The construct callback is once-only: a second invocation emits
  `ERR_MULTIPLE_CALLBACK`, while construct errors are scheduled through the
  existing stream error path so listeners attached after construction can
  observe them.
- Public stream `destroy(err, callback)` now accepts the callback at the native
  method boundary and invokes it through the shared destroy completion path
  instead of dropping it through the previous one-argument arity clamp.
- Transform write completion now defers `drain` until after pending
  `readable` listeners have observed their current chunk. For async
  object-mode transforms with `highWaterMark: 0`, this prevents the next write
  from exposing a second transformed chunk before the consumer's already
  scheduled `setImmediate` turn.

Verification:

```bash
make build
./lambda.exe js ref/node/test/parallel/test-stream-construct.js --no-log
./lambda.exe js ref/node/test/parallel/test-stream-transform-hwm0.js --no-log
./test/test_node_gtest.exe '--gtest_filter=*test_stream_transform_hwm0*:*test_stream_construct*:*test_stream_err_multiple_callback_construction*:*test_stream_duplex_end*:*test_stream_base_prototype_accessors_enumerability*' --timeout=15000 --gtest_brief=1
./test/test_node_gtest.exe --modules=stream --timeout=20000 --gtest_brief=1
```

Results:

- Direct official `test-stream-construct.js`: pass.
- Direct official `test-stream-transform-hwm0.js`: pass.
- Focused official stream filter: 5/5 passed, 0 regressions,
  `NEW_PASS test-stream-construct.js`, `NEW_PASS test-stream-duplex-end.js`,
  `NEW_PASS test-stream-transform-hwm0.js`.
- Direct official `test-stream-err-multiple-callback-construction.js`: pass.
- Stream module sweep: 212 selected tests, 163 passed, 49 expected failures,
  0 crashes, 0 timeouts, 2 pre-existing baseline regressions still present
  (`test-stream-destroy.js`, `test-stream-iter-from-sync.js`), and 3
  improvements (`test-stream-construct.js`, `test-stream-duplex-end.js`,
  `test-stream-transform-hwm0.js`).

### 2026-06-29 Track A slice: stream duplex surface

Continued the stream no-network surface slice:

- Added `stream.duplexPair()` as two cross-wired `Duplex` endpoints. Each side
  is a real `Duplex` instance, writes push into the peer readable side, `_final`
  pushes EOF to the peer, and the existing writable machinery still owns cork,
  uncork, write callbacks, and empty-byte-chunk handling.
- Added a first structural `Duplex.from()` bridge for Node-style readable and
  writable streams plus promise inputs. Objects with `{ readable, writable }`
  are wrapped into a duplex facade; readable events feed the facade readable
  side, facade writes feed the wrapped writable side, and promise fulfillment or
  rejection maps to readable data/EOF or stream error.
- Added an initial `stream.compose(...)` export that chains normalized streams
  and returns a duplex wrapper over the first writable side and last readable
  side. This removes the missing-API failure boundary, but the full compose
  official test still has semantic `mustCall` gaps and remains a follow-up.

Verification:

```bash
make build
./lambda.exe js ref/node/test/parallel/test-stream-duplexpair.js --no-log
./lambda.exe js ref/node/test/parallel/test-stream-duplex-from.js --no-log
./lambda.exe js ref/node/test/parallel/test-stream-compose.js --no-log
./test/test_node_gtest.exe --modules=stream --timeout=20000 --gtest_brief=1
```

Results:

- Direct official `test-stream-duplexpair.js`: pass.
- Direct official `test-stream-duplex-from.js`: advanced past the previous
  missing `Duplex.from`/Promise boundary, but still fails on pending
  `mustCall`s and leak reporting from later unsupported forms.
- Direct official `test-stream-compose.js`: advanced past the previous missing
  `stream.compose` export, but still fails on pending `mustCall`s.
- Stream module sweep: 212 selected tests, 164 passed, 48 remaining failures,
  0 crashes, 0 timeouts, 2 pre-existing baseline regressions still present
  (`test-stream-destroy.js`, `test-stream-iter-from-sync.js`), and 4
  improvements (`test-stream-construct.js`, `test-stream-duplex-end.js`,
  `test-stream-duplexpair.js`, `test-stream-transform-hwm0.js`).

### 2026-06-29 Track A continuation: Duplex.from web and async edges

Extended the stream-duplex surface without adding test-specific branches:

- `ReadableStream` and `WritableStream` constructors now preserve their
  underlying source/sink argument through both `globalThis` and
  `require('stream/web')` constructors.
- The minimal WebStream shims now keep queued readable chunks, closed/read
  state, reader objects, writable sinks, writer objects, and sink
  `write`/`close` calls.
- `require('buffer').Blob` is now constructable from the buffer namespace, which
  unblocks Blob inputs to `Duplex.from()`.
- `Duplex.from()` now bridges Blob, WebReadable, WebWritable, and mixed
  `{ readable, writable }` web-stream objects into Node-style duplex facades.
- Readable-side errors from wrapped `{ readable, writable }` pairs now destroy
  the facade and the wrapped writable; `_read` exceptions are routed through
  stream `destroy(err)`; duplicate writable callback errors are suppressed.
- `pipeline()` now normalizes iterable sources with `Readable.from()` and invokes
  a function final stage as an async sink instead of requiring every stage to
  already expose `.pipe()`.
- Async-generator lowering now preserves `mt->in_async` while compiling the
  shared generator state machine. This lets `await` inside async generators use
  the async-generator suspension marker instead of the synchronous await helper,
  fixing `for await` over pending stream iterator promises.
- `Duplex.from(function)` now routes promise-like function return values through
  the existing promise-backed duplex helper, so rejected async function results
  are surfaced as stream `error` events.

Verification:

```bash
make build
./lambda.exe js ref/node/test/parallel/test-stream-duplexpair.js --no-log
./lambda.exe js ref/node/test/parallel/test-stream-duplex-from.js --no-log
```

Results:

- `make build`: pass.
- Direct official `test-stream-duplexpair.js`: still pass.
- Direct official `test-stream-duplex-from.js`: pass.
- Reduced async-generator probes now resolve streamed `for await` values
  without `iterator result is not an object`.
- Reduced `Duplex.from(async function)` probe now emits the rejected promise as
  `error: myCustomError`.

## Verification Policy

Every implementation slice should finish with:

```bash
make build
./test/test_node_gtest.exe --modules=<focused-modules> --timeout=20000 --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --timeout=20000 --gtest_brief=1
git diff --check
```

For `http`, `https`, `net`, `tls`, `worker`, `cluster`, or child-process slices,
run the harness from a context where the local socket preflight passes. A
sandbox EPERM preflight failure is not evidence about LambdaJS behavior.

For performance-sensitive or timeout-prone Node tests, use release builds before
making performance claims:

```bash
make release
./test/test_node_gtest.exe --modules=<focused-modules> --timeout=20000 --gtest_brief=1
```

## Non-Goals

- Do not implement HTTP/2, dgram, WASI, full ICU, V8 snapshots, SEA, or native
  addons as part of Tune5.
- Do not add filename-specific branches for official tests.
- Do not expand skip lists to hide implementable failures unless Track E makes
  an explicit scope decision for worker/cluster.
- Do not use debug builds for timing claims.

## Acceptance Criteria

Tune5 is worth accepting only if it produces:

- a refreshed official Node inventory tied to the active `ref/node` commit;
- zero new baseline regressions;
- no new crash/timeout quarantine unless documented as an intentional scope
  decision;
- at least one structural cluster win, measured by focused official subsets:
  - stream/I/O lifecycle;
  - async context/diagnostics;
  - fidelity modules;
  - crypto asymmetric;
  - worker/cluster/process model;
- a documented explanation for rejected or deferred paths so Node6 does not
  repeat them.
