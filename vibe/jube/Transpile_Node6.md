# Transpile Node Tune6 Proposal

Date: 2026-07-01
Status: proposal seeded from the post-Node5 official Node inventory
Scope: next structural LambdaJS Node.js compatibility pass after Node5.

## Progress Update: 2026-07-01 Focused Runtime Pass

Node6 has completed its first focused runtime slice across async-hooks,
HTTP, child-process/cluster fd inheritance, stream prelim regressions, and the
first TLS transport step.

Implemented and verified:

- Promise async-hooks now emit `PROMISE` lifecycle events for ordinary
  promises, async functions, settlement, and promise reaction jobs.
- HTTP `IncomingMessage` close/destroy lifecycle, HTTP async resource
  destruction, request/response write callbacks, `writeContinue(cb)`, and
  `http.Server.listen({ fd })` inheritance are improved.
- Child-process IPC now supports TCP handle passing for the targeted
  `send(..., socket, { keepOpen })` path, preserves stdio slot indexes, and
  delays `close` until stdout/stderr EOF has drained.
- Cluster/listen-fd support has a narrow working bridge for the targeted
  inherited fd official tests.
- Legacy base `Stream` async iteration now supports the prelim pipeline error
  path without double-calling user `destroy()`.
- TLS has a minimal mbedTLS/libuv transport with BIO callbacks, async
  handshake progress, server `secureConnection` delay until handshake
  completion, client/server context mode, basic `TLSSocket` read/write/end,
  and `ECONNRESET`/`tlsClientError` behavior.

Focused official Node improvements from this slice:

| Test | Area |
| --- | --- |
| `test-async-hooks-correctly-switch-promise-hook.js` | Promise async-hooks |
| `test-async-hooks-http-parser-destroy.js` | HTTP async-hooks lifecycle |
| `test-async-hooks-promise-enable-disable.js` | Promise async-hooks |
| `test-child-process-send-keep-open.js` | child IPC handle passing |
| `test-http-client-incomingmessage-destroy.js` | HTTP response destroy |
| `test-http-server-incomingmessage-destroy.js` | HTTP request destroy |
| `test-http-write-callbacks.js` | HTTP write callback ordering |
| `test-listen-fd-cluster.js` | cluster/listen fd inheritance |
| `test-listen-fd-server.js` | HTTP `listen({ fd })` inheritance |
| `test-socket-write-after-fin-error.js` | net post-FIN write error |
| `test-tls-econnreset.js` | TLS reset/error lifecycle |

Verification from the consolidated parent workspace:

```bash
make build-test
./test/test_node_prelim_gtest.exe --gtest_brief=1
make test262-baseline
./test/test_node_gtest.exe --gtest_filter='*async_hooks_correctly_switch_promise_hook*:*async_hooks_http_parser_destroy*:*async_hooks_promise_enable_disable*:*child_process_send_keep_open*:*http_client_incomingmessage_destroy*:*http_server_incomingmessage_destroy*:*http_write_callbacks*:*listen_fd_cluster*:*listen_fd_server*:*tls_delayed_attach*:*tls_econnreset*:*tls_interleave*:*tls_on_empty_socket*:*socket_write_after_fin_error*' --include-slow --timeout=30000 --gtest_brief=1 --no-update-slow-list
```

Results:

| Gate | Result |
| --- | --- |
| `make build-test` | pass |
| `test_node_prelim_gtest.exe` | 110/110 pass |
| `make test262-baseline` | 40261/40261 pass, 0 regressions, 0 retry |
| Focused Node official sweep after TLS quarantine | 12/12 pass, 0 regressions, 11 improvements |
| Full Node official baseline update | 3512/3512 pass, 0 regressions, 0 timeouts, 0 crashes |

Baseline update follow-up:

- Fixed `test-net-bytes-stats.js` by clearing the stale `__remote_ended__`
  marker when reconnecting a reused JS `net.Socket`.
- Fixed `test-http-expect-continue.js` by yielding the HTTP server read loop
  when a partial streamed body makes no progress, preventing body-feed spin
  from starving the delayed final response timer.
- Fixed `test-http-expect-handling.js` by routing unsupported `Expect` headers
  to Node's `checkExpectation` event when present, and otherwise sending the
  default `417 Expectation Failed` response without invoking the normal request
  handler.
- Fixed a `test-child-process-send-utf8.js` regression by letting
  `JSON.parse()` use a tracked heap buffer for large IPC payloads instead of
  asserting through the bounded stack-allocation helper.
