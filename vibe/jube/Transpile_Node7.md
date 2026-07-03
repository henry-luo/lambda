# Transpile Node Tune7 Net/TLS Ownership Redesign

Date: 2026-07-02
Status: completed in the post-Node6 net/TLS ownership session
Scope: LambdaJS Node.js compatibility pass for `net.Socket` to `tls.TLSSocket`
adoption, close ownership, and borrowed-socket lifecycle.

## Progress Update: 2026-07-02 Net/TLS Ownership

Node7 captures the larger net/TLS redesign that followed the Node6 TLS
transport and slow-test cleanup work. The immediate release crashers were:

- `test-tls-connect-given-socket.js`
- `test-tls-socket-close.js`

Both pointed at the same root cause: a single `uv_tcp_t` was effectively owned
by both `net.Socket` and `tls.TLSSocket`. TLS overwrote `tcp->data` with its
own native wrapper while the net layer could still read, write, close, or
interpret that handle as a `JsSocket`. The `tls.connect({ socket })` path also
created a placeholder `uv_tcp_t`, which introduced another close hazard when
the real existing socket was adopted.

Implemented design:

- added an explicit net-to-TLS adoption hook:
  `js_net_socket_adopt_for_tls(socket_obj, tls_obj)`;
- added a TLS-to-net finalization hook:
  `js_net_socket_tls_closed(socket_obj, had_error)`;
- made TLS the sole native owner of the shared `uv_tcp_t` after adoption;
- stopped net from issuing native reads, writes, shutdowns, or closes on an
  adopted socket;
- made adopted net `write()`, `end()`, and `destroy()` delegate to the
  owning `TLSSocket`;
- removed the placeholder `uv_tcp_t` allocation from
  `tls.connect({ socket })`;
- made TLS close finalization notify the borrowed net socket exactly once,
  emit the correct close state, detach JS/native references, and free native
  state in one path.

Touched files:

| File | Role |
| --- | --- |
| `lambda/js/js_net.cpp` | tracks adopted net sockets, delegates public net operations to TLS, and exposes the adoption/finalization hooks |
| `lambda/js/js_tls.cpp` | adopts existing net sockets, owns native close after adoption, and centralizes TLS close finalization |

## Ownership Invariants

After `tls.connect({ socket })` adopts an existing `net.Socket`:

- the `uv_tcp_t` belongs to TLS until native close is complete;
- `tcp->data` points at the TLS native wrapper, not the net wrapper;
- the net wrapper remains the JavaScript compatibility object, but not the
  native transport owner;
- net pause/resume/read paths must not restart raw TCP reads;
- net write/end/destroy paths must route through TLS so bytes pass through
  mbedTLS and `close_notify` ordering remains coherent;
- TLS close is responsible for notifying the borrowed net socket once, then
  clearing the borrowed-socket link.

This keeps the public Node shape where the original net socket still exists,
while avoiding two native owners for one libuv handle.

## Verification

Consolidated verification from the parent workspace:

```bash
./test/test_node_gtest.exe --gtest_filter='*test_tls_connect_given_socket:*test_tls_socket_close:*test_net_throttle:*test_repl_unexpected_token_recoverable:*test_stream2_readable_empty_buffer_no_eof' --include-slow --no-update-slow-list --timeout=60000
./test/test_node_gtest.exe --gtest_filter='*test_tls_connect_given_socket:*test_tls_socket_close:*test_tls_reuse_host_from_socket:*test_socket_writes_before_passed_to_tls_socket:*test_tls_wrap_econnreset_socket:*test_tls_socket_destroy:*test_tls_async_cb_after_socket_end:*test_tls_socket_allow_half_open_option' --include-slow --no-update-slow-list --timeout=60000
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --gtest_brief=1
./test/test_node_prelim_gtest.exe
make test262-baseline
```

Results:

| Gate | Result |
| --- | --- |
| Focused release gate | 5/5 pass |
| Borrowed-socket TLS group | 8/8 pass |
| Full Node official baseline | 3513/3513 pass, 0 regressions, 0 crashes, 0 timeouts |
| `test_node_prelim_gtest.exe` | 110/110 pass |
| `make test262-baseline` | 40261/40261 pass, 0 regressions, 0 retry |

## Follow-Up Notes

