# Radiant ledger implementation — 2026-11-11

This bounded delivery follows **D9.1.1v4 / D9.1.2v2 / D9.1.3v2**. Native
layout, paint and state remain Radiant-owned; resources use the existing input
service and image cache. No Lambda or lambda-input implementation is changed.
Document-owned borrows and retained resource validation preserve **D5.5**.

## Delivered core behavior

### Horizontal flex baseline groups — RAD03-1 / RAD04-1 / RAD08-5

The resolver previously discarded `first`/`last` from container `align-items`;
the flex passes also measured every participant from its first baseline.
Container and self-alignment now preserve the compound value, use the selected
laid-out line baseline, and keep first/last sharing groups independent. Auto
cross sizes and wrapped-line distribution are recomputed after content layout,
before groups are anchored to the appropriate line edge. Cross-axis auto
margins exclude an item from baseline participation.

Native acceptance covers first/last groups, auto/fixed heights, mixed self
alignment, row reversal, retained last-to-first changes, wrapped lines,
`align-content:stretch`, auto margins and explicit start overrides. Local
Chrome independently confirms the wrapped geometry in
`temp/ledger-impl/flex-wrap-chrome.json`.

The aggregate exposed a regression in `align_baseline_nested_column`: the new
lookup had trusted a nested flex export even without a sharing group, replacing
the required first-child fallback. The repaired lookup keeps that distinction;
the reduced native control and exact corpus case match fresh local Chrome at
the original 30px offset. The first failed aggregate remains in
`temp/ledger-impl-radiant-baseline-before-nested.log`; no reference was changed.

The broader ledger family remains PARTIAL: orthogonal baselines, spanning
grid items and exhaustive nested-container baseline export remain unqualified.

### Video posters — RAD22-6

Poster admission no longer depends on successful player/source creation.
Posters use ordinary image paint and its object-fit, clipping, retained resource
validation and vector export path. Replacing/removing the attribute refreshes
the borrowed cache surface. A placeholder records only whether poster pixels
were painted; it does not add a borrowed poster pointer to the display list.
Ready/idle media retains its poster. Playback with an available decoded frame
supersedes even a retained pre-play placeholder; cached playback follows the
same state distinction. An absent/broken poster retains the existing fallback.

Native pixels cover a poster without a video source, `preload:none`, contain
letterboxing, cover sizing with ancestor clipping, replacement, removal and
failed image loading. The macOS decoder control verifies frame precedence in
the actual post-composite path. Decoder execution requires platform access;
the sandboxed audio-decoder failure and successful native rerun are retained.

### Layout whitespace cache — RAD06-6

The active text layout, wrapping and lookahead queries share one pass-local
parent result. The cache expires on style/mutation epochs, layout/measurement
generations, owner/view/property changes and style resolution. Scratch nodes,
query probes and alternate views retain the uncached path. Existing specified
inline values and UA defaults remain authoritative. The retained-mutation
control checks preformatted, normal, overridden and UA-preformatted content.

This is a bounded hot-path cache, without a performance claim. Context-free
paint/query helpers retain their resolver; the historical collapsed-whitespace
node-bounds observation remains unqualified.

### Owner-local decomposition — RAD02-1 / RAD17-1

`css_alignment.cpp` and `css_alignment.hpp` now own container/self alignment
resolution and the shared stores. `state_batch.cpp` owns batching and the
selection/dirty synchronization shared through `state_store_internal.hpp`.
These are extractions along existing seams; remaining monolith decomposition
and deferred callback behavior stay open. The existing owner manifest's
`radiant/*.cpp` pattern discovers all new modules; generated build files are
not edited.

## Verification

`make -j8 run-radiant-baseline` passes **891 native tests plus one static corpus
smoke test**, with one existing hardware MIDI skip; **7/7 runner contracts**;
**3,851 static and 630 dynamic geometry comparisons**, with six existing static
fixture skips and no failures. Focused layout tests pass **49/49**, display-list
tests **106/106**, and state tests **38/38**. The native poster playback control
passes with platform decoder access.
`make lint ARGS='--rule ^no-int-cast-radiant$'` checks **89 casts with zero unmarked**;
`make test-lint` passes **5/5**. Input-facade and whitespace checks pass.

The final aggregate and artifact receipt is
[verification summary](../../radiant/temp/ledger-impl-verification-summary.json). It pins
the standalone host and changed source/header/test hashes. Focused receipts include the
original failing flex tests, native poster pixels, retained-poster failure and
repair, display-list/PaintIR parity, state tests and whitespace mutation checks.
The CLI poster reproducer passes in PNG and SVG; its raster image and rasterized
SVG preview were visually inspected. Only ledger/implementation documentation
changed after the passing aggregate. No reference, tolerance or fixture corpus
was changed; broader qualification limits above remain explicit.
