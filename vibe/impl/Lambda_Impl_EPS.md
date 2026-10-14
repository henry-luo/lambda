# Lambda EPS implementation record

**Date:** 2026-10-14. **Status:** vector, base-font text and legacy raster-image
profiles implemented; embedded font programs and full LanguageLevel remain open.

Design: [Lambda_Pkg_EPS.md](../Lambda_Pkg_EPS.md). This implementation follows
**D7.2.4** (shipped document packages), **D4.1.3** (Input-owned parsing),
**S1.4** (explicit guest identity), **S12.1.2** (procedural execution loop),
**S7.4.2** (raised conversion failures), and **S7.6.7v4** (procedure statement
handlers). No formal language ruling changes.

## Delivered behavior

- `input(path, 'eps')`, auto detection and `parse(source, 'eps')` produce the
  versioned EPS document schema. Plain EPS and DOS EPS wrappers are accepted.
  Bounds/checksum checks, nested DSC scopes, counted data blocks, trailer
  bounding boxes and original source bytes are retained.
- `lambda.doc.eps.eps` exports `eps_metadata`, `interpret`, `display_to_svg`,
  `eps_to_svg` and `supported_operators`. Interpretation/conversion are `pn`;
  SVG emission and metadata access are `fn`.
- Guest stacks, composite IDs and interval descriptors preserve sharing
  independently of Lambda COW. Procedures, dictionary lookup, immediate names,
  binding, evaluated array/dictionary construction, loops and operator dispatch
  run in Lambda Script.
- Local `save`/`restore` rewinds non-string composites and graphics state,
  preserves string bytes and emitted paint, and rejects live discarded objects.
- Vector painting supports paths/cubic arcs, RGB/gray/CMYK, line styles,
  hairlines, clipping, saved paths, matrix operations and explicit transforms.
  Path points remain fixed in EPS space while strokes use the paint-time CTM.
- Standard 14 font dictionaries support lookup, scaling/matrix derivation and
  selection, supplemented by four Palatino variants. Graphics and local VM
  saves preserve the selected font and cache. Printable ASCII StandardEncoding
  supports `show`, `stringwidth`, `ashow`, `widthshow` and `awidthshow`.
- In-memory files, byte/hex/line reads, `token` and ASCIIHex/ASCII85/RunLength
  filters share the demand scanner's source cursor. Legacy `image`/`colorimage`
  accept strings, files or procedure callbacks with packed/interleaved or planar
  gray/RGB/CMYK samples at 1/2/4/8 bits, and emit embedded PNG images.
- Standalone SVG preserves draw order, normalizes the bounding-box origin,
  flips y, clips the viewport and retains point dimensions. Optional scale,
  background and resource ID prefixes are explicit.
- CLI `convert -t svg`, `render`, `layout` and `view` use the existing document
  transform machinery. The host invokes the procedural package export directly;
  no pure Lambda wrapper calls a procedure (**S12.1.1v2**).

## Source and reuse

| Source | Responsibility |
|---|---|
| `lambda/input/input-eps.cpp` | EPS envelope, DSC metadata, source ownership and detection. |
| `lambda/input/input-postscript.hpp/.cpp` | Demand scanner and shared PDF/PostScript literal-string reader. |
| `lambda/input/input-postscript-data.cpp` | Bounded byte reads, filter adapters and packed/planar pixel transport. |
| `lambda/input/pdf_decompress.h/.cpp` | Shared bounded ASCIIHex/ASCII85/RunLength codecs, with explicit consumed lengths and strictness. |
| `lambda/input/input-pdf-image.hpp` | Shared PDF PNG/data-URI encoding primitive. |
| `lambda/runtime/postscript_runtime.cpp` | `ps_scan`/`ps_read`/`ps_png` Item bridges into the context-owned Input; no native evaluator. |
| `lmd/package/doc/eps/objects.ls` | Explicit object store, permissions, references and operand helpers. |
| `lmd/package/doc/eps/interp.ls`, `operators.ls` | Execution frames, budgets, operator signatures and dispatch. |
| `lmd/package/doc/eps/graphics.ls`, `matrices.ls`, `vm.ls` | Guest graphics, transforms and local VM saves. |
| `lmd/package/doc/eps/fonts.ls` | Standard 14 font identities, dictionaries, transformed matrices and selection. |
| `lmd/package/doc/eps/text.ls`, `files.ls`, `images.ls` | Glyph advances, guest file cursors, filter dispatch and resumable image callbacks. |
| `lmd/package/doc/eps/svg.ls`, `eps.ls` | SVG output and public exports. |