This redesign fixes the shared-handle ownership class rather than only the two
release crashers. The remaining TLS work should build on these handoff
boundaries instead of bypassing them with direct `uv_tcp_t` access from both
modules.

The TLSv1.0/TLSv1.1 min/max matrix remains intentionally skipped under the
current Lambda Node policy. There is no plan to support TLSv1.0/TLSv1.1 in
Lambda Node at the moment, and that decision is independent of the borrowed
socket ownership redesign.

## Progress Update: 2026-07-02 Slow 30s Drain Follow-Up

This follow-up session continued the slow-test cleanup after the borrowed
socket redesign. The key finding is that several tests now pass functionally
but still wait for the event-loop process-drain watchdog in release builds.
The common symptom is a still-refed `UV_PROCESS` handle at shutdown, not a
JavaScript assertion failure.

Confirmed release 30s drain cases:

- `test-tls-session-cache.js`
- `test-tls-server-verify.js`
- `test-tls-ecdh-auto.js`
- `test-https-foafssl.js`
- `test-tls-env-extra-ca.js`
- `test-tls-ecdh.js`
- `test-tls-env-extra-ca-with-options.js`
- `test-child-process-fork-getconnections.js`
- `test-child-process-fork-closed-channel-segfault.js`
- `test-child-process-advanced-serialization.js`
- `test-child-process-http-socket-leak.js`
- `test-cluster-worker-disconnect-on-error.js`

Focused individual probes showed that at least these are true standalone
30s drains, not only collateral from parallel execution:

- `test-tls-session-cache.js`
- `test-child-process-fork-getconnections.js`
- `test-cluster-worker-disconnect-on-error.js`

### TLS Session-Cache Compatibility

`test-tls-session-cache.js` had a correctness regression before this session:
the legacy session cache callbacks were emitted even when the client offered
TLS session tickets. Node's behavior is:

- no-ticket / session-id clients drive `newSession` and `resumeSession`;
- ticket-capable clients bypass the legacy session-id cache callbacks.

Implemented design in `lambda/js/js_tls.cpp`:

- Lambda's current mbedTLS-backed server path still does not provide full
  OpenSSL-style server session cache semantics, so the Node compatibility layer
  synthesizes stable session id/data for the legacy callback API;
- server-side TLS reads now probe the ClientHello before mbedTLS consumes it;
- extension `35` (`session_ticket`) marks a ticket-capable client;
- synthetic `newSession` / `resumeSession` events are emitted only when the
  ClientHello did not offer tickets;
- partial ClientHello probe buffers are cleared on TLS socket close/error.

This fixes the assertion behavior of `test-tls-session-cache.js`. In release,
the test still drains for about 30s because the OpenSSL child/process lifecycle
remains referenced until the watchdog.

### Safe IPC / Cluster Lifecycle Partials

The non-TLS worker made narrow lifecycle fixes that were safe to keep:

| File | Design Note |
| --- | --- |
| `lambda/js/js_globals.cpp` | `process.once()` wrapper now forwards both event arguments, so IPC message handlers receive `(message, handle)` instead of losing transferred handles. |
| `lambda/js/js_child_process.cpp` | `ChildProcess.send(message, handle, cb)` callbacks are tied to actual IPC write completion instead of queue time, preventing JS teardown from racing ahead of transferred native handles. |
| `lambda/js/js_net.cpp` | server-handle cleanup helpers cover failed listen paths, IPC server transfers, and worker-side disconnect server drain. |
| `lambda/js/js_runtime.cpp` | worker-side `cluster.worker.disconnect()` closes active worker servers before closing process IPC. |

These changes improve lifecycle ordering, but they do not remove all release
30s process-drain waits.

### Design Boundary: Broader Process / IPC Ownership

The remaining slow cases should not be fixed with test-specific timeout
shortening. They need a broader ownership model across:

- child process lifetime versus JS `ChildProcess` object lifetime;
- IPC pipe ref/unref and close ordering;
- transferred socket/server ownership when `keepOpen` is false;
- primary-side cluster worker registry and `cluster.disconnect()` fan-out;
- worker-side server shutdown before IPC disconnect;
- release-mode process-drain rules for children that already emitted `exit`
  and `close` at the JavaScript level.

An attempted minimal primary-side cluster registry was intentionally not kept:
it was too broad for the session and showed release timing risk. The next
session should make this a first-class design rather than accreting another
small patch around the watchdog.

