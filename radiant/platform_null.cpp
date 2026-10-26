// platform_null.cpp — the null Radiant platform backend for builds without a GUI
// (LAMBDA_NO_GUI: lambda-headless, D7.1.4v2). It replaces the former GLFW/GL
// stub file: the binary links neither GLFW nor OpenGL nor a native toolkit.
//
// Contract (platform.hpp): every call behaves as the desktop backend does
// before its windowing library is initialized — init fails, windows and
// cursors are never created, queries report zero, everything else is a no-op.
// Headless layout, render and event simulation already run in that state.

#ifdef LAMBDA_NO_GUI

#include "platform.hpp"
#include <stddef.h>

bool radiant_platform_init(bool) { return false; }
void radiant_platform_terminate(void) {}
double radiant_platform_time(void) { return 0.0; }
void radiant_platform_poll_events(void) {}
void radiant_platform_wait_events(double) {}
void radiant_platform_post_empty_event(void) {}

RadiantWindow* radiant_platform_create_window(int, int, const char*, bool) { return NULL; }
void radiant_platform_destroy_window(RadiantWindow*) {}
void radiant_platform_show_window(RadiantWindow*) {}
void radiant_platform_focus_window(RadiantWindow*) {}
bool radiant_platform_window_should_close(RadiantWindow*) { return true; }
void radiant_platform_set_window_should_close(RadiantWindow*, bool) {}
void radiant_platform_set_window_title(RadiantWindow*, const char*) {}
void radiant_platform_set_window_icon(RadiantWindow*, const RadiantIconImage*, int) {}
void radiant_platform_set_window_size(RadiantWindow*, int, int) {}
void radiant_platform_set_window_min_width(RadiantWindow*, int) {}
static void zero_pair(int* a, int* b) { if (a) *a = 0; if (b) *b = 0; }
void radiant_platform_get_window_size(RadiantWindow*, int* width, int* height) { zero_pair(width, height); }
void radiant_platform_get_window_pos(RadiantWindow*, int* x, int* y) { zero_pair(x, y); }
void radiant_platform_get_framebuffer_size(RadiantWindow*, int* width, int* height) {
    zero_pair(width, height);
}
void radiant_platform_get_content_scale(RadiantWindow*, float* x, float* y) {
    if (x) *x = 1.0f;
    if (y) *y = 1.0f;
}
bool radiant_platform_window_focused(RadiantWindow*) { return false; }
bool radiant_platform_window_hovered(RadiantWindow*) { return false; }
void* radiant_platform_native_window(RadiantWindow*) { return NULL; }

bool radiant_platform_read_placement(RadiantWindow*, RadiantWindowPlacement*) { return false; }
bool radiant_platform_apply_fullscreen(RadiantWindow*, bool, const RadiantWindowPlacement*) {
    return false;
}

void radiant_platform_attach_window(RadiantWindow*) {}
void radiant_platform_set_input_enabled(RadiantWindow*, bool) {}

void radiant_platform_get_cursor_pos(RadiantWindow*, double* x, double* y) {
    if (x) *x = 0.0;
    if (y) *y = 0.0;
}
void radiant_platform_set_relative_pointer(RadiantWindow*, bool) {}
void radiant_platform_set_cursor_hidden(RadiantWindow*, bool) {}
RadiantCursor* radiant_platform_create_standard_cursor(RadiantCursorShape) { return NULL; }
RadiantCursor* radiant_platform_create_image_cursor(const RadiantIconImage*, int, int) { return NULL; }
void radiant_platform_destroy_cursor(RadiantCursor*) {}
void radiant_platform_set_cursor(RadiantWindow*, RadiantCursor*) {}

const char* radiant_platform_get_clipboard(RadiantWindow*) { return NULL; }
void radiant_platform_set_clipboard(RadiantWindow*, const char*) {}

bool radiant_platform_present_init(RadiantWindow*) { return false; }
void radiant_platform_present(RadiantWindow*, const ImageSurface*, const Bound*, bool) {}

// --- native toolkit companions (ime_mac.mm, app_icon_mac.mm are not built) ---
extern "C" void radiant_ime_mac_attach(void*) {}
extern "C" void radiant_app_icon_mac_set(const unsigned char*, size_t) {}

#endif // LAMBDA_NO_GUI
