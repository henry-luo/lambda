// webview_layer_linux.cpp — Linux WebKitGTK offscreen layer backend
//
// Creates a WebKitWebView inside a GtkOffscreenWindow for headless rendering.
// Content is captured via webkit_web_view_get_snapshot() → cairo_surface_t,
// then converted from cairo's ARGB32 (premultiplied) to RGBA and stored in an
// ImageSurface for compositing by Radiant's render pass.
// Events are injected via JavaScript dispatchEvent (same approach as macOS).
//
// Requires: webkit2gtk-4.1 (or webkit2gtk-4.0 as fallback)

#if defined(__linux__)

#include <webkit2/webkit2.h>
#include <gtk/gtk.h>
#include <cairo/cairo.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "webview.h"
#include "webview_handle_linux.h"

// ImageSurface and mem_ helpers
#define Rect RadiantRect
#include "view.hpp"
#undef Rect

extern "C" {
#include "../lib/log.h"
#include "../lib/mem.h"
}

// ---------------------------------------------------------------------------
// GTK initialization (shared with child backend via weak symbol pattern)
// ---------------------------------------------------------------------------

static bool s_gtk_layer_initialized = false;

static void ensure_gtk_layer_initialized() {
    if (s_gtk_layer_initialized) return;
    int argc = 0;
    if (!gtk_init_check(&argc, nullptr)) {
        log_error("webview_layer_linux: gtk_init_check failed");
        return;
    }
    s_gtk_layer_initialized = true;
    log_info("webview_layer_linux: GTK initialized");
}

// ---------------------------------------------------------------------------
// IPC bridge + MutationObserver dirty tracking script
// ---------------------------------------------------------------------------

static const char* k_layer_bridge_js =
    "window.__lambda = {"
    "  invoke: function(command, payload) {"
    "    var msg = JSON.stringify({ command: command, payload: payload });"
    "    window.webkit.messageHandlers.lambda.postMessage(msg);"
    "  },"
    "  _callbacks: {},"
    "  on: function(event, callback) { this._callbacks[event] = callback; }"
    "};"
    // MutationObserver to auto-mark dirty on DOM mutations
    "new MutationObserver(function() {"
    "  window.webkit.messageHandlers.lambda.postMessage("
    "    JSON.stringify({ command: '__dirty', payload: {} })"
    "  );"
    "}).observe(document, {"
    "  childList: true, subtree: true, attributes: true, characterData: true"
    "});";

// ---------------------------------------------------------------------------
// Navigation callbacks
// ---------------------------------------------------------------------------

static void on_layer_load_changed(WebKitWebView* view, WebKitLoadEvent event,
                                   gpointer user_data) {
    (void)view;
    WebViewHandle* handle = (WebViewHandle*)user_data;
    if (event == WEBKIT_LOAD_FINISHED) {
        handle->loaded = true;
        handle->dirty  = true;
        log_info("webview_layer_linux: navigation finished");
    }
}

static void on_layer_load_failed(WebKitWebView* view, WebKitLoadEvent event,
                                  const char* uri, GError* error, gpointer user_data) {
    (void)view; (void)event; (void)user_data;
    log_error("webview_layer_linux: navigation failed for %s: %s", uri, error->message);
}

// ---------------------------------------------------------------------------
// IPC / dirty message handler
// ---------------------------------------------------------------------------

static void on_layer_script_message(WebKitUserContentManager* manager,
                                     WebKitJavascriptResult* result,
                                     gpointer user_data) {
    (void)manager;
    WebViewHandle* handle = (WebViewHandle*)user_data;
    if (handle) {
        handle->dirty = true;
    }
    // check if it's a real IPC message (not just the internal __dirty ping)
#if WEBKIT_CHECK_VERSION(2, 22, 0)
    JSCValue* value = webkit_javascript_result_get_js_value(result);
    if (jsc_value_is_string(value)) {
        char* msg = jsc_value_to_string(value);
        if (strstr(msg, "__dirty") == nullptr) {
            log_info("webview layer IPC (linux): received message: %s", msg);
            // TODO: dispatch to Lambda runtime
        }
        g_free(msg);
    }
#else
    (void)result;
#endif
}

// ---------------------------------------------------------------------------
// lambda:// URI scheme handler (same logic as child backend)
// ---------------------------------------------------------------------------

