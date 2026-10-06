#pragma once

// Pointer-kind field templates for Radiant Heap objects.
//
// Every long-lived Radiant object has one owner in one ownership tree, and every
// pointer field declares how it relates to that tree:
//
//   Own<T>      the field owns the target; it is traced and torn down with the holder
//   OwnArr<T>   the field owns a block of scalar elements (no element tracing)
//   OwnSpan<T>  an OwnArr with its element count, for arrays sized at runtime
//   Up<T>       the target is in the holder's node or an ancestor node (outlives it)
//   Counted<T>  the target is owned elsewhere and pinned by a count
//   Shared<T>   the target is a value shared by several holders (an ancestor's
//               prop, an interned canonical entry); its owner keeps it alive
//               for every holder. A holder writes through it only after a
//               copy-on-write gate has replaced it with the holder's private
//               copy, and the holder records that it owns that copy
//   Handle<T>   index + generation into a slot table; not dereferenceable
//   ViewProp<T, Slot>  a Document-level node owns T in its document's view-tree
//               storage; the view tree releases or clears it before that storage goes
//   ViewRef<T, Slot>   a Document-level node borrows T from view-tree storage (or
//               from something that outlives it); cleared the same way
//   Slot names the holder's view slot; only a slot listed in the holder's
//   slot X-macro exists, and the view-tree teardown handles every listed slot.
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

template<bool B, class R = void> struct KindEnableIf {};
template<class R> struct KindEnableIf<true, R> { typedef R type; };

// U* converts implicitly to T* (same type, added const, or derived to base)
template<class U, class T> struct KindUpcast {
    static char test(T*);
    static long test(...);
    static const bool value = sizeof(test((U*)nullptr)) == sizeof(char);
};

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
    operator T*() const { return p_; }                                               \
    /* explicit casts read like casts of the raw pointer, e.g.                   */  \
    /* (DomElement*)node->parent; the kind constrains writes, not reads          */  \
    template<class U> explicit operator U*() const { return (U*)p_; }

// Same kind, derived to base: Up<DomElement> -> Up<DomNode>. Not for OwnArr,
// where the element type sets the stride.
#define LAM_MEM_KIND_UPCAST(Kind)                                                    \
    template<class U, class = typename KindEnableIf<KindUpcast<U, T>::value>::type>  \
    constexpr Kind(Kind<U> o) : p_(o.p_) {}

template<class T> struct Up {
    LAM_MEM_KIND_POINTER_BODY(Up)
    LAM_MEM_KIND_UPCAST(Up)
};

template<class T> struct Own {
    LAM_MEM_KIND_POINTER_BODY(Own)
    LAM_MEM_KIND_UPCAST(Own)
    // a non-owning view of the owned target, for passing down or linking back
    Up<T> borrow() const { return Up<T>(p_); }
};

template<class T> struct OwnArr {
    LAM_MEM_KIND_POINTER_BODY(OwnArr)
    T& operator[](size_t i) const { return p_[i]; }
};

template<class T> struct Counted {
    LAM_MEM_KIND_POINTER_BODY(Counted)
    LAM_MEM_KIND_UPCAST(Counted)
};

template<class T> struct Shared {
    LAM_MEM_KIND_POINTER_BODY(Shared)
    LAM_MEM_KIND_UPCAST(Shared)
};

template<class T> struct Foreign {
    LAM_MEM_KIND_POINTER_BODY(Foreign)
    LAM_MEM_KIND_UPCAST(Foreign)
};

#undef LAM_MEM_KIND_POINTER_BODY
#undef LAM_MEM_KIND_UPCAST

// A pointer from a Document-level node into its document's view tree. A
// ViewTree exists only for a viewed document, so these are the one sanctioned
// downward pointers: the slot ties each field to its view-tree teardown entry.
template<class T> struct ViewPropInit { T* p_; };
template<class T> struct ViewRefInit { T* p_; };

