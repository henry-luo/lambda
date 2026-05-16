#pragma once

#include "layout.hpp"
#include "layout_cache.hpp"

namespace radiant {

struct LayoutRunModeScope {
    ::LayoutContext* lycon;
    RunMode saved_run_mode;

    LayoutRunModeScope(::LayoutContext* l, RunMode mode);
    ~LayoutRunModeScope();

    LayoutRunModeScope(const LayoutRunModeScope&) = delete;
    LayoutRunModeScope& operator=(const LayoutRunModeScope&) = delete;
};

struct LayoutMeasureScope {
    ::LayoutContext* lycon;
    BlockContext saved_block;
    Linebox saved_line;
    FontBox saved_font;
    ::DomNode* saved_elmt;
    RunMode saved_run_mode;
    SizingMode saved_sizing_mode;
    AvailableSpace saved_available_space;

    LayoutMeasureScope(::LayoutContext* l, ::DomNode* measure_elmt);
    ~LayoutMeasureScope();

    LayoutMeasureScope(const LayoutMeasureScope&) = delete;
    LayoutMeasureScope& operator=(const LayoutMeasureScope&) = delete;
};

KnownDimensions layout_known_dimensions_from_block(::ViewBlock* block);
KnownDimensions layout_known_dimensions_from_context(::LayoutContext* lycon);

bool layout_pass_cache_get(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, SizeF* out_size, const char* label);

void layout_pass_cache_store(::LayoutContext* lycon, ::DomElement* element,
    KnownDimensions known_dimensions, SizeF result, const char* label);

} // namespace radiant
