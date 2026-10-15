# Image I/O utility consolidation

Date: 2026-10-15
Status: implemented and verified on macOS ARM64; binary measurements below use
the release CLI with ThinLTO.

Design: [Common image library](../Lib_Image.md). This analysis covers the common
core, all eight raster format adapters, their imported codecs, and the native
release source selection in `build_lambda_config.json`. Direct adaptation of
the imported image sources follows the source policy approved for this work.

Spec linkage: **D7.1.1**, **D7.1.2v2**, and **D7.1.3** keep context-free codec
mechanisms in `lib` and resource acquisition in `lambda-io`; **D4.1.4v5** retains
tracked native allocation; **S7.4.3–S7.4.4** retain fault/error classification.
The runtime-only CLI remains the profile specified by **D7.1.6**. No formal
ruling, public API, format capability, or build-source selection changes.

## Findings

The useful duplication is concentrated in pixel preparation and adapter glue.
The imported compression implementations contain similar-looking operations,
but most have different format rules or CPU-specific requirements. Sharing
those algorithms would be a substantially larger project with uncertain size
and performance benefits.

The macOS ARM64 codec target selects 562 translation units. The following
inventory counts nonblank, noncomment lines in their selected source files,
before preprocessing. It excludes included headers, does not expand included
implementation files, and does not estimate which functions survive linking.
It is a source-composition inventory, not executable LOC.

| Provider | Selected files | Source code lines after consolidation |
|---|---:|---:|
| libaom | 283 | 205,941 |
| libtiff | 37 | 33,342 |
| libwebp | 75 | 29,441 |
| libpng | 18 | 20,856 |
| libjpeg-turbo | 117 | 19,962 |
| libavif | 26 | 17,990 |
| giflib | 4 | 1,268 |
| libnsbmp | 1 | 930 |
| Generated AOM build configuration | 1 | 2 |
| Total | 562 | 329,732 |

AV1 accounts for about 62% of this source inventory. Adapter consolidation
therefore reduces maintenance duplication but cannot produce a large reduction
in the entire release source base. The existing build already omits unused
applications, backends, converters, and codec entry points.

## Consolidations implemented

### Pixel rows and owned pixel preparation

`image_rgb_row` provides grayscale expansion, RGB/BGR ordering, and explicit
alpha compositing. `image_prepare_pixels` borrows compatible input views or
allocates one checked, operation-owned conversion buffer. Conversion respects
the source stride and never writes to the caller's pixels. Full-view conversion
checks cancellation per row and uses the existing scratch and process-budget
admission path.

Consumers:

- JPEG uses the prepared RGB/gray view, including RGBA flattening.
- WebP and AVIF share grayscale-to-RGB expansion.
- BMP uses the row helper for its bottom-up BGR rows, retaining a single padded
  output row rather than allocating a complete RGB image.
- The common encoder uses the preparation helper for straight-alpha copies,
  delegating unpremultiplication to the existing RGBA row utility.

Two arithmetic details remain deliberate. JPEG composites premultiplied samples
directly against the background. Other affected encoders retain the existing
straight-alpha canonicalization first; BMP subsequently flattens that view.
Combining those rounding steps would change some output bytes. PNG continues
to unpremultiply through its streaming row encoder.

The row utility has a narrow RGB output contract. An initial generalized
channel-conversion version increased optimized code size; removing unused
conversion modes retained sharing without that expansion.

### Orientation

One immutable eight-entry `int8_t` matrix table describes EXIF orientations.
The common pixel normalizer and AVIF's composition of `irot`/`imir` use the same
table. The normalizer computes a destination row origin and signed pixel step,
avoiding a separate eight-case switch and repeated coordinate transforms in
the inner loop. Intrinsic/display dimensions and thumbnail ordering remain
unchanged.

### Warning records

`image_warn` formats bounded warning records, attaches the operation name, and
invokes the per-operation callback for PNG and TIFF. The native warning format
is an explicit argument, preserving PNG diagnostic identity inside ICO
containers. Warnings remain separate from the first-error record; there is no
new global diagnostic state.

### Encoded buffering

The TIFF random-access client now contains `ImageByteBuffer` directly. It no
longer mirrors the data, length, and capacity fields or reconstructs and copies
back a temporary buffer on every write. TIFF seeks, source reads, output limits,
zero-filled gaps, and sink publication still use the same existing checks.

### Byte order

GIF preflight and libnsbmp call `lib/endian.h` directly, removing local forwarding
readers. libavif's eight host/network conversion functions retain their existing
symbols and signatures while delegating their byte operations to the same
helpers. Its 64-bit writer composes the existing big-endian 32-bit writer.
No additional endian framework or dependency is introduced.

## Other candidates reviewed

