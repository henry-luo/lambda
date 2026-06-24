#ifndef RADIANT_EDITING_GEOMETRY_HPP
#define RADIANT_EDITING_GEOMETRY_HPP

#include "dom_range.hpp"
#include "editing.hpp"

struct DocState;
struct DomText;
struct EditingTargetRange;
struct TextRect;
struct UiContext;
class DomElement;

enum EditingBoundaryKind {
    EDITING_BOUNDARY_NONE = 0,
    EDITING_BOUNDARY_DOM,
    EDITING_BOUNDARY_TEXT_CONTROL
};

enum EditingClampPolicy {
    EDITING_CLAMP_WITHIN_SURFACE = 0,
    EDITING_CLAMP_SKIP_TEXT_CONTROLS
};

enum EditingPointBehavior {
    EDITING_POINT_BEHAVIOR_DEFAULT = 0,
    EDITING_POINT_BEHAVIOR_MAC
};

struct EditingBoundary {
    EditingBoundaryKind kind;
    EditingSurface surface;
    DomBoundary dom;
    View* view;
    uint32_t offset;
};

struct EditingCaretRect {
    float x;
    float y;
    float width;
    float height;
    bool valid;
};

typedef void (*EditingGeometryRectCb)(float x, float y, float w, float h,
                                      void* userdata);

void editing_boundary_clear(EditingBoundary* out);
void editing_caret_rect_clear(EditingCaretRect* out);

bool editing_geometry_surface_contains_boundary(const EditingSurface* surface,
                                                const EditingBoundary* boundary);

bool editing_geometry_surface_contains_range(const EditingSurface* surface,
                                             const DomRange* range);

bool editing_geometry_surface_contains_target_range(
    const EditingSurface* surface,
    const EditingTargetRange* range);

bool editing_geometry_hit_test_boundary(UiContext* uicon,
                                        View* root_view,
                                        const EditingSurface* surface,
                                        float vx,
                                        float vy,
                                        EditingClampPolicy policy,
                                        EditingBoundary* out,
                                        EditingPointBehavior behavior =
                                            EDITING_POINT_BEHAVIOR_DEFAULT);

bool editing_geometry_text_control_offset_for_point(UiContext* uicon,
                                                    DomElement* elem,
                                                    float vx,
                                                    float vy,
                                                    uint32_t* out_offset);

bool editing_geometry_text_control_boundary_from_point(UiContext* uicon,
                                                       DomElement* elem,
                                                       float vx,
                                                       float vy,
                                                       EditingBoundary* out);

bool editing_geometry_text_control_caret_rect(UiContext* uicon,
                                              DomElement* elem,
                                              uint32_t offset,
                                              EditingCaretRect* out);

bool editing_geometry_text_control_for_each_selection_rect(UiContext* uicon,
                                                           DomElement* elem,
                                                           uint32_t start_offset,
                                                           uint32_t end_offset,
                                                           EditingGeometryRectCb cb,
                                                           void* userdata);

bool editing_geometry_dom_text_boundary_from_byte_offset(DomText* text,
                                                         uint32_t byte_offset,
                                                         EditingBoundary* out);

bool editing_geometry_dom_text_boundary_from_point(UiContext* uicon,
                                                   DomText* text,
                                                   TextRect* rect,
                                                   float vx,
                                                   float vy,
                                                   EditingBoundary* out);

bool editing_geometry_dom_text_caret_rect(UiContext* uicon,
                                          DomText* text,
                                          uint32_t byte_offset,
                                          EditingCaretRect* out);

bool editing_geometry_caret_rect(UiContext* uicon,
                                 const EditingBoundary* boundary,
                                 EditingCaretRect* out);

#endif // RADIANT_EDITING_GEOMETRY_HPP