### Verification From This Session

Commands run after the safe partials:

```bash
make release
make -C build/premake config=release_native test_node_gtest test_node_prelim_gtest -j8
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_tls_session_cache*' --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_child_process_fork_getconnections*' --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_worker_disconnect_on_error*' --gtest_brief=1
./test/test_node_prelim_gtest.exe
make test262-baseline
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_brief=1
```

Results:

| Gate | Result |
| --- | --- |
| `test-tls-session-cache.js` focused | pass, still about 30.1s in release |
| `test-child-process-fork-getconnections.js` focused | pass, still about 30.1s in release |
| `test-cluster-worker-disconnect-on-error.js` focused | pass, still about 30.1s in release |
| `test_node_prelim_gtest.exe` | 110/110 pass, same existing 128-byte memtrack warning |
| `make test262-baseline` | 40261/40261 pass, 0 regressions, 0 retry |
| Full Node official baseline-only | 3537/3537 pass, 0 regressions, 0 crashes, 0 timeouts; 11 slow-list tests excluded |

## Progress Update: 2026-07-02 Cluster / IPC Ownership Redesign

This session implemented the first concrete pass of the broader process / IPC
ownership model called out above. The root problem was that `uv_write2`
completion was being treated as the end of transferred socket ownership. That
is too early for Node-style IPC: the sender can finish writing the message
before the receiver has accepted the passed descriptor, and server connection
accounting must remain valid until the receiver-side socket actually closes.

Implemented design:

- handle-bearing IPC messages are wrapped with an internal envelope so ordinary
  user/control messages cannot accidentally consume a delayed pending fd;
- `ChildProcess.send(..., handle)` with `keepOpen: false` now has two native
  transfer phases:
  - receiver `uv_accept()` sends an internal `handle_accepted` frame, allowing
    the sender to close its endpoint safely;
  - receiver socket close sends an internal `socket_closed` frame, allowing the
    sender-side server connection count to decrement;
- accepted sockets sent across IPC keep sender-side server accounting pending
  while the duplicate sender endpoint is closed;
- `server.getConnections(callback)` runs its callback on next tick, matching
  Node's asynchronous shape and avoiding reentrant teardown inside IPC message
  handlers;
- the event-loop drain watchdog now re-checks for live refed process handles at
  each 5s interval instead of choosing the 30s process grace once at drain
  start.

Touched files:

| File | Role |
| --- | --- |
| `lambda/js/js_child_process.cpp` | queues transferred connection ownership, consumes internal accepted/closed control frames, and drains transfer accounting on process release |
| `lambda/js/js_globals.cpp` | unwraps handle-bearing IPC envelopes and sends internal receiver-to-sender transfer-control frames |
| `lambda/js/js_net.cpp` | tracks received IPC sockets, defers sender server accounting, and makes `getConnections()` async |
| `lambda/js/js_event_loop.cpp` | keeps the longer process-drain grace only while refed process handles still exist |

### IPC Ownership Invariants

For `keepOpen: false` TCP socket transfers:

- `uv_write2` completion means the message was queued to libuv, not that the
  receiver owns a usable socket yet;
- sender endpoint close is safe only after the receiver accepts the handle;
- sender server connection accounting completes only when the receiver-side
  socket closes;
- internal transfer-control frames must not surface as userland `message`
  events;
- no-handle IPC messages must not consume a pending descriptor.

These rules preserve Node's visible `ChildProcess.send()` / `server.getConnections()`
behavior while making native handle ownership explicit.

### Verification From This Session

Commands run after consolidation:

```bash
make -C build/premake config=debug_native lambda -j8
make release
make -C build/premake config=release_native test_node_gtest test_node_prelim_gtest -j8
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_child_process_fork_getconnections*:*test_child_process_fork_closed_channel_segfault*:*test_child_process_http_socket_leak*:*test_cluster_worker_disconnect_on_error*:*test_tls_session_cache*' --gtest_brief=1
./test/test_node_prelim_gtest.exe --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_brief=1
make test262-baseline
```

Results:

| Gate | Result |
| --- | --- |
| Focused child-process / cluster / TLS slice | 5/5 pass, 0 regressions |
| `test_node_prelim_gtest.exe` | 110/110 pass, same existing 128-byte memtrack warning |
| Full Node official baseline-only | 3537/3537 pass, 0 regressions, 0 crashes, 0 timeouts; 11 slow-list tests excluded |
| `make test262-baseline` | 40261/40261 pass, 0 regressions, 0 retry |

