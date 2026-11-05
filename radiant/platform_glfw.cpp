// platform_glfw.cpp — the desktop Radiant platform backend: GLFW windows and
// input, presentation through the window's core-profile presenter (gl_core), GLFW cursors and the
// plain-text clipboard (platform.hpp, vibe/Lambda_Design_Mobile.md MB7).
// Radiant's key, button and modifier codes use GLFW's numbering, so input
// translation is the identity on codes; the static_asserts below hold it.

#ifndef LAMBDA_NO_GUI

#include "platform.hpp"
#include "event.hpp"
#include "view.hpp"
#include "gl_core.hpp"
#include "../lib/log.h"
#include <math.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// GLFW's native-access functions, declared by their pointer-sized ABI rather
// than through glfw3native.h: that header drags the Carbon/Win32 SDK headers
// into this view.hpp unit, where their Rect and min/max collide with Radiant's.
extern "C" {
#ifdef __APPLE__
void* glfwGetCocoaWindow(GLFWwindow* window);       // id (NSWindow*)
#elif defined(_WIN32)
void* glfwGetWin32Window(GLFWwindow* window);       // HWND
#else
unsigned long glfwGetX11Window(GLFWwindow* window); // X11 Window (XID)
#endif
}

// Windows OpenGL headers stop at 1.1; these are 1.2 constants.
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif

static_assert(RDT_KEY_A == GLFW_KEY_A && RDT_KEY_ESCAPE == GLFW_KEY_ESCAPE &&
              RDT_KEY_F25 == GLFW_KEY_F25 && RDT_KEY_KP_EQUAL == GLFW_KEY_KP_EQUAL &&
              RDT_KEY_MENU == GLFW_KEY_MENU && RDT_KEY_UNRECOGNIZED == GLFW_KEY_UNKNOWN,
              "Radiant key codes keep GLFW's numbering");
static_assert(RDT_MOUSE_BUTTON_LEFT == GLFW_MOUSE_BUTTON_LEFT &&
              RDT_MOUSE_BUTTON_RIGHT == GLFW_MOUSE_BUTTON_RIGHT &&
              RDT_MOUSE_BUTTON_MIDDLE == GLFW_MOUSE_BUTTON_MIDDLE,
              "Radiant mouse buttons keep GLFW's numbering");
static_assert(RDT_MOD_SHIFT == GLFW_MOD_SHIFT && RDT_MOD_CTRL == GLFW_MOD_CONTROL &&
              RDT_MOD_ALT == GLFW_MOD_ALT && RDT_MOD_SUPER == GLFW_MOD_SUPER,
              "Radiant modifier bits keep GLFW's values");

static GLFWwindow* glfw_window(RadiantWindow* window) { return (GLFWwindow*)window; }
static RadiantWindow* radiant_window(GLFWwindow* window) { return (RadiantWindow*)window; }

// Caps/Num Lock state bits are not Radiant modifiers.
static int glfw_mods(int mods) {
    return mods & (RDT_MOD_SHIFT | RDT_MOD_CTRL | RDT_MOD_ALT | RDT_MOD_SUPER);
}

// --- library lifetime and event pump ---

struct GlfwApplicationRuntime {
    bool initialized = false;
    ~GlfwApplicationRuntime() {
        if (initialized) glfwTerminate();
    }
};

bool radiant_platform_init(bool hidden_host) {
    // Cocoa's application launch is process-wide; a host must not retire its
    // delegate and make a later host wait for that one-time notification again.
    static GlfwApplicationRuntime application;
    if (application.initialized) return true;
    // Force X11 on Linux so the window is visible in mixed Wayland/XWayland sessions.
#if defined(__linux__) && defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
#ifdef __APPLE__
    if (hidden_host) glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
#else
    (void)hidden_host;
#endif
    application.initialized = glfwInit() == GLFW_TRUE;
    return application.initialized;
}

