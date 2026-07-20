#include "js_mir_internal.hpp"

// ============================================================================
// Completion-style MIR helpers
// ============================================================================

static const char* jm_suspend_kind_name(JsMirSuspendKind kind) {
    switch (kind) {
    case JS_MIR_SUSPEND_YIELD: return "yield";
    case JS_MIR_SUSPEND_AWAIT: return "await";
    case JS_MIR_SUSPEND_IMPLICIT_AWAIT: return "implicit await";
    }
    return "suspend";
}

int jm_next_resume_state(JsMirTranspiler* mt, JsMirSuspendKind kind) {
    if (!mt) return -1;
    int next_state = ++mt->gen_yield_index;
    if (next_state > mt->gen_yield_count || next_state >= 64 ||
        !mt->gen_state_labels[next_state]) {
        log_error("js-mir resume-state: %s index %d exceeds allocated labels (%d)",
            jm_suspend_kind_name(kind), next_state, mt->gen_yield_count);
        return -1;
    }
    return next_state;
}

void jm_emit_suspend_env_save(JsMirTranspiler* mt) {
    if (!mt || !mt->gen_env_reg) return;
    for (int sd = 1; sd <= mt->scope_depth; sd++) {
        if (!mt->var_scopes[sd]) continue;
        size_t iter = 0;
        void* item;
        while (hashmap_iter(mt->var_scopes[sd], &iter, &item)) {
            JsVarScopeEntry* entry = (JsVarScopeEntry*)item;
            if (!entry->var.from_env && entry->var.mir_type == MIR_T_I64) {
                if (mt->gen_local_slot_count >= mt->gen_dynamic_slot_limit) {
                    log_error("js-mir suspend env: dynamic binding slots exhausted before spill region");
                    continue;
                }
                // The name prepass cannot distinguish lexical shadows; give the
                // exact active binding its own suspend home before first yield.
                entry->var.from_env = true;
                entry->var.env_slot = mt->gen_local_slot_count++;
                entry->var.env_reg = mt->gen_env_reg;
            }
            if (entry->var.env_slot < 0 || !entry->var.from_env) continue;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    entry->var.env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1),
                MIR_new_reg_op(mt->ctx, entry->var.reg)));
        }
    }
}

void jm_emit_resume_env_restore(JsMirTranspiler* mt) {
    if (!mt || !mt->gen_env_reg) return;
    for (int sd = 1; sd <= mt->scope_depth; sd++) {
        if (!mt->var_scopes[sd]) continue;
        size_t iter = 0;
        void* item;
        while (hashmap_iter(mt->var_scopes[sd], &iter, &item)) {
            JsVarScopeEntry* entry = (JsVarScopeEntry*)item;
            if (entry->var.env_slot < 0 || !entry->var.from_env) continue;
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, entry->var.reg),
                MIR_new_mem_op(mt->ctx, MIR_T_I64,
                    entry->var.env_slot * (int)sizeof(uint64_t), mt->gen_env_reg, 0, 1)));
        }
    }
}

void jm_emit_try_state_reset(JsMirTranspiler* mt) {
    if (!mt) return;
    for (int td = 0; td < mt->try_ctx_depth; td++) {
        JsTryContext* context = &mt->try_ctx_stack[td];
        if (context->has_return_reg) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, context->has_return_reg),
                MIR_new_int_op(mt->ctx, 0)));
        }
        if (context->return_val_reg) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, context->return_val_reg),
                MIR_new_int_op(mt->ctx, 0)));
        }
        if (context->saved_exc_flag_reg) {
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, context->saved_exc_flag_reg),
                MIR_new_int_op(mt->ctx, 0)));
        }
        if (context->saved_exc_val_reg) {
            MIR_reg_t null_value = jm_emit_null(mt);
            jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
                MIR_new_reg_op(mt->ctx, context->saved_exc_val_reg),
                MIR_new_reg_op(mt->ctx, null_value)));
        }
    }
}

void jm_emit_async_resume_refresh(JsMirTranspiler* mt) {
    if (!mt) return;
    jm_scope_env_reload_vars(mt);
    jm_env_reload_shared_captures(mt);
}