PDF retains its public token schema. Its content and dictionary literal-string
readers now share the bounded escape reader. Dictionary hex pairs can span
whitespace through the common decoder. PDF's existing ASCIIHex filter recovery
policy remains permissive; PostScript scanning selects strict decoding.

Sharing ASCII85 exposed a pre-existing allocation defect: one `z` produces four
bytes, so allocating from a fixed 4/5 encoded-size estimate was insufficient.
The shared decoder now grows under an explicit decoded-byte cap, rejects
overflowing groups and stops at the actual stream terminator. Codec wrappers
share their output ownership helper.

PDF's path emitter is exported as `paint` and reused by EPS. Existing PDF
numeric, color, SVG and matrix helpers are imported; PDF resource lookup and
page interpretation remain separate.

SVG document loading also exposed a URL ownership defect: the serialized SVG
branch returned a new HTML document while leaving the original source URL
unowned. It now uses the ordinary HTML loader with that URL and caller pool,
preserving relative asset resolution and the loader's teardown contract.

## Initial limits and unsupported forms

Native EPS input has a 64 MiB source ceiling. Conversion limits are supplied
through `options.limits`; defaults are policy ceilings, not performance claims:

| Limit | Default |
|---|---:|
| `max_steps` | 10000000 |
| `max_stack` | 8192 |
| `max_execution_depth` | 1024 |
| `max_dictionary_depth`, `max_graphics_depth`, `max_clip_depth` | 128 |
| `max_objects` | 16384 |
| `max_composite_length`, `max_token_bytes` | 1048576 |
| `max_procedure_depth` | 128; native scanner ceiling 256 |
| `max_scan_objects` | 65536 per scan request |
| `max_path_segments` | 100000 |
| `max_draws` | 50000 |
| `max_image_pixels` | 16777216 |
| `max_decoded_bytes` | 67108864 cumulative reads/filter outputs |

Dictionary keys are names in this profile; boolean logical forms are supported.
`rect*` operators support the four-number form. Non-ASCII StandardEncoding,
embedded/custom font programs, reencoding, image dictionaries, filters other
than the three listed above, external file access, `stopped`,
global VM, arbitrary executable strings, PostScript
binary object encodings, printer/device control and vendor extensions fail if
executed. There is no producer-specific substitution or external renderer
fallback. Unsupported code in an unexecuted procedure can remain unused.

Integer objects use a signed 32-bit guest range and overflowing arithmetic
promotes to binary64 real objects; real arithmetic uses binary64. This is a
declared profile choice, not a change to Lambda numerics. Native scans validate
numeric syntax rather than accepting Lambda's boolean/null coercions.

Provide a distinct `id_prefix` when combining independent EPS fragments in one
HTML document. Transparent SVG is the default; document/raster backgrounds
follow the existing renderer. Base-font advances use Radiant's installed font
metrics and can vary by platform; this profile does not ship canonical
PostScript AFM metrics or embedded font outlines.

## Verification

Native tests live in `test/test_input_eps_gtest.cpp`; a PDF regression verifies
whitespace-separated hex pairs. Script cases live in `test/lambda/eps/`, each
with a `.txt` golden, registered as procedural tests.

The focused checks cover envelope failures, DOS sections, byte strings and
encodings, scan limits/boundaries, guest aliasing, name binding, loops, failure
diagnostics, graphics transforms/clips, local VM restoration and portable SVG.

| Check | Result |
|---|---|
| EPS native parser/scanner/codec tests | 9/9 passed. |
| PDF parser regressions | 26/26 passed. |
| EPS script goldens | 8/8 passed under auto and under JIT with forced collection and freed-memory poisoning. |
| Build and CLI | `make build` passed; fixture and unknown-extension input converted to standalone SVG; direct EPS layout and PNG rendering passed. Debug allocation tracking reported no leaks. |
| Radiant dimension lint | `no-int-cast-radiant` passed. |

