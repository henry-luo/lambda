// webview_child_linux.cpp — Linux WebKitGTK child window backend
//
// Creates a GTK popup window containing a WebKitWebView and positions it
// at the layout-computed absolute screen coordinates (GLFW window position +
// layout offset). The GTK event loop is pumped via g_main_context_iteration()
// during post-layout sync so web view events are processed without blocking.
//
// Requires: webkit2gtk-4.1 (or webkit2gtk-4.0 as fallback)

#if defined(__linux__)

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <webkit2/webkit2.h>
#include <gtk/gtk.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "webview.h"
#include "webview_handle_linux.h"

extern "C" {
#include "../lib/log.h"
#include "../lib/mem.h"
}

// ---------------------------------------------------------------------------
// GTK initialization — called once before any web view is created
// ---------------------------------------------------------------------------

static bool s_gtk_initialized = false;

static void ensure_gtk_initialized() {
    if (s_gtk_initialized) return;
    int argc = 0;
    if (!gtk_init_check(&argc, nullptr)) {
        log_error("webview_child_linux: gtk_init_check failed — webviews unavailable");
        return;
    }
    s_gtk_initialized = true;
    log_info("webview_child_linux: GTK initialized");
}

// ---------------------------------------------------------------------------
// IPC bridge JavaScript injected at document start
// ---------------------------------------------------------------------------

static const char* k_bridge_js =
    "window.__lambda = {"
    "  invoke: function(command, payload) {"
    "    var msg = JSON.stringify({ command: command, payload: payload });"
    "    window.webkit.messageHandlers.lambda.postMessage(msg);"
    "  },"
    "  _callbacks: {},"
    "  _nextId: 1,"
    "  on: function(event, callback) {"
    "    this._callbacks[event] = callback;"
    "  }"
    "};";

// ---------------------------------------------------------------------------
// Navigation callbacks
// ---------------------------------------------------------------------------

static void on_load_changed(WebKitWebView* view, WebKitLoadEvent event, gpointer user_data) {
    (void)view;
    WebViewHandle* handle = (WebViewHandle*)user_data;
    if (event == WEBKIT_LOAD_FINISHED) {
        handle->loaded = true;
        log_info("webview_child_linux: navigation finished");
    }
}

static void on_load_failed(WebKitWebView* view, WebKitLoadEvent event,
                           const char* uri, GError* error, gpointer user_data) {
    (void)view; (void)event; (void)user_data;
    log_error("webview_child_linux: navigation failed for %s: %s", uri, error->message);
}

// ---------------------------------------------------------------------------
// IPC message handler — receives window.__lambda.invoke() calls from JS
// ---------------------------------------------------------------------------

static void on_script_message(WebKitUserContentManager* manager,
                               WebKitJavascriptResult* result,
                               gpointer user_data) {
    (void)manager; (void)user_data;
#if WEBKIT_CHECK_VERSION(2, 22, 0)
    JSCValue* value = webkit_javascript_result_get_js_value(result);
    if (jsc_value_is_string(value)) {
        char* msg = jsc_value_to_string(value);
        log_info("webview IPC (linux): received message: %s", msg);
        // TODO: parse JSON { command, payload } and dispatch to Lambda runtime
        g_free(msg);
    }
#else
    // webkit2gtk < 2.22 fallback
    JSGlobalContextRef ctx = webkit_javascript_result_get_global_context(result);
    JSValueRef jsval = webkit_javascript_result_get_value(result);
    JSStringRef jsstr = JSValueToStringCopy(ctx, jsval, NULL);
    size_t len = JSStringGetMaximumUTF8CStringSize(jsstr);
    char* msg = (char*)g_malloc(len);
    JSStringGetUTF8CString(jsstr, msg, len);
    log_info("webview IPC (linux): received message: %s", msg);
    g_free(msg);
    JSStringRelease(jsstr);
#endif
}

// ---------------------------------------------------------------------------
// lambda:// custom URI scheme handler — serves local files from CWD
// ---------------------------------------------------------------------------

static const char* mime_for_ext(const char* path) {
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
    else if (strcasecmp(dot, "webp") == 0) return "image/webp";
    else if (strcasecmp(dot, "woff") == 0) return "font/woff";
    else if (strcasecmp(dot, "woff2")== 0) return "font/woff2";
    else if (strcasecmp(dot, "ttf")  == 0) return "font/ttf";
    return "application/octet-stream";
}