JsTryContext* jm_find_completion_context(JsMirTranspiler* mt, JsMirCompletionKind kind) {
    if (!mt) return NULL;
    for (int depth = mt->try_ctx_depth - 1; depth >= 0; depth--) {
        JsTryContext* context = &mt->try_ctx_stack[depth];
        if (context->yield_state_only) continue;
        if (kind == JS_MIR_COMPLETION_GENERATOR_RETURN_SIGNAL && !context->has_finally) {
            continue;
        }
        return context;
    }
    return NULL;
}

static MIR_label_t jm_completion_target(JsTryContext* context, JsMirCompletionKind kind,
        bool include_end_label) {
    if (!context) return 0;
    switch (kind) {
    case JS_MIR_COMPLETION_AWAIT_REJECTION:
        return context->catch_label;
    case JS_MIR_COMPLETION_RETURN:
    case JS_MIR_COMPLETION_RETURN_THROUGH_CLEANUP:
        return context->has_finally ? context->finally_label : context->end_label;
    case JS_MIR_COMPLETION_GENERATOR_RETURN_SIGNAL:
        return context->finally_label;
    case JS_MIR_COMPLETION_THROW:
        if (context->has_catch) return context->catch_label;
        if (context->has_finally) return context->finally_label;
        return include_end_label ? context->end_label : 0;
    }
    return 0;
}

void jm_emit_pending_exception_check(JsMirTranspiler* mt, JsMirCompletionKind kind) {
    if (!mt) return;
    JsTryContext* context = jm_find_completion_context(mt, kind);
    MIR_label_t target = jm_completion_target(context, kind, false);
    if (!target && kind == JS_MIR_COMPLETION_THROW && !context) {
        if (!mt->func_except_label) mt->func_except_label = jm_new_label(mt);
        target = mt->func_except_label;
    }
    if (!target) return;
    // Consecutive checks with the same target observe identical exception
    // state: no intervening MIR instruction can set or clear the pending flag.
    if (mt->last_exception_poll_branch &&
        mt->last_exception_poll_target == target &&
        DLIST_TAIL(MIR_insn_t, mt->em.func->insns) == mt->last_exception_poll_branch) {
        return;
    }
    MIR_reg_t exception = jm_call_0(mt, "js_check_exception", MIR_T_I64);
    MIR_insn_t branch;
    if (mt->arg_stack_scope && mt->arg_stack_scope->mark) {
        MIR_label_t clean_path = jm_new_label(mt);
        branch = MIR_new_insn(mt->ctx, MIR_BF,
            MIR_new_label_op(mt->ctx, clean_path),
            MIR_new_reg_op(mt->ctx, exception));
        jm_emit(mt, branch);
        // An argument expression can throw before its owning call reaches the
        // normal scope epilogue. Unwind every active nested extent here so the
        // exceptional edge cannot retain half-built GC-visible argument frames.
        MIR_reg_t unwind_mark = 0;
        for (JsMirArgStackScope* scope = mt->arg_stack_scope;
                scope; scope = scope->parent) {
            if (scope->mark) unwind_mark = scope->mark;
        }
        // The oldest active mark precedes every nested buffer, so one reset
        // reclaims the full exceptional extent without redundant restores.
        jm_call_void_1(mt, "js_args_restore", MIR_T_I64,
            MIR_new_reg_op(mt->ctx, unwind_mark));
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, target)));
        jm_emit_label(mt, clean_path);
    } else {
        branch = MIR_new_insn(mt->ctx, MIR_BT,
            MIR_new_label_op(mt->ctx, target),
            MIR_new_reg_op(mt->ctx, exception));
        jm_emit(mt, branch);
    }
    mt->last_exception_poll_branch = branch;
    mt->last_exception_poll_target = target;
}

static MIR_reg_t jm_exception_poll_result(MIR_insn_t call) {
    if (!call || !MIR_call_code_p(call->code) || call->nops < 3 ||
        call->ops[2].mode != MIR_OP_REG) return 0;
    return call->ops[2].u.reg;
}

static bool jm_branch_uses_poll_result(MIR_context_t ctx, MIR_insn_t branch,
        MIR_reg_t result) {
    if (!branch || !result || !MIR_branch_code_p(branch->code)) return false;
    size_t count = MIR_insn_nops(ctx, branch);
    for (size_t i = 0; i < count; i++) {
        int output = 0;
        MIR_insn_op_mode(ctx, branch, i, &output);
        if (!output && branch->ops[i].mode == MIR_OP_REG &&
            branch->ops[i].u.reg == result) return true;
    }
    return false;
}

