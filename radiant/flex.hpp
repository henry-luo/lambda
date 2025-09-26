#ifndef RADIANT_FLEX_HPP
#define RADIANT_FLEX_HPP

// CRITICAL FIX: Align enum values with Lexbor constants to eliminate conversion functions
// This ensures perfect compatibility between Radiant enums and Lexbor CSS constants

// Include Lexbor constants
#include "../lexbor/source/lexbor/css/value/const.h"

// Define custom constants not in Lexbor
#ifndef LXB_CSS_VALUE_SPACE_EVENLY
#define LXB_CSS_VALUE_SPACE_EVENLY (LXB_CSS_VALUE__LAST_ENTRY + 28)
#endif

typedef enum { 
    DIR_ROW = LXB_CSS_VALUE_ROW,                        // 0x010c
    DIR_ROW_REVERSE = LXB_CSS_VALUE_ROW_REVERSE,        // 0x010d
    DIR_COLUMN = LXB_CSS_VALUE_COLUMN,                  // 0x010e
    DIR_COLUMN_REVERSE = LXB_CSS_VALUE_COLUMN_REVERSE   // 0x010f
} FlexDirection;

typedef enum { 
    WRAP_NOWRAP = LXB_CSS_VALUE_NOWRAP,                 // 0x0111
    WRAP_WRAP = LXB_CSS_VALUE_WRAP,                     // 0x0112
    WRAP_WRAP_REVERSE = LXB_CSS_VALUE_WRAP_REVERSE      // 0x0113
} FlexWrap;

typedef enum { 
    JUSTIFY_START = LXB_CSS_VALUE_FLEX_START,           // 0x0005
    JUSTIFY_END = LXB_CSS_VALUE_FLEX_END,               // 0x0006
    JUSTIFY_CENTER = LXB_CSS_VALUE_CENTER,              // 0x0007
    JUSTIFY_SPACE_BETWEEN = LXB_CSS_VALUE_SPACE_BETWEEN, // 0x0008
    JUSTIFY_SPACE_AROUND = LXB_CSS_VALUE_SPACE_AROUND,  // 0x0009
    JUSTIFY_SPACE_EVENLY = LXB_CSS_VALUE_SPACE_EVENLY   // Custom constant
} JustifyContent;

typedef enum { 
    ALIGN_AUTO = LXB_CSS_VALUE_AUTO,                    // 0x000c
    ALIGN_START = LXB_CSS_VALUE_FLEX_START,             // 0x0005
    ALIGN_END = LXB_CSS_VALUE_FLEX_END,                 // 0x0006
    ALIGN_CENTER = LXB_CSS_VALUE_CENTER,                // 0x0007
    ALIGN_BASELINE = LXB_CSS_VALUE_BASELINE,            // 0x000b
    ALIGN_STRETCH = LXB_CSS_VALUE_STRETCH,              // 0x000a
    ALIGN_SPACE_BETWEEN = LXB_CSS_VALUE_SPACE_BETWEEN,  // 0x0008
    ALIGN_SPACE_AROUND = LXB_CSS_VALUE_SPACE_AROUND,    // 0x0009
    ALIGN_SPACE_EVENLY = LXB_CSS_VALUE_SPACE_EVENLY     // Custom constant
} AlignType;
typedef enum { VIS_VISIBLE, VIS_HIDDEN, VIS_COLLAPSE } Visibility;
typedef enum { POS_STATIC, POS_ABSOLUTE } PositionType;
typedef enum { WM_HORIZONTAL_TB, WM_VERTICAL_RL, WM_VERTICAL_LR } WritingMode;
typedef enum { TD_LTR, TD_RTL } TextDirection;

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
