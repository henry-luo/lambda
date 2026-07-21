#pragma once

#include <assert.h>

#include "../lambda-data.hpp"

namespace lam {

template<TypeId Tag> struct TagToType;
template<> struct TagToType<LMD_TYPE_STRING> { typedef String type; };
template<> struct TagToType<LMD_TYPE_SYMBOL> { typedef Symbol type; };
template<> struct TagToType<LMD_TYPE_BINARY> { typedef Binary type; };
template<> struct TagToType<LMD_TYPE_RANGE> { typedef Range type; };
template<> struct TagToType<LMD_TYPE_ARRAY_NUM> { typedef ArrayNum type; };
template<> struct TagToType<LMD_TYPE_ARRAY> { typedef Array type; };
template<> struct TagToType<LMD_TYPE_MAP> { typedef Map type; };
template<> struct TagToType<LMD_TYPE_VMAP> { typedef VMap type; };
template<> struct TagToType<LMD_TYPE_ELEMENT> { typedef Element type; };
template<> struct TagToType<LMD_TYPE_OBJECT> { typedef Object type; };
template<> struct TagToType<LMD_TYPE_TYPE> { typedef Type type; };
template<> struct TagToType<LMD_TYPE_FUNC> { typedef Function type; };

template<TypeId Tag>
typename TagToType<Tag>::type* item_payload(Item raw);

template<>
inline String* item_payload<LMD_TYPE_STRING>(Item raw) { return raw.get_string(); }
template<>
inline Symbol* item_payload<LMD_TYPE_SYMBOL>(Item raw) { return raw.get_symbol(); }
template<>
inline Binary* item_payload<LMD_TYPE_BINARY>(Item raw) { return raw.get_binary(); }
template<>
inline Range* item_payload<LMD_TYPE_RANGE>(Item raw) { return raw.range; }
template<>
inline ArrayNum* item_payload<LMD_TYPE_ARRAY_NUM>(Item raw) { return raw.array_num; }
template<>
inline Array* item_payload<LMD_TYPE_ARRAY>(Item raw) { return raw.array; }
template<>
inline Map* item_payload<LMD_TYPE_MAP>(Item raw) { return raw.map; }
template<>
inline VMap* item_payload<LMD_TYPE_VMAP>(Item raw) { return raw.vmap; }
template<>
inline Element* item_payload<LMD_TYPE_ELEMENT>(Item raw) { return raw.element; }
template<>
inline Object* item_payload<LMD_TYPE_OBJECT>(Item raw) { return raw.object; }
template<>
inline Type* item_payload<LMD_TYPE_TYPE>(Item raw) { return raw.type; }
template<>
inline Function* item_payload<LMD_TYPE_FUNC>(Item raw) { return raw.function; }

template<TypeId Tag>
class ItemOf {
    Item raw_;

public:
    explicit ItemOf(Item raw) : raw_(raw) {
        assert(get_type_id(raw) == Tag);
    }

    Item raw() const { return raw_; }

    typename TagToType<Tag>::type* get() const {
        return item_payload<Tag>(raw_);
    }

    typename TagToType<Tag>::type* operator->() const {
        return get();
    }
};

template<TypeId Tag>
class MaybeItemOf {
    Item raw_;
    bool has_;

public:
    MaybeItemOf() : raw_(ItemNull), has_(false) {}
    explicit MaybeItemOf(Item raw) : raw_(raw), has_(true) {}

    explicit operator bool() const { return has_; }
    ItemOf<Tag> value() const {
        assert(has_);
        return ItemOf<Tag>(raw_);
    }
};

template<TypeId Tag>
MaybeItemOf<Tag> item_as(Item raw) {
    if (get_type_id(raw) != Tag) return MaybeItemOf<Tag>();
    return MaybeItemOf<Tag>(raw);
}

} // namespace lam
