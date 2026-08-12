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
