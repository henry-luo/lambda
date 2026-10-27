# PPTX import fixtures

Run `python3 test/input/pptx/generate.py` to reproduce the synthetic packages.
The standard-library generator fixes ZIP timestamps and ordering; it needs no
Office installation and writes no scratch files.

`minimal`, `relocated`, `strict` and `alternate-prefix` check the public API and
relationship/namespace identity. `features` checks authored slide order distinct
from filenames, shared layout/master/theme parts, placeholder geometry and
typography, theme backgrounds, Unicode, direct formatting, a raster, a table,
group scaling, core metadata and speaker notes. `formatting` checks numbered
list starts, nested continuation and restarts, safe/unsafe hyperlinks and a
zero-width vertical line whose stroke supplies its paint bounds.

`paint` checks opposite-winding freeform holes, cubic/quadratic paths, an empty
shape text body, sorted linear/radial gradient stops, alpha, stretched/cropped
picture fills and duotone pixels with source transparency. `unsupported-paint`
keeps warnings for unknown path commands, unsupported gradient profiles and
unimplemented image effects. `warnings` repeats master decorations across six
slides to check grouping, occurrence counts and bounded disclosure behavior.

`unsupported` keeps usable text while diagnosing a shape, graphic frame,
transition and animation. `external-image` verifies that external media are
not fetched. Error fixtures cover missing/duplicate slide relationships,
package escapes, invalid sizes, macro-enabled main parts, malformed XML, DTDs
and missing/wrong parts. Fundamental failures use public error codes; ordinary
unsupported content defaults to warnings and can be made strict by the caller.

`reference/` contains six unchanged fixtures from
[python-pptx](https://github.com/scanny/python-pptx), pinned in `manifest.json`
with source URLs, byte counts and SHA-256 hashes. Its MIT license is included.
They exercise ordinary title/subtitle placeholders, inheritance, font flags and
languages, notes, pictures, tables and merged-cell warnings. `oracle.json`
independently extracts slide counts, dimensions and every saved slide `a:t`
using Python's XML/ZIP readers. Run `python3 test/input/pptx/reference/verify.py`
to verify the hashes and regenerate/check that oracle. Tests compare all
non-whitespace source text with import output; unsupported features remain
diagnosed rather than being rebaselined away.

Every functional regression in `test/lambda/pptx/` has a paired `.txt` and is
registered in the Lambda baseline. Native UI replays cover player navigation,
editor import/edit/save/reopen, recoverable warnings, fatal-error preservation,
cancellation and document-browser tab retention. See
[the implementation profile](../../../vibe/Lambda_Pkg_Pptx.md).
