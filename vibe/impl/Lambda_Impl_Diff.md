# Lambda Document Diff — Implementation

> **Status:** initial unified package implemented, 2026-10-16. Verification
> results and remaining gate failures are recorded below. Authorized by the user on 2026-10-15.
> **Design:** [Lambda_Pkg_Diff.md](../Lambda_Pkg_Diff.md).
> **Public package:** `lambda.doc.diff` (D7.2.4/D7.2.6).
> **Contracts:** S12.1.1v3/S12.1.3, S11.1.2v4, D4.2.6, D5.3.3;
> grammar facilities follow GM1–GM4/GM10/GM11v2/GM14.

## Delivered surface

The facade is `lmd/package/doc/diff.ls`. It exports pure `parse`, `to_html`,
`viewer` and `repository`, and procedural `read`, `open_repo`, `compare` and
`load_file`. [Lambda_Diff.md](../../doc/Lambda_Diff.md) documents options, the
model, launch commands and limits. `.diff` and `.patch` open in the interactive
viewer; `layout` and `render` select the complete static projection.

| Module | Responsibility |
|---|---|
| `grammar.ls` | Patch records, quoted paths, raw NUL-delimited Git entries and path lists. |
| `parse.ls`, `common.ls` | Count-aware hunk/file assembly, validation, byte paths, spans and diagnostics. |
| `rows.ls` | Indexed virtual rows, hunk movement, full-file search and model selection/copy. |
| `presentation.ls` | Shared unified rows, scoped styles and complete static HTML. |
| `git.ls` | Repository discovery, comparison plans, manifests and selected-body capture. |
| `jobs.ls`, `surface.ls` | Async capture, generations, cancellation, virtual file/row lists and review controls. |
| `view.ls` | Document-transform entry for patch files. |

Grammar syntax produces typed line records with byte spans. Semantic lowering
uses declared hunk counts to distinguish conventional unified file headers from
identical payload prefixes. Payloads remain ranges in the captured binary;
quoted paths decode into bytes before display. Truncated/overlapping hunks,
invalid newline markers, unexpected payload and combined patches are rejected.

Repository review supports unstaged, staged, HEAD-to-worktree and two-revision
comparisons, nested launch directories, linked worktrees and unborn staged
comparisons. Raw manifests preserve byte paths and rename identities. Untracked
files load as additions; unmerged paths show a conflict summary. Only the
selected body is loaded and retained. Live reads validate the raw entry and
compare two patch captures; this is a stable observation, not an atomic
filesystem snapshot. Zero worktree object IDs do not prove content freshness
since manifest capture.

The viewer provides file filtering, hunk/page/wheel/keyboard movement, search,
side copy and model selection across virtual pages. UTF-16 DOM offsets convert
through the source editor's existing helpers to codepoint positions (S2.5.8).
Shift-click extends from the model anchor even when offscreen. Copy omits
headers/gutters and respects missing final newlines. Static output emits every
loaded file and row, independent of viewport size.

## Generic native support

These mechanisms contain no Git parsing or review policy (D7.5.2/D7.5.3).

- **Binary grammar and result ownership (GM14, D5.3.3).** Byte matchers lower
  token ASTs to a byte alphabet and Latin-1 RE2. Binary captures remain binary;
  text parsing retains Unicode behavior. Public grammar calls use a private
  parse pool and rooted GC-owned result graphs instead of registering an
  accumulating InputManager arena. Existing native Input-owned parsing remains
  available. Span narrowing is checked. Control bytes in diagnostics are
  escaped. `parse(bytes,"text")` also returns a GC-owned string.
- **Byte construction (S2.2.2v2).** `binary(array)` validates integer bytes and builds
  the payload without text encoding; empty bytes retain the language's null
  representation.
- **Structured process capture (S12.1.1v3, S13.3.2).** `io.exec(program,args,
  options)` accepts argv, cwd, environment overrides, per-stream byte limits
  and a deadline. A libuv worker owns native copies only; managed results are
  built on the runtime thread. Separate binary stdout/stderr retain NUL and
  trailing newlines. Cancellation waits for process/pipe cleanup, with process
  group termination and bounded drain turns. Git policy disables external
  diff, textconv, configured attribute filters, color and optional index locks;
  paths use literal pathspecs and commands use no shell.
