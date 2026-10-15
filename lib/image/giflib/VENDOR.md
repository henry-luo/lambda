# giflib source

Official upstream: https://git.code.sf.net/p/giflib/code

Release: 5.2.2 (`44241952659c5db27da3d9db85d910c2b6904216`)

Library-only import for the common image library. Modified directly under the
user-approved source policy in `vibe/Lib_Image.md`; changes tracked through Git.
License: COPYING. Unneeded CLI tools and font/quantizer code are omitted.


Build integration (2026-10-15): upstream build systems, templates, and
repository metadata are removed. `build_lambda_config.json` owns the
explicit image I/O source lists, feature definitions, platform/CPU
selection, and generated headers. Premake builds these sources into
`build/lib/liblambda-image-codecs.a` with the ordinary Lambda targets.
No CMake configuration, staging tree, or separately built codec prefix
is used. Licenses, patent notices, and copyright attributions remain.