Remaining timing note: the full baseline still shows `test-child-process-http-socket-leak.js`
and `test-cluster-worker-disconnect-on-error.js` around the 10s boundary, and
two unrelated child/TLS cases still hit the 30s process-drain grace. The IPC
ownership change fixes the transferred-socket correctness/lifecycle root cause
without pretending all process-drain waits are solved.

## Progress Update: 2026-07-02 Listen Callback Ordering Follow-Up

This follow-up fixed one of the remaining 10s-ish cluster cases:
`test-cluster-worker-disconnect-on-error.js`.

Root cause: Lambda scheduled `server.listen()` callbacks with the internal
next-tick queue, which can be flushed before the surrounding JS statement
sequence has finished unwinding from the native `listen()` call. The Node test
does:

```js
server.listen(0, () => {
  assert(worker);
  worker.send({ port: server.address().port });
});

worker = cluster.fork();
```

When Lambda ran the listen callback too early, `worker` was not assigned yet.
The primary never sent the port to the worker, and the test then waited for the
harness drain/timer boundary despite having no intentional long timer.

Implemented design:

- HTTP server `listen()` callbacks now run through a zero-delay timer so they
  execute after the current JS stack;
- net server `listen()` uses the same after-stack scheduling;
- this keeps callbacks asynchronous relative to the native `listen()` call
  without changing error scheduling or server state transitions.

Touched files:

| File | Role |
| --- | --- |
| `lambda/js/js_http.cpp` | schedules HTTP `server.listen()` callbacks after the current JS stack |
| `lambda/js/js_net.cpp` | schedules net `server.listen()` callbacks after the current JS stack |

Verification:

```bash
make -C build/premake config=debug_native lambda -j8
make release
make -C build/premake config=release_native test_node_gtest test_node_prelim_gtest -j8
./test/test_node_gtest.exe --baseline-only --include-slow --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_worker_disconnect_on_error*:*test_child_process_http_socket_leak*:*test_cluster_process_disconnect*:*test_cluster_disconnect*:*test_cluster_concurrent_disconnect*:*test_cluster_eaccess*:*test_listen_fd_detached_inherit*:*test_child_process_fork_net_server*:*test_child_process_fork_closed_channel_segfault*:*test_child_process_fork_getconnections*' --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_worker_disconnect_on_error*' --gtest_brief=1
./test/test_node_prelim_gtest.exe --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_brief=1
make test262-baseline
```

Results:

| Gate | Result |
| --- | --- |
| Focused cluster/child/net slice | 18/18 pass, 0 regressions |
| `test-cluster-worker-disconnect-on-error.js` focused | pass in about 0.17s, down from about 10s |
| `test_node_prelim_gtest.exe` | 110/110 pass, same existing 128-byte memtrack warning |
| Full Node official baseline-only | second full run 3537/3537 pass, 0 regressions, 0 crashes, 0 timeouts; 11 slow-list tests excluded |
| `make test262-baseline` | 40261/40261 pass, 0 regressions, 0 retry |

Rejected during consolidation: a proposed `spawnSync` shortcut for
compile-cache and snapshot slow-list fixtures was removed because it encoded
test-specific synthetic results instead of fixing the underlying runtime
behavior.

Remaining timing note: `test-child-process-http-socket-leak.js` remains around
10s, and several cluster/socket-transfer cases remain around the 5s drain
boundary. Those are separate lifecycle/handle-retention issues from the
listen-callback ordering bug fixed here.

## Progress Update: 2026-07-02 Cluster 5s Drain Cleanup

This follow-up reduced most of the remaining cluster tests that were passing
functionally but waiting for the 5s drain boundary.

Implemented design:

- `process.prependListener()` and `process.prependOnceListener()` now preserve
  true prepend ordering for the process event map. `internalMessage` ordering is
  observable in cluster tests because user listeners can wrap a transferred
  handle's `close()` before the runtime's SCHED_RR compatibility path consumes
  the accepted socket.
- `process.channel` is now exposed for IPC children and supports `ref()` /
  `unref()`. `process.channel.unref()` explicitly overrides hidden cluster IPC
  listeners so RR workers with no other refed handles can exit without waiting
  for the drain watchdog.
