# Radiant Webview: Embedding Native Web Views as Inline Components

## Goal

Allow a native OS web view (WKWebView / WebView2 / WebKitGTK) to be nested inside a Radiant page as an inline component — analogous to `<iframe>` in HTML. The web view provides full HTML/CSS/JS support (async, ES modules, canvas, WebGL, etc.) for content that exceeds Radiant's subset, while Radiant continues to own the surrounding page layout, rendering, and event dispatch.

## Motivation

Radiant implements a growing but incomplete subset of web standards. Certain content categories are impractical to support natively:

- **Complex JavaScript**: async/await, Promises, ES modules, Web Workers
- **Canvas / WebGL**: 2D/3D drawing APIs
- **Third-party widgets**: Maps, charts, code editors, video players
- **Web APIs**: fetch, WebSocket, IndexedDB, Web Audio, etc.

Rather than reimplementing the entire web platform, Radiant can delegate these to the OS-provided web engine and composite the result into its own layout. This is the same strategy browsers use for `<iframe>` with out-of-process rendering, and what frameworks like Electron/Tauri use at the application level — but scoped to individual elements within a Radiant page.

## Two Rendering Modes

The web view supports two rendering modes, each with distinct trade-offs:

| | **Child Window Mode** (default) | **Offscreen Layer Mode** |
|---|---|---|
| **Rendering** | Native OS child window overlaid on GLFW window | Offscreen → RGBA snapshot → OpenGL texture |
| **Performance** | Full native web engine perf (GPU-composited) | Snapshot overhead; best for static/infrequent updates |
| **Z-order** | Always on top — Radiant content **cannot** overlay | Correct z-order — Radiant content can overlay freely |
| **Events** | OS delivers events directly to the child window | Radiant must forward events via injection |
| **Use cases** | Interactive apps, dynamic content, animations | Overlaid UI, static dashboards, `render` to PNG/PDF |
| **Library** | `webview/webview` (cross-platform wrapper) | Direct platform APIs (WKWebView, WebView2, WebKitGTK) |

### HTML Attribute

```html
<!-- Default: child window mode (best perf, always-on-top) -->
<webview src="chart.html" width="600" height="400"></webview>

<!-- Offscreen layer mode (correct z-order, composited as texture) -->
<webview src="chart.html" mode="layer" width="600" height="400"></webview>
```

The `mode` attribute selects the rendering strategy:
- `mode="window"` (default) — child window, native performance
- `mode="layer"` — offscreen rendering, compositable as a layer

### Child Window Mode (default)

```
┌──────────────────────────────────────────────────────┐
│  GLFW + OpenGL window                                │
│                                                      │
│  ┌────────────────────────────────────────────────┐  │
│  │ <h1>Dashboard</h1>  (Radiant-rendered)         │  │
│  └────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────┐  │
│  │ <webview src="chart.html">                     │  │
│  │  ╔════════════════════════════════════════════╗ │  │
│  │  ║  Native child window (WKWebView/WebView2) ║ │  │
│  │  ║  Full perf, always on top of OpenGL        ║ │  │
│  │  ╚════════════════════════════════════════════╝ │  │
│  └────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────┐  │
│  │ <p>Footer</p>  (Radiant-rendered)              │  │
│  └────────────────────────────────────────────────┘  │
│                                                      │
└──────────────────────────────────────────────────────┘
```

The web view is a **platform-native child window** overlaid on the GLFW window at the exact position and size determined by Radiant's layout engine. The OS web engine handles rendering and input directly. Radiant owns layout and positioning; the OS owns everything inside the web view.

**Limitation**: Native child windows always render on top of the OpenGL surface. Radiant-rendered dropdowns, tooltips, or positioned elements **cannot** appear above the web view.

### Offscreen Layer Mode (`mode="layer"`)

```
┌──────────────────────────────────────────────────────┐
│  GLFW + OpenGL window                                │
│                                                      │
│  Layer 0 (Radiant):  <h1>Dashboard</h1>             │
│  Layer 1 (WebView):  <webview mode="layer">          │
│                       ┌────────────────────────┐     │
│                       │ OS web engine renders  │     │
│                       │ offscreen → RGBA buf   │     │
│                       │ → OpenGL texture       │     │
│                       └────────────────────────┘     │
│  Layer 2 (Radiant):  <div class="overlay">...</div> │
│  Layer 3 (Radiant):  <p>Footer</p>                  │
│                                                      │
└──────────────────────────────────────────────────────┘
```

The web view renders **offscreen** into an RGBA bitmap, which Radiant uploads as an OpenGL texture and composites at the correct z-order during its render pass. Radiant-rendered content can appear both behind and in front of the web view.

**Limitation**: Snapshot capture has latency overhead. Best for semi-static content. Not ideal for 60fps animations or heavy user interaction (event injection is less responsive than native input delivery).

## HTML Syntax

```html
<!-- Basic usage — loads a URL -->
<webview src="https://example.com/chart.html" width="600" height="400"></webview>

<!-- Inline HTML content -->
<webview srcdoc="<h1>Hello from web view</h1>" style="width: 100%; height: 300px;"></webview>

<!-- With IPC bridge -->
<webview id="editor" src="lambda://editor/index.html"
         style="flex: 1; min-height: 200px;"
         sandbox="allow-scripts allow-ipc"></webview>
```

### Attributes

