// Stage D: private declaration scope guard.

#pragma once

#include "js_runtime_state.hpp"

// Private declaration writes alone may bypass the receiver brand check. Class
// initializer expressions execute outside this scope so nested private access
// still observes whether the derived class has installed its brand.
class ScopedPrivateDefineActive {
public:
    ScopedPrivateDefineActive()
        : prev_(js_runtime_state.operations.private_define_active) {
        js_runtime_state.operations.private_define_active = true;
    }
    ~ScopedPrivateDefineActive() {
        js_runtime_state.operations.private_define_active = prev_;
    }
    ScopedPrivateDefineActive(const ScopedPrivateDefineActive&) = delete;
    ScopedPrivateDefineActive& operator=(const ScopedPrivateDefineActive&) = delete;
private:
    bool prev_;
};

// JSCU44: a `with` scope must not leak into a called function. The callee's
// chain is its own captured env, so entering a call relinks one borrowed frame
// and leaving restores the caller's head -- no copy, no dynamic root range.
// The callee releases its own frames at every completion, so leaving frees none.
struct JsWithActivation {
    JsWithActivation() = default;
    JsWithFrame frame = {};
    JsWithFrame* saved_head = NULL;
    bool entered = false;

    void enter(Item* captured, int depth) {
        saved_head = js_with_activation_enter(captured, depth, &frame);
        entered = true;
    }

    ~JsWithActivation() { if (entered) js_with_activation_leave(saved_head); }

    JsWithActivation(const JsWithActivation&) = delete;
    JsWithActivation& operator=(const JsWithActivation&) = delete;
};
