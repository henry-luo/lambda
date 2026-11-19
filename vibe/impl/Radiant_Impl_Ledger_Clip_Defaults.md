# Continued Radiant ledger repairs — 2026-11-11

Authority: **D9.1.1v4 / D9.1.3v2**. These repairs remain in native Radiant.
View-owned scalar properties and scoped paint data preserve **D5.5**; no
Lambda or lambda-input implementation, header, test or build file is changed.

## RAD02-3 — legacy rectangle clipping

The property dispatcher explicitly ignored `clip`. A reduced positioned HTML
box therefore painted and accepted hits outside `rect(...)`, including when
its descendant was lifted into a signed stacking layer. The failing-before
native case is preserved in `temp/ledger-next-clip-before.{json,log}` and
`temp/ledger-next-clip-before.png`.

The native resolver now computes the four lengths through the existing length
resolver and retains an auto-edge mask in `PositionProp`. The used-geometry
snapshot includes this property, so cached style restoration and intrinsic
measurement do not drop it. Inheritance copies computed lengths; auto edges
use the final border box. The shared geometry helper applies only to absolute
and fixed boxes, following [CSS 2.2 §11.1.2](https://www.w3.org/TR/CSS22/visufx.html#clipping).

Raster scopes, the shared vector walker, stacking-ancestor scopes and native
HTML hit testing consume that geometry. The clip surrounds the owner's paint,
effects and descendants and intersects an existing `clip-path` independently.
It does not turn the element into a scroll container. Empty/inverted rectangles
retain an empty paint clip instead of dropping the operation.

Two native regressions cover comma/space notation, absolute/fixed applicability,
relative non-applicability, auto/negative/empty edges, descendant paint and hits,
clip-path paint intersection, retained changes, font-relative calculated lengths,
variables, explicit inheritance and `unset`. Six PNG and six SVG states match
fresh local Chrome pixel samples exactly; the native PNG and Chrome reference
were visually inspected. Browser samples and hit targets are retained in
`temp/ledger-next-clip-browser.json`; generated PNG/SVG files use
`temp/ledger-next-clip-<phase>.*`.

This closes the ignored native HTML rectangle report. Exhaustive clip CSSOM
reflection/animation, SVG-specific picking, existing `clip-path` picking and
fragmented/paged contexts are not qualified by these controls.

## RAD03-3 / RAD04-3 — replaced defaults

The existing `layout_replaced_default_size` provider already served measurement,
but inline SVG, HTML hints, intrinsic-image sizing, SVG metadata and raster
picture fallback still repeated the same literals. Native default width/height
and audio height now have one definition in `view.hpp`; those consumers share
the constants or the existing tag-based provider. The values and the algorithms
selecting natural size, preferred ratio, available-space constraints and HTML
attributes are unchanged. Full static/dynamic geometry and native sizing tests
qualify this mechanical consolidation.

## RAD16-5 — frame-clock control flow

Removed the shared trailing store/return from `radiant_frame_clock_start`.
The macOS mode guard's defensive rejection remains inside its platform branch;
every branch now returns its result explicitly. The final source review caught
that the historical claim of unconditional unreachability was too broad, and
preserved this rejection before final qualification. Existing native animation
and scheduling suites provide regression coverage; no platform policy or clock
abstraction is changed.

## Qualification

The full `make -j8 run-radiant-baseline test-native-closure` result and pinned
source/artifact hashes are recorded in
[the verification receipt](../../radiant/temp/ledger-next-verification-summary.json).
The final gate passes **893 native tests plus one static corpus smoke test**,
**7 runner contracts**, **3,851 static and 630 dynamic geometry comparisons**,
and native archive closure. It retains one hardware MIDI skip and six static
fixture skips. The dynamic layout suite passes **51/51** including the two
new clip regressions; no test or geometry comparison fails.
Layout integer-cast lint checks 89 casts with zero unmarked; its five contracts,
the input-facade check and `git diff --check` pass. References, fixture corpora
and tolerances are unchanged. No performance claim is made.
