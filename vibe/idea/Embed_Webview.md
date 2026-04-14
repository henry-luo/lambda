# Embedding Native Web View in Radiant (Tauri-like)

## Feasibility

Yes, it's feasible. Radiant already has the key infrastructure pieces: GLFW windowing, platform-specific code paths (macOS/Linux/Windows), an event loop, and IPC-capable Lambda runtime. The question is *which integration model* and *how deep*.

## Integration Models

| Model | Description | Complexity |
|-------|-------------|------------|
| **A. Web view as primary surface** | Replace GLFW+OpenGL with native web view (like Tauri). Lambda backend ↔ web view IPC. Radiant layout engine becomes optional. | Medium |
| **B. Web view as child component** | GLFW window hosts both OpenGL surface and embedded web view. Radiant renders most content; web view handles JS-heavy regions. | High |
| **C. Radiant renders, web view for JS only** | Radiant remains the renderer. Embed a headless web view solely for JavaScript execution, feeding DOM mutations back to Radiant's own layout engine. | Very High |

**Model A is the practical starting point** — it's what Tauri, Electron, and Wry do.

## Dev Tasks for Model A (Tauri-like)

### Phase 1 — Platform Web View Wrapper

1. **macOS**: Create `WKWebView` wrapper in Objective-C++ (`.mm`), embed in `NSWindow`
2. **Linux**: Integrate `WebKitGTK` (`webkit2gtk-4.1`), embed in `GtkWindow`
3. **Windows**: Integrate `WebView2` (Microsoft Edge), embed in `HWND`
4. **Abstract API**: Define a `WebView` interface (`navigate(url)`, `eval_js(code)`, `bind(name, callback)`, `set_html(html)`)
5. **Replace or augment GLFW**: For web view mode, either bypass GLFW entirely (use native windows) or create the web view as a child of the GLFW native window handle (`glfwGetCocoaWindow()`, `glfwGetX11Window()`, `glfwGetWin32Window()`)

### Phase 2 — IPC Bridge (Lambda ↔ Web View)

6. **JS→Lambda channel**: Web view calls `window.__lambda.invoke(cmd, payload)` → routed to Lambda function handlers
7. **Lambda→JS channel**: Lambda calls `webview_eval_js()` to push data/events into the web view
8. **Serialization**: JSON-based message protocol (Lambda already has JSON I/O in `lambda/input/input-json.cpp` and `lambda/format/`)
9. **Security**: Origin restrictions, allowlist for IPC commands, sanitize eval'd JS

### Phase 3 — Asset Serving

10. **Custom protocol handler**: Register `lambda://` scheme to serve local files from Lambda's virtual filesystem (like Tauri's `asset://`)
11. **Or** embedded HTTP server: Radiant already has `http_module.cpp` — adapt for local asset serving

### Phase 4 — Window Management

12. **Window lifecycle**: Create, show, hide, resize, close, fullscreen
13. **Multi-window support**: Window registry, per-window state
14. **Menu bar**: Native menu integration (macOS menu bar, Windows/Linux standard menus)
15. **System tray**: Optional, platform-specific

### Phase 5 — Integration with Radiant's Existing Capabilities

16. **Hybrid rendering mode**: Option to render via Radiant's own engine (PDF/SVG/PNG export) or via web view (interactive display)
17. **`lambda.exe view` dual mode**: `--native` uses current Radiant renderer, `--webview` uses native web view
18. **Hot reload**: File watcher → re-render on change (Radiant already has event simulation infra)

## Key Differences from Tauri

| Aspect | Tauri | Radiant+WebView |
|--------|-------|-----------------|
| Backend language | Rust | C/C++ + Lambda Script |
| Has its own layout engine | No | Yes (Radiant) — can render without web view |
| JS required for UI | Yes | Optional — Lambda Script can drive UI |
| PDF/SVG export | No (needs browser) | Yes (native, already built) |
| Bundle size | ~3-5 MB | Already ~8 MB release, web view is OS-provided |

## Binary Size Impact (Model A)

Model A uses the **OS-provided** web engine, not a bundled one, so the size increase is negligible.

| Platform | Web Engine | Bundled? | Added to exe |
|----------|-----------|----------|-------------|
| **macOS** | WKWebView (WebKit, ships with macOS) | No | ~20-50 KB wrapper code |
| **Windows** | WebView2 (Edge, ships with Windows 10+) | No* | ~20-50 KB wrapper + ~150 KB loader DLL |
| **Linux** | WebKitGTK (system package) | No | ~20-50 KB wrapper code |

**Total impact: ~50-200 KB** on top of the current ~8 MB release binary.

This is the same strategy Tauri uses to avoid Electron's ~150+ MB bundle (which includes a full Chromium).

*\*On older Windows 10 machines without Edge WebView2 runtime, users may need a ~1.5 MB bootstrapper, but Microsoft now ships it with Windows by default.*

## Shortcut: Use `webview/webview` Library

The [webview/webview](https://github.com/nicedoc/webview) C/C++ library (~1 header file) already wraps WKWebView/WebView2/WebKitGTK with a unified API. This would collapse Phase 1 into a single dependency + a thin adapter, making the MVP achievable relatively quickly.

## Summary

Model A is very doable. Radiant's unique advantage over pure Tauri is that you keep the native layout engine for offline rendering (PDF, SVG, PNG) and only use the web view for interactive display — best of both worlds.
