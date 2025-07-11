// enums
typedef enum { DIR_ROW, DIR_ROW_REVERSE, DIR_COLUMN, DIR_COLUMN_REVERSE } FlexDirection;
typedef enum { WRAP_NOWRAP, WRAP_WRAP, WRAP_WRAP_REVERSE } FlexWrap;
typedef enum { JUSTIFY_START, JUSTIFY_END, JUSTIFY_CENTER, JUSTIFY_SPACE_BETWEEN, JUSTIFY_SPACE_AROUND, JUSTIFY_SPACE_EVENLY } JustifyContent;
typedef enum { 
    ALIGN_START, ALIGN_END, ALIGN_CENTER, ALIGN_BASELINE, ALIGN_STRETCH,
    ALIGN_SPACE_BETWEEN, ALIGN_SPACE_AROUND, ALIGN_SPACE_EVENLY
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

typedef struct {
    Point pos;
    int width, height;
    int min_width, max_width;
    int min_height, max_height;
    int margin[4];  // top, right, bottom, left
    Visibility visibility;
    PositionType position;
    FlexItemProp; // embed FlexItemProp
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

typedef struct {
    int width, height;
    FlexItem* items;
    int item_count;
    FlexContainerProp; // embed FlexContainerProp
    WritingMode writing_mode;
    TextDirection text_direction;
} FlexContainer;