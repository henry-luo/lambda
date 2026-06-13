#ifndef RADIANT_EDITING_DISPATCH_HPP
#define RADIANT_EDITING_DISPATCH_HPP

// shared editing event dispatch policy for form text controls and
// contenteditable/data-editable hosts.

#include "editing.hpp"
#include "editing_intent.hpp"

struct EventContext;
struct DocState;

typedef bool (*EditingDispatchInputEventFn)(EventContext* evcon, View* target,
                                            const char* type,
                                            const EditingIntent* intent,
                                            void* user);
typedef bool (*EditingDispatchLambdaEventFn)(EventContext* evcon, View* target,
                                             const char* type,
                                             const EditingIntent* intent,
                                             void* user);
typedef bool (*EditingCopySelectionFn)(DocState* state, const char* prefix,
                                       void* user);
typedef bool (*EditingTransactionMutateFn)(EventContext* evcon,
                                           DocState* state,
                                           const EditingSurface* surface,
                                           const EditingIntent* intent,
                                           void* user);

struct EditingDispatchHooks {
    EditingDispatchInputEventFn dispatch_input_event;
    EditingDispatchLambdaEventFn dispatch_lambda_event;
    EditingCopySelectionFn copy_selection;
    void* user;
};

struct EditingTransaction {
    const EditingSurface* surface;
    const EditingIntent* intent;
    const EditingDispatchHooks* hooks;
    EditingTransactionMutateFn mutate;
    void* mutate_user;
    const char* operation;
    bool dispatch_input_without_mutation;
    bool mutation_invalidates_layout;
    bool mutation_invalidates_paint;
};

bool editing_run_transaction(EventContext* evcon,
                             const EditingTransaction* tx,
                             bool* out_prevented,
                             bool* out_mutated,
                             bool* out_lambda_handled);

bool editing_dispatch_beforeinput(EventContext* evcon,
                                  const EditingSurface* surface,
                                  const EditingIntent* intent,
                                  const EditingDispatchHooks* hooks);

bool editing_dispatch_beforeinput_ex(EventContext* evcon,
                                     const EditingSurface* surface,
                                     const EditingIntent* intent,
                                     const EditingDispatchHooks* hooks,
                                     bool dispatch_input_after,
                                     bool* out_prevented,
                                     bool* out_lambda_handled);

void editing_dispatch_input(EventContext* evcon,
                            const EditingSurface* surface,
                            const EditingIntent* intent,
                            const EditingDispatchHooks* hooks);

void editing_dispatch_log_intent(EventContext* evcon,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent);

bool editing_dispatch_form_beforeinput(EventContext* evcon,
                                       const EditingSurface* surface,
                                       const EditingIntent* intent,
                                       const EditingDispatchHooks* hooks,
                                       bool* out_prevented);

void editing_dispatch_form_input(EventContext* evcon,
                                 const EditingSurface* surface,
                                 const EditingIntent* intent,
                                 const EditingDispatchHooks* hooks);

#endif // RADIANT_EDITING_DISPATCH_HPP
