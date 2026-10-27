# Lambda DevTools implementation

**Started:** 2026-10-26. **Status:** M0, the first M1 inspector, and the Lambda
scratch/value slice of M2 implemented. The current phase also adds Radiant child
paging and selection reveal. Qualification results are recorded by delivery below.

Design: [Lambda_Design_Devtools.md](../Lambda_Design_Devtools.md). The user selected
Lambda values and a minimal request/reply/event protocol over local pipes.
Existing semantics and precise ownership remain authoritative: S1.8, D5.3.3,
D5.4.1–D5.4.4, D8.1.1v18 and D8.1.3v42.

## First delivery

M0 and a bounded live-document M1: an owner-thread inspector, length-prefixed
Mark messages, native DOM/style/geometry reads, inline CSS edits, source access,
a headless client and a Live workspace in the existing Electron DevTool.
Source stepping, resource tracing, profiling and memory retention analysis
follow the design's later milestones.

The connection owns only native transport buffers and copied Mark replies.
Document handles remain weak; application state is read at the viewer's ordinary
host-turn boundary. No new interpreter, execution-thread migration, native-stack
GC scanning or hot-path synchronization is introduced.

## Acceptance and verification budget

- Exercise real child-process pipes, fragmented/coalesced frames, invalid
  envelopes/versions, oversized frames, EOF, outstanding requests and independent
  targets. Verify native Mark symbols and document elements across the pipe.
- Inspect block/inline/scroll/transform/flex/grid fixtures, edit one declaration
  and verify the causal geometry/pixel change; reject a stale edit.
- Build the existing frontend and test connection/codec behavior with Node.
  Validate the rendered Live workspace and native output.
- At the stable milestone boundary run Lambda, Radiant and Test262 baselines,
  and validate the native module boundary after adding the inspector API.

## Delivered behavior

Run `make devtool`, select **Live**, and launch a local HTML or Lambda document.
The tools process stays separate from the inspected native process. The Live
workspace provides a lazy DOM tree, computed styles, border-box geometry, inline
CSS edits with priority and one-step undo, native PNG preview, preview picking
and selection highlighting, a local source snapshot and process output. The
**Native window** option also opens the inspected document in Radiant.
The existing **Tests** comparison workspace remains available.

For a frontend-free session:

```bash
node utils/devtool/inspect.mjs test/devtools/document.html
```

Pass a `.mark` request file as the second argument to issue another operation.
The CLI performs `hello` first; `hello.ops` is the implemented capability list.

| Area | Implementation |
|---|---|
| Transport and dispatch | `lambda/devtools/protocol.{hpp,cpp}`: inherited duplex fd 3, four-byte big-endian byte count, UTF-8 Mark; bounded framing, queued owner-thread dispatch, structured errors and disconnect cleanup |
| Document adapter | `radiant/devtools_adapter.{hpp,cpp}`: weak non-reused DOM handles, document lifetime resource, mutation/style/layout revision, shared CSSOM and visual geometry |
| Host integration | `lambda/main.cpp`, `radiant/window.cpp`: explicit `devtools` command and normal host-loop boundary; no debugger work in runtime hot paths |
| Frontend | `utils/devtool/`: launch/pipe ownership in Electron main, narrow preload bridge, transport-neutral client and data-only Mark codec, React Live workspace |
| Verification | `test/devtools/`: native subprocess protocol and pixel tests, shared runner report adapter, actual Electron UI workflow and HTML fixture |

Implemented operations: `hello`, `tree`, `node`, `style`, `set_style`, `pick`,
`highlight`, `screenshot`, `source`, `release`. Events: `changed`, `selected`.
Requests/replies/events are Lambda maps; operation names are symbols, node
snapshots use Lambda elements, and PNG data is Lambda binary. Control messages
are never evaluated as scripts. Saved source is explicitly an attach-time local
file snapshot, not a loader-generation or imported-module source archive.

## Shared fixes needed by the inspector

- `format_mark()` now uses a shared data-literal printing mode. Root strings and
  control characters are escaped; whole-valued floats retain their kind and
  signed zero; decimal and arbitrary-precision integer suffixes survive a Mark
  round trip. `convert -t mark` uses this formatter. Diagnostic display printing
  retains its existing behavior; the existing complex-number golden also passes.