static void jm_merge_exception_predecessor(bool* next_clean, bool* seen,
        int successor, int block_count, bool edge_clean) {
    if (successor < 0 || successor >= block_count) return;
    if (!seen[successor]) {
        next_clean[successor] = edge_clean;
        seen[successor] = true;
    } else {
        next_clean[successor] = next_clean[successor] && edge_clean;
    }
}

void jm_optimize_exception_polls(JsMirTranspiler* mt) {
    if (!mt || !mt->em.func || !mt->em.func_item) return;
    bool has_exception_poll = false;
    for (int i = 0; i < mt->em.frame.gc_call_site_count; i++) {
        if (mt->em.frame.gc_call_sites[i].is_exception_poll) {
            has_exception_poll = true;
            break;
        }
    }
    // Poll-free functions have no rewrite candidate. Avoid constructing their
    // instruction CFG, which dominates compile time in large module batches.
    if (!has_exception_poll) return;
    int instruction_count = 0;
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, mt->em.func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) instruction_count++;
    if (instruction_count <= 0) return;

    MIR_insn_t* instructions = (MIR_insn_t*)mem_alloc(
        (size_t)instruction_count * sizeof(MIR_insn_t), MEM_CAT_TEMP);
    MirRootLivenessBlock* blocks = (MirRootLivenessBlock*)mem_alloc(
        (size_t)instruction_count * sizeof(MirRootLivenessBlock), MEM_CAT_TEMP);
    int* instruction_blocks = (int*)mem_alloc(
        (size_t)instruction_count * sizeof(int), MEM_CAT_TEMP);
    MIR_label_t* labels = (MIR_label_t*)mem_alloc(
        (size_t)instruction_count * sizeof(MIR_label_t), MEM_CAT_TEMP);
    int* label_blocks = (int*)mem_alloc(
        (size_t)instruction_count * sizeof(int), MEM_CAT_TEMP);
    if (!instructions || !blocks || !instruction_blocks || !labels ||
        !label_blocks) {
        log_error("js-mir exception dataflow: CFG allocation failed");
        abort();
    }
    int ii = 0;
    for (MIR_insn_t insn = DLIST_HEAD(MIR_insn_t, mt->em.func->insns);
            insn; insn = DLIST_NEXT(MIR_insn_t, insn)) instructions[ii++] = insn;
    const MirGcCallSite** instruction_sites =
        (const MirGcCallSite**)mem_calloc(
            (size_t)instruction_count, sizeof(MirGcCallSite*), MEM_CAT_TEMP);
    if (!instruction_sites) {
        log_error("js-mir exception dataflow: call-site index allocation failed");
        abort();
    }
    MIR_reg_t max_poll_result = 0;
    int call_site_index = 0;
    for (int i = 0; i < instruction_count; i++) {
        if (call_site_index >= mt->em.frame.gc_call_site_count ||
                mt->em.frame.gc_call_sites[call_site_index].insn !=
                    instructions[i]) continue;
        const MirGcCallSite* site =
            &mt->em.frame.gc_call_sites[call_site_index++];
        instruction_sites[i] = site;
        if (site->is_exception_poll) {
            MIR_reg_t result = jm_exception_poll_result(instructions[i]);
            if (result > max_poll_result) max_poll_result = result;
        }
    }
    // Call metadata is emitted in MIR order. Losing that correspondence would
    // make the exception proof silently optimistic, so fail before rewriting.
    if (call_site_index != mt->em.frame.gc_call_site_count) {
        log_error("js-mir exception dataflow: indexed %d/%d call sites",
            call_site_index, mt->em.frame.gc_call_site_count);
        abort();
    }
    int* poll_result_uses = max_poll_result
        ? (int*)mem_calloc((size_t)max_poll_result + 1,
            sizeof(int), MEM_CAT_TEMP) : NULL;
    if (max_poll_result && !poll_result_uses) {
        log_error("js-mir exception dataflow: poll-use index allocation failed");
        abort();
    }
    for (int i = 0; i < instruction_count; i++) {
        size_t operand_count = MIR_insn_nops(mt->ctx, instructions[i]);
        for (size_t oi = 0; oi < operand_count; oi++) {
            int output = 0;
            MIR_insn_op_mode(mt->ctx, instructions[i], oi, &output);
            MIR_op_t op = instructions[i]->ops[oi];
            if (poll_result_uses && !output && op.mode == MIR_OP_REG &&
                    op.u.reg <= max_poll_result) {
                poll_result_uses[op.u.reg]++;
            }
        }
    }
    int block_count = 0;
    int block_start = 0;
    for (int i = 0; i < instruction_count; i++) {
        bool starts = i == 0 || instructions[i]->code == MIR_LABEL ||
            em_root_block_terminates(instructions[i - 1]);
        if (starts && i > block_start) {
            blocks[block_count++] = {block_start, i};
            block_start = i;
        }
    }
    if (block_start < instruction_count) {
        blocks[block_count++] = {block_start, instruction_count};
    }
    for (int bi = 0; bi < block_count; bi++) {
        for (int i = blocks[bi].start; i < blocks[bi].end; i++) {
            instruction_blocks[i] = bi;
        }
    }
    JitExceptionEffect* block_effects = (JitExceptionEffect*)mem_alloc(
        (size_t)block_count * sizeof(JitExceptionEffect), MEM_CAT_TEMP);
    JitExceptionEffect* poll_effects = (JitExceptionEffect*)mem_alloc(
        (size_t)block_count * sizeof(JitExceptionEffect), MEM_CAT_TEMP);
    MIR_insn_t* poll_branches = (MIR_insn_t*)mem_calloc(
        (size_t)block_count, sizeof(MIR_insn_t), MEM_CAT_TEMP);
    if (!block_effects || !poll_effects || !poll_branches) {
        log_error("js-mir exception dataflow: transfer allocation failed");
        abort();
    }
    for (int bi = 0; bi < block_count; bi++) {
        JitExceptionEffect effect = JIT_EXCEPTION_PRESERVES;
        poll_effects[bi] = JIT_EXCEPTION_PRESERVES;
        for (int i = blocks[bi].start; i < blocks[bi].end; i++) {
            const MirGcCallSite* site = instruction_sites[i];
            if (!site) continue;
            if (site->is_exception_poll) {
                MIR_reg_t result = jm_exception_poll_result(instructions[i]);
                if (i + 1 == blocks[bi].end - 1 &&
                    jm_branch_uses_poll_result(
                        mt->ctx, instructions[i + 1], result)) {
                    poll_branches[bi] = instructions[i + 1];
                    poll_effects[bi] = effect;
                }
            } else if (site->exception_effect != JIT_EXCEPTION_PRESERVES) {
                effect = site->exception_effect;
            }
        }
        // A block's last non-preserving call fully determines its transfer;
        // caching it keeps CFG convergence linear in edges, not MIR volume.
        block_effects[bi] = effect;
    }
    int label_count = 0;
    for (int i = 0; i < instruction_count; i++) {
        if (instructions[i]->code == MIR_LABEL) {
            labels[label_count] = instructions[i];
            label_blocks[label_count++] = instruction_blocks[i];
        }
    }
    int successor_words = (block_count + 63) / 64;
    size_t successor_count = (size_t)block_count * (size_t)successor_words;
    uint64_t* successors = (uint64_t*)mem_alloc(
        successor_count * sizeof(uint64_t), MEM_CAT_TEMP);
    bool* entry_clean = (bool*)mem_alloc((size_t)block_count, MEM_CAT_TEMP);
    bool* next_clean = (bool*)mem_alloc((size_t)block_count, MEM_CAT_TEMP);
    bool* seen = (bool*)mem_alloc((size_t)block_count, MEM_CAT_TEMP);
    if (!successors || !entry_clean || !next_clean || !seen) {
        log_error("js-mir exception dataflow: state allocation failed");
        abort();
    }
    memset(successors, 0, successor_count * sizeof(uint64_t));
    for (int bi = 0; bi < block_count; bi++) {
        em_root_collect_block_successors(bi, blocks, block_count, instructions,
            labels, label_blocks, label_count, successors, successor_words);
        entry_clean[bi] = true;
    }
    int* successor_offsets = (int*)mem_alloc(
        (size_t)(block_count + 1) * sizeof(int), MEM_CAT_TEMP);
    if (!successor_offsets) {
        log_error("js-mir exception dataflow: successor offset allocation failed");
        abort();
    }
    int edge_count = 0;
    for (int bi = 0; bi < block_count; bi++) {
        successor_offsets[bi] = edge_count;
        for (int word = 0; word < successor_words; word++) {
            uint64_t pending = successors[(size_t)bi *
                (size_t)successor_words + (size_t)word];
            while (pending) {
                em_root_take_lowest_set_bit(&pending);
                edge_count++;
            }
        }
    }
    successor_offsets[block_count] = edge_count;
    int* successor_edges = edge_count > 0
        ? (int*)mem_alloc((size_t)edge_count * sizeof(int), MEM_CAT_TEMP)
        : NULL;
    if (edge_count > 0 && !successor_edges) {
        log_error("js-mir exception dataflow: successor index allocation failed");
        abort();
    }
    int edge_index = 0;
    for (int bi = 0; bi < block_count; bi++) {
        for (int word = 0; word < successor_words; word++) {
            uint64_t pending = successors[(size_t)bi *
                (size_t)successor_words + (size_t)word];
            while (pending) {
                int bit = em_root_take_lowest_set_bit(&pending);
                successor_edges[edge_index++] = word * 64 + bit;
            }
        }
    }
    mem_free(successors);
    successors = NULL;
    entry_clean[0] = false;

    bool changed = true;
    while (changed) {
        changed = false;
        memset(next_clean, 1, (size_t)block_count * sizeof(bool));
        memset(seen, 0, (size_t)block_count * sizeof(bool));
        next_clean[0] = false;
        seen[0] = true;
        for (int bi = 0; bi < block_count; bi++) {
            MIR_insn_t poll_branch = poll_branches[bi];
            bool poll_before_clean = poll_effects[bi] == JIT_EXCEPTION_PRESERVES
                ? entry_clean[bi]
                : poll_effects[bi] == JIT_EXCEPTION_CLEARS;
            bool out_clean = block_effects[bi] == JIT_EXCEPTION_PRESERVES
                ? entry_clean[bi]
                : block_effects[bi] == JIT_EXCEPTION_CLEARS;
            int poll_target = -1;
            int fallthrough = bi + 1;
            if (poll_branch && poll_branch->nops > 0 &&
                poll_branch->ops[0].mode == MIR_OP_LABEL) {
                poll_target = em_root_find_label_block(
                    poll_branch->ops[0].u.label, labels, label_blocks, label_count);
            }
            for (int edge = successor_offsets[bi];
                    edge < successor_offsets[bi + 1]; edge++) {
                int successor = successor_edges[edge];
                bool edge_clean = out_clean;
                bool reachable = true;
                if (poll_branch && (poll_branch->code == MIR_BT ||
                    poll_branch->code == MIR_BF)) {
                    bool target_is_clean = poll_branch->code == MIR_BF;
                    if (poll_before_clean) {
                        reachable = target_is_clean
                            ? successor == poll_target
                            : successor == fallthrough;
                        edge_clean = true;
                    } else if (successor == poll_target) {
                        edge_clean = target_is_clean;
                    } else if (successor == fallthrough) {
                        edge_clean = !target_is_clean;
                    }
                }
                if (reachable) jm_merge_exception_predecessor(next_clean,
                    seen, successor, block_count, edge_clean);
            }
        }
        for (int bi = 0; bi < block_count; bi++) {
            bool value = seen[bi] ? next_clean[bi] : false;
            if (bi == 0) value = false;
            if (entry_clean[bi] != value) {
                entry_clean[bi] = value;
                changed = true;
            }
        }
    }

    // The fixed point proves the pending flag false on all incoming paths.
    // Remove only status reads whose result has no use beyond its adjacent
    // branch; otherwise retain a zero materialization for downstream users.
    for (int bi = 0; bi < block_count; bi++) {
        bool clean = entry_clean[bi];
        for (int i = blocks[bi].start; i < blocks[bi].end; i++) {
            MIR_insn_t insn = instructions[i];
            const MirGcCallSite* site = instruction_sites[i];
            if (!site) continue;
            if (site->is_exception_poll) {
                MIR_reg_t result = jm_exception_poll_result(insn);
                MIR_insn_t branch = i + 1 < blocks[bi].end
                    ? instructions[i + 1] : NULL;
                if (clean && result) {
                    bool adjacent = jm_branch_uses_poll_result(
                        mt->ctx, branch, result);
                    int uses = result <= max_poll_result
                        ? poll_result_uses[result] : 0;
                    if (adjacent && uses == 1 && branch->code == MIR_BT) {
                        MIR_remove_insn(mt->ctx, mt->em.func_item, branch);
                        MIR_remove_insn(mt->ctx, mt->em.func_item, insn);
                    } else if (adjacent && uses == 1 && branch->code == MIR_BF) {
                        MIR_insn_t jump = MIR_new_insn(mt->ctx, MIR_JMP,
                            branch->ops[0]);
                        MIR_insert_insn_before(mt->ctx, mt->em.func_item,
                            branch, jump);
                        MIR_remove_insn(mt->ctx, mt->em.func_item, branch);
                        MIR_remove_insn(mt->ctx, mt->em.func_item, insn);
                    } else {
                        MIR_insn_t zero = MIR_new_insn(mt->ctx, MIR_MOV,
                            MIR_new_reg_op(mt->ctx, result),
                            MIR_new_int_op(mt->ctx, 0));
                        MIR_insert_insn_before(mt->ctx, mt->em.func_item,
                            insn, zero);
                        MIR_remove_insn(mt->ctx, mt->em.func_item, insn);
                    }
                }
                continue;
            }
            if (site->exception_effect == JIT_EXCEPTION_CLEARS) clean = true;
            else if (site->exception_effect != JIT_EXCEPTION_PRESERVES) clean = false;
        }
    }

    mem_free(seen);
    mem_free(next_clean);
    mem_free(entry_clean);
    mem_free(successor_edges);
    mem_free(successor_offsets);
    mem_free(poll_result_uses);
    mem_free(instruction_sites);
    mem_free(poll_branches);
    mem_free(poll_effects);
    mem_free(block_effects);
    mem_free(label_blocks);
    mem_free(labels);
    mem_free(instruction_blocks);
    mem_free(blocks);
    mem_free(instructions);
}

