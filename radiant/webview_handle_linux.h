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
