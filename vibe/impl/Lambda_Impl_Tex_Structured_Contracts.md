# TeX engine structured contracts

## Current checkpoint — 2026-11-17

User request: extend the reader refactor to the other TeX engine scripts.
Baseline: current worktree, saved under `temp/tex-structured/before/`; preserve all earlier changes.

Use concrete normal results and expected nullable absence (S11.4.2v3), normalize query absence at source boundaries (S7.10.7), and handle actual errors where recovery policy is known (S7.5.1v6, S7.6.1v9). Keep errors needed by callers visible; do not replace `any^` with a broad alias. Preserve source identity, snapshots, diagnostics and numerical behavior. Constrained return admission remains base-only (S11.4.6v4).

Installed: support modules, stream contracts, and the main expansion/digestion boundaries. Qualification is recorded below. Deeper macro and digestion helpers remain a separate unfinished portion of this migration.

## Installed changes

- `stream.ls`: named stream, token, level, trace, read-step, argument, raw-reader,
  source-tail and trace-result contracts. Expected TeX argument failure retains
  the advanced stream, partial tokens and cause. Dynamic reader admission is
  isolated in `read_source`; unexpected validation failures remain visible.
  Trace limit scanning uses a local loop. EOF retains the stream API's existing
  `{stream, token:null}` convention because callers need the final stream.
- `dimension.ls`, `glue.ls`: exact scaled-point and Glue/OrderedPart contracts;
  decimal accumulation is iterative. Unit lookup is nullable; arithmetic exposes
  a concrete successful type rather than `any`.
- `expl3.ls`, `fp.ls`: named macro and decimal/parser/evaluation results.
  Numeric input conversion handlers return parser diagnostics directly, before
  arithmetic. Query absence uses the existing reader text-span adapter.
- `pdfstrings.ls`, `md5.ls`: concrete byte/text/integer contracts and one shared
  hex-byte formatter. PDF unescaping retains its policy of ignoring non-hex
  input and zero-padding a final nibble.
- `reconstruct.ls`: named fragment, segment and reconstruction contracts;
  initialize the previous endpoint explicitly. Invalid UTF-8 spans retain their
  nullable canonicalization policy rather than producing arbitrary downstream text.
- `expansion.ls`: a common State contract; named numeric/glue/word/name/sign,
  conditional, stream-line and token-balance results. State transformations
  expose State rather than unrestricted values. Malformed package input is
  rejected before creating a configured stream, avoiding a nullable configured
  value carried past a guard.
- `digest.ls`, `profile.ls`, `primitives.ls`: named digestion/group/optional/parse
  results and concrete public/lookup outcomes. Partial parse data remains
  available with diagnostics. Kernel loading remains a handled input boundary.

This pass removes all `any^` return declarations from the support modules,
stream and profile (reader already had none), and reduces expansion from 334
to 124 and digest from 130 to 123. Remaining broad functions are explicitly
unfinished work: macro argument/signature records, expression variants,
resource/package lookup results, and the digestion source-mapping/AST helpers
need their own contracts. No blanket replacement with `map^` or a general
outcome alias has been used to claim those functions migrated.