bool jm_emit_delayed_return_completion(JsMirTranspiler* mt, MIR_reg_t value,
        JsMirCompletionKind kind) {
    if (!mt || (kind != JS_MIR_COMPLETION_RETURN &&
        kind != JS_MIR_COMPLETION_RETURN_THROUGH_CLEANUP &&
        kind != JS_MIR_COMPLETION_GENERATOR_RETURN_SIGNAL)) {
        return false;
    }
    JsTryContext* context = jm_find_completion_context(mt, kind);
    if (!context || !context->return_val_reg || !context->has_return_reg) return false;
    if (kind == JS_MIR_COMPLETION_RETURN && mt->in_generator && !context->has_finally) {
        return false;
    }
    MIR_label_t target = jm_completion_target(context, kind, true);
    if (!target) return false;
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, context->return_val_reg),
        MIR_new_reg_op(mt->ctx, value)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_MOV,
        MIR_new_reg_op(mt->ctx, context->has_return_reg),
        MIR_new_int_op(mt->ctx, 1)));
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
        MIR_new_label_op(mt->ctx, target)));
    return true;
}

MIR_reg_t jm_native_return_reg(JsMirTranspiler* mt, MIR_reg_t value) {
    if (!mt || !mt->in_native_func || !mt->current_fc) return value;
    if (mt->current_fc->return_type != LMD_TYPE_FLOAT) return value;
    MIR_type_t value_type = MIR_reg_type(mt->ctx, value, mt->em.func);
    if (value_type == MIR_T_D) return value;
    // Delayed completions use boxed I64 slots, so native float returns must unbox here.
    return jm_emit_unbox_float(mt, value);
}

