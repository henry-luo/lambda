# Lambda I/O Terminal: script-first sessions, frames and readline

**Status:** Implementation is in progress. The script package and CLI mount
are active; a Jube-backed Node adapter covers line events, callback and Promise
questions, prompt and cursor queries. Full Node conformance, Windows validation,
the native-code reduction gate and the remaining completion checks in §7 are
open. The original proposal and audited baseline below are retained for review.
Package name `lambda.io.terminal` approved by the user on
2026-10-06. User-directed architecture: a `view` or `edit` template, editor data
in instance state and/or its model, `on` event handlers, and a pure body producing
a `<terminal>` session root containing a `<frame>` presentation, with state at
both session and frame levels. **One active frame per terminal** is confirmed
by the user on 2026-10-05. Detailed attributes, lifecycle and host/adapter
contracts remain proposed. **Confirmed design goal: minimize native C+ and
replace as much of `cmdedit` and historical `js_readline` as possible with Lambda
source.** See [the implementation record](impl/Lambda_Impl_IO_Terminal.md) for
current code and validation.

**Date:** 2026-10-06. **Version:** 0.4.1.

**Source audit:** Lambda-opus `f722d537f`. Sections describing “current source”
refer to that baseline, before the implementation began.

**Scope:** Move command-line editing and readline behavior into shipped Lambda
source, shared by the Lambda REPL and Node-compatible readline interfaces.
Introduce a reusable terminal rendering target and template host; keep OS
mechanisms and host-language adaptation native. This does not port the REPL
evaluator or the entire Node stream library.