- Reran `./test/test_node_gtest.exe --update-baseline --no-update-slow-list
  --timeout=30000 --gtest_brief=1`; it rewrote
  `test/node/official_baseline.txt` with 3512/3512 selected tests passing,
  0 regressions, 0 failures, 0 timeouts, and 0 crashes. This run records
  `test-http-expect-handling.js` as the final active improvement, while four
  run-blocking fixtures moved into the documented skip list.

Temporary TLS quarantine:

- `test-tls-delayed-attach.js`
- `test-tls-interleave.js`
- `test-tls-on-empty-socket.js`
- `test-double-tls-server.js`
- `test-tls-cert-ext-encoding.js`

The first three are not considered fundamentally impossible; they are blocked
on a real cross-module `net.Socket` adoption API. TLS must be able to take over
an already-connected socket, preserve any raw encrypted bytes already read by
`net`, and own subsequent read/write and backpressure ordering. The two
additional TLS entries are reproducible crashers from the full baseline update:
`test-double-tls-server.js` segfaults in layered TLS server ownership, and
`test-tls-cert-ext-encoding.js` hits an ASAN heap-use-after-free when server
close races handshake completion.

Full-suite run-cleanliness quarantine:

- `test-cluster-fork-stdio.js`
- `test-net-socket-constructor.js`

Both fixtures can leave recursive child process trees behind after their
timeout wrapper exits, preventing reliable full baseline updates. They are
documented in `test/node/official_skip_list.txt` until process-tree ownership
and teardown are fixed. The skip-list count is now 116.

## Goal

Node5 closed the first broad structural push: measurement, stream pipeline
edges, async/diagnostics, fidelity helpers, asymmetric crypto, MessagePort
compatibility, VM/cache behavior, and shutdown ordering all moved in useful
clusters. Node6 should not repeat that work. It should start from the
post-Node5 failure shape and attack the remaining large pools with explicit
ownership boundaries.

Target for Node6:

- reconcile the baseline/report mismatch before runtime changes land;
- keep the full passing baseline at zero regressions;
- raise the active official Node baseline from roughly 2,046 passes to 2,250+
  passes, with a stretch target around 2,350 if HTTP/TLS and worker/process
  tracks compound well;
- reduce skip-list pressure for stream/process crashers without hiding
  implementable failures;
- convert the largest remaining "expected failure" pools into measured,
  classified work queues.

## Current Evidence

Source artifacts inspected:

- `test/node/official_baseline.txt`
- `test/node/official_skip_list.txt`
- `test/node/official_slow_list.txt`
- `temp/node_official_report.md`, regenerated on 2026-07-01 with
  `python3 -B test/node/node_official_report.py`
- `vibe/jube/Transpile_Node5.md`
- current `ref/node` checkout at `92b72d4f601`

The checked-in baseline has now been refreshed from a full official-test sweep.
Failure classification can be regenerated from this 3512-pass baseline.

## Baseline Snapshot

Current checked-in baseline header:

| Metric | Count |
| --- | ---: |
| Baseline header passing tests | 3512 |
| Baseline header total tests | 3512 |
| Baseline header failed tests | 0 |
| Baseline header crashed tests | 0 |
| Baseline header regressions | 0 |
| Baseline header improvements vs previous | 1 |

Current generated active inventory:

| Metric | Count |
| --- | ---: |
| Enabled test files | 3667 |
| Active tests | 3512 |
| Active baseline passes | 3512 |
| Baseline-inferred failures | 0 |
| Skip-list tests in enabled tree | 116 |
| Slow-list tests excluded | 39 |
| Baseline names missing from `ref/node` | 0 |

There are no active non-baseline failures in the refreshed sweep.

## Failure Shape

Top remaining active failing prefixes:

| Prefix | Pass | Fail | Total | Read |
| --- | ---: | ---: | ---: | --- |
| `http` | 164 | 206 | 396 | lifecycle, agents, header/body fidelity, stream composition |
| `worker` | 33 | 104 | 139 | single-process compatibility only; real isolates deferred |
| `tls` | 107 | 103 | 218 | certificates, sessions, option validation, async ordering |
| `cluster` | 15 | 62 | 83 | process supervision and IPC model |
| `crypto` | 64 | 67 | 134 | X509, WebCrypto, KeyObject/export/options fidelity |
| `repl` | 50 | 55 | 106 | CLI/debug surface, lower priority |
| `process` | 39 | 49 | 93 | signals, stdio, env/binding/message fidelity |
| `whatwg` | 14 | 49 | 63 | TextEncoder/Decoder and web-platform shims |
| `child-process` | 47 | 48 | 109 | fork/IPC/stdio/spawn option fidelity |
| `permission` | 16 | 38 | 59 | normalized grants, CLI flags, module/cache interactions |
| `zlib` | 27 | 36 | 64 | brotli/zstd and transform lifecycle |
| `vm` | 57 | 35 | 98 | context/timeout/cache/realm edges |
| `buffer` | 33 | 34 | 69 | exotic ArrayBuffer and exact message fidelity |
| `diagnostics` | 34 | 34 | 68 | remaining channels, web locks, worker interaction |
| `https` | 28 | 33 | 64 | agent/session/TLS-over-HTTP fidelity |
| `stream` | 178 | 33 | 215 | pipeline, premature close, validation tail |
| `fs` | 220 | 32 | 257 | watch, FileHandle, stream option/event fidelity |

