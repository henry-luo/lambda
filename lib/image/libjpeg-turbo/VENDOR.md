# Lambda image source: libjpeg-turbo

Upstream: https://github.com/libjpeg-turbo/libjpeg-turbo

Imported release: 3.1.2

Archive SHA-256: `560f6338b547544c4f9721b18d8b87685d433ec78b3c644c70d77adad22c55e6`

Official source imported for the common image library (2026-10-15).
Local adaptations are made directly in this directory and reviewed through Git.
No separate patch files are required, per the user-approved source policy in
`vibe/lib/Lib_Image.md`. License notices are retained. Unneeded test corpora, tools,
and application directories are omitted; only library targets are built.

The Lambda BMP writer in `lib/image/image_bitmap.cpp` adapts the uncompressed Windows header and BGR row layout from `src/wrbmp.c`; its IJG attribution and `README.ijg` notices are retained.


Build integration (2026-10-15): upstream build systems, templates, and
repository metadata are removed. `build_lambda_config.json` owns the
explicit image I/O source lists, feature definitions, platform/CPU
selection, and generated headers. Premake builds these sources into
`build/lib/liblambda-image-codecs.a` with the ordinary Lambda targets.
No CMake configuration, staging tree, or separately built codec prefix
is used. Licenses, patent notices, and copyright attributions remain.

`LAMBDA_IMAGE_IO_ONLY` omits TurboJPEG file converters and lossless
transforms; Lambda owns file I/O and orientation. Their source files
are excluded from the build. ARM64 uses the upstream NEON intrinsics;
the x86_64 profile uses the portable JPEG implementation.
