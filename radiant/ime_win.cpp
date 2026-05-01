// F7 (Radiant_Design_Form_Input.md §3.7): Windows IME shim.
//
// Subclasses the GLFW HWND's WndProc with `SetWindowLongPtrW` and
// intercepts WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION /
// WM_IME_ENDCOMPOSITION, dispatching to te_ime_* whenever the focused
// element is a text control. Other messages (and IME messages received
// while no text control is focused) fall through to GLFW's WndProc.
//
// On non-Windows platforms this file compiles to an empty stub.

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/log.h"

// Opaque types — see ime_mac.mm comment.
struct UiContext;
struct RadiantState;
class DomElement;
class View;

extern "C" GLFWwindow*   radiant_ui_get_glfw_window(struct UiContext*);
extern "C" RadiantState* radiant_ui_get_state(struct UiContext*);

View* focus_get(RadiantState*);
bool  tc_is_text_control(DomElement*);

void te_ime_begin(DomElement*);
void te_ime_update(DomElement*, const char*, uint32_t, uint32_t);
void te_ime_commit(DomElement*, RadiantState*, void*, const char*, uint32_t);
void te_ime_cancel(DomElement*);
bool te_ime_is_composing(DomElement*);

extern "C" void radiant_ime_win_attach(struct UiContext* uicon);

namespace {

UiContext* g_ime_uicon  = nullptr;
WNDPROC    g_orig_wndproc = nullptr;

DomElement* ime_focused_text_control() {
    if (!g_ime_uicon) return nullptr;
    RadiantState* state = radiant_ui_get_state(g_ime_uicon);
    if (!state) return nullptr;
    View* v = focus_get(state);
    if (!v) return nullptr;
    DomElement* e = (DomElement*)v;
    return tc_is_text_control(e) ? e : nullptr;
}

RadiantState* ime_state() {
    if (!g_ime_uicon) return nullptr;
    return radiant_ui_get_state(g_ime_uicon);
}

// Read GCS_COMPSTR or GCS_RESULTSTR as UTF-8. Returns malloc'd buffer
// (caller frees) or NULL. Sets *out_len to byte length.
char* ime_read_string(HIMC himc, DWORD flag, uint32_t* out_len) {
    LONG wide_bytes = ImmGetCompositionStringW(himc, flag, NULL, 0);
    if (wide_bytes <= 0) { *out_len = 0; return nullptr; }
    int wide_chars = (int)(wide_bytes / (LONG)sizeof(WCHAR));
    WCHAR* wbuf = (WCHAR*)malloc((size_t)(wide_chars + 1) * sizeof(WCHAR));
    if (!wbuf) { *out_len = 0; return nullptr; }
    ImmGetCompositionStringW(himc, flag, wbuf, (DWORD)wide_bytes);
    wbuf[wide_chars] = 0;
    int u8_bytes = WideCharToMultiByte(CP_UTF8, 0, wbuf, wide_chars,
                                       NULL, 0, NULL, NULL);
    if (u8_bytes <= 0) { free(wbuf); *out_len = 0; return nullptr; }
    char* u8 = (char*)malloc((size_t)u8_bytes + 1);
    if (!u8) { free(wbuf); *out_len = 0; return nullptr; }
    WideCharToMultiByte(CP_UTF8, 0, wbuf, wide_chars, u8, u8_bytes, NULL, NULL);
    u8[u8_bytes] = 0;
    free(wbuf);
    *out_len = (uint32_t)u8_bytes;
    return u8;
}

uint32_t ime_caret_position(HIMC himc) {
    LONG cur = ImmGetCompositionStringW(himc, GCS_CURSORPOS, NULL, 0);
    if (cur < 0) return 0;
    return (uint32_t)cur;
}

LRESULT CALLBACK ime_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DomElement* e = ime_focused_text_control();
    RadiantState* state = ime_state();

    if (e) {
        switch (msg) {
        case WM_IME_STARTCOMPOSITION:
            te_ime_begin(e);
            log_debug("[IME win] WM_IME_STARTCOMPOSITION");
            return 0;
        case WM_IME_COMPOSITION: {
            HIMC himc = ImmGetContext(hwnd);
            if (!himc) break;
            if (lp & GCS_RESULTSTR) {
                uint32_t len = 0;
                char* u8 = ime_read_string(himc, GCS_RESULTSTR, &len);
                if (u8 && state) {
                    te_ime_commit(e, state, (void*)e, u8, len);
                    log_debug("[IME win] commit '%s'", u8);
                }
                free(u8);
            }
            if (lp & GCS_COMPSTR) {
                uint32_t len = 0;
                char* u8 = ime_read_string(himc, GCS_COMPSTR, &len);
                uint32_t caret = ime_caret_position(himc);
                if (u8) {
                    if (!te_ime_is_composing(e)) te_ime_begin(e);
                    te_ime_update(e, u8, len, caret);
                    log_debug("[IME win] update '%s' caret=%u", u8, caret);
                }
                free(u8);
            }
            ImmReleaseContext(hwnd, himc);
            return 0;
        }
        case WM_IME_ENDCOMPOSITION:
            if (te_ime_is_composing(e)) te_ime_cancel(e);
            log_debug("[IME win] WM_IME_ENDCOMPOSITION");
            return 0;
        }
    }
    return CallWindowProcW(g_orig_wndproc, hwnd, msg, wp, lp);
}

} // namespace

extern "C" void radiant_ime_win_attach(struct UiContext* uicon) {
    if (!uicon) return;
    g_ime_uicon = uicon;
    GLFWwindow* glfw_win = radiant_ui_get_glfw_window(uicon);
    if (!glfw_win) return;

    HWND hwnd = glfwGetWin32Window(glfw_win);
    if (!hwnd) {
        log_error("[IME win] glfwGetWin32Window returned NULL");
        return;
    }
    if (g_orig_wndproc) return; // already attached
    g_orig_wndproc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                                (LONG_PTR)ime_wndproc);
    log_info("[IME win] attached IME shim to GLFW HWND");
}

#else // !_WIN32

extern "C" void radiant_ime_win_attach(void* uicon) { (void)uicon; }

#endif // _WIN32
