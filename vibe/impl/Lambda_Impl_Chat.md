# Lambda Chat — implementation plan

**Date:** 2026-11-02

**Status:** local chat and core browser WebSocket API implemented; qualification below

**Scope:** desktop one-to-one text messaging, `lambda demo chat`, RFC 6455
interoperability and the browser document WebSocket API.

## 1. Contracts and boundaries

Use the existing procedural IO module contract **D7.2.4v4** for
`import ws: lambda.io.websocket`. Every operation is a `pn`
(**S12.1.1v3**); socket waits park a task (**S13.1.2v2**, **D6.3.1**).
Document-owned tasks drive the UI (**S13.3.1v2**). Native records retain
values through precise roots (**D5.3.3**); native worker jobs hold no Items.
This extends the IO catalogue within existing language semantics and keeps
`io.fetch` as request/response IO. No formal ruling changes are required.

The resource design **S12.4.1v3–S12.4.3** is only partially implemented.
Follow `lambda.io.mail`'s present task-owned lifetime: explicit close is
available; owner completion closes remaining sockets and wakes waiters.
Do not claim block-scoped resource ownership is implemented here.

Keep three layers separate:

1. Native WebSocket transport: handshake, framing, TLS, bounded reads,
   cancellation, duplex socket readiness and connection lifetime.
2. Lambda chat service: message validation, IDs, persistence, receipts,
   replay and duplicate suppression.
3. Demo: contact list, conversation, composer, connection state and receipts.

