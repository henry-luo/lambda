// webview_handle_linux.h — Shared WebViewHandle definition for Linux backends
//
// Both child-window and layer-mode backends include this to share the handle type.
// Child mode: GTK popup window overlaid on the GLFW X11 window.
// Layer mode: GtkOffscreenWindow for headless rendering via cairo snapshot.

#ifndef RADIANT_WEBVIEW_HANDLE_LINUX_H
#define RADIANT_WEBVIEW_HANDLE_LINUX_H

#if defined(__linux__)

#include <webkit2/webkit2.h>
#include <gtk/gtk.h>
#include "../lib/file.h"

static inline const char* webview_linux_mime_for_path(const char* path) {
    if (!path) return "application/octet-stream";
    if (file_path_has_ext_ci(path, "html") || file_path_has_ext_ci(path, "htm")) return "text/html";
    if (file_path_has_ext_ci(path, "css")) return "text/css";
    if (file_path_has_ext_ci(path, "js")) return "application/javascript";
    if (file_path_has_ext_ci(path, "json")) return "application/json";
    if (file_path_has_ext_ci(path, "png")) return "image/png";
    if (file_path_has_ext_ci(path, "jpg") || file_path_has_ext_ci(path, "jpeg")) return "image/jpeg";
    if (file_path_has_ext_ci(path, "gif")) return "image/gif";
    if (file_path_has_ext_ci(path, "svg")) return "image/svg+xml";
    if (file_path_has_ext_ci(path, "webp")) return "image/webp";
    if (file_path_has_ext_ci(path, "woff")) return "font/woff";
    if (file_path_has_ext_ci(path, "woff2")) return "font/woff2";
    if (file_path_has_ext_ci(path, "ttf")) return "font/ttf";
    if (file_path_has_ext_ci(path, "otf")) return "font/otf";
    return "application/octet-stream";
}

void webview_linux_finish_lambda_scheme_request(WebKitURISchemeRequest* request,
                                                const char* log_prefix);

struct WebViewHandle {
    WebKitWebView* wk_view;     // the WebKitWebView instance
    int mode;                    // 0 = child window, 1 = layer (matches WebViewMode enum)

    // child window mode fields
    GtkWidget* gtk_window;      // GtkWindow (GTK_WINDOW_POPUP) hosting the web view
    GLFWwindow* glfw_window;    // GLFW window for screen-position queries
    float last_x, last_y;       // last positioned coords (CSS pixels, for repositioning)

    // layer mode fields
    GtkWidget* offscreen_win;   // GtkOffscreenWindow (NULL in child mode)
    bool dirty;                  // content changed, needs re-snapshot
    bool loaded;                 // initial navigation finished
    bool snapshot_in_progress;  // prevent concurrent snapshots

    // shared
    float width, height;        // logical CSS pixel size
    float pixel_ratio;          // DPI scale factor
};

#endif // __linux__
#endif // RADIANT_WEBVIEW_HANDLE_LINUX_H