double radiant_platform_time(void) { return glfwGetTime(); }
void radiant_platform_poll_events(void) { glfwPollEvents(); }
void radiant_platform_wait_events(double timeout_seconds) { glfwWaitEventsTimeout(timeout_seconds); }
void radiant_platform_post_empty_event(void) { glfwPostEmptyEvent(); }

// --- windows ---

RadiantWindow* radiant_platform_create_window(int width, int height, const char* title,
                                              bool visible) {
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    // hidden graphics hosts never present native window decorations.
    glfwWindowHint(GLFW_DECORATED, visible ? GLFW_TRUE : GLFW_FALSE);
    // the same core profile as native graphics contexts, so the presenter can later sample their textures
    native_gl_core_profile_hints();
    GLFWwindow* window = glfwCreateWindow(width, height, title, NULL, NULL);
    glfwDefaultWindowHints();
    return radiant_window(window);
}

// a window's presenter lives in its GLFW user pointer (unused otherwise)
static NativeGlContext* glfw_presenter(RadiantWindow* window) {
    return window ? (NativeGlContext*)glfwGetWindowUserPointer(glfw_window(window)) : nullptr;
}

void radiant_platform_destroy_window(RadiantWindow* window) {
    if (!window) return;
    // the presenter releases its GL objects while its window's context still exists
    native_gl_destroy(glfw_presenter(window));
    glfwSetWindowUserPointer(glfw_window(window), nullptr);
    glfwDestroyWindow(glfw_window(window));
}
void radiant_platform_show_window(RadiantWindow* window) { glfwShowWindow(glfw_window(window)); }
void radiant_platform_focus_window(RadiantWindow* window) { glfwFocusWindow(glfw_window(window)); }
bool radiant_platform_window_should_close(RadiantWindow* window) {
    return glfwWindowShouldClose(glfw_window(window)) != 0;
}
void radiant_platform_set_window_should_close(RadiantWindow* window, bool close) {
    glfwSetWindowShouldClose(glfw_window(window), close ? GLFW_TRUE : GLFW_FALSE);
}
void radiant_platform_set_window_title(RadiantWindow* window, const char* title) {
    glfwSetWindowTitle(glfw_window(window), title);
}

void radiant_platform_set_window_icon(RadiantWindow* window, const RadiantIconImage* images,
                                      int count) {
    if (!window || !images || count <= 0) return;
    GLFWimage glfw_images[8];
    if (count > 8) count = 8;
    for (int i = 0; i < count; i++) {
        glfw_images[i] = {images[i].width, images[i].height, (unsigned char*)images[i].rgba};
    }
    // GLFW copies the pixels before returning.
    glfwSetWindowIcon(glfw_window(window), count, glfw_images);
}

void radiant_platform_set_window_size(RadiantWindow* window, int width, int height) {
    glfwSetWindowSize(glfw_window(window), width, height);
}
void radiant_platform_set_window_min_width(RadiantWindow* window, int width) {
    // GLFW 3.4 Cocoa drops both minima when either minimum dimension is DONT_CARE.
    glfwSetWindowSizeLimits(glfw_window(window), width, 0, GLFW_DONT_CARE, GLFW_DONT_CARE);
}
void radiant_platform_get_window_size(RadiantWindow* window, int* width, int* height) {
    glfwGetWindowSize(glfw_window(window), width, height);
}
void radiant_platform_get_window_pos(RadiantWindow* window, int* x, int* y) {
    glfwGetWindowPos(glfw_window(window), x, y);
}
void radiant_platform_get_framebuffer_size(RadiantWindow* window, int* width, int* height) {
    glfwGetFramebufferSize(glfw_window(window), width, height);
}
void radiant_platform_get_content_scale(RadiantWindow* window, float* x, float* y) {
    glfwGetWindowContentScale(glfw_window(window), x, y);
}
bool radiant_platform_window_focused(RadiantWindow* window) {
    return glfwGetWindowAttrib(glfw_window(window), GLFW_FOCUSED) != 0;
}
bool radiant_platform_window_hovered(RadiantWindow* window) {
    return glfwGetWindowAttrib(glfw_window(window), GLFW_HOVERED) != 0;
}

