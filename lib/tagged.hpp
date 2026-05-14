#pragma once

#include <assert.h>

#include "../radiant/view.hpp"

namespace lam {

template<bool B, class T = int>
struct EnableIf {};

template<class T>
struct EnableIf<true, T> {
    typedef T type;
};

template<ViewType T> struct ViewTagToType;
template<> struct ViewTagToType<RDT_VIEW_TEXT> { typedef ViewText type; };
template<> struct ViewTagToType<RDT_VIEW_BR> { typedef ViewElement type; };
template<> struct ViewTagToType<RDT_VIEW_MARKER> { typedef ViewMarker type; };
template<> struct ViewTagToType<RDT_VIEW_INLINE> { typedef ViewSpan type; };
template<> struct ViewTagToType<RDT_VIEW_MATH> { typedef ViewSpan type; };
template<> struct ViewTagToType<RDT_VIEW_INLINE_BLOCK> { typedef ViewBlock type; };
template<> struct ViewTagToType<RDT_VIEW_BLOCK> { typedef ViewBlock type; };
template<> struct ViewTagToType<RDT_VIEW_LIST_ITEM> { typedef ViewBlock type; };
template<> struct ViewTagToType<RDT_VIEW_TABLE> { typedef ViewTable type; };
template<> struct ViewTagToType<RDT_VIEW_TABLE_ROW_GROUP> { typedef ViewTableRowGroup type; };
template<> struct ViewTagToType<RDT_VIEW_TABLE_ROW> { typedef ViewTableRow type; };
template<> struct ViewTagToType<RDT_VIEW_TABLE_CELL> { typedef ViewTableCell type; };
template<> struct ViewTagToType<RDT_VIEW_TABLE_COLUMN_GROUP> { typedef ViewBlock type; };
template<> struct ViewTagToType<RDT_VIEW_TABLE_COLUMN> { typedef ViewBlock type; };

template<ViewType T>
typename ViewTagToType<T>::type* view_as(View* v) {
    if (!v || v->view_type != T) return nullptr;
    return static_cast<typename ViewTagToType<T>::type*>(v);
}

template<ViewType T>
const typename ViewTagToType<T>::type* view_as(const View* v) {
    if (!v || v->view_type != T) return nullptr;
    return static_cast<const typename ViewTagToType<T>::type*>(v);
}

template<ViewType T>
typename ViewTagToType<T>::type* view_require(View* v) {
    assert(v && v->view_type == T);
    return static_cast<typename ViewTagToType<T>::type*>(v);
}

template<ViewType T>
const typename ViewTagToType<T>::type* view_require(const View* v) {
    assert(v && v->view_type == T);
    return static_cast<const typename ViewTagToType<T>::type*>(v);
}

template<class F>
decltype(auto) visit_view(View* v, F&& f) {
    if (!v) return f(static_cast<View*>(nullptr));
    switch (v->view_type) {
        case RDT_VIEW_TEXT: return f(view_require<RDT_VIEW_TEXT>(v));
        case RDT_VIEW_BR: return f(view_require<RDT_VIEW_BR>(v));
        case RDT_VIEW_MARKER: return f(view_require<RDT_VIEW_MARKER>(v));
        case RDT_VIEW_INLINE: return f(view_require<RDT_VIEW_INLINE>(v));
        case RDT_VIEW_MATH: return f(view_require<RDT_VIEW_MATH>(v));
        case RDT_VIEW_INLINE_BLOCK: return f(view_require<RDT_VIEW_INLINE_BLOCK>(v));
        case RDT_VIEW_BLOCK: return f(view_require<RDT_VIEW_BLOCK>(v));
        case RDT_VIEW_LIST_ITEM: return f(view_require<RDT_VIEW_LIST_ITEM>(v));
        case RDT_VIEW_TABLE: return f(view_require<RDT_VIEW_TABLE>(v));
        case RDT_VIEW_TABLE_ROW_GROUP: return f(view_require<RDT_VIEW_TABLE_ROW_GROUP>(v));
        case RDT_VIEW_TABLE_ROW: return f(view_require<RDT_VIEW_TABLE_ROW>(v));
        case RDT_VIEW_TABLE_CELL: return f(view_require<RDT_VIEW_TABLE_CELL>(v));
        case RDT_VIEW_TABLE_COLUMN_GROUP: return f(view_require<RDT_VIEW_TABLE_COLUMN_GROUP>(v));
        case RDT_VIEW_TABLE_COLUMN: return f(view_require<RDT_VIEW_TABLE_COLUMN>(v));
        case RDT_VIEW_NONE: return f(static_cast<View*>(nullptr));
    }
    return f(static_cast<View*>(nullptr));
}

template<ViewType T> struct IsBlockView { enum { value = false }; };
template<> struct IsBlockView<RDT_VIEW_INLINE_BLOCK> { enum { value = true }; };
template<> struct IsBlockView<RDT_VIEW_BLOCK> { enum { value = true }; };
template<> struct IsBlockView<RDT_VIEW_LIST_ITEM> { enum { value = true }; };
template<> struct IsBlockView<RDT_VIEW_TABLE> { enum { value = true }; };
template<> struct IsBlockView<RDT_VIEW_TABLE_ROW_GROUP> { enum { value = true }; };
template<> struct IsBlockView<RDT_VIEW_TABLE_ROW> { enum { value = true }; };
template<> struct IsBlockView<RDT_VIEW_TABLE_CELL> { enum { value = true }; };
template<> struct IsBlockView<RDT_VIEW_TABLE_COLUMN_GROUP> { enum { value = true }; };
template<> struct IsBlockView<RDT_VIEW_TABLE_COLUMN> { enum { value = true }; };

template<ViewType T>
using EnableIfBlockView = typename EnableIf<IsBlockView<T>::value, int>::type;

template<ViewType T, EnableIfBlockView<T> = 0>
ViewBlock* view_as_block(View* v) {
    return v && v->view_type == T ? static_cast<ViewBlock*>(v) : nullptr;
}

inline bool view_type_is_block(ViewType type) {
    switch (type) {
        case RDT_VIEW_INLINE_BLOCK:
        case RDT_VIEW_BLOCK:
        case RDT_VIEW_LIST_ITEM:
        case RDT_VIEW_TABLE:
        case RDT_VIEW_TABLE_ROW_GROUP:
        case RDT_VIEW_TABLE_ROW:
        case RDT_VIEW_TABLE_CELL:
        case RDT_VIEW_TABLE_COLUMN_GROUP:
        case RDT_VIEW_TABLE_COLUMN:
            return true;
        default:
            return false;
    }
}

inline ViewElement* view_as_element(View* v) {
    return v && v->is_element() ? static_cast<ViewElement*>(v) : nullptr;
}

inline const ViewElement* view_as_element(const View* v) {
    return v && v->is_element() ? static_cast<const ViewElement*>(v) : nullptr;
}

inline ViewElement* view_require_element(View* v) {
    assert(v && v->is_element());
    return static_cast<ViewElement*>(v);
}

inline const ViewElement* view_require_element(const View* v) {
    assert(v && v->is_element());
    return static_cast<const ViewElement*>(v);
}

inline ViewBlock* view_as_block(View* v) {
    return v && view_type_is_block(v->view_type) ? static_cast<ViewBlock*>(v) : nullptr;
}

inline const ViewBlock* view_as_block(const View* v) {
    return v && view_type_is_block(v->view_type) ? static_cast<const ViewBlock*>(v) : nullptr;
}

inline ViewBlock* view_require_block(View* v) {
    assert(v && view_type_is_block(v->view_type));
    return static_cast<ViewBlock*>(v);
}

inline const ViewBlock* view_require_block(const View* v) {
    assert(v && view_type_is_block(v->view_type));
    return static_cast<const ViewBlock*>(v);
}

inline ViewText* view_require_text(View* v) {
    return view_require<RDT_VIEW_TEXT>(v);
}

inline const ViewText* view_require_text(const View* v) {
    return view_require<RDT_VIEW_TEXT>(v);
}

inline ViewTableCell* view_require_table_cell(View* v) {
    return view_require<RDT_VIEW_TABLE_CELL>(v);
}

inline const ViewTableCell* view_require_table_cell(const View* v) {
    return view_require<RDT_VIEW_TABLE_CELL>(v);
}

inline View* dom_view(DomNode* n) {
    return static_cast<View*>(n);
}

inline const View* dom_view(const DomNode* n) {
    return static_cast<const View*>(n);
}

inline DomNode* view_dom_node(View* v) {
    return static_cast<DomNode*>(v);
}

inline const DomNode* view_dom_node(const View* v) {
    return static_cast<const DomNode*>(v);
}

inline ViewBlock* unsafe_view_block_api_span(ViewSpan* span) {
    // Some legacy layout APIs accept ViewBlock* but only touch DomElement/ViewSpan
    // storage. ViewBlock adds no fields, so this preserves ABI layout while making
    // the exceptional conversion grepable.
    return reinterpret_cast<ViewBlock*>(span);
}

inline ViewText* unsafe_view_text_storage(DomText* text) {
    // DomText and ViewText share storage; ViewText currently adds no fields.
    return reinterpret_cast<ViewText*>(text);
}

inline ViewTable* unsafe_view_table_storage(ViewBlock* block) {
    // ViewTable currently adds table helper methods only. Some setup paths need
    // table storage before the runtime view tag has been switched to TABLE.
    return reinterpret_cast<ViewTable*>(block);
}

inline ViewTable* unsafe_view_table_storage(DomNode* node) {
    return reinterpret_cast<ViewTable*>(node);
}

template<DomNodeType T> struct DomNodeTagToType;
template<> struct DomNodeTagToType<DOM_NODE_ELEMENT> { typedef DomElement type; };
template<> struct DomNodeTagToType<DOM_NODE_TEXT> { typedef DomText type; };
template<> struct DomNodeTagToType<DOM_NODE_COMMENT> { typedef DomComment type; };
template<> struct DomNodeTagToType<DOM_NODE_DOCTYPE> { typedef DomComment type; };

template<DomNodeType T>
typename DomNodeTagToType<T>::type* dom_as(DomNode* n) {
    if (!n || n->node_type != T) return nullptr;
    return static_cast<typename DomNodeTagToType<T>::type*>(n);
}

template<DomNodeType T>
const typename DomNodeTagToType<T>::type* dom_as(const DomNode* n) {
    if (!n || n->node_type != T) return nullptr;
    return static_cast<const typename DomNodeTagToType<T>::type*>(n);
}

template<DomNodeType T>
typename DomNodeTagToType<T>::type* dom_require(DomNode* n) {
    assert(n && n->node_type == T);
    return static_cast<typename DomNodeTagToType<T>::type*>(n);
}

template<DomNodeType T>
const typename DomNodeTagToType<T>::type* dom_require(const DomNode* n) {
    assert(n && n->node_type == T);
    return static_cast<const typename DomNodeTagToType<T>::type*>(n);
}

inline DomElement* dom_require_element(DomNode* n) {
    return dom_require<DOM_NODE_ELEMENT>(n);
}

inline const DomElement* dom_require_element(const DomNode* n) {
    return dom_require<DOM_NODE_ELEMENT>(n);
}

inline DomText* dom_require_text(DomNode* n) {
    return dom_require<DOM_NODE_TEXT>(n);
}

inline const DomText* dom_require_text(const DomNode* n) {
    return dom_require<DOM_NODE_TEXT>(n);
}

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
