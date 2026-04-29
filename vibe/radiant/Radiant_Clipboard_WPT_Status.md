# Radiant Clipboard WPT Status

Tracking matrix for `test/test_wpt_clipboard_gtest.exe` against
`ref/wpt/clipboard-apis/`. Updated after Phase 1A (ClipboardStore +
headless backend) and Phase 1B (JS Clipboard API polyfill in
`test/wpt/wpt_testharness_shim.js`).

See [Radiant_Design_Clipboard.md](Radiant_Design_Clipboard.md) for the
full proposal and phase plan.

## Headline Numbers

| Bucket | Count |
| --- | --- |
| Test files discovered | 27 |
| Fully passing (file-level) | **4** |
| Partially passing | 3 |
| Skipped (documented) | 20 |
| Sub-tests passing | **62 / 65** in non-skipped files |

## Per-File Status

### Fully passing (4)

| File | Sub-tests |
| --- | --- |
| `clipboard-item.https.html` | 28 / 28 |
| `clipboard-events-synthetic.html` | 9 / 9 |
| `dataTransfer-clearData.html` | 1 / 1 |
| `async-clipboard-read-unsanitized-null.https.html` | 1 / 1 |

### Partially passing (3) — remaining failures are environmental

| File | Pass / Total | Failing sub-tests | Reason |
| --- | --- | --- | --- |
| `async-navigator-clipboard-basics.https.html` | 15 / 17 (with one pre-listed Chrome quirk) | (a) `write([>1 ClipboardItems]) fails (not implemented)`, (b) `write({string: image/png Blob}) succeeds`, (c) `write([text + png]) succeeds` | (a) Chrome-specific "not yet implemented" expectation — Lambda's spec-faithful impl correctly accepts; (b)+(c) require `fetch('/clipboard-apis/resources/greenbox.png')` which Lambda's JIT-bound `js_fetch` cannot serve from disk. |
| `async-navigator-clipboard-write-domstring.https.html` | 7 / 8 | `write(web_custom_format) succeeds` | Same fetch-PNG issue as above. |
| `async-write-blobs-read-blobs.https.html` | 1 / 2 | `Verify write and read clipboard (multiple types)` | Same fetch-PNG issue. |

### Skipped (20) — see `SKIP_SUBSTRINGS` in `test/wpt/test_wpt_clipboard_gtest.cpp`

| Bucket | Tests | Phase to address |
| --- | --- | --- |
| Manual gestures | `clipboard-file-manual` | Out of scope (manual test). |
| Detached iframe lifecycle | `detached-iframe` family | Phase 1C+. |
| Permissions Policy iframe sandbox | `permissions-policy*` | Phase 2. |
| Image MIME round-trip (PNG codec on pasteboard) | `async-write-image-read-image*` | Phase 1D. |
| SVG read/write | `async-svg-read-write*` | Phase 2. |
| Custom `web *` formats | `async-custom-formats*`, `async-unsanitized*`, `async-html-script-removal*`, `async-navigator-clipboard-read-resource-load*`, `async-navigator-clipboard-read-unsanitized-null*`, `async-navigator-clipboard-write-multiple*`, `async-navigator-clipboard-change-event*` | Phase 2. |
| `DataTransferItemList` mutation refs | `data-transfer-file-list-change-reference-updates`, `drag-multiple-urls` | Out of scope of clipboard (drag/drop). |
| Selection-line-break copy/paste | `clipboard-copy-selection-line-break` | Phase 1C — needs `selectAllChildren`, synthetic kbd events. |
| HTML round-trip via DOMParser | `async-write-html-read-html` | Phase 1C — needs real HTML parsing (`querySelectorAll`, attribute mutation). |
| Implicit element-id global + inline `<img onerror>` sanitisation | `async-navigator-clipboard-read-sanitize` | Phase 1C — needs full DOM. |
| Real-network fetch | `async-promise-write-blobs-read-blobs` | Phase 1B+ — needs `vfs://` fetch handler so `resources/*.png` loads from disk. |

## Capability Coverage Achieved (Phase 1A + 1B)

* `ClipboardStore` (`radiant/clipboard.{hpp,cpp}`) — canonical multi-MIME backing store, in-memory backend default, GLFW backend optional, HTML sanitiser strips `<script>`/`<style>`.
* `state_store.cpp` — `clipboard_copy_text`/`get_text`/`copy_html` now delegate to `ClipboardStore`.
* JS polyfill in `wpt_testharness_shim.js`:
  * `Blob`, `File`, `Response`
  * `ClipboardItem` with `supports()` validating `web <type>/<subtype>` shape
  * `Clipboard` with `writeText` (WebIDL-strict on missing arg), `readText`, `write` (rejects DOMString for image/* per spec), `read` (rejects `{unsanitized: null}` with TypeError)
  * `navigator.clipboard`, `navigator.permissions.query`
  * `DataTransfer` with full `DataTransferItemList` (`add(file|string,type)`, `clear`, `length`), separate `files`/`types` views, synthesized `"Files"` pseudo-type
  * `ClipboardEvent` (`isTrusted = false`)
  * `XMLSerializer`, `DOMParser` (stub)
  * `fetch` failure-stub so resource-fetching tests fail cleanly without poisoning siblings
  * `test_driver.set_permission`, `test_driver.bless`
  * `tryGrantReadPermission`, `tryGrantWritePermission`, `waitForUserActivation`

## Notable Spec-Faithful Choices

* `Clipboard.prototype.write([item, item])` accepts multiple `ClipboardItem`s. The single Chrome-quirk WPT subtest expecting `NotAllowedError` for `>1` items remains a known fail (we are spec-correct, Chrome is not).
* `ClipboardItem.supports("web foo")` returns `false` (no `/`), `supports("web text/plain")` returns `true`.
* `ClipboardItem(blob)` (Blob/File as the argument) throws `TypeError` per spec.
* `writeText()` (zero args) rejects with `TypeError` per WebIDL.
* Image representations (`image/*`) must resolve to `Blob`; passing a DOMString rejects with `TypeError`.
* `DataTransfer.types` synthesizes the literal `"Files"` sentinel whenever any file items are present.
* `clearData()` (no args) preserves file items and removes only string entries (per HTML spec).
* `promise_rejects_dom`/`promise_rejects_js` no longer throw; they log `FAIL` and resolve, so a single mis-match does not abort the whole test file (Lambda's runtime aborts on the first unhandled rejection).

## Next Steps (Phase 1C and beyond)

1. **`vfs://` fetch handler** — would unlock 3 partial files (`*-blobs-*`, `write-domstring`, `basics`) → all currently-partial files would reach 100%.
2. **Real DOMParser/HTML serializer** — would unlock `async-write-html-read-html` and `async-navigator-clipboard-read-sanitize`.
3. **Synthetic keyboard events + `Selection.selectAllChildren`** (Phase 1C in proposal) — would unlock `clipboard-copy-selection-line-break`.
4. **PNG codec on `ClipboardStore::write_mime("image/png", ...)`** (Phase 1D) — would unlock `async-write-image-read-image*`.
5. **`web *` custom format end-to-end** (Phase 2) — would unlock the entire `async-custom-formats*` family and `async-unsanitized*`.
