# Lambda DevTools implementation

**Started:** 2026-10-26. **Status:** M0 and the first M1 inspector implemented;
aggregate qualification has recorded failures below.

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

Source breakpoints/stepping and paused-frame evaluation, arbitrary runtime-value
inspection, structured console diagnostics, resource tracing, performance and
memory panels remain later milestones. Matched CSS rules, inline-fragment
overlays and native-window click picking are also pending. The current UI shows
the first page of children; protocol clients can page with `offset`/`limit`.
The frontend codec covers the implemented document/control values and exact
common Mark scalars; ranges, sized numeric families and arbitrary runtime graphs
still need qualification before a runtime-value inspector is exposed.
This delivery was exercised on macOS; Linux/Windows pipe handoff and GUI
behavior still need platform qualification.
Full Chrome parity is not claimed.
