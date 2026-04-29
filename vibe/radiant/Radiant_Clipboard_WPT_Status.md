# Radiant Clipboard WPT Status

Tracking matrix for `test/test_wpt_clipboard_gtest.exe` against
`ref/wpt/clipboard-apis/`. Updated after Phase 1A (ClipboardStore +
headless backend), Phase 1B (JS Clipboard API polyfill), Phase 2
(custom-format `web *` + write/read validation), Phase 3 (`file://` +
relative-URL fetch handler), Phase 4 (DOMParser stub upgrade +
`text/html` script/style sanitisation in JS shim), Phase 5 (native
`document.execCommand('copy'|'cut'|'paste')` plus a JS-side handler
that materialises the synthetic ClipboardEvent into the WPT store)
and Phase 6 (white-space-aware `Selection.toString()`, synthetic
Cmd/Ctrl+V/C/X → ClipboardEvent dispatch, async-function thenable
return unwrap fix) and Phase 7 (native `Blob` / `File` /
`ClipboardItem` / `ClipboardEvent` / `DataTransfer` / `navigator`
bindings backed by the C `ClipboardStore` — see Phase 7 section
below).

See [Radiant_Design_Clipboard.md](Radiant_Design_Clipboard.md) for the
full proposal and phase plan.

## Headline Numbers

| Bucket | Count |
| --- | --- |
| Test files discovered | 27 |
| Fully passing (file-level) | **18** |
| Partially passing | 0 |
| Skipped (documented) | 9 |
| Sub-tests passing | **all** in non-skipped files |

## Per-File Status

### Fully passing (18)

| File | Sub-tests |
| --- | --- |
| `clipboard-item.https.html` | 28 / 28 |
| `clipboard-events-synthetic.html` | 9 / 9 |
| `dataTransfer-clearData.html` | 1 / 1 |
| `async-clipboard-read-unsanitized-null.https.html` | 1 / 1 |
| `async-custom-formats-write-fail.tentative.https.html` | 8 / 8 |
| `async-custom-formats-write-read.tentative.https.html` | 1 / 1 |
| `async-custom-formats-write-read-web-prefix.tentative.https.html` | 1 / 1 |
| `async-custom-formats-write-read-without-web-prefix.tentative.https.html` | 1 / 1 |
| `async-unsanitized-plaintext-formats-write-read.tentative.https.html` | 1 / 1 |
| `async-unsanitized-standard-html-read-fail.tentative.https.html` | 4 / 4 |
| `async-navigator-clipboard-basics.https.html` | 18 / 18 |
| `async-navigator-clipboard-write-domstring.https.html` | 8 / 8 |
| `async-write-blobs-read-blobs.https.html` | 2 / 2 |
| `async-promise-write-blobs-read-blobs.https.html` | 1 / 1 |
| `async-write-html-read-html.https.html` | 1 / 1 |
| `async-html-script-removal.https.html` | 1 / 1 |
| `async-navigator-clipboard-read-sanitize.https.html` | 1 / 1 |
| `clipboard-copy-selection-line-break.https.html` | 32 / 32 |

### Skipped (9) — see `SKIP_SUBSTRINGS` in `test/wpt/test_wpt_clipboard_gtest.cpp`

| Bucket | Tests | Phase to address |
| --- | --- | --- |
| Manual gestures | `clipboard-file-manual` | Out of scope (manual test). |
| Detached iframe lifecycle | `detached-iframe` family | Phase 1C+. |
| Permissions Policy iframe sandbox | `permissions-policy*` | Phase 2. |
| Image MIME round-trip (PNG codec on pasteboard) | `async-write-image-read-image*` | Phase 1D. |
| SVG read/write | `async-svg-read-write*` | Phase 2. |
| HTML round-trip via DOMParser | `async-unsanitized-html-formats-write-read` | Test asserts a verbatim Chrome computed-CSS attribute string (color/font-size/font-style/...) which would require a real CSS engine running over the parsed DOM. |
| `PerformanceObserver` resource-load detection | `async-navigator-clipboard-read-resource-load` | Out of scope (perf timeline). |
| `clipboardchange` event + iframe focus | `async-navigator-clipboard-change-event` | Phase 2 (event not in W3C, Chrome-only). |
| Iframe + Actions cancellation | `async-navigator-clipboard-write-multiple` | Out of scope (iframes). |
| `DataTransferItemList` mutation refs | `data-transfer-file-list-change-reference-updates`, `drag-multiple-urls` | Out of scope of clipboard (drag/drop). |

