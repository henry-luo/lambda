# Radiant Design — Full WPT-Conformant Clipboard Support

Status: Proposal
Owner: Radiant
Related: [Radiant_Design_Selection.md](Radiant_Design_Selection.md), [Radiant_Form2.md](Radiant_Form2.md), [Radiant_JS_Integration3.md](Radiant_JS_Integration3.md)
WPT suite: [`ref/wpt/clipboard-apis/`](../../ref/wpt/clipboard-apis)

---

## 1. Motivation

Today Radiant has only a *minimal* clipboard surface:

| Layer | What exists | File |
|-------|-------------|------|
| Native | `glfwSetClipboardString` / `glfwGetClipboardString` plain-text only | [radiant/state_store.cpp](../../radiant/state_store.cpp) (`clipboard_copy_text`, `clipboard_get_text`, `clipboard_copy_html` — falls back to text) |
| Event loop | Hard-coded Cmd+C / Cmd+X / Cmd+V handling for `<input>` and `<textarea>` | [radiant/event.cpp](../../radiant/event.cpp#L3762-L3835), [radiant/event.cpp](../../radiant/event.cpp#L4132-L4151) |
| JS DOM | None — `navigator.clipboard`, `ClipboardEvent`, `DataTransfer.clipboardData`, and `document.execCommand('copy'\|'cut'\|'paste')` are all absent |
| WPT coverage | 0% — `ref/wpt/clipboard-apis/` (~36 tests) is not exercised |

The W3C [Clipboard APIs](https://w3c.github.io/clipboard-apis/) spec defines two cooperating surfaces that we need to implement end-to-end so that real-world editors, paste handlers, and the WPT `clipboard-apis` suite work in Radiant:

1. **Sync clipboard events + `execCommand`** — the legacy DOM surface (`copy` / `cut` / `paste` events with a `clipboardData` `DataTransfer`, plus `document.execCommand('copy'|'cut'|'paste')`).
2. **Async Clipboard API** — `navigator.clipboard.{read,write,readText,writeText}` returning Promises of `ClipboardItem[]`, with permission gating and user-activation requirements.

Both surfaces ultimately read/write the **same OS pasteboard**, so they must share a single canonical store inside Radiant.

---

## 2. Goals & Non-Goals

### Goals
- 100 % spec coverage for everything in [`ref/wpt/clipboard-apis/`](../../ref/wpt/clipboard-apis) that does not require Permissions Policy iframe sandboxing or true OS-level synthetic input injection.
- A single in-process `ClipboardStore` with multi-MIME items, mirrored to the OS pasteboard.
- Sync `copy` / `cut` / `paste` events firing on the right targets per spec, with a fully functional `event.clipboardData`.
- `document.execCommand('copy'|'cut'|'paste')` working on the focused editing host / form control.
- `navigator.clipboard.*` Promise API gated by user activation + Permissions API stubs.
- A new GTest (`test_wpt_clipboard_gtest.exe`) that discovers and runs every `*.html` under `ref/wpt/clipboard-apis/` against `lambda.exe js --document`, exactly like [test_wpt_selection_gtest.cpp](../../test/wpt/test_wpt_selection_gtest.cpp).

### Non-Goals (Phase 1)
- Cross-origin Permissions Policy enforcement (no iframe in current `js --document` runner).
- Native rich HTML / image clipboard interop on **Linux X11** (kept as plain-text fallback; macOS + Windows get full `text/html` and `image/png`).
- Drag-and-drop — uses a related but distinct `DataTransfer` lifecycle; tracked separately in [Radiant_Design_Selection2.md](Radiant_Design_Selection2.md).
- Custom format unsanitized read/write where the OS pasteboard format is not addressable (the `*.tentative.https.html` tests are accepted as **expected-fail** in Phase 1).

---

## 3. WPT Surface Inventory

Quick categorisation of the 36 files under `ref/wpt/clipboard-apis/` (drives the implementation backlog):

| Category | Example | Capability needed |
|----------|---------|-------------------|
| IDL | `idlharness.https.window.js` | `Clipboard`, `ClipboardItem`, `ClipboardEvent` interfaces in `js/dom/` |
| `navigator.clipboard` basics | `async-navigator-clipboard-basics.https.html` | `navigator.clipboard` instance + Promise plumbing |
| Read / write text | `async-navigator-clipboard-write-domstring.https.html` | `writeText(string)` → OS pasteboard |
| Blob round-trip | `async-write-blobs-read-blobs.https.html` | `Blob`, `ClipboardItem` ctor accepting `{[type]: Blob}` |
| HTML round-trip | `async-write-html-read-html.https.html` | `text/html` MIME on pasteboard + sanitiser |
| Image round-trip | `async-write-image-read-image.https.html` | `image/png` decoder/encoder bridged to pasteboard |
| Sanitiser | `async-navigator-clipboard-read-sanitize.https.html` | HTML sanitiser on read (drop scripts/styles) |
| Custom formats | `async-custom-formats-write-read*.tentative.html` | `web ` prefix + raw MIME plumbing (Phase 2) |
| Sync events | `clipboard-events-synthetic.html`, `dataTransfer-clearData.html` | `ClipboardEvent` + `DataTransfer` on `copy`/`cut`/`paste` |
| Selection-driven copy | `clipboard-copy-selection-line-break.https.html` | DOM `Selection` → serialised text/html (depends on Selection design, already in flight) |
| Permissions | `permissions/`, `permissions-policy/` | `navigator.permissions.query({name:'clipboard-read'\|'clipboard-write'})` |
| Detached frame | `detached-iframe/` | Lifecycle: invalid `Document` rejects clipboard ops |

The full per-test status table will live in `vibe/radiant/Radiant_Clipboard_WPT_Status.md` once the test runner is landed and produces a baseline report.

---

## 4. Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  JS DOM (lambda/js/)                                        │
│  ┌──────────────────┐  ┌────────────────┐  ┌──────────────┐ │
│  │ navigator.       │  │ document       │  │ Clipboard    │ │
│  │   clipboard.*    │  │  .execCommand  │  │   Event      │ │
│  │   (async)        │  │   ('copy'…)    │  │ + Data       │ │
│  │                  │  │   (sync)       │  │   Transfer   │ │
│  └────────┬─────────┘  └───────┬────────┘  └──────┬───────┘ │
└───────────┼────────────────────┼──────────────────┼─────────┘
            │                    │                  │
            ▼                    ▼                  ▼
┌─────────────────────────────────────────────────────────────┐
│  Radiant ClipboardStore  (radiant/clipboard.{hpp,cpp})      │
│  - canonical multi-MIME item                                │
│  - permission state per origin                              │
│  - sanitiser registry                                       │
│  - dispatches ClipboardEvent on selection / focus host      │
└─────────────────────────┬───────────────────────────────────┘
                          │ platform abstraction
        ┌─────────────────┼──────────────────┐
        ▼                 ▼                  ▼
   GLFW/plain-text   NSPasteboard        Win32 OLE
   (fallback)        (mac)               clipboard
```

### 4.1 New core module: `radiant/clipboard.{hpp,cpp}`

```cpp
// radiant/clipboard.hpp  (sketch)
namespace radiant {

struct ClipboardEntry {
    Str          mime;     // e.g. "text/plain", "text/html", "image/png", "web text/custom"
    StrBuf       data;     // raw bytes (utf-8 for text/*, binary otherwise)
};

struct ClipboardItem {
    ArrayList    entries;  // ClipboardEntry list — one per representation
    bool         is_unsanitized;
};

class ClipboardStore {
public:
    static ClipboardStore* instance();

    // canonical store (mirrored to OS pasteboard on write)
    void                  write_items(ArrayList* items);
    const ArrayList*      read_items() const;
    void                  clear();

    // MIME helpers used by both sync + async paths
    Str                   read_text(const char* mime) const;          // "text/plain", "text/html"
    void                  write_text(const char* mime, Str text);

    // sanitisation pluggable per MIME (HTML strips <script>/<style>/event attrs)
    Str                   sanitize(const char* mime, Str raw) const;

    // platform bridge
    void                  pull_from_os();   // sync OS → store before async read()
    void                  push_to_os();     // sync store → OS after write()
};

} // namespace radiant
```

Existing `clipboard_copy_text` / `clipboard_get_text` / `clipboard_copy_html` are reimplemented on top of `ClipboardStore` so the form-control Cmd+C/X/V paths in `event.cpp` keep working unchanged.

### 4.2 Platform back-ends

| Platform | text/plain | text/html | image/png | File |
|----------|-----------|-----------|-----------|------|
| macOS  | `NSPasteboard` `NSPasteboardTypeString` | `NSPasteboardTypeHTML` | `NSPasteboardTypePNG` | new `radiant/clipboard_mac.mm` |
| Linux X11 | `glfwSetClipboardString` (today) | extend via XCB `XA_STRING` + `text/html` target | KIV (Phase 2) | new `radiant/clipboard_linux.cpp` |
| Windows | `OpenClipboard` + `CF_UNICODETEXT` | `CF_HTML` (Microsoft HTML Format header) | `CF_DIB` | new `radiant/clipboard_win.cpp` |
| Headless (`lambda.exe js --document`) | in-memory only — no OS bridge | same | same | `radiant/clipboard_headless.cpp` (default for tests) |

The headless backend is what the new gtest exercises — the OS pasteboard is replaced by an in-process `ClipboardStore` so tests are deterministic.

### 4.3 JS bindings (`lambda/js/dom/clipboard.cpp`)

New JS interfaces (added to the JS DOM type registry — same pattern as `Range` / `Selection` in [Radiant_Design_Selection.md](Radiant_Design_Selection.md)):

```
[Exposed=Window] interface Clipboard : EventTarget {
    Promise<sequence<ClipboardItem>> read();
    Promise<DOMString>               readText();
    Promise<undefined>               write(sequence<ClipboardItem> data);
    Promise<undefined>               writeText(DOMString data);
};

[Exposed=Window] interface ClipboardItem {
    constructor(record<DOMString, (Blob or DOMString or Promise<(Blob or DOMString)>)> items,
                optional ClipboardItemOptions options = {});
    readonly  attribute FrozenArray<DOMString> types;
    Promise<Blob> getType(DOMString type);
    static boolean supports(DOMString type);
};

[Exposed=Window] interface ClipboardEvent : Event {
    constructor(DOMString type, optional ClipboardEventInit eventInitDict = {});
    readonly attribute DataTransfer? clipboardData;
};
```

Plus extend the existing `DataTransfer` (used by drag-and-drop) so it serves both contexts: `getData(type)`, `setData(type, data)`, `clearData(type?)`, `items`, `types`, `files`.

### 4.4 Sync event flow (`copy` / `cut` / `paste`)

Per [spec § The clipboard event handlers](https://w3c.github.io/clipboard-apis/#clipboard-event-interfaces):

1. User triggers gesture (Cmd+C, context-menu Copy, or `execCommand('copy')`).
2. Radiant computes the **clipboard target**: focused editable element, else current `Selection` anchor’s containing element, else `body`.
3. Construct `ClipboardEvent('copy', {clipboardData: new DataTransfer()})` with the *current* selection serialised into `text/plain` and `text/html` entries.
4. Dispatch on the target. Default action = write `clipboardData`’s entries to `ClipboardStore`. If the handler called `event.preventDefault()`, **do not** auto-populate — use only what the handler put into `clipboardData`.
5. After dispatch returns, push `ClipboardStore` → OS pasteboard.

`paste` is the mirror: pull OS → store → build a populated `DataTransfer` → dispatch → if not `preventDefault`'d, perform the default insertion into the editing host.

### 4.5 Async API flow

```
navigator.clipboard.writeText("hi")
  → check user activation (per spec; relaxed in headless test mode via env var)
  → check permission state for "clipboard-write" (default: granted in headless)
  → ClipboardStore.write_text("text/plain", "hi")  → push to OS
  → resolve Promise<undefined>

navigator.clipboard.read()
  → permission check ("clipboard-read")
  → ClipboardStore.pull_from_os()
  → wrap each entry into a JS ClipboardItem with lazy Blob promises
  → resolve Promise<[ClipboardItem]>
```

### 4.6 Permissions

Add a tiny `navigator.permissions.query({name})` returning `{state: 'granted'|'denied'|'prompt'}` from a per-origin map seeded by an `--allow-clipboard` runner flag (default `granted` for headless tests, matching how WPT itself uses `test_driver.set_permission`).

---

## 5. Phased Delivery

| Phase | Scope | Acceptance |
|-------|-------|------------|
| **1A** | `ClipboardStore` + headless backend; refactor `clipboard_copy_text`/`get_text`/`copy_html` onto it; preserve all existing form-control behaviour | All current Lambda + Radiant baseline tests still pass |
| **1B** | JS bindings for `Clipboard`, `ClipboardItem`, `ClipboardEvent`, `DataTransfer`; `navigator.clipboard.{readText,writeText,read,write}`; `navigator.permissions.query` stub | All `text-write-read/` and `async-navigator-clipboard-basics` tests pass |
| **1C** | Sync `copy`/`cut`/`paste` event dispatch + `document.execCommand('copy'\|'cut'\|'paste')`; HTML sanitiser | `clipboard-events-synthetic.html`, `dataTransfer-clearData.html`, `clipboard-copy-selection-line-break.https.html` pass |
| **1D** | `text/html` + `image/png` round-trip on macOS NSPasteboard; same on Windows `CF_HTML`/`CF_DIB` | `async-write-html-read-html.https.html`, `async-write-image-read-image.https.html` pass on mac/win |
| **2**  | Custom format prefix (`web *`), unsanitized read, change-event, full Linux rich-MIME | Remaining `*.tentative.https.html` |

---

## 6. Risks & Open Questions

1. **User-activation requirement**. WPT relies on `testdriver.js` (`test_driver.bless`, `test_driver.click`) to satisfy this. Our shim already stubs `test_driver` — we will extend it with `set_permission` + `bless` to flip an internal "user-activated" flag for the duration of one task.
2. **`testharnessreport.js`** is not currently shimmed. Most clipboard tests load it; today it’s silently dropped by [test_wpt_selection_gtest.cpp](../../test/wpt/test_wpt_selection_gtest.cpp) (it’s a `/resources/` path). That’s fine — our `_wpt_print_summary()` plays the same role.
3. **HTML sanitiser**. We can reuse the HTML parser in `lambda/input/input-html.cpp` and the formatter in `lambda/format/format-html.cpp` — strip `<script>`, `<style>`, `on*` attributes, `javascript:` URLs.
4. **Image MIME**. PNG encode/decode must come from a library available in the build (libpng or stb_image). Defer to Phase 1D.
5. **Promise plumbing**. Async API requires Promise + microtask support in the JS runtime. Confirm with `lambda/js/` owner that this is wired (`Promise.resolve` / `.then` are in use elsewhere — yes).

---

## 7. Test Strategy

### 7.1 New gtest

A new parameterised gtest, [`test/wpt/test_wpt_clipboard_gtest.cpp`](../../test/wpt/test_wpt_clipboard_gtest.cpp), discovers every `*.html` (and `*.js` window-test) under `ref/wpt/clipboard-apis/`, applies the standard pipeline:

1. extract inline `<script>` + inline allowlisted local helpers (`resources/user-activation.js` etc.);
2. prepend `wpt_testharness_shim.js` (extended with `Promise`-aware `promise_test`, `test_driver.set_permission`, `test_driver.bless`, headless `Blob`/`ClipboardItem` mock when JS bindings are not yet available);
3. run with `lambda.exe js <tmp>.js --document <html>`;
4. parse `WPT_RESULT: N/M` and emit `FAIL: <name>` lines as `ADD_FAILURE`.

A skip-list mirrors the selection runner’s pattern (e.g. `clipboard-file-manual.html` requires interactive input; `detached-iframe/` requires real iframe lifecycle).

Until Phase 1B lands every test will fail at "navigator.clipboard is undefined" — the gtest exists as a **tracking baseline**, exactly like `test_wpt_selection_gtest` originally was.

### 7.2 Build wiring

Add to `build_lambda_config.json` next to the selection runner:

```json
{
  "source": "test/wpt/test_wpt_clipboard_gtest.cpp",
  "name": "WPT Clipboard API Tests",
  "category": "extended",
  "binary": "test_wpt_clipboard_gtest.exe",
  "libraries": ["gtest"],
  "requires_lambda_exe": true,
  "icon": "📋"
}
```

Then `make build-test && ./test/test_wpt_clipboard_gtest.exe` produces a per-test pass/fail matrix. Progress per phase is measured by the delta in `WPT_RESULT` totals.

### 7.3 Per-phase exit criterion

Phase N is "done" when its target subset of `clipboard-apis/` tests reports `WPT_RESULT: N/N` with zero `FAIL:` lines. A small allowlist of expected-skip tests is maintained in the gtest source.

---

## 8. File Touch List (summary)

| Action | File |
|--------|------|
| **NEW** | `radiant/clipboard.hpp`, `radiant/clipboard.cpp` |
| **NEW** | `radiant/clipboard_mac.mm`, `radiant/clipboard_linux.cpp`, `radiant/clipboard_win.cpp`, `radiant/clipboard_headless.cpp` |
| **NEW** | `lambda/js/dom/clipboard.cpp` (`Clipboard`, `ClipboardItem`, `ClipboardEvent`, `DataTransfer` extensions) |
| **NEW** | `lambda/js/dom/permissions.cpp` (`navigator.permissions.query` stub) |
| **NEW** | `test/wpt/test_wpt_clipboard_gtest.cpp` |
| **EDIT** | `radiant/state_store.cpp` — reimplement `clipboard_*` on `ClipboardStore` |
| **EDIT** | `radiant/event.cpp` — fire `ClipboardEvent` on Cmd+C/X/V before/after the existing form-control path; honour `preventDefault` |
| **EDIT** | `test/wpt/wpt_testharness_shim.js` — add `promise_test`, `test_driver.set_permission`/`.bless`, minimal `Blob` polyfill |
| **EDIT** | `build_lambda_config.json` — register `test_wpt_clipboard_gtest.exe` |
| **EDIT** | `lambda/js/dom/document.cpp` — wire `document.execCommand('copy'\|'cut'\|'paste')` through `ClipboardStore` |
