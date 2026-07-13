#pragma once

#include "layout.hpp"

typedef struct VelmtBox {
    float x;
    float y;
    float width;
    float height;
} VelmtBox;

typedef struct VelmtEdges {
    float left;
    float right;
    float top;
    float bottom;
} VelmtEdges;

typedef struct Velmt {
    View* view;
    DomElement* element;
    int index;
    VelmtBox border_box;
    VelmtEdges margin;
    VelmtEdges border;
    VelmtEdges padding;
} Velmt;

typedef struct CustomLayoutContext {
    LayoutContext* lycon;
    ViewBlock* parent;
    const char* layout_name;
    Velmt* children;
    int child_count;
    float available_width;
    float available_height;
    float css_width;
    float css_height;
    CssEnum direction;
    const char* writing_mode;
} CustomLayoutContext;

typedef struct CustomLayoutPlacement {
    int child_index;
    float x;
    float y;
} CustomLayoutPlacement;

typedef struct CustomLayoutResult {
    CustomLayoutPlacement* placements;
    int placement_count;
    int placement_capacity;
    float width;
    float height;
    bool has_width;
    bool has_height;
} CustomLayoutResult;

typedef bool (*CustomLayoutFn)(const CustomLayoutContext* context, CustomLayoutResult* result);

bool custom_layout_register(const char* name, CustomLayoutFn fn);
CustomLayoutFn custom_layout_lookup(const char* name);
bool custom_layout_result_place(CustomLayoutResult* result, int child_index, float x, float y);

const char* custom_layout_name_from_css_value(const CssValue* value);
const char* custom_layout_name_for_element(DomElement* element);
bool layout_custom_apply(LayoutContext* lycon, ViewBlock* block, const char* layout_name);