The full Lambda gate initially encountered concurrent removal of the shared
`lambda.exe`. Its runner was replayed from `temp/eps-baseline/` with a stable
executable and the normal fixture tree. That run passed 6553 checks and reported
two failures: `edit_view_only`'s math round-trip check and the
`regression_music_layout` GC-stress timeout. EPS cases passed. The original
test harnesses and goldens for those failures were not changed.

The Radiant baseline reported failures in page snapshots, layout fixtures and
view tests. Its Scene3D suite repeatedly hit six-minute idle timeouts; this
verification run was stopped after 31 minutes. The broad gates therefore are
not clean; their results do not establish full-repository regression safety.

Current T0 activation rejects the package import graph with `AST_NODE_IMPORT`;
auto/JIT are the supported execution tiers for this delivered package. No
performance claims were made from the debug verification builds.

### Viewer admission correction

The first implementation registered EPS in the CLI and common document router,
but missed the viewer's earlier format detector. Consequently, direct
`view artwork.eps` rejected the file before invoking its transform. The viewer
now reuses `eps_source_expected` for `.eps`/`.epsi` admission and identifies EPS
in its window title and supported-format diagnostic.

`RadiantViewTest.LoadsEpsAsHeadlessView` failed with exit 255 before the rebuild
and passes afterward, including zero retained allocations. Adjacent PDF/SVG
viewer tests pass; a mixed-case `.EPSI` copy also loads cleanly. The sample was
checked through the actual GUI viewer with `LAMBDA_AUTO_CLOSE=1` and exited 0.
`make build` and the Radiant dimension lint pass for this correction.

### Producer dictionary correction

The user-supplied `test/input/sample-1.eps` (553417 bytes, Cairo 1.15.12)
exposed a guest `dict` allocation defect. Its empty-map branch was written
`if (...) {}`, which is an empty procedural block and yields null under
**S16.4.2v2**. Creating the empty map in initializer position restores the
dictionary's backing value; `begin`, `def`, bound aliases and dictionary
shadowing then use the ordinary object-store implementation.

This vector artwork completes with 106452 execution steps, 51 guest objects
and 188 draws. The default step budget is now 250000, with caller-specified
lower ceilings still enforced. This policy change applies to every input;
there is no producer or filename dispatch. Text painting remains unsupported.

The independently authored `test/input/eps/dictionary-prolog.eps` fixture
covers empty dictionaries, dictionary-stack shadowing and bound drawing
aliases. Its script case has a `.txt` golden and its headless viewer case
checks clean teardown. The user-provided artwork stays outside the authored
regression corpus.

Verification of this correction passed all seven EPS script cases under auto
and JIT with forced GC, plus four EPS/PDF/SVG headless viewer cases. The full
user artwork loads through both headless and GUI viewers and converts to SVG
with all 188 draws and 10666 cubic curves. The package modules load from source,
so this correction does not require rebuilding the native executable.

### Standard 14 font selection

The supplied `test/input/eps/knightstour.eps` selects Times-Roman at size 72
before drawing vector paths; it never calls a text-painting operator. Its first
failure was the missing `findfont` operator. The package now implements
`findfont`, `scalefont`, `makefont`, `setfont`, `currentfont` and `selectfont`.

