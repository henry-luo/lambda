# Radiant Browse Repros

These pages are minimal local reproductions for issues first found by the
online Radiant browser-smoke test. They are intended for `lambda view` or
future local gtests so regressions can be checked without relying on external
sites.

Suggested local runs:

```sh
./lambda.exe view test/browse/repro/js-array-non-index-property.html --headless
./lambda.exe view test/browse/repro/js-document-computed-call.html --headless
./lambda.exe view test/browse/repro/css-clip-path-calc-polygon.html --headless
./lambda.exe view test/browse/repro/http-staged-html-network-discovery.html --headless
./lambda.exe view test/browse/repro/svg-cache-format-preserved.html --headless
./lambda.exe view test/browse/repro/runtime-error-word-content.html --headless
./lambda.exe view test/browse/repro/font-face-fallback-source-list.html --headless
./lambda.exe view test/browse/repro/js-medium-library-watchdog.html --headless
```

For `http-header-before-head-relative-resource.html`, serve the repository root
or this directory over localhost and use the HTTP URL. That repro targets the
HTTP staging path that injects a `<base>` tag before linked resources are
resolved.

`http-staged-html-network-discovery.html` mirrors the same staging class with a
static local file: a document that carries an HTTP `<base>` and a linked
stylesheet should still initialize network resource discovery.

`svg-cache-format-preserved.html` uses an extensionless SVG URL that is stored
under a hashed `.cache` filename. The loaded image surface must remain SVG so
rendering does not try to blit a NULL raster pixel buffer.

`runtime-error-word-content.html` documents a harness issue found on language
reference pages: ordinary page text can mention panic or POSIX signals without
being a process crash.

`font-face-fallback-source-list.html` captures old-style `@font-face` fallback
lists where unsupported `.eot` entries precede WOFF/TTF sources. Radiant should
skip unsupported formats and avoid serially downloading every fallback source.

`js-medium-library-watchdog.html` is a compact local script workload for the JS
watchdog path. Online pages exposed that the watchdog includes parse/transpile
time, so medium browser libraries need more than the tiny-script budget.
