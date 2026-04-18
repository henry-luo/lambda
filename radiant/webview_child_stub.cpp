// webview_child_stub.cpp — no-op stub for platforms without native web view support
//
// Compiled on Linux/Windows (until those backends are implemented) and headless builds.

#ifndef __APPLE__

#include "webview.h"

extern "C" {
#include "../lib/log.h"
}

WebViewHandle* webview_platform_create(GLFWwindow* window,
                                       float x, float y, float w, float h,
                                       float pixel_ratio) {
    log_info("webview_platform_create: stub (no native web view on this platform)");
    return nullptr;
}

void webview_platform_destroy(WebViewHandle* handle) {
    (void)handle;
}

void webview_platform_navigate(WebViewHandle* handle, const char* url) {
    (void)handle; (void)url;
}

void webview_platform_set_html(WebViewHandle* handle, const char* html) {
    (void)handle; (void)html;
}

void webview_platform_eval_js(WebViewHandle* handle, const char* js) {
    (void)handle; (void)js;
}

void webview_platform_set_bounds(WebViewHandle* handle,
                                 float x, float y, float w, float h,
                                 float pixel_ratio) {
    (void)handle; (void)x; (void)y; (void)w; (void)h; (void)pixel_ratio;
}

void webview_platform_set_visible(WebViewHandle* handle, bool visible) {
    (void)handle; (void)visible;
}

#endif // !__APPLE__