void* radiant_platform_native_window(RadiantWindow* window) {
    if (!window) return NULL;
#ifdef __APPLE__
    return glfwGetCocoaWindow(glfw_window(window));
#elif defined(_WIN32)
    return glfwGetWin32Window(glfw_window(window));
#else
    return (void*)(uintptr_t)glfwGetX11Window(glfw_window(window));
#endif
}

// --- fullscreen ---

static GLFWmonitor* glfw_fullscreen_monitor(RadiantWindowBounds bounds, float* overlap) {
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* selected = glfwGetPrimaryMonitor();
    *overlap = 0;
    for (int i = 0; i < count; i++) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        if (!mode) continue;
        int x, y;
        glfwGetMonitorPos(monitors[i], &x, &y);
        float area = radiant_fullscreen_overlap(bounds,
            {(float)x, (float)y, (float)mode->width, (float)mode->height});
        if (area > *overlap) { *overlap = area; selected = monitors[i]; }
    }
    return selected;
}

bool radiant_platform_read_placement(RadiantWindow* window, RadiantWindowPlacement* placement) {
    if (!window) return false;
    int count = 0;
    glfwGetMonitors(&count);
    if (!count) return false;
    int x, y, width, height;
    glfwGetWindowPos(glfw_window(window), &x, &y);
    glfwGetWindowSize(glfw_window(window), &width, &height);
    placement->bounds = {(float)x, (float)y, (float)width, (float)height};
    placement->maximized = glfwGetWindowAttrib(glfw_window(window), GLFW_MAXIMIZED) != 0;
    placement->fullscreen = glfwGetWindowMonitor(glfw_window(window)) != nullptr;
    return true;
}

bool radiant_platform_apply_fullscreen(RadiantWindow* window, bool enabled,
                                       const RadiantWindowPlacement* restore) {
    RadiantWindowPlacement current = {};
    if (!radiant_platform_read_placement(window, &current)) return false;
    float overlap;
    GLFWmonitor* monitor = glfw_fullscreen_monitor(
        enabled ? current.bounds : restore->bounds, &overlap);
    if (!monitor) return false;
    glfwGetError(nullptr);
    if (enabled) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (!mode) return false;
        glfwSetWindowMonitor(glfw_window(window), monitor, 0, 0,
                             mode->width, mode->height, mode->refreshRate);
    } else {
        RadiantWindowBounds bounds = restore->bounds;
        // a disconnected display must not leave the restored window unreachable.
        if (overlap <= 0) {
            int x, y, width, height;
            glfwGetMonitorWorkarea(monitor, &x, &y, &width, &height);
            bounds = radiant_fullscreen_fit(bounds,
                {(float)x, (float)y, (float)width, (float)height});
        }
        int x = (int)lroundf(bounds.x); // INT_CAST_OK: GLFW screen-coordinate boundary
        int y = (int)lroundf(bounds.y); // INT_CAST_OK: GLFW screen-coordinate boundary
        int width = (int)lroundf(bounds.width); // INT_CAST_OK: GLFW window-size boundary
        int height = (int)lroundf(bounds.height); // INT_CAST_OK: GLFW window-size boundary
        glfwSetWindowMonitor(glfw_window(window), nullptr, x, y, width, height, GLFW_DONT_CARE);
        if (restore->maximized) glfwMaximizeWindow(glfw_window(window));
    }
    const char* message = nullptr;
    if (glfwGetError(&message) != GLFW_NO_ERROR) {
        log_error("fullscreen: platform transition failed: %s", message ? message : "unknown error");
        return false;
    }
    log_info("fullscreen: platform requested %s", enabled ? "fullscreen" : "windowed");
    return true;
}