static const char* layer_mime_for_ext(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    dot++;
    if      (strcasecmp(dot, "html") == 0 || strcasecmp(dot, "htm") == 0) return "text/html";
    else if (strcasecmp(dot, "css")  == 0) return "text/css";
    else if (strcasecmp(dot, "js")   == 0) return "application/javascript";
    else if (strcasecmp(dot, "json") == 0) return "application/json";
    else if (strcasecmp(dot, "png")  == 0) return "image/png";
    else if (strcasecmp(dot, "jpg")  == 0 || strcasecmp(dot, "jpeg") == 0) return "image/jpeg";
    else if (strcasecmp(dot, "gif")  == 0) return "image/gif";
    else if (strcasecmp(dot, "svg")  == 0) return "image/svg+xml";
    else if (strcasecmp(dot, "woff") == 0) return "font/woff";
    else if (strcasecmp(dot, "woff2")== 0) return "font/woff2";
    return "application/octet-stream";
}

static void on_layer_lambda_scheme(WebKitURISchemeRequest* request, gpointer user_data) {
    (void)user_data;
    const char* path = webkit_uri_scheme_request_get_path(request);
    if (!path) path = "/";
    const char* rel = (path[0] == '/') ? path + 1 : path;

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        GError* err = g_error_new(G_FILE_ERROR, G_FILE_ERROR_NOENT, "failed to get CWD");
        webkit_uri_scheme_request_finish_error(request, err);
        g_error_free(err);
        return;
    }

    char full_path[8192];
    snprintf(full_path, sizeof(full_path), "%s/%s", cwd, rel);

    char* resolved = realpath(full_path, nullptr);
    if (!resolved) {
        GError* err = g_error_new(G_FILE_ERROR, G_FILE_ERROR_NOENT,
                                  "file not found: %s", full_path);
        webkit_uri_scheme_request_finish_error(request, err);
        g_error_free(err);
        return;
    }

    char* cwd_resolved = realpath(cwd, nullptr);
    if (!cwd_resolved || strncmp(resolved, cwd_resolved, strlen(cwd_resolved)) != 0) {
        log_error("webview_layer_linux lambda://: path traversal blocked: %s", resolved);
        if (cwd_resolved) g_free(cwd_resolved);
        g_free(resolved);
        GError* err = g_error_new(G_FILE_ERROR, G_FILE_ERROR_ACCES, "path traversal blocked");
        webkit_uri_scheme_request_finish_error(request, err);
        g_error_free(err);
        return;
    }
    g_free(cwd_resolved);

    GError* file_err = nullptr;
    GMappedFile* mapped = g_mapped_file_new(resolved, FALSE, &file_err);
    g_free(resolved);

    if (!mapped) {
        webkit_uri_scheme_request_finish_error(request, file_err);
        g_error_free(file_err);
        return;
    }

    gsize data_len = g_mapped_file_get_length(mapped);
    const char* data = g_mapped_file_get_contents(mapped);
    GInputStream* stream = g_memory_input_stream_new_from_data(
        g_memdup2(data, data_len), data_len, g_free);
    g_mapped_file_unref(mapped);

    webkit_uri_scheme_request_finish(request, stream, (gint64)data_len,
                                     layer_mime_for_ext(full_path));
    g_object_unref(stream);
}

// ---------------------------------------------------------------------------
// Helper: pump GLib main loop for up to timeout_ms milliseconds or until done
// ---------------------------------------------------------------------------

static void pump_main_loop(volatile bool* done, int timeout_ms) {
    GMainContext* ctx = g_main_context_default();
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (!(*done)) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (long)((now.tv_sec  - start.tv_sec)  * 1000 +
                                 (now.tv_nsec - start.tv_nsec) / 1000000);
        if (elapsed_ms >= timeout_ms) break;
        g_main_context_iteration(ctx, FALSE);
    }
}

// ---------------------------------------------------------------------------
// Snapshot callback state (used to bridge async webkit snapshot to sync call)
// ---------------------------------------------------------------------------

struct SnapshotState {
    ImageSurface* surface;
    bool done;
    bool success;
    WebViewHandle* handle;
};