- object-form pipe `listen({ path, readableAll, writableAll })` now applies the
  expected socket-file mode after bind. Without the chmod, the official test
  threw before it could close the server and disconnect the worker.
- HTTP `server.listen()` treats numeric strings as TCP ports, matching
  `net.Server.listen()`. `process.env.PORT` from cluster workers was previously
  interpreted as a pipe path, so the worker missed the expected `EADDRINUSE`
  path and waited for the watchdog.

Touched files:

| File | Role |
| --- | --- |
| `lambda/js/js_globals.cpp` | process prepend ordering and `process.channel.ref/unref` IPC liveness |
| `lambda/js/js_net.cpp` | pipe listen mode bits for object-form pipe listens |
| `lambda/js/js_http.cpp` | numeric-string HTTP listen ports |

Verification from the parent workspace:

```bash
make build
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_worker_handle_close*:*test_cluster_rr_handle_close*:*test_cluster_rr_handle_ref_unref*:*test_cluster_rr_handle_keep_loop_alive*' --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_listen_pipe_readable_writable*:*test_cluster_ipc_throw*:*test_cluster_net_server_drop_connection*' --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster*' --gtest_brief=1
```

Results:

| Gate | Result |
| --- | --- |
| Focused RR/channel group | 4/4 pass, about 3.3s total; `test-cluster-rr-handle-keep-loop-alive.js` intentionally waits about 3s |
| Focused residual group | 3/3 pass; `test-cluster-listen-pipe-readable-writable.js` and `test-cluster-ipc-throw.js` about 0.47s |
| Full cluster slice | 76/76 pass, 0 regressions, about 9.6s |

Remaining design boundary: `test-cluster-net-server-drop-connection.js` still
passes but remains around 5.9s. The root cause is not another missing unref or
callback-ordering edge; Lambda's cluster path still lets workers bind a pipe
server directly, while Node's SCHED_RR model keeps the listener in the primary
and sends accepted sockets to workers as `newconn` handles. Fixing this requires
a broader primary-owned SCHED_RR pipe-server distribution design.

## Progress Update: 2026-07-03 Cluster Pipe Timing Follow-up

This session validated the post-redesign cluster pipe behavior and removed two
more harness-visible 5s waits from the focused cluster set.

Implemented / retained design:

- Unix pipe paths are normalized by resolving the existing parent directory
  before `uv_pipe_bind()` / `uv_pipe_connect()`. Node official tests can build
  `common.PIPE` through symlinked or `..`-heavy harness paths; handing that raw
  spelling to libuv can fail before the deferred `listen` callback gets a turn.
- `test-cluster-listen-pipe-readable-writable.js` remains on the local
  object-form pipe-listen path so `readableAll` / `writableAll` chmod applies to
  the socket file being asserted.
- Hidden cluster `process.disconnect` routing remains non-liveness-counted, so
  idle workers do not wait for the drain watchdog only because the runtime
  installed internal listeners.

Rejected during consolidation:

- A `test_node_gtest.cpp` no-`cd` / file-capture / serial-list harness path for
  cluster tests was tried and removed. It improved neither the remaining
  `test-cluster-net-server-drop-connection.js` wait nor the semantic
  `common.mustCall` warning seen in manual runs, and it was too broad for a
  runtime compatibility fix.

Verification:

```bash
make build-test
make -C build/premake config=debug_native test_node_gtest -j10 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_listen_pipe_readable_writable*:*test_cluster_eaccess*' --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_net_server_drop_connection*' --gtest_brief=1
```

Results:

| Gate | Result |
| --- | --- |
| `test-cluster-listen-pipe-readable-writable.js` + `test-cluster-eaccess.js` | 2/2 pass, about 0.9s total; individual timings about 0.55s and 0.86s |
| `test-cluster-net-server-drop-connection.js` before RR cleanup | pass, still about 5.28s |

Remaining boundary: `test-cluster-net-server-drop-connection.js` still needs a
runtime-side ownership/lifecycle fix. Direct shell runs of the official command
exit quickly, but the gtest runner observes a 5s drain wait. Harness-only
changes were not sufficient, so the next pass should inspect which cluster
worker/server/socket handle remains refed until `EVENT_LOOP_DRAIN_TIMEOUT_MS`
after the 10 piped client connections are distributed and the workers receive
their disconnect messages.

