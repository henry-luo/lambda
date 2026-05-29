#ifndef RADIANT_EDITING_TARGET_RANGE_HPP
#define RADIANT_EDITING_TARGET_RANGE_HPP

// shared StaticRange-style target range computation for editing InputEvents.
// rich hosts use DOM Selection boundaries. Text controls use a synthetic
// StaticRange-style boundary over the control element with UTF-16
// selectionStart/End offsets until E0 can promote form values to concrete DOM
// text nodes.

#include "dom_range.hpp"
#include "editing.hpp"
#include "editing_intent.hpp"

#include <stdint.h>

struct DocState;

struct EditingTargetRange {
    DomBoundary start;
    DomBoundary end;
};

uint32_t editing_compute_target_ranges(DocState* state,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       EditingTargetRange* out,
                                       uint32_t cap);

#endif // RADIANT_EDITING_TARGET_RANGE_HPP
