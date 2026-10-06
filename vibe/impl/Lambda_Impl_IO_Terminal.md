# Lambda I/O terminal implementation record

Status: **in progress**. Design and completion gates are in
[Lambda_IO_Terminal.md](../Lambda_IO_Terminal.md). The CLI and an initial Node
adapter both use the same Lambda package. Full Node compatibility, Windows
validation and the native-code reduction gate remain open.

## Current implementation

- `lambda.io.terminal` resolves as a shipped package alongside the exact
  `lambda.io` built-in namespace. The resolver excludes only a direct leaf
  that collides with a built-in `io_*` export (D7.2.4).
  The package was renamed to `lambda.io.terminal` on 2026-10-06; the Node
  `readline` and `readline/promises` module names remain compatibility APIs.
- The Lambda package owns code-point editing, bounded history, draft
  navigation, undo/redo values, byte/UTF-8/CSI decoding, Unicode-aware cell
  layout and desired/acknowledged row diffing. Its `<readline_terminal>` view
  returns one `<terminal>` with one active `<frame>` produced by a child
  `<readline_frame>` edit template. Both templates use `on` handlers and pure
  bodies (S12.1.3); `readline_next` replaces the child model while retaining
  terminal history (S9.1.4, S9.1.7). The live buffer/caret are frame instance
  state, so CLI/Node mounts do not need an OS file or a `temp.` document; the
  latter remains an optional document-backed embed.
- The runtime exposes only utf8proc scalar width/category queries through
  `lambda.io.cell_width` and `lambda.io.unicode_category`; Lambda computes
  wrapping, tabs, caret and word classification (S2.5.8, D7.1.6).
- Handler invocation moved from Radiant to the shared template registry.
  Headless dispatch and a scoped `emit` receiver now exist, so a non-GUI host
  can deliver an event to a template and route child submission to its parent.
  This is the generic host seam; there is no terminal-specific native editor
  command or renderer.
- The CLI mounts one retained template session and exchanges bytes and line
  submissions with it. Its active frame can be replaced without discarding
  terminal history; the shell retains expression evaluation. Forced collection
  exposed that the native template-state hashmap was invisible to precise GC.
  The activation root visitor now traces model keys and state values through a
  collector-neutral state-store callback (D5.3.3, D5.4).
- The frame handlers now include Unicode word movement, kill operations,
  transpose, session-owned bounded kill ring and yank. The protocol collapses
  CRLF across chunk boundaries before dispatching Enter. Ctrl-C yields a
  distinct interrupt outcome and clears pending shell continuation; Ctrl-D
  deletes under the cursor or ends an empty frame. These are Lambda
  reducers, not native editor commands (S2.5.8, S12.1.3).
- The script decoder now recognizes CSI modifier parameters and bracketed
  paste delimiters. Pasted text, including tabs and CRLF, is accumulated in
  Lambda state and delivered as one edit/undo unit per bounded chunk. The VT
  script enables paste mode for a live frame and disables it on submit,
  interrupt, EOF or transport release (S12.1.3).
- Interpreter execution of the decoder exposed a computed-field scratch-plan
  undercount. The shared map/element planner now accounts for the owner,
  key/source and value homes actually held by their evaluator. A second
  diagnostic came from allocating zero bytes for an empty map's field store;
  `map_fill` and `map_fill_items` now share a rooted, nonempty-only allocation
  helper (D5.3.3).
- The CLI's legacy Tab behavior was literal tab insertion because it registers
  no completion provider. The frame handler now routes that key through its
  ordinary text-input/undo path; a real completion provider remains pending.
- `lib/terminal_device.c` now owns POSIX/Windows terminal mode, size and
  transport operations. The old `cmdedit.c`/header and UTF-8 editor wrappers
  have been deleted; their old direct editor test target was replaced by
  script fixtures and headless/PTY integration tests. The POSIX device also
  restores mode after SIGTERM and passes resize observations to the script
  session. Windows enables VT input and output, so the Lambda byte decoder can
  consume console keys where the console supports VT; this path has not yet
  been validated on Windows.
- A generic retained `TemplateHostSession` and size-gated Jube template API
  mount, dispatch, render and close templates without exposing runtime structs
  to `node_core` (D7.3.3, D7.4.2v2). Nested JS-to-Lambda entry suspends the JS
  thread binding and selects session-owned precise root/number side stacks;
  closing inside a binding defers destruction until that binding unwinds
  (D5.3.3). Jube mount, dispatch and render entries raise the activation
  barrier during synchronous script work (D7.4.2v2). The Node adapter now
  subscribes to input events, queues copied
  semantic outcomes until the JS realm is restored, and publishes line,
  close, callback-question and Promise-question results. Lambda owns prompt
  changes, question state, active-frame replacement, UTF-16 cursor conversion
  and screen-cell cursor layout (S12.1.3, S2.5.8).
