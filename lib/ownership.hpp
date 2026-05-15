#pragma once

#include <assert.h>
#include <stddef.h>
#include <new>

#include "mem.h"
#include "mempool.h"
#include "arena.h"

namespace lam {

struct GcHeapDomain {};
struct PoolDomain {};
struct LayoutSessionDomain {};
struct InputScratchDomain {};

template<bool Value>
struct BoolConstant {
    static const bool value = Value;
};

typedef BoolConstant<true> TrueType;
typedef BoolConstant<false> FalseType;

template<bool Condition, class T = void>
struct OwnershipEnableIf {};

template<class T>
struct OwnershipEnableIf<true, T> {
    typedef T type;
};

template<class SourceDomain, class StorageDomain>
struct DomainOutlives : FalseType {};

template<> struct DomainOutlives<PoolDomain, PoolDomain> : TrueType {};
template<> struct DomainOutlives<GcHeapDomain, GcHeapDomain> : TrueType {};
template<> struct DomainOutlives<LayoutSessionDomain, LayoutSessionDomain> : TrueType {};
template<> struct DomainOutlives<PoolDomain, LayoutSessionDomain> : TrueType {};
template<> struct DomainOutlives<GcHeapDomain, LayoutSessionDomain> : TrueType {};
template<> struct DomainOutlives<GcHeapDomain, PoolDomain> : TrueType {};
template<> struct DomainOutlives<InputScratchDomain, InputScratchDomain> : TrueType {};
template<> struct DomainOutlives<GcHeapDomain, InputScratchDomain> : TrueType {};
template<> struct DomainOutlives<PoolDomain, InputScratchDomain> : TrueType {};

template<class Domain> struct DomainTraits;

template<>
struct DomainTraits<GcHeapDomain> {
    template<class T>
    static void destroy(T*) {}
};

template<>
struct DomainTraits<PoolDomain> {
    template<class T>
    static void destroy(T*) {}
};

template<>
struct DomainTraits<LayoutSessionDomain> {
    template<class T>
    static void destroy(T* p) {
        if (!p) return;
        p->~T();
        mem_free(p);
    }
};

template<>
struct DomainTraits<InputScratchDomain> {
    template<class T>
    static void destroy(T*) {}
};

template<class T, class Domain>
class BorrowedPtr {
    T* p_;

public:
    constexpr BorrowedPtr() : p_(nullptr) {}
    explicit constexpr BorrowedPtr(T* p) : p_(p) {}

    T* get() const { return p_; }
    T* operator->() const { return p_; }
    T& operator*() const { return *p_; }
    explicit operator bool() const { return p_ != nullptr; }
};

template<class T, class Domain>
class OwnedPtr {
    T* p_;

public:
    OwnedPtr() : p_(nullptr) {}
    explicit OwnedPtr(T* p) : p_(p) {}

    OwnedPtr(OwnedPtr&& o) noexcept : p_(o.p_) {
        o.p_ = nullptr;
    }

    OwnedPtr& operator=(OwnedPtr&& o) noexcept {
        if (this != &o) reset(o.release());
        return *this;
    }

    OwnedPtr(const OwnedPtr&) = delete;
    OwnedPtr& operator=(const OwnedPtr&) = delete;

    ~OwnedPtr() { reset(); }

    T* get() const { return p_; }
    T* operator->() const { return p_; }
    T& operator*() const { return *p_; }
    explicit operator bool() const { return p_ != nullptr; }

    T* release() {
        T* t = p_;
        p_ = nullptr;
        return t;
    }

    void reset(T* n = nullptr) {
        if (p_ != n) {
            DomainTraits<Domain>::destroy(p_);
        }
        p_ = n;
    }

    operator BorrowedPtr<T, Domain>() const {
        return BorrowedPtr<T, Domain>(p_);
    }
};

template<class T> using GcPtr = BorrowedPtr<T, GcHeapDomain>;
template<class T> using PoolPtr = BorrowedPtr<T, PoolDomain>;
template<class T> using SessionPtr = OwnedPtr<T, LayoutSessionDomain>;

template<class T, class Domain>
BorrowedPtr<const T, Domain> borrow_const(BorrowedPtr<T, Domain> b) {
    return BorrowedPtr<const T, Domain>(b.get());
}

template<class T>
PoolPtr<T> pool_make(Pool* pool) {
    return PoolPtr<T>(static_cast<T*>(pool_calloc(pool, sizeof(T))));
}

template<class T>
SessionPtr<T> session_make(MemCategory cat) {
    void* raw = mem_alloc(sizeof(T), cat);
    if (!raw) return SessionPtr<T>();
    return SessionPtr<T>(::new (raw) T());
}

inline OwnedPtr<char, LayoutSessionDomain> session_strdup(const char* s, MemCategory cat) {
    return OwnedPtr<char, LayoutSessionDomain>(mem_strdup(s, cat));
}

template<class T>
PoolPtr<T> checked_pool_ptr(Pool* pool, T* raw) {
    (void)pool;
    return PoolPtr<T>(raw);
}

template<class T, class Domain>
class PersistentField {
    T* p_;

public:
    PersistentField() : p_(nullptr) {}

    template<class SourceDomain>
    typename OwnershipEnableIf<DomainOutlives<SourceDomain, Domain>::value>::type
    set(BorrowedPtr<T, SourceDomain> b) {
        p_ = b.get();
    }

    template<class S>
    void set(OwnedPtr<T, S>&&) = delete;

    template<class S>
    typename OwnershipEnableIf<!DomainOutlives<S, Domain>::value>::type
    set(BorrowedPtr<T, S>) = delete;

    T* get() const { return p_; }
};

template<class T, class Domain>
class PersistentFieldRef {
    T*& p_;

public:
    explicit PersistentFieldRef(T*& p) : p_(p) {}

    template<class SourceDomain>
    typename OwnershipEnableIf<DomainOutlives<SourceDomain, Domain>::value>::type
    set(BorrowedPtr<T, SourceDomain> b) {
        p_ = b.get();
    }

    void clear() { p_ = nullptr; }

    template<class S>
    void set(OwnedPtr<T, S>&&) = delete;

    template<class S>
    typename OwnershipEnableIf<!DomainOutlives<S, Domain>::value>::type
    set(BorrowedPtr<T, S>) = delete;

    T* get() const { return p_; }
};

template<class T>
PoolPtr<T> promote_to_pool(Pool* pool, const T* src) {
    if (!src) return PoolPtr<T>();
    void* raw = pool_alloc(pool, sizeof(T));
    if (!raw) return PoolPtr<T>();
    return PoolPtr<T>(::new (raw) T(*src));
}

inline PoolPtr<char> promote_to_pool(Pool* pool, const char* s) {
    return PoolPtr<char>(pool_strdup(pool, s));
}

inline PoolPtr<char> promote_to_arena(Arena* arena, const char* s) {
    return PoolPtr<char>(arena_strdup(arena, s));
}

typedef struct Heap Heap;

template<class T>
GcPtr<T> copy_to_gc(Heap* heap, const T* src) = delete;

template<class T>
SessionPtr<T> take_ownership(T* raw) {
    return SessionPtr<T>(raw);
}

template<class T>
T* detach_session_buffer(SessionPtr<T>& p) {
    return p.release();
}

template<class T, class D>
T* unsafe_borrow_raw(BorrowedPtr<T, D> p) {
    return p.get();
}

template<class T, class D>
T* unsafe_release(OwnedPtr<T, D>& p) {
    return p.release();
}

} // namespace lam
