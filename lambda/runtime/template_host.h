#pragma once

#include <stdint.h>
#include "side_stack.h"

struct EvalContext;
struct Context;
struct TemplateHostSession;

struct TemplateHostActivationHooks {
    // A host with another active language realm releases only its thread-local
    // binding. Persistent realm state and its exact roots remain owned there.
    bool (*suspend)(EvalContext* owner, bool* was_active);
    bool (*resume)(EvalContext* owner, bool was_active);
};

// A retained headless template owns its evaluator and model root. Hosts may
// enter it for a bounded render/dispatch phase, then return to their runtime.
TemplateHostSession* template_host_session_open(const char* source,
                                                const char* reference);
TemplateHostSession* template_host_session_open_with_hooks(const char* source,
    const char* reference, const TemplateHostActivationHooks* hooks);
uint64_t template_host_session_root_word(const TemplateHostSession* session);
void template_host_session_close(TemplateHostSession* session);

class TemplateHostBinding {
    TemplateHostSession* session_;
    EvalContext* saved_;
    Context* saved_input_;
    EvalContext* target_;
    bool switched_;
    bool saved_realm_active_;
    LambdaSideStackRegion* saved_root_region_;
    LambdaSideStackRegion* saved_number_region_;
    bool valid_;

public:
    explicit TemplateHostBinding(TemplateHostSession* session);
    ~TemplateHostBinding();
    bool valid() const { return valid_; }
    TemplateHostBinding(const TemplateHostBinding&) = delete;
    TemplateHostBinding& operator=(const TemplateHostBinding&) = delete;
};