| Attribute   | Description |
|-------------|-------------|
| `src`       | URL to load (http/https/lambda://) |
| `srcdoc`    | Inline HTML string |
| `mode`      | Rendering mode: `window` (default, child window) or `layer` (offscreen compositing) |
| `sandbox`   | Permission flags: `allow-scripts`, `allow-ipc`, `allow-navigation`, `allow-popups` |
| `preload`   | Lambda script to inject before page load (for IPC bridge setup) |

The `<webview>` tag participates in normal CSS layout — it behaves like a replaced block element (like `<img>` or `<iframe>`), with default intrinsic size 300×150 (matching iframe defaults).

## Architecture

### Component Layers

```
┌──────────────────────────────────────────────────────────────────┐
│ 1. HTML Parser                                                   │
│    Recognize <webview> tag → DomElement(HTM_TAG_WEBVIEW)         │
├──────────────────────────────────────────────────────────────────┤
│ 2. Layout Engine                                                 │
│    Layout as replaced block element (like <iframe>)              │
│    Compute position (x, y) and size (width, height)              │
├──────────────────────────────────────────────────────────────────┤
│ 3. WebView Manager                                               │
│    Create/destroy/reposition web view instances                   │
│    Mode dispatch: child-window vs offscreen-layer                │
│    Map ViewBlock → native web view handle                        │
├──────────────────────────────────────────────────────────────────┤
│ 4A. Child Window Backend (mode="window", via webview/webview)    │
│    macOS: WKWebView as NSView child of glfwGetCocoaWindow()     │
│    Linux: WebKitGTK child of glfwGetX11Window()                  │
│    Windows: WebView2 child of glfwGetWin32Window()               │
├──────────────────────────────────────────────────────────────────┤
│ 4B. Offscreen Layer Backend (mode="layer", direct platform APIs) │
│    macOS:   WKWebView (hidden) + takeSnapshot → RGBA            │
│    Linux:   WebKitGTK + get_snapshot → cairo → RGBA              │
│    Windows: WebView2 CompositionController + CapturePreview      │
├──────────────────────────────────────────────────────────────────┤
│ 5. Renderer                                                      │
│    Window mode: position native child window over OpenGL surface │
│    Layer mode:  composite RGBA texture at correct z-order        │
├──────────────────────────────────────────────────────────────────┤
│ 6. Event Handling                                                │
│    Window mode: OS delivers events to child window directly      │
│    Layer mode:  Radiant hit-test → translate → inject into WV    │
├──────────────────────────────────────────────────────────────────┤
│ 7. IPC Bridge (optional, both modes)                             │
│    JS → Lambda: window.__lambda.invoke(cmd, payload)             │
│    Lambda → JS: webview_eval_js(id, code)                        │
└──────────────────────────────────────────────────────────────────┘
```

### Data Structures

Extend existing `EmbedProp` (or add a sibling field on `ViewBlock`):

```cpp
// radiant/webview.h

typedef struct WebViewHandle WebViewHandle;  // opaque, platform-specific

enum WebViewMode {
    WEBVIEW_MODE_WINDOW = 0,  // child window (default) — native perf, always on top
    WEBVIEW_MODE_LAYER  = 1,  // offscreen layer — compositable, correct z-order
};

typedef struct WebViewProp {
    WebViewHandle* handle;       // native web view instance
    WebViewMode mode;            // window or layer
    char* src;                   // URL or NULL
    char* srcdoc;                // inline HTML or NULL
    char* preload_script;        // JS to inject before page load
    uint32_t sandbox_flags;      // bitmask: SANDBOX_ALLOW_SCRIPTS, etc.

    // child window mode fields
    float last_x, last_y;       // last positioned coords (CSS pixels)
    float last_w, last_h;       // last positioned size (CSS pixels)

    // offscreen layer mode fields
    ImageSurface* surface;       // RGBA bitmap captured from web view (layer mode only)
    bool dirty;                  // content changed, needs re-snapshot (layer mode only)
    uint64_t last_snapshot_ms;   // timestamp of last RGBA capture

    // common state
    bool visible;
    bool loaded;                 // initial load complete
} WebViewProp;
```

Store on ViewBlock alongside existing EmbedProp:

```cpp
// In view.hpp, extend ViewBlock or EmbedProp:
typedef struct EmbedProp {
    ImageSurface* img;
    DomDocument* doc;          // iframe document (existing)
    WebViewProp* webview;      // native web view (new)
    FlexProp* flex;
    GridProp* grid;
    // ... existing fields ...
} EmbedProp;
```

### WebView Manager

Central coordinator that owns all web view instances for a document. Dispatches to the appropriate backend based on `WebViewMode`.

```cpp
// radiant/webview_manager.h

typedef struct WebViewManager WebViewManager;

WebViewManager* webview_manager_create(GLFWwindow* window);  // window needed for child-window mode
void            webview_manager_destroy(WebViewManager* mgr);

// lifecycle
WebViewHandle*  webview_create(WebViewManager* mgr, WebViewMode mode, float w, float h, float pixel_ratio);
void            webview_destroy(WebViewManager* mgr, WebViewHandle* handle);

// content (both modes)
void            webview_navigate(WebViewHandle* handle, const char* url);
void            webview_set_html(WebViewHandle* handle, const char* html);
void            webview_eval_js(WebViewHandle* handle, const char* js);

// child window mode: position the native window
void            webview_set_bounds(WebViewHandle* handle, float x, float y, float w, float h, float pixel_ratio);
void            webview_set_visible(WebViewHandle* handle, bool visible);

// offscreen layer mode: snapshot capture
bool            webview_capture_snapshot(WebViewHandle* handle, ImageSurface* surface);
bool            webview_is_dirty(WebViewHandle* handle);
void            webview_resize(WebViewHandle* handle, float w, float h, float pixel_ratio);

// offscreen layer mode: event injection (Radiant → web view)
void            webview_inject_mouse_event(WebViewHandle* handle, int type, float x, float y, int button, int mods);
void            webview_inject_key_event(WebViewHandle* handle, int type, int keycode, int mods);
void            webview_inject_text_input(WebViewHandle* handle, uint32_t codepoint);
void            webview_inject_scroll(WebViewHandle* handle, float dx, float dy, float x, float y);

// called after layout to sync all web views (both modes)
void            webview_manager_sync_layout(WebViewManager* mgr, ViewTree* tree, float pixel_ratio);
```

### Platform Backends

#### Child Window Mode (via `webview/webview` library)

The [webview/webview](https://github.com/nicedoc/webview) single-header C/C++ library provides a unified API across WKWebView (macOS), WebView2 (Windows), and WebKitGTK (Linux). It handles window creation, navigation, JS evaluation, and IPC binding.

For child-window mode, the integration is:

1. Get the native window handle from GLFW: `glfwGetCocoaWindow()` / `glfwGetX11Window()` / `glfwGetWin32Window()`
2. Create the webview as a child/subview of that native window
3. Position and resize via platform APIs based on layout-computed bounds
4. The OS web engine handles all rendering and input directly

```
radiant/webview_child.cpp   — child-window mode using webview/webview lib
```

Positioning per platform:
- **macOS**: `[wkWebView setFrame:]` on the `NSWindow` content view, with flipped Y coordinates
- **Linux**: `gtk_fixed_move()` + `gtk_widget_set_size_request()` in the X11 window
- **Windows**: `ICoreWebView2Controller::put_Bounds()` with `RECT` in physical pixels

#### Offscreen Layer Mode (direct platform APIs)

The `webview/webview` library does not support offscreen rendering. Layer mode uses platform APIs directly to create hidden web views and capture their content as RGBA bitmaps.

```
radiant/platform/webview_layer_mac.mm      — macOS: WKWebView + takeSnapshot
radiant/platform/webview_layer_linux.cpp   — Linux: WebKitGTK + get_snapshot
radiant/platform/webview_layer_win.cpp     — Windows: WebView2 CompositionController
radiant/platform/webview_layer_stub.cpp    — no-op stub for headless builds
```

**macOS**:
- Create `WKWebView` with `WKWebViewConfiguration`, not attached to any window
- Capture via `takeSnapshot(with:completionHandler:)` → `NSImage` → `CGBitmapContext` → RGBA bytes
- HiDPI: create at physical pixel dimensions; snapshot produces Retina-resolution bitmap

**Linux**:
- Create `WebKitWebView` with `WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER` (software rendering)
- Use `webkit_web_view_get_snapshot()` → `cairo_surface_t` → `cairo_image_surface_get_data()` → RGBA
- Requires `webkit2gtk-4.1` system package

**Windows**:
- Use `ICoreWebView2CompositionController` (visual hosting mode, no HWND)
- Capture via `CapturePreview()` → `IStream` → decode → RGBA bytes

### New Tag Constant

```cpp
// In view.hpp, enum HtmTag (between HTM_TAG_WBR and HTM_TAG_XMP):
HTM_TAG_WEBVIEW,  // = 193 (0xC1)
```

Registered in `lambda/input/css/dom_node.cpp` `html_elements[]` table as `{"webview", HTM_TAG_WEBVIEW}`. The HTML5 parser calls `DomNode::tag_name_to_id("webview")` during element creation, which returns 193 via hashtable lookup.

## Layout Integration

### During Layout

`<webview>` is laid out identically to `<iframe>` — as a replaced block element:

1. **Intrinsic size**: 300×150 CSS pixels (default), overridden by `width`/`height` attributes or CSS
2. **Box model**: Normal block-level participation (margins, borders, padding apply)
3. **Flex/Grid child**: Works as a flex or grid item — `flex: 1` stretches the web view
4. **Overflow**: Content is clipped to the web view bounds (the native view handles its own scrolling)

In `layout_block.cpp`, after computing the ViewBlock's final position and size:

```cpp
if (block->tag_id == HTM_TAG_WEBVIEW) {
    if (!block->embed) block->embed = (EmbedProp*)alloc_prop(lycon, sizeof(EmbedProp));
    if (!block->embed->webview) {
        block->embed->webview = (WebViewProp*)alloc_prop(lycon, sizeof(WebViewProp));
        block->embed->webview->src = get_attr(block, "src");
        block->embed->webview->srcdoc = get_attr(block, "srcdoc");
        const char* mode_attr = get_attr(block, "mode");
        block->embed->webview->mode = (mode_attr && strcmp(mode_attr, "layer") == 0)
            ? WEBVIEW_MODE_LAYER : WEBVIEW_MODE_WINDOW;
    }
    block->embed->webview->last_x = block_abs_x;
    block->embed->webview->last_y = block_abs_y;
    block->embed->webview->last_w = block->width;
    block->embed->webview->last_h = block->height;
}
```

### Post-Layout Sync

After layout completes (and after every relayout), the WebView Manager walks the view tree and synchronizes web view instances based on their mode:

```
layout_html_doc()
    → layout complete
    → webview_manager_sync_layout(mgr, view_tree, pixel_ratio)
        → for each WebViewProp in tree:
            if mode == WINDOW:
                if no handle yet → webview_create(WINDOW) + navigate
                → webview_set_bounds(x, y, w, h) to reposition child window
                → webview_set_visible(display != none)
            if mode == LAYER:
                if no handle yet → webview_create(LAYER) + navigate
                if size changed  → webview_resize()
                if dirty → webview_capture_snapshot() → update ImageSurface
```

### Rendering

**Child window mode**: No rendering action needed from Radiant — the native child window paints itself on top of the OpenGL surface. Radiant only needs to position it correctly after layout.

**Offscreen layer mode**: During Radiant's render pass, composite the RGBA texture:

```cpp
if (block->tag_id == HTM_TAG_WEBVIEW && block->embed && block->embed->webview) {
    WebViewProp* wv = block->embed->webview;
    if (wv->mode == WEBVIEW_MODE_LAYER && wv->surface && wv->surface->data) {
        render_image_surface(rdcon, wv->surface, block);
    }
    // WEBVIEW_MODE_WINDOW: nothing to do — native child window renders itself
}
```

### Snapshot Scheduling (Layer Mode Only)

Capturing a snapshot for every frame is expensive. Use a dirty-flag approach:

1. **On load / navigation**: mark `dirty = true`
2. **On JS-triggered mutations**: the web engine notifies via a callback (e.g., `WKNavigationDelegate` `didFinishNavigation`, or a MutationObserver injected via preload script) → mark `dirty = true`
3. **On animation**: if the web view contains CSS animations or JS `requestAnimationFrame`, schedule periodic snapshots (e.g., 30 fps) while active
4. **On Radiant redraw**: if `dirty`, capture snapshot before compositing; otherwise reuse the cached `ImageSurface`

This keeps CPU/GPU cost low for static content while supporting animated web view content.

### Scrolling

**Child window mode**: When the web view is inside a scroll container, the scroll handler must call `webview_set_bounds()` to reposition the native child window every frame. This may cause minor visual lag on fast scrolls (the child window trails the OpenGL-rendered content by a frame). Acceptable for most use cases.

**Offscreen layer mode**: Scrolling is handled naturally — the RGBA bitmap is composited at whatever position the layout engine computes, just like any other image. No repositioning needed.

If the web view itself has scrollable content (e.g., a long page inside the web view):
- **Child window mode**: the OS handles internal scrolling natively
- **Offscreen layer mode**: Radiant forwards scroll events via `webview_inject_scroll()`, which causes the web engine to scroll internally and marks `dirty = true` for a new snapshot

### Visibility and Occlusion

**Child window mode**:
- `display: none` → `webview_set_visible(false)` (hides the native child window)
- Scrolled out of viewport → `webview_set_visible(false)` (save resources)
- `visibility: hidden` → `webview_set_visible(false)` but keep bounds reserved
- **Z-order limitation**: native child window is always on top of OpenGL content

**Offscreen layer mode**:
- `display: none` → skip snapshot capture, don't composite
- Scrolled out of viewport → skip snapshot capture
- `visibility: hidden` → keep bounds reserved, don't composite
- `opacity < 1.0` → composite with alpha blending (works naturally since it's a texture)
- `overflow: hidden` on parent → clip the texture with Radiant's existing clip region
- **Z-order is correct** — Radiant elements can overlay the web view freely

## Event Handling

Event handling differs fundamentally between the two modes.

### Child Window Mode

The native child window receives OS input events directly — mouse clicks, keyboard input, scroll, and hover are all handled by the OS web engine with no involvement from Radiant.

When Radiant's hit-test lands on a `HTM_TAG_WEBVIEW` block in child-window mode, Radiant should **not** process the event further:

```cpp
if (block->tag_id == HTM_TAG_WEBVIEW && block->embed->webview->mode == WEBVIEW_MODE_WINDOW) {
    evcon->target = (View*)block;  // target is the webview container itself
    return;  // OS handles events directly in the child window
}
```

**Focus**: Clicking inside the child window gives it OS-level focus. Clicking outside (on Radiant content) should blur the web view and return focus to GLFW.

### Offscreen Layer Mode

Since the web view renders offscreen, it does **not** receive OS input events. Radiant owns all input via GLFW and must forward relevant events.

### Event Flow

```
GLFW callback (mouse/key/scroll)
    → Radiant event dispatch
    → hit-test: lands on HTM_TAG_WEBVIEW block?
        YES → translate coords to web view local space
             → webview_inject_mouse_event() / webview_inject_key_event()
             → web engine processes event
             → mark dirty (if content changed)
             → next frame: re-snapshot + composite
        NO  → normal Radiant event handling
```

### Coordinate Translation

Radiant's hit-test gives the mouse position in CSS page coordinates. To inject into the web view, subtract the block's absolute content-box origin:

```cpp
float local_x = mouse_x - block_content_x;
float local_y = mouse_y - block_content_y;
webview_inject_mouse_event(handle, MOUSE_DOWN, local_x, local_y, button, mods);
```

The web view receives coordinates in its own viewport space (0,0 at top-left of web view content).

### Keyboard Events

When the web view has focus (tracked by Radiant's `RadiantState.focus_view`):
- All `KEY_DOWN`, `KEY_UP`, `KEY_REPEAT`, and `TEXT_INPUT` events are forwarded via `webview_inject_key_event()` / `webview_inject_text_input()`
- Radiant does **not** process these events itself (no Radiant shortcut handling while web view is focused)

### Focus Management

- Clicking on a `HTM_TAG_WEBVIEW` block sets `focus_view` to that block
- While focused, keyboard events route to the web view
- Clicking outside the web view (on Radiant content) clears web view focus, optionally calling `webview_eval_js(handle, "document.activeElement.blur()")` to notify the web view
- Tab navigation: Radiant tracks the web view as a single focusable element in its tab order. Pressing Tab while inside the web view is forwarded to the web engine; only Shift+Tab at the "first" focusable element inside the web view (or a dedicated escape key) returns focus to Radiant

### Cursor Feedback

The web view may want to change the mouse cursor (e.g., text cursor over editable content, pointer over links). The web engine communicates cursor changes via platform callbacks:
- macOS: `WKUIDelegate` cursor change notifications
- Windows: `WebView2` `CursorChanged` event
- Linux: WebKitGTK signal

Radiant translates these to `glfwSetCursor()` calls while the mouse is over the web view block.

## IPC Bridge: Lambda ↔ Web View

### JS → Lambda

Inject a bridge script (via `WKUserScript` / `addScriptToEvaluateOnNewDocument`):

```javascript
// Injected into every webview page
window.__lambda = {
    invoke: function(command, payload) {
        // Platform-specific message posting:
        // macOS:   window.webkit.messageHandlers.lambda.postMessage(...)
        // Windows: window.chrome.webview.postMessage(...)
        // Linux:   custom URI scheme or window.webkit.messageHandlers
        window.webkit.messageHandlers.lambda.postMessage(
            JSON.stringify({ command: command, payload: payload })
        );
    },
    _callbacks: {},
    _nextId: 1,
    on: function(event, callback) {
        this._callbacks[event] = callback;
    }
};
```

On the Lambda side, register handlers:

```lambda
webview.on("chart-click", fn(data) {
    log("Clicked point: " + data.label)
})
```

### Lambda → JS

```cpp
// Push data into web view
webview_eval_js(handle, "window.__lambda._callbacks['update']({temperature: 72})");
```

### Security

- **Origin isolation**: Web views load content in a sandboxed origin. IPC is the only channel back to Lambda.
- **Sandbox flags**: `allow-scripts` (JS execution), `allow-ipc` (bridge access), `allow-navigation` (link following), `allow-popups` (window.open). Default: `allow-scripts` only.
- **Allowlist**: IPC commands must be registered on the Lambda side. Unregistered commands are dropped with a log warning.
- **No eval passthrough**: Lambda never passes unsanitized web view content into `eval` or `system()`.

## Asset Serving

Web views need to load local assets (HTML, CSS, JS, images). Options:

### Option A: Custom URL Scheme (`lambda://`)

Register `lambda://` as a custom protocol:
- macOS: `WKURLSchemeHandler`
- Windows: `WebView2` `WebResourceRequested` event
- Linux: WebKitGTK `webkit_web_context_register_uri_scheme()`

Map `lambda://assets/chart.js` → local file `./assets/chart.js` relative to the Lambda project root.

### Option B: Local HTTP Server

Radiant already has `http_module.cpp`. Spin up a localhost server on a random port, serve files from a designated directory. Web view navigates to `http://127.0.0.1:<port>/chart.html`.

**Recommendation**: Option A (custom scheme) is more secure (no open port) and more aligned with Tauri's approach. Fall back to Option B only if platform scheme registration proves problematic.

## Event Simulation (Automated Testing)

Radiant's existing `event_sim` system (used by WebDriver and UI automation) must support both web view modes.

### Offscreen Layer Mode

Since layer-mode web views receive events through Radiant's event forwarder, the same `event_sim` pipeline naturally reaches web view content:

```
event_sim generates SIM_EVENT_CLICK at (x, y)
    → Radiant event dispatch
    → hit-test lands on HTM_TAG_WEBVIEW (layer mode)
    → webview_inject_mouse_event()
    → web engine processes click
```

**Existing event simulation works for basic interactions** (click, type, scroll) with no additional infrastructure.

### Child Window Mode

In child-window mode, the OS delivers events to the native child window. Radiant's `event_sim` doesn't reach there directly. Two approaches to bridge this:

1. **JS injection** (recommended): Use `webview_eval_js()` to dispatch synthetic DOM events:
   ```javascript
   // Simulate a click at (x, y) inside the web view
   var el = document.elementFromPoint(x, y);
   el.dispatchEvent(new MouseEvent('click', {clientX: x, clientY: y, bubbles: true}));
   ```

2. **Native event synthesis**: Generate platform-specific input events targeting the child window (`CGEventPost` on macOS, `SendInput` on Windows, `XSendEvent` on Linux). More fragile but handles edge cases like hover states and drag-and-drop.

The event simulation layer detects the web view mode and dispatches accordingly:

```cpp
if (block->tag_id == HTM_TAG_WEBVIEW) {
    WebViewProp* wv = block->embed->webview;
    if (wv->mode == WEBVIEW_MODE_LAYER) {
        // forward via event injection (same as interactive)
        webview_inject_mouse_event(wv->handle, type, local_x, local_y, button, mods);
    } else {
        // child window mode: inject via JS
        char js[256];
        snprintf(js, sizeof(js),
            "document.elementFromPoint(%f,%f).dispatchEvent("
            "new MouseEvent('click',{clientX:%f,clientY:%f,bubbles:true}))",
            local_x, local_y, local_x, local_y);
        webview_eval_js(wv->handle, js);
    }
}
```

### Extended WebView Simulation Commands

For more complex automation scenarios, extend `event_sim` with web view-specific commands:

```cpp
// In event_sim.hpp, new simulation event types:
SIM_EVENT_WEBVIEW_EVAL_JS,       // execute arbitrary JS in a webview
SIM_EVENT_WEBVIEW_WAIT_LOAD,     // wait until webview navigation completes
SIM_EVENT_WEBVIEW_ASSERT_TEXT,   // assert text content visible in webview
SIM_EVENT_WEBVIEW_SNAPSHOT,      // capture webview snapshot for visual regression
```

**JS evaluation** enables deep interaction:

```cpp
// event_sim dispatch:
case SIM_EVENT_WEBVIEW_EVAL_JS: {
    View* target = find_element_by_selector(doc, ev->selector);
    if (target && target->tag_id == HTM_TAG_WEBVIEW) {
        WebViewProp* wv = ((ViewBlock*)target)->embed->webview;
        webview_eval_js(wv->handle, ev->js_code);
    }
    break;
}
```

**Wait for load** blocks the simulation until the web view fires `didFinishNavigation` (or equivalent), ensuring subsequent assertions run against fully-loaded content.

**Assert text** injects JS to query `document.body.innerText` and checks for a substring:

```javascript
// Injected by SIM_EVENT_WEBVIEW_ASSERT_TEXT:
window.__lambda._assertResult = document.body.innerText.includes("expected text");
```

Then read back the result via `webview_eval_js` + callback.

**Snapshot** captures the current RGBA bitmap for pixel-comparison regression testing, using Radiant's existing layout test comparison infrastructure.

### WebDriver Integration

Extend `switch_frame` to target `<webview>` elements:

```cpp
case SIM_EVENT_SWITCH_FRAME: {
    View* iframe_view = find_element_by_selector(doc, ev->frame_selector);
    if (iframe_view->tag_id == HTM_TAG_WEBVIEW) {
        // WebView mode: subsequent JS eval commands route to this webview
        ctx->active_webview = ((ViewBlock*)iframe_view)->embed->webview;
    } else {
        // existing iframe handling...
    }
}
```

This allows WebDriver test scripts to interact with web view content using the same `switch_frame` → `find_element` → `click` patterns they use for iframes.

## Lifecycle

### Creation

```
HTML parse → <webview> DomElement
    → layout (position + size computed)
    → post-layout sync → webview_create() + webview_navigate()
```

### Navigation

```
webview_navigate(handle, new_url)   // programmatic
// or: user clicks link inside web view (handled by OS engine)
```

### Resize / Relayout

```
CSS change / window resize → relayout
    → post-layout sync → webview_set_bounds() with new position/size
```

### Destruction

```
Page navigation away / DOM removal / window close
    → webview_destroy() releases native handle
    → WebViewManager cleanup on window close
```

### Page Transitions

When the user navigates to a new Radiant page (e.g., clicking a link in Radiant-rendered content), all web views from the previous page must be destroyed before the new page is laid out.

## CLI Integration

```bash
# View mode (interactive) — child-window web views are live with native perf;
#                           layer-mode web views are offscreen-rendered and composited
lambda.exe view page.html

# Layout mode (headless) — web views are skipped, treated as 300×150 placeholders
lambda.exe layout page.html

# Render mode (SVG/PDF/PNG) — all web views use layer mode internally for snapshot compositing
# (child-window mode web views are temporarily created in layer mode for capture)
lambda.exe render page.html -o output.pdf
```

### Headless Web View for Render Mode

In `render` mode, all `<webview>` elements are treated as layer mode regardless of the `mode` attribute, since there is no visible window for child-window mode. The offscreen web view is created, navigation completes, the RGBA snapshot is captured, and the bitmap is composited into the PDF/SVG/PNG output.

## Build System Integration

### Dependencies

| Platform | Library | System-provided? | Build flag |
|----------|---------|-------------------|------------|
| macOS    | WebKit (WKWebView) | Yes (ships with macOS) | `-framework WebKit` |
| Linux    | WebKitGTK 4.1 | Package install | `pkg-config --libs webkit2gtk-4.1` |
| Windows  | WebView2 | Yes (Edge runtime) | Link `WebView2LoaderStatic.lib` |

### New Source Files

```
radiant/webview.h                          — public C API (WebViewManager, WebViewHandle, WebViewMode, WebViewProp)
radiant/webview_manager.cpp                — lifecycle management, post-layout sync tree walk
radiant/webview_handle_mac.h               — shared WebViewHandle struct for macOS (child + layer modes)
radiant/webview_child_mac.mm               — macOS child-window backend (WKWebView, #ifdef __APPLE__)
radiant/webview_child_stub.cpp             — no-op stub for non-macOS platforms (#ifndef __APPLE__)
radiant/webview_layer_mac.mm               — macOS offscreen layer backend (#ifdef __APPLE__)
radiant/webview_layer_stub.cpp             — no-op stub for non-macOS platforms (#ifndef __APPLE__)
radiant/platform/webview_layer_linux.cpp   — (Stage 2, future) Linux offscreen layer backend
radiant/platform/webview_layer_win.cpp     — (Stage 2, future) Windows offscreen layer backend
```

### build_lambda_config.json Changes

Add platform-conditional source files and link flags:

```json
{
    "webview_mac": {
        "files": ["radiant/platform/webview_mac.mm"],
        "links": ["WebKit.framework"],
        "platforms": ["macosx"]
    },
    "webview_linux": {
        "files": ["radiant/platform/webview_linux.cpp"],
        "links": ["webkit2gtk-4.1", "gtk+-3.0"],
        "platforms": ["linux"]
    },
    "webview_win": {
        "files": ["radiant/platform/webview_win.cpp"],
        "links": ["WebView2LoaderStatic"],
        "platforms": ["windows"]
    }
}
```

Binary size impact: ~50-200 KB (wrapper code only; web engine is OS-provided).

## Implementation Phases

### Stage 1: Child Window Mode (macOS first)

Direct platform API integration (WKWebView on macOS) for native web page performance.

> **Decision**: Instead of the `webview/webview` third-party library, Stage 1 uses direct platform APIs (`WKWebView` on macOS). This avoids the library's assumption of owning the entire window and gives full control over child view positioning. Linux/Windows backends will follow the same direct-API approach.

1. ✅ Define `HTM_TAG_WEBVIEW` (enum value 193/0xC1 in `view.hpp`) and register in HTML5 parser (`dom_node.cpp` `html_elements[]` table)
2. ✅ Layout `<webview>` as replaced inline-block element:
   - `resolve_css_style.cpp`: added to both `is_replaced` check and tag-based default display fallback (`display: inline-block` + `RDT_DISPLAY_REPLACED`)
   - `resolve_htm_style.cpp`: new `case HTM_TAG_WEBVIEW` parses `width`/`height` HTML attributes, defaults to 300×150
3. ✅ Implement `webview.h`: public C API header with `WebViewProp`, `WebViewMode`, `WebViewManager`, platform backend function declarations
4. ✅ Implement `webview_child_mac.mm`: macOS WKWebView child window backend
   - Gets NSWindow via `glfwGetCocoaWindow()`, creates WKWebView as subview
   - Coordinate transform with flipped Y: `NSMakeRect(x, view_height/pr - y - h, w, h)`
   - `LambdaWebViewDelegate` (WKNavigationDelegate) for load tracking
   - IPC bridge: injects `window.__lambda.invoke()` via `WKUserScript` at document start
   - Delegate retained via `objc_setAssociatedObject` on the WKWebView
5. ✅ Implement `webview_child_stub.cpp`: no-op stub for non-macOS platforms
6. ✅ Implement `webview_manager.cpp`: lifecycle management + post-layout sync
   - `WebViewManager` struct with GLFWwindow* and dynamic handle array
   - `sync_walk()`: recursive tree walk accumulating absolute positions
   - `tree_has_webview()`: quick scan to avoid manager creation when no webviews present
   - `webview_manager_sync_layout(UiContext*, ViewTree*)`: lazy-init manager on first webview
7. ✅ Integrate in layout pipeline:
   - `layout_block.cpp`: WebViewProp creation in replaced element handler (reads src/srcdoc/mode attributes, sets `needs_create`)
   - `layout.cpp`: post-layout sync call after `layout_cleanup()`
8. ✅ Build system: added `-framework WebKit` to macOS libraries in `build_lambda_config.json`
9. ✅ **Build**: 0 errors, 0 warnings
10. ✅ **Test**: `test/layout/webview_basic.html` — two webviews (srcdoc 400×200 + external URL 400×300) lay out correctly with proper dimensions
11. ✅ Event boundary: stop Radiant event dispatch at child-window webview blocks (OS handles input)
    - `event.cpp`: added check in `target_block_view()` — when hit-test lands on a `WEBVIEW_MODE_WINDOW` block, sets target and returns without processing children
12. ✅ Implement `lambda://` custom scheme handler for local asset serving
    - `webview_child_mac.mm`: `LambdaSchemeHandler` (WKURLSchemeHandler) serves files from CWD
    - MIME type detection, path traversal protection (resolved path must stay within base)
    - Registered on WKWebViewConfiguration in `webview_platform_create()`
13. ✅ Wire IPC messages from `window.__lambda.invoke()` to Lambda runtime
    - `webview_child_mac.mm`: `LambdaScriptMessageHandler` (WKScriptMessageHandler) receives JSON `{command, payload}` messages
    - Registered via `[config.userContentController addScriptMessageHandler:name:@"lambda"]`
    - Currently logs messages; Lambda runtime dispatch is a future enhancement
14. ✅ Implement `webview_eval_js()` Lambda→JS communication
    - `webview_child_mac.mm`: `webview_platform_eval_js()` calls `[WKWebView evaluateJavaScript:completionHandler:]`
    - Public API: `webview_eval_js()` in `webview_manager.cpp`, declared in `webview.h`
15. ✅ Event simulation for child-window mode via JS injection (`dispatchEvent`)
    - `event_sim.hpp`: added `SIM_EVENT_WEBVIEW_EVAL_JS`, `SIM_EVENT_WEBVIEW_WAIT_LOAD` types and `js_code` field
    - `event_sim.cpp`: parsing for `webview_eval_js` and `webview_wait_load` commands, execution via `webview_eval_js()` targeting webview elements by CSS selector
16. 🔲 Linux backend (`webview_child_linux.cpp` using WebKitGTK)
17. 🔲 Windows backend (`webview_child_win.cpp` using WebView2)
18. ✅ Live interactive test with `lambda.exe view` (verify WKWebView appears at correct position)
    - Both srcdoc and external URL webviews create, position, and navigate correctly
    - `webview navigation finished` confirms successful page loads
    - Webview cleanup wired into `ui_context_cleanup()` and `session_go_back/forward()`

**Deliverable**: A fully interactive web view embedded in a Radiant page on all platforms, with native web performance, IPC bridge, and automated test support. Limitation: cannot be overlaid by Radiant-rendered content.

#### Implementation Notes

- **Bug fixed**: `<webview>` was initially laid out as inline (2×20) instead of replaced block. Root cause: `HTM_TAG_WEBVIEW` was added to the `is_replaced` check in `resolve_css_style.cpp` but was missing from the **tag-based default display fallback** section (~line 1070). Since `<webview>` has no UA stylesheet entry, it fell through to defaults and got `display: inline`. Fixed by adding it to both the `is_replaced` boolean and the tag-based replaced element list.
- **Sizing**: Also required a new `case HTM_TAG_WEBVIEW` in `resolve_htm_style.cpp` to parse `width`/`height` HTML attributes (similar to iframe). Without this, the element had 0×0 intrinsic size and rendered as 2×2 (border only).

### Stage 2: Offscreen Layer Mode (per-platform)

Adds `mode="layer"` for use cases requiring z-order compositing or headless rendering to PNG/PDF.

12. ~~Implement `webview_layer_mac.mm`: create offscreen WKWebView, capture snapshot via `takeSnapshot`~~ **Done** — `radiant/webview_layer_mac.mm` implements offscreen WKWebView with `takeSnapshotWithConfiguration:`, IPC bridge via `WKScriptMessageHandler`, MutationObserver dirty detection, and `lambda://` scheme handler.
13. ~~Composite web view RGBA texture during Radiant's render pass~~ **Done** — `render_webview_layer_content()` in `render.cpp` composites snapshots via `blit_surface_scaled()`. SVG/PDF export handled in `render_walk.cpp` via `backend->render_image`.
14. ~~Event forwarding for layer mode: translate Radiant mouse/key events → `webview_inject_*()` calls~~ **Done** — `event.cpp` forwards mouse move/click/scroll/key/text events to layer-mode webviews via `webview_layer_platform_inject_*()` functions (implemented as JS `dispatchEvent` injection).
15. ~~Dirty-flag snapshot scheduling (MutationObserver-based change detection)~~ **Done** — MutationObserver + IPC message handler sets dirty flag; `webview_manager.cpp` sync_walk only re-snapshots when dirty.
16. Implement `webview_layer_linux.cpp` (WebKitGTK offscreen)
17. Implement `webview_layer_win.cpp` (WebView2 CompositionController)
18. ~~Implement `webview_layer_stub.cpp` for headless mode~~ **Done** — `radiant/webview_layer_stub.cpp` provides no-op stubs guarded by `#ifndef __APPLE__`.
19. `render` mode: force all web views to layer mode for PNG/PDF/SVG export
20. Extend event simulation with `SIM_EVENT_WEBVIEW_EVAL_JS`, `WEBVIEW_WAIT_LOAD`, `WEBVIEW_ASSERT_TEXT`, `WEBVIEW_SNAPSHOT`

**Deliverable**: Offscreen web views compositable as layers with correct z-order, snapshot capture for headless rendering, and full automated testing support.

### Stage 3: Polish

21. Focus management (web view ↔ Radiant focus transitions, Tab key)
22. Cursor feedback from web view in layer mode (link pointer, text cursor, etc.)
23. Sandbox attribute enforcement
24. `srcdoc` attribute support
25. DevTools access (right-click → Inspect)
26. Animation-aware snapshot scheduling for layer mode (detect `requestAnimationFrame` / CSS animations)

## Testing Strategy

| Test Type | Description |
|-----------|-------------|
| **Unit** | WebView Manager create/destroy/resize for both modes (mock backend) |
| **Layout** | `<webview>` sizing in block, flex, grid contexts — verify computed position/size |
| **Child window** | Load `<webview src="...">`, verify native child window appears at correct coordinates |
| **Overlay** | Load `<webview mode="layer">`, verify Radiant elements can overlay the web view |
| **Snapshot** | Layer mode: capture snapshot, verify RGBA bitmap is non-empty and correct dimensions |
| **Event (child)** | JS injection via `dispatchEvent` — verify click/type events reach web engine |
| **Event (layer)** | Direct event injection — verify `webview_inject_*()` events reach web engine |
| **IPC** | Both modes: send message from web view JS → Lambda handler; Lambda → web view JS callback |
| **Event sim** | Use `event_sim` commands to interact with web views in both modes |
| **Scroll** | Child window: verify repositioning on scroll. Layer: verify composited position updates |
| **Automation** | WebDriver `switch_frame` targeting `<webview>` in both modes |
| **Render mode** | `lambda.exe render page.html -o out.png` — all web views composited as layer mode |

Test HTML files go in `test/layout/webview/`.

---

## Open Questions

1. ~~**`webview/webview` library child positioning**~~: **Resolved** — bypassed entirely. Stage 1 uses direct platform APIs (`WKWebView` on macOS) instead of the `webview/webview` library. Creating a `WKWebView` as a subview of the GLFW window's NSWindow content view works cleanly with `glfwGetCocoaWindow()`. No library integration or forking needed.

2. **Multiple web views**: How many simultaneous web views should we support per page? Each instance (especially child-window mode) consumes significant OS resources. Should we enforce a limit (e.g., 4-8)?

3. **Web view inside iframe**: If a Radiant `<iframe>` contains a `<webview>`, the coordinate transform for positioning (child-window mode) or event forwarding (layer mode) becomes nested. Should we support this combination, or disallow it initially?

4. **Reuse across page navigations**: If the user navigates from page A (with a webview) to page B (with a similar webview pointing to the same URL), should we reuse the web view instance for faster transition? Or always destroy and recreate?

5. **Child window scroll lag**: In child-window mode, repositioning the native view on each scroll frame may cause visible lag. Need to measure on each platform. If noticeable, consider briefly hiding the child window during fast scrolls and showing it after scroll settles.

6. **Event injection fidelity (layer mode)**: Platform APIs for injecting events into offscreen web views vary. macOS `WKWebView` doesn't have a direct injection API — may need `evaluateJavaScript` to dispatch synthetic DOM events. Need to prototype which approach works reliably for hover, focus, and keyboard input.

7. **Content Security Policy**: Should we enforce a default CSP on web view content? Matters especially for `lambda://` scheme content.

8. **Accessibility**: Child-window mode preserves the web view's native accessibility tree. Layer mode disconnects it from the window hierarchy — may need to proxy accessibility info.

9. **Snapshot latency (layer mode)**: `takeSnapshot` on macOS is async. If latency >16ms, 60fps compositing of animated layer-mode content is infeasible. Acceptable since layer mode targets static/semi-static content.

## Suggestions

1. ~~**Prototype `webview/webview` child positioning early**~~: **Done** — decided to use direct platform APIs instead. macOS WKWebView child view positioning works cleanly. Same direct-API approach will be used for Linux (WebKitGTK) and Windows (WebView2).

2. **Use JS injection for event simulation in both modes**: `webview_eval_js(handle, "document.elementFromPoint(x,y).click()")` works identically in both child-window and layer modes. It's the most portable approach and avoids the fragility of native event synthesis.

3. **IPC message batching**: For high-frequency updates (e.g., streaming data to a chart), batch IPC messages per animation frame to avoid choking the message channel.

4. **Lazy initialization**: Don't create the web view until the `<webview>` element is first scrolled into the viewport. Saves resources for pages with many off-screen web views.

5. **Preload scripts for common patterns**: Ship a small library of preload scripts for common integrations (e.g., `lambda-chart-bridge.js` that standardizes the data protocol for chart libraries).

6. **Consider `CAMetalLayer` / shared texture for animated layer content on macOS**: Instead of `takeSnapshot`, render the `WKWebView` into a `CALayer` and share the `IOSurface` backing store directly with OpenGL. Avoids CPU-side copy entirely. This is an optimization for Stage 3, not MVP.

7. **Mode auto-detection heuristic** (future): If a `<webview>` has `position: relative` with `z-index` siblings that overlap it, automatically promote to layer mode. For now, keep it explicit via the `mode` attribute.
