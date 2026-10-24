#pragma once

#include "layout.hpp"

namespace radiant {

enum CssStyleMiss : uint8_t {
    CSS_STYLE_COLD, CSS_STYLE_CONTEXT, CSS_STYLE_CHANGED, CSS_STYLE_VIEW_RESET,
    CSS_STYLE_PARENT, CSS_STYLE_FONT, CSS_STYLE_STORAGE, CSS_STYLE_VIEWPORT,
    CSS_STYLE_INHERIT, CSS_STYLE_PERCENTAGE, CSS_STYLE_CONTAINER, CSS_STYLE_EFFECT,
    CSS_STYLE_ALLOCATION, CSS_STYLE_MISS_COUNT
};
extern uint64_t css_style_misses[CSS_STYLE_MISS_COUNT];
const char* css_style_miss_name(CssStyleMiss reason);
CssStyleMiss css_resolved_style_last_miss(DomElement* element);
uint32_t css_resolved_style_input_revision(LayoutContext* context, DomElement* element);
uint32_t css_resolved_style_inherited_revision(DomElement* element);

struct CssResolvedSize {
    float value;
    bool percentage;
};
// the caller first applies CSS for this query; never read layout-consumed preferred sizes.
bool css_resolved_style_size(LayoutContext* context, DomElement* element,
    LayoutAxis axis, CssResolvedSize* result);

// tier-3: geometry inputs shared by CSS used values and intrinsic queries.
struct CssGeometryContext {
    SizeF block, parent, viewport;
    ViewBlock* percentage_owner; // borrowed containing-box identity for dependent descendants
    float inline_basis;
    float parent_given_width, parent_given_height;
    bool has_parent, intrinsic_width, intrinsic_height, vertical_inline, percentage_height_auto;
    bool percentage_owner_definite;
};

struct CssAxisValues {
    float given, minimum, maximum, percent, min_percent, max_percent;
    float fit_limit, fit_percent;
    CssEnum type, min_type, max_type;
};

struct CssUsedGeometry {
    CssEnum box_sizing;
    bool context_width, context_height;
    float context_given_width, context_given_height;
    CssAxisValues axes[2];
    Margin margin;
    Spacing padding, border_width;
    float insets[4], inset_percents[4];
    bool inset_present[4];
    float flex_basis, flex_grow, flex_shrink, aspect_ratio;
    CssEnum align_self;
    int order;
    bool flex_percent, flex_content, flex_stretch;
    CssEnum position, clear, float_prop;
    int z_index;
    bool has_z_index;
};
// retained flow restores used lengths without changing the computed record's clean baseline.
void css_used_geometry_capture(LayoutContext* context, DomElement* element, CssUsedGeometry* geometry);
void css_used_geometry_restore(LayoutContext* context, DomElement* element, const CssUsedGeometry& geometry);
CssGeometryContext css_geometry_context(LayoutContext* context, DomElement* element);
bool css_geometry_context_equal(const CssGeometryContext& a, const CssGeometryContext& b);
bool css_resolved_geometry_context_equal(DomElement* element,
    const CssGeometryContext& a, const CssGeometryContext& b);

// selected-view computed state; size-query caches have a separate lifetime.
bool css_resolved_style_apply(LayoutContext* context, DomElement* element);
bool css_resolved_style_cache_allowed(LayoutContext* context, DomElement* element);
bool css_resolved_style_matches_context(LayoutContext* context, DomElement* element,
    bool computed_dependencies_only = false);
bool css_resolved_style_is_containing_block_independent(DomElement* element);
bool css_resolved_style_is_block_size_independent(DomElement* element);
void css_resolved_style_release(LayoutCache* cache, Pool* pool);
void css_resolved_style_record(LayoutContext* context, CssPropertyCode property,
    const CssDeclaration* declaration, const CssValue* value, int64_t priority);
void css_resolved_style_note_length(LayoutContext* context, uintptr_t property,
    const CssValue* value);
void css_resolved_style_note_axis(LayoutContext* context, LayoutAxis axis);
void css_resolved_style_record_html_axis(LayoutContext* context, LayoutAxis axis, float value,
    bool percentage, bool persist, bool clear_unresolved, bool store_resolved);

struct CssStyleRecordScope {
    LayoutContext* context;
    CssResolvedStyle* saved;
    CssResolvedStyle* recording;
    CssStyleRecordScope(LayoutContext* context, DomElement* element);
    void commit();
    ~CssStyleRecordScope();
    CssStyleRecordScope(const CssStyleRecordScope&) = delete;
    CssStyleRecordScope& operator=(const CssStyleRecordScope&) = delete;
};

} // namespace radiant

// normalized property application shares the existing dispatcher and side rules.
void css_apply_computed_property(CssPropertyCode property,
    const CssDeclaration* declaration, const CssValue* value, int64_t priority,
    LayoutContext* context);

extern int64_t g_style_resolve_incremental;
extern int64_t g_style_geometry_evaluations;
extern int64_t g_style_resolve_full;
extern int64_t g_style_resolve_measure;