**Spec linkage:** [S12.1.3](../doc/Lambda_Formal_Semantics.md) (pure template body,
procedural handlers); S9.1.4/S9.1.7 (instance state ownership); S12.1.1v2 (effects);
S2.5.8, S8.3.1v3, S17.4.1 (text coordinates); S12.4 (resources);
S13.1.2v2/S13.3.2 (suspension/cancellation);
[D7.1.4v2/D7.1.6](../doc/Lambda_Formal_Design.md) (runtime-only CLI profile);
D7.2.1–D7.2.4/D7.2.6 (packages and imports); D7.2.5 (analogous DOM
policy/mechanism split, not a terminal-specific ruling); D5.3.3 (precise roots);
D7.3/D7.4 (Jube boundaries). For the specific temporary-document contract,
[PTH44v2, PTH76 and PTH50v3](Lambda_Design_Reference.md#44-temp-documents-pth44v2-pth76)
provide the working design details. API spellings below are proposals, not new
formal rulings.

## 1. Recommendation and feasibility

**Use a session template containing a child `edit` template for the current
readline frame. The implementation keeps editable text and caret in frame
template state; the frame model carries identity, prompt and options.**
Session state survives successive commands; frame state survives successive
renders of one command. Their `on` handlers implement interaction, and their
pure bodies compose a `<terminal>` tree containing the current `<frame>`.
A native shell mounts the template, delivers input events, presents its output
and receives submitted lines. The package does not need its own `pn main()`.
An application may use `pn main()` to mount it, or a convenience `pn read_line()`
to mount/await it, but neither should contain a second editing loop.

This directly follows S12.1.3: **“template body = pure `fn` transformation;
mutation only in `on` handlers (`pn`)”**. A `view` with all editor data in instance
state is also viable. An application that needs document transactions can put
the buffer in a `temp.` model and bind an `edit` template to it; that adds model
head/commit wiring and is not required for the state-owned CLI/Node editor.
Store each value in one authoritative place, not both state and model.

**The language is sufficient; the host/runtime integration at the audited
baseline was not.**
Templates, state, procedures, strings, arrays and document values can express
the editor. Existing headless template state and interpreter/MIR template entry
  points are useful foundations. Missing work includes a terminal host, event
integration, a terminal renderer, terminal lifecycle/I/O and package
resolution. No new language syntax or alternate compiler backend is needed.

“Fully ported” means **editing, input-protocol decoding and terminal rendering
algorithms execute in Lambda**. A generic renderer is not inherently native:
frame normalization, text/cell layout, wrapping, diffing, caret placement and
ANSI/VT generation all operate on values and belong in script. Native C+ is
limited to OS/device access, safe runtime entry/lifetime, shared Unicode table
access and language-boundary adaptation. A renamed C+ editor or native renderer
does not meet the goal.
There are two separate completion levels: replacement of the active CLI and
historical JS behavior, and full Node readline conformance. The latter also
requires API families and stream integration missing from the deleted JS code.

### 1.1 Completion contract for minimal native code

The minimal-native goal applies to rendering and protocol code as well as editor
policy. It is enforced through the following design and completion requirements:

- Sections 3.4 and 5 define a script/native ownership contract and the minimum
  primitive services; being generic alone is not a reason to make code native.
- Section 4.5 specifies how script rendering runs after pure template evaluation,
  including output acknowledgement, backpressure and renderer-state ownership.
- Section 6.2 limits the JS adapter to language/stream mechanics; readline policy
  and compatibility state transitions stay in Lambda.
- Appendix D maps the old native function families to destinations and defines
  retirement and net-native-code gates, including all replacement files.

This is a completed allocation of responsibilities for the proposal, **not a
claim of implementation or parity**. Runtime entry, model rebinding, stream
integration and platform behavior still need the gates in Appendices A/B/D.

## 2. What exists in this checkout

### 2.1 `lib/cmdedit.c`: an active native terminal editor

The current source has 1,888 lines, with another 282 in
`lib/cmdedit_utf8.c`; these are physical line counts, not an estimate of removable
code. `lambda/main-repl.cpp` calls its `repl_*` API, and `run_repl()` in
`lambda/main.cpp` owns evaluation, continuation prompts and accumulated source.

| Responsibility | Current implementation | Destination |
|---|---|---|
| TTY detection, raw mode, terminal size, OS input/output | `terminal_init`, `terminal_raw_mode`, `terminal_get_size`, `terminal_read_key`, `terminal_write` | Native byte/OS-record transport; Lambda protocol decoder feeds template events |
| Signal capture/restoration | `install_signal_handlers`, `restore_signal_handlers`, `check_signals` | Runtime signal transport and resource cleanup |
| Insert/delete/movement/redraw | `editor_*`, `enhanced_bindings`, `handle_*` | Lambda handlers/body, layout, diff and control-sequence generation |
| History | Bounded linked list, adjacent deduplication, prefix search, file load/save | Lambda arrays and history policy; existing file I/O |
| Kill/yank/transpose/word operations | Native handlers and a ten-entry global kill ring | Lambda state and commands |
| Completion | `rl_attempted_completion_function`, first-candidate replacement | Lambda completion protocol; host callback adapter where needed |
| Non-TTY input | `getline` on POSIX; a Windows input loop | Streaming line splitter using the same input transport |
| C compatibility | `readline`, `add_history`, `repl_*`, `rl_*` globals | Temporary migration adapter, then removal after caller audit |

The current behavior is less complete than some helper names suggest:

- Interactive character insertion accepts only bytes 32–126, despite UTF-8
  cursor helpers. Transposition swaps bytes. Unicode input/editing is therefore
  not established by the UTF-8 helper tests.
- The POSIX forward-delete decoder returns `KEY_DELETE == 127`, which the
  subsequent platform mapping remaps to backspace. Key identity and raw byte
  identity need separate representations.
- `rl_bind_key` is a successful no-op. Word movement helpers are not bound to
  decoded Meta keys. Prefix-history search exists without a bound search UI.
- Redraw clears one line; it does not model wrapped rows. `SIGWINCH` is noticed
  but its refresh remains a TODO.
- Failure to enter raw mode recursively calls the same readline path while
  the terminal remains marked TTY. This is not a working fallback.
- Ctrl-C, Ctrl-D and input failure all return `NULL`; `run_repl()` interprets
  that as loop termination. History is added inside the interactive editor
  and again by the REPL, with adjacent deduplication hiding the overlap.
- History loading uses a 1,024-byte buffer. `read_history` masks all load
  failures when a filename is supplied, rather than just missing-file errors.

These are source-audit findings, not newly executed reproductions. Before
changing each behavior, add a focused reproducer and specify the intended result.
Do not make the script copy these defects to match a native snapshot.

### 2.2 `js_readline.cpp`: historical, not currently compiled

**`lambda/js/js_readline.cpp` is absent at the audited HEAD.** Commit
`bc68bf228e4adf38959936bcf657f617d40ced86` (2026-09-16, “node source clean up”)
deleted it. This analysis uses the 1,805-line file at
`bc68bf228^:lambda/js/js_readline.cpp`.

The current `lambda/module/node_core/node_core_module.cpp` still registers
`readline` and `readline/promises`, but their factories delegate to
`node_core_host_namespace`. The current
`jube_host_node_resolve_host_namespace` in `lambda/jube/jube_registry.cpp`
only lists `module` and `url`. Those descriptors therefore do not provide a
working source implementation of readline today. The remaining registry test
and entries in `test/node/official_baseline.txt` are historical expectations,
not evidence that readline currently works.

The deleted file was a separate editor, not a wrapper around `cmdedit`:

| Historical JS behavior | Port requirement |
|---|---|
| `createInterface`, positional `Interface`, callback and Promise namespaces | Keep Node-visible constructors/identity in the compatibility adapter |
| `question`, custom promisification, AbortSignal handling | Lambda question state machine; JS callback/Promise/error adaptation |
| `on`, `close`, `write`, prompt setters/getters, `getCursorPos` | Lambda state transitions and layout; adapter provides JS object/event semantics |
| String/typed-array chunks, partial UTF-8, CR/LF, final partial line | Shared incremental decoder and line splitter |
| Character/word editing, history navigation/search, kill/yank, undo/redo | Shared editor commands with profile-specific defaults |
| Synchronous/callback/Promise completion and repeated Tab | Explicit completion requests and replies, generation checking, shared matching/layout |
| Input listeners, microtasks, retained input/interface pairs | Host subscriptions/scheduling/precise roots, not editor policy |

Its limitations matter when defining acceptance:

- Several edits and byte decoding use fixed 8,192-byte arrays, while completion
  rendering uses smaller fixed buffers. A port needs explicit limits or dynamic
  storage, never silent truncation.
- `readline_render_completion_matches` assumes particular candidate positions;
  other paths contain literal “First group” and “Error: message” output and
  fixed counts of empty writes. Replace these with a general algorithm using
  the actual candidates, prompt, width and error. They are not a specification.
- Cursor positions are UTF-8 byte offsets. Lambda's text API uses code points;
  the JS-facing position requires its own compatibility conversion.
- `on` retains one callback per event. Input setup replaces `write`/`end` and
  sometimes `emit`; `close` does not implement a complete subscription teardown.
  Use real stream/EventEmitter contracts at the boundary.
- Finite `crlfDelay` is stored but elapsed-time suppression across chunks is
  not implemented; the cross-chunk special case checks only the infinite delay.
- The exported namespace contains only `createInterface`, `Interface` and
  `default`. It does not establish the full pause/resume, async-iterator,
  cursor-helper, public keypress or Promise `Readline` action-batching surface.

Node's reference API covers arbitrary readable/writable streams, events,
callback/Promise questions, terminal helpers and async iteration. Use the
[official readline documentation](https://nodejs.org/api/readline.html) as the
API reference and pin implementation tests to this repository's `ref/node`
revision. The local `node_version.h` identifies a 27.0.0 development tree;
the live documentation currently identifies 26.10.0, so they must not silently
be treated as the same test snapshot.

## 3. Package and ownership boundaries

### 3.1 Namespace

**Approved name (USER, 2026-10-06): `lambda.io.terminal`.** It names the whole
session, including its active frame, line editing and presentation. `io.read`
retains its existing whole-file input meaning; `repl` would imply a complete
read–evaluate–print loop, including evaluation. Node's public `readline` name
is a compatibility surface over this package.

D7.2.4 says **“`lambda.*` is the one root for everything Lambda ships”** and
**“no path may be both a built-in module and a shipped package.”** Keep the
existing built-in `lambda.io`/`io`; put the script at the distinct leaf
`lambda.io.terminal`.

Recommended source layout:

```text
lmd/package/io/terminal.ls              public facade and terminal session template
lmd/package/io/terminal/frame.ls        frame editor template and handlers
lmd/package/io/terminal/model.ls        pure editing operations and model helpers
lmd/package/io/terminal/keys.ls         key bindings and command selection
lmd/package/io/terminal/text.ls         UTF-8 chunk assembly and coordinate conversion
lmd/package/io/terminal/protocol.ls     terminal bytes/OS records to input events
lmd/package/io/terminal/layout.ls       frame normalization, cell rows and caret
lmd/package/io/terminal/diff.ls         previous/desired screen to output operations
lmd/package/io/terminal/vt.ls           ANSI/VT encoding and prompt-control parsing
lmd/package/io/terminal/present.ls      script presentation state and output ordering
lmd/package/io/terminal/display.ls      frame content and completion presentation
lmd/package/io/terminal/history.ls      history, search, kill ring and undo helpers
```

For example, `import rl: lambda.io.terminal` would select the facade.
This import is **not implemented in the audited tree**:
`append_shipped_package_module_path`
in `lambda/runtime/build_ast.cpp:12978` explicitly excludes all
`lambda.io.*` paths. Change the resolver to distinguish exact built-in exports
and registered modules from distinct shipped leaf modules. Preserve built-in
precedence and reject exact-path collisions; do not add a readline-only special
case or a working-directory fallback. Test aliases and fully qualified calls.
This extends source resolution under D7.2.4/D7.2.6 without turning `lambda.io`
itself into two modules.

Do not register a native editor module at the exact package path. Generic
terminal-host services may be exposed through the existing runtime I/O boundary;
`lambda/radiant` can supply the corresponding presentation integration without
owning a second implementation of readline.

### 3.2 A mounted template, hosted by the shell

```text
CLI shell / Lambda application                 Node readline adapter
            |                                          |
            +----------- mount / dispatch -------------+
                                  |
                       terminal session template
                          | session state
                       frame edit template
                          | frame model/state
                          |          ^ on handlers
                     pure bodies     | input/events
                          |          |
                   <terminal><frame ...>>
                          |          |
                  Lambda layout / diff / VT
                          |          |
                   minimal native transport
```

The shell creates a session/model, loads the package once, mounts its template
and drives the ordinary runtime event loop. An event enters an `on` handler;
the handler updates model/state and may emit a semantic event. At the event
boundary the host reconciles changes, reevaluates the affected pure body and
presents the new tree. The shell receives `line`, `interrupt`, `eof` or failure
outcomes. It still owns evaluation, continuation prompts and REPL commands.

A standalone Lambda program can bootstrap this from `pn main()`. The native
Lambda REPL mounts the same component directly. Importing the package or merely
constructing a `<terminal>` element must not acquire a TTY, start a loop or print:
those are effects under S12.1.1v2. A convenience procedural reader is a wrapper
around this mounted lifecycle, not the primary editor implementation.

D7.2.1 requires module bindings to be immutable. There are no replacements for
`g_editor`, `g_history` or the global kill ring in script module globals. Each
mounted editor has its own state and model. Shared history is explicit policy.

### 3.3 Terminal target and Radiant layering

`<terminal>` represents the entire mounted terminal session. Its `<frame>`
child describes the current presentation, including text and logical caret.
The initial backend connects to the shell's existing TTY or supplied streams;
these elements do not create a GUI window. A later Radiant GUI backend could
render the same structure with a different input/output adapter.

Support the elements alongside those used by `lambda/radiant`, without requiring
a window, DOM or CSS layout engine to edit a terminal line. The session/frame
structure describes output and lifetime; it is not a list of `print()` calls
or imperative cursor commands.

D7.1.6 defines `lambda-cli` as the runtime-only host and excludes Radiant;
D7.1.4v2 also distinguishes runtime-only CLI from full-engine headless modes.
Therefore keep the common template host and script renderer independent of
Radiant, with a Radiant adapter sharing them where appropriate.
Do not link `radiant.a` into the minimal CLI just to recognize `<terminal>`.
The existing GUI `view`/`edit` commands need not change meaning: a terminal mount
is a separate presentation target for the same template language.

### 3.4 Ownership contract: Lambda first, native by necessity

D7.2.5 already puts DOM editing policy in the shipped Lambda behavior package.
Apply the same separation here as a design choice; that ruling does not itself
require a native terminal renderer. Every new C+ routine must identify an OS,
VM/GC or host-language operation that cannot already be expressed through the
available script primitives. Otherwise implement it in Lambda.

| Responsibility | Lambda owns | Native C+ may retain |
|---|---|---|
| Device connection | Session policy and requested mode | Open/borrow handles, is-TTY/size/capability queries, mode save/restore, exclusive lease |
| Input | UTF-8 tails, CSI/SS3/OSC parsing where applicable, Escape deadlines, bracketed paste, key/modifier mapping, Windows record interpretation | Nonblocking bytes or a direct value projection of OS console records, arrival time, EOF/error/resize/signal delivery |
| Editor | All buffer edits, cursor/selection, commands, history/search, kill ring, undo/redo, completion and questions | No editor commands or native text buffer |
| Frame rendering | Element normalization, style/prompt parsing, widths using shared Unicode metadata, rows/wrapping/tabs, caret, completion columns, screen diff, ANSI/VT serialization | Ordered byte writes; for a non-VT Windows backend, direct checked execution of already-planned OS drawing operations |
| Presentation lifecycle | Desired/acknowledged screen values, coalescing, transcript versus live-frame ordering, redraw after other output | Queue/backpressure mechanics, partial writes, completion notification and cleanup |
| Unicode | Byte/code-point/UTF-16 coordinate algorithms and word-boundary policy | Thin access to existing utf8proc width/category data; no copied Unicode tables |
| Template hosting | Session/frame decisions and handlers | Shared mount/context binding, invoke/apply, event delivery, document-source identity, precise roots |
| Node compatibility | Defaults, history rules, line splitting, question/abort decisions, completion requests, pause/iterator/batch state when implemented | JS identity/coercion/errors, callbacks/Promise objects, actual EventEmitter/stream/AbortSignal registration through existing services |

The renderer helpers above are reusable Lambda code even while shipped inside
`lambda.io.terminal`; no additional public package name is required now. A later
GUI adapter must not duplicate the editor. No native ANSI parser, cell-layout
engine, history list or completion algorithm is allowed under a different name.
Existing standard-library string/array primitives remain usable; this port does
not attempt to reimplement the general language runtime in script.

## 4. Model, state and output contract

### 4.1 Two state scopes, with one owner for each value

| Scope | Recommended data | Lifetime |
|---|---|---|
| Terminal session template/model | History entries and persistence policy, kill ring, shared options, active frame identity and session status | Mount to unmount; survives Enter, interruption and frame replacement |
| Frame model | Identity, base prompt and immutable frame-specific options | One input interaction; can span wrapped rows or continuation prompts |
| Frame template state | Editable text, caret/selection, undo/redo, history navigation/draft, search and completion candidates/generations | Persists across updates to this frame; resets or transfers explicitly when the frame is replaced |
| Private Lambda presentation/protocol state at session scope | Decoder tails, deadline generations, desired/acknowledged screen, pending presentation and transcript events | Survives frame replacement; ordinary rooted Lambda values, not native editor structs |
| Native host state attached to the terminal session | Terminal lease, capability observations, subscriptions, timer/write request IDs and transport buffers | Owns device lifecycle and in-flight I/O; does not interpret editor or renderer state |

Session history and frame history-navigation state are different data: entries
survive across inputs, while the selected entry and pending draft belong to the
current interaction. A frame receives history/options as values and asks the
session to update shared data through events. Do not keep independently mutable
copies of the history list or kill ring in both scopes. Sharing across separate
terminal sessions is an additional explicit model, not a module global.

Implement the two script scopes with two corresponding template instances:
a session template renders `<terminal>` and applies the child editor template;
the editor renders `<frame>`. Element nesting alone does not create mutable
state scopes. S9.1.4 says state lives in a procedural activation or template/
object instance, and S9.1.7 excludes module globals. Both bodies obey S12.1.3;
handlers and document operations own mutation. The returned elements remain
ordinary immutable values, not mutable containers for native handles.

A `temp.` document, when chosen by an embedding application, is **in memory,
not an OS temporary file**. PTH44v2/PTH76 specify its evaluation/document-context
lifetime and immediate creation; PTH50v3 specifies snapshot/head behavior. Two
template state scopes do not require a document provider or a disk file. The
current CLI and Node mounts own the live buffer in frame instance state;
file-history persistence remains a separate operation.

A local snapshot assignment is not a document commit. If an application chooses
the optional `temp.` model, its host must connect edit-model updates to the
current document head and invalidate the associated templates. Existing `temp()`
alone does not provide that wiring. Use one mutation route through the document
update machinery; do not maintain a local text copy and periodically overwrite
the real model.

For that optional document-backed mode there is a concrete identity issue:
`TemplateStateKey` includes the matched
`model_item`. A commit can replace that item. The host needs stable session and
frame source bindings, with state rebinding for updated heads. Updating a parent
must not remount the child; updating frame content must not reset its cursor or
undo. An intentional new frame identity must initialize new frame state while
preserving session state. These host bindings are proposed integration work,
not an assertion that an `id` attribute currently implements keyed reconciliation.
Do not change global template identity semantics without reviewing existing
users. Unmount releases both levels and their roots.

### 4.2 Pure bodies, session root and frame snapshots

The proposed output structure is:

```lambda
<terminal id: 'repl',
    <frame id: 12, caret: {region: 'input', offset: 3},
        <span role: 'prompt', "λ> ">
        <span role: 'input', "abc">
    >
>
```

This is a schematic value, not a runnable mounted editor. Attribute names and
child vocabulary remain proposed. The session template produces the outer
root; the child frame template produces the inner result. Session identity is
stable for the connection's lifetime. The logical frame identity is stable
while editing the current input, even though each evaluation returns a new
immutable frame snapshot.

**Confirmed (USER, 2026-10-05): one active `<frame>` per `<terminal>`.**
A frame can contain multiple text rows, completion suggestions and status
content. An idle terminal may have no active frame; a second simultaneously
active frame is invalid. Previous rendered revisions are not accumulated as
sibling `<frame>` elements. Multiple visible regions belong within the one
active frame, rather than introducing competing frame lifetimes or focus.

Distinguish **logical frame lifetime** from **render revision**:

1. Mount the terminal session and its first frame editor.
2. An input event changes the frame model/state; reevaluation produces a new
   snapshot of the same frame. The renderer diffs it against the previous
   presentation. Cursor/undo/completion state survives.
3. Submission emits the accepted line once to the session. The session records
   history according to policy and requests that this presentation be committed
   to scrollback. Merely returning a frame does not submit it.
4. Retire the old frame, cancel/invalidate its pending work, and create the next
   frame when the shell is ready. History, kill ring and the terminal lease
   survive. Enter that merely continues multiline input can stay within the
   same logical frame; the caller's input-completeness policy decides.
5. Unmounting `<terminal>` ends the session and releases the native lease.

A frame snapshot is therefore one desired display state, not an append-only
output record or an animation tick. Rendering may coalesce redundant snapshots;
semantic submit/interrupt/output events must never be dropped or duplicated.
Async replies carry both frame identity and request generation, so a completion
for an old input cannot modify a new frame that reused its generation counter.

Caret offsets are logical code-point positions within a named input region.
The renderer maps those to terminal cells; styled prompts and completion rows
do not change the input coordinate space. Start with text runs, line breaks,
styles, logical caret and optional status/completion rows. Do not promise
arbitrary HTML/CSS inside `<frame>`. Unknown constructs should give a visible
diagnostic. ANSI-colored Node prompts need an explicit normalization path,
including nonprinting CSI/OSC sequences, not byte-count width guesses.

The Lambda renderer measures cells, wraps rows and plans the operations to
clear the old footprint and position the physical cursor. An unchanged snapshot produces no output. Frame replacement
still reconciles against the terminal's last physical presentation; allocating
a new frame identity is not a reason to leave old rows or reopen the TTY. The
template decides what to display; Lambda renderer helpers compute geometry
and encode ANSI/VT output. Native transport only executes the resulting writes
or already-planned platform operations.

The terminal session coordinates **committed scrollback** and the **live frame**.
Rerendering never appends the prompt/line again. Other task output passes through
the session's script presentation coordinator, which plans the suspend/write/
redraw sequence and submits ordered transport writes. Removing a frame
without submitting it clears/retires its region according to lifecycle policy;
it must not implicitly append it to history or scrollback. Arbitrary external
writes to the same descriptor cannot be assumed to preserve screen position.

### 4.3 Event and outcome contract

| Event delivered to the template | Handler responsibility |
|---|---|
| Text input / paste | Insert one decoded chunk, update caret and undo, invalidate stale completion |
| Keydown with key and modifiers | Select Lambda editing/history/search command; Enter submits |
| Completion reply with frame identity and generation | Reject stale replies, replace range or display candidates |
| Geometry/capability change | Update presentation inputs; preserve model text and cursor |
| Stream chunk/end in nonterminal mode | Run shared incremental line assembly and deliver final partial line |
| Interrupt / close / input failure | Apply profile policy and emit distinct outcomes |

Input targets the active frame's handlers. Submission and shared-history/kill
requests bubble to the session template, which owns their shared state. The
session receives close/resize/transport events and propagates relevant geometry
or lifecycle changes to its child. Resize preserves both identities.

These are proposed normalized template events, produced by the Lambda decoder; existing GUI event names alone do
not imply terminal dispatch exists. Each event's state/model changes must become
visible before outward callbacks run. Reentrant callbacks enter a coherent
session, and the outer dispatch must not overwrite newer nested state. Commit
and redraw happen at documented event boundaries; input bursts may batch
presentation without losing semantic line events.

`emit` already models template event bubbling. Extend the host to receive root
semantic events, preserving parent-template routing when embedded. The current
`lambda_radiant_emit` uses a single registered Radiant hook; it is not already a
per-terminal-session receiver. A terminal mount needs scoped dispatch context and
an explicit root sink or queued outcome channel, without stealing GUI events.

Completion providers receive the line/cursor and return candidates/replacement
range. Synchronous providers may answer in the current event; asynchronous
providers schedule a later completion event with the frame identity and a
request generation. Handlers must not block the shared loop or park inside a
Jube host frame. Cancellation, edits,
new requests, frame retirement and close invalidate outstanding replies.

An Enter that submits input emits the accepted text once. EOF with pending nonterminal text emits
that line before EOF. Ctrl-D on an empty interactive buffer ends input; with
text it performs the configured delete command. Recommend Ctrl-C interrupts the
current CLI input and reprompts instead of exiting; this is a proposed UX change,
not an existing formal ruling. Errors and cancellation are not ordinary EOF.

### 4.4 Editing and text policy

Port active CLI bindings plus historical JS word editing, undo/redo and
completion through shared commands. Keep CLI history defaults/filtering distinct
from Node options. A history-navigation round trip restores the pending draft.
The REPL decides whether complete logical inputs or continuation fragments enter
history, through one script policy. File persistence uses existing file APIs;
a missing file may be accepted, while permission/decode/write failures remain
observable.

S2.5.8/S8.3.1v3/S17.4.1 require code-point text coordinates. Keep those distinct
from transport bytes, terminal cells and JS UTF-16 positions. Initial support
can use code-point editing with the existing utf8proc width data. Grapheme
editing and exact emoji-ZWJ terminal agreement remain additional scope; neither
follows automatically from code-point support.

The Lambda protocol component retains partial UTF-8/key sequences and decides
Escape ambiguity and CRLF deadlines using host timestamps/timer notifications.
Windows records are copied into ordinary values by native code; script maps
virtual keys, repeats, modifiers and UTF-16 units to the same event shape. Raw
byte 127 must not conflate forward delete with backspace. Native input code does
not own protocol state or decide what a key means. Bracketed-paste assembly and
paste limits also belong in Lambda.

Nonterminal sessions split chunks into CR/LF/CRLF-delimited lines, retain finite
CRLF-delay timing, and deliver an unterminated final line. They do not activate
raw mode or emit cursor escapes. No fixed 8 KiB buffers or silent truncation;
bounded queues report limits explicitly. Arrays/records and string slices are
adequate initially; optimize the script representation before proposing any
native editor-buffer extension outside this design's allowlist.

### 4.5 Script renderer invocation and output acknowledgement

The native host invokes the template bodies through the existing runtime. Once
affected bodies have produced the desired tree, it passes that value and device
observations to a **Lambda presentation backend** bound once when mounting. It
must not inspect text runs to compute rows or terminal control sequences.

The backend uses pure helpers with conceptual contracts:

```text
fn decode_input(protocol_state, raw_event, capabilities) -> DecodeResult
fn layout_frame(frame, capabilities) -> Screen
fn diff_screen(previous_screen, desired_screen, capabilities) -> RenderPlan
fn encode_vt(operations, capabilities) -> string
```

Start with deterministic row layout and a simple script diff that clears/repaints
the changed portion of the owned region; an optimal edit script is unnecessary.
Handle right-margin pending wrap, wide/combining characters, tabs, shortened
rows and style resets explicitly. Optimize output volume only after the screen
oracle and release measurements justify it. Cell width describes the selected
Unicode/terminal profile, not a universal promise about every emulator.

`DecodeResult` contains updated protocol state, normalized events and requested
deadlines. `Screen` contains rows/cells, styles, logical-to-physical caret data
and the owned footprint. `RenderPlan` contains ordered low-level output plus
the predicted next screen. They are ordinary data, not new language effects.
Only handler/backend procedural entry points submit events, timers or writes;
template bodies and these helpers remain pure under S12.1.3/S12.1.1v2.

The presentation coordinator's mutable values belong to the terminal session.
They may be retained as an opaque rooted Lambda state value by the host, separate
from view invalidation. Updating a write request or acknowledged screen must not
recursively trigger the editor body/presentation hook. The host supplies generic
callback invocation and roots; it neither maintains a parallel C+ screen model
nor selects redraw operations. Editor changes and resize explicitly request a
new presentation, while transport acknowledgements enter the script coordinator.

Serialize output transactions per sink. Keep at most one submitted presentation
plan awaiting completion and remember the newest desired snapshot in script.
On complete acceptance by the ordered transport, advance the acknowledged screen
and plan from it to the latest desired frame. Do not advance the cache after a
partial write or emit another plan relative to an unaccepted screen. Node stream
backpressure (`write` returning false) means accepted-but-wait-for-drain, not a
request to resend the same bytes. The adapter maps that stream contract to
transport events; Lambda controls what presentation to submit next.

Coalesce replaceable frame snapshots only. Submitted lines, external transcript
writes, close and outcome notifications retain order and are delivered once.
A partial-write error invalidates the physical-screen assumption and enters the
error/cleanup path; never blindly replay an append to scrollback. On resize,
finish or fail the current write transaction, invalidate geometry and recompute
layout from the current frame. Blank/control-only writes and exact callback
ordering required by Node must be evidence-based compatibility decisions in
script, not hard-coded output counts in the C+ adapter.

## 5. Runtime sufficiency and minimum native services

| Capability | Current evidence | Required change |
|---|---|---|
| Editor/protocol/render algorithms | Strings, arrays/maps, code-point indexing, `ord`/`chr`, pure functions and procedures | Implement in Lambda; no native editor/layout/diff/ANSI engine |
| Templates/state | `template_registry`, `template_state`, `render_map` support interpreter/MIR and headless state | Factor generic host entry and stable session/frame bindings; no second template engine |
| Temporary model (optional) | `temp()` and document contexts | Required only for a document-backed embed; state-owned CLI/Node mounts need no commit path |
| Shipped imports | Resolver excludes `lambda.io.*` leaves | Fix generic leaf resolution and exact-path collision handling |
| Device input/output | Mechanisms exist within `cmdedit.c`; `pn_print` has no reliable transport contract | Extract only OS operations; expose event subscription and checked ordered writes |
| Unicode metadata | `cmdedit_utf8.c` already calls utf8proc | Expose a thin deterministic width/category query; loops and offset/word algorithms stay in script |
| Timers/cancellation | Existing concurrency runtime and Jube async services | Reuse scheduling/cleanup; add only missing event/timer delivery bindings |
| JS boundary | Jube call/object/Promise/root services exist; readline factory absent | Add generic module/template-session access and thin stream/language adapters |

The existing string-pattern `w`/`a` classes are documented as ASCII ranges; they
are not a substitute for Unicode category metadata when porting the UTF-8 word
helpers. Expose the existing table data rather than transcribing Unicode ranges.
`string(binary)` formats a literal, so the protocol decoder must assemble UTF-8
explicitly using byte access and `chr`, or reuse a proven general codec. No new
native codec is a prerequisite for the port.

### 5.1 Native allowlist and data crossing the boundary

These conceptual services define the native budget; exact exported spellings
remain proposed. Reuse an existing service rather than add a second wrapper
where its ownership, errors and scheduling already meet the contract.

| Service | Native responsibility | What stays in Lambda |
|---|---|---|
| Mount / dispatch / unmount | Load once, enter the correct interpreter/MIR activation, bind source identities, root values and restore host context | Session/frame lifecycle decisions, active-frame validation, all handlers |
| Subscribe / unsubscribe input | Borrow/open the selected transport, deliver bytes or OS records, EOF/errors/size/signals on the runtime thread | Key/UTF-8/paste decoding, line splitting, deadlines and interpretation |
| Device info / mode | Query input/output kinds and dimensions, apply requested raw/VT mode transactionally, retain restoration data | Capability selection/fallback policy and requested display behavior |
| Submit output / completion | Write ordered length-delimited data, retain it until accepted, handle partial writes/backpressure, report completion/error | Layout/diff/VT encoding, display cache and transcript/redraw ordering |
| Timer / monotonic time | Existing clock/scheduler operations and cancellation | Escape/CRLF policy, generation checks, deciding when a timer is needed |
| Post/deliver event | Generic target-instance routing and rooted event delivery, including scoped parent/root emission | Decode results, child selection, submit/history/completion decisions |
| Unicode properties | Existing utf8proc cell width/general category lookup for a valid scalar | Width accumulation, tabs, wrapping, coordinate conversion and word policy |
| JS objects and subscriptions | Existing coercion/error/identity/Promise/callback and stream/EventEmitter/AbortSignal mechanics | Readline-specific state machine and compatibility decisions |

Transport handles are host-owned leases or opaque IDs, not script pointers
(D7.4.1v2/D7.4.3). A `data` event has nonempty bytes; distinct events carry EOF,
error, resize or timeout. S2.2.2v2's solid binary type is not an empty-buffer EOF
protocol. The protocol component chooses the active frame and uses generic
instance dispatch for its normalized events. `emit` retains parent routing;
root outcomes use the mount's sink. Existing single-global Radiant emit wiring
must be generalized without a terminal-specific event-handler interpreter.

Timestamp input arrival and schedule deadlines in one monotonic clock domain;
use integer milliseconds at this boundary. Timer results carry session/frame
and request generations. The Unicode query is a pure function returning cell
width and general category for a valid scalar; reject out-of-range values and
surrogates, and preserve a distinct nonprinting result so script handles controls
and tabs. Input byte indexing already returns `u8` values in `item_at`; it does
not require a new native byte-walking editor helper.

Use bounded, non-suspending dispatch for synchronous JS calls. Handler updates
and required synchronous callbacks complete before that call returns; asynchronous
I/O/timer/completion work posts later events. Reuse runtime scheduling rather
than add a C+ readline loop or pump a nested event loop. Exceeding input/output
queue limits produces a defined failure/backpressure event, never truncation.

On POSIX, reuse runtime readiness with termios. On Windows, enable VT where
supported so the same Lambda encoder is used. If a supported console lacks VT,
Lambda generates low-level operations for a small direct Win32 executor: already
chosen cursor coordinates, erase ranges, attributes and text. It must not parse
a frame, choose wrapping/diff strategy or interpret an ANSI stream. Redirected
streams receive bytes through their stream adapter. Platform string encoding
conversion is a transport mechanism, not another editor implementation.

Native signal handlers only record/wake. A second incompatible device owner
fails explicitly. A write completes once its full data is accepted by the ordered
transport, not after terminal-device drain; flush any retained userspace buffer
before awaiting input. Cleanup restores OS modes even if Lambda rendering or a
handler fails. No native `render_frame`, `read_key`, `move_word`, `history_next`
or `complete_line` API containing those algorithms is part of this design.

### 5.2 Cleanup with host-owned sessions

The shell/host owns the terminal lease for a mounted editor and guarantees
cleanup on initialization failure, normal unmount, handler/output failure,
cancellation and runtime shutdown. Supported signal and suspend/resume paths
must restore/reacquire modes correctly. SIGKILL cannot have an in-process
restoration guarantee. Rooted session values obey D5.3.3; no conservative native
stack scanning is permitted.

This ownership changes the earlier proposal's prerequisite: a complete public
Lambda resource implementation is **not required just to mount this editor**.
The terminal is held by the host, as a window is held by a GUI host. A procedural
`read_line()` wrapper may register host cleanup with its task lifetime and await
an outcome under S13.1.2v2/S13.3.2.

If a later API exposes an independently transferable terminal resource to script,
S12.4's auto-close/ownership rules must be implemented, including errors and
cancellation. S12.4.3 says the GC finalizer is only a backstop; explicit close on
the happy path alone is insufficient. Do not introduce unsupported `finally`,
`defer` or `with` examples to disguise that gap.

### 5.3 What stays entirely in Lambda

Input decoding, bindings, editing, word movement, transpose, undo/redo, kill/yank,
history, line splitting, question state, completion, layout, screen diff and
terminal control encoding are script code. Pure helpers remain replayable
outside a mount, but tests must also exercise actual `on` dispatch. No shell
`stty` commands, readline-specific
native editor API, alternate backend or vendor patch is needed.

## 6. REPL and Node integration

### 6.1 Lambda REPL

`run_repl()` currently reads through a C pointer-returning API. Replace that
boundary with a mounted template session and line/outcome delivery. Retain its
package, document context, template state and source binding independently of
the user's evolving REPL evaluation context. Load/compile the package once,
not per keystroke. Preserve precise roots across input and evaluation.

On submission, finish the live frame, suspend raw input and return its text to
the shell. After evaluation, create the next frame within the same terminal
session. Continuation input can retain the current frame when the shell asks
for more text. Continuation prompts, statement completeness and evaluation stay in the shell;
the template does not become a REPL evaluator. Failed user code must not corrupt
the editor context, and model resets must preserve the intended history/state.

Bundle `package/io` in every interactive CLI installation and resolve it relative
to Lambda home under D7.2.3/D7.2.6. Test the installed runtime-only CLI without
the source checkout, Radiant or Node modules. Missing package diagnostics should
be clear; a plain-input recovery path must not become another rich editor.

### 6.2 Node-compatible readline

The Node adapter mounts the **same template** against arbitrary readable/writable
streams. It converts JS calls/chunks to events and template outcomes to callbacks,
EventEmitter events or Promise settlements. A stream-backed session must not
open the process TTY. Constructor/prototype identity, listener ownership,
AbortSignal registration, JS error objects and Promise machinery use existing
host-language services. The adapter contains only their thin bindings; it must
not reimplement a private EventEmitter, Promise scheduler or stream library.

S13.1.2v2 makes every exported Lambda `pn` Promise-returning; direct `pub pn`
methods therefore cannot implement all synchronous Node readline methods.
Use a host template-dispatch entry for the bounded, non-suspending handler phase,
just as a UI host enters an event handler. This is an internal host entry, not a
change to the public Lambda-to-JS procedure membrane. Async completion and I/O
continuations become later events after the caller has unwound.

D7.4.2v2 forbids parking with a JubeHostAPI frame on the stack. Prove that the
chosen handler entry respects that contract on interpreter and MIR; do not
assume every arbitrary `on` handler is safe for synchronous Jube entry. The
handler phase is deliberately non-suspending: attempted suspension raises
the existing barrier fault. Async work must be scheduled explicitly as later
events; do not silently defer a half-executed handler.
Never pump a nested event loop to wait for a Lambda procedure.

Keep question/abort/close decisions, history filtering, completion generations,
CRLF timing, key dispatch and output planning in Lambda. The adapter maps each
JS method/property operation to one typed template event/query, and translates
outcomes back to JS. It may retain callback/Promise/stream references in existing
rooted host slots, but not a second line buffer, cursor, history or question
state machine. JS line/cursor coercion uses JS services; actual code-point/UTF-16
coordinate calculation is shared Lambda code. `getCursorPos` consumes the same
Lambda layout result used for rendering.

As full Node surface is added, `emitKeypressEvents` uses the shared script decoder;
cursor/clear helpers use the shared script encoder; Promise `Readline` batching
is a script operation list with host Promise settlement. Pause/resume and iterator
state belong to script, while actual stream pause/listener calls and JS iterator
objects use their existing host mechanisms. These families remain the separately
reported conformance scope, not reasons for a second native algorithm set.

Prefer existing source-based JS wrappers where a shipped loader supports them;
otherwise keep the necessary C+ object trampolines small. Do not add a new JS
loader solely to claim fewer adapter lines, or move portable readline behavior
into a JS-only implementation instead of the shared Lambda package.

The session must publish new state before calling JS listeners. Nested `write`,
`close`, or property changes must not be overwritten by the outer dispatch.
Reflect line/cursor/history access through explicit model/state mapping; a
detached history copy is not enough where caller mutation is observable.
Maintain S9.1's Lambda value semantics at the JS aliasing boundary.

Expose generic versioned template-session/load/dispatch services through Jube
only where missing, following D7.3/D7.4; no `Runtime*`, AST/MIR pointers or direct
private runtime imports in `node_core`. The existing graph loader alone does
not establish a usable session-to-template interface. Do not restore deleted
`js_readline.cpp` or `js_stream_*` internals. Current streams/EventEmitter and
missing full-Node API families remain explicit dependencies and separate gates.

## 7. Adoption and completion criteria

Start with a minimal terminal-session template and frame editor that accept
events, update a temporary model and redraw. Verify both updating the same frame
and replacing it while preserving session history. That exercises the new
architectural seam before porting all editing commands.

Completion requires:

- All editor, protocol and renderer algorithms in Lambda, invoked by CLI and
  Node through the same package; actual handler/backend entry and pure replay
  both covered. New C+ is restricted to the section 5.1 allowlist.
- Stable session/frame state across head replacement and rerender; intentional
  frame replacement resets frame state while preserving session history/kill
  ring. Completion from a retired frame cannot affect the next one.
- Enforce one active frame per terminal; reject a tree with multiple active
  frames and verify that replacing a frame never leaves two active instances.
- Terminal restoration on normal/error/cancellation paths and responsive shared
  task scheduling, with no mandatory Radiant dependency in `lambda-cli`.
- Accurate rendered screen/caret state for wrapping, shrinking, resize, styled
  prompts and coordinated output; rerender does not duplicate scrollback.
- Installed-package, pipe/redirect/EOF and multi-session tests, plus JS synchronous
  behavior, reentrancy, Promise ordering and forced-GC coverage.
- Retirement of `cmdedit.c`/header/editor-specific wrappers after caller migration;
  preserve reusable platform/Unicode mechanisms. No native editing policy remains
  hidden in the transport, renderer hook, generic helper or adapter. Appendix D
  inventories every retained native responsibility and counts replacement code.
- Full Node compatibility reported separately until its missing API families,
  stream prerequisites and pinned corpus pass.

## Appendix A. Implementation sequence and code locations

| Phase | Work and dependencies | Exit gate |
|---|---|---|
| P0 — characterize | PTY reproducers for `run_repl`; audit 84 CmdEditTest cases; pin historical JS and `ref/node` cases; specify events/outcomes and terminal/frame lifetime | Existing defects distinguished from compatibility and proposed changes |
| P1 — shared mechanisms | Extract shared handler entry from `radiant/event.cpp`; reuse registry/state/render-map; connect document heads, scoped dispatch and rooted script backend hooks | Headless template handles input on interpreter/MIR; both state scopes survive head replacement; frame replacement retires only the child; GUI dispatch still works |
| P2 — native primitives | Extract only OS transport/modes from `cmdedit.c`; expose Unicode metadata; reuse timer/task/cleanup services and minimal stream bridge | Byte/record I/O, cancellation, partial writes and restoration pass; no native decoder/layout/diff introduced |
| P3 — script package/backend | Implement templates, decoder, layout/diff/VT and presentation coordinator; fix leaf resolution; define optional procedural facade | Actual event/render hooks, chunking, acknowledged screen, Unicode/history/completion and installed imports pass |
| P4 — CLI | Mount from `main-repl.cpp`/`main.cpp`; retain editor context; coordinate evaluation/output/history | PTY and pipe interaction, repeated evaluations, continuation and failure paths pass in CLI profile |
| P5 — Node | Generic Jube template-session seam and current stream/event adapters; same template/state | Synchronous entry, callback/Promise ordering, reentrancy and historical surface pass; remaining full-Node status explicit |
| P6 — retire/package | Remove native policy/stale globals; edit `build_lambda_config.json` and regenerate through `make`; update package manifests/docs | Focused, baseline, platform, installation and Appendix D native-reduction gates pass |

Follow [Doc_Convention](../doc/Doc_Convention.md); substantial implementation
progress belongs under `vibe/impl/`. This working proposal does not revise a
formal ruling. Any approved change to formal semantics/design must update the
relevant specification and this record together.

## Appendix B. Validation plan

**Source inspection and Git history only: no runtime build, test suite or
performance experiment was run for this documentation change.** The existing
`lambda.exe` predates the audited HEAD. New fixture names below are proposed.

| Fixture family | Required assertions |
|---|---|
| `test/lambda/io_readline_model.ls` + `.txt` | Insert/delete/move, multibyte transpose, kill/yank, undo/redo, history/draft policy |
| `test/lambda/io_readline_protocol.ls` + `.txt` | UTF-8/CSI/CRLF splits, Escape deadline, finite CRLF delay, EOF distinction, large paste |
| `test/lambda/io_readline_display.ls` + `.txt` | Pure terminal/frame-tree output, stable identities across render revisions, prompt/completion variants, logical caret |
| `test/lambda/proc/io_readline_session.ls` + `.txt` | Actual parent/child handler dispatch, temp commit/rebind, frame update versus replacement, session history survival, stale-frame replies, cleanup |
| `test/lambda/io_terminal_render.ls` + `.txt` | Script layout/diff/VT, exact output for wrapping/shrinking/styles, operation plans for non-VT Windows, no output for unchanged frames |
| `test/test_io_terminal_gtest.cpp` | Native primitive contract, script-backend invocation, partial writes/backpressure, roots, dispatch and cleanup; no duplicated C+ renderer oracle |
| `test/test_io_readline_pty.py` | Actual keystrokes/paste, screen/cursor, termios restoration, resize, redirected streams, shell evaluation/context survival |
| `test/node/io_readline_shared_core.js` + `.txt` | Same mounted template; synchronous visible state, reentrancy, history mutation, questions, abort/close, no listener/root leaks |

Include CJK, combining marks, tabs, styled/ANSI prompts and supplementary JS
characters. Assert that model edits preserve cursor/history and that source
snapshots remain immutable. Replacing a frame must reset its caret/undo without
losing session history or reacquiring the terminal. Deliver a stale completion
after replacement with a matching request counter but different frame identity
and assert that it is ignored. Resize must preserve both state scopes. Reevaluate an unchanged body repeatedly and verify
no terminal output. Inject other task output while editing and verify the line
and physical cursor recover. A second terminal owner must fail cleanly. Exercise
render coalescing under delayed writes, Node write(false)/drain without duplicate
bytes, output failure after a prefix, and timer races across frame replacement.
Run representative input/render cases with the Lambda backend disabled and
require a clear unavailable-feature error, proving no hidden native editor
quietly takes over; separately identified plain-input recovery is not parity.

Use POSIX PTYs on macOS/Linux and a Windows console/ConPTY harness on Windows.
Mocks and the 84 old C tests do not replace real interaction and screen checks.
Check the runtime-only CLI dependency boundary and installed package resolution.
If shared Radiant dispatch changes, exercise existing document-editor handlers
and state lifetimes as well as the terminal target.

After corresponding implementation/fixtures exist, commands should include:

```sh
make build-test
./test/test_io_terminal_gtest.exe
python3 test/test_io_readline_pty.py --lambda ./lambda.exe
./lambda.exe test/lambda/io_readline_model.ls
./lambda.exe test/lambda/io_readline_protocol.ls
./lambda.exe test/lambda/io_readline_display.ls
./lambda.exe test/lambda/io_terminal_render.ls
./lambda.exe run test/lambda/proc/io_readline_session.ls
./lambda.exe js test/node/io_readline_shared_core.js --no-log
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 ./lambda.exe js test/node/io_readline_shared_core.js --no-log
./test/test_node_gtest.exe --modules=readline --no-update-slow-list
make test-lambda-baseline
make test262-baseline
make check-node-module-architecture
```

Rebuild matching host and Jube DSOs when their ABI changes; do not validate a
new host against stale `node-core` artifacts. Run the installed dynamic-module
readline cases as well as static integration, adding them to the authoritative
Makefile matrix. Run `make test-radiant-baseline` when shared Radiant code changes. Register every
new `.ls` with its `.txt` expected result in the normal harness and run focused
Lambda cases on interpreter and MIR, with/without forced GC. New fixture commands
are not existing targets or reported passes. Do not patch vendored tests or mask
runtime failures in harnesses. Run full `make node-baseline` only when explicitly
requested; focused readline compatibility is required for this feature.

Report failures and stream prerequisites individually. The official corpus
includes iterators/backpressure, CSI/keypress, finite CRLF delay, Escape timeout,
recursive writes, positions, Promise completion, raw mode and reopen. Namespace
smoke tests are not a conformance gate. Performance work requires `make release`,
matched transcripts and exact binaries; distinguish startup/first prompt from
steady-state redraw, large paste, allocation/GC and idle CPU.

## Appendix C. Audit anchors

Line numbers refer to `f722d537f`; symbol names are the durable anchors.

| Source | Anchor |
|---|---|
| `lib/cmdedit.c:257`, `:314`, `:413`, `:1037` | `terminal_init`, `terminal_raw_mode`, `terminal_read_key`, `editor_readline` |
| `lib/cmdedit.c:819`, `:1229`, `:1453`, `:1852` | `editor_refresh_display`, `history_add_entry`, `handle_tab_completion`, `handle_transpose_chars` |
| `lib/cmdedit_utf8.c:31` | `cmdedit_utf8_display_width`, existing utf8proc dependency |
| `lambda/main-repl.cpp:134`, `:217`; `lambda/main.cpp:1112` | CLI editor wrappers and `run_repl` |
| `lambda/runtime/build_ast.cpp:2789`, `:12978`, `:13050` | Built-in member lookup, shipped-path exclusions and import dispatch |
| `lambda/runtime/sys_func_registry.c:1097`; `lambda/runtime/concurrency.cpp:1408` | I/O exports and whole-file `pn_io_read` |
| `lambda/runtime/lambda-proc.cpp:60`; `lambda/runtime/lambda-eval.cpp:4877` | `pn_print` and binary-to-string formatting |
| `lambda/runtime/lambda-data-runtime.cpp:3459`; `doc/Lambda_String_Pattern.md` (Character Classes) | Binary byte indexing and documented ASCII word/alphabetic classes |
| `lambda/runtime/concurrency.cpp:1215`; `lambda/runtime/concurrency_js.cpp:187` | Task scope cleanup and `lambda_js_wrap_procedure` |
| `lambda/runtime/template_registry.{h,cpp}`; `lambda/runtime/template_state.{h,cpp}` | Compiled/interpreted template entries, state identity and headless state storage |
| `lambda/runtime/render_map.{h,cpp}`; `lambda/runtime/doc_context.cpp`; `lambda/runtime/lambda-eval.cpp` (`temp_document`) | Render/source association, dirty tracking, document head installation and temporary documents |
| `radiant/event.cpp` (`call_template_event_handler`, `invoke_template_handler`); `lambda/runtime/radiant_event_hook.{h,cpp}` | Existing graphical dispatch and single registered emit hook |
| `lmd/package/edit/shell.ls`; `test/ui/doc_editor.ls` | Existing edit-template, instance-state and handler examples |
| `lambda/runtime/module_registry.cpp:436`; `lambda/jube/jube.h:1044` | Procedure export membrane and `JubeModuleGraphAPI` |
| `lambda/module/node_core/node_core_module.cpp:216`; `lambda/jube/jube_registry.cpp:3681` | Readline descriptor factories and current host namespace resolver |
| `lambda/module/node_core/node_tty.cpp:141` | Stub `isatty` installation |
| `bc68bf228^:lambda/js/js_readline.cpp` | Historical source: `readline_decode_input_text`, `readline_render_completion_matches`, `readline_handle_tab`, `js_readline_write_impl`, `js_readline_createInterface` |

To inspect the historical implementation without restoring it:

```sh
git show bc68bf228^:lambda/js/js_readline.cpp
git show --stat bc68bf228 -- lambda/js/js_readline.cpp
```

## Appendix D. Replacement inventory and native-code completion gate

This inventory is a design obligation, not a list of implemented changes.
Baseline is `f722d537f`. Active `cmdedit.c`/`.h` and `cmdedit_utf8.c`/`.h` total
**2,402 physical lines** (1,888 + 186 + 282 + 46). The deleted JS file had 1,805
lines at `bc68bf228^`; it contributes **zero current deletion credit**. Physical
counts include comments/blank lines and are not promised net savings.

| Audited function family | Required destination | Retirement evidence |
|---|---|---|
| `terminal_init/cleanup/raw_mode/get_size/write`, signal install/restore | Extract only OS lifecycle and raw transport; script owns requested behavior | Per-platform primitive tests and restoration on every exit |
| `terminal_read_key` | Split OS byte/record read from Lambda key/protocol decode | No native CSI/key editing switch; chunk-split and Windows record fixtures |
| `editor_*`, `find_key_handler`, all `handle_*` | Lambda frame handlers/helpers; refresh becomes script layout/diff/VT | Real PTY/Node replay plus script backend output assertions |
| `history_*`, `kill_ring_*`, `find_*word*`, `rl_*` globals | Lambda session/frame state and command helpers | History/draft/kill/undo/completion tests; no mutable native policy globals |
| `cmdedit_utf8_*` | Existing string primitives plus Lambda offset/word/width loops; thin shared utf8proc metadata export | Remove editor-specific wrappers and migrate useful tests |
| `readline`, `repl_*`, `add/clear/read/write_history` | Shell mounts/outcomes and package history APIs | Audit callers in `main-repl.cpp`, `main.cpp`, tests and generated build graph; remove temporary rich-editor adapter |
| Historical JS `readline_decode_input_text`, UTF-8/position/character helpers | Shared Lambda protocol, text and layout | Chunking and UTF-16/code-point/cell conversion tests |
| Historical JS word/delete/undo/history helpers and `js_readline_write_impl` | Same frame handlers used by CLI, selected profile/options | No native Node editor branch; matching logical-state/output tests |
| Historical JS completion prefix/render/tab/callback helpers | Lambda completion and renderer; host only calls/receives provider callbacks | Arbitrary candidate counts and stale/reentrant completion tests |
| Historical JS question/signal/line-emission helpers | Lambda question/outcome state; generic JS reference/signal/callback bridge | Abort/close/callback/Promise ordering and listener cleanup |
| Historical namespace, object, input-map and output-write helpers | Current Jube namespace/prototype/root/stream facilities plus thin trampolines | Static/dynamic module parity, forced GC and no monkey-patched stream implementation |

Before implementation, P0 must classify remaining functions/callers omitted by
these families and attach each required behavior to a fixture. Existing no-op,
hard-coded or defective behavior is characterized, not treated as a normative
compatibility requirement. Unused exported helpers are removed after a caller
and public-contract audit; do not port unused C shims merely to retain their names.

Required closeout artifacts, under `vibe/impl/` when implementation starts:

1. A before/after inventory of every production C/C++ file touched or introduced,
   including headers, runtime/template/Jube glue, platform backends and adapters.
   Count all extracted code: moving C+ from `lib/` to `lambda/` saves nothing.
2. For every retained/new native function, identify its section 5.1 primitive,
   existing helper reused and reason script cannot own it. Remove duplicated
   protocol, editor or rendering algorithms regardless of file naming.
3. Report both physical and code-line totals and net native reduction using
   `utils/verify_loc_reduction.sh`, with a manifest covering deleted, modified
   **and new** production files. Record Lambda source additions separately;
   keep tests/vendor code out of production totals. The scoped native total must
   decrease. Do not remove comments/blank lines or compress formatting to pass.
4. Delete `cmdedit` source/headers when callers have migrated; keep any essential
   extracted platform/Unicode primitive under its shared owner. The historical
   JS source stays deleted and its behavior is restored through Lambda. No
   native fallback rich editor, duplicated renderer or private stream library.
5. Link behavior, PTY/Windows, output-acknowledgement, static/dynamic Node, GC,
   packaging and baseline results. Distinguish historical-surface replacement
   from full Node conformance and record unresolved prerequisites explicitly.

Example LOC gate, after P0 creates the NUL-delimited complete manifest (not an
existing artifact or an executed check):

```sh
./utils/verify_loc_reduction.sh --ref f722d537f --files0-from temp/terminal_native_files.nul
```

No arbitrary native-line cap or percentage is promised before implementation.
The functional ownership allowlist, removal of the old editor implementations
and measured net decrease are all required; none alone establishes completion.

## Appendix S. Superseded architecture

Version 0.1.1 proposed ~~a public procedural `read_line` driver around a pure
`step(state, event)` reducer, script-generated ANSI output and mandatory public
terminal-resource support; Node would call the reducer directly~~. It separated
portable policy from OS mechanisms and avoided the exported-`pn` Promise
membrane, but duplicated driving responsibilities across hosts.

Version 0.2.0 adopted the user's template direction, with ~~a single
`<console>` output root and an undivided editor-instance scope~~. Its generic
renderer abstraction and host-owned cleanup remain; the original element did not express
session lifetime separately from the current presentation. Pure helpers remain
useful, and an optional `pn read_line` awaits the mount. Synchronous Node entry
now requires an explicit non-suspending host dispatch contract; the old reducer
proposal is not a second active architecture.

Version 0.3.0 adopts the user's `<terminal><frame ...>>` structure. A terminal
session owns a child frame editor; session state survives frame replacement,
and frame state survives render revisions. The output vocabulary, ownership
split and lifecycle above replace the single-root presentation. The exact
attributes and host reconciliation API remain proposed; no formal semantic
ruling is changed.

Version 0.4.0 supersedes the earlier ~~native general-purpose renderer and
host-owned protocol interpretation/cache~~. The v0.3.1 audit also found that
the Node adapter's scope and replacement-code accounting were too open-ended.
The minimal-C+ goal requires Lambda
layout, diff, VT encoding, decoding and presentation state as well as editing.
Native code executes only the primitives in section 5.1. The session/frame
hierarchy and one-active-frame decision are unchanged. Appendix D makes actual
replacement and net native reduction explicit implementation gates.
