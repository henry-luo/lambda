// F7 (Radiant_Design_Form_Input.md §3.7): macOS IME shim.
//
// Strategy: keep GLFW's existing content view (which itself implements
// NSTextInputClient and feeds glfwSetCharCallback) intact. We replace the
// view's class at runtime (`object_setClass`) with a small subclass that
// overrides the four NSTextInputClient methods we care about and forwards
// to `te_ime_*` whenever a text control currently has Radiant focus.
// Calls fall back to `super` (via objc_msgSendSuper) so plain-text input
// on non-text-control elements still flows through GLFW unchanged.
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

// Opaque types provided elsewhere. We never deref any of them in this
// file; everything is passed by pointer.
struct UiContext;
struct RadiantState;
class DomElement;
class View;

extern "C" GLFWwindow*    radiant_ui_get_glfw_window(struct UiContext*);
extern "C" RadiantState*  radiant_ui_get_state(struct UiContext*);
extern "C" void           radiant_state_request_repaint(struct RadiantState*);

View* focus_get(RadiantState* state);
bool  tc_is_text_control(DomElement* elem);

void te_ime_begin(DomElement* elem);
void te_ime_update(DomElement* elem, const char* preedit, uint32_t len, uint32_t caret);
void te_ime_commit(DomElement* elem, RadiantState* state, void* target,
                   const char* committed, uint32_t len);
void te_ime_cancel(DomElement* elem);
bool te_ime_is_composing(DomElement* elem);

extern "C" void radiant_ime_mac_attach(struct UiContext* uicon);

namespace {

UiContext* g_ime_uicon = nullptr;

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

@interface RadiantIMEContentView : NSView <NSTextInputClient>
@end

@implementation RadiantIMEContentView

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange {
    DomElement* e = ime_focused_text_control();
    if (!e) {
        struct objc_super sup = { self, [self superclass] };
        ((void (*)(struct objc_super*, SEL, id, NSRange, NSRange))objc_msgSendSuper)(
            &sup, _cmd, string, selectedRange, replacementRange);
        return;
    }
    const char* utf8 = ns_to_utf8(string);
    uint32_t len = (uint32_t)strlen(utf8);
    if (!te_ime_is_composing(e)) {
        te_ime_begin(e);
    }
    te_ime_update(e, utf8, len, (uint32_t)selectedRange.location);
    log_debug("[IME mac] setMarkedText '%s' caret=%lu",
              utf8, (unsigned long)selectedRange.location);
    // Wake the GLFW main loop so the preedit underline appears immediately
    // (the IME callbacks bypass GLFW's regular event callbacks).
    radiant_state_request_repaint(ime_state());
    glfwPostEmptyEvent();
}

- (void)unmarkText {
    DomElement* e = ime_focused_text_control();
    if (e && te_ime_is_composing(e)) {
        te_ime_cancel(e);
        log_debug("[IME mac] unmarkText -> cancel");
        return;
    }
    struct objc_super sup = { self, [self superclass] };
    ((void (*)(struct objc_super*, SEL))objc_msgSendSuper)(&sup, _cmd);
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    DomElement* e = ime_focused_text_control();
    RadiantState* state = ime_state();
    if (!e || !state || !te_ime_is_composing(e)) {
        // Plain typing — fall through to GLFW so its char callback path
        // (which already inserts text and dispatches keypress events) runs.
        struct objc_super sup = { self, [self superclass] };
        ((void (*)(struct objc_super*, SEL, id, NSRange))objc_msgSendSuper)(
            &sup, _cmd, string, replacementRange);
        return;
    }
    const char* utf8 = ns_to_utf8(string);
    uint32_t len = (uint32_t)strlen(utf8);
    te_ime_commit(e, state, (void*)e, utf8, len);
    log_debug("[IME mac] insertText commit '%s'", utf8);
    // Wake the GLFW main loop so the committed text appears without
    // waiting for the next mouse-move / animation tick.
    radiant_state_request_repaint(state);
    glfwPostEmptyEvent();
}

- (NSRect)firstRectForCharacterRange:(NSRange)range
                         actualRange:(NSRangePointer)actualRange {
    // TODO: derive an accurate caret rect via te_x_at_offset + the text
    // control's screen position.
    if (actualRange) *actualRange = range;
    NSWindow* w = [self window];
    NSRect frame = w ? [w frame] : NSMakeRect(0, 0, 0, 0);
    return NSMakeRect(frame.origin.x, frame.origin.y, 0, 0);
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
