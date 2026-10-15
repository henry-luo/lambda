# Lambda image source: libtiff

Upstream: https://gitlab.com/libtiff/libtiff

Imported release: 4.7.1

Archive SHA-256: `f698d94f3103da8ca7438d84e0344e453fe0ba3b7486e04c5bf7a9a3fabe9b69`

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
