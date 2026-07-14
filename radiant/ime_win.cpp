// F7 (Radiant_Design_Form_Input.md §3.7): Windows IME shim.
//
// Subclasses the GLFW HWND's WndProc with `SetWindowLongPtrW` and
// intercepts WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION /
// WM_IME_ENDCOMPOSITION, dispatching to Radiant's shared editing
// composition controller whenever the focused element is a text control.
// Other messages fall through to GLFW's WndProc.
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
#include "../lib/memtrack.h"
#define RADIANT_EVENT_CORE_ONLY
#include "event.hpp"
#undef RADIANT_EVENT_CORE_ONLY

// Opaque types — see ime_mac.mm comment.
struct UiContext;
struct DocState;
class DomElement;
class View;

extern "C" GLFWwindow*   radiant_ui_get_glfw_window(struct UiContext*);
extern "C" DocState* radiant_ui_get_state(struct UiContext*);
extern "C" bool radiant_dispatch_editing_composition_event(struct UiContext*,
                                                           EventType,
                                                           const char*,
                                                           uint32_t);
extern "C" bool radiant_editing_focused_caret_rect(struct UiContext*,
                                                   float*,
                                                   float*,
                                                   float*,
                                                   float*);

View* focus_get(DocState*);
bool  tc_is_text_control(DomElement*);

extern "C" void radiant_ime_win_attach(struct UiContext* uicon);

namespace {

UiContext* g_ime_uicon  = nullptr;
WNDPROC    g_orig_wndproc = nullptr;
bool       g_form_composing = false;

DomElement* ime_focused_text_control() {
    if (!g_ime_uicon) return nullptr;
    DocState* state = radiant_ui_get_state(g_ime_uicon);
    if (!state) return nullptr;
    View* v = focus_get(state);
    if (!v) return nullptr;
    DomElement* e = (DomElement*)v;  // RADIANT_CAST_OK: native IME bridge keeps View/DomElement opaque across Windows headers
    return tc_is_text_control(e) ? e : nullptr;
}

bool ime_dispatch_editing(EventType event_type, const char* text, uint32_t caret) {
    return g_ime_uicon &&
        radiant_dispatch_editing_composition_event(g_ime_uicon, event_type,
                                                   text, caret);
}

void ime_update_candidate_window(HIMC himc) {
    if (!himc || !g_ime_uicon) return;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    if (!radiant_editing_focused_caret_rect(g_ime_uicon, &x, &y, &w, &h)) {
        return;
    }
    CANDIDATEFORM form;
    memset(&form, 0, sizeof(form));
    form.dwIndex = 0;
    form.dwStyle = CFS_CANDIDATEPOS;
    form.ptCurrentPos.x = (LONG)x;
    form.ptCurrentPos.y = (LONG)(y + h);
    ImmSetCandidateWindow(himc, &form);
}

// Read GCS_COMPSTR or GCS_RESULTSTR as UTF-8. Returns mem_alloc'd buffer
// (caller mem_free()s) or NULL. Sets *out_len to byte length.
char* ime_read_string(HIMC himc, DWORD flag, uint32_t* out_len) {
    LONG wide_bytes = ImmGetCompositionStringW(himc, flag, NULL, 0);
    if (wide_bytes <= 0) { *out_len = 0; return nullptr; }
    int wide_chars = (int)(wide_bytes / (LONG)sizeof(WCHAR));
    WCHAR* wbuf = (WCHAR*)mem_alloc((size_t)(wide_chars + 1) * sizeof(WCHAR), MEM_CAT_TEMP);
    if (!wbuf) { *out_len = 0; return nullptr; }
    ImmGetCompositionStringW(himc, flag, wbuf, (DWORD)wide_bytes);
    wbuf[wide_chars] = 0;
    int u8_bytes = WideCharToMultiByte(CP_UTF8, 0, wbuf, wide_chars,
                                       NULL, 0, NULL, NULL);
    if (u8_bytes <= 0) { mem_free(wbuf); *out_len = 0; return nullptr; }
    char* u8 = (char*)mem_alloc((size_t)u8_bytes + 1, MEM_CAT_TEMP);
    if (!u8) { mem_free(wbuf); *out_len = 0; return nullptr; }
    WideCharToMultiByte(CP_UTF8, 0, wbuf, wide_chars, u8, u8_bytes, NULL, NULL);
    u8[u8_bytes] = 0;
    mem_free(wbuf);
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

    if (e) {
        switch (msg) {
        case WM_IME_STARTCOMPOSITION:
            if (!ime_dispatch_editing(RDT_EVENT_COMPOSITION_START, "", 0)) {
                break;
            }
            g_form_composing = true;
            log_debug("[IME win] WM_IME_STARTCOMPOSITION");
            return 0;
        case WM_IME_COMPOSITION: {
            HIMC himc = ImmGetContext(hwnd);
            if (!himc) break;
            ime_update_candidate_window(himc);
            if (lp & GCS_RESULTSTR) {
                uint32_t len = 0;
                char* u8 = ime_read_string(himc, GCS_RESULTSTR, &len);
                if (u8) {
                    if (ime_dispatch_editing(RDT_EVENT_COMPOSITION_END,
                                             u8, 0)) {
                        g_form_composing = false;
                    }
                    log_debug("[IME win] commit '%s'", u8);
                }
                mem_free(u8);
            }
            if (lp & GCS_COMPSTR) {
                uint32_t len = 0;
                char* u8 = ime_read_string(himc, GCS_COMPSTR, &len);
                uint32_t caret = ime_caret_position(himc);
                if (u8) {
                    if (!g_form_composing &&
                        ime_dispatch_editing(RDT_EVENT_COMPOSITION_START,
                                             "", 0)) {
                        g_form_composing = true;
                    }
                    if (g_form_composing &&
                        !ime_dispatch_editing(RDT_EVENT_COMPOSITION_UPDATE,
                                              u8, caret)) {
                        g_form_composing = false;
                    }
                    log_debug("[IME win] update '%s' caret=%u", u8, caret);
                }
                mem_free(u8);
            }
            ImmReleaseContext(hwnd, himc);
            return 0;
        }
        case WM_IME_ENDCOMPOSITION:
            if (g_form_composing &&
                ime_dispatch_editing(RDT_EVENT_COMPOSITION_END, "", 0)) {
                g_form_composing = false;
            }
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

// On Windows, provide a no-op stub for the Mac-only IME attachment.
#ifdef _WIN32
extern "C" void radiant_ime_mac_attach(void* uicon) { (void)uicon; }
#endif