static void on_snapshot_ready(GObject* object, GAsyncResult* result, gpointer user_data) {
    SnapshotState* state = (SnapshotState*)user_data;
    GError* error = nullptr;

    cairo_surface_t* cairo_surf = webkit_web_view_get_snapshot_finish(
        WEBKIT_WEB_VIEW(object), result, &error);

    if (error || !cairo_surf) {
        log_error("webview_layer_linux: snapshot failed: %s",
                  error ? error->message : "null surface");
        if (error) g_error_free(error);
        state->done    = true;
        state->success = false;
        return;
    }

    // cairo ARGB32 = premultiplied ARGB, 4 bytes/pixel, native byte order
    // We need RGBA (non-premultiplied, big-endian RGBA as in ImageSurface)
    int cw = cairo_image_surface_get_width(cairo_surf);
    int ch = cairo_image_surface_get_height(cairo_surf);

    if (cw <= 0 || ch <= 0) {
        log_error("webview_layer_linux: snapshot has zero dimensions");
        cairo_surface_destroy(cairo_surf);
        state->done    = true;
        state->success = false;
        return;
    }

    ImageSurface* surf = state->surface;
    int pitch = cw * 4;

    if (surf->width != cw || surf->height != ch || !surf->pixels) {
        if (surf->pixels) mem_free(surf->pixels);
        surf->pixels = mem_calloc((size_t)(pitch * ch), 1, MEM_CAT_LAYOUT);
        surf->width  = cw;
        surf->height = ch;
        surf->pitch  = pitch;
        surf->format = IMAGE_FORMAT_PNG;  // RGBA raster
    }

    // flush cairo surface to ensure pixel data is available
    cairo_surface_flush(cairo_surf);
    unsigned char* src = cairo_image_surface_get_data(cairo_surf);
    int src_stride = cairo_image_surface_get_stride(cairo_surf);
    unsigned char* dst = (unsigned char*)surf->pixels;

    // convert ARGB32 premultiplied (cairo, little-endian on x86: B G R A) → RGBA
    for (int row = 0; row < ch; row++) {
        const unsigned char* src_row = src + row * src_stride;
        unsigned char*       dst_row = dst + row * pitch;
        for (int col = 0; col < cw; col++) {
            // cairo ARGB32 pixel in memory (little-endian): [B][G][R][A]
            unsigned char b = src_row[col * 4 + 0];
            unsigned char g = src_row[col * 4 + 1];
            unsigned char r = src_row[col * 4 + 2];
            unsigned char a = src_row[col * 4 + 3];

            // un-premultiply alpha
            if (a != 0 && a != 255) {
                r = (unsigned char)((r * 255 + a / 2) / a);
                g = (unsigned char)((g * 255 + a / 2) / a);
                b = (unsigned char)((b * 255 + a / 2) / a);
            }

            // write RGBA
            dst_row[col * 4 + 0] = r;
            dst_row[col * 4 + 1] = g;
            dst_row[col * 4 + 2] = b;
            dst_row[col * 4 + 3] = a;
        }
    }

    cairo_surface_destroy(cairo_surf);

    log_debug("webview_layer_linux: snapshot captured %dx%d", cw, ch);
    state->done    = true;
    state->success = true;
}

// ---------------------------------------------------------------------------
// Platform API — layer mode
// ---------------------------------------------------------------------------

