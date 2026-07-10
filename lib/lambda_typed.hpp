#pragma once

#include <assert.h>
#include <stdint.h>

#include "ownership.hpp"
#include "../lambda/lambda-data.hpp"

namespace lam {

template<TypeId Tag> struct ItemTagToType;

template<> struct ItemTagToType<LMD_TYPE_RAW_POINTER> { typedef void type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_NULL> { typedef void type; enum { is_pointer = false, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_BOOL> { typedef bool type; enum { is_pointer = false, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_NUM_SIZED> { typedef Item type; enum { is_pointer = false, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_INT> { typedef int64_t type; enum { is_pointer = false, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_INT64> { typedef int64_t type; enum { is_pointer = true, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_UINT64> { typedef uint64_t type; enum { is_pointer = true, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_FLOAT> { typedef double type; enum { is_pointer = true, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_DECIMAL> { typedef Decimal type; enum { is_pointer = true, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_DTIME> { typedef DateTime type; enum { is_pointer = true, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_SYMBOL> { typedef Symbol type; enum { is_pointer = true, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_STRING> { typedef String type; enum { is_pointer = true, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_BINARY> { typedef Binary type; enum { is_pointer = true, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_PATH> { typedef Path type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_RANGE> { typedef Range type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_ARRAY_NUM> { typedef ArrayNum type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_ARRAY> { typedef Array type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_MAP> { typedef Map type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_VMAP> { typedef VMap type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_ELEMENT> { typedef Element type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_OBJECT> { typedef Object type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_TYPE> { typedef Type type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_FUNC> { typedef Function type; enum { is_pointer = true, is_direct_pointer = true }; };
template<> struct ItemTagToType<LMD_TYPE_ANY> { typedef void type; enum { is_pointer = false, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_ERROR> { typedef LambdaError type; enum { is_pointer = true, is_direct_pointer = false }; };
template<> struct ItemTagToType<LMD_TYPE_UNDEFINED> { typedef void type; enum { is_pointer = false, is_direct_pointer = false }; };

template<TypeId Tag> struct LambdaAlwaysFalse { enum { value = false }; };

template<TypeId Tag>
class ItemOf {
    Item raw_;

public:
    typedef typename ItemTagToType<Tag>::type Pointee;

    ItemOf() {
        raw_.item = 0;
    }

    explicit ItemOf(Item it) : raw_(it) {
        assert(get_type_id(it) == Tag);
    }

    static constexpr TypeId tag() {
        return Tag;
    }

    static ItemOf<Tag> from_raw_unchecked(Item it) {
        ItemOf<Tag> witness;
        witness.raw_ = it;
        return witness;
    }

    template<TypeId T = Tag, typename OwnershipEnableIf<ItemTagToType<T>::is_pointer, int>::type = 0>
    static ItemOf<Tag> from_ptr(Pointee* p) {
        Item it;
        if constexpr (ItemTagToType<Tag>::is_direct_pointer) {
            it.item = (uint64_t)(uintptr_t)p;
        } else {
            it.item = ((uint64_t)Tag << 56) | (uint64_t)(uintptr_t)p;
        }
        return from_raw_unchecked(it);
    }

    Item raw() const {
        return raw_;
    }

    template<TypeId T = Tag, typename OwnershipEnableIf<ItemTagToType<T>::is_pointer, int>::type = 0>
    Pointee* ptr() const {
        if constexpr (ItemTagToType<Tag>::is_direct_pointer) {
            return (Pointee*)(uintptr_t)raw_.item;
        } else {
            return (Pointee*)(uintptr_t)(raw_.item & 0x00FFFFFFFFFFFFFFULL);
        }
    }

    template<TypeId T = Tag, typename OwnershipEnableIf<ItemTagToType<T>::is_pointer, int>::type = 0>
    Pointee* operator->() const {
        return ptr();
    }

    auto value() const {
        if constexpr (Tag == LMD_TYPE_BOOL) {
            return raw_.bool_val != 0;
        } else if constexpr (Tag == LMD_TYPE_INT) {
            return raw_.get_int56();
        } else if constexpr (Tag == LMD_TYPE_NUM_SIZED) {
            return raw_;
        } else {
            static_assert(LambdaAlwaysFalse<Tag>::value, "ItemOf<Tag>::value() is only available for inline value tags");
        }
    }
};

template<TypeId Tag>
class ItemMatch {
    bool matched_;
    ItemOf<Tag> value_;

public:
    ItemMatch() : matched_(false), value_() {}
    ItemMatch(Item it, bool matched) : matched_(matched), value_(matched ? ItemOf<Tag>(it) : ItemOf<Tag>()) {}

    explicit operator bool() const {
        return matched_;
    }

    ItemOf<Tag> operator*() const {
        return value_;
    }

    Item raw() const {
        return value_.raw();
    }

    auto value() const {
        return value_.value();
    }

    template<TypeId T = Tag, typename OwnershipEnableIf<ItemTagToType<T>::is_pointer, int>::type = 0>
    typename ItemTagToType<T>::type* ptr() const {
        return value_.ptr();
    }

    template<TypeId T = Tag, typename OwnershipEnableIf<ItemTagToType<T>::is_pointer, int>::type = 0>
    typename ItemTagToType<T>::type* operator->() const {
        return value_.ptr();
    }
};

template<TypeId Tag>
ItemMatch<Tag> as(Item it) {
    return ItemMatch<Tag>(it, get_type_id(it) == Tag);
}

template<TypeId Tag>
ItemOf<Tag> require(Item it) {
    assert(get_type_id(it) == Tag);
    return ItemOf<Tag>(it);
}

template<class F>
decltype(auto) visit(Item it, F&& f) {
    switch (get_type_id(it)) {
        case LMD_TYPE_RAW_POINTER: return f(require<LMD_TYPE_RAW_POINTER>(it));
        case LMD_TYPE_NULL: return f(require<LMD_TYPE_NULL>(it));
        case LMD_TYPE_BOOL: return f(require<LMD_TYPE_BOOL>(it));
        case LMD_TYPE_NUM_SIZED: return f(require<LMD_TYPE_NUM_SIZED>(it));
        case LMD_TYPE_INT: return f(require<LMD_TYPE_INT>(it));
        case LMD_TYPE_INT64: return f(require<LMD_TYPE_INT64>(it));
        case LMD_TYPE_UINT64: return f(require<LMD_TYPE_UINT64>(it));
        case LMD_TYPE_FLOAT: return f(require<LMD_TYPE_FLOAT>(it));
        case LMD_TYPE_DECIMAL: return f(require<LMD_TYPE_DECIMAL>(it));
        case LMD_TYPE_DTIME: return f(require<LMD_TYPE_DTIME>(it));
        case LMD_TYPE_SYMBOL: return f(require<LMD_TYPE_SYMBOL>(it));
        case LMD_TYPE_STRING: return f(require<LMD_TYPE_STRING>(it));
        case LMD_TYPE_BINARY: return f(require<LMD_TYPE_BINARY>(it));
        case LMD_TYPE_PATH: return f(require<LMD_TYPE_PATH>(it));
        case LMD_TYPE_RANGE: return f(require<LMD_TYPE_RANGE>(it));
        case LMD_TYPE_ARRAY_NUM: return f(require<LMD_TYPE_ARRAY_NUM>(it));
        case LMD_TYPE_ARRAY: return f(require<LMD_TYPE_ARRAY>(it));
        case LMD_TYPE_MAP: return f(require<LMD_TYPE_MAP>(it));
        case LMD_TYPE_VMAP: return f(require<LMD_TYPE_VMAP>(it));
        case LMD_TYPE_ELEMENT: return f(require<LMD_TYPE_ELEMENT>(it));
        case LMD_TYPE_OBJECT: return f(require<LMD_TYPE_OBJECT>(it));
        case LMD_TYPE_TYPE: return f(require<LMD_TYPE_TYPE>(it));
        case LMD_TYPE_FUNC: return f(require<LMD_TYPE_FUNC>(it));
        case LMD_TYPE_ANY: return f(require<LMD_TYPE_ANY>(it));
        case LMD_TYPE_ERROR: return f(require<LMD_TYPE_ERROR>(it));
        case LMD_TYPE_UNDEFINED: return f(require<LMD_TYPE_UNDEFINED>(it));
    }
    return f(it);
}

template<TypeId T> struct IsMapLike : FalseType {};
template<> struct IsMapLike<LMD_TYPE_MAP> : TrueType {};
template<> struct IsMapLike<LMD_TYPE_VMAP> : TrueType {};
template<> struct IsMapLike<LMD_TYPE_ELEMENT> : TrueType {};
template<> struct IsMapLike<LMD_TYPE_OBJECT> : TrueType {};

template<TypeId T> struct IsArrayLike : FalseType {};
template<> struct IsArrayLike<LMD_TYPE_ARRAY> : TrueType {};
template<> struct IsArrayLike<LMD_TYPE_ARRAY_NUM> : TrueType {};
template<> struct IsArrayLike<LMD_TYPE_ELEMENT> : TrueType {};

template<TypeId T> struct HasArrayStorage : FalseType {};
template<> struct HasArrayStorage<LMD_TYPE_ARRAY> : TrueType {};
template<> struct HasArrayStorage<LMD_TYPE_ELEMENT> : TrueType {};

template<TypeId T, typename OwnershipEnableIf<HasArrayStorage<T>::value, int>::type = 0>
Array* as_array(ItemOf<T> v) {
    return static_cast<Array*>(v.ptr());
}

inline ArrayNum* as_array_num(ItemOf<LMD_TYPE_ARRAY_NUM> v) {
    return v.ptr();
}

inline Map* as_map(ItemOf<LMD_TYPE_MAP> v) {
    return v.ptr();
}

inline Map* as_map(ItemOf<LMD_TYPE_OBJECT> v) {
    return (Map*)v.ptr();
}

inline VMap* as_vmap(ItemOf<LMD_TYPE_VMAP> v) {
    return v.ptr();
}

inline Element* as_element(ItemOf<LMD_TYPE_ELEMENT> v) {
    return v.ptr();
}

class HoleSentinel {
    Item raw_;

    explicit HoleSentinel(Item raw) : raw_(raw) {
        assert(is(raw));
    }

public:
    static uint64_t raw_value() {
        return ITEM_JS_DELETED_SENTINEL;
    }

    static HoleSentinel make() {
        Item raw;
        raw.item = raw_value();
        return HoleSentinel(raw);
    }

    static HoleSentinel from_raw(Item raw) {
        return HoleSentinel(raw);
    }

    static bool is(Item raw) {
        return raw.item == raw_value();  // RAW_ITEM_EQ_OK: typed-array hole sentinel identity.
    }

    Item raw() const {
        return raw_;
    }
};

inline Item hole_sentinel_item() {
    return HoleSentinel::make().raw();
}

inline bool is_hole_sentinel(Item raw) {
    return HoleSentinel::is(raw);
}

template<class Ret>
class [[nodiscard]] ItemOrError {
    Ret ret_;

public:
    explicit ItemOrError(Ret ret) : ret_(ret) {}

    bool ok() const {
        return ret_.err == nullptr;
    }

    bool has_error() const {
        return ret_.err != nullptr;
    }

    LambdaError* error() const {
        return ret_.err;
    }

    auto value() const {
        assert(!ret_.err);
        return ret_.value;
    }

    Ret raw() const {
        return ret_;
    }
};

template<class Ret>
ItemOrError<Ret> item_or_error(Ret ret) {
    return ItemOrError<Ret>(ret);
}

typedef GcPtr<ShapeEntry> ShapeRef;
typedef GcPtr<const ShapeEntry> ConstShapeRef;

inline ShapeRef shape_borrow(ShapeEntry* p) {
    return ShapeRef(p);
}

inline ConstShapeRef shape_borrow(const ShapeEntry* p) {
    return ConstShapeRef(p);
}

inline ShapeRef shape_next(ShapeRef shape) {
    return shape ? shape_borrow(shape->next) : ShapeRef();
}

inline ConstShapeRef shape_next(ConstShapeRef shape) {
    return shape ? shape_borrow((const ShapeEntry*)shape->next) : ConstShapeRef();
}

template<class T>
GcPtr<T> gc_borrow(T* p) {
    return GcPtr<T>(p);
}

} // namespace lam