// --- input translation ---

static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    RdtEvent event;
    rdt_event_set_key(&event,
        (action == GLFW_PRESS || action == GLFW_REPEAT) ? RDT_EVENT_KEY_DOWN : RDT_EVENT_KEY_UP,
        key, glfw_mods(mods), scancode, glfwGetTime());
    event.key.repeat = action == GLFW_REPEAT;
    radiant_host_input(radiant_window(window), &event);
}

static void glfw_char_callback(GLFWwindow* window, unsigned int codepoint) {
    RdtEvent event;
    rdt_event_set_text_input(&event, codepoint, glfwGetTime());
    radiant_host_input(radiant_window(window), &event);
}

// GLFW reports logical window coordinates, so the platform scale is 1:1.
static RdtLogicalPoint glfw_logical_point(double x, double y) {
    return rdt_platform_to_logical_point((float)x, (float)y, 1.0f, 1.0f);
}

static void glfw_cursor_position_callback(GLFWwindow* window, double x, double y) {
    RdtEvent event;
    RdtLogicalPoint logical = glfw_logical_point(x, y);
    rdt_event_set_mouse_position(&event, RDT_EVENT_MOUSE_MOVE, logical.x, logical.y, glfwGetTime());
    radiant_host_input(radiant_window(window), &event);
}

static void glfw_cursor_enter_callback(GLFWwindow* window, int entered) {
    if (!entered) radiant_host_pointer_left(radiant_window(window));
}

static void glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    RdtLogicalPoint logical = glfw_logical_point(x, y);
    RdtEvent event;
    rdt_event_set_mouse_button(&event,
        action == GLFW_PRESS ? RDT_EVENT_MOUSE_DOWN : RDT_EVENT_MOUSE_UP,
        logical.x, logical.y, button, 1, glfw_mods(mods), glfwGetTime());
    radiant_host_input(radiant_window(window), &event);
}

static void glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    RdtLogicalPoint logical = glfw_logical_point(x, y);
    RdtEvent event;
    rdt_event_set_scroll(&event, logical.x, logical.y, xoffset, yoffset, glfwGetTime());
    radiant_host_input(radiant_window(window), &event);
}

void radiant_platform_set_input_enabled(RadiantWindow* window, bool enabled) {
    GLFWwindow* w = glfw_window(window);
    if (!w) return;
    glfwSetKeyCallback(w, enabled ? glfw_key_callback : NULL);
    glfwSetCharCallback(w, enabled ? glfw_char_callback : NULL);
    glfwSetCursorPosCallback(w, enabled ? glfw_cursor_position_callback : NULL);
    glfwSetCursorEnterCallback(w, enabled ? glfw_cursor_enter_callback : NULL);
    glfwSetMouseButtonCallback(w, enabled ? glfw_mouse_button_callback : NULL);
    glfwSetScrollCallback(w, enabled ? glfw_scroll_callback : NULL);
    log_info("platform-glfw: input callbacks %s", enabled ? "registered" : "removed");
}

// --- window notifications ---

static void glfw_framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    log_debug("platform-glfw: framebuffer resized to %d x %d", width, height);
    radiant_host_metrics_changed(radiant_window(window));
}
static void glfw_content_scale_callback(GLFWwindow* window, float scale_x, float scale_y) {
    // framebuffer and content-scale notifications may arrive separately; the
    // render turn queries both sizes and publishes them once
    log_debug("platform-glfw: content scale changed to %.3f x %.3f", scale_x, scale_y);
    radiant_host_metrics_changed(radiant_window(window));
}
static void glfw_refresh_callback(GLFWwindow* window) { radiant_host_refresh(radiant_window(window)); }
static void glfw_close_callback(GLFWwindow* window) { radiant_host_close_requested(radiant_window(window)); }
static void glfw_focus_callback(GLFWwindow* window, int focused) {
    radiant_host_focus_changed(radiant_window(window), focused != 0);
}
static void glfw_bounds_callback(GLFWwindow* window, int, int) {
    radiant_host_bounds_changed(radiant_window(window));
}
static void glfw_size_callback(GLFWwindow* window, int, int) {
    radiant_host_size_changed(radiant_window(window));
}
static void glfw_maximize_callback(GLFWwindow* window, int) {
    radiant_host_metrics_changed(radiant_window(window));
}
// GLFW's monitor callback is global; the viewer has one window.
static GLFWwindow* g_attached_window;
static void glfw_monitor_callback(GLFWmonitor*, int) {
    radiant_host_metrics_changed(radiant_window(g_attached_window));
}

