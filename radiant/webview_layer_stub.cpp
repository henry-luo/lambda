// webview_layer_stub.cpp — no-op stub for offscreen layer mode
//
// Compiled on Windows (until that backend is implemented) and headless builds.
// Linux uses webview_layer_linux.cpp; macOS uses webview_layer_mac.mm.

#if !defined(__APPLE__) && !defined(__linux__)

#include "webview.h"

extern "C" {
#include "../lib/log.h"
}

WebViewHandle* webview_layer_platform_create(float w, float h, float pixel_ratio) {
    log_info("webview_layer_platform_create: stub (no offscreen web view on this platform)");
    return nullptr;
}

void webview_layer_platform_destroy(WebViewHandle* handle) {
    (void)handle;
}

void webview_layer_platform_navigate(WebViewHandle* handle, const char* url) {
    (void)handle; (void)url;
}

void webview_layer_platform_set_html(WebViewHandle* handle, const char* html) {
    (void)handle; (void)html;
}

void webview_layer_platform_eval_js(WebViewHandle* handle, const char* js) {
    (void)handle; (void)js;
}

void webview_layer_platform_resize(WebViewHandle* handle, float w, float h, float pixel_ratio) {
    (void)handle; (void)w; (void)h; (void)pixel_ratio;
}

bool webview_layer_platform_snapshot(WebViewHandle* handle, struct ImageSurface* surface) {
    (void)handle; (void)surface;
    return false;
}

bool webview_layer_platform_is_dirty(WebViewHandle* handle) {
    (void)handle;
    return false;
}

void webview_layer_platform_mark_dirty(WebViewHandle* handle, bool dirty) {
    (void)handle; (void)dirty;
}

void webview_layer_platform_inject_mouse(WebViewHandle* handle,
                                          int type, float x, float y,
                                          int button, int mods) {
    (void)handle; (void)type; (void)x; (void)y; (void)button; (void)mods;
}

void webview_layer_platform_inject_key(WebViewHandle* handle,
                                        int type, int keycode, int mods) {
    (void)handle; (void)type; (void)keycode; (void)mods;
}

void webview_layer_platform_inject_text(WebViewHandle* handle, uint32_t codepoint) {
    (void)handle; (void)codepoint;
}

void webview_layer_platform_inject_scroll(WebViewHandle* handle,
                                           float dx, float dy,
                                           float x, float y) {
    (void)handle; (void)dx; (void)dy; (void)x; (void)y;
}

#endif // !defined(__APPLE__) && !defined(__linux__)