| Area | Assessment |
|---|---|
| Checked sizes, allocation, cancellation, exact reads, bounded writes | Already shared through the core operation/source/sink helpers and `math_checked.hpp`; new adapter-specific replacements would duplicate them. |
| Thumbnail geometry and filtering | Already shared by `image_target_size` and the alpha-correct area reducer. Native JPEG/WebP scaling and ICO/TIFF representation selection remain codec-specific. |
| PNG/TIFF DEFLATE | Already use the same zlib provider. There is no second DEFLATE implementation to extract. |
| GIF/TIFF LZW | Different bit order, dictionary growth boundaries, framing, and compatibility behavior. A parameterized engine would need independent conformance work and is not justified by this refactor. |
| JPEG/WebP/AV1 entropy readers and writers | Different coding models, refill invariants, and native kernels. Common byte access does not make their bitstream engines interchangeable. |
| RGB/YUV conversion and predictors | Sampling, range, bit depth, rounding, and SIMD contracts differ. Share simple image-boundary pixel preparation; retain the native mathematical kernels. |
| Native allocation entry points | Alignment, realloc behavior, threading, and ownership differ. A generic allocator shim would require a separate lifetime/budget audit, not merely replacing function names. |
| WebP/AOM low-level endian wrappers | Some further textual consolidation is possible. These helpers inline, and their type-generation/debug assertions differ; expected binary savings are negligible. |
| Metadata defaults | Single-page stills share defaults, but unknown GIF/WebP frame counts, TIFF pages/SubIFDs, and embedded PNG probes require overrides. A universal initializer offers little net LOC reduction. |
| JPEG EXIF and TIFF directories | A small bounded APP1 scanner and a general TIFF reader have different allocation and traversal contracts. Using the TIFF decoder for JPEG orientation would add overhead. |
| Memory/random-access BMFF identification | Similar box walking, but the memory signature API accepts prefixes while the resource scanner enforces complete boxes and metadata budgets. Sharing would need explicit policy parameters and careful compatibility tests. |
| File probe/decode wrappers | Share file acquisition already. Combining differently typed output operations would add dispatch glue with little net saving. |
| Per-format native error mapping | Status domains and native diagnostics differ. Shared record construction is useful; a generic mapping framework would replace short mappings with more machinery. |

Further substantial savings would require changes to codec capabilities,
backend selection, or native algorithms. They are separate decisions with
different compatibility and performance costs; this consolidation preserves
the current eight-format scope and native SIMD paths.

## Release measurements

An initial baseline executable and the original image sources were saved before
editing. The final executable was built using `make build-cli JOBS=6`: macOS
ARM64, `-O3`, `-march=native`, ThinLTO, and dead stripping. To isolate the size
comparison from concurrent work in the checkout, the saved original image
sources were then compiled with the same release commands and relinked against
the final build's identical non-image objects. The copied codec archive differs
only in the two refactored vendor objects. Both comparison executables receive
the ordinary macOS `strip -x` step.

For active source LOC, each audited translation unit was preprocessed with its
actual release compile command. A Clang virtual filesystem overlay supplied
the saved original files for the baseline without modifying the checkout.
Counts include unique source locations that produce nonblank code after
preprocessing, attributed only to the audited production files. Shared
headers count once. Tests, comments, blank lines, external headers, and inactive
branches do not count. This measures active source, not machine instructions.

| Audited production files | Before | After | Change |
|---|---:|---:|---:|
| Common core and internal header | 913 | 944 | +31 |
| JPEG adapter | 146 | 122 | −24 |
| AVIF adapter | 182 | 169 | −13 |
| WebP adapter | 125 | 116 | −9 |
| BMP/ICO adapter | 271 | 260 | −11 |
| PNG adapter | 200 | 194 | −6 |
| TIFF adapter | 369 | 361 | −8 |
| GIF implementation | 384 | 381 | −3 |
| libnsbmp implementation | 922 | 910 | −12 |
| libavif utilities | 236 | 217 | −19 |
| Unchanged public/compatibility/resource files | 518 | 518 | 0 |
| Total | 4,266 | 4,192 | **−74 (1.73%)** |

The same audited files have 4,371 → 4,298 nonblank, noncomment source lines
before preprocessing, a reduction of 73. Neither count relies on deleting
comments/blank lines or packing existing statements onto fewer lines.

`size -m` and file-size measurements of this controlled comparison:

| Release CLI measurement | Before (bytes) | After (bytes) | Change |
|---|---:|---:|---:|
| Machine-code section `__text` | 13,343,144 | 13,343,216 | +72 |
| Read-only constants in `__TEXT` | 1,358,720 | 1,358,464 | −256 |
| C strings | 553,558 | 553,483 | −75 |
| Used section payload in `__TEXT` | 15,364,430 | 15,364,179 | −251 |
| Mapped `__TEXT` segment | 15,368,192 | 15,368,192 | 0 |
| Stripped executable file | 16,098,312 | 16,098,320 | +8 |

The source reduction is real; the executable footprint is effectively
unchanged. The 251-byte payload saving includes an eight-byte increase in unwind
information. Alignment and link/signature metadata prevent that small payload
change from becoming a smaller mapped segment or executable file.

These results do not establish a material executable-size or throughput
improvement. ThinLTO already optimizes across translation units; source
deduplication and binary-size reduction are separate measurements. No release
throughput benchmark or x86_64/Windows validation was performed.

Raw measurements, saved baseline sources, preprocessor outputs, compile-command
extraction, build logs, and test logs are under `temp/image-refactor/`.

## Validation

- Native build and release CLI build succeed.
- Image I/O suite: **27/27**. The new cases cover 28 encoder roundtrips across
  gray/RGB/RGBA and premultiplied inputs with padded source rows; explicit
  background rejection and failed-output ownership; recoverable PNG/TIFF
  warnings; and all 12 AVIF rotation/mirror combinations. Existing JPEG
  orientation coverage now also asserts corner pixels for all eight transforms.
- GIF animation/disposal suite: **23/23**.
- Renderer/vector suite: **159/159**.
- Release CLI execution of `test/lambda/image_io_formats.ls` matches its existing
  golden file, covering the language-facing image input/output paths.

The existing image tests also retain thumbnail strategies, TIFF page/tile/SubIFD
selection, ICO DIB/PNG selection, chunked readers, allocation-failure cleanup,
row callbacks, and destination preservation. This is regression validation for
the refactor; it is not an independent conformance corpus for every variant of
all eight formats.
