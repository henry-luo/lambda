#pragma once

#include "mir_emitter_shared.hpp"

struct MirLoopInvariant {
    MIR_insn_t definition;
    uint32_t writes;
    bool invariant;
    bool needed;
};

static inline bool em_loop_pure_call(MirEmitter* em, MIR_insn_t insn) {
    if (insn->code != MIR_CALL || insn->nops < 3 ||
            insn->ops[0].mode != MIR_OP_REF ||
            insn->ops[0].u.ref->item_type != MIR_proto_item ||
            insn->ops[0].u.ref->u.proto->nres != 1 ||
            insn->ops[1].mode != MIR_OP_REF) return false;
    const char* name = MIR_item_name(em->ctx, insn->ops[1].u.ref);
    JitImportMetadata effects = {};
    bool found = em->lookup_import_metadata
        ? em->lookup_import_metadata(name, &effects)
        : jit_import_get_metadata(name, &effects);
    return found && (effects.flags & JIT_IMPORT_PURE_SCALAR_CALL) &&
        effects.gc_effect == JIT_EFFECT_NO_GC && effects.reentry_effect == JIT_REENTRY_NO &&
        effects.exception_effect == JIT_EXCEPTION_PRESERVES &&
        effects.ret_class == JIT_VALUE_NON_GC_SCALAR &&
        (effects.flags & JIT_IMPORT_NUMBER_STACK_PRESERVES);
}

static inline bool em_loop_scalar_instruction(MIR_insn_t insn) {
    // memory, division, conversions with out-of-range machine behavior, and
    // pointer-producing calls never become speculative loop invariants.
    switch (insn->code) {
    case MIR_MOV: case MIR_DMOV: case MIR_FMOV:
    case MIR_ADD: case MIR_SUB: case MIR_MUL: case MIR_NEG:
    case MIR_AND: case MIR_OR: case MIR_XOR: case MIR_LSH: case MIR_RSH: case MIR_URSH:
    case MIR_DADD: case MIR_DSUB: case MIR_DMUL: case MIR_DNEG:
    case MIR_I2D: case MIR_UI2D: case MIR_F2D:
        return true;
    default: return false;
    }
}

// The structured loop's only external entry is the fallthrough before `first`.
// Move only the scalar dependency slice of audited pure calls. GC and alias
// writes elsewhere cannot invalidate these values; memory witnesses stay local.
static inline int em_hoist_loop_scalar_calls(MirEmitter* em,
        MIR_label_t first, MIR_label_t end) {
    if (!em || !first || !end) return 0;
    MIR_reg_t max_reg = 0;
    bool has_pure_call = false;
    for (MIR_insn_t insn = first; insn && insn != end;
            insn = DLIST_NEXT(MIR_insn_t, insn)) {
        has_pure_call |= em_loop_pure_call(em, insn);
        for (size_t i = 0; i < insn->nops; i++) {
            if (insn->ops[i].mode == MIR_OP_REG && insn->ops[i].u.reg > max_reg)
                max_reg = insn->ops[i].u.reg;
        }
    }
    if (!has_pure_call) return 0;
    MirLoopInvariant* facts = (MirLoopInvariant*)mem_calloc(
        (size_t)max_reg + 1, sizeof(MirLoopInvariant), MEM_CAT_TEMP);
    if (!facts) return 0;
    for (MIR_insn_t insn = first; insn && insn != end;
            insn = DLIST_NEXT(MIR_insn_t, insn)) {
        for (size_t i = 0; i < insn->nops; i++) {
            int output = 0;
            MIR_insn_op_mode(em->ctx, insn, i, &output);
            if (output && insn->ops[i].mode == MIR_OP_REG) {
                MirLoopInvariant* fact = &facts[insn->ops[i].u.reg];
                fact->writes++;
                fact->definition = insn;
            }
        }
    }
    // Entry arguments are initialized independently of control flow. Other
    // outside definitions may be conditional, so they are not guessed stable.
    for (MIR_reg_t reg = 1; reg <= max_reg; reg++) {
        if (facts[reg].writes == 0 && em_root_is_function_argument_reg(em, reg))
            facts[reg].invariant = true;
    }
    bool changed;
    do {
        changed = false;
        for (MIR_insn_t insn = first; insn && insn != end;
                insn = DLIST_NEXT(MIR_insn_t, insn)) {
            bool call = em_loop_pure_call(em, insn);
            if (!call && !em_loop_scalar_instruction(insn)) continue;
            MIR_reg_t result = 0;
            bool eligible = true;
            for (size_t i = call ? 2 : 0; i < insn->nops; i++) {
                int output = 0;
                MIR_insn_op_mode(em->ctx, insn, i, &output);
                MIR_op_t op = insn->ops[i];
                if (output) {
                    if (op.mode != MIR_OP_REG || result) { eligible = false; break; }
                    result = op.u.reg;
                    if (facts[result].writes != 1) { eligible = false; break; }
                } else if (op.mode == MIR_OP_REG) {
                    if (!facts[op.u.reg].invariant) { eligible = false; break; }
                } else if (op.mode != MIR_OP_INT && op.mode != MIR_OP_UINT &&
                        op.mode != MIR_OP_DOUBLE && op.mode != MIR_OP_FLOAT) {
                    eligible = false;
                    break;
                }
            }
            if (!eligible || !result) continue;
            if (!facts[result].invariant) { facts[result].invariant = true; changed = true; }
            if (call) facts[result].needed = true;
        }
    } while (changed);
    // Backward closure selects dependencies without moving unrelated arithmetic.
    do {
        changed = false;
        for (MIR_reg_t reg = 1; reg <= max_reg; reg++) {
            MIR_insn_t insn = facts[reg].definition;
            if (!facts[reg].needed || !insn) continue;
            for (size_t i = 0; i < insn->nops; i++) {
                int output = 0;
                MIR_insn_op_mode(em->ctx, insn, i, &output);
                if (!output && insn->ops[i].mode == MIR_OP_REG) {
                    MirLoopInvariant* input = &facts[insn->ops[i].u.reg];
                    if (!input->needed) { input->needed = true; changed = true; }
                }
            }
        }
    } while (changed);
    int count = 0;
    for (MIR_insn_t insn = DLIST_NEXT(MIR_insn_t, first); insn && insn != end;) {
        MIR_insn_t next = DLIST_NEXT(MIR_insn_t, insn);
        for (size_t i = 0; i < insn->nops; i++) {
            int output = 0;
            MIR_insn_op_mode(em->ctx, insn, i, &output);
            if (output && insn->ops[i].mode == MIR_OP_REG && facts[insn->ops[i].u.reg].needed) {
                // MIR_remove_insn frees the instruction. Unlink without freeing
                // so relocation also preserves the emitter's call-record identity.
                DLIST_REMOVE(MIR_insn_t, em->func->insns, insn);
                MIR_insert_insn_before(em->ctx, em->func_item, first, insn);
                count += insn->code == MIR_CALL;
                break;
            }
        }
        insn = next;
    }
    mem_free(facts);
    return count;
}
