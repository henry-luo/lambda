#include "view.hpp"

#include <assert.h>
#include <math.h>
#include <string.h>

static BlockProp make_block_prop_default() {
    BlockProp value = {};
    value.text_align = CSS_VALUE_START;
    value.align_content = CSS_VALUE__UNDEF;
    value.direction = CSS_VALUE_LTR;
    value.text_spacing_trim = CSS_VALUE_NORMAL;
    value.break_before = CSS_VALUE_AUTO;
    value.break_after = CSS_VALUE_AUTO;
    value.orphans = 2;
    value.widows = 2;
    value.tab_size = 8;
    value.given_min_width = value.given_max_width = -1.0f;
    value.given_min_height = value.given_max_height = -1.0f;
    value.box_sizing = CSS_VALUE_CONTENT_BOX;
    value.box_decoration_break = CSS_VALUE_SLICE;
    value.given_width = value.given_height = -1.0f;
    value.given_width_percent = value.given_height_percent = NAN;
    value.contain_intrinsic_width = value.contain_intrinsic_height = -1.0f;
    value.given_min_width_percent = value.given_max_width_percent = NAN;
    value.given_min_height_percent = value.given_max_height_percent = NAN;
    value.text_indent_percent = NAN;
    return value;
}

static FontProp make_font_prop_default() {
    FontProp value = {};
    value.font_size = 16.0f;
    value.font_style = CSS_VALUE_NORMAL;
    value.font_weight = CSS_VALUE_NORMAL;
    value.font_weight_numeric = 400;
    value.font_variant = CSS_VALUE_NORMAL;
    value.font_size_from_medium = true;
    return value;
}

static InlineProp make_inline_prop_default() {
    InlineProp value = {};
    value.cursor = CSS_VALUE_AUTO;
    value.caret_shape = CSS_VALUE_AUTO;
    value.vertical_align = CSS_VALUE_BASELINE;
    value.opacity = 1.0f;
    value.visibility = CSS_VALUE_VISIBLE;
    value.mix_blend_mode = CSS_VALUE_NORMAL;
    return value;
}

static ScrollProp make_scroll_prop_default() {
    ScrollProp value = {};
    value.overflow_x = value.overflow_y = CSS_VALUE_VISIBLE;
    return value;
}

static PositionProp make_position_prop_default() {
    PositionProp value = {};
    value.position = CSS_VALUE_STATIC;
    value.top_percent = value.right_percent = NAN;
    value.bottom_percent = value.left_percent = NAN;
    value.clear = CSS_VALUE_NONE;
    value.float_prop = CSS_VALUE_NONE;
    return value;
}

static EmbedProp make_embed_prop_default() {
    EmbedProp value = {};
    value.object_fit = CSS_VALUE_FILL;
    value.object_position_x = value.object_position_y = 50.0f;
    value.object_position_x_is_percent = true;
    value.object_position_y_is_percent = true;
    return value;
}

static TransformProp make_transform_prop_default() {
    TransformProp value = {};
    value.origin_x = value.origin_y = 50.0f;
    value.origin_x_percent = value.origin_y_percent = true;
    value.perspective_origin_x = value.perspective_origin_y = 50.0f;
    value.transform_style = (CssEnum)0;
    value.backface_visibility = CSS_VALUE_VISIBLE;
    return value;
}

static MultiColumnProp make_multicol_prop_default() {
    MultiColumnProp value = {};
    value.column_gap = 16.0f;
    value.column_gap_is_normal = true;
    value.rule_style = CSS_VALUE_NONE;
    value.rule_color.a = 255;
    value.span = COLUMN_SPAN_NONE;
    value.fill = COLUMN_FILL_BALANCE;
    value.wrap = COLUMN_WRAP_AUTO;
    return value;
}

static FlexItemProp make_flex_item_prop_default() {
    FlexItemProp value = {};
    value.flex_basis = -1.0f;
    value.flex_shrink = 1.0f;
    value.align_self = CSS_VALUE_AUTO;
    return value;
}

static GridItemProp make_grid_item_prop_default() {
    GridItemProp value = {};
    value.justify_self = CSS_VALUE_AUTO;
    value.align_self_grid = CSS_VALUE_AUTO;
    value.is_grid_auto_placed = true;
    return value;
}

extern const BlockProp BLOCK_PROP_DEFAULT = make_block_prop_default();
extern const BoundaryProp BOUNDARY_PROP_DEFAULT = {};
extern const FontProp FONT_PROP_DEFAULT = make_font_prop_default();
extern const InlineProp INLINE_PROP_DEFAULT = make_inline_prop_default();
extern const ScrollProp SCROLL_PROP_DEFAULT = make_scroll_prop_default();
extern const PositionProp POSITION_PROP_DEFAULT = make_position_prop_default();
extern const EmbedProp EMBED_PROP_DEFAULT = make_embed_prop_default();
extern const TransformProp TRANSFORM_PROP_DEFAULT = make_transform_prop_default();
extern const FilterProp FILTER_PROP_DEFAULT = {};
extern const MultiColumnProp MULTICOL_PROP_DEFAULT = make_multicol_prop_default();
extern const FlexItemProp FLEX_ITEM_PROP_DEFAULT = make_flex_item_prop_default();
extern const GridItemProp GRID_ITEM_PROP_DEFAULT = make_grid_item_prop_default();
extern const TableProp TABLE_PROP_DEFAULT = {};
extern const TableCellProp TABLE_CELL_PROP_DEFAULT = {};
extern const FormControlProp FORM_CONTROL_PROP_DEFAULT = {};

const BlockProp* DomElement::block() const { return blk ? blk : &BLOCK_PROP_DEFAULT; }
const BoundaryProp* DomElement::boundary() const { return bound ? bound : &BOUNDARY_PROP_DEFAULT; }
const FontProp* DomElement::fontp() const { return font ? font : &FONT_PROP_DEFAULT; }
const InlineProp* DomElement::inl() const { return in_line ? in_line : &INLINE_PROP_DEFAULT; }
const ScrollProp* DomElement::scroll() const { return scroller ? scroller : &SCROLL_PROP_DEFAULT; }
const PositionProp* DomElement::positionp() const { return position ? position : &POSITION_PROP_DEFAULT; }
const EmbedProp* DomElement::embedp() const { return embed ? embed : &EMBED_PROP_DEFAULT; }
const TransformProp* DomElement::transformp() const { return transform ? transform : &TRANSFORM_PROP_DEFAULT; }
const FilterProp* DomElement::filterp() const { return filter ? filter : &FILTER_PROP_DEFAULT; }
BlockProp* DomElement::block_mut() { return blk; }
BoundaryProp* DomElement::boundary_mut() { return bound; }
FontProp* DomElement::font_mut() { return font; }
InlineProp* DomElement::inline_mut() {
    // Callers without an allocator cannot perform COW. Keep this legacy accessor
    // from silently exposing immutable canonical storage to a writer.
    assert(!inline_prop_shared());
    return in_line;
}
ScrollProp* DomElement::scroll_mut() { return scroller; }
PositionProp* DomElement::position_mut() { return position; }
EmbedProp* DomElement::embed_mut() { return embed; }
TransformProp* DomElement::transform_mut() { return transform; }
FilterProp* DomElement::filter_mut() { return filter; }
