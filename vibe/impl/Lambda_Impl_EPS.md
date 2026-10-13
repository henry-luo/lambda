# Lambda EPS implementation record

**Date:** 2026-10-14. **Status:** initial vector profile implemented; broader
text/image/font profiles remain open.

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
| `lambda/input/pdf_decompress.h/.cpp` | Shared bounded ASCIIHex/ASCII85 codecs, with explicit consumed lengths and strictness. |
| `lambda/runtime/postscript_runtime.cpp` | `ps_scan` Item bridge into the context-owned Input; no native evaluator. |
| `lmd/package/doc/eps/objects.ls` | Explicit object store, permissions, references and operand helpers. |
| `lmd/package/doc/eps/interp.ls`, `operators.ls` | Execution frames, budgets, operator signatures and dispatch. |
| `lmd/package/doc/eps/graphics.ls`, `matrices.ls`, `vm.ls` | Guest graphics, transforms and local VM saves. |
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
| `max_steps` | 100000 |
| `max_stack` | 8192 |
| `max_execution_depth` | 1024 |
| `max_dictionary_depth`, `max_graphics_depth`, `max_clip_depth` | 128 |
| `max_objects` | 16384 |
| `max_composite_length`, `max_token_bytes` | 1048576 |
| `max_procedure_depth` | 128; native scanner ceiling 256 |
| `max_scan_objects` | 65536 per scan request |
| `max_path_segments` | 100000 |
| `max_draws` | 50000 |

Dictionary keys are names in this profile; boolean logical forms are supported.
`rect*` operators support the four-number form. Fonts/text, images, file/filter
operators, `stopped`, global VM, arbitrary executable strings, PostScript
binary object encodings, printer/device control and vendor extensions fail if
executed. There is no producer-specific substitution or external renderer
fallback. Unsupported code in an unexecuted procedure can remain unused.

Integer objects use a signed 32-bit guest range and overflowing arithmetic
promotes to binary64 real objects; real arithmetic uses binary64. This is a
declared profile choice, not a change to Lambda numerics. Native scans validate
numeric syntax rather than accepting Lambda's boolean/null coercions.

Provide a distinct `id_prefix` when combining independent EPS fragments in one
HTML document. Transparent SVG is the default; document/raster backgrounds
follow the existing renderer. Base-font and image support remain separate
delivery gates, requiring metrics/codec and visual-reference fixtures.

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
| EPS script goldens | 6/6 passed under auto and JIT; 6/6 under JIT with forced collection and freed-memory poisoning. |
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
