# Lambda image source: libavif

Upstream: https://github.com/AOMediaCodec/libavif

Imported release: 1.3.0

Archive SHA-256: `0a545e953cc049bf5bcf4ee467306a2f113a75110edf59e61248873101cd26c1`

Official source imported for the common image library (2026-10-15).
Local adaptations are made directly in this directory and reviewed through Git.
No separate patch files are required, per the user-approved source policy in
`vibe/Lib_Image.md`. License notices are retained. Unneeded test corpora, tools,
and application directories are omitted; only library targets are built.

Lambda uses only the libaom AV1 backend and the bundled minimal libyuv scaling fallback; optional applications and backends are not built.

Byte-order adaptation (2026-10-15): the existing host/network conversion entry
points delegate to `lib/endian.h`, retaining their signatures and portable
unaligned byte semantics. See `vibe/impl/Lib_Image_Refactor.md` for validation.


Build integration (2026-10-15): upstream build systems, templates, and
repository metadata are removed. `build_lambda_config.json` owns the
explicit image I/O source lists, feature definitions, platform/CPU
selection, and generated headers. Premake builds these sources into
`build/lib/liblambda-image-codecs.a` with the ordinary Lambda targets.
No CMake configuration, staging tree, or separately built codec prefix
is used. Licenses, patent notices, and copyright attributions remain.
