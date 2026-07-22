#pragma once

// Native GC rooting is runtime policy. Keep these C++ helpers beside the
// runtime side-stack API so value-model headers do not own collection state.
#include "lambda-stack.h"

#ifdef __cplusplus
extern "C++" {
#endif

class RootFrame {
    LambdaRootFrame frame_;

public:
    RootFrame(Context* runtime, size_t slot_count) : frame_{} {
        // Pool/arena-backed input paths have no collecting runtime, so their
        // fallback homes are safe; only a real runtime reservation may fail closed.
        if (runtime && !lambda_root_frame_begin(runtime, &frame_, slot_count)) {
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
    Context* runtime_;
    uint64_t slot_;
    bool registered_;

public:
    PersistentRooted(Context* runtime, T value)
        : runtime_(runtime), slot_((uint64_t)(uintptr_t)value),
          registered_(heap_register_gc_root_for(runtime_, &slot_)) {}

    ~PersistentRooted() {
        if (registered_) heap_unregister_gc_root_for(runtime_, &slot_);
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
    Context* runtime_;
    uint64_t slot_;
    bool registered_;

public:
    PersistentRooted(Context* runtime, Item value)
        : runtime_(runtime), slot_(value.item),
          registered_(heap_register_gc_root_for(runtime_, &slot_)) {}

    ~PersistentRooted() {
        if (registered_) heap_unregister_gc_root_for(runtime_, &slot_);
    }

    Item get() const { return (Item){.item = slot_}; }
    void set(Item value) { slot_ = value.item; }
    bool valid() const { return registered_; }
    uint64_t* home() { return &slot_; }

    PersistentRooted(const PersistentRooted&) = delete;
    PersistentRooted& operator=(const PersistentRooted&) = delete;
};

class AutoAssertNoGC {
    Context* runtime_;

public:
    explicit AutoAssertNoGC(Context* runtime) : runtime_(runtime) {
        heap_no_gc_scope_begin(runtime_);
    }
    ~AutoAssertNoGC() { heap_no_gc_scope_end(runtime_); }

    AutoAssertNoGC(const AutoAssertNoGC&) = delete;
    AutoAssertNoGC& operator=(const AutoAssertNoGC&) = delete;
};

class AutoDeferGC {
    Context* runtime_;

public:
    explicit AutoDeferGC(Context* runtime) : runtime_(runtime) {
        heap_gc_defer_collection_begin(runtime_);
    }
    ~AutoDeferGC() { heap_gc_defer_collection_end(runtime_); }

    AutoDeferGC(const AutoDeferGC&) = delete;
    AutoDeferGC& operator=(const AutoDeferGC&) = delete;
};

#ifdef __cplusplus
}
#endif