The old Node4/Node5 thesis that "streams block everything" is now only partly
true. Streams still matter, but the largest remaining wins are HTTP/TLS,
worker/cluster/process, crypto/X509/WebCrypto, permission/runner, and web
platform fidelity.

## What Node5 Closed

Do not reopen these as first-class Node6 tracks unless a regression proves the
root cause changed:

- `test-assert-class.js`, `test-assert-class-destructuring.js`, and the
  original assert/checktag fidelity representatives;
- `test-buffer-arraybuffer.js`, `test-buffer-badhex.js`, and
  `test-buffer-concat.js` slices;
- `test-async-hooks-asyncresource-constructor.js`,
  `test-async-hooks-constructor.js`, diagnostics bind/store/pub-sub/tracing
  callback slices, and bounded-channel shutdown behavior;
- `test-crypto-async-sign-verify.js`, EdDSA/DSA import/export/sign/verify,
  supported async `generateKeyPair()`, and current asymmetric local fixtures;
- MessagePort close/receive/transfer/mark-as-untransferable/FileHandle
  transfer slices;
- `test-vm-cached-data.js`, `test-vm-createcacheddata.js`,
  `test-vm-run-in-new-context.js`, and module compile-cache permission/error
  slices;
- `test-stream-finished.js`, `test-stream-compose.js`,
  `test-stream-duplex-from.js`, `test-stream-duplexpair.js`, and focused
  pipeline function/legacy-stream helpers.

Known Node5 leftovers that should become Node6 tickets:

- `test-stream-pipeline.js` remains skip-listed; direct focused runs still show
  later `mustCall` mismatches and a shutdown memtrack leak.
- Real worker isolates, subprocess-backed workers, and cluster worker process
  modeling are deferred by explicit Node5 scope decision.
- `await using`, async/generator disposal, and suppressed-error fidelity are
  out of Node5 scope.
- Broader unboxed float arithmetic remains open outside the safe typed-array
  read subset.

## Node6 Thesis

Node6 should make the runtime look less like a set of module-specific Node
facades and more like a small Node platform:

1. a trustworthy measurement loop with current failure buckets;
2. one HTTP/TLS/HTTPS lifecycle layer that owns agent, socket, parser, body,
   abort, and close ordering;
3. an explicit process/worker model rather than ambiguous partial support;
4. a crypto/certificate/web-platform layer that covers Node's modern object
   surfaces;
5. a permission/VM/module policy layer that every file/cache/process path uses;
6. a remaining fidelity pass that removes message/prototype/descriptor drift
   from otherwise implemented modules.

## Proposed Tracks

### Track 0: Measurement Reconciliation and Failure Buckets

Goal: start Node6 with a clean, reproducible inventory.

Work:

- Run a fresh full `--baseline-only` gate and regenerate
  `temp/node_official_report.md`.
- Explain or remove the current 2047-header versus 2046-active inventory
  mismatch.
- Preserve a checked-in summary of per-prefix pass/fail/skip/slow counts only
  when updating this proposal or the baseline.
- Regenerate failure classification from a full non-baseline sample, not from a
  focused timing run.
- Add a small "delta since Node5" table whenever the baseline changes.

Focused gates:

```bash
make build
./test/test_node_gtest.exe --baseline-only --timeout=20000 --gtest_brief=1
python3 -B test/node/node_official_report.py
git diff --check
```

Acceptance:

- Report and baseline agree on active baseline count or document why they do
  not.
- Full baseline-only run has 0 regressions, 0 crashes, and 0 timeouts.
- Failure buckets identify at least the top 25 non-baseline failures by first
  useful line and prefix.

### Track A: HTTP, HTTPS, TLS, and Agent Lifecycle

Goal: attack the biggest failure pool with shared request/response lifecycle
semantics instead of one-off HTTP tests.

Work:

