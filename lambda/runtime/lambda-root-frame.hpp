#pragma once

// Native GC rooting is runtime policy. Keep these C++ helpers beside the
// runtime side-stack API so value-model headers do not own collection state.
#include "lambda-stack.h"

extern "C" Context* eval_context_tls_runtime(void);

#ifdef __cplusplus
extern "C++" {
#endif

class RootFrame {
    LambdaRootFrame frame_;

public:
    explicit RootFrame(size_t slot_count) : frame_{} {
        // Pool/arena-backed input paths have no collecting runtime, so their
        // fallback homes are safe; only a real runtime reservation may fail closed.
        if (eval_context_tls_runtime() &&
                !lambda_root_frame_begin(&frame_, slot_count)) {
            lambda_root_frame_overflow_error();
        }
    }

    ~RootFrame() { lambda_root_frame_end(&frame_); }

    bool valid() const { return frame_.active; }
    uint64_t* slot(size_t index) { return lambda_root_frame_slot(&frame_, index); }
    uint64_t* take_slot() { return lambda_root_frame_take_slot(&frame_); }

    RootFrame(const RootFrame&) = delete;
    RootFrame& operator=(const RootFrame&) = delete;
};

// A contiguous exact-root region for ABI adapters.  Unlike RootFrame's
// take_slot interface, callers use this only when a callee requires an Item*
// span whose lifetime is bounded by one native activation.
class RootSpan {
    LambdaRootFrame frame_;

public:
    explicit RootSpan(size_t slot_count) : frame_{} {
        if (slot_count == 0) return;
        if (eval_context_tls_runtime() &&
                !lambda_root_frame_begin(&frame_, slot_count)) {
            lambda_root_frame_overflow_error();
        }
    }

    ~RootSpan() { lambda_root_frame_end(&frame_); }

    bool valid() const { return frame_.active; }
    size_t size() const { return frame_.slot_count; }
    uint64_t* words() { return frame_.slots; }
    const uint64_t* words() const { return frame_.slots; }

    // The span's cells are Item-shaped, so an ABI adapter can hand the callee
    // `Item*` directly instead of copying out of a native buffer the collector
    // cannot see.
    Item* items() {
        static_assert(sizeof(Item) == sizeof(uint64_t),
            "ABI adapter Item roots must match side-root cells");
        static_assert(alignof(Item) == alignof(uint64_t),
            "ABI adapter Item roots must match side-root alignment");
        return (Item*)(void*)frame_.slots;
    }

    RootSpan(const RootSpan&) = delete;
    RootSpan& operator=(const RootSpan&) = delete;
};

template <typename T>
class Rooted {
    uint64_t* slot_;
    uint64_t fallback_slot_;

public:
    Rooted(RootFrame& frame, T value)
        : slot_(frame.take_slot()), fallback_slot_(0) { set(value); }

    T get() const {
        return (T)(uintptr_t)*(slot_ ? slot_ : &fallback_slot_);
    }

    void set(T value) {
        *(slot_ ? slot_ : &fallback_slot_) = (uint64_t)(uintptr_t)value;
    }

    // GC-free input/format libraries deliberately have no runtime side stack.
    // Preserve handle semantics locally when their non-collecting RootFrame is invalid.
    uint64_t* home() { return slot_ ? slot_ : &fallback_slot_; }
    const uint64_t* home() const { return slot_ ? slot_ : &fallback_slot_; }

    Rooted(const Rooted&) = delete;
    Rooted& operator=(const Rooted&) = delete;
};

template <>
class Rooted<Item> {
    uint64_t* slot_;
    uint64_t fallback_slot_;

public:
    Rooted(RootFrame& frame, Item value)
        : slot_(frame.take_slot()), fallback_slot_(0) { set(value); }

    Item get() const { return (Item){.item = *(slot_ ? slot_ : &fallback_slot_)}; }
    void set(Item value) { *(slot_ ? slot_ : &fallback_slot_) = value.item; }
    uint64_t* home() { return slot_ ? slot_ : &fallback_slot_; }
    const uint64_t* home() const { return slot_ ? slot_ : &fallback_slot_; }

    Rooted(const Rooted&) = delete;
    Rooted& operator=(const Rooted&) = delete;
};

template <typename T>
class LambdaHandle {
    const uint64_t* slot_;

public:
    explicit LambdaHandle(const Rooted<T>& rooted) : slot_(rooted.home()) {}
    T get() const { return slot_ ? (T)(uintptr_t)*slot_ : (T)nullptr; }
};

template <>
class LambdaHandle<Item> {
    const uint64_t* slot_;

public:
    explicit LambdaHandle(const Rooted<Item>& rooted) : slot_(rooted.home()) {}
    Item get() const { return (Item){.item = slot_ ? *slot_ : 0}; }
};

template <typename T>
class LambdaMutableHandle {
    uint64_t* slot_;

public:
    explicit LambdaMutableHandle(Rooted<T>& rooted) : slot_(rooted.home()) {}
    T get() const { return slot_ ? (T)(uintptr_t)*slot_ : (T)nullptr; }
    void set(T value) { if (slot_) *slot_ = (uint64_t)(uintptr_t)value; }
};

template <>
class LambdaMutableHandle<Item> {
    uint64_t* slot_;

public:
    explicit LambdaMutableHandle(Rooted<Item>& rooted) : slot_(rooted.home()) {}
    Item get() const { return (Item){.item = slot_ ? *slot_ : 0}; }
    void set(Item value) { if (slot_) *slot_ = value.item; }
};

template <typename T>
class PersistentRooted {
    uint64_t slot_;
    bool registered_;

public:
    explicit PersistentRooted(T value)
        : slot_((uint64_t)(uintptr_t)value),
          registered_(heap_try_register_gc_root(&slot_)) {}

    ~PersistentRooted() {
        if (registered_) heap_unregister_gc_root(&slot_);
    }

    T get() const { return (T)(uintptr_t)slot_; }
    void set(T value) { slot_ = (uint64_t)(uintptr_t)value; }
    bool valid() const { return registered_; }
    uint64_t* home() { return &slot_; }

    PersistentRooted(const PersistentRooted&) = delete;
    PersistentRooted& operator=(const PersistentRooted&) = delete;
};

template <>
class PersistentRooted<Item> {
    uint64_t slot_;
    bool registered_;

public:
    explicit PersistentRooted(Item value)
        : slot_(value.item), registered_(heap_try_register_gc_root(&slot_)) {}

    ~PersistentRooted() {
        if (registered_) heap_unregister_gc_root(&slot_);
    }

    Item get() const { return (Item){.item = slot_}; }
    void set(Item value) { slot_ = value.item; }
    bool valid() const { return registered_; }
    uint64_t* home() { return &slot_; }

    PersistentRooted(const PersistentRooted&) = delete;
    PersistentRooted& operator=(const PersistentRooted&) = delete;
};

class AutoAssertNoGC {
public:
    AutoAssertNoGC() { heap_no_gc_scope_begin(); }
    ~AutoAssertNoGC() { heap_no_gc_scope_end(); }

    AutoAssertNoGC(const AutoAssertNoGC&) = delete;
    AutoAssertNoGC& operator=(const AutoAssertNoGC&) = delete;
};

class AutoDeferGC {
public:
    AutoDeferGC() { heap_gc_defer_collection_begin(); }
    ~AutoDeferGC() { heap_gc_defer_collection_end(); }

    AutoDeferGC(const AutoDeferGC&) = delete;
    AutoDeferGC& operator=(const AutoDeferGC&) = delete;
};

// JS_ROOTS(frame, name_a, value_a, name_b, value_b, ...) declares a RootFrame
// whose slot count is derived from the argument count, then one Rooted<Item>
// per (name, value) pair in order. Deriving the size removes the hand-counted
// `RootFrame roots(N)` mismatch class; the expansion is exactly the
// RootFrame/Rooted structure it replaces, so rooting behaviour is unchanged.
//
// A value expression must not contain a top-level comma (parenthesise it if
// it does) — the preprocessor splits arguments on those.
#define JS_PP_ARG_N( \
    _1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16, \
    _17,_18,_19,_20,_21,_22,_23,_24,N,...) N
