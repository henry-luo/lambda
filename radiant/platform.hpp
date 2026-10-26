#pragma once
// platform.hpp — Radiant's platform backend seam (vibe/Lambda_Design_Mobile.md
// MB7). Radiant sources name only these functions and opaque handles; exactly
// one backend per build defines them: platform_glfw.cpp on desktop,
// platform_null.cpp for builds without a GUI (LAMBDA_NO_GUI, D7.1.4v2), and a
// UIKit backend on iOS. The backend translates native input into RdtEvent and
// reports it, and window notifications, through the radiant_host_* functions
// at the end of this header; host policy (history keys, close guards, click
// series, redraw) stays in window.cpp.
//
// Null-backend contract: every call behaves as the desktop backend does before
// its windowing library is initialized — init fails, window/cursor creation
// returns NULL, queries report zero, everything else is a no-op. Headless runs
// already take that path, so the same Radiant sources serve every profile.

#include <stdint.h>
#include "window_fullscreen.hpp"

union RdtEvent;
struct ImageSurface;
struct Bound;

typedef struct RadiantWindow RadiantWindow;   // a native top-level window
typedef struct RadiantCursor RadiantCursor;   // a native pointer cursor

typedef enum {
    RADIANT_CURSOR_ARROW,
    RADIANT_CURSOR_IBEAM,
    RADIANT_CURSOR_CROSSHAIR,
    RADIANT_CURSOR_HAND,
    RADIANT_CURSOR_EW_RESIZE,
    RADIANT_CURSOR_NS_RESIZE,
    RADIANT_CURSOR_NWSE_RESIZE,
    RADIANT_CURSOR_NESW_RESIZE,
    RADIANT_CURSOR_ALL_RESIZE,
} RadiantCursorShape;

typedef struct RadiantIconImage {
    int width, height;
    const uint8_t* rgba;   // straight (non-premultiplied) RGBA, row-major
} RadiantIconImage;

// --- library lifetime and event pump ---
// `hidden_host`: a windowless run that only needs a native graphics/webview
// parent; it must not claim the application menu bar
bool radiant_platform_init(bool hidden_host);
void radiant_platform_terminate(void);
double radiant_platform_time(void);                  // seconds on the backend's clock
void radiant_platform_poll_events(void);
void radiant_platform_wait_events(double timeout_seconds);
void radiant_platform_post_empty_event(void);        // wakes a blocked wait from any thread

// --- windows ---
RadiantWindow* radiant_platform_create_window(int width, int height, const char* title,
                                              bool visible);
void radiant_platform_destroy_window(RadiantWindow* window);
void radiant_platform_show_window(RadiantWindow* window);
void radiant_platform_focus_window(RadiantWindow* window);
bool radiant_platform_window_should_close(RadiantWindow* window);
void radiant_platform_set_window_should_close(RadiantWindow* window, bool close);
void radiant_platform_set_window_title(RadiantWindow* window, const char* title);
void radiant_platform_set_window_icon(RadiantWindow* window, const RadiantIconImage* images,
                                      int count);
void radiant_platform_set_window_size(RadiantWindow* window, int width, int height);
void radiant_platform_set_window_min_width(RadiantWindow* window, int width);
void radiant_platform_get_window_size(RadiantWindow* window, int* width, int* height);
void radiant_platform_get_window_pos(RadiantWindow* window, int* x, int* y);
void radiant_platform_get_framebuffer_size(RadiantWindow* window, int* width, int* height);
void radiant_platform_get_content_scale(RadiantWindow* window, float* x, float* y);
bool radiant_platform_window_focused(RadiantWindow* window);
bool radiant_platform_window_hovered(RadiantWindow* window);
// NSWindow* on macOS, HWND on Windows, the X11 Window id on Linux; NULL otherwise
void* radiant_platform_native_window(RadiantWindow* window);

// --- fullscreen: the RadiantFullscreenBackend operations for one window ---
bool radiant_platform_read_placement(RadiantWindow* window, RadiantWindowPlacement* placement);
bool radiant_platform_apply_fullscreen(RadiantWindow* window, bool enabled,
                                       const RadiantWindowPlacement* restore);

// --- host attachment: start window notifications; input is gated separately
// so a replayed event file owns the pointer and keyboard ---
void radiant_platform_attach_window(RadiantWindow* window);
void radiant_platform_set_input_enabled(RadiantWindow* window, bool enabled);

// --- pointer and cursors ---
void radiant_platform_get_cursor_pos(RadiantWindow* window, double* x, double* y);
void radiant_platform_set_relative_pointer(RadiantWindow* window, bool enabled);
void radiant_platform_set_cursor_hidden(RadiantWindow* window, bool hidden);
RadiantCursor* radiant_platform_create_standard_cursor(RadiantCursorShape shape);
RadiantCursor* radiant_platform_create_image_cursor(const RadiantIconImage* image,
                                                    int hotspot_x, int hotspot_y);
void radiant_platform_destroy_cursor(RadiantCursor* cursor);
void radiant_platform_set_cursor(RadiantWindow* window, RadiantCursor* cursor);

// --- clipboard (plain text) ---
const char* radiant_platform_get_clipboard(RadiantWindow* window);
void radiant_platform_set_clipboard(RadiantWindow* window, const char* text);

// --- presentation of Radiant's CPU-rendered surface ---
bool radiant_platform_present_init(RadiantWindow* window);
// Shows `surface` in `window`. With `partial`, only `damage` changed since the
// previous present of the same-sized surface; otherwise everything did.
void radiant_platform_present(RadiantWindow* window, const ImageSurface* surface,
                              const Bound* damage, bool partial);

// --- host entry points the backend calls (defined by window.cpp) ---
void radiant_host_input(RadiantWindow* window, union RdtEvent* event);
void radiant_host_pointer_left(RadiantWindow* window);
void radiant_host_close_requested(RadiantWindow* window);
void radiant_host_refresh(RadiantWindow* window);
void radiant_host_focus_changed(RadiantWindow* window, bool focused);
void radiant_host_bounds_changed(RadiantWindow* window);
void radiant_host_size_changed(RadiantWindow* window);
void radiant_host_metrics_changed(RadiantWindow* window);  // framebuffer, scale, maximize, monitors
