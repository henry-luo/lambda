// Stage D: Hidden-state RAII guards.
//
// The JS runtime has two file-scope/extern globals that are mutated
// transiently inside specific call paths:
//
//   - js_skip_accessor_dispatch (bool): set true while writing the
//     literal accessor-pair value into the slot, so the recursive
//     js_property_set does NOT dispatch the setter via accessor logic.
//
//   - js_proxy_receiver (Item, file-static in js_runtime.cpp): captures
//     the original Receiver across nested proxy [[Get]]/[[Set]] forwards
//     so getter `this` binds to the proxy (not the target).
//
// Both were managed with manual `prev = X; X = new; ...; X = prev`
// blocks, leaving the door open for state leaks if an early return
// (exception sentinel) skipped the restore. These RAII wrappers
// guarantee restoration on scope exit, including all early returns.
//
// Design constraint: `js_proxy_receiver` is `static` in js_runtime.cpp
// (file-private). The guard for it lives in that translation unit only;
// see `ScopedProxyReceiver` defined adjacent to the static declaration.

#pragma once

extern "C" {
extern bool js_skip_accessor_dispatch;
}

// Set js_skip_accessor_dispatch=true for the lifetime of the guard,
// restoring the previous value on destruction. Safe across early returns
// and (for builds where exceptions can propagate through C++ frames)
// stack unwinding.
class ScopedSkipAccessorDispatch {
public:
    ScopedSkipAccessorDispatch() : prev_(js_skip_accessor_dispatch) {
        js_skip_accessor_dispatch = true;
    }
    ~ScopedSkipAccessorDispatch() { js_skip_accessor_dispatch = prev_; }
    ScopedSkipAccessorDispatch(const ScopedSkipAccessorDispatch&) = delete;
    ScopedSkipAccessorDispatch& operator=(const ScopedSkipAccessorDispatch&) = delete;
private:
    bool prev_;
};
