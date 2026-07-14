// F7 (Radiant_Design_Form_Input.md §3.7): macOS IME shim.
//
// Strategy: keep GLFW's existing content view (which itself implements
// NSTextInputClient and feeds glfwSetCharCallback) intact. We replace the
// view's class at runtime (`object_setClass`) with a small subclass that
// overrides the four NSTextInputClient methods we care about and forwards
// composition events to Radiant's shared editing controller. Calls fall back
// to `super` (via objc_msgSendSuper) so plain-text input on non-editing
// elements still flows through GLFW unchanged.
//
// This translation unit deliberately avoids including view.hpp because
// AppKit's MacTypes defines `Rect` and conflicts with view.hpp::Rect. We
// reach UiContext fields through the `radiant_ui_get_*` accessors in
// ui_context.cpp.

#ifdef __APPLE__

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#import <objc/message.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <stdint.h>
#include <string.h>

#include "../lib/log.h"
#define RADIANT_EVENT_CORE_ONLY
#include "event.hpp"
#undef RADIANT_EVENT_CORE_ONLY

// Opaque types provided elsewhere. We never deref any of them in this
// file; everything is passed by pointer.
struct UiContext;
struct DocState;
class DomElement;
class View;

extern "C" GLFWwindow*    radiant_ui_get_glfw_window(struct UiContext*);
extern "C" DocState*  radiant_ui_get_state(struct UiContext*);
extern "C" void           radiant_state_request_repaint(struct DocState*);
extern "C" bool           radiant_dispatch_editing_composition_event(struct UiContext*,
                                                                     EventType,
                                                                     const char*,
                                                                     uint32_t);
extern "C" bool           radiant_editing_focused_caret_rect(struct UiContext*,
                                                             float*,
                                                             float*,
                                                             float*,
                                                             float*);

View* focus_get(DocState* state);
bool  tc_is_text_control(DomElement* elem);

extern "C" void radiant_ime_mac_attach(struct UiContext* uicon);

namespace {

UiContext* g_ime_uicon = nullptr;
bool g_form_composing = false;
bool g_rich_composing = false;

DomElement* ime_focused_text_control() {
    if (!g_ime_uicon) return nullptr;
    DocState* state = radiant_ui_get_state(g_ime_uicon);
    if (!state) return nullptr;
    View* v = focus_get(state);
    if (!v) return nullptr;
    DomElement* e = (DomElement*)v;  // RADIANT_CAST_OK: native IME bridge keeps View/DomElement opaque to avoid AppKit Rect conflicts
    return tc_is_text_control(e) ? e : nullptr;
}

DocState* ime_state() {
    if (!g_ime_uicon) return nullptr;
    return radiant_ui_get_state(g_ime_uicon);
}

bool ime_dispatch_editing(EventType event_type, const char* text, uint32_t caret) {
    if (!g_ime_uicon) return false;
    bool handled = radiant_dispatch_editing_composition_event(g_ime_uicon,
        event_type, text, caret);
    if (handled) {
        radiant_state_request_repaint(ime_state());
        glfwPostEmptyEvent();
    }
    return handled;
}

const char* ns_to_utf8(id obj) {
    NSString* s = nil;
    if ([obj isKindOfClass:[NSAttributedString class]]) {
        s = [(NSAttributedString*)obj string];
    } else if ([obj isKindOfClass:[NSString class]]) {
        s = (NSString*)obj;
    }
    return s ? [s UTF8String] : "";
}

} // namespace

@interface RadiantIMEContentView : NSView
@end

@implementation RadiantIMEContentView

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange {
    DomElement* e = ime_focused_text_control();
    if (!e) {
        const char* utf8 = ns_to_utf8(string);
        if (!g_rich_composing) {
            if (!ime_dispatch_editing(RDT_EVENT_COMPOSITION_START, "", 0)) {
                struct objc_super sup = { self, [self superclass] };
                ((void (*)(struct objc_super*, SEL, id, NSRange, NSRange))objc_msgSendSuper)(
                    &sup, _cmd, string, selectedRange, replacementRange);
                return;
            }
            g_rich_composing = true;
        }
        if (!ime_dispatch_editing(RDT_EVENT_COMPOSITION_UPDATE, utf8, (uint32_t)selectedRange.location)) {
            g_rich_composing = false;
            struct objc_super sup = { self, [self superclass] };
            ((void (*)(struct objc_super*, SEL, id, NSRange, NSRange))objc_msgSendSuper)(
                &sup, _cmd, string, selectedRange, replacementRange);
            return;
        }
        log_debug("[IME mac] rich setMarkedText '%s' caret=%lu",
                  utf8, (unsigned long)selectedRange.location);
        return;
    }
    const char* utf8 = ns_to_utf8(string);
    if (!g_form_composing) {
        if (!ime_dispatch_editing(RDT_EVENT_COMPOSITION_START, "", 0)) {
            struct objc_super sup = { self, [self superclass] };
            ((void (*)(struct objc_super*, SEL, id, NSRange, NSRange))objc_msgSendSuper)(
                &sup, _cmd, string, selectedRange, replacementRange);
            return;
        }
        g_form_composing = true;
    }
    if (!ime_dispatch_editing(RDT_EVENT_COMPOSITION_UPDATE, utf8,
                              (uint32_t)selectedRange.location)) {
        g_form_composing = false;
        struct objc_super sup = { self, [self superclass] };
        ((void (*)(struct objc_super*, SEL, id, NSRange, NSRange))objc_msgSendSuper)(
            &sup, _cmd, string, selectedRange, replacementRange);
        return;
    }
    log_debug("[IME mac] setMarkedText '%s' caret=%lu",
              utf8, (unsigned long)selectedRange.location);
}