- Native CSSOM editing reuses the existing declaration store/commit path and
  exposes authored values/priorities for undo. Empty values remove declarations.
- A paint-only edit followed by capture exposed a native crash: mutation
  scheduling dereferenced an absent JS execution capsule. It now reads the
  document's existing host state. The screenshot/background-edit test covers the
  triggering path; `temp/devtools-crash-trace.log` records the original LLDB trace.

## Verification record

| Check | Result / evidence |
|---|---|
| `make build` | PASS; `temp/devtools-build.log` |
| `node test/devtools/run.mjs --report temp/devtools-protocol-report.json` | PASS, 15/15 real native pipe/codec/DOM/CSS/lifecycle/pixel checks |
| Focused checks on final release host | PASS, 15/15; `temp/devtools-protocol-release-tests.log` |
| Shared runner selecting `test_devtools_protocol` | PASS, 15/15; `temp/devtools-shared-runner.log` |
| `npm run build --prefix utils/devtool` | PASS; existing duplicate `scrollback` key warning in TerminalPanel; `temp/devtools-frontend-build.log` |
| Electron `test/devtools/ui-check.cjs` | PASS on debug and final release host: launch, native tree/styles/PNG, edit/undo, source, native-window target, retained Tests workspace; `temp/devtools-ui-check.log`, `temp/devtools-ui-release-check.log` |
| Radiant dimension-cast lint | PASS; `temp/devtools-lint.log` |
| `make test-lambda-baseline` (unrestricted) | 8,277/8,279; MIR Hotwire HTTP timeout and `dom_media_playback` failure; `temp/devtools-lambda-baseline-unrestricted.log` |
| `make test-radiant-baseline` (unrestricted) | Initial run: 6,231 passed, 141 partial, 2 skipped, 2 failed out of 6,376; `temp/devtools-radiant-baseline.log`; details below |
| `make test262-baseline` | PASS, 40,261/40,261 active baseline cases, zero regressions, 2,652 skipped; `temp/devtools-test262-baseline.log` |
| `make check-module-boundary` | Strict DSOs build; Class-F ratchet fails on two unlisted imports; `temp/devtools-module-boundary.log` |

The initial sandboxed Lambda baseline was 8,275/8,279: four HTTP listener cases
could not bind loopback sockets. The unrestricted rerun removed that restriction;
the remaining `Tiers/HotwireHttpTest.ClientAndServer/1` timed out at 30 seconds
but passed alone, while `JavaScriptTests/JsFileTest.Run/dom_media_playback`
reproduced alone. These runs do not establish that either failure is pre-existing.

The Radiant run's two failures were the new Node script's missing shared-runner
JSON report and `CssCascadeMemory.PageLoadAndRecascadeStayWithinBaseline`
(`jqueryui` CSS-role live bytes 375,038 against a 373,940 budget). The reporting
adapter is fixed and passes through the shared runner at 15/15. The memory
budget failure remains recorded; its budget was not changed. Layout suites
reported zero regressions with 137 partial cases; UI automation passed 723/723,
DOM UI integration 165/165 and page snapshots 51/51. Aggregate green is not claimed.

The module-boundary checker reports `doc_state_request_repaint(DocState*)` and
`radiant_dom_m4b_src_get` as unlisted Class-F imports (136 actual, 165 in the
recorded set). Both call sites are present in unchanged `HEAD` DOM code; the
inspector introduces neither call. This run does not provide a clean-control
boundary build. The checker and its ratchet were not relaxed.

UI screenshots are saved to `temp/devtools/live-inspector.png` and
`temp/devtools/live-inspector-edited.png`; both were visually inspected. GUI
checks need window-server access and `ELECTRON_RUN_AS_NODE` unset.

## Remaining scope

Source breakpoints/stepping and paused-frame evaluation, live document runtime
scope inspection, structured console diagnostics, resource tracing, performance and
memory panels remain later milestones. Matched CSS rules, inline-fragment
overlays and native-window click picking are also pending. Tree paging and reveal
are delivered by the phase below.
The scratch target below inspects Lambda values using previews and handles;
sized numeric families and other unsupported entities have exact-text or opaque
previews rather than an unqualified direct Mark value.
This delivery was exercised on macOS; Linux/Windows pipe handoff and GUI
behavior still need platform qualification.
Full Chrome parity is not claimed.