WebViewHandle* webview_layer_platform_create(float w, float h, float pixel_ratio) {
    ensure_gtk_layer_initialized();
    if (!s_gtk_layer_initialized) return nullptr;

    WebViewHandle* handle = (WebViewHandle*)mem_calloc(1, sizeof(WebViewHandle), MEM_CAT_LAYOUT);
    handle->mode        = WEBVIEW_MODE_LAYER;
    handle->width       = w;
    handle->height      = h;
    handle->pixel_ratio = pixel_ratio;
    handle->dirty       = true;
    handle->loaded      = false;

    // lambda:// scheme (register once per default context)
    static bool scheme_registered = false;
    if (!scheme_registered) {
        WebKitWebContext* context = webkit_web_context_get_default();
        webkit_web_context_register_uri_scheme(context, "lambda",
                                               on_layer_lambda_scheme, nullptr, nullptr);
        scheme_registered = true;
    }

    WebKitUserContentManager* ucm = webkit_user_content_manager_new();

    // inject bridge + MutationObserver at document end
    WebKitUserScript* script = webkit_user_script_new(
        k_layer_bridge_js,
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
        nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, script);
    webkit_user_script_unref(script);

    // register IPC handler that also marks dirty
    g_signal_connect(ucm, "script-message-received::lambda",
                     G_CALLBACK(on_layer_script_message), handle);
    webkit_user_content_manager_register_script_message_handler(ucm, "lambda");

    WebKitSettings* settings = webkit_settings_new();
    webkit_settings_set_enable_javascript(settings, TRUE);
    // software rendering so snapshot works reliably offscreen
    webkit_settings_set_hardware_acceleration_policy(settings,
        WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);

    handle->wk_view = WEBKIT_WEB_VIEW(
        g_object_new(WEBKIT_TYPE_WEB_VIEW,
                     "user-content-manager", ucm,
                     "settings", settings,
                     nullptr));
    g_object_unref(ucm);
    g_object_unref(settings);

    g_signal_connect(handle->wk_view, "load-changed",
                     G_CALLBACK(on_layer_load_changed), handle);
    g_signal_connect(handle->wk_view, "load-failed",
                     G_CALLBACK(on_layer_load_failed),  handle);

    // GtkOffscreenWindow renders off-screen with a real cairo surface
    handle->offscreen_win = gtk_offscreen_window_new();
    gtk_widget_set_size_request(handle->offscreen_win,
                                (int)(w * pixel_ratio),   // INT_CAST_OK: physical pixel width
                                (int)(h * pixel_ratio));  // INT_CAST_OK: physical pixel height
    gtk_container_add(GTK_CONTAINER(handle->offscreen_win), GTK_WIDGET(handle->wk_view));
    gtk_widget_show_all(handle->offscreen_win);

    // pump once so GTK realizes the window
    while (gtk_events_pending()) gtk_main_iteration_do(FALSE);

    log_info("webview_layer_platform_create (linux): offscreen %.0fx%.0f (pr=%.1f)",
             w, h, pixel_ratio);
    return handle;
}

void webview_layer_platform_destroy(WebViewHandle* handle) {
    if (!handle) return;
    if (handle->offscreen_win) {
        gtk_widget_destroy(handle->offscreen_win);
        handle->offscreen_win = nullptr;
    }
    handle->wk_view = nullptr;
    mem_free(handle);
    log_info("webview_layer_platform_destroy (linux): handle released");
}

void webview_layer_platform_navigate(WebViewHandle* handle, const char* url) {
    if (!handle || !handle->wk_view || !url) return;
    handle->loaded = false;
    handle->dirty  = true;
    webkit_web_view_load_uri(handle->wk_view, url);
    log_info("webview_layer_linux: navigating to %s", url);
}

void webview_layer_platform_set_html(WebViewHandle* handle, const char* html) {
    if (!handle || !handle->wk_view || !html) return;
    handle->loaded = false;
    handle->dirty  = true;
    webkit_web_view_load_html(handle->wk_view, html, nullptr);
    log_debug("webview_layer_linux: loaded inline HTML (%zu bytes)", strlen(html));
}

void webview_layer_platform_eval_js(WebViewHandle* handle, const char* js) {
    if (!handle || !handle->wk_view || !js) return;
    webkit_web_view_run_javascript(handle->wk_view, js, nullptr, nullptr, nullptr);
    handle->dirty = true;
}

void webview_layer_platform_resize(WebViewHandle* handle, float w, float h, float pixel_ratio) {
    if (!handle || !handle->offscreen_win) return;
    handle->width       = w;
    handle->height      = h;
    handle->pixel_ratio = pixel_ratio;
    gtk_widget_set_size_request(handle->offscreen_win,
                                (int)(w * pixel_ratio),   // INT_CAST_OK: physical pixel width
                                (int)(h * pixel_ratio));  // INT_CAST_OK: physical pixel height
    handle->dirty = true;
    while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
    log_debug("webview_layer_linux: resized to %.0fx%.0f (pr=%.1f)", w, h, pixel_ratio);
}

bool webview_layer_platform_snapshot(WebViewHandle* handle, ImageSurface* surface) {
    if (!handle || !handle->wk_view || !surface) return false;
    if (handle->snapshot_in_progress) return false;

    handle->snapshot_in_progress = true;

    SnapshotState state;
    state.surface = surface;
    state.done    = false;
    state.success = false;
    state.handle  = handle;

    webkit_web_view_get_snapshot(
        handle->wk_view,
        WEBKIT_SNAPSHOT_REGION_FULL_DOCUMENT,
        WEBKIT_SNAPSHOT_OPTIONS_NONE,
        nullptr,              // GCancellable
        on_snapshot_ready,
        &state);

    // pump event loop until snapshot completes (max 500ms)
    pump_main_loop(&state.done, 500);

    handle->snapshot_in_progress = false;
    if (state.success) {
        handle->dirty = false;
    }
    return state.success;
}