static void on_lambda_scheme_request(WebKitURISchemeRequest* request, gpointer user_data) {
    (void)user_data;
    const char* uri = webkit_uri_scheme_request_get_uri(request);
    const char* path = webkit_uri_scheme_request_get_path(request);
    if (!path) path = "/";

    // strip leading /
    const char* rel = (path[0] == '/') ? path + 1 : path;

    char cwd[4096];
    if (!getcwd(cwd, sizeof(cwd))) {
        GError* err = g_error_new(G_FILE_ERROR, G_FILE_ERROR_NOENT,
                                  "failed to get CWD");
        webkit_uri_scheme_request_finish_error(request, err);
        g_error_free(err);
        return;
    }

    char full_path[8192];
    snprintf(full_path, sizeof(full_path), "%s/%s", cwd, rel);

    // security: resolve and ensure within CWD
    char* resolved = realpath(full_path, nullptr);
    if (!resolved) {
        log_error("webview lambda:// scheme: file not found: %s", full_path);
        GError* err = g_error_new(G_FILE_ERROR, G_FILE_ERROR_NOENT,
                                  "file not found: %s", full_path);
        webkit_uri_scheme_request_finish_error(request, err);
        g_error_free(err);
        return;
    }

    char* cwd_resolved = realpath(cwd, nullptr);
    if (!cwd_resolved || strncmp(resolved, cwd_resolved, strlen(cwd_resolved)) != 0) {
        log_error("webview lambda:// scheme: path traversal blocked: %s", resolved);
        if (cwd_resolved) g_free(cwd_resolved);
        g_free(resolved);
        GError* err = g_error_new(G_FILE_ERROR, G_FILE_ERROR_ACCES,
                                  "path traversal blocked");
        webkit_uri_scheme_request_finish_error(request, err);
        g_error_free(err);
        return;
    }
    g_free(cwd_resolved);

    // read file
    GError* file_err = nullptr;
    GMappedFile* mapped = g_mapped_file_new(resolved, FALSE, &file_err);
    g_free(resolved);

    if (!mapped) {
        log_error("webview lambda:// scheme: cannot read file: %s", file_err->message);
        webkit_uri_scheme_request_finish_error(request, file_err);
        g_error_free(file_err);
        return;
    }

    gsize data_len = g_mapped_file_get_length(mapped);
    const char* data = g_mapped_file_get_contents(mapped);
    GInputStream* stream = g_memory_input_stream_new_from_data(
        g_memdup2(data, data_len), data_len, g_free);
    g_mapped_file_unref(mapped);

    const char* mime = mime_for_ext(full_path);
    webkit_uri_scheme_request_finish(request, stream, (gint64)data_len, mime);
    g_object_unref(stream);

    log_debug("webview lambda:// scheme: served %s (%zu bytes, %s)", full_path, data_len, mime);
}

// ---------------------------------------------------------------------------
// Helper: create a configured WebKitWebContext with lambda:// scheme + IPC
// ---------------------------------------------------------------------------

static WebKitWebView* create_webkit_view(WebViewHandle* handle,
                                          float w, float h) {
    WebKitWebContext* context = webkit_web_context_get_default();

    // register lambda:// URI scheme (only once per process)
    static bool scheme_registered = false;
    if (!scheme_registered) {
        webkit_web_context_register_uri_scheme(context, "lambda",
                                               on_lambda_scheme_request, nullptr, nullptr);
        scheme_registered = true;
    }

    WebKitUserContentManager* ucm = webkit_user_content_manager_new();

    // inject IPC bridge at document start
    WebKitUserScript* script = webkit_user_script_new(
        k_bridge_js,
        WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
        nullptr, nullptr);
    webkit_user_content_manager_add_script(ucm, script);
    webkit_user_script_unref(script);

    // register IPC message handler
    g_signal_connect(ucm, "script-message-received::lambda",
                     G_CALLBACK(on_script_message), handle);
    webkit_user_content_manager_register_script_message_handler(ucm, "lambda");

    WebKitSettings* settings = webkit_settings_new();
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_javascript_can_access_clipboard(settings, TRUE);

    WebKitWebView* view = WEBKIT_WEB_VIEW(
        g_object_new(WEBKIT_TYPE_WEB_VIEW,
                     "user-content-manager", ucm,
                     "settings", settings,
                     nullptr));

    g_object_unref(ucm);
    g_object_unref(settings);

    // load-changed covers both WEBKIT_LOAD_FINISHED and errors
    g_signal_connect(view, "load-changed", G_CALLBACK(on_load_changed), handle);
    g_signal_connect(view, "load-failed",  G_CALLBACK(on_load_failed),  handle);

    return view;
}

// ---------------------------------------------------------------------------
// Platform API — child window mode
// ---------------------------------------------------------------------------

