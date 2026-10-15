# Lambda image source: libaom

Upstream: https://aomedia.googlesource.com/aom

Imported release: 3.12.1

Archive SHA-256: `77a37e4c9914d602bbb7beb31b138c4735761700d52a754d05d18625a02189cf`

Official source imported for the common image library (2026-10-15).
Local adaptations are made directly in this directory and reviewed through Git.
No separate patch files are required, per the user-approved source policy in
`vibe/Lib_Image.md`. License notices are retained. Unneeded test corpora, tools,
and application directories are omitted; only library targets are built.


Build integration (2026-10-15): upstream build systems, templates, and
repository metadata are removed. `build_lambda_config.json` owns the
explicit image I/O source lists, feature definitions, platform/CPU
selection, and generated headers. Premake builds these sources into
`build/lib/liblambda-image-codecs.a` with the ordinary Lambda targets.
No CMake configuration, staging tree, or separately built codec prefix
is used. Licenses, patent notices, and copyright attributions remain.

`rtcd.pl` is the upstream function-dispatch code generator, relocated
from `build/cmake/` with its notices intact. Its feature inputs and
invocations are declared in the Lambda build config. ARM64 retains
runtime SIMD dispatch; the x86_64 profile uses generic AV1 routines.
Optional libyuv, SVT-AV1, WebM, and standalone rate-control providers
are excluded from the image I/O source target.
