# Ledger names, presence and run-configuration follow-up

**Status:** bounded implementation and qualification complete; LR03-40 remains PARTIAL for Mark encoding. **Base:** `0023db901`.

Existing authority: **S16.9.5**, **S8.2.2v6**, **D5.4.1–D5.4.4**;
profiling capacity is **LR01-5**, with no new language ruling.

This bounded pass covers the four selected records: remaining nominal-field
and attribute presence, namespace key integration (LR03-40), profiling
record/path truncation (LR01-5), and the process-global dry-run flag (LR12-6).

Acceptance cases:

- Optional nominal fields remain absent when omitted, including spread-built
  literals; present null still meets the declared value type. Required nullable
  fields remain required. Defaults and nominal identity/methods survive sparse
  layouts. Both element-validator modes require non-optional attributes.
- Qualified symbols carry local spelling plus resolved namespace identity.
  Map reads/writes, element literals and parsed XML keep qualified and global
  keys distinct; namespace aliases agree. XML scope/default/reset behavior and
  serialization preserve resolved identity and existing markup behavior.
- A 71-script profile retains every row and complete paths longer than 512
  bytes; records own their paths and are released before memory accounting.
- Dry-run configuration belongs to the active run owner, including native I/O
  and compile-time resource reads; independent owners cannot affect each other.

Evidence lives under `temp/ledger-batch4-20261029/`; original audit probes are
under `temp/ledger-triage4-20261029/`. Qualification includes focused
interpreter/JIT/AUTO and forced-GC cases, ASan, the Lambda baseline and Test262.
No pending-ruling policy is changed. S16.9.5, LR01-5 and LR12-6 are archived
in the fixed ledger; LR03-40 retains its explicit remaining format boundary.

## Markup compatibility boundary

The XML representation change exposed native DOM/SVG clients that still ask
with lexical QNames. The adapter now resolves those spellings against field
namespace metadata; XML document matching reads resolved tag identity. The SVG
inline renderer reuses the existing shared extractor instead of its duplicate.
Done criteria are qualified/global href separation, DOM namespace reads and
prefix replacement, plus unchanged XHTML/MathML and the Radiant baseline.
This is an adapter repair, with no layout or paint algorithm change. Native
QName checks precede the aggregate gates; one SVG `<use>` capture checks that
the resource-bearing qualified href reaches its rendering consumer.

## Compatibility and remaining format scope

The first aggregate exposed consumers that assumed XML prefixes were part of
stored characters. The shared document XML adapter now compares qualified
symbols against declarations using the existing `symbol(local, URI)` operation;
DOCX, DocBook and CSL keep their public normalized models. No new namespace
reflection API is invented. Iteration retrieves a value through its complete
key instead of discarding the namespace. `symbol(local, URI)` roots temporary
operands and retains its Target in the run owner until teardown.

Tests that asserted the superseded nested-map namespace behavior now assert
flat qualified keys under S8.2.2v6. Required-nullable recursive schemas spell
optional presence explicitly. SVG local-name inspection is explicit in its
fixture; QName-aware DOM reads remain a separate markup adapter.

**Remaining LR03-40 scope:** Mark has no ruled namespace-URI encoding. Its
ordinary literal-name escaping test remains active, and the namespace-bearing
round-trip reproducer is `test/lambda/_namespace_mark_pending.ls` (currently
false). XML round trips are qualified separately. LR03-40 stays PARTIAL for
this format/design seam; do not claim all namespace encodings are complete or
choose new Mark syntax without a ruling. Existing namespace reflection and
prefix-reservation decisions remain deferred. The other three selected records
can close independently when their gates finish.

## Implementation

**S16.9.5:** nominal declarations and inherited fields retain `is_optional`.
The common `object_from_fields` admission path constructs a sparse physical
shape from fields actually supplied; defaults are evaluated only when needed.
Both tiers use that path for optional constructors and share existing literal
admission. MIR method epilogues write only mutable fields that may change,
tracking optional-field presence so reads and untaken assignments cannot
materialize an absent key. Full and fast element validation now check required
attribute presence separately from its nullable value type. The motion package
schemas explicitly mark fields whose documented values may be omitted.

**S8.2.2v6 / LR03-40:** source namespace imports create owned, parsed Targets.
Symbols store only local characters, with the resolved Target in namespace
metadata. Qualified map reads/writes, key enumeration and iteration preserve
that metadata, as do Input copies and editor shape rebuilds. Namespace-bearing
shapes stay out of transition-tree paths that cannot encode their keys.
MarkEditor drafts carry namespace identity and preserve qualified/global fields
independently through add, replace, retype and delete. XML resolves in-scope
bindings, default namespaces, resets and rebinding; serialization synthesizes
necessary declarations while retaining authored aliases as spelling hints.
The DOM adapter provides lexical QName reads without changing Lambda’s global
string-key semantics; native resource consumers use URI/local-name queries.

**LR01-5:** a growable ArrayList stores owned profile records with complete
paths, replacing the 64-row and 512-byte limits. Allocation failure is reported.
The existing profile-reporting mutex serializes store operations; records are
cleared before process memory accounting and again safely at static teardown.

**D5.4.1–D5.4.4 / LR12-6:** `g_dry_run` is removed. Native C and C++ paths read
`Runtime::dry_run` through `lambda_dry_run_enabled()` and the active canonical
context. CLI compile-time resource reads establish that owner first. Native
coverage alternates independent threaded owners and exercises the C I/O path.

## Qualification

The final source includes the DOM metadata/QName follow-up below. Focused checks pass:

