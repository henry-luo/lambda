// webview_handle_mac.h — Shared WebViewHandle definition for macOS backends
//
// Both child-window and layer-mode backends include this to share the handle type.
// The handle is mode-aware: child mode uses parent_view for positioning,
// layer mode uses width/height/dirty for offscreen snapshot management.

#ifndef RADIANT_WEBVIEW_HANDLE_MAC_H
#define RADIANT_WEBVIEW_HANDLE_MAC_H

#ifdef __APPLE__

#import <WebKit/WebKit.h>

@class LambdaSchemeHandler;

struct WebViewHandle {
    WKWebView* wk_view;        // the native web view
    int mode;                   // 0 = child window, 1 = layer (matches WebViewMode enum)

    // child window mode fields
    NSView* parent_view;        // GLFW window's content view (NULL for layer mode)
    LambdaSchemeHandler* scheme_handler;  // retained lambda:// handler (child mode)

    // layer mode fields
    float width, height;        // logical size in CSS pixels (layer mode)
    bool dirty;                 // content has changed since last snapshot (layer mode)
    bool loaded;                // initial navigation finished
    bool snapshot_in_progress;  // prevent concurrent snapshots (layer mode)

    // shared
    float pixel_ratio;          // DPI scale factor
};

#endif // __APPLE__
#endif // RADIANT_WEBVIEW_HANDLE_MAC_H
