#include "view.hpp"
#include "layout.hpp"

#include <string.h>

template <typename Prop>
static Prop* ensure_view_prop(Pool* pool, Prop*& storage, const Prop& defaults) {
    if (!storage && pool) {
        storage = (Prop*)pool_calloc(pool, sizeof(Prop));
        memcpy(storage, &defaults, sizeof(Prop));
    }
    return storage;
}

static Pool* prop_pool(ViewTree* tree) { return tree ? tree->pool : nullptr; }

static Pool* prop_pool(LayoutContext* lycon) {
    if (!lycon) return nullptr;
    if (lycon->doc && lycon->doc->view_tree) return lycon->doc->view_tree->pool;
    // Focused layout tests and embedders can provide the pass pool without a ViewTree shell.
    return lycon->pool;
}

BlockProp* DomElement::ensure_block(ViewTree* tree) { return ensure_view_prop(prop_pool(tree), blk, BLOCK_PROP_DEFAULT); }
BoundaryProp* DomElement::ensure_boundary(ViewTree* tree) { return ensure_view_prop(prop_pool(tree), bound, BOUNDARY_PROP_DEFAULT); }
FontProp* DomElement::ensure_font(ViewTree* tree) { return ensure_view_prop(prop_pool(tree), font, FONT_PROP_DEFAULT); }
InlineProp* DomElement::ensure_inline(ViewTree* tree) { return ensure_view_prop(prop_pool(tree), in_line, INLINE_PROP_DEFAULT); }

static ScrollProp* ensure_scroll_prop(DomElement* element, Pool* pool) {
    ScrollProp* value = ensure_view_prop(pool, element->scroller, SCROLL_PROP_DEFAULT);
    if (value && !value->pane) {
        // Every allocated scroll group owns a pane; canonical defaults remain allocation-free.
        value->pane = (ScrollPane*)pool_calloc(pool, sizeof(ScrollPane));
    }
    return value;
}

ScrollProp* DomElement::ensure_scroll(ViewTree* tree) { return ensure_scroll_prop(this, prop_pool(tree)); }

PositionProp* DomElement::ensure_position(ViewTree* tree) { return ensure_view_prop(prop_pool(tree), position, POSITION_PROP_DEFAULT); }
EmbedProp* DomElement::ensure_embed(ViewTree* tree) { return ensure_view_prop(prop_pool(tree), embed, EMBED_PROP_DEFAULT); }
TransformProp* DomElement::ensure_transform(ViewTree* tree) { return ensure_view_prop(prop_pool(tree), transform, TRANSFORM_PROP_DEFAULT); }
FilterProp* DomElement::ensure_filter(ViewTree* tree) { return ensure_view_prop(prop_pool(tree), filter, FILTER_PROP_DEFAULT); }

static MultiColumnProp* ensure_multicol_prop(DomElement* element, Pool* pool) {
    MultiColumnProp* value = element->multicol_prop();
    if (!value && pool) {
        value = (MultiColumnProp*)pool_calloc(pool, sizeof(MultiColumnProp));
        memcpy(value, &MULTICOL_PROP_DEFAULT, sizeof(MultiColumnProp));
        element->set_multicol_prop(value);
    }
    return value;
}

MultiColumnProp* DomElement::ensure_multicol(ViewTree* tree) { return ensure_multicol_prop(this, prop_pool(tree)); }

FlexItemProp* DomElement::ensure_flex_item(ViewTree* tree) {
    if (parent_item_kind() == PARENT_ITEM_GRID) return nullptr;
    if (parent_item_kind() != PARENT_ITEM_FLEX) {
        fi = nullptr;
        set_parent_item_kind(PARENT_ITEM_FLEX);
    }
    return ensure_view_prop(prop_pool(tree), fi, FLEX_ITEM_PROP_DEFAULT);
}