WebViewHandle* webview_platform_create(GLFWwindow* window,
                                       float x, float y, float w, float h,
                                       float pixel_ratio) {
    ensure_gtk_initialized();
    if (!s_gtk_initialized) return nullptr;

    WebViewHandle* handle = (WebViewHandle*)mem_calloc(1, sizeof(WebViewHandle), MEM_CAT_LAYOUT);
    handle->mode        = WEBVIEW_MODE_WINDOW;
    handle->glfw_window = window;
    handle->width       = w;
    handle->height      = h;
    handle->pixel_ratio = pixel_ratio;

    handle->wk_view = create_webkit_view(handle, w, h);

    // create a borderless popup GTK window to host the web view
    handle->gtk_window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_decorated(GTK_WINDOW(handle->gtk_window), FALSE);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(handle->gtk_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(handle->gtk_window), TRUE);
    gtk_window_set_accept_focus(GTK_WINDOW(handle->gtk_window), TRUE);

    gtk_container_add(GTK_CONTAINER(handle->gtk_window), GTK_WIDGET(handle->wk_view));
    gtk_widget_show_all(handle->gtk_window);

    // position at absolute screen coordinates
    int win_x = 0, win_y = 0;
    glfwGetWindowPos(window, &win_x, &win_y);
    int abs_x = (int)(win_x + x);          // INT_CAST_OK: screen pixel coordinates
    int abs_y = (int)(win_y + y);          // INT_CAST_OK: screen pixel coordinates
    int ww    = (int)(w);                  // INT_CAST_OK: CSS pixel width → GTK logical px
    int wh    = (int)(h);                  // INT_CAST_OK: CSS pixel height → GTK logical px
    gtk_window_move(GTK_WINDOW(handle->gtk_window), abs_x, abs_y);
    gtk_window_resize(GTK_WINDOW(handle->gtk_window), ww, wh);

    handle->last_x = x;
    handle->last_y = y;

    // pump GTK events so the window appears
    while (gtk_events_pending()) gtk_main_iteration_do(FALSE);

    log_info("webview_platform_create (linux): GTK popup at (%d,%d) %dx%d", abs_x, abs_y, ww, wh);
    return handle;
}

void webview_platform_destroy(WebViewHandle* handle) {
    if (!handle) return;
    if (handle->gtk_window) {
        gtk_widget_destroy(handle->gtk_window);
        handle->gtk_window = nullptr;
    }
    // wk_view is destroyed as child of gtk_window
    handle->wk_view = nullptr;
    mem_free(handle);
    log_info("webview_platform_destroy (linux): handle released");
}

void webview_platform_navigate(WebViewHandle* handle, const char* url) {
    if (!handle || !handle->wk_view || !url) return;
    handle->loaded = false;
    webkit_web_view_load_uri(handle->wk_view, url);
    log_info("webview_platform_navigate (linux): loading %s", url);
}

void webview_platform_set_html(WebViewHandle* handle, const char* html) {
    if (!handle || !handle->wk_view || !html) return;
    handle->loaded = false;
    webkit_web_view_load_html(handle->wk_view, html, nullptr);
    log_debug("webview_platform_set_html (linux): loaded inline HTML (%zu bytes)", strlen(html));
}

void webview_platform_eval_js(WebViewHandle* handle, const char* js) {
    if (!handle || !handle->wk_view || !js) return;
    webkit_web_view_run_javascript(handle->wk_view, js, nullptr, nullptr, nullptr);
}

void webview_platform_set_bounds(WebViewHandle* handle,
                                 float x, float y, float w, float h,
                                 float pixel_ratio) {
    if (!handle || !handle->gtk_window || !handle->glfw_window) return;

    int win_x = 0, win_y = 0;
    glfwGetWindowPos(handle->glfw_window, &win_x, &win_y);
    int abs_x = (int)(win_x + x);      // INT_CAST_OK: screen pixel coordinates
    int abs_y = (int)(win_y + y);      // INT_CAST_OK: screen pixel coordinates
    int ww    = (int)(w);              // INT_CAST_OK: CSS pixel width
    int wh    = (int)(h);              // INT_CAST_OK: CSS pixel height

    gtk_window_move(GTK_WINDOW(handle->gtk_window), abs_x, abs_y);
    gtk_window_resize(GTK_WINDOW(handle->gtk_window), ww, wh);

    handle->last_x = x;
    handle->last_y = y;
    handle->width  = w;
    handle->height = h;
    handle->pixel_ratio = pixel_ratio;

    // pump pending GTK events to apply the move/resize
    while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
}

void webview_platform_set_visible(WebViewHandle* handle, bool visible) {
    if (!handle || !handle->gtk_window) return;
    if (visible) {
        gtk_widget_show(handle->gtk_window);
    } else {
        gtk_widget_hide(handle->gtk_window);
    }
    while (gtk_events_pending()) gtk_main_iteration_do(FALSE);
    log_debug("webview_platform_set_visible (linux): %s", visible ? "true" : "false");
}

#endif // __linux__
