# Lambda image source: libnsbmp

Upstream: https://www.netsurf-browser.org/projects/libnsbmp/

Imported release: 0.1.7

Archive SHA-256: `5407a7682a122baaaa5a15b505290e2d37df54c13c5edef4b09d12c862d82293`

Official source imported for the common image library (2026-10-15).
Local adaptations are made directly in this directory and reviewed through Git.
No separate patch files are required, per the user-approved source policy in
`vibe/Lib_Image.md`. License notices are retained. Unneeded test corpora, tools,
and application directories are omitted; only library targets are built.

Lambda adaptations: allocator callbacks carry operation userdata; palettes and ICO nodes use those callbacks; metadata-only probing skips bitmap allocation; byte order uses `lib/endian.h`; DIB/palette offsets and partial-byte AND masks are validated before access; legacy all-zero 32-bit alpha follows the AND mask.

Utility consolidation (2026-10-15): local endian forwarding functions are
removed; call sites use the shared signed/unsigned readers directly. See
`vibe/impl/Lib_Image_Refactor.md` for validation.


Build integration (2026-10-15): upstream build systems, templates, and
repository metadata are removed. `build_lambda_config.json` owns the
explicit image I/O source lists, feature definitions, platform/CPU
selection, and generated headers. Premake builds these sources into
`build/lib/liblambda-image-codecs.a` with the ordinary Lambda targets.
No CMake configuration, staging tree, or separately built codec prefix
is used. Licenses, patent notices, and copyright attributions remain.
