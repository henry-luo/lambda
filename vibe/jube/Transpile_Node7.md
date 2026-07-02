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
