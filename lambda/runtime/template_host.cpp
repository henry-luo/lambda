#include "template_host.h"

#include "transpiler.hpp"
#include "runtime-state.h"
#include "heap_api.h"
#include "../input/input.hpp"
#include "../../lib/memtrack.h"
#include "../../lib/log.h"

extern __thread EvalContext* context;
extern __thread Context* input_context;

struct TemplateHostSession {
    Runtime runtime;
    Input* source_output;
    uint64_t model_root;
    bool root_registered;
    TemplateHostActivationHooks hooks;
    bool mounted_realm_active;
    LambdaSideStackRegion root_region;
    LambdaSideStackRegion number_region;
    unsigned binding_depth;
    bool close_pending;
};

static bool template_host_suspend(TemplateHostSession* session,
                                  EvalContext* owner, bool* was_active) {
    *was_active = false;
    return !session->hooks.suspend || session->hooks.suspend(owner, was_active);
}

static bool template_host_resume(TemplateHostSession* session,
                                 EvalContext* owner, bool was_active) {
    return !was_active || !session->hooks.resume ||
        session->hooks.resume(owner, was_active);
}

TemplateHostBinding::TemplateHostBinding(TemplateHostSession* session)
    : session_(session), saved_(context), saved_input_(input_context),
      target_(session ? runtime_get_eval_context(&session->runtime) : nullptr),
      switched_(false), saved_realm_active_(false),
      saved_root_region_(nullptr), saved_number_region_(nullptr), valid_(false) {
    if (!target_) return;
    if (saved_ != target_) {
        lambda_side_stack_regions_current(&saved_root_region_,
            &saved_number_region_);
        if (saved_ && !template_host_suspend(session, saved_,
                &saved_realm_active_)) return;
        if (saved_ && !eval_context_shutdown(saved_)) {
            template_host_resume(session, saved_, saved_realm_active_);
            return;
        }
        lambda_side_stack_regions_select(&session->root_region,
            &session->number_region);
        if (!eval_context_init(target_)) {
            lambda_side_stack_regions_select(saved_root_region_,
                saved_number_region_);
            if (saved_) {
                eval_context_init(saved_);
                template_host_resume(session, saved_, saved_realm_active_);
            }
            return;
        }
        if (!template_host_resume(session, target_,
                session->mounted_realm_active)) {
            eval_context_shutdown(target_);
            lambda_side_stack_regions_select(saved_root_region_,
                saved_number_region_);
            if (saved_) {
                eval_context_init(saved_);
                template_host_resume(session, saved_, saved_realm_active_);
            }
            return;
        }
        switched_ = true;
    }
    input_context = (Context*)target_;
    valid_ = true;
    session_->binding_depth++;
}

TemplateHostBinding::~TemplateHostBinding() {
    if (!valid_) return;
    input_context = saved_input_;
    if (switched_) {
        bool active = false;
        template_host_suspend(session_, target_, &active);
        eval_context_shutdown(target_);
        lambda_side_stack_regions_select(saved_root_region_,
            saved_number_region_);
        if (saved_) {
            eval_context_init(saved_);
            template_host_resume(session_, saved_, saved_realm_active_);
        }
    }
    session_->binding_depth--;
    if (session_->binding_depth == 0 && session_->close_pending)
        template_host_session_close(session_);
}

TemplateHostSession* template_host_session_open(const char* source,
                                                const char* reference) {
    return template_host_session_open_with_hooks(source, reference, nullptr);
}

TemplateHostSession* template_host_session_open_with_hooks(const char* source,
        const char* reference, const TemplateHostActivationHooks* hooks) {
    if (!source || !reference) return nullptr;
    TemplateHostSession* session = (TemplateHostSession*)mem_calloc(1,
        sizeof(TemplateHostSession), MEM_CAT_SYSTEM);
    if (!session) return nullptr;
    if (hooks) session->hooks = *hooks;
    runtime_init(&session->runtime);
    EvalContext* saved = context;
    Context* saved_input = input_context;
    bool saved_realm_active = false;
    LambdaSideStackRegion* saved_root_region = nullptr;
    LambdaSideStackRegion* saved_number_region = nullptr;
    lambda_side_stack_regions_current(&saved_root_region, &saved_number_region);
    if (saved && !template_host_suspend(session, saved, &saved_realm_active)) {
        runtime_cleanup(&session->runtime);
        mem_free(session);
        return nullptr;
    }
    if (saved && !eval_context_shutdown(saved)) {
        template_host_resume(session, saved, saved_realm_active);
        runtime_cleanup(&session->runtime);
        mem_free(session);
        return nullptr;
    }
    lambda_side_stack_regions_select(&session->root_region,
        &session->number_region);
    session->source_output = run_script_mir(&session->runtime, source,
        (char*)reference, false);
    EvalContext* mounted = runtime_get_eval_context(&session->runtime);
    if (session->source_output && mounted &&
            get_type_id(session->source_output->root) == LMD_TYPE_ELEMENT) {
        session->model_root = session->source_output->root.item;
        session->root_registered = heap_try_register_gc_root_range(
            &session->model_root, 1);
    }
    if (context == mounted) {
        template_host_suspend(session, mounted, &session->mounted_realm_active);
        eval_context_shutdown(mounted);
    }
    lambda_side_stack_regions_select(saved_root_region, saved_number_region);
    input_context = saved_input;
    if (saved) {
        eval_context_init(saved);
        template_host_resume(session, saved, saved_realm_active);
    }
    if (!session->root_registered) {
        log_error("template-host-open: mount lacks a rooted template model result=%p mounted=%p type=%d",
            (void*)session->source_output, (void*)mounted,
            session->source_output ? (int)get_type_id(session->source_output->root) : -1);
        template_host_session_close(session);
        return nullptr;
    }
    return session;
}

uint64_t template_host_session_root_word(const TemplateHostSession* session) {
    return session ? session->model_root : 0;
}

void template_host_session_close(TemplateHostSession* session) {
    if (!session) return;
    // A callback may request close while this session still owns the active
    // side stack; the outermost binding performs destruction after restoring it.
    if (session->binding_depth) {
        session->close_pending = true;
        return;
    }
    EvalContext* saved = context;
    Context* saved_input = input_context;
    EvalContext* mounted = runtime_get_eval_context(&session->runtime);
    bool saved_realm_active = false;
    LambdaSideStackRegion* saved_root_region = nullptr;
    LambdaSideStackRegion* saved_number_region = nullptr;
    lambda_side_stack_regions_current(&saved_root_region, &saved_number_region);
    if (mounted && saved != mounted) {
        if (saved) {
            template_host_suspend(session, saved, &saved_realm_active);
            eval_context_shutdown(saved);
        }
        lambda_side_stack_regions_select(&session->root_region,
            &session->number_region);
        eval_context_init(mounted);
        template_host_resume(session, mounted, session->mounted_realm_active);
    }
    if (session->root_registered) {
        heap_unregister_gc_root_range(&session->model_root);
        session->root_registered = false;
    }
    runtime_cleanup(&session->runtime);
    if (session->source_output && session->source_output->pool) {
        pool_destroy(session->source_output->pool);
    }
    lambda_side_stack_regions_select(saved_root_region, saved_number_region);
    lambda_side_stack_region_release(&session->root_region);
    lambda_side_stack_region_release(&session->number_region);
    input_context = saved_input;
    if (saved && saved != mounted) {
        eval_context_init(saved);
        template_host_resume(session, saved, saved_realm_active);
    }
    mem_free(session);
}
