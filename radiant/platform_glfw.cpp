// platform_glfw.cpp — the desktop Radiant platform backend: GLFW windows and
// input, a legacy-OpenGL texture blit for presentation, GLFW cursors and the
// plain-text clipboard (platform.hpp, vibe/Lambda_Design_Mobile.md MB7).
// Radiant's key, button and modifier codes use GLFW's numbering, so input
// translation is the identity on codes; the static_asserts below hold it.

#ifndef LAMBDA_NO_GUI

#include "platform.hpp"
#include "event.hpp"
#include "view.hpp"
#include "../lib/log.h"
#include <math.h>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#endif
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

bool radiant_platform_init(bool hidden_host) {
    // Force X11 on Linux so the window is visible in mixed Wayland/XWayland sessions.
#if defined(__linux__) && defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
#ifdef __APPLE__
    if (hidden_host) glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
#else
    (void)hidden_host;
#endif
    return glfwInit() == GLFW_TRUE;
}

void radiant_platform_terminate(void) { glfwTerminate(); }
double radiant_platform_time(void) { return glfwGetTime(); }
void radiant_platform_poll_events(void) { glfwPollEvents(); }
void radiant_platform_wait_events(double timeout_seconds) { glfwWaitEventsTimeout(timeout_seconds); }
void radiant_platform_post_empty_event(void) { glfwPostEmptyEvent(); }

// --- windows ---

RadiantWindow* radiant_platform_create_window(int width, int height, const char* title,
                                              bool visible) {
    glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(width, height, title, NULL, NULL);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    return radiant_window(window);
}

void radiant_platform_destroy_window(RadiantWindow* window) {
    if (window) glfwDestroyWindow(glfw_window(window));
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
    glfwSetWindowSizeCallback(w, glfw_bounds_callback);
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

bool radiant_platform_present_init(RadiantWindow* window) {
    if (!window) return false;
    glfwMakeContextCurrent(glfw_window(window));
    glfwSwapInterval(1);  // vsync
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // disable byte-alignment restriction
    glClearColor(0.8f, 0.8f, 0.8f, 1.0f); // light grey behind a partially drawn surface
    return true;
}

void radiant_platform_present(RadiantWindow* window, const ImageSurface* surface,
                              const Bound* damage, bool partial) {
    if (!window || !surface || !surface->pixels) return;
    int framebuffer_width = 0, framebuffer_height = 0;
    glfwGetFramebufferSize(glfw_window(window), &framebuffer_width, &framebuffer_height);
    if (framebuffer_width <= 0 || framebuffer_height <= 0) return;

    // set up the viewport and projection for one full-window quad
    glViewport(0, 0, framebuffer_width, framebuffer_height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClear(GL_COLOR_BUFFER_BIT);

    bool exact_framebuffer_present =
        framebuffer_width == surface->width && framebuffer_height == surface->height;
    // One texture persists across presents (Radiant_Design_Animation_Repaint §3 A3): a frame
    // that wrote only a damage rect uploads just those rows and columns.
    static GLuint texture = 0;
    static int texture_width = 0, texture_height = 0;
    bool reuse = texture && glIsTexture(texture) && texture_width == surface->width &&
        texture_height == surface->height;
    if (!reuse) {
        if (texture && glIsTexture(texture)) glDeleteTextures(1, &texture);
        glGenTextures(1, &texture);
        texture_width = surface->width;
        texture_height = surface->height;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    if (!reuse || !partial) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surface->width, surface->height, 0,
            GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV, surface->pixels);
    } else if (damage && damage->right > damage->left && damage->bottom > damage->top) {
        int x0 = (int)fmaxf(0.0f, floorf(damage->left)); // INT_CAST_OK: texel column
        int y0 = (int)fmaxf(0.0f, floorf(damage->top)); // INT_CAST_OK: texel row
        int x1 = (int)fminf((float)surface->width, ceilf(damage->right)); // INT_CAST_OK: texel column
        int y1 = (int)fminf((float)surface->height, ceilf(damage->bottom)); // INT_CAST_OK: texel row
        if (x1 > x0 && y1 > y0) {
            glPixelStorei(GL_UNPACK_ROW_LENGTH, surface->pitch / 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x1 - x0, y1 - y0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8_REV,
                (const uint8_t*)surface->pixels + (size_t)y0 * surface->pitch + (size_t)x0 * 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint present_filter = exact_framebuffer_present ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, present_filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, present_filter);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(-1, -1);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(1, -1);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(1, 1);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    glfwSwapBuffers(glfw_window(window));
    glFinish(); // wait until the frame is visible, which keeps live resizing smooth
}

#endif // LAMBDA_NO_GUI