## M2 Lambda and Radiant delivery

**User direction:** proceed with Lambda and Radiant developer support;
JavaScript evaluation is deferred. No JavaScript evaluation API or runtime hook
is added by this phase.

### Scope and authority

The Live workspace now includes **Lambda scratch**. It starts another native
process with `lambda devtools --scratch --pipe 3`. This is the existing
persistent **functional** REPL: declarations, immutable top-level bindings and
entry rollback follow S16.7.4–S16.7.6 and D8.1.1v18. The console does not borrow
the Radiant page's lexical environment or introduce a Lambda string-evaluation
builtin (S1.8). Cmd/Ctrl+Enter submits an entry; Alt+Up/Down recalls history.

Results expand as typed, shallow previews. Containers use monotonically allocated
handles and bounded property pages; aliases share a handle. Maps, arrays,
elements, integer/character ranges, binary, Unicode text and typed/strided arrays
use the existing readers and scalar accessors. Typed arrays report shape and
expand in flattened logical order. Small common scalars retain native Mark
kinds and exact values. Wide/sized/complex numerics use exact scalar text with
type metadata until their wire spellings are fully qualified. Functions and
unsupported entities stay opaque. Expansion invokes no user function.

`RootVector` keeps precise retained Item slots, and each handle owns stable
numeric payload storage (D5.2, D5.3.3). The REPL's existing runtime retains its
Script, imported Inputs and binding slab for the session. **Clear results**
releases inspector handles, including handles from a partial expansion; it
keeps REPL bindings. **Stop scratch** ends only that target. **Collect** forces
GC to verify retention. Disconnect clears roots before session/runtime teardown.

The Radiant Elements tree now pages beyond the first 100 children. Selecting
through the preview picker reveals collapsed ancestors, using sibling offsets
to load the relevant page. It does not load every preceding sibling. Existing
style, preview, native-window and Tests workflows are retained.

### Protocol additions

The existing four-byte framing, Mark messages, version 1 handshake and flat
command table remain unchanged. Each connection still identifies one target.

| Target / operation | Behavior |
|---|---|
| Scratch `eval {source}` | Submit one persistent REPL entry; reply `{result: preview, declarations: bool}` or a normal `incomplete`/`rejected`/`evaluation` error |
| Scratch `bindings {offset?, limit?}` | Page the live scope without rerunning accepted entries |
| Scratch `inspect {ref, offset?, limit?}` | Return `entries: [{key, kind, value: preview}]`, `count` and `next`; long string/symbol/binary pages use `value` instead of `entries` |
| Scratch `release {refs}` / `release {all: true}` | Drop precise inspector roots; IDs are never reused |
| Scratch `gc` | Collect and return the number of retained inspector handles |
| Scratch `source {offset?, limit?}` | Page accepted session bytes, labeled `scratch_session`; rejected entries are absent |
| Radiant `path {node}` | Root-to-node records with each node's sibling `offset`, for bounded tree reveal |

Limits: 256 entries per value/binding page, 4096 bytes per text/binary/source
page, 4096 live value handles, 65536 handle IDs per connection, 65536 bytes per
submitted entry, and 256 ancestors per Radiant path. Text byte offsets must
start at UTF-8 boundaries and pages never split a character. Hitting a handle
limit is recoverable with `release {all: true}`. Session source/AST/bindings keep
the normal persistent REPL lifetime and are not subject to the result-handle
budget. A running entry remains cooperative; **Stop scratch** terminates that
process if it cannot return. Exact numbers over the scalar-preview digit budget
are opaque with a digit count, so even a computed BigInt cannot trigger an
unbounded scalar rendering while inspecting a result.

### Files and verification budget

`lambda/runtime/devtools_scratch.cpp` hosts the session, value roots, previews
and commands. `lambda/devtools/scratch.hpp` exports the launcher entry only.
`utils/devtool/` reuses the transport/client for a second independent process;
`LambdaScratch.jsx` implements the console. `radiant/devtools_adapter.cpp` adds
the path reader; `LiveInspector.jsx` uses paged tree windows.

