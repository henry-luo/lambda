#ifndef RADIANT_FLEX_HPP
#define RADIANT_FLEX_HPP

#include "view.hpp"

typedef enum {
    DIR_ROW = CSS_VALUE_ROW,
    DIR_ROW_REVERSE = CSS_VALUE_ROW_REVERSE,
    DIR_COLUMN = CSS_VALUE_COLUMN,
    DIR_COLUMN_REVERSE = CSS_VALUE_COLUMN_REVERSE
} FlexDirection;

typedef enum {
    WRAP_NOWRAP = CSS_VALUE_NOWRAP,
    WRAP_WRAP = CSS_VALUE_WRAP,
    WRAP_WRAP_REVERSE = CSS_VALUE_WRAP_REVERSE
} FlexWrap;

typedef enum {
    JUSTIFY_START = CSS_VALUE_FLEX_START,
    JUSTIFY_END = CSS_VALUE_FLEX_END,
    JUSTIFY_CENTER = CSS_VALUE_CENTER,
    JUSTIFY_SPACE_BETWEEN = CSS_VALUE_SPACE_BETWEEN,
    JUSTIFY_SPACE_AROUND = CSS_VALUE_SPACE_AROUND,
    JUSTIFY_SPACE_EVENLY = CSS_VALUE_SPACE_EVENLY
} JustifyContent;

typedef enum {
    ALIGN_AUTO = CSS_VALUE_AUTO,
    ALIGN_START = CSS_VALUE_FLEX_START,
    ALIGN_END = CSS_VALUE_FLEX_END,
    ALIGN_CENTER = CSS_VALUE_CENTER,
    ALIGN_BASELINE = CSS_VALUE_BASELINE,
    ALIGN_STRETCH = CSS_VALUE_STRETCH,
    ALIGN_SPACE_BETWEEN = CSS_VALUE_SPACE_BETWEEN,
    ALIGN_SPACE_AROUND = CSS_VALUE_SPACE_AROUND,
    ALIGN_SPACE_EVENLY = CSS_VALUE_SPACE_EVENLY
} AlignType;

// Note: Visibility, PositionType, WritingMode, TextDirection are now defined in view.hpp
// to ensure they're available before FlexProp uses them

// structs (with field names in snake_case)
typedef struct { int x, y; } Point;

typedef struct {
    int flex_basis;  // -1 for auto
    float flex_grow;
    float flex_shrink;
    AlignType align_self;
    int order;
    float aspect_ratio;
    int baseline_offset;
    // Flags for percentage values
    int is_flex_basis_percent : 1;
    int is_margin_top_auto : 1;
    int is_margin_right_auto : 1;
    int is_margin_bottom_auto : 1;
    int is_margin_left_auto : 1;
} FlexItemProp;

typedef struct FlexItem : FlexItemProp {
    Point pos;
    int width, height;
    int min_width, max_width;
    int min_height, max_height;
    int margin[4];  // top, right, bottom, left
    Visibility visibility;
    PositionType position;
    // flags for percentage values
    int is_width_percent : 1;
    int is_height_percent : 1;
    int is_min_width_percent : 1;
    int is_max_width_percent : 1;
    int is_min_height_percent : 1;
    int is_max_height_percent : 1;
} FlexItem;

typedef struct {
    FlexItem** items;
    int item_count;
    int total_base_size;
    int height;
} FlexLine;

// Add a temporary structure to keep track of original indices during sorting
typedef struct {
    FlexItem item;
    int original_index;
} FlexItemWithIndex;

typedef struct {
    FlexDirection direction;
    FlexWrap wrap;
    JustifyContent justify;
    AlignType align_items;
    AlignType align_content;
    int row_gap;
    int column_gap;
} FlexContainerProp;

typedef struct FlexContainer: FlexContainerProp {
    int width, height;
    FlexItem* items;
    int item_count;
    WritingMode writing_mode;
    TextDirection text_direction;
} FlexContainer;

#endif // RADIANT_FLEX_HPP