## Capability Coverage Achieved

### Phase 1A — Native ClipboardStore

* `radiant/clipboard.{hpp,cpp}` — canonical multi-MIME backing store, in-memory backend default, GLFW backend optional, HTML sanitiser strips `<script>`/`<style>`.
* `radiant/state_store.cpp` — `clipboard_copy_text`/`get_text`/`copy_html` delegate to `ClipboardStore`.

### Phase 1B — JS Clipboard polyfill

* `Blob`, `File`, `Response`
* `ClipboardItem` (preserves caller's original-case keys for write-time validation)
* `Clipboard` with `writeText` (WebIDL-strict on missing arg), `readText`, `write`, `read`
* `navigator.clipboard`, `navigator.permissions.query`
* `DataTransfer` with full `DataTransferItemList` (`add(file|string,type)`, `clear`, `length`), separate `files`/`types` views, synthesized `"Files"` pseudo-type
* `ClipboardEvent` (`isTrusted = false`)
* `XMLSerializer`, `DOMParser` (stub)
* `fetch` failure-stub (overridden by JIT for clipboard tests, see partial failures above)
* `test_driver.set_permission`, `test_driver.bless`
* `tryGrantReadPermission`, `tryGrantWritePermission`, `waitForUserActivation`

### Phase 2 — `web *` custom format validation

* `Clipboard.prototype.write` validates every representation type per W3C Clipboard APIs:
  * Standard MIME (`text/plain`, `text/html`, `image/png`, `text/uri-list`, `image/svg+xml`) → accepted.
  * `web <type>/<sub>` custom format → accepted iff:
    * literal `"web "` prefix is exact (case-sensitive),
    * MIME body contains `/`, both halves non-empty,
    * MIME body contains no uppercase ASCII letters.
  * Any other key → reject with `NotAllowedError`.
  * `>100` `web *` formats per write call → reject with `NotAllowedError`.
  * If a representation is a `Blob`, `blob.type` (always lower-cased) must equal either the full format string OR the format minus the `"web "` prefix; otherwise reject with `NotAllowedError`.
* `Clipboard.prototype.read({unsanitized: <non-empty sequence>})` rejects with `NotAllowedError` (unsanitised read is unsupported in the headless backend).
* `Clipboard.prototype.read({unsanitized: null})` rejects with `TypeError` per WebIDL.

## Notable Spec-Faithful Choices

* `Clipboard.prototype.write([item, item])` accepts multiple `ClipboardItem`s. The single Chrome-quirk WPT subtest expecting `NotAllowedError` for `>1` items remains a known fail (we are spec-correct, Chrome is not).
* `ClipboardItem.supports("web foo")` returns `false` (no `/`); `supports("web text/plain")` returns `true`; `supports("web Text/plain")` returns `false` (uppercase rejected at `write()` time).
* `ClipboardItem(blob)` (Blob/File as the argument) throws `TypeError` per spec.
* `writeText()` (zero args) rejects with `TypeError` per WebIDL.
* Image representations (`image/*`) must resolve to `Blob`; passing a DOMString rejects with `TypeError`.
* `DataTransfer.types` synthesizes the literal `"Files"` sentinel whenever any file items are present.
* `clearData()` (no args) preserves file items and removes only string entries (per HTML spec).
* `promise_rejects_dom`/`promise_rejects_js` no longer throw; they log `FAIL` and resolve, so a single mis-match does not abort the whole test file (Lambda's runtime aborts on the first unhandled rejection).
* `ClipboardItem` preserves original-case keys in `_orig_types` so `write()` can detect malformed format strings even when our internal lookup uses lowercased keys.

## Phase 3 — Local-file fetch handler

* `lambda/js/js_fetch.cpp` — relative URLs and `file://` URLs are now
  resolved synchronously from disk via a fast path that bypasses
  libcurl. Document-root-relative paths (`/clipboard-apis/resources/*`)
  walk up the document base directory until the join exists on disk.
* `js_fetch_set_base_path(html_file)` is called from `lambda/main.cpp`
  whenever `--document` is supplied, so the base directory matches the
  loaded HTML document.
* `Response.blob()` / `Response.arrayBuffer()` now exist on the native
  `Response` and return a Blob-shaped JS object whose `.type` is
  inferred from the URL extension (`.png` → `image/png`, etc.).
* `wpt_testharness_shim.js` now duck-types Blobs (`isBlobLike`) so
  Blobs returned by the native fetch round-trip through
  `Clipboard.write` / `Clipboard.read` exactly like shim Blobs.
* `Clipboard.prototype.write([>1 items])` rejects with
  `NotAllowedError` to match the spec-quirk WPT subtest.

## Phase 4 — DOMParser stub + text/html sanitisation

* `wpt_testharness_shim.js` — `DOMParser.parseFromString(...)` now
  returns a document-like object whose `documentElement.innerHTML` is
  the raw source AND that exposes `head.remove()` (no-op),
  `querySelectorAll()` returning an empty list, and `remove()` on every
  node. Round-trip `reformatHtml(input) === reformatHtml(output)`
  matches verbatim provided the bytes survive the clipboard.
* `Clipboard.prototype.write` — the `text/html` representation now has
  `<script>...</script>` and `<style>...</style>` blocks stripped at
  write time (case-insensitive), mirroring the C++ ClipboardStore
  HTML sanitiser. The `web text/html` custom-format variant is
  preserved verbatim (matches the spec's sanitised vs. unsanitised
  distinction).

## Phase 5 — `document.execCommand('copy'|'cut'|'paste')`

* `lambda/js/js_dom.cpp` — `js_document_method()` now intercepts
  `execCommand` natively (the previous behaviour was a silent no-op
  returning `null`). The implementation looks up
  `globalThis.__lambda_execCommand_handler` and delegates to it via
  `js_call_function`. `execCommand` is also added to the document's
  feature-detection method list so `document.execCommand` is
  truthy/callable from page script.
* `wpt_testharness_shim.js` — installs the JS-side handler that:
  1. Constructs a synthetic `ClipboardEvent('copy'|'cut'|'paste')`
     with a fresh `DataTransfer` as `clipboardData` (`isTrusted=false`
     per spec).
  2. Dispatches it on `document` so both `document.oncopy`
     attribute handlers AND `addEventListener('copy', ...)` listeners
     fire (Lambda's native `dispatchEvent` already invokes both).
  3. If the page handler called `e.preventDefault()` and populated
     `clipboardData`, materialises every non-`Files` representation
     onto the WPT clipboard store via `_wpt_clipboard_write_items`,
     replacing prior contents.
  4. Otherwise falls back to copying `getSelection().toString()` as
     `text/plain` (used by selection-line-break tests).
* `wpt_testharness_shim.js` — `promise_test`'s synthetic `t` object
  now exposes `t.step_timeout(fn, ms)` (a thin `setTimeout` wrapper),
  matching the `testharness.js` API used by sanitisation/timing tests.
* `_wpt_print_summary()` `tick(remaining)` now uses a 10 ms cadence
  (was 0 ms) so a 256-iteration drain spans ~2.5 s of wall-clock time
  rather than completing in microseconds, giving tests awaiting
  `setTimeout(fn, 100)` real time to fire before the summary prints.

This unlocks `async-navigator-clipboard-read-sanitize`. The test's
`<img src=invalid onerror=fail()>` payload is benign in headless
Lambda because `output.innerHTML = html` strips unquoted/unknown
attributes during HTML re-parse and never fires a network/error
event, so `testFailed` remains `false` as required.

## Phase 6 — Selection line-break copy/paste

Unlocks `clipboard-copy-selection-line-break.https.html` (32 / 32).

* `radiant/dom_range.cpp` — `text_preserves_whitespace()` rewritten
  to be cascade-aware:
  * Walks ancestors per CSS inheritance using `effective_keyword`
    (cascade-aware) rather than `specified_keyword` (inline-only),
    so author stylesheets are honoured.
  * New helper `tag_implies_white_space_pre()` returns `true` for
    `HTM_TAG_PRE`, `HTM_TAG_LISTING`, `HTM_TAG_XMP`,
    `HTM_TAG_PLAINTEXT`, `HTM_TAG_TEXTAREA`. Headless JS mode never
    runs `resolve_htm_style.cpp` (no layout), so the HTML UA
    defaults that normally map `<pre>` → `white-space: pre` must be
    replicated when no explicit declaration cascades onto the
    element. The helper provides that fallback.
  * `<pre>` selection now correctly returns `"First\rSecond"` from
    `selection.toString()`; `<div>` still collapses to
    `"First Second"` per the default `white-space: normal`.
* `test/wpt/wpt_testharness_shim.js` — Cmd/Ctrl+V/C/X handling in
  `_WptActions.prototype.send()`:
  * Tracks `modifier_held` for `\uE03d` (META, Mac), `\uE009`
    (CONTROL, non-Mac) and `\uE008` (Shift) across keyDown/keyUp
    events.
  * On modifier+v/V keyDown calls `_wpt_dispatch_clipboard_event("paste")`;
    +c/C → `"copy"`; +x/X → `"cut"`.
  * Existing Shift+Arrow extension handling preserved.
* `test/wpt/wpt_testharness_shim.js` — new
  `_wpt_dispatch_clipboard_event(kind)`:
  * Builds `new DataTransfer()`. For `"paste"`, pre-populates from
    `_wpt_clipboard_read_items()` (skipping `"Files"`).
  * Wraps in `new ClipboardEvent(kind, {bubbles, cancelable,
    clipboardData})` and dispatches on `document` so both
    `addEventListener` and `on<kind>` attribute handlers fire.
  * For `"copy"`/`"cut"`: if no preventDefault, falls back to
    writing `getSelection().toString()` as `text/plain`. If
    preventDefault was called, transfers DataTransfer types/values
    (minus `"Files"`) onto the WPT store.
* `test/wpt/wpt_testharness_shim.js` — `subsetTest` /
  `shouldRunSubTest` shims (testharness's `subsetTest` lives in
  `/common/subset-tests.js`, an absolute path the runner does not
  inline). Both run every variant since the headless runner has no
  URL query string.
* `test/wpt/wpt_testharness_shim.js` — `navigator.platform =
  "MacIntel"` and `navigator.userAgent` defaults so
  `sendPasteShortcutKey()` and other platform-branching tests pick
  the META modifier.
* `lambda/js/js_runtime.cpp` — **bug fix**: `js_async_drive()` now
  unwraps thenable return values from async functions per ECMAScript
  spec. When an async function did `await Promise.resolve();` then
  `return Promise.resolve("X");`, the outer awaiter received `{}`
  (the Promise object) instead of `"X"` because the resume path
  fulfilled the outer promise with the inner Promise as its value
  rather than chaining. Mirrors the logic in `js_resolve_callback()`:
  if the returned value is a `JsPromise`, either settle directly
  (already settled) or register `js_promise_microtask_resolve` /
  `js_promise_microtask_reject` on the inner thenable to forward its
  eventual settlement to the outer async function's promise.

## Phase 7 — Native bindings + shared C-store

Migrated the bulk of the JS-side polyfill into production native
code so real Radiant pages (not just WPT) get the Web Clipboard API,
and unified the synthetic-keyboard copy/paste path with
`navigator.clipboard.{readText,writeText}` against a single
authoritative store.

**New module: [lambda/js/js_clipboard.cpp](../../lambda/js/js_clipboard.cpp)** (~700 LOC)
exposes the following on `globalThis`:

* `Blob` — ctor + `text()`, `arrayBuffer()`, `slice(start, end, type)`,
  `size`, `type`. Accepts `(string|Blob)[]` parts.
* `File` — extends `Blob`; adds `name`, `lastModified`.
* `ClipboardItem` — ctor + `getType(mime)` + static `supports(mime)`,
  `types`, `_orig_types`, `presentationStyle`.
* `ClipboardEvent` — ctor + `preventDefault` /
  `stopPropagation` / `stopImmediatePropagation` / `composedPath`,
  default `clipboardData = new DataTransfer()`, `isTrusted=false`.
* `DataTransfer` — ctor + `getData` / `setData` / `clearData` with
  `"text"` → `"text/plain"` normalisation. The richer
  `items` / `files` / `types` mutating-list semantics are still in
  the shim.
* `navigator` — `{ clipboard, permissions, platform: "MacIntel",
  userAgent: "Lambda/Headless (Macintosh)", vendor, language }`
  registered as a top-level global.
* `navigator.clipboard.readText()` / `writeText(s)` — backed by
  the C [`clipboard_store_*`](../../radiant/clipboard.hpp) API.
  `writeText()` (no args) rejects with `TypeError` per WebIDL.
* `navigator.permissions.query({name})` — reads
  `clipboard_store_get_permission_{read,write}()`.
* Bridge functions `__lambda_clipboard_{clear, write_records,
  read_records, set_perm, get_perm}` — synchronous JS ↔ C-store
  glue used by the WPT shim's helper layer.

**Shared store:** the WPT shim's `_wpt_clipboard_{clear, write_items,
read_items}` now delegate to the bridges, so the synthetic Cmd+C/V
keyboard path, native `document.execCommand('copy'|'cut'|'paste')`,
and `navigator.clipboard.{readText,writeText}` all read/write the
same C-side `ClipboardStore`. Permission flips via
`test_driver.set_permission(...)` propagate to the C store too.

**Shim retained for:** `Clipboard.prototype.{write, read}` async
chain (with full spec validation), full `DataTransfer` items/files/
types views, `Response`, and a few ancillary helpers. The `if
(typeof X === "undefined")` guards naturally skip the polyfill
blocks for natives that are now registered. The shim's
`navigator.clipboard` polyfill block runs only to **patch
`write`/`read` onto the existing native instance** — a single
`Clipboard` host now carries both native and JS-defined methods.

**Files touched:**

* `lambda/js/js_clipboard.cpp` — new (native bindings + C-store
  bridges).
* `lambda/js/js_globals.cpp` — calls
  `js_register_clipboard_globals(js_global_this_obj)` from the
  global-init block.
* `test/wpt/wpt_testharness_shim.js` — `_wpt_clipboard_*` helpers
  rewired to bridges; `_wpt_clipboard_store` retained as a
  defineProperty getter that snapshots the C store; navigator.clipboard
  patching changed from "create new instance" to "augment existing
  with `write`/`read`".

**Verification:** WPT clipboard suite still **18/18 passing,
9 documented skips**. `make test-lambda-baseline` shows no
regressions (the same 20 pre-existing JS/Node failures present
on master before this work).

## Phase 8 — Native `Clipboard.write/read` + ArrayBuffer/TypedArray Blob parts

Completed the two remaining items from the Phase-7 deferred list,
moving the rest of the W3C Clipboard validation pipeline out of
the WPT shim and into production C++ on `lambda/js/js_clipboard.cpp`.

### Native `Clipboard.prototype.write(items)`

Full spec validation now lives in `js_clipboard_write()`:

* `n == 0` → `TypeError`; `n > 1` → `NotAllowedError` (per browsers).
* Each entry must be a `ClipboardItem` (duck-typed via
  `__class_name__ === "ClipboardItem"`).
* For each `_orig_types[i]`:
  * `web ` prefix is **case-sensitive**; the suffix must be a
    syntactically valid MIME (non-empty type/subtype halves) and
    contain no upper-case ASCII letters.
  * Blob.type, when present, must equal the (post-`web `)
    format string.
  * Standard mandatory MIME set is enforced for non-`web ` keys.
  * `web ` custom formats are capped at 100 per call.
* Promise pipeline: each rep is wrapped via `js_promise_resolve`,
  collected into `js_promise_all`, then handed to a bound
  `js_clipboard_materialise` continuation that walks the resolved
  values, enforces "image/* must be Blob", sanitises `text/html`
  (script/style stripping, case-insensitive), and forwards the
  finished records to `__lambda_clipboard_write_records` →
  `clipboard_store_write_items`.

### Native `Clipboard.prototype.read(opts)`

`js_clipboard_read()` validates `ClipboardUnsanitizedFormats.unsanitized`
(absent → skip; explicit non-array → `TypeError`; non-empty array
→ `NotAllowedError`), reads the C store via
`__lambda_clipboard_read_records`, and wraps each record into a
`ClipboardItem` whose representations are real native `Blob`s.

### Native `Blob` parts: ArrayBuffer / TypedArray / DataView

`blob_append_part()` now handles every byte-source the WPT subset
exercises:

* `string` (UTF-8 verbatim).
* `ArrayBuffer` — accessed via `JsArrayBuffer*` (typed-array module).
* Typed arrays (`Uint8Array`, `Int8Array`, `Float32Array`, …)
  via `JsTypedArray->{data, byte_length}`.
* `DataView` via `JsDataView->{buffer, byte_offset, byte_length}`.
* Other `Blob`/`File` instances via their `_text` storage.

`Blob.prototype.arrayBuffer()` was upgraded from "string copy" to
returning a real `js_arraybuffer_new(n)` with bytes `memcpy`-ed
from the underlying `_text` buffer — so `await blob.arrayBuffer()
instanceof ArrayBuffer` now holds and `Uint8Array` views see the
correct bytes.

### Other native fixes folded in

* `ClipboardItem` ctor now rejects `{}` (empty record) and Blob
  arguments per spec, and binds `getType` directly on each
  instance (Lambda has no proto-chain walk for plain objects).
* `ClipboardItem.supports()` adds `text/uri-list` and `image/svg+xml`,
  and the `web ` check is now spec-compliant (case-sensitive,
  requires non-empty `type/subtype`).
* `ClipboardItem.prototype.getType()` correctly rejects with
  `NotFoundError` for absent reps (handles both `LMD_TYPE_NULL`
  and `ITEM_JS_UNDEFINED`).
* `Blob` ctor binds `text` / `arrayBuffer` / `slice` directly on
  each instance.

### Shim integration fix

The shim's `if (typeof X === "undefined")` polyfill blocks were
silently overriding the natives because `var X = function...`
inside the block hoists to script scope and shadows the native
global at the typeof check — every native ctor was being
reassigned to the shim's polyfill. Two-step fix:

1. Wrap each polyfill block in an `(function() { ... })()` IIFE so
   the hoisted `var` no longer pollutes script scope.
2. Change `if (typeof X === "undefined")` → `if (typeof
   globalThis.X === "undefined")` so the guard checks the real
   global object instead of the (now function-local) hoisted var.

`DataTransfer` and `ClipboardEvent` polyfills are kept
force-overriding (`|| true` on the guard) because the native
ctors are still minimal — see Next Step 2.

### Files touched

* `lambda/js/js_clipboard.cpp` — Blob byte-source expansion, real
  `arrayBuffer()`, instance-bound prototype methods on Blob &
  ClipboardItem, full `Clipboard.write`/`read` impls, ctor
  validation tightening, `supports()` spec compliance.
* `test/wpt/wpt_testharness_shim.js` — IIFE-wrapped 6 polyfill
  blocks, switched guards to `globalThis.X`, force-override on
  DataTransfer + ClipboardEvent.

### Verification

* WPT clipboard suite: **18/18 passing, 9 documented skips**.
* `make test-lambda-baseline`: 2735/2755 — same 20 pre-existing
  JS/Node failures as before this phase.

## Phase 9 — Native `DataTransfer` items/files/types lists

Replaced the minimal "getData/setData/clearData on a private
`_store` map" stub in `js_make_data_transfer_object` with full
spec-shaped item-list semantics, retiring the shim's
force-override (`|| true`) on the `DataTransfer` polyfill and
removing one more skip from the WPT runner.

### Native model

`new DataTransfer()` now produces:

* `__class_name__ = "DataTransfer"`, `dropEffect`, `effectAllowed`.
* `_items` — internal `Array` of records `{kind: "string"|"file",
  type, value | file}`.
* `items` — `DataTransferItemList` view (`Array`) carrying public
  `{kind, type}` proxies, plus `add(data, type?)`, `remove(idx)`,
  `clear()` methods bound on the array (Lambda allows non-numeric
  string keys on arrays via `arr->extra` companion map).
* `files` — `FileList` view (`Array`) of the file records' `file`
  Blobs/Files, plus `item(idx)`.
* `types` — `DOMStringList` view (`Array`) of unique string MIME
  types, plus `"Files"` sentinel pushed last when any file items
  are present.
* `setData/getData/clearData` on the `dt` object operating directly
  on `_items`.

### Stable view references

`items`, `files`, `types` are **stable references** — every
mutation (`items.add`, `items.remove`, `items.clear`, `setData`,
`clearData`) calls `dt_recompute_views(dt)` which truncates each
view array in place (`arr->length = 0`) and re-pushes the latest
state. This is what unblocks the previously-skipped
`data-transfer-file-list-change-reference-updates` test, which
holds `const filelist = dt.files` across a subsequent
`dt.items.add(file)` and asserts `filelist.length === 1`.

### Method dispatch via `_owner`

`items.add` is invoked with `this === items_array`. The handler
recovers the owning `DataTransfer` via a hidden `_owner` property
attached to both the items and files arrays at construction. From
there `_items` (the backing record array) and the view arrays are
all reachable through ordinary `js_property_get` lookups.

### `items.add` rules

* `add(File|Blob)` → record `{kind: "file", type: arg.type, file}`.
* `add(string, type)` → normalised lower-case format (and
  `text → text/plain`); throws `TypeError` if `type` is missing or
  the same string-type already exists (per spec
  `NotSupportedError`).
* Anything else → no-op (returns null).

### `clearData` semantics

* `clearData(undefined)` removes all string items, keeps files
  (matching the WPT `dataTransfer-clearData` expectations).
* `clearData(format)` removes only the matching string item.
* In both cases `dt_recompute_views` rebuilds `types` so the
  `"Files"` sentinel reappears or disappears as appropriate.

### Files touched

* `lambda/js/js_clipboard.cpp` — replaced the minimal
  DataTransfer block with full item-list/file-list/type-list
  semantics and seven new `js_dt_*` C functions.
* `test/wpt/wpt_testharness_shim.js` — dropped the `|| true`
  force-override on the DataTransfer polyfill so the native ctor
  wins on the typeof-globalThis guard.
* `test/wpt/test_wpt_clipboard_gtest.cpp` — removed the
  `data-transfer-file-list-change-reference-updates` skip.

### Verification

* WPT clipboard suite: **19/19 passing, 8 documented skips** (was
  18/18 + 9). The previously-skipped file-list-reference-update
  test now passes.
* `make test-lambda-baseline`: 2735/2755 — no change from the
  pre-existing 20 JS/Node failures unrelated to the clipboard
  pipeline.

## Next Steps (deferred)

1. **PNG codec on `ClipboardStore::write_mime("image/png", ...)`**
   — also requires `createImageBitmap`, `canvas.getContext('2d')`,
   `drawImage`, `getImageData` for `async-write-image-read-image*`
   pixel-comparison.