- (void)unmarkText {
    if (g_form_composing && ime_dispatch_editing(RDT_EVENT_COMPOSITION_END, "", 0)) {
        g_form_composing = false;
        log_debug("[IME mac] unmarkText -> cancel");
        return;
    }
    if (g_rich_composing && ime_dispatch_editing(RDT_EVENT_COMPOSITION_END, "", 0)) {
        g_rich_composing = false;
        log_debug("[IME mac] rich unmarkText -> cancel");
        return;
    }
    if (g_form_composing) g_form_composing = false;
    if (g_rich_composing) g_rich_composing = false;
    struct objc_super sup = { self, [self superclass] };
    ((void (*)(struct objc_super*, SEL))objc_msgSendSuper)(&sup, _cmd);
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    DomElement* e = ime_focused_text_control();
    if (!e || !g_form_composing) {
        if (g_rich_composing) {
            const char* utf8 = ns_to_utf8(string);
            if (ime_dispatch_editing(RDT_EVENT_COMPOSITION_END, utf8, 0)) {
                g_rich_composing = false;
                log_debug("[IME mac] rich insertText commit '%s'", utf8);
                return;
            }
            g_rich_composing = false;
        }
        // Plain typing — fall through to GLFW so its char callback path
        // (which already inserts text and dispatches keypress events) runs.
        struct objc_super sup = { self, [self superclass] };
        ((void (*)(struct objc_super*, SEL, id, NSRange))objc_msgSendSuper)(
            &sup, _cmd, string, replacementRange);
        return;
    }
    const char* utf8 = ns_to_utf8(string);
    if (!ime_dispatch_editing(RDT_EVENT_COMPOSITION_END, utf8, 0)) {
        g_form_composing = false;
        struct objc_super sup = { self, [self superclass] };
        ((void (*)(struct objc_super*, SEL, id, NSRange))objc_msgSendSuper)(
            &sup, _cmd, string, replacementRange);
        return;
    }
    g_form_composing = false;
    log_debug("[IME mac] insertText commit '%s'", utf8);
}

- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actualRange {
    if (actualRange) *actualRange = range;
    float x = 0.0f;
    float y = 0.0f;
    float w_rect = 0.0f;
    float h_rect = 0.0f;
    NSWindow* w = [self window];
    NSRect frame = w ? [w frame] : NSMakeRect(0, 0, 0, 0);
    if (!radiant_editing_focused_caret_rect(g_ime_uicon,
            &x, &y, &w_rect, &h_rect)) {
        return NSMakeRect(frame.origin.x, frame.origin.y, 0, 0);
    }
    CGFloat scale = w ? [w backingScaleFactor] : 1.0;
    CGFloat screen_x = frame.origin.x + ((CGFloat)x / scale);
    CGFloat screen_y = frame.origin.y + frame.size.height -
        (((CGFloat)y + (CGFloat)h_rect) / scale);
    return NSMakeRect(screen_x, screen_y,
                      (CGFloat)w_rect / scale,
                      (CGFloat)h_rect / scale);
}

@end

extern "C" void radiant_ime_mac_attach(struct UiContext* uicon) {
    if (!uicon) return;
    g_ime_uicon = uicon;
    GLFWwindow* glfw_win = radiant_ui_get_glfw_window(uicon);
    if (!glfw_win) return;

    NSWindow* ns_window = glfwGetCocoaWindow(glfw_win);
    if (!ns_window) {
        log_error("[IME mac] glfwGetCocoaWindow returned nil");
        return;
    }
    NSView* content = [ns_window contentView];
    if (!content) {
        log_error("[IME mac] content view is nil");
        return;
    }

    // Build (once) a synthetic subclass of the GLFW content view's class
    // that carries our NSTextInputClient overrides, then re-isa-swap the
    // live instance into it.
    Class glfw_cls = object_getClass(content);
    static Class radiant_cls = nullptr;
    if (!radiant_cls) {
        const char* name = "RadiantIMEPatchedContentView";
        radiant_cls = objc_getClass(name);
        if (!radiant_cls) {
            radiant_cls = objc_allocateClassPair(glfw_cls, name, 0);
            Class src = [RadiantIMEContentView class];
            SEL sels[] = {
                @selector(setMarkedText:selectedRange:replacementRange:),
                @selector(unmarkText),
                @selector(insertText:replacementRange:),
                @selector(firstRectForCharacterRange:actualRange:),
            };
            for (size_t i = 0; i < sizeof(sels)/sizeof(sels[0]); i++) {
                Method m = class_getInstanceMethod(src, sels[i]);
                if (m) {
                    class_addMethod(radiant_cls, sels[i],
                                    method_getImplementation(m),
                                    method_getTypeEncoding(m));
                }
            }
            objc_registerClassPair(radiant_cls);
        }
    }
    object_setClass(content, radiant_cls);
    log_info("[IME mac] attached IME shim to GLFW content view");
}

#else // !__APPLE__

extern "C" void radiant_ime_mac_attach(void* uicon) { (void)uicon; }

#endif // __APPLE__