GridItemProp* DomElement::ensure_grid_item(ViewTree* tree) {
    if (parent_item_kind() == PARENT_ITEM_FLEX) return nullptr;
    if (parent_item_kind() != PARENT_ITEM_GRID) {
        gi = nullptr;
        set_parent_item_kind(PARENT_ITEM_GRID);
    }
    return ensure_view_prop(prop_pool(tree), gi, GRID_ITEM_PROP_DEFAULT);
}

TableProp* DomElement::ensure_table(ViewTree* tree) {
    if (role_kind() != ROLE_NONE && role_kind() != ROLE_TABLE) return nullptr;
    if (role_kind() != ROLE_TABLE) { tb = nullptr; set_role_kind(ROLE_TABLE); }
    return ensure_view_prop(prop_pool(tree), tb, TABLE_PROP_DEFAULT);
}

TableCellProp* DomElement::ensure_cell(ViewTree* tree) {
    if (role_kind() != ROLE_NONE && role_kind() != ROLE_CELL) return nullptr;
    if (role_kind() != ROLE_CELL) { td = nullptr; set_role_kind(ROLE_CELL); }
    return ensure_view_prop(prop_pool(tree), td, TABLE_CELL_PROP_DEFAULT);
}

FormControlProp* DomElement::ensure_form(ViewTree* tree) {
    if (role_kind() != ROLE_NONE && role_kind() != ROLE_FORM) return nullptr;
    if (role_kind() != ROLE_FORM) { form = nullptr; set_role_kind(ROLE_FORM); }
    return ensure_view_prop(prop_pool(tree), form, FORM_CONTROL_PROP_DEFAULT);
}

BoundaryProp* DomElement::ensure_boundary(LayoutContext* lycon) { return ensure_view_prop(prop_pool(lycon), bound, BOUNDARY_PROP_DEFAULT); }
InlineProp* DomElement::ensure_inline(LayoutContext* lycon) { return ensure_view_prop(prop_pool(lycon), in_line, INLINE_PROP_DEFAULT); }
PositionProp* DomElement::ensure_position(LayoutContext* lycon) { return ensure_view_prop(prop_pool(lycon), position, POSITION_PROP_DEFAULT); }
EmbedProp* DomElement::ensure_embed(LayoutContext* lycon) { return ensure_view_prop(prop_pool(lycon), embed, EMBED_PROP_DEFAULT); }
TransformProp* DomElement::ensure_transform(LayoutContext* lycon) { return ensure_view_prop(prop_pool(lycon), transform, TRANSFORM_PROP_DEFAULT); }
FilterProp* DomElement::ensure_filter(LayoutContext* lycon) { return ensure_view_prop(prop_pool(lycon), filter, FILTER_PROP_DEFAULT); }

ScrollProp* DomElement::ensure_scroll(LayoutContext* lycon) { return ensure_scroll_prop(this, prop_pool(lycon)); }

MultiColumnProp* DomElement::ensure_multicol(LayoutContext* lycon) { return ensure_multicol_prop(this, prop_pool(lycon)); }

BlockProp* DomElement::ensure_block(LayoutContext* lycon) {
    bool was_absent = !blk;
    BlockProp* value = ensure_view_prop(prop_pool(lycon), blk, BLOCK_PROP_DEFAULT);
    if (value && was_absent && lycon) {
        // Text alignment and direction inherit from the active block context, not the canonical CSS initial.
        value->text_align = lycon->block.text_align;
        value->direction = lycon->block.direction;
    }
    return value;
}

FontProp* DomElement::ensure_font(LayoutContext* lycon) {
    bool was_absent = !font;
    FontProp* value = ensure_view_prop(prop_pool(lycon), font, FONT_PROP_DEFAULT);
    if (value && was_absent && lycon && lycon->font.style) {
        // Font groups inherit as a value snapshot so later cascade mutation remains element-owned.
        *value = *lycon->font.style;
        value->owns_font_handle = false;
        assert(value->font_size >= 0);
    }
    return value;
}
