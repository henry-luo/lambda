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

static inline NSString* webview_mac_mime_for_path(NSString* path) {
    NSString* ext = path.pathExtension.lowercaseString;
    if ([ext isEqualToString:@"html"] || [ext isEqualToString:@"htm"]) return @"text/html";
    if ([ext isEqualToString:@"css"]) return @"text/css";
    if ([ext isEqualToString:@"js"]) return @"application/javascript";
    if ([ext isEqualToString:@"json"]) return @"application/json";
    if ([ext isEqualToString:@"png"]) return @"image/png";
    if ([ext isEqualToString:@"jpg"] || [ext isEqualToString:@"jpeg"]) return @"image/jpeg";
    if ([ext isEqualToString:@"gif"]) return @"image/gif";
    if ([ext isEqualToString:@"svg"]) return @"image/svg+xml";
    if ([ext isEqualToString:@"webp"]) return @"image/webp";
    if ([ext isEqualToString:@"woff"]) return @"font/woff";
    if ([ext isEqualToString:@"woff2"]) return @"font/woff2";
    if ([ext isEqualToString:@"ttf"]) return @"font/ttf";
    if ([ext isEqualToString:@"otf"]) return @"font/otf";
    return @"application/octet-stream";
}

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