bool webview_layer_platform_is_dirty(WebViewHandle* handle) {
    if (!handle) return false;
    return handle->dirty;
}

void webview_layer_platform_mark_dirty(WebViewHandle* handle, bool dirty) {
    if (!handle) return;
    handle->dirty = dirty;
}

// ---------------------------------------------------------------------------
// Event injection — JavaScript dispatchEvent (same approach as macOS layer)
// ---------------------------------------------------------------------------

void webview_layer_platform_inject_mouse(WebViewHandle* handle,
                                          int type, float x, float y,
                                          int button, int mods) {
    if (!handle || !handle->wk_view) return;

    const char* event_name;
    switch (type) {
        case 0: event_name = "mousedown"; break;
        case 1: event_name = "mouseup";   break;
        case 2: event_name = "mousemove"; break;
        case 3: event_name = "click";     break;
        default: event_name = "mousemove"; break;
    }

    char js[1024];
    if (type == 3) {
        snprintf(js, sizeof(js),
            "(function(){"
            "var el=document.elementFromPoint(%f,%f);"
            "if(el){el.click();}"
            "})()", x, y);
    } else {
        snprintf(js, sizeof(js),
            "(function(){"
            "var el=document.elementFromPoint(%f,%f);"
            "if(el)el.dispatchEvent(new MouseEvent('%s',{"
            "clientX:%f,clientY:%f,button:%d,bubbles:true,cancelable:true}));"
            "})()", x, y, event_name, x, y, button);
    }

    webkit_web_view_run_javascript(handle->wk_view, js, nullptr, nullptr, nullptr);
    handle->dirty = true;
}

void webview_layer_platform_inject_key(WebViewHandle* handle,
                                        int type, int keycode, int mods) {
    if (!handle || !handle->wk_view) return;
    const char* event_name = (type == 0) ? "keydown" : "keyup";
    char js[512];
    snprintf(js, sizeof(js),
        "(function(){"
        "var el=document.activeElement||document.body;"
        "el.dispatchEvent(new KeyboardEvent('%s',{"
        "keyCode:%d,bubbles:true,cancelable:true}));"
        "})()", event_name, keycode);
    webkit_web_view_run_javascript(handle->wk_view, js, nullptr, nullptr, nullptr);
    handle->dirty = true;
}

void webview_layer_platform_inject_text(WebViewHandle* handle, uint32_t codepoint) {
    if (!handle || !handle->wk_view) return;
    char utf8[8] = {};
    int len = 0;
    if (codepoint < 0x80) {
        utf8[0] = (char)codepoint; // INT_CAST_OK: ASCII range
        len = 1;
    } else if (codepoint < 0x800) {
        utf8[0] = (char)(0xC0 | (codepoint >> 6));
        utf8[1] = (char)(0x80 | (codepoint & 0x3F));
        len = 2;
    } else if (codepoint < 0x10000) {
        utf8[0] = (char)(0xE0 | (codepoint >> 12));
        utf8[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (codepoint & 0x3F));
        len = 3;
    } else {
        utf8[0] = (char)(0xF0 | (codepoint >> 18));
        utf8[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (codepoint & 0x3F));
        len = 4;
    }
    utf8[len] = '\0';

    char js[256];
    snprintf(js, sizeof(js),
        "(function(){"
        "var el=document.activeElement||document.body;"
        "el.dispatchEvent(new InputEvent('input',{"
        "data:'%s',inputType:'insertText',bubbles:true}));"
        "})()", utf8);
    webkit_web_view_run_javascript(handle->wk_view, js, nullptr, nullptr, nullptr);
    handle->dirty = true;
}

void webview_layer_platform_inject_scroll(WebViewHandle* handle,
                                           float dx, float dy,
                                           float x, float y) {
    if (!handle || !handle->wk_view) return;
    char js[256];
    snprintf(js, sizeof(js),
        "(function(){"
        "var el=document.elementFromPoint(%f,%f);"
        "if(el)el.dispatchEvent(new WheelEvent('wheel',{"
        "deltaX:%f,deltaY:%f,clientX:%f,clientY:%f,bubbles:true}));"
        "})()", x, y, dx, dy, x, y);
    webkit_web_view_run_javascript(handle->wk_view, js, nullptr, nullptr, nullptr);
    handle->dirty = true;
}

#endif // __linux__