- Node's bounded history size, dot-command policy, duplicate removal and
  pause/resume state now enter the Lambda session as events. A synchronous JS
  completer, callback completer, or Promise completer receives text through
  the caret and returns candidates; Lambda computes the common prefix or
  single replacement, updates undo/caret and rejects a reply after a newer
  edit or frame change. The adapter copies the provider result before
  reentering Lambda and never edits the line itself. `write(data, key)` now
  sends key objects to a Lambda handler; modifier normalization and commands
  remain in the frame (S12.1.3, D7.4.2v2).
- Existing EventEmitter `emit` had allocated before rooting incoming event
  arguments. Rooting those values at entry fixed a forced-GC loss of the first
  `data` event; the same test now exercises ordinary and Promise readline,
  Unicode cursor position, reentrant `write`, synchronous completion and
  automatic listener teardown.

## Evidence so far

- A source-matched CLI PTY baseline accepted `12`, Backspace, `3`, Enter and
  evaluated `13`; this characterized the old editor before changes.
- `make build-test` passes, including the isolated state-store link target.
- The `io_readline_model`, `io_readline_display`, `io_terminal_render` and
  `io_terminal_diff` fixtures matched their `.txt` output on auto, JIT and
  interpreter tiers. The procedural `io_readline_protocol` fixture now returns
  sixteen true assertions, including Tab decoding.
- The headless JIT and interpreter event-chain tests pass under
  `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, including frame
  replacement, recall, kill and yank. The real PTY/pipe fixture passes under
  the same flags, including key editing, wrapping, kill/yank, CRLF,
  Ctrl-C/Ctrl-D, resize, final partial line, partial transport writes,
  and termios restoration.
- The procedural editing and byte-protocol fixtures pass on JIT and interpreter
  tiers. A PTY fixture proves a bracketed paste is one undo unit, and the
  PTY suite passes under forced collection and freed-object poisoning.
- After the package rename, `make test-lambda-baseline` passed 6,230/6,230;
  the last `make test262-baseline` run, before that rename, passed
  40,261/40,261 with zero regressions.
  Focused Node line, Promise-question and synchronous/callback/Promise
  completion scripts passed under forced GC and freed-object poisoning after
  the latest adapter changes. The key-object path and installed CLI PTY suite
  passed under the same flags.
  The latest default Radiant aggregate reported 3,991 passes, 350 partials
  and one failure: the HTTP-image fixture cannot bind a loopback listener in
  the sandbox. That fixture passed when loopback access was allowed; all 109
  page loads passed. No default full Radiant or Node pass is claimed.
- `make lambda-cli` passed after the rename; its release runtime-only binary
  passed the PTY suite from a copied `lmd` tree outside the source working
  directory. The focused readline scripts also passed with an isolated dynamic `node-core`
  module. The broader `test-jube-node-core-dynamic` gate stopped in the
  unrelated `node-net` initializer (`node:net` unavailable), before its
  readline registry cases; rerunning after `make build-node-net` did not
  change that result. After adding the Jube and Node adapter, the current
  changed-file
  production C/C++ inventory is up 342 physical lines and 772 credited
  code lines versus `f722d537f`, as counted by
  `utils/verify_loc_reduction.sh`; the promised net code
  reduction gate is therefore **not yet met**. The focused Node scripts pass
  under forced GC and freed-object poisoning. Nineteen official readline
  cases currently fail preflight, mostly because the separate `assert` and
  `stream` host namespaces are unresolved. No official Node pass is claimed.

## Required next work

1. Expand the native event-chain test with frame-retirement, stale-event and
   handler-failure checks. Confirm all state and external references survive
   forced GC and cleanup.
2. Complete Lambda editing, input protocol and renderer behavior: script
   rendering of completion candidates,
   remaining key/OSC sequences and Windows VT fallback behavior,
   styled prompts, exact terminal output encoding, resize, screen
   acknowledgement and transcript ordering. Expand the focused fixtures in
   Appendix B of the design record.
3. Validate the minimal OS transport on Windows, including VT input/output,
   resize and modifier delivery; add failure/cleanup fixtures and verify the
   installed runtime-only CLI. Audit the retired `cmdedit` cases against
   script and PTY coverage and run the native LOC gate.
4. Extend the Jube-backed Node adapter beyond the shared line/question core:
   option validation, completion error forwarding, history reflection, async
   iteration, abort, error and stream backpressure. Exercise the now-installed
   Jube activation barrier with a suspension fixture and verify Promise
   ordering (D7.4.2v2, S13.1.2v2).
5. Inventory every
   remaining native primitive against design §5.1, then run the scoped LOC
   gate. Run `make test-lambda-baseline`, `make test262-baseline`,
   `make test-radiant-baseline` for shared dispatch, and the focused Node and
   installed-package gates. A release build is required for any performance
   claim.