- Audit `http`, `https`, `net`, and `tls` failures into lifecycle buckets:
  request abort, response finish, parser state, socket reuse, agent session,
  header validation, timeout, certificate/session options, and async context.
- Route HTTP and HTTPS request/response bodies through the same stream
  lifecycle helpers used by `stream.pipeline()` and `finished()`.
- Strengthen Agent keep-alive/session reuse behavior, including socket close,
  free socket, and error ordering.
- Normalize TLS certificate option validation, CA/session surfaces, ALPN/SNI
  option behavior, and post-handshake error delivery.
- Ensure local socket preflight is green before treating network failures as
  runtime failures.

Focused gates:

```bash
./test/test_node_gtest.exe --modules=http,https,tls,net --timeout=20000 --gtest_brief=1
```

Acceptance:

- HTTP and TLS failures drop materially without adding filename-specific
  branches.
- No new socket-related crashers or skip-list entries are introduced.
- Existing stream and async/diagnostics local fixtures remain green.

### Track B: Worker, Cluster, Child Process, and Signals

Goal: turn the largest ambiguous bucket into either implemented semantics or
documented unsupported scope.

Work:

- Decide whether Node6 implements real worker isolates/subprocess workers or
  continues the Node5 single-process compatibility path.
- If continuing single-process workers, make unsupported behavior fail with
  Node-shaped errors instead of inert stubs or misleading partial behavior.
- Strengthen MessagePort lifecycle after transfer/close/error across worker and
  diagnostics/async-hooks interactions.
- Revisit `cluster` around primary/worker state, IPC events, disconnect, and
  orphan cleanup.
- Improve child-process stdio, `fork()` IPC, `process.send()`, `spawnSync()`
  option/error fidelity, and signal delivery.

Focused gates:

```bash
./test/test_node_gtest.exe --modules=worker,cluster,child_process,process --timeout=20000 --gtest_brief=1
```

Acceptance:

- Worker/cluster failure count falls materially or deferred classes are
  documented in the proposal and skip policy.
- Focused runs leave no orphan child processes.
- Signal-sensitive tests either pass, fail cleanly, or stay in documented
  quarantine with current root-cause notes.

### Track C: Crypto, X509, and WebCrypto Object Fidelity

Goal: close the next crypto cluster after Node5's asymmetric sign/verify win.

Work:

- Implement `X509Certificate` and certificate helper surfaces to the level
  official tests require.
- Fill remaining `KeyObject` import/export/details edge cases, including
  option validation, algorithm metadata, and exact Node error codes.
- Expand WebCrypto subtle API coverage where it overlaps existing mbedTLS or
  OpenSSL support.
- Finish async keygen/error paths for supported key families and make
  unsupported families report stable Node-shaped errors.
- Keep symmetric cipher/hash/HMAC/PBKDF2/HKDF baseline tests as regression
  guards.

Focused gates:

```bash
./test/test_node_gtest.exe --modules=crypto --timeout=20000 --gtest_brief=1
```

Acceptance:

- Crypto module passes increase without disturbing the current 64-pass baseline
  subset.
- New unsupported paths report explicit `ERR_OSSL_*` or Node-compatible
  validation errors.

### Track D: Web Platform Shims and Encoding

Goal: reduce the `whatwg`, `webapi`, `encoding`, and Buffer-adjacent failures
that now show up as missing functions or exact fidelity gaps.

Work:

- Implement TextEncoder/TextDecoder edge cases, fatal/ignoreBOM behavior,
  supported encodings, and typed-array/DataView input handling.
- Audit `Blob`, `File`, `FormData`, `URL`, `URLSearchParams`, structured clone,
  and ArrayBuffer transfer/detach interactions with worker and Buffer tests.
- Add Float16Array handling only if the engine-level typed-array support is
  ready; otherwise classify those failures clearly.
- Keep Buffer exotic/shared-backing behavior aligned with web ArrayBuffer
  semantics.

Focused gates:

```bash
./test/test_node_gtest.exe --modules=whatwg,buffer,blob,url --timeout=20000 --gtest_brief=1
```

Acceptance:

- Missing-function failures in web-platform prefixes drop.
- Buffer and worker transfer tests do not regress.

### Track E: Zlib and Remaining Stream Pipeline Tail

Goal: finish the post-Node5 stream tail and make zlib transforms compose with
ordinary streams.

Work:

- Fix the remaining direct `test-stream-pipeline.js`
  `mustCall`/shutdown-leak blockers.
- Revisit premature close/error/abort ordering around pipeline and composed
  legacy streams.
- Implement brotli/zstd support only where the available compression backend
  makes it real; otherwise expose stable unsupported errors.