- **Task lifetime (D5.3.3).** Completed result/mailbox graphs are traced through
  reachable handles instead of remaining scheduler roots forever. Scope-owned
  handles stay rooted until scope exit. Small native task records and scalar/
  fault homes remain until scheduler teardown.
- **Escaped callback activation (D5.3.3, D6.3.1).** Each fresh activation owns
  its interpreter state. An escaped task no longer inherits a pointer to a
  retained event callback's stack-local state guard. The ambient hook has a
  destroy callback for owned caches, including abandoned stacks.
- **DOM transport (D7.5.3).** Clipboard writing reuses the platform clipboard.
  The existing boundary hit test is exposed to Lambda-only documents through
  the shared realm-neutral geometry result builder. No application state or
  selection policy lives in native code.
- **AUTO imports (D7.2.2, D8.1.1).** Imported type/grammar declarations resolve
  before T0 runtime-variable slots in a promoted function; these compile-time
  bindings have no value slab slot. A hot imported-grammar fixture covers this.

Building the required gates also exposed a generated linker dependency issue:
source-built external libraries lost their transitive provider links. The
Premake generator now follows `build_target` dependencies; its self-test checks
that the archive consumer receives iconv/lzma/zstd/bzip2. No generated Lua or
vendor source was edited.

## Verification

Functional verification uses a debug host; no performance results are claimed.
The final focused matrix passes 60/60 tier/script runs. Parser and viewport
validation changes were also checked after the full runtime gate.
The shared checkout also contains unrelated package moves and native work.

| Check | Result |
|---|---|
| Focused grammar/diff/process goldens | 20/20 on interpreter, AUTO and MIR JIT. |
| Diff UI fixtures | Patch 6/6, repository 14/14, virtual review 16/16 assertions, on all three tiers. |
| Byte grammar with GC before every allocation | 12/12 assertions on JIT; all five parser/manifest/rows/process/Git scripts also match their goldens. |
| Virtual review with GC before every allocation | 16/16 assertions on interpreter. |
| `LambdaConcurrencyRuntime.*:ActivationCore.*` | 26/26 native tests, including completed-result reachability. |
| Radiant position/dimension lint | Passed. |
| Premake generator self-test | Passed during implementation. |
| Full Lambda baseline | Main runtime gate 1589/1589, input baseline 2112/2112 and math corpus 921/921. Early native checks failed while concurrent rebuilds made the executable unavailable. The separate recheck passed 1692/1693 across those 16 suites; only forced-GC `music_layout` remains failed (exit 137 in its JIT-forced mode). The original full command is not a green gate. |
| Full Radiant baseline | Build passed. UI 596/602 (all three diff fixtures passed); layout 3143/3294; page snapshot 51/52; paged secondary-view runner failed. Stopped after about ten minutes with repeated scene/view timeouts; no complete green gate is claimed. |

Fixtures are harness-owned under `test/lambda/{diff,grammar,proc}` and `test/ui`.
Every new Lambda unit script has a golden. The parser also covers invalid-byte
hunk titles, displayed through the same byte-safe path as payload rows.
Disposable repositories exercise
rename, empty/binary/symlink/untracked files, Unicode paths, disabled clean
filters, linked worktrees, unborn HEAD, conflicts and stale loads. Process tests
cover simultaneous large streams, exact bytes, bounds, deadlines, environment,
cancellation and a child ignoring SIGTERM after closing its pipes. UI fixtures
exercise mount, mode changes, lazy loads, refresh, superseded tasks, search,
offscreen copy, Shift-click and saved-patch routing.

## Limits

The initial viewer is unified and read-only. Side-by-side alignment, intraline
highlights, source-backed context expansion, automatic refresh and repository
writes remain follow-on scope. Combined merge patches are unsupported; binary
patch bodies are opaque summaries. Copy contains patch-visible context only.
Invalid UTF-8 retains byte identity and displays/copies an explicit binary
literal. Default patch/process source bounds are 16 MiB, and the 200,000 record
limit is checked after grammar recognition. Only a current body is cached.
The broader gates retain the forced-GC music failure and the Radiant failures
listed above. The separate Jube DOM ordinal-parity test also fails on member/alias
dispatch in the current checkout; the targeted task/activation suites pass. Windows process
branches have not been exercised on this macOS host. Release
scale/performance measurements remain unperformed.
