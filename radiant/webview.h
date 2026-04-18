// webview.h — Native web view embedding for Radiant layout engine
//
// Stage 1: Child window mode (macOS WKWebView as child of GLFW window)
// Stage 2: Offscreen layer mode (render to RGBA, composite as texture)

#ifndef RADIANT_WEBVIEW_H
#define RADIANT_WEBVIEW_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// forward declarations
struct GLFWwindow;
struct ImageSurface;
struct ViewTree;

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

typedef struct WebViewHandle WebViewHandle;  // opaque, platform-specific

enum WebViewMode {
    WEBVIEW_MODE_WINDOW = 0,  // child window (default) — native perf, always on top
    WEBVIEW_MODE_LAYER  = 1,  // offscreen layer — compositable, correct z-order (Stage 2)
};

// sandbox permission flags (bitmask)
#define WEBVIEW_SANDBOX_ALLOW_SCRIPTS     0x01
#define WEBVIEW_SANDBOX_ALLOW_IPC         0x02
#define WEBVIEW_SANDBOX_ALLOW_NAVIGATION  0x04
#define WEBVIEW_SANDBOX_ALLOW_POPUPS      0x08

typedef struct WebViewProp {
    WebViewHandle* handle;       // native web view instance (NULL until created)
    enum WebViewMode mode;       // window or layer

    const char* src;             // URL to load (borrowed from DOM attribute)
    const char* srcdoc;          // inline HTML (borrowed from DOM attribute)

    // positioning (CSS logical pixels)
    float last_x, last_y;       // last positioned coords
    float last_w, last_h;       // last positioned size

    // offscreen layer mode fields
    struct ImageSurface* surface;   // RGBA bitmap captured from web view (layer mode only)
    bool dirty;                     // content changed, needs re-snapshot
    uint64_t last_snapshot_ms;      // timestamp of last RGBA capture

    // state
    bool visible;
    bool loaded;                 // initial load complete
    bool needs_create;           // set during layout, consumed by post-layout sync
} WebViewProp;

// ---------------------------------------------------------------------------
// WebView Manager — owns all web view instances for a window
// ---------------------------------------------------------------------------

typedef struct WebViewManager WebViewManager;

WebViewManager* webview_manager_create(struct GLFWwindow* window);
void            webview_manager_destroy(WebViewManager* mgr);

// lifecycle
WebViewHandle*  webview_handle_create(WebViewManager* mgr, float w, float h, float pixel_ratio);
void            webview_handle_destroy(WebViewManager* mgr, WebViewHandle* handle);

// content
void            webview_navigate(WebViewHandle* handle, const char* url);
void            webview_set_html(WebViewHandle* handle, const char* html);
void            webview_eval_js(WebViewHandle* handle, const char* js);

// child window positioning
void            webview_set_bounds(WebViewHandle* handle, float x, float y,
                                   float w, float h, float pixel_ratio);
void            webview_set_visible(WebViewHandle* handle, bool visible);

// post-layout sync: walk view tree and create/reposition all web views
// Lazily creates the WebViewManager on first <webview> element encounter.
// Pass the UiContext so the manager can be stored there.
struct UiContext;
void            webview_manager_sync_layout(struct UiContext* uicon,
                                            struct ViewTree* tree);

// poll dirty layer-mode webviews and re-snapshot; returns true if any were updated
bool            webview_manager_poll_dirty(struct UiContext* uicon,
                                           struct ViewTree* tree);

// destroy all web views (called before page navigation)
void            webview_manager_clear(WebViewManager* mgr);

// ---------------------------------------------------------------------------
// Platform backend (implemented per-platform)
// ---------------------------------------------------------------------------

// create a native child web view parented to the given GLFW window
WebViewHandle*  webview_platform_create(struct GLFWwindow* window,
                                        float x, float y, float w, float h,
                                        float pixel_ratio);
void            webview_platform_destroy(WebViewHandle* handle);
void            webview_platform_navigate(WebViewHandle* handle, const char* url);
void            webview_platform_set_html(WebViewHandle* handle, const char* html);
void            webview_platform_eval_js(WebViewHandle* handle, const char* js);
void            webview_platform_set_bounds(WebViewHandle* handle,
                                            float x, float y, float w, float h,
                                            float pixel_ratio);
void            webview_platform_set_visible(WebViewHandle* handle, bool visible);

// ---------------------------------------------------------------------------
// Layer mode platform backend (offscreen rendering)
// ---------------------------------------------------------------------------

// create an offscreen web view (not attached to any window)
WebViewHandle*  webview_layer_platform_create(float w, float h, float pixel_ratio);
void            webview_layer_platform_destroy(WebViewHandle* handle);

// content (same API as child window)
void            webview_layer_platform_navigate(WebViewHandle* handle, const char* url);
void            webview_layer_platform_set_html(WebViewHandle* handle, const char* html);
void            webview_layer_platform_eval_js(WebViewHandle* handle, const char* js);

// resize the offscreen web view
void            webview_layer_platform_resize(WebViewHandle* handle, float w, float h, float pixel_ratio);

// capture current web view content as RGBA bitmap into the given ImageSurface
// returns true if snapshot was captured, false on error
bool            webview_layer_platform_snapshot(WebViewHandle* handle, struct ImageSurface* surface);

// check if content has changed since last snapshot
bool            webview_layer_platform_is_dirty(WebViewHandle* handle);

// mark content as dirty (e.g. after JS mutation)
void            webview_layer_platform_mark_dirty(WebViewHandle* handle, bool dirty);

// event injection: forward Radiant input events to offscreen web view
void            webview_layer_platform_inject_mouse(WebViewHandle* handle,
                    int type, float x, float y, int button, int mods);
void            webview_layer_platform_inject_key(WebViewHandle* handle,
                    int type, int keycode, int mods);
void            webview_layer_platform_inject_text(WebViewHandle* handle, uint32_t codepoint);
void            webview_layer_platform_inject_scroll(WebViewHandle* handle,
                    float dx, float dy, float x, float y);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_WEBVIEW_H