void jm_emit_throw_completion(JsMirTranspiler* mt, MIR_reg_t value) {
    if (!mt) return;
    jm_call_void_1(mt, "js_throw_value", MIR_T_I64, MIR_new_reg_op(mt->ctx, value));
    JsTryContext* context = jm_find_completion_context(mt, JS_MIR_COMPLETION_THROW);
    MIR_label_t target = jm_completion_target(context, JS_MIR_COMPLETION_THROW, true);
    if (target) {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, target)));
        return;
    }
    MIR_reg_t null_value = jm_emit_null(mt);
    MIR_reg_t native_value = jm_native_return_reg(mt, null_value);
    jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, native_value)));
}

void jm_emit_pending_exception_exit(JsMirTranspiler* mt) {
    if (!mt) return;
    MIR_reg_t pending = jm_call_0(mt, "js_check_exception", MIR_T_I64);
    MIR_label_t no_exception = jm_new_label(mt);
    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_BF,
        MIR_new_label_op(mt->ctx, no_exception), MIR_new_reg_op(mt->ctx, pending)));
    JsTryContext* context = jm_find_completion_context(mt, JS_MIR_COMPLETION_THROW);
    MIR_label_t target = jm_completion_target(context, JS_MIR_COMPLETION_THROW, true);
    if (target) {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, MIR_new_label_op(mt->ctx, target)));
    } else {
        MIR_reg_t null_value = jm_emit_null(mt);
        MIR_reg_t native_value = jm_native_return_reg(mt, null_value);
        jm_emit(mt, MIR_new_ret_insn(mt->ctx, 1, MIR_new_reg_op(mt->ctx, native_value)));
    }
    jm_emit_label(mt, no_exception);
}

