/* Radiant interaction state-machine boundary — Phase 3.
 *
 * This module provides the event cascade boundary used by platform input,
 * event_sim, WebDriver, layout diagnostics, and future transition APIs.
 * It deliberately starts as a small validation/snapshot shell; focus,
 * selection, caret, IME, and form transitions plug into this boundary in
 * later phases.
 */

#ifndef RADIANT_STATE_MACHINE_HPP
#define RADIANT_STATE_MACHINE_HPP

#include <stdint.h>
#include <stdbool.h>
#include "state_store.hpp"
#include "event_state_log.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct StateValidationReport {
    bool ok;
    uint32_t failures;
    char message[256];
} StateValidationReport;

typedef enum FocusTransitionKind {
    FOCUS_TRANSITION_FOCUS_ELEMENT,
    FOCUS_TRANSITION_BLUR_CURRENT,
    FOCUS_TRANSITION_MOVE,
} FocusTransitionKind;

typedef struct FocusTransitionArgs {
    View* target;
    bool from_keyboard;
    View* root;
    bool forward;
} FocusTransitionArgs;

typedef enum CaretTransitionKind {
    CARET_TRANSITION_COLLAPSE_TO_BOUNDARY,
} CaretTransitionKind;

typedef struct CaretTransitionArgs {
    View* target;
    int offset;
} CaretTransitionArgs;

typedef enum SelectionTransitionKind {
    SELECTION_TRANSITION_START_POINTER_SELECTION,
    SELECTION_TRANSITION_END_POINTER_SELECTION,
    SELECTION_TRANSITION_EXTEND_TO_BOUNDARY,
    SELECTION_TRANSITION_EXTEND_TO_VIEW,
    SELECTION_TRANSITION_SET_BASE_AND_EXTENT,
    SELECTION_TRANSITION_SELECT_ALL,
    SELECTION_TRANSITION_COLLAPSE_TO_START,
    SELECTION_TRANSITION_COLLAPSE_TO_END,
    SELECTION_TRANSITION_CLEAR_SELECTION,
} SelectionTransitionKind;

typedef struct SelectionTransitionArgs {
    View* target;
    int anchor_offset;
    int focus_offset;
} SelectionTransitionArgs;

typedef enum HoverTransitionKind {
    HOVER_TRANSITION_SET_TARGET,
} HoverTransitionKind;

typedef struct HoverTransitionArgs {
    View* target;
} HoverTransitionArgs;

typedef enum ActiveTransitionKind {
    ACTIVE_TRANSITION_SET_TARGET,
} ActiveTransitionKind;

typedef struct ActiveTransitionArgs {
    View* target;
} ActiveTransitionArgs;

typedef enum DragTransitionKind {
    DRAG_TRANSITION_SET_STATE,
    DRAG_TRANSITION_BEGIN_DROP,
    DRAG_TRANSITION_UPDATE_DROP_MOTION,
    DRAG_TRANSITION_SET_DROP_ACTIVE,
    DRAG_TRANSITION_SET_DROP_TARGET,
    DRAG_TRANSITION_CLEAR_DROP,
} DragTransitionKind;

typedef struct DragTransitionArgs {
    View* target;
    View* source;
    View* drop_target;
    bool dragging;
    bool active;
    float x;
    float y;
    bool has_drop_range;
    DomBoundary drop_start;
    DomBoundary drop_end;
    const char* drag_data;
} DragTransitionArgs;

bool focus_transition(DocState* state,
                      FocusTransitionKind kind,
                      FocusTransitionArgs* args);

bool caret_transition(DocState* state,
                      CaretTransitionKind kind,
                      CaretTransitionArgs* args);

bool selection_transition(DocState* state,
                          SelectionTransitionKind kind,
                          SelectionTransitionArgs* args);

bool hover_transition(DocState* state,
                      HoverTransitionKind kind,
                      HoverTransitionArgs* args);

bool active_transition(DocState* state,
                       ActiveTransitionKind kind,
                       ActiveTransitionArgs* args);

bool drag_transition(DocState* state,
                     DragTransitionKind kind,
                     DragTransitionArgs* args);

/* Begin one event cascade. `cause` follows the design vocabulary:
 * input, webdriver, event_sim, navigation, timer, script, internal, layout.
 * Returns 0 when logging is disabled; callers may still call end safely.
 */
uint64_t state_begin_event_cascade(DocState* state,
                                   EventStateLog* log,
                                   const char* cause);

/* Settle state, validate interaction invariants, emit state.validated or
 * state.invalid plus a compact state.snapshot.
 */
bool radiant_state_settle(DocState* state,
                          EventStateLog* log,
                          uint64_t cascade_id);

/* End one event cascade. Calls radiant_state_settle() before emitting
 * cascade.end, making the boundary the single consistency checkpoint.
 */
void state_end_event_cascade(DocState* state,
                             EventStateLog* log,
                             uint64_t cascade_id);

/* Validate interaction invariants without emitting log records. */
bool radiant_state_validate_interaction(DocState* state,
                                        StateValidationReport* report);

/* Debug-only invariant assertion. Compiles to a no-op under NDEBUG. */
void radiant_state_assert_valid(DocState* state, const char* context);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RADIANT_STATE_MACHINE_HPP */