## Progress Update: 2026-07-03 RR Pipe Worker Cleanup

The remaining `test-cluster-net-server-drop-connection.js` wait was fixed by
cleaning up primary-owned SCHED_RR pipe dispatch state when workers disconnect
or exit.

Implemented design:

- primary-side RR pipe servers now remove disconnected worker slots and clear
  pending dispatch handles tied to that worker;
- if all workers for a primary-owned pipe server are gone, the shared accept
  handle is closed instead of staying refed until the event-loop drain watchdog;
- worker `disconnect` and `exit` forwarding in the cluster runtime now both
  notify the net layer, covering explicit `Worker.disconnect()` and
  worker-initiated `process.disconnect()` paths.

Touched files:

| File | Role |
| --- | --- |
| `lambda/js/js_net.cpp` | releases primary RR pipe server worker slots, pending handles, and accept handles |
| `lambda/js/js_runtime.cpp` | wires worker disconnect/exit events into RR pipe cleanup |

Verification:

```bash
make -C build/premake config=debug_native lambda test_node_gtest -j10 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_net_server_drop_connection*' --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_listen_pipe_readable_writable*:*test_cluster_eaccess*' --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster*' --gtest_brief=1
```

Results:

| Gate | Result |
| --- | --- |
| `test-cluster-net-server-drop-connection.js` | pass, about 0.63s |
| readable/writable + eaccess neighbors | 2/2 pass, about 0.9s total |
| full cluster slice | 76/76 pass, 0 regressions, about 10.0s |

Follow-up disconnect cleanup:

- the official-test runner now closes inherited fd >= 3 immediately before
  execing each test command. Parallel `popen()` runs can otherwise leak sibling
  capture-pipe writers into spawned Lambda/cluster children, delaying EOF until
  unrelated worker drain watchdogs fire;
- four cluster disconnect fixtures are serial because their worker/IPC teardown
  is process-pipe sensitive and they do not benefit from parallel execution.

Additional verification:

```bash
make -C build/premake config=debug_native test_node_gtest -j10 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_disconnect_unshared_udp*:*test_cluster_disconnect_race*:*test_cluster_disconnect_unshared_tcp*:*test_cluster_disconnect_with_no_workers*' --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster_disconnect_unshared_tcp*' --gtest_brief=1
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_cluster*' --gtest_brief=1
```

Results:

| Gate | Result |
| --- | --- |
| four disconnect fixtures, focused | 4/4 pass; transient filtered run had one 5.55s TCP outlier |
| `test-cluster-disconnect-unshared-tcp.js`, standalone | pass, about 0.56s |
| full cluster slice after FD isolation | 76/76 pass, 0 regressions, about 11.7s; four disconnect fixtures all below 0.8s in `temp/node_official_times.tsv` |

Former remaining timing boundary, now addressed:

- `test-cluster-disconnect-unshared-udp.js`
- `test-cluster-disconnect-race.js`
- `test-cluster-disconnect-unshared-tcp.js`
- `test-cluster-disconnect-with-no-workers.js`

## Progress Update: 2026-07-03 Slow-List Consolidation

This pass continued the 10s-ish official-test cleanup after the cluster
ownership fixes. The main rule was to remove waits by fixing runtime ownership
or algorithmic root causes, not by broadening skips.

Implemented design:

- HTTP server backpressure now stops parsing already-buffered pipelined request
  bytes after response backpressure pauses reads. This keeps
  `test-http-pipeline-flood.js` from overprocessing flood traffic.
- HTTP response lifecycle now settles aborted responses, emits response `drain`,
  handles close-delimited zero-byte final writes, dispatches upgrade requests to
  upgrade listeners, and supports the response/socket surfaces needed by
  keep-alive reuse and `IncomingMessage.destroy()`.
- Advanced IPC serialization now encodes Buffers and typed arrays as compact
  byte envelopes instead of enumerating every numeric property. This removes
  the orphaned child process wait in
  `test-child-process-advanced-serialization-splitted-length-field.js`.
- Process lifecycle and async error handling now preserve uncaught errors
  through process hooks, keep timer/immediate resources active during uncaught
  dispatch, provide queueMicrotask async-hook resources, validate
  `process.exitCode`, route console writes through process streams, and make
  warning delivery match Node timing closely enough for the official common and
  process fixtures.
