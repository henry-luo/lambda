#pragma once

// Pointer-kind field templates for Radiant Heap objects.
//
// Every long-lived Radiant object has one owner in one ownership tree, and every
// pointer field declares how it relates to that tree:
//
//   Own<T>      the field owns the target; it is traced and torn down with the holder
//   OwnArr<T>   the field owns a block of scalar elements (no element tracing)
//   Up<T>       the target is in the holder's node or an ancestor node (outlives it)
//   Counted<T>  the target is owned elsewhere and pinned by a count
//   Handle<T>   index + generation into a slot table; not dereferenceable
//   Foreign<T>  an opaque vendor resource (ThorVG, FreeType, platform objects)
//
// The wrappers are pointer-sized, trivial and standard-layout, so structs that
// are zero-allocated from pools and copied with memcpy keep working and keep
// their layout. Reads convert implicitly to T*; writes need the kind's explicit
// constructor, so a raw pointer never silently becomes an owning or outliving one.
// Locals and parameters stay raw T*: a raw pointer is a borrow for the current turn.
//
// What the types check: that each field has a kind and that kinds are not mixed
// on assignment. What they do not check: that a claimed kind is true (for example
// that an Up<T> really points into an outliving node). That is left to lint over
// the AST and the debug heap tracer.

#include <stddef.h>
#include <stdint.h>

namespace lam {

template<class T> struct KindIsVoid { static const bool value = false; };
template<> struct KindIsVoid<void> { static const bool value = true; };
template<> struct KindIsVoid<const void> { static const bool value = true; };

// Shared body for the dereferenceable kinds. Each kind is its own type, so a
// value of one kind never converts to another.
#define LAM_MEM_KIND_POINTER_BODY(Kind)                                              \
    static_assert(!KindIsVoid<T>::value, #Kind "<void> has no static target type"); \
    T* p_;                                                                           \
    Kind() = default;                                                                \
    explicit constexpr Kind(T* p) : p_(p) {}                                         \
    constexpr Kind(decltype(nullptr)) : p_(nullptr) {}                              \
    Kind& operator=(decltype(nullptr)) { p_ = nullptr; return *this; }               \
    T* get() const { return p_; }                                                    \
    T* operator->() const { return p_; }                                             \
    operator T*() const { return p_; }

template<class T> struct Up {
    LAM_MEM_KIND_POINTER_BODY(Up)
};

template<class T> struct Own {
    LAM_MEM_KIND_POINTER_BODY(Own)
    // a non-owning view of the owned target, for passing down or linking back
    Up<T> borrow() const { return Up<T>(p_); }
};

template<class T> struct OwnArr {
    LAM_MEM_KIND_POINTER_BODY(OwnArr)
    T& operator[](size_t i) const { return p_[i]; }
};

template<class T> struct Counted {
    LAM_MEM_KIND_POINTER_BODY(Counted)
};

template<class T> struct Foreign {
    LAM_MEM_KIND_POINTER_BODY(Foreign)
};

#undef LAM_MEM_KIND_POINTER_BODY

// Slot reference: valid only through lookup in the owning slot table, which
// compares generations and returns null for a recycled slot.
template<class T> struct Handle {
    static_assert(!KindIsVoid<T>::value, "Handle<void> has no static target type");
    uint32_t index;
    uint32_t gen;   // 0 = null handle

    bool is_null() const { return gen == 0; }
    bool operator==(const Handle& o) const { return index == o.index && gen == o.gen; }
    bool operator!=(const Handle& o) const { return !(*this == o); }
};

// The node (ownership-tree level) that holds a Heap struct's storage. Declared
// once per struct; lint and the heap tracer use it to check Up<T> fields
// against the ancestor relation. Undefined for types that are not Heap structs.
template<class T> struct NodeOf;

// Layout pins shared by every kind: a kind must be a drop-in for the raw field.
#define LAM_MEM_KIND_ASSERT_LAYOUT(Kind)                                                  \
    static_assert(sizeof(Kind<int>) == sizeof(int*), #Kind " must be pointer-sized");     \
    static_assert(alignof(Kind<int>) == alignof(int*), #Kind " must align like a pointer"); \
    static_assert(__is_trivial(Kind<int>), #Kind " must be trivial");                      \
    static_assert(__is_standard_layout(Kind<int>), #Kind " must be standard-layout")

LAM_MEM_KIND_ASSERT_LAYOUT(Own);
LAM_MEM_KIND_ASSERT_LAYOUT(OwnArr);
LAM_MEM_KIND_ASSERT_LAYOUT(Up);
LAM_MEM_KIND_ASSERT_LAYOUT(Counted);
LAM_MEM_KIND_ASSERT_LAYOUT(Foreign);
#undef LAM_MEM_KIND_ASSERT_LAYOUT

static_assert(sizeof(Handle<int>) == 8, "Handle must be two 32-bit words");
static_assert(__is_trivial(Handle<int>), "Handle must be trivial");
static_assert(__is_standard_layout(Handle<int>), "Handle must be standard-layout");

} // namespace lam
