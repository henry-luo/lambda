# BroadcastChannel core milestone

2026-10-22. Three remaining online failures (OpenAI 445, 621 and 622) stop at
an absent `BroadcastChannel` constructor. Implement the browser messaging path,
following [HTML broadcasting](https://html.spec.whatwg.org/multipage/web-messaging.html#broadcasting-to-other-browsing-contexts).

- **Ownership:** D7.4.4 declared Jube host records; D5.3.3 precise callback edges;
  D5.4.2–D5.4.3 evaluator-owned registry and teardown. Channels with message
  listeners stay alive while open; closing removes that root without discarding
  listeners. Closed callback cycles can be collected.
- **Core scope:** document channels in one evaluator, same name and origin,
  asynchronous delivery in creation order, no sender echo, one send-time clone
  and an independent receive-time clone per destination, MessageEvent metadata,
  EventTarget listeners/IDL handlers, close and serialization errors.
- **Acceptance:** constructor coercion/branding, listener removal and once,
  mutation isolation, close before delivery, closed send, uncloneable data,
  forced-GC delivery/teardown; strict replay of the three original URLs.
- **Deferred:** worker and cross-evaluator/process routing and a browser-wide
  storage-partition broker. These need the scheduling work recorded in DO21;
  this milestone does not claim those contexts.
- **Verification budget:** focused AST/MIR document fixtures with forced GC;
  neighboring messaging/EventTarget fixtures; runtime and Radiant milestone
  gates after stabilization. Preserve online URL/timeout/error checks.

Channel behavior passes focused AST/MIR forced-GC tests and 20 repeated stress
runs. The main fixture also agrees with HeadlessChrome 148. The first strict
three-page replay exposed a second root cause: Atlas/overview entered the Node
branch of bundled SHA-256 code because browser globals exposed `process`; empty
server imports then made `Buffer.from` throw.

**Browser profile repair:** use the existing document module-resolution profile
to exclude Node-only global slots during catalog admission, lazy resolution and
late Jube module activation (D7.3.3–D7.3.4). Module globals declare their exposure
on their records. CLI realms retain their Node compatibility; explicit module
imports remain separate from publishing globals. Cover reflection, `typeof`,
dynamic Function, browser-branch selection and post-activation visibility with
`test/ui/dom/browser_global_profile`. Shared script-library feature flags and
website-specific switches are unnecessary. The DOM UI suite passes 163/163,
including the new fixture; its ten assertions also pass with forced GC.
OpenAI replays now reach missing `Worker`/`CryptoKey` and script timeouts. Two
N26 replays still report their bootstrap error. None of these five pages passes
the original strict checks; the complete 37-page set was not rerun.

**Qualification repair:** the runtime baseline exposed a pre-existing teardown
overflow, confirmed on the starting release and by ASan. Import bridge records
are base `Script` allocations carrying the JS language profile; they are not
`JsScript` AST owners. Conversion now checks the extension lifecycle hook before
reading AST callable caches (D5.4.3). The same helper guards module lookup and
re-export traversal. The existing fresh-realm reset unit covers both bridge and
AST owners. Five mixed Lambda/JS batch fixtures pass on the final release host.

**Qualification at wrap-up:** Lambda baseline 5916/5916 and Input 2112/2112;
Test262 40261/40261, zero regressions. Radiant records 6148 passes, 143 partials,
two skips and one failure: `NativeLayoutPreservesScriptCpuBudgetAndObserversRemainBounded`.
That watchdog failure reproduces on the starting release; host-backed interpreter
cancellation polling remains outstanding. A separate ASan fiber-stack report in
`conc/to_promise` also remains unresolved. The final two module-cast guards were
added after the aggregate gates; the final release rebuild passes all 12 focused
AST/MIR forced-GC checks and five mixed-script batch goldens. No full online sweep
or canonical package rebuild was completed at this wrap-up.
