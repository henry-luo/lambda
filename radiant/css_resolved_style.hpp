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

// selected-view computed state; size-query caches have a separate lifetime.
bool css_resolved_style_apply(LayoutContext* context, DomElement* element);
void css_resolved_style_release(LayoutCache* cache, Pool* pool);
void css_resolved_style_record(LayoutContext* context, CssPropertyCode property,
    const CssDeclaration* declaration, const CssValue* value, int64_t priority);
void css_resolved_style_note_length(LayoutContext* context, uintptr_t property,
    const CssValue* value);
void css_resolved_style_note_axis(LayoutContext* context, LayoutAxis axis);

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