- Namespace ownership, deep copy and editor cases: **41/41** native tests.
- DOM QName/URI reads, prefix replacement and source/view isolation: **41/41**.
- Nominal negative admission across interpreter/JIT/AUTO, full/fast required
  attribute validation, and 71 complete profile paths over 512 bytes: **3/3**.
- Independent native dry-run owners: **1/1**.
- Forced/poisoned GC with `LAMBDA_ROOT_WITNESS=2`: **27/27** script/tier cases,
  including optional method mutation, namespace iteration and XML scope.
- SVG `<use>`: inspected 100×100 captures show the expected red 40×40 square
  at (20,20), including an alternate XLink prefix. The pre-adapter binary’s
  corresponding capture was blank. Hashes and observations are retained in
  `svg-use-comparison.json`; this is a rendering check, not a performance claim.

Aggregate qualification:

- `make test-lambda-baseline`: **8452/8452** combined (**6340** runtime +
  **2112** input). Final source run: `lambda-baseline-delivery.log` and its JSON
  summary. Local loopback tests ran with sandbox elevation.
- `make test262-baseline`: **40261/40261**, **zero regressions**; release host
  retained as `lambda-delivery.exe` with SHA-256
  `ff22d5454e5c5bc98356ceb2df2a3e3806770ebcbbf9e03ed841dc820ab0898e`
  after the final DOM bridge repair (`test262-delivery.log`).
- The aggregate layout run planned before the external M59 harness/baseline
  update reports **4725 passed, 136 partial, 413 failed / 5274**. The original
  pinned base/current controls reproduced the same 413 failures. During final
  qualification the shared external corpus changed its layout harness and added
  a writing-modes baseline (15:49); neither is part of this change. A refreshed
  paired run loads that harness once and verifies its 22946 input-file metadata
  records stay unchanged across both binaries. All **3905 per-name statuses
  match**: text 893 passed / 87 partial, inline 169 passed, sizing 776 passed,
  writing modes 1669 passed / 291 partial / 20 failed. There are **zero source
  regressions** in this comparison. Evidence:
  `layout-controls-delivery.json`, its `-delta.json`, and
  `layout-controls-delivery-input-stability.json`. The aggregate layout gate remains
  red; the refreshed controls do not relabel the earlier aggregate snapshot.
- `make lint ARGS='--rule ^no-int-cast-radiant$'`: pass. Full lint remains red
  with 1434 pattern findings and structural findings elsewhere. No pattern
  finding lands on an added line (`lint-qualified-changed-lines.json`); this is not a
  whole-repository lint pass.

- `make build-debug-asan` and forced/poisoned-GC script qualification:
  **39/39** cases (27 positive + 12 negative over all three tiers), zero
  sanitizer errors and zero root-witness diagnostic rows. The instrumented
  host SHA-256 is
  `22209d47a30a7fac190b02f37466733042504ce7e0693fcc6883201f3c921080`;
  results: `asan-delivery.json`.

- The final namespace UI fixtures pass **109/109 assertions**, repeated three
  times, and also pass **109/109 under ASan**. The first aggregate found two
  regressions in this adapter. Recording public attributes hid private
  expanded-name metadata, and local Lambda tag names lost their QName at the
  DOM boundary. Internal identity reads now retain their independent storage;
  DOM construction restores QName spelling while retaining the local tag ID.
  `namespace-ui-controls.json` records three passing base runs and three
  failing intermediate runs per fixture; `namespace-ui-delivery.json` and
  `namespace-ui-asan-delivery.json` qualify the repaired behavior. No fixture assertions
  were removed or weakened.
- `make check-module-boundary`: all four strict DSOs link; the runtime Class-F
  import ratchet remains red for six imports. Their callers are `dom.o` and
  `dom_selection.o`; both sources, the build configuration, checker and ratchet
  baseline are byte-identical to HEAD (`module-boundary-source-control.json`,
  `module-boundary-callers.txt`). The final source reproduces the same six
  imports (`module-boundary-delivery.log`); no ratchet allowance was added.

## Final markup follow-up and aggregate limits

The captured full Radiant run is **8097 passed, 418 failed, 140 partial,
2 skipped / 8657**, including **744/744 UI cases**. Four markup failures were
subsequently repaired: selecting an XML fixture by its normalized local tag,
matching long qualified attributes before HTML case folding, and retaining the
XHTML intake’s normalized native tag names while preserving QNames in other
XML/formatting hosts. The existing three-vocabulary normalization predicate is
shared by the XML parser and DOM bridge; no new namespace policy was chosen.

After those repairs the full paged-media suite passes **504/504**, the DOM suite
passes **41/41**, and all **5 XHTML view/MIME/redirect cases pass three times**.
Both namespace UI fixtures pass **109/109 assertions three times** and also
**109/109 under ASan**. The remaining CSS memory failure is identical on the
pinned base and delivery builds over three runs each: jqueryui initial and
recascade CSS live bytes are **375038** versus the existing **373940** budget.
The memory budget is unchanged (`css-memory-controls.json`,
`css-memory-delivery.json`). The final stable-input layout comparison has the
same **20 failures on both binaries** under the concurrently updated harness.

The full Radiant aggregate was not repeated after this last narrow markup
follow-up. The final focused/native results above qualify those repairs;
aggregate layout, CSS memory, module-import and full-lint residuals remain
explicit. No full Radiant or whole-repository lint pass is claimed.


Formal Appendix A and the owning working docs reflect this scope. The formal-spec change is editorial (71.1.5); no ruling meaning changed. The fixed archive contains 267 actual anchors, including the three moved records.
