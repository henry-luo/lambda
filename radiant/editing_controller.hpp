#ifndef RADIANT_EDITING_CONTROLLER_HPP
#define RADIANT_EDITING_CONTROLLER_HPP

#include "event.hpp"
#include "editing.hpp"
#include "editing_intent.hpp"
#include "view.hpp"

struct DocState;
struct DomElement;
struct EventContext;
struct UiContext;

typedef void (*EditingSelectionSnapshotFn)(EventContext* evcon,
                                           DocState* state,
                                           View* focus_view,
                                           const char* operation,
                                           const EditingIntent* intent,
                                           void* userdata);

typedef bool (*EditingFormSelectionExtendFn)(EventContext* evcon,
                                             DomElement* elem,
                                             DocState* state,
                                             View* target,
                                             int anchor_offset,
                                             int focus_offset,
                                             const char* operation,
                                             void* userdata);

typedef void (*EditingAutoscrollLogFn)(DocState* state,
                                       const EditingSurface* surface,
                                       const char* operation,
                                       float dx,
                                       float dy,
                                       float velocity_x,
                                       float velocity_y,
                                       void* userdata);

typedef bool (*EditingHistoryDispatchFn)(EventContext* evcon,
                                         const EditingSurface* surface,
                                         InputIntentType input_type,
                                         void* userdata);

struct EditingControllerHooks {
    EditingSelectionSnapshotFn selection_snapshot;
    EditingFormSelectionExtendFn form_selection_extend;
    EditingAutoscrollLogFn autoscroll_log;
    EditingHistoryDispatchFn history_dispatch;
    void* user;
};

bool editing_controller_handle_rich_navigation(EventContext* evcon,
                                               DocState* state,
                                               const KeyEvent* key_event,
                                               const EditingControllerHooks* hooks);

bool editing_controller_dispatch_history(EventContext* evcon,
                                         const EditingSurface* surface,
                                         InputIntentType input_type,
                                         const EditingControllerHooks* hooks);

bool editing_controller_undo(EventContext* evcon,
                             const EditingSurface* surface,
                             const EditingControllerHooks* hooks);

bool editing_controller_redo(EventContext* evcon,
                             const EditingSurface* surface,
                             const EditingControllerHooks* hooks);

bool editing_undo(EventContext* evcon,
                  const EditingSurface* surface,
                  const EditingControllerHooks* hooks);

bool editing_redo(EventContext* evcon,
                  const EditingSurface* surface,
                  const EditingControllerHooks* hooks);

bool editing_controller_drag_autoscroll(EventContext* evcon,
                                        DocState* state,
                                        View* surface_target,
                                        float pointer_x,
                                        float pointer_y,
                                        const EditingControllerHooks* hooks);

void editing_controller_drag_autoscroll_stop(DocState* state,
                                             const EditingControllerHooks* hooks);

bool editing_controller_animation_active(DocState* state);

bool editing_controller_animation_tick(UiContext* uicon,
                                       double timestamp,
                                       const EditingControllerHooks* hooks);

#endif // RADIANT_EDITING_CONTROLLER_HPP
