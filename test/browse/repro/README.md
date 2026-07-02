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
./lambda.exe view test/browse/repro/data-uri-image-cache-cleanup.html --headless
./lambda.exe view test/browse/repro/invalid-image-placeholder.html --headless
./lambda.exe view test/browse/repro/http-background-image-no-sync-fetch.html --headless
./lambda.exe view test/browse/repro/large-registry-table-layout.html --headless
./lambda.exe view test/browse/repro/data-uri-svg-cache-ownership.html --headless
./lambda.exe view test/browse/repro/optional-missing-subresources.html --headless
RADIANT_JS_EXTERNAL_SCRIPT_BYTES=128 ./lambda.exe view test/browse/repro/large-external-script-skip.html --headless
RADIANT_JS_TOTAL_SCRIPT_BYTES=128 ./lambda.exe view test/browse/repro/script-total-budget-skip.html --headless
./lambda.exe view test/browse/repro/unsupported-image-fallback.html --headless
./lambda.exe view test/browse/repro/css-var-on-text-layout.html --headless
./lambda.exe view test/browse/repro/grid-track-unitless-zero-overflow.html --headless
./lambda.exe view test/browse/repro/css-var-self-reference.html --headless
./lambda.exe view test/browse/repro/event-listener-mutation-realloc.html --headless
./lambda.exe view test/browse/repro/event-listener-entry-table-realloc.html --headless
./lambda.exe view test/browse/repro/element-scroll-method.html --headless
./lambda.exe view test/browse/repro/event-listener-onerror-reentry.html --headless
./lambda.exe view test/browse/repro/optional-font-retry-storm.html --headless
./lambda.exe view test/browse/repro/js-regex-wrapper-cache-leak.html --headless
./lambda.exe view test/browse/repro/many-remote-font-faces-fallback.html --headless
./lambda.exe view test/browse/repro/onetrust-branch-closure-readback.html --headless
./lambda.exe view test/browse/repro/spring-onetrust-external-script.html --headless
./lambda.exe view test/browse/repro/d3-observable-made-by-custom-element.html --headless
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

`data-uri-image-cache-cleanup.html` keeps several inline image surfaces alive
through layout and render. The shared image cache must own them so shutdown does
not leave MEM_CAT_IMAGE allocations behind.

`invalid-image-placeholder.html` uses a syntactically valid image URL whose
payload cannot be decoded as a supported image. Radiant should render with a
placeholder path instead of reporting a runtime error.

`http-background-image-no-sync-fetch.html` documents pages where CSS background
images are discovered late. Once a network-managed document has moved into
layout/render, Radiant must not perform blocking synchronous HTTP fetches from
the paint path.

`large-registry-table-layout.html` mirrors the shape of IANA/RFC registry
pages: a dense table with many rows and long tokens should remain stable and
bounded during layout.

`data-uri-svg-cache-ownership.html` captures repeated inline SVG images. SVG
data URI surfaces live in the shared image cache, and DOM embed cleanup must not
free the borrowed pointer before cache shutdown.

`optional-missing-subresources.html` captures missing linked CSS, script, font,
and image resources. Online pages often reference stale optional assets; Radiant
should fall back without turning a successfully rendered document into a page
load failure.

`large-external-script-skip.html` documents the browser-script compatibility
guard. Use a low `RADIANT_JS_EXTERNAL_SCRIPT_BYTES` value to exercise the skip
path locally; the online failures used multi-megabyte bundles.

`script-total-budget-skip.html` captures pages that accumulate many smaller
scripts. Once the browser JS source budget is exhausted, remaining scripts
should be skipped gracefully instead of entering unstable JIT/runtime paths.

`unsupported-image-fallback.html` captures unsupported optional image payloads.
They should paint as placeholders without reporting runtime errors or leaking
lazy decode buffers.

`css-var-on-text-layout.html` captures inherited custom properties resolved
while layout is operating on a text node. Variable lookup should climb to the
nearest element instead of assuming the active layout view is always an element.

`grid-track-unitless-zero-overflow.html` captures grid template lists with a
unitless `0` track. CSS accepts `0` as a length, so the first allocation pass
must count it before the second parsing pass stores the track.

`css-var-self-reference.html` captures self-referential custom properties such
as `--loop: var(--loop)`. Variable resolution should fall back gracefully instead
of recursing until stack overflow.

`event-listener-mutation-realloc.html` captures listener arrays that grow while
an event dispatch is iterating a snapshot. Dispatch must not keep pointers into
storage that `addEventListener()` can reallocate.

`event-listener-entry-table-realloc.html` captures listener-entry table growth
while an event dispatch is iterating an existing target. Dispatch must re-find
the target's listener list after callbacks run because adding listeners on new
targets can reallocate the entry table that stores `NodeListeners`.

`empty-network-resource.html` captures empty linked resources. Temporary network
read buffers for zero-byte files must be freed on the early-return path, and a
successful zero-byte HTTP response should be treated as an empty no-op resource.
Serve this directory on localhost, then run:

```sh
python3 -m http.server 8765 --bind 127.0.0.1
./lambda.exe view http://127.0.0.1:8765/empty-network-resource.html --headless
```

`element-scroll-method.html` captures pages that call `Element.scroll`,
`Element.scrollTo`, and `Element.scrollBy` before layout. These methods should
queue/update scroll state without crashing when no scroll pane exists yet.

`event-listener-onerror-reentry.html` captures listener exception reporting
while a page-owned `window.onerror` handler is present. Dispatch should report
and swallow the original listener failure without crashing the page load.

`optional-font-retry-storm.html` captures pages with many optional web fonts that
fail to load. Font fallback should happen promptly; each failed optional font
must not consume repeated network retry rounds before layout can continue.

`js-regex-wrapper-cache-leak.html` captures RegExp wrapper ownership during
browser-script batch cleanup. Cached compiled RegExp entries must release their
native wrapper state before shutdown so MEM_CAT_JS allocations do not leak.

`many-remote-font-faces-fallback.html` captures large `@font-face` source lists
with remote WOFF/WOFF2 files. CSS parsing should record the font family, but
layout startup must not synchronously download every source before browser font
fallback can choose a local substitute.

`onetrust-branch-closure-readback.html` captures closure environment tracking
across conditional/switch branches. A closure created inside one branch must not
leave its environment register as the later readback target after control-flow
merge.

`spring-onetrust-external-script.html` uses the local `otSDKStub.js` fixture,
trimmed from the Spring Framework docs OneTrust script. It covers the cached
function-declaration variant of the same branch-local closure readback bug.

`d3-observable-made-by-custom-element.html` uses the local
`observable-made-by.js` fixture from D3's docs. The bundle previously reached an
identifier-shaped AST node without a parsed name and crashed during LambdaJS MIR
lowering; Radiant should now log the nameless-node fallback and keep loading.