void jm_emit_abrupt_jump_cleanup(JsMirTranspiler* mt) {
    for (int t = mt->try_ctx_depth - 1; t >= 0; t--) {
        JsTryContext* tc = &mt->try_ctx_stack[t];
        if (tc->has_finally && tc->finally_body && !tc->inlining_finally &&
            tc->finally_body->node_type == JS_AST_NODE_BLOCK_STATEMENT) {
            tc->inlining_finally = true;
            JsBlockNode* fin = (JsBlockNode*)tc->finally_body;
            JsAstNode* fs = fin->statements;
            while (fs) {
                jm_transpile_statement(mt, fs);
                fs = fs->next;
            }
            tc->inlining_finally = false;
        }
    }

    for (int w = 0; w < mt->with_depth; w++) {
        jm_call_void_0(mt, "js_with_pop");
    }
}



static void jm_emit_close_intervening_iterators(JsMirTranspiler* mt, int target_index) {
    for (int i = mt->loop_depth - 1; i > target_index; i--) {
        if (mt->loop_stack[i].iterator_to_close) {
            jm_emit_iterator_close(mt, mt->loop_stack[i].iterator_to_close);
        }
    }
}

void jm_emit_break_completion(JsMirTranspiler* mt, JsBreakContinueNode* brk) {
    jm_emit_abrupt_jump_cleanup(mt);
    if (brk->label && brk->label_len > 0) {
        for (int i = mt->loop_depth - 1; i >= 0; i--) {
            if (mt->loop_stack[i].label_name &&
                mt->loop_stack[i].label_name_len == brk->label_len &&
                memcmp(mt->loop_stack[i].label_name, brk->label, brk->label_len) == 0) {
                jm_emit_close_intervening_iterators(mt, i);
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, mt->loop_stack[i].break_label)));
                break;
            }
        }
    } else if (mt->loop_depth > 0) {
        jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
            MIR_new_label_op(mt->ctx, mt->loop_stack[mt->loop_depth - 1].break_label)));
    }
}

void jm_emit_continue_completion(JsMirTranspiler* mt, JsBreakContinueNode* cont) {
    jm_emit_abrupt_jump_cleanup(mt);
    if (cont->label && cont->label_len > 0) {
        for (int i = mt->loop_depth - 1; i >= 0; i--) {
            if (mt->loop_stack[i].label_name &&
                mt->loop_stack[i].label_name_len == cont->label_len &&
                memcmp(mt->loop_stack[i].label_name, cont->label, cont->label_len) == 0) {
                if (mt->loop_stack[i].continue_label) {
                    jm_emit_close_intervening_iterators(mt, i);
                    jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                        MIR_new_label_op(mt->ctx, mt->loop_stack[i].continue_label)));
                }
                break;
            }
        }
    } else if (mt->loop_depth > 0) {
        for (int i = mt->loop_depth - 1; i >= 0; i--) {
            if (mt->loop_stack[i].continue_label) {
                jm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP,
                    MIR_new_label_op(mt->ctx, mt->loop_stack[i].continue_label)));
                break;
            }
        }
    }
}