template<class T, auto Slot> struct ViewProp {
    static_assert(!KindIsVoid<T>::value, "ViewProp<void> has no static target type");
    T* p_;
    ViewProp() = default;
    explicit constexpr ViewProp(T* p) : p_(p) {}
    constexpr ViewProp(ViewPropInit<T> i) : p_(i.p_) {}
    constexpr ViewProp(decltype(nullptr)) : p_(nullptr) {}
    ViewProp& operator=(decltype(nullptr)) { p_ = nullptr; return *this; }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    operator T*() const { return p_; }
    template<class U> explicit operator U*() const { return (U*)p_; }
    static constexpr auto slot = Slot;
};

template<class T, auto Slot> struct ViewRef {
    static_assert(!KindIsVoid<T>::value, "ViewRef<void> has no static target type");
    T* p_;
    ViewRef() = default;
    explicit constexpr ViewRef(T* p) : p_(p) {}
    constexpr ViewRef(ViewRefInit<T> i) : p_(i.p_) {}
    constexpr ViewRef(decltype(nullptr)) : p_(nullptr) {}
    ViewRef& operator=(decltype(nullptr)) { p_ = nullptr; return *this; }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    operator T*() const { return p_; }
    template<class U> explicit operator U*() const { return (U*)p_; }
    static constexpr auto slot = Slot;
};

// Kind constructors with the target type deduced, for write sites:
// child->parent = lam::up(element) declares the store as an outliving link.
template<class T> constexpr Up<T> up(T* p) { return Up<T>(p); }
// borrowing an owned or outliving field keeps the outlives claim
template<class T> constexpr Up<T> up(const Own<T>& o) { return Up<T>(o.p_); }
template<class T> constexpr Up<T> up(const Up<T>& u) { return u; }
template<class T> constexpr Up<T> up(const OwnArr<T>& a) { return Up<T>(a.p_); }
// borrowing a view-tree field from a holder below the document (Stack, a pass)
template<class T, auto S> constexpr Up<T> up(const ViewProp<T, S>& v) { return Up<T>(v.p_); }
template<class T, auto S> constexpr Up<T> up(const ViewRef<T, S>& v) { return Up<T>(v.p_); }
template<class T> constexpr Own<T> own(T* p) { return Own<T>(p); }
template<class T> constexpr OwnArr<T> own_arr(T* p) { return OwnArr<T>(p); }
template<class T> constexpr Counted<T> counted(T* p) { return Counted<T>(p); }
template<class T> constexpr Shared<T> shared(T* p) { return Shared<T>(p); }
template<class T> constexpr Foreign<T> foreign(T* p) { return Foreign<T>(p); }
// the field's slot comes from its declaration, so write sites name only the target
template<class T> constexpr ViewPropInit<T> view_prop(T* p) { return ViewPropInit<T>{p}; }
template<class T> constexpr ViewRefInit<T> view_ref(T* p) { return ViewRefInit<T>{p}; }

// An owned array and its element count in one field, so a tracer or an
// external verifier can bound every element access by the recorded length.
template<class T> struct OwnSpan {
    OwnArr<T> data;
    size_t count;

    T& operator[](size_t i) const { return data[i]; }
    T* begin() const { return data.get(); }
    T* end() const { return data.get() + count; }
    bool empty() const { return count == 0; }
};

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
LAM_MEM_KIND_ASSERT_LAYOUT(Shared);
LAM_MEM_KIND_ASSERT_LAYOUT(Foreign);
#undef LAM_MEM_KIND_ASSERT_LAYOUT

static_assert(sizeof(OwnSpan<int>) == sizeof(int*) + sizeof(size_t), "OwnSpan is a pointer and a count");
static_assert(__is_trivial(OwnSpan<int>), "OwnSpan must be trivial");
static_assert(sizeof(Handle<int>) == 8, "Handle must be two 32-bit words");
static_assert(__is_trivial(Handle<int>), "Handle must be trivial");
static_assert(__is_standard_layout(Handle<int>), "Handle must be standard-layout");

} // namespace lam