Reference protocols: [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455.html),
[libcurl connect-only](https://curl.se/libcurl/c/CURLOPT_CONNECT_ONLY.html),
[libcurl raw IO](https://curl.se/libcurl/c/curl_easy_send.html).
Reuse curl's verified TLS and host trust store for WSS. Do not add a TLS
implementation or edit vendor sources.

## 2. Finite first milestone

### M0 — native transport

Provide `open(url, options?)`, `send(connection, text_or_binary)`,
`receive(connection, timeout_ms?)`, `close(handle)`, `listen(port)` and
`accept(listener, timeout_ms?)`. `listen` is a local-development listener
bound to IPv4 loopback, including port zero. Production TLS termination and
the general `serve` integration are separate milestones.

`receive` returns `{type, data, code}`: text/binary messages or a peer close.
Timeout is an error, distinct from close and empty data. Connections permit
one reader and one writer concurrently; another operation in the same
direction fails explicitly. Message bytes and HTTP upgrade headers are
bounded. Validate masking, frame lengths, fragmentation, control frames,
UTF-8 and the handshake accept value. Handle ping/pong and closing.

Acceptance: native client/server round-trip, independent Python peer,
fragmented text with interleaved ping, binary and empty payload, rejection
of invalid frames/handshakes, receive timeout, cancellation/owner teardown,
and verified WSS against an isolated local TLS fixture.

### M1 — reusable chat service

Put the application protocol and pure transitions in `lambda.chat`.
Use JSON envelopes, stable sender-generated IDs, accepted/delivered/read
receipts, deduplication, and history replay. Keep a single service task as
the persistence owner, with an atomic snapshot written before acceptance.
Failures must be visible; do not show acceptance for an unsaved message.
Use `output(..., {atomic: true})` for this small demonstration store; a
transactional database API is future work, not a hidden dependency.

The local demo identities are explicitly demonstration profiles, without
production authentication or end-to-end encryption. A remote deployment
must authenticate and authorize both devices and conversation membership.
Keep transport security and end-to-end encryption separate.

Acceptance: two real local clients exchange text, sender ID cannot be
supplied in an ordinary send, repeated IDs do not duplicate messages,
offline messages replay, read receipts affect the correct conversation,
and restarting the service preserves accepted history.

### M2 — `lambda demo chat`

Launch without configuration. Show a WhatsApp-inspired green/cream desktop
interface with contacts, search, message bubbles, composer, timestamps,
receipts and connection state. Offer two local participant views so users
can demonstrate both sides without another installation. Both views use
real WebSocket connections to the local service. No scripted auto-replies.
Support disconnect/reconnect to demonstrate pending messages and replay.

Register in the CLI catalogue and demo launcher. Keep the profile under
`temp/chat` by default, overridable for isolated runs. Document execution,
storage, transport and limitations in the demo README.

Acceptance: native event replay opens the demo, sends Unicode text, switches
participants, observes delivery/read, disconnects/reconnects a participant,
and exercises search and empty-message validation. Capture and inspect a
nonempty desktop image and a narrow viewport.

## 3. Verification budget

Use focused protocol, package and UI checks during implementation. Every
new golden-driven `.ls` test has a sibling `.txt`. At the milestone boundary,
run `make test-lambda-baseline` after engine changes; run the Radiant
baseline if Radiant engine code changes. Demo-only CSS/Lambda changes use
native UI replays. Run `git diff --check` for this change's files.
Record environmental and pre-existing failures without modifying unrelated
code, goldens or the in-progress merge. No performance claims from debug.

## 4. Follow-up milestones

1. General HTTP-server upgrade routes, origin/subprotocol policies,
   authenticated remote service and deployment configuration.
2. Transactional storage, durable client outbox, pagination, retry/backoff,
   reconnect cursors and explicit retention/storage quotas.
3. Audited end-to-end encryption binding, secure device key storage,
   verification, revocation and multi-device fanout.
4. Production qualification: malformed-peer fuzzing, slow consumers,
   connection/load limits, observability, restart/failure injection and
   cross-platform builds.
5. iOS/Android host lifecycle and push integration; attachments, groups,
   voice notes and WebRTC calling are separately scoped features.

## 5. Completion record

Implemented M0–M2. Focused qualification passes; the full baseline has the
scalar-rooting residue recorded below.

- Native `lambda.io.websocket` and its shared IO carrier/socket helpers are
  implemented. Duplex readiness uses one libuv poll handle per socket.
  Client TLS uses curl and the host trust store; local listeners reject
  browser Origin requests. Explicit close releases immediately after a
  best-effort normal close frame; it does not wait for peer closure.
- `lambda.chat` owns the JSON protocol, demo profiles, atomic snapshots,
  duplicate suppression, accepted/delivered/read states and replay. The
  worker dispatcher selects idle workers, so repeated reconnects cannot
  queue behind an unrelated active participant.
- `lambda demo chat` is registered in the CLI, launcher and icon/art
  catalogue. Four participants have independent connections and inboxes.
  Desktop and 390 px UI replays pass; native captures were inspected.
- The first service test exposed a pre-existing equality defect: virtual-map
  comparison rejected even the same task handle. The evaluator now compares
  task capability identity under **S13.1.3v2**. A dedicated golden covers
  direct equality, mailbox copies, nested maps and distinct handles.
- Focused transport, identity, cancellation and service goldens pass on
  interpreter/AUTO/JIT and every-allocation GC with poisoned frees.
- Eight independent Python test groups pass: duplex backpressure with a
  simultaneous ping, fragmented UTF-8/binary/close on all execution tiers,
  invalid accept, malformed frames, timeout with partial input, unsupported
  negotiation, untrusted WSS refusal and trusted WSS exchange. The gate is
  `make test-websocket` and also runs in `make test-lambda-full`.
- `make test-lambda-baseline` was run. Its first aggregate was invalidated
  by the shared `lambda.exe` disappearing during batch startup (spawn errors
  and absent batch results). The already-built baseline was rerun with the
  same selection, without rebuilding, and separate results under
  `temp/chat-baseline-retry`. The executable hash remained unchanged.
- That retry passed **8,514/8,517** combined cases: input **2,112/2,112**,
  Lambda **6,402/6,405**. Every new chat/transport/identity case passed. The
  Procart field-output failure passed its isolated tier-parity rerun, and
  the document conversion timeout passed in isolation (32 s).
- **Remaining baseline failure:** `test/lambda/proc/mir_mvp_roots.ls` prints
  `false` for the retained boxed subnormal value after overwriting its source
  variable under interpreter/AUTO; JIT prints the expected `true`. This is
  a scalar ownership case (**D5.2.1v3**, **D5.3.4**) outside the chat path.
  No scalar code or golden was changed to conceal it. The full baseline is
  therefore **not green**; this change does not claim otherwise.
- Evidence: `temp/chat-tiers.log`, `temp/chat-protocol-tests.log`, native UI
  event-result JSON files, `temp/chat-baseline-retry.log`,
  `temp/chat-baseline-residue.log`, and the per-tier
  `temp/chat-mvp-roots-*.log`.

The milestone is a local desktop demonstration. Production authentication,
end-to-end encryption, remote/TLS listeners, durable client outboxes,
transactional storage, history paging/quotas and mobile hosts remain the
follow-up work in §4. No performance claim or cross-platform qualification
is made.

## 6. WebSocket web interoperability requirement (2026-11-02)

The user's compatibility requirement makes browser interoperability an
acceptance gate, rather than an inference from the demo's native round-trip.
The native API remains procedural under **D7.2.4v4**, with socket suspension
under **S13.1.2v2** and precise ownership under **D5.3.3**.

This milestone follows [RFC 6455](https://www.rfc-editor.org/rfc/rfc6455.html)
and the connection/closing requirements of the
[WHATWG WebSockets Standard](https://websockets.spec.whatwg.org/):

- Accept configured browser Origins; retain an explicit origin policy for
  local services. Reject invalid upgrades with an HTTP response.
- Validate and negotiate subprotocol tokens, including the response matching
  a client offer. Compression is optional and is not advertised.
- Validate URLs and upgrade headers, including fragments, duplicate singleton
  headers, canonical keys, and repeated list-valued headers.
- Complete the closing handshake, retain queued writes, validate close
  codes/reason bytes, and send appropriate protocol-error close statuses.
- Verify real Chromium clients against Lambda's server, Lambda clients
  against the standard Node `ws` server, and malformed/fragmented peer cases.

The WHATWG JavaScript interface (`WebSocket`, events, `readyState`,
`bufferedAmount`, `binaryType`) is explicitly included by the user, alongside
wire compatibility. It is installed in browser document realms; the inert
Radiant preamble stub is removed. Core JS globals remain subject to
**D7.3.1**; native host calls obey **D7.4.2v2**.

Implementation: the native task transport handles both Lambda procedures
and the browser API. A browser constructor returns in CONNECTING; native
reader/writer tasks perform connection setup, mailbox-ordered sends and
closing. Networking task envelopes dispatch open/message/error/close using
the existing EventTarget implementation. Failed connections enter CLOSED
before error and close fire in the same task. Binary conversion uses the
current binaryType at message dispatch; outgoing views snapshot their exact
byte range. CloseEvent stores readonly payload in the existing event carrier.
The wrapper traces native state and listeners through a private record;
document retirement cancels tasks. No conservative-stack roots are added
(**D5.3.3**). Cookie selection and upgrade Set-Cookie storage use the browser
profile jar, including HttpOnly cookies on requests.

Qualification gates:

- `python3 test/websocket/test_websocket.py`: independent RFC peers,
  fragmentation, masking, UTF-8, control frames, timeout recovery and WSS.
- `node test/websocket/test_browser_websocket.cjs`: LambdaJS against the
  independent Node ws implementation and Chromium against Lambda's listener;
  normal/failure events, subprotocols, Blob/ArrayBuffer/view messages,
  buffered bytes, close-during-connect and default/custom close bodies.
- Repeat the browser gate with `LAMBDA_GC_FORCE_EVERY=1` and
  `LAMBDA_GC_POISON_FREED=1`; run existing EventTarget/BroadcastChannel tests
  because native listener ownership now shares a small notification interface.
- Required Lambda and Radiant baselines at the milestone boundary; keep
  unrelated existing failures visible.

Remaining standards coverage: the focused cases are not the complete WPT
WebSocket suite. HTTP/2 extended CONNECT, workers, compression, and general
server Upgrade routes are not implemented. The browser host's complete Fetch
policy integration (CSP connect-src, HSTS/mixed-content, forbidden ports and
cookie SameSite/partition policies) remains a separate qualification item;
do not claim complete browser security-policy or full WPT conformance.

### Web compatibility qualification record

- Ten independent Python protocol test groups pass, including explicit
  1002/1007/1009 error close frames, rejected Origin/upgrade cases, repeated
  list-valued headers, subprotocol selection, peer close during a blocked
  writer, fragmented UTF-8, ping/pong, partial-read recovery and verified WSS.
  The complete gate also passes under forced GC with freed-memory poisoning.
- Browser API cases pass against the root `ws` development dependency;
  real Chromium exchanges text/binary messages with Lambda's listener and
  observes a clean custom-code close. The browser gate passes under forced
  GC, including failed negotiation, close-during-connect, USVString conversion,
  binary dispatch, queued-send flushing and finite-drain resource retirement.
- The focused cookie request-scope unit passes, including HttpOnly exclusion
  from document.cookie, secure transport, domain and path restrictions.
  Existing BroadcastChannel GC ownership checks pass after the shared listener
  notification change (**D5.3.3**).
- Desktop demo replay: 29 events, 15 assertions passed; mobile replay:
  11 events, 7 assertions passed.
- `make test-lambda-baseline`: **8,516/8,517** pass (2,112 input and
  6,404 runtime). The sole failure is the pre-existing
  `proc_mir_mvp_roots` tiny-float copy/root mismatch recorded in §5.
  Every chat/WebSocket regression passes; no golden or harness was weakened.
- `make test-radiant-baseline`: **PASS**, with 8,723 passing, 303 recorded
  partials and two skips out of 9,028; no unexpected failures. All 746 UI
  automation checks pass. Logs retain the baseline's expected render failures
  rather than treating them as newly supported behavior.

Evidence: `temp/websocket-protocol-final.log`,
`temp/websocket-protocol-gc-final.log`, `temp/websocket-browser-qualified.log`,
`temp/websocket-browser-gc-final.log`, `temp/websocket-cookie-unit.log`,
`temp/websocket-broadcast-gc.log`, `temp/chat-websocket-{ui,mobile}-final-result.json`,
`temp/websocket-lambda-baseline.log`, and
`temp/websocket-radiant-baseline.log`. The final rebuilt root executable also
passes both interoperability gates (`temp/websocket-root-protocol.log` and
`temp/websocket-root-browser.log`).