void radiant_platform_attach_window(RadiantWindow* window) {
    GLFWwindow* w = glfw_window(window);
    if (!w) return;
    g_attached_window = w;
    // receive the state of the Caps Lock and Num Lock keys
    glfwSetInputMode(w, GLFW_LOCK_KEY_MODS, GLFW_TRUE);
    glfwSetFramebufferSizeCallback(w, glfw_framebuffer_size_callback);
    glfwSetWindowContentScaleCallback(w, glfw_content_scale_callback);
    glfwSetWindowRefreshCallback(w, glfw_refresh_callback);
    glfwSetWindowCloseCallback(w, glfw_close_callback);
    glfwSetWindowFocusCallback(w, glfw_focus_callback);
    glfwSetWindowPosCallback(w, glfw_bounds_callback);
    glfwSetWindowSizeCallback(w, glfw_size_callback);
    glfwSetWindowMaximizeCallback(w, glfw_maximize_callback);
    glfwSetMonitorCallback(glfw_monitor_callback);
}

// --- pointer and cursors ---

void radiant_platform_get_cursor_pos(RadiantWindow* window, double* x, double* y) {
    glfwGetCursorPos(glfw_window(window), x, y);
}

void radiant_platform_set_relative_pointer(RadiantWindow* window, bool enabled) {
    GLFWwindow* w = glfw_window(window);
    if (!w) return;
    if (enabled) {
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(w, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    } else {
        if (glfwRawMouseMotionSupported()) glfwSetInputMode(w, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void radiant_platform_set_cursor_hidden(RadiantWindow* window, bool hidden) {
    glfwSetInputMode(glfw_window(window), GLFW_CURSOR, hidden ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_NORMAL);
}

RadiantCursor* radiant_platform_create_standard_cursor(RadiantCursorShape shape) {
    int glfw_shape = GLFW_ARROW_CURSOR;
    switch (shape) {
        case RADIANT_CURSOR_ARROW: glfw_shape = GLFW_ARROW_CURSOR; break;
        case RADIANT_CURSOR_IBEAM: glfw_shape = GLFW_IBEAM_CURSOR; break;
        case RADIANT_CURSOR_CROSSHAIR: glfw_shape = GLFW_CROSSHAIR_CURSOR; break;
        case RADIANT_CURSOR_HAND: glfw_shape = GLFW_HAND_CURSOR; break;
        case RADIANT_CURSOR_EW_RESIZE: glfw_shape = GLFW_HRESIZE_CURSOR; break;
        case RADIANT_CURSOR_NS_RESIZE: glfw_shape = GLFW_VRESIZE_CURSOR; break;
#ifdef GLFW_RESIZE_NWSE_CURSOR
        case RADIANT_CURSOR_NWSE_RESIZE: glfw_shape = GLFW_RESIZE_NWSE_CURSOR; break;
        case RADIANT_CURSOR_NESW_RESIZE: glfw_shape = GLFW_RESIZE_NESW_CURSOR; break;
        case RADIANT_CURSOR_ALL_RESIZE: glfw_shape = GLFW_RESIZE_ALL_CURSOR; break;
#else
        default: break;
#endif
    }
    return (RadiantCursor*)glfwCreateStandardCursor(glfw_shape);
}

RadiantCursor* radiant_platform_create_image_cursor(const RadiantIconImage* image,
                                                    int hotspot_x, int hotspot_y) {
    if (!image) return NULL;
    GLFWimage glfw_image = {image->width, image->height, (unsigned char*)image->rgba};
    return (RadiantCursor*)glfwCreateCursor(&glfw_image, hotspot_x, hotspot_y);
}

void radiant_platform_destroy_cursor(RadiantCursor* cursor) {
    if (cursor) glfwDestroyCursor((GLFWcursor*)cursor);
}
void radiant_platform_set_cursor(RadiantWindow* window, RadiantCursor* cursor) {
    glfwSetCursor(glfw_window(window), (GLFWcursor*)cursor);
}

// --- clipboard ---

const char* radiant_platform_get_clipboard(RadiantWindow* window) {
    return window ? glfwGetClipboardString(glfw_window(window)) : NULL;
}
void radiant_platform_set_clipboard(RadiantWindow* window, const char* text) {
    if (window) glfwSetClipboardString(glfw_window(window), text);
}

// --- presentation: the CPU surface becomes one texture drawn as a quad ---

// the window's presenter, created on first use with the window's context current
static NativeGlContext* glfw_presenter_ensure(RadiantWindow* window) {
    NativeGlContext* presenter = glfw_presenter(window);
    if (presenter || !window) return presenter;
    glfwMakeContextCurrent(glfw_window(window));
    char diagnostic[256];
    presenter = native_gl_presenter_create(glfw_window(window), diagnostic, sizeof(diagnostic));
    if (!presenter) { log_error("platform: window presenter unavailable: %s", diagnostic); return nullptr; }
    glfwSetWindowUserPointer(glfw_window(window), presenter);
    return presenter;
}

bool radiant_platform_present_init(RadiantWindow* window) {
    if (!window) return false;
    glfwMakeContextCurrent(glfw_window(window));
    glfwSwapInterval(1);  // vsync
    return glfw_presenter_ensure(window) != nullptr;
}

void radiant_platform_present(RadiantWindow* window, const ImageSurface* surface,
                              const Bound* damage, bool partial, uint32_t background_rgba,
                              float pixel_scale_x, float pixel_scale_y,
                              const RadiantPresentLayers* composite) {
    NativeGlContext* presenter = glfw_presenter_ensure(window);
    if (!presenter || !surface || !surface->pixels) return;
    int framebuffer_width = 0, framebuffer_height = 0;
    glfwGetFramebufferSize(glfw_window(window), &framebuffer_width, &framebuffer_height);
    if (framebuffer_width <= 0 || framebuffer_height <= 0) return;

    // only display density scales the retained image; a larger viewport must
    // not zoom fixed-size content before the new layout arrives.
    int present_width = (int)lroundf(surface->width * pixel_scale_x); // INT_CAST_OK: GL drawable extent
    int present_height = (int)lroundf(surface->height * pixel_scale_y); // INT_CAST_OK: GL drawable extent
    NativeGlPresentFrame frame = {};
    frame.page = surface;
    frame.damage = damage;
    frame.partial = partial;
    frame.background_rgba = background_rgba;
    frame.present_width = present_width; frame.present_height = present_height;
    frame.framebuffer_width = framebuffer_width; frame.framebuffer_height = framebuffer_height;
    if (composite && composite->count) {
        frame.layers = composite->layers;
        frame.layer_count = composite->count;
        frame.page_hidden = composite->surface_hidden;
        frame.reference = composite->reference;
    }
    if (!native_gl_present(presenter, &frame))
        log_error("platform: window present failed: %s", native_gl_diagnostic(presenter));
    glfwSwapBuffers(glfw_window(window));
}

#endif // LAMBDA_NO_GUI
