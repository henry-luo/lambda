#pragma once

#include <stdint.h>

struct UiContext;
struct DomDocument;
struct DocState;
struct DomNode;
union RdtEvent;

// Lambda owns event handling; Radiant delivers borrowed events through its host.
void handle_event(UiContext* ui, DomDocument* document, RdtEvent* event);
bool radiant_register_event_hooks();
bool lambda_event_dispatch_composition(UiContext* ui, RdtEvent* event);
bool radiant_editing_animation_active(DocState* state);
bool radiant_editing_animation_tick(UiContext* ui, double timestamp);
extern "C" bool radiant_editing_focused_caret_rect(UiContext* ui, float* x, float* y,
                                                   float* width, float* height);
extern "C" bool radiant_dispatch_event_sim_select_change(UiContext* ui, DomNode* target, int index);
extern "C" bool radiant_dispatch_editing_text_drag_drop(UiContext* ui, DomNode* source,
    uint32_t source_start, uint32_t source_end, DomNode* target, uint32_t target_start,
    uint32_t target_end, const char* payload, const char* html, bool move);
