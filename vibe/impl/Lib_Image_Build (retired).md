# Image Libraries: Build, Dependencies, and Size Optimization

> **Status: RETIRED 2026-10-23.** The standing design decisions (library inventory, one-implementation-per-job dependencies, AVIF decoder-only and TIFF compression profiles, size policy, open size candidates, change gates) now live in [`Lib_Image.md` §5.4](../lib/Lib_Image.md#54-build-profile-dependencies-and-size-policy). This file is kept for its measurements, link-map attribution, validation record, and audit-evidence pointers.

Date: 2026-10-16
Last verified against tree: 39ce33435bce plus the 2026-10-16 optimization
changes, macOS ARM64.
Status: decoder-only AVIF, size optimization for all codecs, and the reduced
TIFF profile are implemented. Source-level AOM/JPEG specializations remain
follow-on proposals, as individually marked.

This is a documentation-only record; it is not part of the build.
Scope: the native raster image libraries, their shared dependencies, and their
contribution to the release executable. It does not change feature policy or
the formal specifications.

Spec linkage:

- D7.1.2v2: "Raw context-free mechanisms stay lib"; resource acquisition stays
  in lambda-io. Byte codecs and shared utilities follow that boundary.
- S7.4.4: "Rich diagnostics are mandatory at every checked-channel failure."
  Removing a codec capability must preserve an explicit, recoverable error.

Related records:
[image design](../lib/Lib_Image.md),
[implementation and supported subsets](Lib_Image_Impl.md),
[earlier utility consolidation](Lib_Image_Refactor.md),
[formal design](../../doc/Lambda_Formal_Design.md), and
[formal semantics](../../doc/Lambda_Formal_Semantics.md).

## 1. Current libraries and public feature set

The public registry is `lib/image_core.cpp:image_codecs`. There are eight
readable formats and six writable formats. A codec being compiled does not
mean that every upstream feature is exposed by Lambda.

| Library under lib/image | Imported version | Role | ARM64 source files |
|---|---|---|---:|
| libpng | 1.6.55 | PNG decoding and encoding | 18 |
| libjpeg-turbo | 3.1.2 | JPEG decoding/encoding; JPEG-in-TIFF backend | 117 |
| giflib | 5.2.2 | GIF decoding; Lambda supplies frame composition | 4 |
| libwebp | 1.6.0 | Still WebP decoding/encoding, including internal SharpYUV | 75 |
| libavif | 1.3.0 | AVIF container, metadata, color conversion, scaling glue | 25 |
| libaom | 3.12.1 | AV1 decoder needed to decompress AVIF image payloads | 125 |
| libtiff | 4.7.1 | TIFF directories, strips/tiles, compression adapters | 32 |
| libnsbmp | 0.1.7 | BMP and ICO DIB decoding | 1 |

These total 397 files, plus generated `aom_build_config.c`: 398 translation
units in the macOS ARM64 archive. Counts describe compilation inputs, not how
many objects or functions survive linking. The libavif count includes five
bundled libyuv fallback sources. Versions and licenses are recorded in each
library's `VENDOR.md` and retained license files.

| Format | Read | Write | Current public subset and preview behavior |
|---|---|---|---|
| PNG | Yes | Yes | Palette/gray/RGB/RGBA, transparency, Adam7; 16-bit input reduced to RGBA8. Noninterlaced rows can be reduced incrementally; Adam7 may require full storage. |
| JPEG | Yes | Yes | 8-bit baseline/progressive gray/RGB, EXIF orientation, native scaled decoding. Progressive coefficients still require source-size storage. Alpha output requires an explicit background. |
| GIF | Yes | No | First frame or selected composited frame; legacy animation timing/disposal retained. Selected frames/interlace can require full storage. |
| WebP | Yes | Yes | Lossy/lossless stills and alpha; native output scaling. Lossless history and alpha can require source-size planes. Animated WebP is unsupported. |
| AVIF | Yes | No | SDR stills, alpha, rotation/mirroring, high-bit-depth input converted to RGBA8. Full AV1 planes are decoded before common resizing; strict shrinking is rejected. |
| BMP | Yes | Yes | Supported Windows palette/RGB/bitfield/RLE inputs; uncompressed 24-bit output. Alpha output requires an explicit background. |
| TIFF | Yes | Yes | Classic TIFF/BigTIFF, selected pages, strips/tiles, gray/palette/RGB/RGBA. Compatible direct reduced-image SubIFDs can supply previews. Output is single-page, 8-bit, Deflate-compressed TIFF. |
| ICO | Yes | Yes | PNG and DIB entries with validated masks/offsets; select a sufficient representation for previews. Output contains one PNG entry. |

The interchange pixels are RGBA8. ICC presence and AVIF CICP metadata can be
reported, but this is not a color-management or profile-preservation pipeline.
CMYK JPEG, HDR PQ/HLG tone mapping, AVIF clean apertures, general AVIF sequence
decoding, GIF writing, and scientific TIFF sample preservation are outside the
current public subset. SVG uses the separate vector-rendering path.

TIFF has a narrower public photometric subset than the bundled library:
`lib/image/image_tiff.cpp:image_tiff_decode` admits MINISWHITE, MINISBLACK,
RGB, and PALETTE. YCbCr, CMYK/separated, and LogLuv inputs are rejected there.
Its direct scanline path requires 8-bit contiguous RGB/RGBA; other admitted
layouts use libtiff's RGBA strip/tile readers. In particular, compiled JPEG
compression support does not imply support for every JPEG-compressed TIFF.

## 2. Build setup

The authoritative inputs are in `build_lambda_config.json` (imported from `build_deps_config.json` via `config_imports`):

1. `targets[name=lambda-image-codecs]` lists the library C files, common
   include directories, per-file flags, and CPU/platform source sets.
2. `image_codecs.generated_files` describes feature/version headers under
   `build_temp/image-config/`.
3. `image_codecs.dispatch_generators` invokes libaom's Perl `rtcd.pl` DSL to
   generate the ARM64 and generic dispatch headers.
4. The `image-codecs` library provider names the static archive and its
   `build_target`, so consumers build it before linking.

Build flow:

    build_lambda_config.json
        -> utils/generate_image_config.py -> generated codec headers/dispatch
        -> utils/generate_premake.py -> generated Premake Lua -> Makefiles
        -> build/lib/liblambda-image-codecs.a -> Lambda / tests / consumers

The codec target is C17, position-independent, and compiled with NDEBUG.
Lambda's own C++ integration follows C++17. The target requests Size
optimization, producing -Os for all codec C files on the measured Clang build.
The earlier libaom/libavif-only -Os file rule is removed as redundant.
Release builds use LTO, dead stripping, and post-link symbol stripping. On
macOS, the size comparisons in this document use `strip -x`.

Generated codec include directories precede system codec headers. Production
image builds do not run upstream CMake/Meson projects or use a separately
installed image-codec prefix. Edit JSON and regenerate through `make`; do not
manually edit generated Lua, Makefiles, or headers.

Architecture selection:

- ARM64: PNG/JPEG/WebP NEON; libaom runtime CPU dispatch and per-file flags for
  selected NEON, dot-product, i8mm, and SVE variants. The AOM decoder closure
  has 68 portable files, one ARM CPU-detection file, and 56 SIMD files.
- x86_64: PNG SSE2 and WebP SSE2/SSE4.1; portable JPEG and generic AOM. The
  configured image archive has 333 source files on macOS x86_64.
- macOS/Linux use `tif_unix.c`; Windows uses `tif_win32.c`. Source selection
  is configured for these platforms; the reported measurements and execution
  checks cover macOS ARM64 only.

Current AVIF settings are `CONFIG_AV1_ENCODER=0`, `CONFIG_AV1_DECODER=1`,
high-bit-depth decoding enabled, and ARM64 runtime dispatch enabled.
libavif defines AVIF_CODEC_AOM, AVIF_STATIC, and AVIF_CODEC_AOM_DECODE;
AVIF_CODEC_AOM_ENCODE is absent. `libavif/src/write.c` and the AOM encoder
implementation closure are excluded. Some shared upstream API files, such as
`aom/src/aom_encoder.c`, are still compiled but are not extracted at host link.

Do not define a feature to 0 when the upstream condition is `#ifdef` or
`defined(...)`: those features must be omitted entirely to disable them.

## 3. Inter-library dependencies

| Consumer | Dependency | What is shared |
|---|---|---|
| libpng | zlib | PNG Deflate compression/decompression and checksums |
| libtiff: tif_zip.c | zlib | Deflate for TIFF strips/tiles |
| libtiff: tif_jpeg.c | libjpeg-turbo | 8-bit JPEG compression/decompression inside TIFF strips/tiles |
| libavif | libaom | AV1 payload decoding; the AVIF container alone cannot decompress pixels |
| libavif | bundled minimal libyuv sources | Internal plane-scaling fallback; not a separate full libyuv dependency |
| libwebp | its bundled SharpYUV | RGB/YUV conversion used by WebP encoding |
| ICO PNG entries / ICO writer | shared libpng adapter | Reuses PNG decoding/encoding |
| BMP and ICO DIB entries | libnsbmp | Shared DIB/palette/mask reader |
| ThorVG external PNG/JPEG loaders | same libpng / libjpeg-turbo | No second private PNG/JPEG implementation in the measured release |
| Lambda ZIP implementation | same zlib | Raw Deflate streams inside ZIP entries |
| Other archive readers | zlib, liblzma, libzstd, bzip2 as configured | Existing compression backends, separate from image-format adapters |

The macOS configuration links one zlib static archive, declared as
`/opt/homebrew/opt/zlib/lib/libz.a`. Existing archive providers similarly name
one liblzma archive and one libzstd archive. Other platform configurations
select their own paths; these are not extra copies embedded per consumer.

TIFF is a container, not a single compression algorithm. `tif_jpeg.c` handles
TIFF-specific JPEG tables, strip/tile boundaries, codec state, and diagnostics
before calling libjpeg-turbo. `tif_webp.c` performs the corresponding adaptation
for WebP-compressed TIFF. Neither is a second implementation of its codec.
See the [upstream compression configuration](https://libtiff.gitlab.io/libtiff/internals.html).

`tif_zip.c` is a Deflate adapter, not a ZIP archive parser. TIFF predictors,
strip resets, tags, and errors differ from ZIP entry framing. Reusing zlib is
appropriate; replacing the TIFF adapter with the archive reader is not.

Current TIFF compression configuration:

- Built: uncompressed, LZW, PackBits, CCITT RLE/Fax3/Fax4, ordinary 8-bit
  JPEG, and Deflate.
- Excluded by this optimization: JPEG12, PixarLog, SGILog/LogLuv, NeXT, and
  ThunderScan. Both implementation files and enabling definitions are removed
  from the build configuration.
- Already excluded: WebP, LZMA, Zstd, old-style OJPEG, JBIG, and LERC. Their
  source files may exist in the imported tree but contribute no executable
  bytes. tif_pixarlog.c would use zlib if re-enabled.

If TIFF LZMA/Zstd support were added, it could call the existing compression
libraries. However, TIFF adapters install encode and decode callbacks; adding
them can pull encoder routines into a program whose archives only need the
decoder. Shared linkage does not make additional functionality free.

## 4. Is TIFF's jpeg_12 useful?

`tif_jpeg_12.c` is the 12-bit-per-sample JPEG-in-TIFF adapter. It reuses
`tif_jpeg.c` under a 12-bit specialization and calls the same libjpeg-turbo
library's 12-bit API. It is distinct from ordinary 8-bit JPEG-in-TIFF and from
the many other ways of storing high-bit-depth TIFF samples.

It has real specialist value. GDAL explicitly supports reading and writing
12-bit JPEG-compressed TIFF, while recommending 8-bit data for usual JPEG
compression. The libjpeg-turbo 3.1.2 usage documentation also warns that
non-8-bit precision is not widely implemented by decoders. Together these
support treating 12-bit JPEG-in-TIFF as specialist geospatial/imaging
compatibility rather than a default requirement for ordinary document images.
This is a qualitative compatibility assessment, not measured usage telemetry.
[GDAL GTiff](https://gdal.org/en/stable/drivers/raster/gtiff.html),
[libjpeg-turbo 3.1.2 usage](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/3.1.2/doc/usage.txt).

Before its removal, the adapter did not deliver working 12-bit TIFF pixel
decoding through Lambda's public image API:

1. `image_tiff_decode` uses the direct TIFFReadScanline path only for 8-bit
   contiguous RGB/RGBA. Twelve-bit input goes to the RGBA strip/tile path.
2. `libtiff/libtiff/tif_getimage.c:TIFFRGBAImageBegin` accepts only 1, 2, 4,
   8, and 16 bits per sample. Twelve-bit samples are rejected. TIFFRGBAImageOK
   has the same restriction. The upstream RGBA API documents this limit.
3. `image_tiff_encode` always writes 8-bit Deflate TIFF, so the public writer
   does not use the JPEG12 encoder either.

The stored `test/input/image/tiff/jpeg12-gray.tiff` fixture was encoded before
removal with libtiff 4.7.1/libjpeg-turbo 3.1.2. The original backend read its
scanline successfully, and TIFFRGBAImageOK rejected RGBA conversion. The new
GTest verifies successful 12-bit metadata probing and an explicit
unsupported_feature result for pixel decoding.
[RGBA reader limits](https://libtiff.gitlab.io/libtiff/functions/TIFFReadRGBAImage.html).

Implemented: JSON excludes `lib/image/libtiff/libtiff/tif_jpeg_12.c` and omits
HAVE_JPEGTURBO_DUAL_MODE_8_12 and the unused LIBJPEG_12_PATH definition from
generated tif_config.h. Regeneration removes the 8-bit adapter's references to
TIFFReInitJPEG_12 and related symbols. The imported source remains available
for an intentional future GeoTIFF/high-depth profile. Ordinary 8-bit RGB
JPEG-in-TIFF strips and tiles are covered by the new image test.

`image_tiff_decode` recognizes 12-bit JPEG-compressed TIFF before allocating
pixels and returns unsupported_feature with a specific message, preserving
rich checked-channel diagnostics (S7.4.4). The writer remains 8-bit Deflate.

Before removal, the adapter accounted for 19,415 live bytes (19.0 KiB), or 12,818
bytes (12.5 KiB) in the -Os experiment. These are attribution figures, not a
measured executable reduction from disabling JPEG12. Removing it does not
automatically remove libjpeg-turbo's own 12-/16-bit routines: common runtime
dispatch still references those routines. Keep that larger specialization as
a separate investigation. JPEG12 removal is verified in the rebuilt archive;
its adapter symbols and member are absent.

## 5. Measured release footprint

The initial audit baseline, after decoder-only AVIF and before the further
optimizations, was 25,853,448 bytes (24.66 MiB), SHA-256:
`9a060ec17d3afd96bb51441580a2fe44dd9249875a246fce6ce2f7c2b4e6da29`.

A diagnostic relink reproduced all file-backed Mach-O section bytes of that
executable. The link-map attribution checked 465 independent symbol anchors
when recovering anonymous ThinLTO object owners. The following counts are
live file-backed code/data bytes, not archive sizes, RAM use, or exact
standalone savings from removing a library. Inlined code may be attributed
to the caller, and linker metadata/alignment are accounted separately.

| Library | Pre-optimization live bytes | Isolated all-image -Os live bytes |
|---|---:|---:|
| libaom | 1,123,604 | 1,123,604 |
| libjpeg-turbo | 536,773 | 369,203 |
| libtiff | 465,887 | 354,820 |
| libwebp | 452,908 | 233,335 |
| libpng | 166,830 | 104,784 |
| libavif, including scaling fallback | 78,909 | 79,009 |
| libnsbmp | 8,916 | 5,172 |
| giflib | 7,730 | 5,598 |

libaom/libavif already use -Os; the small libavif difference is an observed
relink result, not a new feature. Shared zlib, archive backends, and Lambda's
own adapters are outside this library table.

| Isolated executable, all stripped with strip -x | Bytes | MiB | Saving from diagnostic baseline |
|---|---:|---:|---:|
| Diagnostic baseline | 25,853,464 | 24.6558 | 0 |
| All image libraries at -Os | 25,292,336 | 24.1207 | 561,128 bytes / 548.0 KiB |
| Also disable four legacy TIFF codecs | 25,259,208 | 24.0891 | 594,256 bytes / 580.3 KiB |

The diagnostic baseline is 16 bytes larger than production due to file
metadata; code/data sections match. The additional legacy TIFF saving is
33,128 bytes (32.4 KiB) after -Os. Do not add current -O3 source attribution
figures to these measured -Os reductions.


Final implemented profile (2026-10-16): all image libraries use Size, and the
five TIFF implementation files above are excluded. The production executable
is 25,242,584 bytes (24.0732 MiB), saving 610,864 bytes (0.5826 MiB) against
the pre-optimization production executable. SHA-256:
`76b8bc3ab734047e9f75e2330f043d0b3dba6cc3025060eaeab6da3eeffae534`.

The final production link map reproduces the executable's file-backed section
bytes and verifies 474 independent ThinLTO owner anchors. Final live bytes are
1,123,604 AOM; 367,755 JPEG; 314,554 TIFF; 233,335 WebP; 104,784 PNG;
79,009 libavif/fallback; 5,598 giflib; and 5,172 libnsbmp. No removed TIFF
implementation contributes to the map. `lambda.exe` and `release/lambda`
contain the same optimized executable.

## 6. Implemented optimizations and follow-on candidates

### 6.1 Size optimization for all image libraries

Status: implemented in production JSON and validated on macOS ARM64.

The lambda-image-codecs target sets optimize to Size. Its generated release
CFLAGS contain -Os; there is no target-level -O3 override. Per-file ISA flags,
runtime dispatch, LTO, and dead stripping are retained. The old libaom/libavif
-Os file rule is redundant and has been removed.

The isolated flag change saved approximately 0.54 MiB without feature
removal. The combined production profile is measured below. Five alternating
release benchmark trials used eight iterations per operation on a deterministic
768 by 512 RGB image, default quality 85, and 128-pixel previews. Median
after/before operation times were:

| Format | Encode | Decode | Thumbnail |
|---|---:|---:|---:|
| PNG | 1.017x | 1.001x | 1.008x |
| JPEG | 1.004x | 1.043x | 1.027x |
| WebP | 1.035x | 1.023x | 1.018x |
| TIFF | 1.002x | 0.999x | 1.002x |

Encoded sizes and decoded pixel checksums matched across profiles. This is one
synthetic RGB workload, not a broad corpus/performance guarantee. SIMD stays
enabled; measured median slowdowns were at most 4.3% in this run.

### 6.2 Remove TIFF features outside the chosen public subset

Status: implemented in production JSON, including JPEG12 exclusion.

LOGLUV_SUPPORT, PIXARLOG_SUPPORT, NEXT_SUPPORT, and THUNDER_SUPPORT are
omitted, along with tif_luv.c, tif_pixarlog.c, tif_next.c, and tif_thunder.c.
Together with the JPEG12 removal described in section 4, the image target
loses five translation units. The archive is rebuilt from a clean member list.
The native library no longer accepts the four removed compression families;
LogLuv photometric input was already rejected by Lambda. This is an intentional
compatibility reduction under the user-authorized minimal profile.

The separate four-codec experiment saved 32.4 KiB after -Os; the final combined
production saving is reported below. Keep CCITT Fax support for
bilevel/scanned documents: tif_fax3.c plus its table source currently account
for about 133 KiB, including 97 KiB of tables, but removal would discard
useful document compatibility. Keep Deflate, LZW, PackBits, and ordinary JPEG.

The TIFF codec registry in `tif_codec.c:_TIFFBuiltinCODECS` holds pointers to
enabled initializers. Those initializers install callback tables, retaining
codec code even if the common wrapper never explicitly chooses that codec.
JSON feature definitions must remove those references; dead stripping cannot
infer which compression tags future input files will contain.

### 6.3 Investigate libaom's retained forward quantization table

Status: source/link-map finding; no implementation or measured removal.

`av1/common/quant_common.c:av1_qm_init` initializes both gqmatrix and giqmatrix
even in decoder-only builds. This retains wt_matrix_ref and iwt_matrix_ref,
each 100,320 bytes. The forward-matrix consumers found in this tree are in
encoder sources; inverse matrices are needed for decoding.

Conditionalizing forward-matrix initialization/storage on encoder support is
a candidate for approximately 98 KiB of table removal. Setting
CONFIG_QUANT_MATRIX=0 does not achieve it: the relevant guards also test
CONFIG_AV1_DECODER. This requires a reviewed source-level change and decoder
validation, under the applicable vendor-change policy; it is not a file-list
or ordinary configuration-only optimization.

### 6.4 Investigate JPEG precision and unused processing modes

Status: candidate only; retained bytes are not confirmed removable savings.

The current 12-bit wrapper files account for 117,844 bytes and 16-bit wrappers
for 44,584 bytes. The public JPEG adapter uses the 8-bit interface, but common
libjpeg dispatch and the current TIFF integration retain higher-precision
routines. Do not simply exclude wrapper files or supply dummy functions.
Trace every caller, feature guard, and TIFF use first. Consider precision,
palette quantization, arithmetic coding, and unused transform modes separately
so compatibility losses are explicit and ordinary/progressive JPEG survives.

### 6.5 Deprioritize small utility replacements and already stripped code

TIFF's hash set tracks directory offsets/numbers and detects loops. It has
1,336 directly attributed live bytes, with possible additional inlining into
callers. A Lambda HashMap adapter is feasible, but must preserve ownership,
hash/equality/free callbacks, replacement/deletion, and allocation failures.
Its net saving would be small and must subtract the adapter's own code.

The host linker already does not extract these members:
`tif_print.o`, `tif_version.o`, `tif_extension.o`, `pngpread.o`,
`picture_psnr_enc.o`, and `aom_encoder.o`. Excluding them may simplify builds
but does not shrink this release. Small codec-specific print callbacks can
still survive through TIFF method tables; those are distinct from tif_print.c.
Likewise, tif_unix.c still supplies live allocation/memory helpers even though
the public wrapper uses TIFFClientOpenExt rather than TIFFOpen.

Do not classify a source as removable solely because its link-map contribution
is zero: LTO may have inlined its required routines into other objects.

## 7. Validation and implementation gates

Completed for the isolated all-image -Os build on macOS ARM64:

- 28/28 existing image GTests passed.
- `test/lambda/image_io_formats.ls` matched its existing golden exactly.
- `test/html/image_formats_gallery.html` rendered to a byte-identical PNG.

The four-codec TIFF variant separately passed the same Lambda golden and
byte-identical gallery render; its full image GTest suite was not rerun.
Those original experiments did not include a performance benchmark, full
aggregate baseline, or Linux/Windows build. Subsequent production validation
and the release benchmark are recorded here.

Completed for the production profile: 29/29 image GTests, the exact Lambda
image golden, a byte-identical gallery PNG, the Premake generator self-test,
23/23 GIF player tests, 274/274 host-export validation, and the release
benchmark in section 6.1.
The new TIFF test covers 8-bit JPEG strips/tiles and the stored JPEG12 failure.
The five removed TIFF archive members and their codec symbols are absent.
Full engine aggregate baselines and Linux/Windows execution remain unrun.

For future profile changes:

1. Change JSON, regenerate normally, and rebuild affected objects and the
   archive from a clean member list. Old ar members and same-second object
   timestamps can otherwise invalidate the comparison; follow Developer
   Guide section 7. Do not run `make release` in a worktree.
2. Run the image GTests and Lambda golden. Add focused JPEG-in-TIFF coverage
   when changing TIFF JPEG configuration; prove both preserved 8-bit behavior
   and deliberate handling of unsupported 12-bit input.
3. Compare rendered image-gallery pixels, alpha/orientation cases, and relevant
   malformed-input behavior. Retain directory-loop and resource-limit checks.
4. Measure identically stripped release artifacts and link maps. Benchmark
   release builds for any optimization-flag or SIMD change.
5. Validate the generic and Windows configurations before claiming portability;
   run affected aggregate gates when changes extend into the engines.

Evidence and reproduction inputs from the 2026-10-16 audit are under
`temp/image-size-review/`: findings.json, analysis.json, current.map,
link-map.mk, os-analysis/account.json, slim-analysis/account.json, compiler/link
logs, golden outputs, test output, and gallery PNGs. These are local temporary
artifacts, so this document preserves the essential measurements independently.
The implementation evidence, preserved before binaries/archive, stored-fixture
generator, benchmark source/results, and final validation logs are under
`temp/image-opt-implementation/`. The earlier decoder-only AVIF evidence is
under `temp/aom-decode-only/`.

Relevant maintained entry points:

- [JSON build configuration](../../build_lambda_config.json) (image targets and generators are in its imported [`build_deps_config.json`](../../build_deps_config.json))
- [codec configuration generator](../../utils/generate_image_config.py)
- [native image API](../../lib/image.h)
- [registry and common mechanisms](../../lib/image_core.cpp)
- [TIFF adapter](../../lib/image/image_tiff.cpp)
- [JPEG adapter](../../lib/image/image_jpeg.cpp)
- [image tests](../../test/test_image_io_gtest.cpp)
- [build/worktree guidance](../../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas)