Focused acceptance covers transaction rollback, exact common scalars, aliases,
paged maps/elements/ranges/strided arrays, UTF-8/binary/source limits, forced GC,
release/staleness, quota recovery and target isolation. The actual Electron
workflow covers persistent entry/expansion/bindings/GC/release alongside a live
Radiant document, picker reveal, child paging and the retained Tests workspace.
Run the required Lambda and Radiant gates once this phase is stable, plus
module-boundary validation; record failures without changing budgets/harnesses.

### Qualification

| Check | Result / evidence |
|---|---|
| `make build` | PASS; `temp/devtools-m2-build.log` |
| Final focused native protocol/REPL checks through shared runner | PASS, 25/25; `temp/devtools-m2-final-tests.log`, `temp/devtools-m2-shared/test_devtools_protocol_results.json` |
| Frontend build | PASS; existing TerminalPanel duplicate-key warning; `temp/devtools-m2-frontend-build.log` |
| Electron UI | PASS: scratch/coexistence, picker reveal, child paging, native window and Tests; `temp/devtools-m2-ui.log`; screenshot `temp/devtools/lambda-scratch.png` visually inspected |
| Radiant cast lint | PASS; `temp/devtools-m2-lint.log` |
| Lambda baseline | 8,276/8,281; five recorded failures; `temp/devtools-m2-lambda-baseline.log` |
| Radiant baseline | 6,259 passed, 141 partial, 2 skipped, 5 failed out of 6,407; DevTools 24/24 passes; `temp/devtools-m2-radiant-baseline.log` |
| Test262 baseline | PASS, 40,261/40,261 active cases, zero regressions, 2,652 skipped; `temp/devtools-m2-test262-baseline.log` |
| Module-boundary check | Strict DSOs build; Class-F ratchet fails on six unlisted imports (140 actual / 165 recorded); `temp/devtools-m2-module-boundary.log` |

Focused/UI checks use an isolated debug host at `temp/devtools-m2-host.exe`
(SHA-256 `c3ca75a2e5c6c3b1d2522a33967dc86b7f45ef19100bfea229bfca9775e8faa1`).
It prevents concurrent builds from replacing the executable during a check.
This phase leaves source stepping, paused-scope evaluation, producer-level
structured diagnostics and native-window overlays for later work.

The expanded GUI check exposed the original picker's deferred access to React's
cleared `event.currentTarget`. It now captures coordinates and image geometry
inside dispatch. The UI harness also preserves a failed assertion in Electron's
exit status. The collapsed-ancestor picker scenario and the >100-child paging
scenario both pass after these fixes.

The Lambda gate passed all 50 existing REPL interface tests and all 346 MIR
forced-GC checks. Its five failures were the MIR Hotwire HTTP integration case,
`dom_media_playback`, and the three `proc_spelling_check`,
`proc_spelling_front_coded`, `proc_spelling_vocab_array` cases. A focused spelling
invocation also failed (`temp/devtools-m2-spelling-check.log`). No clean-control
run establishes these as pre-existing; neither test expectations nor runtime
budgets were changed to hide them.

The Radiant failures were the CSS cascade memory budget (`jqueryui`, 375,038
CSS-role live bytes against 373,940) and four spelling UI cases:
`slide_spelling`, `slide_spelling_canvas`, `slide_spelling_plain`,
`slide_spelling_rich`. Layout suites had zero regressions with 137 partial cases;
DOM UI integration passed 165/165 and page snapshots 51/51. Aggregate green is
not claimed. Unrelated package/build/IME work was present in the shared checkout
and was left untouched by this task.

The module-boundary failure includes `view_get_client_matrix`,
`doc_state_request_repaint`, `view_geometry_block_viewport_origin`,
`editing_geometry_text_control_for_each_selection_rect`,
`dom_boundary_from_source_pos` and `radiant_dom_m4b_src_get`. Their call sites
are present in unchanged `HEAD` DOM/selection code. The Class-F ratchet remains
unchanged; no clean-control build is claimed.

The final focused host includes inspector-only fixes made after the aggregate
runs: numeric preview bounds/type labels and reply-owned character-range
strings. Final frontend checks also cover session replacement guards. These
were qualified by the final 25/25 shared-runner check and Electron workflow;
the full engine baselines were not repeated for those isolated inspector paths.