#define JS_PP_NARG_(...) JS_PP_ARG_N(__VA_ARGS__)
#define JS_PP_NARG(...) JS_PP_NARG_(__VA_ARGS__, \
    24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)

#define JS_ROOTS_2(f, a, va) Rooted<Item> a(f, va);
#define JS_ROOTS_4(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_2(f, __VA_ARGS__)
#define JS_ROOTS_6(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_4(f, __VA_ARGS__)
#define JS_ROOTS_8(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_6(f, __VA_ARGS__)
#define JS_ROOTS_10(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_8(f, __VA_ARGS__)
#define JS_ROOTS_12(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_10(f, __VA_ARGS__)
#define JS_ROOTS_14(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_12(f, __VA_ARGS__)
#define JS_ROOTS_16(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_14(f, __VA_ARGS__)
#define JS_ROOTS_18(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_16(f, __VA_ARGS__)
#define JS_ROOTS_20(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_18(f, __VA_ARGS__)
#define JS_ROOTS_22(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_20(f, __VA_ARGS__)
#define JS_ROOTS_24(f, a, va, ...) Rooted<Item> a(f, va); JS_ROOTS_22(f, __VA_ARGS__)
#define JS_ROOTS_PICK_(N, f, ...) JS_ROOTS_##N(f, __VA_ARGS__)
#define JS_ROOTS_PICK(N, f, ...) JS_ROOTS_PICK_(N, f, __VA_ARGS__)
#define JS_ROOTS(f, ...) \
    RootFrame f(JS_PP_NARG(__VA_ARGS__) / 2); \
    JS_ROOTS_PICK(JS_PP_NARG(__VA_ARGS__), f, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