The PDF package exports `is_standard14` beside its existing `standard14`
mapping; EPS uses both. Font dictionaries expose `FontType`, `FontName`,
`FontMatrix` and an opaque `FID` guest object. These objects are readonly and
use the existing explicit ID store (**S1.4**). Derived fonts share the base
identity and replace their matrix; the base matrix remains unchanged, following
the [PostScript Language Reference, third edition, `scalefont`/`makefont`](https://www.adobe.com/jp/print/postscript/pdfs/PLRM.pdf).
`currentfont` initially selects Courier in this profile. The name cache belongs
to each interpreter activation (**S9.1.7**), and local VM saves rewind it with
the heap. Selection is part of the graphics state and follows graphics saves.

Unknown font names and invalid dictionaries raise `invalidfont`; discarded
font identities are rejected by `restore`. Glyph painting, metrics, reencoding
and embedded font resources remain unsupported. In particular, adding setup
support does not make `show` succeed or drop its content.

The authored `font-selection.eps` fixture exercises Times-Roman setup around
saved vector draws. Its `fonts.ls` golden checks all 14 names, 31 positive
font/state invariants and seven failure boundaries. Shared test document/error
helpers live in `mod_helpers.ls`, excluded from test discovery as a module.
Verification passed 96 EPS/PDF script cases, all eight EPS cases under JIT
with forced GC and freed-memory poisoning, and five EPS/PDF/SVG viewer cases
with clean teardown for the EPS fixtures. The supplied artwork also passed the
headless and actual GUI viewers with automatic close enabled.
SVG conversion preserves the supplied artwork's 401 painted paths.

### Complete local EPS directory

The broader local corpus exposed four missing paths: Palatino text in
`circle.eps`, palette/RGB callbacks in the two ImageMagick logos, and planar
ASCII85/RunLength callbacks in the GIMP logo. All execution stays in Lambda
Script; the native additions only expose existing bounded codecs, byte reads
and the shared PDF PNG encoder (**D4.1.3**, **D7.4.6**).

Three defects required general fixes. A bare token must consume its single
whitespace terminator (CRLF counts once) before a callback reads inline data.
The interpreter and `currentfile` now share one explicit guest file cursor;
`restore` preserves that cursor alongside string bytes. Finally, operand and
array storage use tagged integer/real guest records with separate payload
fields, so host numeric widening cannot change a PostScript integer into a
real (**S1.6**). This fixes palette-size multiplication and integer loop values
without changing Lambda numerics.

Image frames retain IDs and counters, with growing buffers stored once in the
activation. Planar callbacks advance the least-filled channel, preserving the
row/channel ordering of inline streams. Filter adapters stop at their exact
EOD markers and enforce cumulative decoded-byte limits. PNG packing validates
dimensions, row padding, component counts and available source bytes before
encoding. The 10-million-step default covers the image callbacks; one RGB
logo executes about 4.53 million VM steps. This is a policy ceiling, not a
debug-build performance claim.

Text measures each glyph with Radiant at 1000 units using the same family,
weight and style as SVG. Explicit per-glyph origins prevent kerning or
ligatures from changing PostScript advances. FontMatrix and currentpoint
determine placement; `stringwidth` preserves currentpoint. The shared PDF
`text_run` accepts optional attributes without changing existing callers.

Authored fixtures `text-graphics.eps`, `raster-hex.eps` and `raster-planar.eps`
cover text with vector paint, cursor resumption after hex pixels, and planar
filtered RGB. Their script goldens also cover malformed/empty sources, limits,
numeric tags and text errors. The seven supplied third-party artworks are
tested locally and remain outside the authored fixture commit.

Verification on 2026-10-14:

| Check | Result |
|---|---|
| Local directory | 13/13 EPS files passed headless view and 13/13 passed actual GUI view with automatic close. |
| EPS native tests | 13/13, including source separators, exact reads/EOD, filter failures, planar pixel colors and padded rows. |
| PDF native tests | 26/26. |
| EPS/PDF selected scripts and controls | 105/105; all 11 EPS goldens also passed JIT with forced GC and freed-memory poisoning. |
| Viewer regression cases | 8/8 EPS/PDF/SVG cases passed; all six authored EPS fixtures check allocation-free teardown. |
| Visual inspection | Three decoded logo PNGs preserve colors/orientation; the authored text/vector PNG shows upright text, circles and a line. |
| Build | `make build` passed and rebuilt the checkout executable. |
| Full Lambda baseline | 6567/6569 passed, including all 2112 input-parser checks. The same `edit_view_only` failure and `regression_music_layout` GC-stress timeout remain; the gate is not clean. |

`python3 utils/test_eps_corpus.py` discovers every `.eps`/`.epsi` below
`test/input/eps`, loads each via the viewer and fails on errors or timeouts.
Use `--gui` for native windows; `--executable` selects a stable host during
concurrent builds. Reports and isolated logs stay under `temp/eps-corpus-*`.