- `assert.partialDeepStrictEqual()` now avoids scanning sparse array holes,
  handles partial Map/Set/TypedArray subsets, tracks active comparison pairs for
  circular structures, compares URL values by `href`, and recognizes typed-array
  numeric SameValue details.
- Real `Float16Array` support was added with binary16 storage, conversion,
  global constructor registration, MIR/runtime paths, and
  `util.types.isFloat16Array`.
- REPL `.load` now evaluates loaded source and propagates errors through the
  REPL stack formatter, including `Error.prepareStackTrace` and virtual REPL
  filenames.
- Wrapped JS stream sockets now inherit the socket prototype bridge and preserve
  synchronous wrapped-stream exceptions while still emitting the socket error
  expected by the official net wrapper tests.

Slow-list update:

- removed stale entries that now run under 10s:
  `test-assert-deep.js`, `test-fs-readdir-stack-overflow.js`,
  `test-http-byteswritten.js`, `test-http-pipeline-flood.js`, and
  `test-http-pipeline-requests-connection-leak.js`;
- retained only real heavy or unsupported cases:
  `test-compile-cache-success.js`, `test-snapshot-typescript.js`,
  `test-stream2-read-sync-stack.js`, and the three documented TLS cases.

Verification:

```bash
make -C build/premake config=debug_native lambda test_node_gtest -j10 CC="gcc" CXX="g++" AR="ar" RANLIB="ranlib"
./test/test_node_gtest.exe --baseline-only --no-update-slow-list --timeout=70000 --gtest_filter='*test_http_client_incomingmessage_destroy*:*test_http_outgoing_end_cork*:*test_http_upgrade_server2*:*test_http_byteswritten*:*test_http_pipeline_flood*:*test_child_process_advanced_serialization_splitted_length_field*:*test_assert_partial_deep_equal*:*test_assert_typedarray_deepequal*:*test_common*:*test_async_wrap_uncaughtexception*:*test_process_uncaught_exception_monitor*:*test_promises_unhandled_rejections*:*test_queue_microtask_uncaught_asynchooks*:*test_repl_pretty_custom_stack*:*test_wrap_js_stream_exceptions*' --gtest_brief=1
```

Results:

| Gate | Result |
| --- | --- |
| repaired focused group | 19/19 pass, 0 regressions, about 7.0s |
| former `test-assert-partial-deep-equal.js` timeout | pass, about 1.4s |
| former child-process advanced serialization orphan wait | pass, about 1.3s |
| former HTTP flood wait | pass, about 0.8s |

Remaining slow-list rationale:

- `test-stream2-read-sync-stack.js` is CPU-bound in the current interpreter:
  the fixture performs 256K synchronous `_read()` / `push(Buffer.allocUnsafe(1))`
  turns rather than waiting on a timeout.
- `test-compile-cache-success.js` and `test-snapshot-typescript.js` execute the
  10.5MB TypeScript snapshot fixture and still need real compile-cache/snapshot
  artifact semantics before joining the normal baseline set.

## Progress Update: 2026-07-03 Test262 Baseline Regression Fix

The slow-list work temporarily exposed nine `make test262-baseline`
regressions: seven in `Float16Array` conversion tests and two in lexical
closure scope tests.

Implemented design:

- `Float16Array` conversion now rounds directly from JS `Number` (`double`) to
  IEEE-754 binary16. The previous implementation narrowed through `float32`
  first, which double-rounded subnormal tie cases such as `2^-25` plus one
  double ulp to zero instead of the minimum half subnormal (`2^-24`).
- Closures created in `for (let ...; ...)` initializers now treat the synthetic
  loop lexical scope as current even before `iteration_depth` increments. This
  prevents later loop-body/test/update writes from mutating the initializer
  closure's captured cell, while still allowing closures created after the
  per-iteration boundary to observe the current iteration binding.
- Stale fallback-call diagnostic logging was removed from
  `js_mir_expression_lowering.cpp` after the release build surfaced unused
  debug-only locals.

Verification:

```bash
./test/test_js_test262_gtest.exe --batch-only --run-async --batch-file=temp/test262_regressions_current.txt
make test262-baseline
```

Results:

| Gate | Result |
| --- | --- |
| focused 9-regression Test262 batch | 9/9 pass |
| `make test262-baseline` | 40261/40261 fully passed, 0 regressions, 0 retry |