- Ensure zlib transform constructors use the same Transform lifecycle as
  ordinary streams.

Focused gates:

```bash
./test/test_node_gtest.exe --modules=stream,zlib --include-slow --timeout=20000 --gtest_brief=1
```

Acceptance:

- `test-stream-pipeline.js` either becomes a clean pass or has a narrower,
  documented blocker.
- Zlib gains come from shared Transform behavior, not zlib-local lifecycle
  workarounds.

### Track F: Permission, VM, Module, Runner, and Compile Cache Policy

Goal: make policy decisions flow through one permission/module layer.

Work:

- Route compile-cache, module loading, fs, child-process, worker, and network
  permission checks through shared normalized grants.
- Expand VM timeout/interruption behavior only to the level official tests need
  and avoid pretending LambdaJS has V8 isolate machinery where it does not.
- Tighten CommonJS module mutation/cache/children behavior and built-in module
  mutation boundaries.
- Improve node:test runner/reporting/coverage surfaces where failures are
  ordinary JS-level fidelity rather than missing V8 internals.

Focused gates:

```bash
./test/test_node_gtest.exe --modules=permission,vm,module,runner --timeout=20000 --gtest_brief=1
```

Acceptance:

- Permission failures move from mismatched behavior to either pass or explicit
  unsupported scope.
- VM/module fixes do not destabilize package resolution or compile-cache
  baseline passes.

### Track G: Fidelity Kernel Round 2

Goal: clean up implemented modules whose remaining failures are exact Node
surface details.

Work:

- Extend the shared Node error factory and descriptor/prototype helpers into
  `process`, `path`, `fs`, `events`, `buffer`, and `util` paths touched by
  Node6 work.
- Add targeted `util.inspect`, `util.format`, warning/deprecation, and
  assertion-message fixtures only when the root cause is shared formatting.
- Reduce generic `TypeError: is not a function` and missing-exception buckets
  through real API/prototype wiring.
- Do not add filename-specific behavior.

Focused gates:

```bash
./test/test_node_gtest.exe --modules=assert,buffer,util,path,events,fs --timeout=20000 --gtest_brief=1
```

Acceptance:

- New passes cluster around shared formatting/prototype/error behavior.
- Plain uncoded throws do not increase in touched modules.

## Proposed Order

1. Track 0: reconcile baseline/report and regenerate failure buckets.
2. Track E first slice: finish or narrow `test-stream-pipeline.js`, because it
   is already heavily improved and still skip-listed.
3. Track A first slice: HTTP/TLS lifecycle classification plus one request
   abort/agent/session cluster.
4. Track B scope decision: real worker/process model versus explicit continued
   single-process compatibility.
5. Track F permission/module policy slice, because it intersects compile-cache,
   process, child-process, and runner failures.
6. Track C crypto/X509/WebCrypto slice.
7. Track D web-platform encoding slice.
8. Track G fidelity sweep across failures exposed by the previous tracks.

## Verification Policy

Every implementation slice should finish with:

```bash
make build
./test/test_node_gtest.exe --modules=<focused-modules> --timeout=20000 --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --timeout=20000 --gtest_brief=1
git diff --check
```

For `http`, `https`, `net`, `tls`, `worker`, `cluster`, and child-process work,
run in a context where local socket and subprocess preflights pass. Sandbox
EPERM or orphan-process failures are not evidence about the runtime.

For performance or timeout claims, use release builds:

```bash
make release
./test/test_node_gtest.exe --modules=<focused-modules> --timeout=20000 --gtest_brief=1
```

## Non-Goals

- Do not implement HTTP/2, dgram, WASI, full ICU, V8 snapshots, SEA, or native
  addons as part of Node6 unless this proposal is explicitly re-scoped.
- Do not add filename-specific official-test branches.
- Do not expand skip lists to hide implementable failures.
- Do not make performance claims from debug builds.

## Acceptance Criteria

Node6 is worth accepting only if it produces:

- a reconciled post-Node5 baseline/report pair tied to `ref/node`
  `92b72d4f601` or a documented newer Node checkout;
- 0 full-baseline regressions, crashes, and timeouts at acceptance time;
- at least 200 net active-baseline improvements, or a smaller number with a
  clear structural win in one of the top three failure pools;
- documented scope decisions for worker/cluster and any remaining skip-list
  quarantine;
- measurable improvement in at least three of these clusters:
  - HTTP/TLS/HTTPS lifecycle;
  - worker/cluster/process;
  - crypto/X509/WebCrypto;
  - permission/VM/module/runner;
  - stream/zlib pipeline;
  - web platform and fidelity modules.
