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

/* Begin one event cascade. `cause` follows the design vocabulary:
 * input, webdriver, event_sim, navigation, timer, script, internal, layout.
 * Returns 0 when logging is disabled; callers may still call end safely.
 */
uint64_t state_begin_event_cascade(RadiantState* state,
                                   EventStateLog* log,
                                   const char* cause);

/* Settle state, validate interaction invariants, emit state.validated or
 * state.invalid plus a compact state.snapshot.
 */
bool radiant_state_settle(RadiantState* state,
                          EventStateLog* log,
                          uint64_t cascade_id);

/* End one event cascade. Calls radiant_state_settle() before emitting
 * cascade.end, making the boundary the single consistency checkpoint.
 */
void state_end_event_cascade(RadiantState* state,
                             EventStateLog* log,
                             uint64_t cascade_id);

/* Validate interaction invariants without emitting log records. */
bool radiant_state_validate_interaction(RadiantState* state,
                                        StateValidationReport* report);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RADIANT_STATE_MACHINE_HPP */