Imported type bindings in record fields are recorded as a compiler issue in
the [Lambda ledger](../Lambda_Issue_Ledger.md#tex-imported-record-type).
Constrained error predicate admission remains governed by S11.4.6v4.

## Validation checkpoint

- The three new paired fixtures pass all 18 interpreter/JIT/AUTO checks with
  default and every-allocation poisoned GC. They cover partial argument
  recovery, optional lookahead replay, snapshots, exact dimensions, numeric
  diagnostics, binary/text escaping and checksums, configuration defaults,
  open-record extras/error payloads, and nullable-pointer union admission.
- The selected 27 TeX goldens pass interpreter and JIT on the pinned release
  host (54 checks). Earlier debug interpreter runs had five 300-second
  timeouts; release qualification supersedes those incomplete runs. These
  are correctness checks, not performance benchmark results.
- A final cleanup adds explicit propagation at 68 scanner/stream call sites
  before projecting their structured result fields. The full expansion
  golden, stream golden, numeric-math golden and reader-contract golden pass
  all three tiers afterward (12 checks); the 18 direct/GC checks also pass.
- Test262 is fully passing: 40,261/40,261.
- The final Lambda baseline reports 7,226/7,230, counting each aborted native
  suite as one failed suite. Its script suite is 2,447/2,448 (the unavailable
  WebRTC dependency), MIR emission is 595/595, and MIR GC stress is 368/368.
  Other failures are the editor assertion, the intermittent PDF allocation
  test abort and the wiki heap crash. The wiki crash also occurs in macOS
  crash reports from 18:35 and 18:43, before this task, with the same allocator
  failure family. The preceding storage-only gate passed all 340 native
  diagnostic tests; no full native pass is claimed for the final gate.
- The formal index and scoped whitespace checks pass. No native/reference
  fixture tolerance was relaxed.

Evidence directory: `temp/tex-structured/`. Key artifacts are
`final-direct-payload.json`, `final-acknowledgement.json`,
`release-{interp,jit}-final/`, `lambda-baseline-all-final.log`,
`lambda-summary-all-final.json`, and `test262-baseline.log`. The release
host is pinned as `lambda-release-final`, SHA256
`b4fb94da7ace4d873efb6ec845ae65dc8069de14cac7292a94d25fa6c051a7d6`.
`final-source-hashes.json` records the final source identity.

Public option inputs retain their existing non-map-to-default policy. The
shared `options_map` uses `match` and its narrowed `~` (S11.2.1) to produce a
clean map; it does not add casts or swallow an incoming error parameter
(S11.4.3). Byte-oriented checksum/escaping functions retain binary input.

## Document load follow-up — 2026-11-27

The host previously restarted document expansion after every captured TeX
resource lookup. The Script engine can now pause before the requesting step.
The host retains that checkpoint with precise GC roots, authorizes and captures
the resource, then retries the step with the new capture (D10.2.2v2, D6.3.3).
The checkpoint preserves the source, groups, diagnostics and expansion budget.
Selecting an expl3 kernel still starts the document with that kernel's state.
The resource request golden compares resumed output with a fresh parse.

Document preloads omit definition source spans that the handoff discards.
The scanner keeps the original UTF-8 byte positions. Primitive dispatch
reuses resolved control meanings. LaTeX callers that request elements skip
the diagnostic report, which otherwise re-parses aligned cells after rendering.
The amsmath golden checks that both entry points produce identical HTML.
Alignment body scans use text subscripts and a short delimiter prefix.
Native text slicing reuses the indexed ASCII character table for single-byte
results without changing code-point positions or slice bounds (S2.5.8,
S7.2.1, S7.10.1v3).
The reader publishes cursor and token metadata once per emitted token.
Control words scan ordinary letters directly and decode superscript pairs
when their category can change the word, retaining the physical byte span.
The expander collects consecutive ordinary characters before appending to its
output. It publishes the reader cursor through a local value snapshot
(S9.1.2). Before the next control or group token executes, it sees the same
emitted tokens and reader position as token-by-token stepping (D10.2.2v2).
Host resource retries retain the pre-step checkpoint.

## Record admission defect exposed by this migration

A sequence comparator stopped returning its chosen result after `step` acquired
its State return contract. The reduced case is a named open record `{core:int}`
receiving `{*:value,extra:"same"}` when `value.extra` was initially null.
Ordinary map construction retained `"same"`; admission changed the extra value
back to null in both interpreter and JIT. Adding the extra field to the target
schema avoided the symptom but was rejected as a package workaround.

The relayout copy wrote an Item-backed constructor null slot through the
TypeId-level `map_field_store(..., LMD_TYPE_NULL)`, which writes null. The
physical descriptor is `LANE_STORAGE_ITEM`: its live value must be preserved
as a complete Item, even when its construction recipe is null. A shared
`map_shape_store_value` handles native nullable lanes, Item lanes and ordinary
lanes, and is used by admission relayout and mutable deep-copy field storage.
The shared field store also zeroed shaped error fields, losing their message
payload. It now copies the complete error Item under S7.4.4. The regression
checks both the admitted error message and preservation of the original map.
The root cause is in Lambda runtime, governed by D3.4.6/D4.2.5 and
S11.4.6v4. Regression: `test/lambda/open_record_extra_overwrite.{ls,txt}`.

## Nullable pointer admission defect exposed by binary contracts

The checksum/byte-escaping input contracts preserve `string | binary`:
file checksums must accept captured binary resources, not just text. That
contract exposed a MIR nullable-pointer admission error. Its present-value
shortcut applied the destination union type before boxing, so a binary scalar
pointer crossed as an untagged container pointer and could be observed as
bool. The fix preserves the producer's proven pointer base until transport
conversion, then assigns the destination contract to the joined result
(D2.3.5, S11.4.6v4). Null still takes ordinary admission and recovery.

Regression: `nullable_pointer_union_admission.{ls,txt}`. The direct structured
fixture now includes binary MD5 and UTF-8-byte escaping, in addition to the
text cases. Evidence: `binary-union.mir`, `binary-dump.log`, and
`final-direct-payload.json`. No package fallback or binary-input restriction
was retained to avoid this compiler path.
