#include "mvp_internal.hpp"
#include "../lambda-number.hpp"
#include "../type_contract.hpp"
#include "../mir_loop_analysis.hpp"
#include "../mir_bitwise.hpp"
#include "../mir_container_access.hpp"
#include "../mir_path.hpp"
#include "../mir_function_value.hpp"
#include "../../io/mark_builder.hpp"
#include "../../../lib/math_checked.hpp"

static MIR_op_t reg(MvpCompiler* c, MIR_reg_t r) { return MIR_new_reg_op(c->em.ctx, r); }
static MIR_op_t imm(MvpCompiler* c, int64_t n) { return MIR_new_int_op(c->em.ctx, n); }
static MIR_label_t label(MvpCompiler* c) { return em_new_label(&c->em); }
static void mark(MvpCompiler* c, MIR_label_t l) { em_emit_label(&c->em, l); }
static void jump(MvpCompiler* c, MIR_label_t l) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_JMP, MIR_new_label_op(c->em.ctx, l)));
}
static void branch(MvpCompiler* c, MIR_insn_code_t op, MIR_label_t l, MIR_op_t a, MIR_op_t b) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, op, MIR_new_label_op(c->em.ctx, l), a, b));
}
static void move(MvpCompiler* c, MIR_reg_t to, MIR_op_t from, MIR_type_t type = MIR_T_I64) {
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, type == MIR_T_D ? MIR_DMOV : MIR_MOV, reg(c, to), from));
}
static MIR_reg_t operation(MvpCompiler* c, MIR_insn_code_t op, MIR_op_t a, MIR_op_t b,
        MIR_type_t type = MIR_T_I64) {
    MIR_reg_t r = em_new_reg(&c->em, "v", type);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, op, reg(c, r), a, b));
    return r;
}
static MIR_reg_t constant(MvpCompiler* c, int64_t n) {
    MIR_reg_t r = em_new_reg(&c->em, "constant", MIR_T_I64);
    move(c, r, imm(c, n));
    return r;
}
static MIR_reg_t float_constant(MvpCompiler* c, double number) {
    uint64_t bits; memcpy(&bits, &number, sizeof(bits));
    for (MvpFloatConstant* p = c->float_constants; p; p = p->next)
        if (p->bits == bits) return p->reg;
    MvpFloatConstant* p = (MvpFloatConstant*)mem_alloc(sizeof(*p), MEM_CAT_TEMP);
    if (!p) { mvp_reject(c->module, NULL, "float constant allocation failed"); return 0; }
    p->next = c->float_constants; p->bits = bits;
    p->reg = em_new_reg(&c->em, "float_constant", MIR_T_D);
    c->float_constants = p;
    // MIR materializes double immediates as loads; define each bit pattern once outside all loops.
    MIR_insn_t saved = c->em.insert_after;
    c->em.insert_after = c->constant_cursor ? c->constant_cursor : c->em.frame.anchor;
    move(c, p->reg, MIR_new_double_op(c->em.ctx, number), MIR_T_D);
    c->constant_cursor = c->em.insert_after; c->em.insert_after = saved;
    return p->reg;
}
static void root_value(void* owner, MIR_reg_t r, JitValueClass cls) {
    MvpCompiler* c = (MvpCompiler*)owner;
    MirFrameState* f = &c->em.frame;
    if (!em_root_note_candidate(&f->gc_candidates, &f->gc_candidate_count,
            &f->gc_candidate_capacity, &f->gc_candidate_by_reg,
            &f->gc_candidate_by_reg_capacity, r, cls, 0)) abort();
}

static MvpBinding* snapshot_bindings(LambdaMvpModule* module) {
    // a callable with no bindings still has a valid empty planning snapshot.
    size_t bytes = module->binding_count * sizeof(MvpBinding);
    MvpBinding* saved = (MvpBinding*)mem_alloc(bytes ? bytes : sizeof(MvpBinding), MEM_CAT_TEMP);
    if (saved && bytes) memcpy(saved, module->bindings, bytes);
    return saved;
}

static void restore_bindings(LambdaMvpModule* module, const MvpBinding* saved) {
    if (module->binding_count) memcpy(module->bindings, saved, module->binding_count * sizeof(MvpBinding));
}

static MirValue value(MvpCompiler* c, MIR_reg_t r, TypeId type, ValueRep rep,
        Type* contract = NULL, AstNode* source = NULL) {
    // error-bearing unions use the generic outcome consumers, not the
    // semantic category of the type-expression object that describes them.
    if (rep == VALUE_REP_ITEM && contract && lambda_type_accepts_error(contract) && type != LMD_TYPE_ERROR)
        type = LMD_TYPE_ANY;
    bool optional = false;
    Type* base = contract ? lambda_type_nullable_lane_base(contract, &optional) : NULL;
    if (optional && base && rep != VALUE_REP_ITEM) type = base->type_id;
    MirValue v = em_value_for_rep(r, type, rep, contract, source);
    if (rep == VALUE_REP_ITEM && contract && lambda_type_has_proven_error(contract)) {
        Type* success = lambda_type_remove_error(c->module->tp->pool, contract);
        // a validated outcome keeps its successful domain independently of
        // the error Item; consumers still test that alternative before unboxing.
        if (success && success != contract && success != &TYPE_ANY_NO_ERROR)
            v.admitted_success_contract = success;
    }
    if (v.value_class == JIT_VALUE_BOXED_ITEM || v.value_class == JIT_VALUE_RAW_GC_POINTER)
        root_value(c, r, v.value_class);
    return v;
}
static MirValue null_value(MvpCompiler* c) {
    return value(c, constant(c, ITEM_NULL), LMD_TYPE_NULL, VALUE_REP_ITEM, &TYPE_NULL);
}
static MIR_reg_t box_float(void* owner, MIR_reg_t r) {
    MvpCompiler* c = (MvpCompiler*)owner;
    int home = em_scalar_home_new(&c->em);
    return em_store_f64_home(&c->em, r,
        em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home)));
}
static MIR_reg_t unbox_float(void* owner, MIR_reg_t r) {
    MvpCompiler* c = (MvpCompiler*)owner;
    return em_call_1(&c->em, "it2d", MIR_T_D, MIR_T_I64, reg(c, r), true);
}
static bool nullable(MirValue v) {
    bool optional = false;
    lambda_type_nullable_lane_base(v.semantic_contract, &optional);
    return optional;
}
static MIR_reg_t null_float(MvpCompiler* c) {
    return float_constant(c, lambda_float_lane_to_double(FLOAT_LANE_NULL_BITS));
}
static MIR_reg_t number_null_flag(MvpCompiler* c, MirValue v) {
    if (!nullable(v)) return constant(c, 0);
    return v.rep == VALUE_REP_F64 ? em_float_lane_null_test(&c->em, v.reg) :
        operation(c, MIR_EQ, reg(c, v.reg), imm(c, INT_LANE_NULL));
}
static void branch_null(MvpCompiler* c, MirValue v, MIR_label_t target) {
    if (nullable(v)) branch(c, MIR_BNE, target, reg(c, number_null_flag(c, v)), imm(c, 0));
}
static MirValue convert(void* owner, MirValue v, ValueRep to) {
    MvpCompiler* c = (MvpCompiler*)owner;
    if (v.maybe_pending) v = em_materialize_pending_value(&c->em, v, MIR_PENDING_REASON_REP_CONVERSION);
    if (v.rep == to) return v;
    bool optional_pointer = false;
    Type* pointer_contract = lambda_type_nullable_lane_base(v.semantic_contract, &optional_pointer);
    TypeId pointer_type = pointer_contract ? pointer_contract->type_id : v.semantic_type;
    bool container_pointer = pointer_type == LMD_TYPE_MAP ||
        lambda_type_nonnull_map_contract(v.semantic_contract) || mvp_array_contract(pointer_contract) ||
        pointer_type == LMD_TYPE_FUNC || pointer_type == LMD_TYPE_TYPE || pointer_type == LMD_TYPE_PATH;
    MIR_reg_t r = 0;
    if (v.semantic_type == LMD_TYPE_NULL) {
        // null has a distinct sentinel in each native nullable lane (D2.4.3).
        r = to == VALUE_REP_F64 ? null_float(c) : constant(c,
            to == VALUE_REP_INT_LANE ? INT_LANE_NULL : to == VALUE_REP_I64 ? BOOL_ERROR :
            to == VALUE_REP_RAW_GC_POINTER ? 0 : ITEM_NULL);
    } else if (to == VALUE_REP_ITEM) {
        if (v.rep == VALUE_REP_INT_LANE) {
            if (mvp_int_range(c->module, (AstNode*)v.provenance_node).finite)
                return value(c, em_box_finite_int_item(&c->em, v.reg), v.semantic_type,
                    to, v.semantic_contract, (AstNode*)v.provenance_node);
            r = em_new_reg(&c->em, "int_item", MIR_T_I64);
            MIR_label_t slow = label(c), done = label(c);
            em_branch_int53_outside(&c->em, v.reg, slow);
            em_box_finite_int_item(&c->em, v.reg, r);
            jump(c, done); mark(c, slow);
            move(c, r, reg(c, em_call_1(&c->em, "int2it_lane", MIR_T_I64, MIR_T_I64, reg(c, v.reg), true)));
            mark(c, done);
        } else if (v.rep == VALUE_REP_F64) {
            if (!nullable(v)) r = em_box_f64_to_item(&c->em, c, box_float, v.reg);
            else {
                MIR_label_t absent = label(c), done = label(c);
                r = em_new_reg(&c->em, "float_item", MIR_T_I64);
                branch_null(c, v, absent);
                move(c, r, reg(c, em_box_f64_to_item(&c->em, c, box_float, v.reg)));
                jump(c, done); mark(c, absent); move(c, r, imm(c, ITEM_NULL)); mark(c, done);
            }
        } else if (v.rep == VALUE_REP_I64 && v.semantic_type == LMD_TYPE_BOOL) {
            r = operation(c, MIR_OR, reg(c, v.reg), imm(c, (uint64_t)LMD_TYPE_BOOL << 56));
            if (nullable(v)) {
                MIR_label_t done = label(c);
                branch(c, MIR_BNE, done, reg(c, v.reg), imm(c, BOOL_ERROR));
                move(c, r, imm(c, ITEM_NULL)); mark(c, done);
            }
        } else if (v.rep == VALUE_REP_RAW_GC_POINTER &&
                (container_pointer || pointer_type == LMD_TYPE_STRING || pointer_type == LMD_TYPE_DECIMAL ||
                 pointer_type == LMD_TYPE_SYMBOL || pointer_type == LMD_TYPE_PATH || pointer_type == LMD_TYPE_FUNC)) {
            r = container_pointer ? v.reg :
                operation(c, MIR_OR, reg(c, v.reg), imm(c, (uint64_t)pointer_type << 56));
            if (nullable(v)) {
                if (container_pointer) {
                    r = em_new_reg(&c->em, "nullable_container", MIR_T_I64);
                    move(c, r, reg(c, v.reg));
                }
                MIR_label_t present = label(c);
                branch(c, MIR_BNE, present, reg(c, v.reg), imm(c, 0));
                move(c, r, imm(c, ITEM_NULL)); mark(c, present);
            }
        } else if (v.rep == VALUE_REP_RAW_NON_GC_POINTER && pointer_type == LMD_TYPE_TYPE) {
            r = v.reg;
        }
    } else if (to == VALUE_REP_F64 && v.rep == VALUE_REP_INT_LANE) {
        r = em_new_reg(&c->em, "int_as_float", MIR_T_D);
        if (mvp_int_range(c->module, (AstNode*)v.provenance_node).finite) {
            em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, r), reg(c, v.reg)));
            return value(c, r, v.semantic_type, to, v.semantic_contract, (AstNode*)v.provenance_node);
        }
        MIR_label_t slow = label(c), done = label(c);
        em_branch_int53_outside(&c->em, v.reg, slow);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, r), reg(c, v.reg)));
        jump(c, done); mark(c, slow);
        move(c, r, reg(c, em_call_1(&c->em, "lambda_int_lane_to_double_c", MIR_T_D,
            MIR_T_I64, reg(c, v.reg), true)), MIR_T_D);
        mark(c, done);
    } else if (to == VALUE_REP_F64 && v.rep == VALUE_REP_ITEM) {
        if (!nullable(v)) r = em_unbox_f64_item(&c->em, c, unbox_float, v.reg);
        else {
            MIR_label_t absent = label(c), done = label(c);
            r = em_new_reg(&c->em, "item_float", MIR_T_D);
            branch(c, MIR_BEQ, absent, reg(c, v.reg), imm(c, ITEM_NULL));
            move(c, r, reg(c, em_unbox_f64_item(&c->em, c, unbox_float, v.reg)), MIR_T_D);
            jump(c, done); mark(c, absent); move(c, r, reg(c, null_float(c)), MIR_T_D); mark(c, done);
        }
    } else if (to == VALUE_REP_INT_LANE && v.rep == VALUE_REP_ITEM) {
        r = em_new_reg(&c->em, "item_int", MIR_T_I64);
        MIR_label_t slow = label(c), done = label(c);
        MIR_reg_t tag = operation(c, MIR_URSH, reg(c, v.reg), imm(c, 56));
        branch(c, MIR_BNE, slow, reg(c, tag), imm(c, LMD_TYPE_INT));
        em_unbox_finite_int_item(&c->em, v.reg, r);
        jump(c, done); mark(c, slow);
        move(c, r, reg(c, em_call_1(&c->em, "lambda_item_to_int_lane_c", MIR_T_I64,
            MIR_T_I64, reg(c, v.reg), true)));
        mark(c, done);
    } else if (to == VALUE_REP_INT_LANE && v.rep == VALUE_REP_F64) {
        r = em_double_to_int_lane(&c->em, v.reg, false, true);
    } else if (to == VALUE_REP_I64 && v.rep == VALUE_REP_ITEM && pointer_type == LMD_TYPE_BOOL) {
        r = operation(c, MIR_AND, reg(c, v.reg), imm(c, 1));
        if (nullable(v)) {
            MIR_label_t done = label(c);
            branch(c, MIR_BNE, done, reg(c, v.reg), imm(c, ITEM_NULL));
            move(c, r, imm(c, BOOL_ERROR)); mark(c, done);
        }
    } else if (to == VALUE_REP_RAW_GC_POINTER && v.rep == VALUE_REP_ITEM &&
            (container_pointer || pointer_type == LMD_TYPE_STRING || pointer_type == LMD_TYPE_DECIMAL ||
             pointer_type == LMD_TYPE_SYMBOL || pointer_type == LMD_TYPE_PATH || pointer_type == LMD_TYPE_FUNC)) {
        r = container_pointer && !nullable(v) ? v.reg :
            operation(c, MIR_AND, reg(c, v.reg), imm(c, 0x00ffffffffffffffULL));
    } else if (to == VALUE_REP_RAW_NON_GC_POINTER && v.rep == VALUE_REP_ITEM && pointer_type == LMD_TYPE_TYPE) {
        r = v.reg;
    }
    if (!r) {
        char reason[240];
        snprintf(reason, sizeof(reason), "unsupported representation conversion in %s: %s (%d) from %d to %d",
            c->function ? c->function->name : "module", get_type_name(v.semantic_type),
            v.semantic_contract ? v.semantic_contract->kind : -1, v.rep, to);
        mvp_reject(c->module, (AstNode*)v.provenance_node, reason);
        // finish a well-typed, unpublished MIR function so ordinary teardown can report the rejection.
        r = em_new_reg(&c->em, "rejected", em_mir_type_for_rep(to));
        move(c, r, to == VALUE_REP_F64 ? MIR_new_double_op(c->em.ctx, 0) : imm(c, 0), em_mir_type_for_rep(to));
    }
    return value(c, r, v.semantic_type, to, v.semantic_contract, (AstNode*)v.provenance_node);
}
static MirValue expression(MvpCompiler* c, AstNode* node, bool discard = false);
static MirValue literal(MvpCompiler* c, AstPrimaryNode* node);
static MirValue array_literal(MvpCompiler* c, AstArrayNode* node, Type* destination = NULL);
static MirValue map_literal(MvpCompiler* c, AstMapNode* node, TypeMap* destination = NULL);
static MirValue string_concat(MvpCompiler* c, AstNode* node, MvpBinding* owner = NULL,
    MvpBinding* replacement = NULL, AstNode* initializer = NULL);
static MirValue statements(MvpCompiler* c, AstNode* node, bool discard = false);
static void array_iteration(MvpCompiler* c, AstNode* body);
static MIR_label_t dense_call_loop(MvpCompiler* c, AstLoopControlNode* loop);
static MIR_label_t dense_array_loop(MvpCompiler* c, AstNode* loop);
static MIR_label_t strided_array_loop(MvpCompiler* c, AstLoopControlNode* loop);
static bool string_comparison_inputs(MvpCompiler* c, AstBinaryNode* node, MirValue* left, MirValue* right);
static void narrow_dense_locals(LambdaMvpModule* m, AstNodeId root, bool outcomes = false);
static MirValue direct_call(MvpCompiler* c, MvpFunction* f, AstNode* args,
    bool discard = false, MvpBinding* destination = NULL, bool propagate = false);
static void return_value(MvpCompiler* c, MirValue v, AstNode* node);
static MirValue propagated_value(MvpCompiler* c, MirValue v, AstNode* node);

static MIR_label_t array_sum_loop(MvpCompiler* c, AstLoopControlNode* node) {
    MvpArraySumPlan plan = {};
    if (!mvp_array_sum_plan(c->module, node, &plan) || !plan.array->array_native) return NULL;
    MIR_reg_t index = plan.counter->reg, sum = plan.sum->reg;
    MIR_reg_t limit = expression(c, plan.limit).reg;
    MIR_label_t generic = label(c), done = label(c), remainder = label(c), body = label(c);
    branch(c, MIR_BEQ, generic, reg(c, plan.array->array_native), imm(c, 0));
    branch(c, MIR_BNE, generic, reg(c, sum), imm(c, 0));
    branch(c, MIR_BLT, generic, reg(c, index), imm(c, 0));
    branch(c, MIR_UBGT, generic, reg(c, limit), imm(c, plan.max_count - plan.inclusive));
    if (plan.inclusive) limit = operation(c, MIR_ADD, reg(c, limit), imm(c, 1));
    branch(c, MIR_UBGT, generic, reg(c, limit), reg(c, plan.array->array_length));
    branch(c, MIR_BGE, done, reg(c, index), reg(c, limit));
    // the complete element domain and trip bound prove every partial/merged sum
    // finite. This allocation-free region can retain its buffer (D4.4.4v4, S4.1.2).
    MIR_reg_t items = em_load_at(&c->em, plan.array->reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "sum_items");
    // packed bool bytes are exactly 0/1, so counting truth is the same finite reduction.
    int width = mvp_array_element(plan.array->contract) == &TYPE_BOOL ? 1 : 8;
    MIR_type_t storage = width == 1 ? MIR_T_U8 : MIR_T_I64;
    constexpr int lanes = 4;
    MIR_reg_t partial[lanes] = {sum};
    for (int i = 1; i < lanes; i++) partial[i] = constant(c, 0);
    MIR_reg_t end = operation(c, MIR_SUB, reg(c, limit), imm(c, lanes - 1));
    branch(c, MIR_BGE, remainder, reg(c, index), reg(c, end)); mark(c, body);
    MIR_reg_t address = em_element_address(&c->em, items, index, width);
    for (int i = 0; i < lanes; i++) {
        MIR_reg_t term = em_load_at(&c->em, address, i * width, storage, "sum_term");
        move(c, partial[i], reg(c, operation(c, MIR_ADD, reg(c, partial[i]), reg(c, term))));
    }
    move(c, index, reg(c, operation(c, MIR_ADD, reg(c, index), imm(c, lanes))));
    branch(c, MIR_BLT, body, reg(c, index), reg(c, end)); mark(c, remainder);
    for (int i = 1; i < lanes; i++) move(c, sum, reg(c, operation(c, MIR_ADD, reg(c, sum), reg(c, partial[i]))));
    branch(c, MIR_BGE, done, reg(c, index), reg(c, limit));
    MIR_label_t tail = label(c); mark(c, tail);
    MIR_reg_t term = em_load_at(&c->em, em_element_address(&c->em, items, index, width), 0, storage, "sum_tail");
    move(c, sum, reg(c, operation(c, MIR_ADD, reg(c, sum), reg(c, term))));
    move(c, index, reg(c, operation(c, MIR_ADD, reg(c, index), imm(c, 1))));
    branch(c, MIR_BLT, tail, reg(c, index), reg(c, limit));
    jump(c, done); mark(c, generic);
    return done;
}

static MIR_label_t array_copy_loop(MvpCompiler* c, AstLoopControlNode* node) {
    MvpArrayCopyPlan plan = {};
    if (!mvp_array_copy_plan(c->module, node, &plan) || !plan.source->array_native ||
            !plan.destination->array_writable) return NULL;
    MIR_label_t checked = label(c), done = label(c), body = label(c);
    MIR_reg_t limit = expression(c, plan.limit).reg;
    branch(c, MIR_UBGT, checked, reg(c, limit), imm(c, INT53_MAX));
    branch(c, MIR_UBGT, checked, reg(c, plan.counter->reg), imm(c, INT53_MAX));
    branch(c, MIR_BGE, done, reg(c, plan.counter->reg), reg(c, limit));
    branch(c, MIR_BEQ, checked, reg(c, plan.source->array_native), imm(c, 0));
    branch(c, MIR_BEQ, checked, reg(c, plan.destination->array_writable), imm(c, 0));
    MIR_reg_t count = operation(c, MIR_SUB, reg(c, limit), reg(c, plan.counter->reg));
    MvpBinding* arrays[] = {plan.source, plan.destination};
    MvpBinding* indices[] = {plan.read_index, plan.write_index};
    MIR_reg_t ends[2], addresses[2];
    int64_t offsets[] = {plan.read_offset, plan.write_offset};
    int width = mvp_array_element(plan.source->contract) == &TYPE_BOOL ? 1 : 8;
    for (int i = 0; i < 2; i++) {
        branch(c, MIR_UBGT, checked, reg(c, indices[i]->reg), imm(c, INT53_MAX));
        ends[i] = operation(c, MIR_ADD, reg(c, indices[i]->reg), reg(c, count));
        branch(c, MIR_UBGT, checked, reg(c, ends[i]), imm(c, INT53_MAX));
        MIR_reg_t first = indices[i]->reg, last = ends[i];
        if (offsets[i]) {
            first = operation(c, MIR_ADD, reg(c, first), imm(c, offsets[i]));
            last = operation(c, MIR_ADD, reg(c, last), imm(c, offsets[i]));
            // base counters stay finite independently of the shifted access;
            // unsigned guards reject negative/overflowing endpoints before effects.
            branch(c, MIR_UBGT, checked, reg(c, first), imm(c, INT53_MAX));
            branch(c, MIR_UBGT, checked, reg(c, last), imm(c, INT53_MAX));
        }
        branch(c, MIR_UBGT, checked, reg(c, last), reg(c, arrays[i]->array_length));
        MIR_reg_t items = em_load_at(&c->em, arrays[i]->reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "copy_items");
        addresses[i] = em_element_address(&c->em, items, first, width);
    }
    MIR_reg_t end = operation(c, MIR_ADD, reg(c, addresses[0]),
        reg(c, operation(c, MIR_MUL, reg(c, count), imm(c, width))));
    // preserve forward source order even when both indices address the same owner.
    mark(c, body);
    MIR_type_t lane = width == 1 ? MIR_T_U8 : MIR_T_I64;
    MIR_reg_t item = em_load_at(&c->em, addresses[0], 0, lane, "copy_item");
    em_store_at(&c->em, addresses[1], 0, lane, item);
    for (int i = 0; i < 2; i++) move(c, addresses[i], reg(c, operation(c, MIR_ADD, reg(c, addresses[i]), imm(c, width))));
    branch(c, MIR_UBLT, body, reg(c, addresses[0]), reg(c, end));
    move(c, plan.read_index->reg, reg(c, ends[0]));
    move(c, plan.write_index->reg, reg(c, ends[1]));
    jump(c, done); mark(c, checked);
    return done;
}

static MIR_label_t array_reverse_loop(MvpCompiler* c, AstLoopControlNode* node) {
    MvpArrayReversePlan plan = {};
    if (c->dense_arrays || !mvp_array_reverse_plan(c->module, node, &plan) || !plan.array->array_writable) return NULL;
    MIR_label_t checked = label(c), done = label(c), body = label(c);
    em_branch_int53_outside(&c->em, plan.first->reg, checked);
    em_branch_int53_outside(&c->em, plan.last->reg, checked);
    branch(c, MIR_BGE, done, reg(c, plan.first->reg), reg(c, plan.last->reg));
    branch(c, MIR_BEQ, checked, reg(c, plan.array->array_writable), imm(c, 0));
    branch(c, MIR_UBGE, checked, reg(c, plan.first->reg), reg(c, plan.array->array_length));
    branch(c, MIR_UBGE, checked, reg(c, plan.last->reg), reg(c, plan.array->array_length));
    MIR_reg_t items = em_load_at(&c->em, plan.array->reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "reverse_items");
    int width = mvp_array_element(plan.array->contract) == &TYPE_BOOL ? 1 : 8;
    MIR_type_t lane = width == 1 ? MIR_T_U8 : MIR_T_I64;
    // both indices move inward, so one complete-range guard covers every swap.
    mark(c, body);
    MIR_reg_t left = em_element_address(&c->em, items, plan.first->reg, width);
    MIR_reg_t right = em_element_address(&c->em, items, plan.last->reg, width);
    MIR_reg_t first = em_load_at(&c->em, left, 0, lane, "reverse_first");
    MIR_reg_t last = em_load_at(&c->em, right, 0, lane, "reverse_last");
    em_store_at(&c->em, left, 0, lane, last); em_store_at(&c->em, right, 0, lane, first);
    move(c, plan.first->reg, reg(c, operation(c, MIR_ADD, reg(c, plan.first->reg), imm(c, 1))));
    move(c, plan.last->reg, reg(c, operation(c, MIR_SUB, reg(c, plan.last->reg), imm(c, 1))));
    branch(c, MIR_BLT, body, reg(c, plan.first->reg), reg(c, plan.last->reg));
    jump(c, done); mark(c, checked);
    return done;
}

static MIR_label_t countdown_loop(MvpCompiler* c, AstLoopControlNode* node) {
    MirFiniteDescendingSumPlan plan = {};
    if (!mir_descending_sum_pattern(c->module->tp->source, node, &plan, true)) return NULL;
    MvpBinding* n = mvp_binding(c->module, plan.counter);
    MvpBinding* sum = mvp_binding(c->module, plan.accumulator);
    if (!n || !sum || n->owner != c->function->id || sum->owner != c->function->id) return NULL;
    if (n->contract == &TYPE_FLOAT && sum->contract == &TYPE_FLOAT) {
        MIR_label_t generic = label(c), done = label(c);
        MIR_op_t zero = reg(c, float_constant(c, 0.0));
        MIR_reg_t positive = operation(c, MIR_DGT, reg(c, n->reg), zero);
        branch(c, MIR_BEQ, generic, reg(c, positive), imm(c, 0));
        branch(c, MIR_DBGT, generic, reg(c, n->reg), reg(c, float_constant(c, MIR_FINITE_DESCENDING_SUM_MAX)));
        branch(c, MIR_DBNE, generic, reg(c, sum->reg), zero);
        MIR_reg_t integer = em_new_reg(&c->em, "countdown_integer", MIR_T_I64);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_D2I, reg(c, integer), reg(c, n->reg)));
        MIR_reg_t roundtrip = em_new_reg(&c->em, "countdown_exact", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, roundtrip), reg(c, integer)));
        branch(c, MIR_DBNE, generic, reg(c, n->reg), reg(c, roundtrip));
        // S4.3: each source addition is exact below 2^53. The guarded integer
        // triangle has the same final value and leaves the counter at -1.
        MIR_reg_t next = operation(c, MIR_ADD, reg(c, integer), imm(c, 1));
        MIR_reg_t product = operation(c, MIR_MUL, reg(c, integer), reg(c, next));
        MIR_reg_t total = operation(c, MIR_RSH, reg(c, product), imm(c, 1));
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, sum->reg), reg(c, total)));
        move(c, n->reg, reg(c, float_constant(c, -1.0)), MIR_T_D);
        jump(c, done); mark(c, generic);
        return done;
    }
    if (n->rep != VALUE_REP_INT_LANE || sum->rep != VALUE_REP_INT_LANE) return NULL;
    MIR_label_t generic = label(c), body = label(c), done = label(c);
    // S4.1.2: the guarded triangular total cannot saturate; all other entries keep the checked source loop.
    branch(c, MIR_UBGT, generic, reg(c, n->reg), imm(c, MIR_FINITE_DESCENDING_SUM_MAX));
    branch(c, MIR_BNE, generic, reg(c, sum->reg), imm(c, 0));
    // independent partial sums break the reduction dependency; the entry proof makes reassociation exact.
    constexpr int lanes = 4;
    MIR_reg_t partial[lanes] = {sum->reg};
    for (int i = 1; i < lanes; i++) partial[i] = constant(c, 0);
    MIR_label_t unrolled = label(c), merge = label(c);
    branch(c, MIR_BLT, merge, reg(c, n->reg), imm(c, lanes - 1));
    mark(c, unrolled);
    for (int i = 0; i < lanes; i++) {
        MIR_reg_t term = i ? operation(c, MIR_SUB, reg(c, n->reg), imm(c, i)) : n->reg;
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_ADD,
            reg(c, partial[i]), reg(c, partial[i]), reg(c, term)));
    }
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_SUB, reg(c, n->reg), reg(c, n->reg), imm(c, lanes)));
    branch(c, MIR_BGE, unrolled, reg(c, n->reg), imm(c, lanes - 1));
    mark(c, merge);
    for (int i = 1; i < lanes; i++)
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_ADD,
            reg(c, sum->reg), reg(c, sum->reg), reg(c, partial[i])));
    branch(c, MIR_BLT, done, reg(c, n->reg), imm(c, 0));
    mark(c, body);
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_ADD,
        reg(c, sum->reg), reg(c, sum->reg), reg(c, n->reg)));
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_SUB,
        reg(c, n->reg), reg(c, n->reg), imm(c, 1)));
    branch(c, MIR_BGE, body, reg(c, n->reg), imm(c, 0));
    jump(c, done); mark(c, generic);
    return done;
}

static void stage_error(MvpCompiler* c, MIR_reg_t error) {
    if (c->inlining) {
        // an inlined failure belongs to this call, not directly to the containing procedure.
        MvpInline* frame = c->inlining;
        if (!frame->error) { mvp_reject(c->module, frame->function->ast, "inlined error effect was not planned"); return; }
        move(c, frame->error, reg(c, error));
        MIR_type_t type = em_mir_type_for_rep(frame->function->abi.result.normal.abi_rep);
        move(c, frame->result, type == MIR_T_D ? MIR_new_double_op(c->em.ctx, 0.0) : imm(c, 0), type);
        jump(c, frame->done);
        return;
    }
    MirFrameState* f = &c->em.frame;
    MIR_op_t normal = f->item_return ? reg(c, error) : f->return_type == MIR_T_D
        ? MIR_new_double_op(c->em.ctx, 0) : imm(c, 0);
    em_stage_function_return(&c->em, normal, error);
}

static void fail_boundary(MvpCompiler* c, MIR_reg_t error, MvpErrorTarget* failure) {
    if (!failure) { stage_error(c, error); return; }
    if (!failure->done) { failure->done = label(c); failure->error = em_new_reg(&c->em, "boundary_error", MIR_T_I64); }
    move(c, failure->error, reg(c, error)); jump(c, failure->done);
}

static MirValue admit(MvpCompiler* c, MirValue v, Type* contract, MvpErrorTarget* failure = NULL,
        const char* site = "MVP scalar boundary") {
    contract = type_field_unwrap_simple_decl(contract);
    if (!contract || contract == &TYPE_ANY) return convert(c, v, VALUE_REP_ITEM);
    // a static success contract cannot erase a boxed error join (D2.8.2, S11.4.1v3).
    bool proven = mvp_boundary_proven(v.semantic_contract, contract);
    MIR_label_t admitted_error = NULL;
    MIR_reg_t outcome = 0;
    if (!proven && v.rep == VALUE_REP_ITEM && mvp_boundary_proven(&TYPE_ERROR, contract)) {
        // an error admitted by the destination is an argument/value, while a
        // rejected non-error input still takes the validation-failure route.
        admitted_error = label(c); outcome = em_new_reg(&c->em, "admitted_outcome", MIR_T_I64);
        move(c, outcome, reg(c, v.reg));
        branch(c, MIR_BEQ, admitted_error,
            reg(c, operation(c, MIR_URSH, reg(c, v.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
    }
    // a nullable native lane needs ordinary admission only on its absence arm.
    MIR_label_t native_admitted = NULL;
    MIR_reg_t native_result = 0;
    bool optional_pointer = false;
    Type* pointer_base = lambda_type_nullable_lane_base(v.semantic_contract, &optional_pointer);
    if (!proven && nullable(v) && ((v.rep == VALUE_REP_INT_LANE && contract == &TYPE_INT) ||
            (v.rep == VALUE_REP_F64 && contract == &TYPE_FLOAT) ||
            (v.rep == VALUE_REP_RAW_GC_POINTER && optional_pointer && mvp_boundary_proven(pointer_base, contract)))) {
        native_admitted = label(c); native_result = v.reg;
        if (v.rep == VALUE_REP_F64) branch(c, MIR_BEQ, native_admitted,
            reg(c, em_float_lane_null_test(&c->em, v.reg)), imm(c, 0));
        else branch(c, MIR_BNE, native_admitted, reg(c, v.reg),
            imm(c, v.rep == VALUE_REP_RAW_GC_POINTER ? 0 : INT_LANE_NULL));
    }
    Type* outcome_success = v.admitted_success_contract ? v.admitted_success_contract : v.semantic_contract;
    bool optional_success = false;
    Type* success_base = lambda_type_nullable_lane_base(outcome_success, &optional_success);
    if (!proven && v.rep == VALUE_REP_ITEM && optional_success && success_base &&
            (mvp_array_contract(success_base) || lambda_type_nonnull_map_contract(success_base)) &&
            mvp_boundary_proven(success_base, contract)) {
        native_admitted = label(c); native_result = v.reg;
        MIR_label_t checked = label(c);
        // a nullable producer proves only the non-null success arm. Errors and
        // absence retain the ordinary boundary diagnostic (S7.7.3).
        branch(c, MIR_BEQ, checked,
            reg(c, operation(c, MIR_URSH, reg(c, v.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        branch(c, MIR_BNE, native_admitted, reg(c, v.reg), imm(c, ITEM_NULL));
        mark(c, checked);
    }
    if (!proven) {
        // typed constructors already admitted their success value; primitive
        // array invariance must use the same proof as ordinary boundaries.
        bool success_proven = mvp_boundary_proven(v.admitted_success_contract, contract);
        v = convert(c, v, VALUE_REP_ITEM);
        MIR_reg_t checked = v.reg;
        if (!success_proven) {
            MIR_reg_t type = em_load_const(&c->em, em_add_const(&c->em, contract), MIR_T_P);
            checked = em_call_3(&c->em, "lambda_type_check", MIR_T_I64,
                MIR_T_I64, reg(c, v.reg), MIR_T_P, reg(c, type), MIR_T_P,
                imm(c, (intptr_t)site), true);
        }
        MIR_reg_t tag = operation(c, MIR_URSH, reg(c, checked), imm(c, 56));
        MIR_label_t accepted = label(c);
        branch(c, MIR_BNE, accepted, reg(c, tag), imm(c, LMD_TYPE_ERROR));
        fail_boundary(c, checked, failure);
        mark(c, accepted);
        v = value(c, checked, contract->type_id, VALUE_REP_ITEM, contract);
    }
    v = convert(c, v, mvp_rep(contract));
    if (native_admitted) {
        move(c, native_result, reg(c, v.reg), v.mir_type); mark(c, native_admitted);
        v = value(c, native_result, contract->type_id, mvp_rep(contract), contract);
    }
    if (admitted_error) {
        move(c, outcome, reg(c, v.reg)); mark(c, admitted_error);
        v = value(c, outcome, contract->type_id, VALUE_REP_ITEM, contract);
    }
    return value(c, v.reg, contract->type_id, v.rep, contract, (AstNode*)v.provenance_node);
}

static void array_writable_guard(MvpCompiler* c, MvpBinding* binding, MIR_label_t done) {
    MIR_reg_t flags = em_load_at(&c->em, binding->reg, offsetof(Container, flags), MIR_T_U8, "array_flags");
    flags = operation(c, MIR_AND, reg(c, flags), imm(c, (1u << 4) | (1u << 5)));
    branch(c, MIR_BNE, done, reg(c, flags), imm(c, 0));
    MIR_reg_t cow = em_load_at(&c->em, binding->reg, offsetof(Container, cow_state), MIR_T_U8, "array_cow");
    cow = operation(c, MIR_AND, reg(c, cow), imm(c, COW_STATE_SHARED));
    branch(c, MIR_BNE, done, reg(c, cow), imm(c, 0));
}

static void refresh_record(MvpCompiler* c, MvpBinding* binding) {
    TypeMap* shape = mvp_record_contract(binding->contract, binding->record_written);
    // forwarding-only activations need the owner home, not another witness.
    if (!shape || !binding->entry || !binding->entry->node ||
            binding->entry->node->node_type != AST_NODE_PARAM) return;
    AstNamedNode* parameter = (AstNamedNode*)binding->entry->node;
    if (binding->record_written ? !mvp_parameter_storage_stable(c->module, parameter) :
            !mvp_parameter_borrowable(c->module, parameter)) return;
    MIR_reg_t* witness = binding->record_written ? &binding->record_writable : &binding->record_readable;
    if (!*witness) *witness = em_new_reg(&c->em, "record_witness", MIR_T_I64);
    move(c, *witness, imm(c, 0));
    MIR_label_t done = label(c);
    em_guard_map_shape(&c->em, binding->reg, constant(c, (intptr_t)shape), done, true,
        !binding->record_written);
    // shape remains valid when a value snapshot removes exclusive write
    // permission; closed readers may cache that weaker fact (D3.2.4v4).
    if (binding->record_written) array_writable_guard(c, binding, done);
    move(c, *witness, imm(c, 1));
    mark(c, done);
}

static void refresh_array(MvpCompiler* c, MvpBinding* binding, bool fresh = false) {
    refresh_record(c, binding);
    Type* element = mvp_array_element(binding->contract);
    // forwarding a borrowed owner needs its root/write-back home, but no local
    // storage witness. Build witnesses only for bindings this function indexes.
    bool dynamic = !element && binding->rep == VALUE_REP_ITEM;
    if ((!element && !dynamic) || !binding->array_accessed) return;
    if (!binding->array_native) {
        binding->array_native = em_new_reg(&c->em, "array_native", MIR_T_I64);
        binding->array_writable = em_new_reg(&c->em, "array_writable", MIR_T_I64);
        binding->array_length = em_new_reg(&c->em, "array_length", MIR_T_I64);
    }
    if (dynamic && !binding->array_float_native)
        binding->array_float_native = em_new_reg(&c->em, "array_float_native", MIR_T_I64);
    move(c, binding->array_native, imm(c, 0)); move(c, binding->array_writable, imm(c, 0));
    if (dynamic) move(c, binding->array_float_native, imm(c, 0));
    MIR_label_t done = label(c);
    MIR_label_t other_lane = dynamic ? label(c) : done;
    // inferred owners can change kind; guard before reading any container header.
    if (dynamic) em_guard_container(&c->em, binding->reg, LMD_TYPE_ARRAY_NUM, done);
    move(c, binding->array_length, reg(c, em_load_at(&c->em, binding->reg,
        LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "length")));
    ArrayNumElemType lane = ELEM_INT;
    if (element) lambda_array_num_elem_type_for_contract(element, &lane);
    if (!fresh) em_array_num_elem_guard(&c->em, binding->reg, lane, other_lane);
    // storage eligibility is independent of a conservative index bound. A bound
    // wider than this array must not send every valid access through helpers.
    move(c, binding->array_native, imm(c, 1));
    if (!fresh) array_writable_guard(c, binding, done);
    move(c, binding->array_writable, imm(c, 1));
    if (dynamic) {
        jump(c, done); mark(c, other_lane);
        // erased bindings retain guarded physical facts, not a new write
        // contract. Every owner replacement/store refreshes these flags.
        em_array_num_elem_guard(&c->em, binding->reg, ELEM_FLOAT, done);
        move(c, binding->array_float_native, imm(c, 1));
    }
    mark(c, done);
}

static bool fresh_array_value(MvpCompiler* c, MirValue v, Type* contract) {
    Type* element = mvp_array_element(contract);
    AstNode* source = ast_unwrap_primary_to_leaf((AstNode*)v.provenance_node);
    if (!element || !source) return false;
    if (source->node_type == AST_NODE_ARRAY) {
        AstNode* first = ((AstArrayNode*)source)->item;
        return first && mvp_value_contract(c->module, first)->type_id == element->type_id;
    }
    if (source->node_type != AST_NODE_CALL_EXPR || !v.admitted_success_contract) return false;
    AstCallNode* fill = (AstCallNode*)source;
    AstNode* callee = ast_unwrap_primary_to_leaf(fill->function);
    return callee && callee->node_type == AST_NODE_SYS_FUNC &&
        ((AstSysFuncNode*)callee)->fn_info->fn == SYSFUNC_FILL &&
        mvp_value_contract(c->module, fill->argument)->type_id == LMD_TYPE_INT &&
        mvp_value_contract(c->module, fill->argument->next)->type_id == element->type_id;
}

static bool binding_needs_number_home(MvpCompiler* c, MvpBinding* binding) {
    Type* scalar = lambda_type_remove_error_and_null(c->module->tp->pool, binding->contract);
    return !scalar || scalar->type_id == LMD_TYPE_TYPE ||
        lambda_type_id_may_be_wide_scalar(scalar->type_id);
}

static MirValue capture_projection(MvpCompiler* c, MirValue v, AstNode* source, bool projection_only = false) {
    if (!source) source = (AstNode*)v.provenance_node;
    AstNode* projected = ast_cow_value_source(source);
    if (projection_only && (!projected || (projected->node_type != AST_NODE_MEMBER_EXPR &&
            projected->node_type != AST_NODE_INDEX_EXPR))) return v;
    Type* domain = v.admitted_success_contract ? v.admitted_success_contract : v.semantic_contract;
    if (!source || mvp_local_binding(c->module, source) ||
            !ast_expr_insertion_needs_capture(source) ||
            (v.rep != VALUE_REP_ITEM && v.rep != VALUE_REP_RAW_GC_POINTER) ||
            (!ast_type_may_be_container(domain) && !mvp_array_contract(domain))) return v;
    // retaining a projected child creates a second observer. Capture at the
    // ownership boundary, leaving transient reads as borrows (S9.1.2, S9.3.1).
    v = convert(c, v, VALUE_REP_ITEM);
    v.reg = em_call_1(&c->em, "cow_capture_value", MIR_T_I64, MIR_T_I64, reg(c, v.reg), true);
    MirValue result = value(c, v.reg, v.semantic_type, v.rep, v.semantic_contract, source);
    result.admitted_success_contract = v.admitted_success_contract;
    return result;
}

static MIR_reg_t module_binding_location(MvpCompiler* c, MIR_item_t reference, MIR_reg_t* slot) {
    MIR_reg_t descriptor = em_new_reg(&c->em, "module_binding", MIR_T_I64);
    move(c, descriptor, MIR_new_ref_op(c->em.ctx, reference));
    MIR_reg_t unit = em_load_at(&c->em, descriptor, offsetof(LambdaModuleVarRef, module_id), MIR_T_U32, "module_unit");
    *slot = em_load_at(&c->em, descriptor, offsetof(LambdaModuleVarRef, slot), MIR_T_U32, "module_index");
    return em_call_2(&c->em, "lambda_module_state_for_unit", MIR_T_P, MIR_T_P, reg(c, c->em.frame.runtime),
        MIR_T_I64, reg(c, unit), true);
}

static void store_binding(MvpCompiler* c, MvpBinding* binding, MirValue v) {
    AstNode* node = binding->entry->node;
    if (!binding->entry->cow_borrow_lowered) v = capture_projection(c, v, NULL, true);
    bool fresh = fresh_array_value(c, v, binding->contract);
    bool inferred = node->node_type == AST_NODE_VARIABLE_DECLARATOR && !((AstDeclaratorNode*)node)->declared_type;
    // the declaration's pooled name outlives the JIT artifact and identifies failed assignments (S7.7.4).
    const char* site = binding->entry->name ? binding->entry->name->chars : "MVP scalar binding";
    if (!inferred && binding->entry->admission_may_fail && binding->rep == VALUE_REP_ITEM) {
        // deferred admission stores its outcome and continues; only an
        // explicit caret chooses an early exit (S7.7.2v2, S7.7.4v2).
        MvpErrorTarget failure = {};
        v = convert(c, admit(c, v, binding->admission_contract, &failure, site), VALUE_REP_ITEM);
        if (failure.done) {
            MIR_reg_t joined = em_new_reg(&c->em, "binding_outcome", MIR_T_I64);
            MIR_label_t done = label(c);
            move(c, joined, reg(c, v.reg)); jump(c, done); mark(c, failure.done);
            move(c, joined, reg(c, failure.error)); mark(c, done);
            v = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, binding->contract);
        }
    } else v = inferred && binding->rep == VALUE_REP_ITEM ? convert(c, v, VALUE_REP_ITEM)
        : admit(c, v, binding->contract, binding->scope ? &binding->scope->failure : NULL, site);
    if (binding->rep == VALUE_REP_ITEM && !mvp_array_element(binding->contract) &&
            binding_needs_number_home(c, binding)) {
        if (!binding->scalar_home) binding->scalar_home = em_scalar_home_new(&c->em);
        MIR_reg_t home = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, binding->scalar_home));
        // mutable boxed bindings own their cold scalar bytes, independently of a reusable producer home (D5.2.3).
        v.reg = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_DYNAMIC, v.reg, home);
    }
    move(c, binding->reg, reg(c, v.reg), v.mir_type);
    if (binding->module_slot) {
        MIR_reg_t address = em_new_reg(&c->em, "module_slot", MIR_T_I64);
        move(c, address, MIR_new_ref_op(c->em.ctx, binding->module_slot));
        em_store_at(&c->em, address, 0, v.mir_type, binding->reg);
    }
    if (binding->module_ref) {
        MIR_reg_t slot;
        MIR_reg_t state = module_binding_location(c, binding->module_ref, &slot);
        MirValue item = convert(c, v, VALUE_REP_ITEM);
        em_call_void_3(&c->em, "lambda_module_var_store", MIR_T_P, reg(c, state),
            MIR_T_I64, reg(c, slot), MIR_T_I64, reg(c, item.reg), true);
    }
    // successful primitive construction owns fresh packed storage; admission
    // already rejected errors before these physical witnesses are published.
    refresh_array(c, binding, fresh);
    if (binding->var_home) em_store_at(&c->em, binding->var_home, 0, MIR_T_I64, binding->reg);
}

static MirValue constant_fill(MvpCompiler* c, AstCallNode* fill, Type* contract, ArrayRepCert* certificate) {
    if (!certificate || (certificate->flags & ARRAY_REP_CERT_COUNTED) ||
            mvp_array_element(contract) != &TYPE_INT || !mvp_pure_key(fill->argument) ||
            !mvp_pure_key(fill->argument->next)) return {};
    MvpIntRange count = mvp_int_range(c->module, fill->argument);
    MvpIntRange scalar = mvp_int_range(c->module, fill->argument->next);
    int64_t samples[64];
    if (!count.finite || count.lower != count.upper || count.lower < 0 || count.lower > 64 ||
            !scalar.finite || scalar.lower != scalar.upper) return {};
    for (int64_t i = 0; i < count.lower; i++) samples[i] = scalar.lower;
    // read-only, nonescaping actuals may share immutable Input storage with the compiled image (D4.1.1v2).
    // cap each folded payload so specialization cannot create an unbounded constant pool.
    MarkBuilder builder(c->module->tp);
    Item item = builder.createNumericArray(ELEM_INT, 1, &count.lower, samples);
    if (get_type_id(item) != LMD_TYPE_ARRAY_NUM) return {};
    lambda_array_install_rep_cert(item, certificate);
    MIR_reg_t result = em_load_const(&c->em, em_add_const(&c->em, item.array_num), MIR_T_I64);
    return value(c, result, LMD_TYPE_ARRAY, VALUE_REP_ITEM, contract, fill);
}

static MirValue contextual_fill(MvpCompiler* c, AstNode* source, Type* contract,
        ArrayRepCert* certificate, const char* boundary, bool read_only = false) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    if (node && node->node_type == AST_NODE_ARRAY) return array_literal(c, (AstArrayNode*)node, contract);
    if (!mvp_array_element(contract) || !node || node->node_type != AST_NODE_CALL_EXPR)
        return {};
    AstCallNode* fill = (AstCallNode*)node;
    AstNode* callee = ast_unwrap_primary_to_leaf(fill->function);
    if (!callee || callee->node_type != AST_NODE_SYS_FUNC || ((AstSysFuncNode*)callee)->fn_info->fn != SYSFUNC_FILL)
        return {};
    if (fill->propagate && (lambda_type_counts_array_length(contract) ||
            !mvp_boundary_proven(mvp_value_contract(c->module, fill->argument->next), mvp_array_element(contract))))
        // constructor errors propagate at the caret; a later destination
        // admission failure must remain the binding's outcome (S7.7.2v2).
        return {};
    if (read_only) {
        MirValue folded = constant_fill(c, fill, contract, certificate);
        if (folded.reg) return folded;
    }
    MirValue count = expression(c, fill->argument);
    MirValue scalar = expression(c, fill->argument->next);
    MIR_reg_t type = em_load_const(&c->em, em_add_const(&c->em, contract), MIR_T_P);
    MIR_reg_t cert = certificate ? em_load_const(&c->em, em_add_const(&c->em, certificate), MIR_T_P) : 0;
    Type* element = mvp_array_element(contract);
    MIR_label_t done = NULL;
    MIR_reg_t native = 0;
    if (cert && !(certificate->flags & ARRAY_REP_CERT_COUNTED) &&
            count.rep == VALUE_REP_INT_LANE && scalar.rep == mvp_rep(element) &&
            lambda_static_boundary_relation(scalar.semantic_contract, element, false) == STATIC_BOUNDARY_PROVEN) {
        MIR_label_t checked = label(c); done = label(c);
        branch(c, MIR_UBGT, checked, reg(c, count.reg), imm(c, INT53_MAX));
        // finite int Items and raw float/bool lanes match the existing constructor's scalar cases.
        // poison ints retain its checked classification and diagnostic path.
        if (scalar.rep == VALUE_REP_INT_LANE) em_branch_int53_outside(&c->em, scalar.reg, checked);
        MIR_reg_t bits = scalar.rep == VALUE_REP_F64 ? em_emit_double_bits(&c->em, scalar.reg) : scalar.reg;
        native = em_call_4(&c->em, "lambda_fill_primitive_bits", MIR_T_I64,
            MIR_T_I64, reg(c, count.reg), MIR_T_I64, reg(c, bits),
            MIR_T_I64, imm(c, certificate->array_num_elem), MIR_T_P, reg(c, cert), true);
        jump(c, done); mark(c, checked);
    }
    MirValue item = convert(c, scalar, VALUE_REP_ITEM);
    // the shared constructor installs the destination certificate in one pass;
    // its error still reaches the declaring binding's ordinary skip boundary.
    MIR_reg_t result;
    if (count.rep == VALUE_REP_INT_LANE && certificate) {
        // immutable certificates share the compiled contract's pool lifetime;
        // the runtime still owns count/value checks, allocation and rooting.
        result = em_call_5(&c->em, "lambda_fill_for_contract_int_lane_resolved", MIR_T_I64,
            MIR_T_I64, reg(c, count.reg), MIR_T_I64, reg(c, item.reg), MIR_T_P, reg(c, type),
            MIR_T_P, imm(c, (intptr_t)boundary), MIR_T_P, reg(c, cert), true);
    } else {
        count = convert(c, count, VALUE_REP_ITEM);
        result = em_call_4(&c->em, "lambda_fill_for_contract", MIR_T_I64,
            MIR_T_I64, reg(c, count.reg), MIR_T_I64, reg(c, item.reg), MIR_T_P, reg(c, type),
            MIR_T_P, imm(c, (intptr_t)boundary), true);
    }
    if (done) { move(c, native, reg(c, result)); mark(c, done); result = native; }
    MirValue v = value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, source);
    v.admitted_success_contract = contract;
    return fill->propagate ? propagated_value(c, v, source) : v;
}

static MirValue binding_value(MvpCompiler* c, MvpBinding* binding, AstNode* source) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    Type* destination_contract = binding->entry->has_type_annotation
        ? binding->admission_contract : binding->contract;
    if (node && node->node_type == AST_NODE_MAP) {
        Type* destination = lambda_type_nonnull_map_contract(destination_contract);
        if (destination) return map_literal(c, (AstMapNode*)node, (TypeMap*)destination);
    }
    if (destination_contract == &TYPE_STRING && node && node->node_type == AST_NODE_BINARY &&
            ((AstBinaryNode*)node)->op == OPERATOR_JOIN) {
        MirValue joined = string_concat(c, node, binding);
        if (joined.reg) return joined;
    }
    if (node && node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        MvpFunction* f = mvp_function(c->module, ast_direct_call_function(call));
        if (f && binding->rep != VALUE_REP_ITEM && binding->rep != VALUE_REP_RAW_GC_POINTER &&
                lambda_static_boundary_relation(f->result_contract, binding->contract, false) == STATIC_BOUNDARY_PROVEN)
            return direct_call(c, f, call->argument, false, binding, call->propagate);
    }
    MirValue filled = contextual_fill(c, source, destination_contract, binding->array_cert,
        binding->entry->name->chars);
    return filled.reg ? filled : expression(c, source);
}

static MirValue module_literal(MvpCompiler* c, MvpBinding* b, AstNode* source) {
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n) return {};
    if (n->node_type == AST_NODE_PRIMARY && n->type && n->type->type_id == LMD_TYPE_STRING) {
        MIR_reg_t r = em_load_const(&c->em, ((TypeConst*)n->type)->const_index, MIR_T_P);
        return value(c, r, LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, b->contract, source);
    }
    if (n->node_type != AST_NODE_ARRAY) return {};
    LambdaArrayContractInfo info = {};
    if (!lambda_array_contract_info(b->contract, &info) || info.rank != 1 || info.counted_axes) return {};
    Type* element = type_field_unwrap_simple_decl(info.immediate_element);
    if (!element || (element->type_id != LMD_TYPE_STRING && element->type_id != LMD_TYPE_FLOAT &&
            element->type_id != LMD_TYPE_INT && element->type_id != LMD_TYPE_BOOL)) return {};
    if (!b->constant_ready) {
        for (AstNode* item = ((AstArrayNode*)n)->item; item; item = item->next) {
            AstNode* leaf = ast_unwrap_primary_to_leaf(item);
            if (!leaf || leaf->node_type != AST_NODE_PRIMARY || !leaf->type ||
                    leaf->type->type_id != element->type_id ||
                    !mvp_boundary_proven(leaf->type, element)) return {};
        }
        // immutable literal tables belong to the Input arena; writes still
        // detach through the ordinary COW boundary (D4.1.2, S9.1.2).
        MarkBuilder builder(c->module->tp);
        ArrayBuilder array = builder.array();
        for (AstNode* item = ((AstArrayNode*)n)->item; item; item = item->next) {
            AstPrimaryNode* leaf = (AstPrimaryNode*)ast_unwrap_primary_to_leaf(item);
            if (element->type_id == LMD_TYPE_STRING) {
                String* text = (String*)c->module->tp->const_list->data[((TypeConst*)leaf->type)->const_index];
                array.append(builder.createStringItem(text->chars, text->len));
            } else if (element->type_id == LMD_TYPE_FLOAT) array.append(((TypeFloat*)leaf->type)->double_val);
            else if (element->type_id == LMD_TYPE_BOOL) array.append((bool)leaf->literal_value);
            else array.append((int64_t)leaf->literal_value);
        }
        b->constant_item = array.final(); b->constant_ready = true;
    }
    MIR_reg_t r = em_load_const(&c->em, em_add_const(&c->em, b->constant_item.array), MIR_T_P);
    return value(c, r, b->contract->type_id, mvp_rep(b->contract), b->contract, source);
}

static MirValue pooled_constant(MvpCompiler* c, AstNode* source, ArrayList* constants) {
    AstNode* leaf = ast_unwrap_primary_to_leaf(source);
    if (!leaf || !constants) return {};
    if (leaf->const_kind == AST_CONST_POOLED) {
        void* owner = constants->data[leaf->const_index];
        return value(c, em_load_const(&c->em, em_add_const(&c->em, owner), MIR_T_P),
            leaf->type->type_id, VALUE_REP_ITEM, leaf->type, source);
    }
    if (leaf->const_kind == AST_CONST_FOLDED) {
        Item* item = (Item*)constants->data[leaf->const_index];
        return value(c, constant(c, item->item), get_type_id(*item), VALUE_REP_ITEM, leaf->type, source);
    }
    if (leaf->node_type != AST_NODE_PRIMARY || !leaf->type) return {};
    Type* type = leaf->type;
    if (type->type_id == LMD_TYPE_BOOL || type->type_id == LMD_TYPE_INT) return literal(c, (AstPrimaryNode*)leaf);
    if (type->is_literal && type->type_id == LMD_TYPE_STRING) {
        void* text = constants->data[((TypeConst*)type)->const_index];
        return value(c, em_load_const(&c->em, em_add_const(&c->em, text), MIR_T_P),
            LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING, source);
    }
    return {};
}

static MirValue read_binding(MvpCompiler* c, MvpBinding* b, AstNode* node, bool capture) {
    if (b && b->module_ref && b->owner != c->function->id) {
        MIR_reg_t slot;
        MIR_reg_t state = module_binding_location(c, b->module_ref, &slot);
        MIR_reg_t item = em_call_2(&c->em, "lambda_module_var_at", MIR_T_I64,
            MIR_T_P, reg(c, state), MIR_T_I64, reg(c, slot), true);
        MirValue loaded = value(c, item, b->contract->type_id, VALUE_REP_ITEM, b->contract, node);
        if (capture && ast_type_may_be_container(b->contract))
            loaded = value(c, em_call_1(&c->em, "cow_capture_value", MIR_T_I64, MIR_T_I64, reg(c, item), true),
                b->contract->type_id, VALUE_REP_ITEM, b->contract, node);
        return convert(c, loaded, b->rep);
    }
    if (b && b->constant_float)
        return value(c, float_constant(c, b->float_value), LMD_TYPE_FLOAT, VALUE_REP_F64, b->contract, node);
    // singleton ranges include every write; immutable module constants need no runtime reload.
    if (b && b->rep == VALUE_REP_INT_LANE && b->range.finite && b->range.lower == b->range.upper)
        return value(c, constant(c, b->range.lower), LMD_TYPE_INT, VALUE_REP_INT_LANE, b->contract, node);
    if (b && b->module_slot && b->owner != c->function->id) {
        MIR_reg_t address = em_new_reg(&c->em, "module_slot", MIR_T_I64);
        move(c, address, MIR_new_ref_op(c->em.ctx, b->module_slot));
        return value(c, em_load_at(&c->em, address, 0, em_mir_type_for_rep(b->rep), "module_scalar"),
            b->contract->type_id, b->rep, b->contract, node);
    }
    if (b && b->owner == AST_FUNCTION_ID_INVALID && b->owner != c->function->id &&
            b->entry && !b->entry->is_mutable && b->entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        MirValue folded = module_literal(c, b, ((AstDeclaratorNode*)b->entry->node)->init);
        if (!folded.reg) folded = pooled_constant(c, ((AstDeclaratorNode*)b->entry->node)->init, c->module->tp->const_list);
        if (folded.reg) return folded;
    }
    if (!b || !b->reg || b->owner != c->function->id) {
        NameEntry* entry = node && node->node_type == AST_NODE_IDENT ? ((AstIdentNode*)node)->entry : NULL;
        AstNode* init = entry && entry->node && entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR
            ? ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)entry->node)->init) : NULL;
        char reason[192];
        snprintf(reason, sizeof(reason), "a value read requires a local binding: %s definition=%d imported=%d constant=%d",
            entry && entry->name ? entry->name->chars : "missing", entry && entry->node ? entry->node->node_type : -1,
            entry && (entry->import || entry->import_owner), init ? init->const_kind : -1);
        mvp_reject(c->module, node, reason); return null_value(c);
    }
    MIR_reg_t r = em_new_reg(&c->em, "read", em_mir_type_for_rep(b->rep));
    move(c, r, reg(c, b->reg), em_mir_type_for_rep(b->rep));
    bool optional_string = false;
    Type* string_base = lambda_type_nullable_lane_base(
        lambda_type_remove_error(c->module->tp->pool, b->contract), &optional_string);
    bool optional_success = false;
    Type* success_base = b->producer_success_contract
        ? lambda_type_nullable_lane_base(b->producer_success_contract, &optional_success) : NULL;
    bool stable_scalar = success_base && (success_base->type_id == LMD_TYPE_FLOAT ||
        success_base->type_id == LMD_TYPE_INT || success_base->type_id == LMD_TYPE_BOOL ||
        success_base->type_id == LMD_TYPE_DECIMAL);
    if (capture && string_base && string_base->type_id == LMD_TYPE_STRING && b->rep == VALUE_REP_RAW_GC_POINTER)
        r = em_call_1(&c->em, "fn_string_freeze", MIR_T_P, MIR_T_P, reg(c, r), true);
    Type* map_contract = lambda_type_nonnull_map_contract(b->contract);
    // unchanged scalar/error bindings already own their number-home bytes;
    // reads reuse that owner and have no mutable buffer to snapshot (D5.2.3).
    if (capture && !stable_scalar && ((b->rep == VALUE_REP_ITEM && (ast_type_may_be_container(b->contract) ||
            (string_base && string_base->type_id == LMD_TYPE_STRING))) || mvp_array_element(b->contract) || map_contract ||
            (string_base && string_base->type_id == LMD_TYPE_MAP) ||
            mvp_array_contract(b->contract))) {
        r = em_call_1(&c->em, "cow_capture_value", MIR_T_I64, MIR_T_I64, reg(c, r), true);
        if (b->array_writable) move(c, b->array_writable, imm(c, 0));
        if (b->record_writable) move(c, b->record_writable, imm(c, 0));
    }
    if (b->rep == VALUE_REP_ITEM && !stable_scalar && !mvp_array_element(b->contract) && binding_needs_number_home(c, b)) {
        int home = em_scalar_home_new(&c->em);
        r = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_DYNAMIC, r,
            em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home)));
    }
    MirValue result = value(c, r, b->contract->type_id, b->rep, b->contract, node);
    // native carriers already encode their domain; this fact describes only
    // the success alternative of a boxed outcome (D2.4.3).
    if (b->rep == VALUE_REP_ITEM && b->producer_success_contract)
        result.admitted_success_contract = b->producer_success_contract;
    return result;
}

static AstNode* function_result_expression(MvpFunction* f) {
    AstNode* body = mvp_single_statement(f->ast->body);
    if (body && body->node_type == AST_NODE_RETURN_STAM) body = ((AstReturnNode*)body)->value;
    else if (f->ast->node_type == AST_NODE_PROC) return NULL;
    return ast_unwrap_primary_to_leaf(body);
}

static AstNode* string_recipe_body(MvpFunction* f) {
    if (!f || mvp_parameter_receiver(f->ast) || f->result_contract != &TYPE_STRING ||
            f->abi.effects.may_return_error || f->abi.effects.may_reenter)
        return NULL;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next)
        if (mvp_contract(p)->type_id != LMD_TYPE_STRING) return NULL;
    return function_result_expression(f);
}

static int recipe_parameter(MvpFunction* f, AstNode* node) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!f || !node || node->node_type != AST_NODE_IDENT) return -1;
    int i = 0;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, i++)
        if (p->entry == ((AstIdentNode*)node)->entry) return i;
    return -1;
}

static int string_recipe_size(LambdaMvpModule* m, AstNode* node, MvpFunction* scope, int depth = 0) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!node || depth > 8) return 0;
    AstNode* leaves[LAMBDA_STRING_CONCAT_MAX_PARTS]; int count = 0, total = 0;
    if (!ast_collect_join_leaves(node, leaves, &count)) return 0;
    for (int i = 0; i < count; i++) {
        AstNode* leaf = ast_unwrap_primary_to_leaf(leaves[i]);
        if (leaf->node_type == AST_NODE_CALL_EXPR) {
            AstCallNode* call = (AstCallNode*)leaf;
            MvpFunction* f = mvp_function(m, ast_direct_call_function(call));
            AstNode* body = string_recipe_body(f);
            if (!body) return 0;
            for (AstNode* a = call->argument; a; a = a->next) {
                AstNode* actual = ast_unwrap_primary_to_leaf(a);
                // inner arguments are pure literal/parameter reads; outer actuals still evaluate once in order.
                if (scope ? recipe_parameter(scope, actual) < 0 &&
                        !(actual && actual->node_type == AST_NODE_PRIMARY && actual->type->type_id == LMD_TYPE_STRING)
                        : mvp_value_contract(m, a)->type_id != LMD_TYPE_STRING) return 0;
            }
            int nested = string_recipe_size(m, body, f, depth + 1);
            if (!nested) return 0;
            total += nested;
        } else if (recipe_parameter(scope, leaf) >= 0 ||
                (leaf->node_type == AST_NODE_PRIMARY && leaf->type && leaf->type->type_id == LMD_TYPE_STRING)) total++;
        else return 0;
        if (total > LAMBDA_STRING_CONCAT_MAX_PARTS) return 0;
    }
    return total;
}

static AstNode* integer_string_argument(MvpCompiler* c, AstNode* node) {
    node = ast_unwrap_primary_to_leaf(node);
    if (!node || node->node_type != AST_NODE_CALL_EXPR) return NULL;
    AstCallNode* call = (AstCallNode*)node;
    AstNode* target = ast_unwrap_primary_to_leaf(call->function);
    MvpFunction* f = mvp_function(c->module, ast_direct_call_function(call));
    if (f) {
        AstNamedNode* p = f->ast->param;
        AstNode* body = function_result_expression(f);
        // the generic string() effect includes reentry; this exact int-only
        // wrapper can only invoke the canonical scalar renderer (S4.1.2).
        if (f->result_contract != &TYPE_STRING || !p || p->next || lambda_type_param(p->type)->is_var_param ||
                mvp_contract(p) != &TYPE_INT || !body || body->node_type != AST_NODE_CALL_EXPR) return NULL;
        AstCallNode* converted = (AstCallNode*)body;
        if (recipe_parameter(f, converted->argument) != 0) return NULL;
        target = ast_unwrap_primary_to_leaf(converted->function);
    }
    if (!target || target->node_type != AST_NODE_SYS_FUNC ||
            ((AstSysFuncNode*)target)->fn_info->fn != SYSFUNC_STRING ||
            mvp_value_contract(c->module, call->argument) != &TYPE_INT) return NULL;
    return call->argument;
}

static MirValue string_recipe_argument(MvpCompiler* c, AstNode* node, MvpFunction* scope, MirValue* actuals) {
    int p = recipe_parameter(scope, node);
    if (p >= 0) return actuals[p];
    // a transparent integer formatter has no observable work beyond its input;
    // retain that input once, in argument order, until the destination join.
    if (AstNode* input = integer_string_argument(c, node)) return expression(c, input);
    return expression(c, node);
}

static void string_recipe_parts(MvpCompiler* c, AstNode* node, MvpFunction* scope,
        MirValue* actuals, MirValue* parts, int* count) {
    AstNode* leaves[LAMBDA_STRING_CONCAT_MAX_PARTS]; int size = 0;
    ast_collect_join_leaves(node, leaves, &size);
    for (int i = 0; i < size; i++) {
        AstNode* leaf = ast_unwrap_primary_to_leaf(leaves[i]);
        if (leaf->node_type != AST_NODE_CALL_EXPR) {
            parts[(*count)++] = string_recipe_argument(c, leaf, scope, actuals);
            continue;
        }
        AstCallNode* call = (AstCallNode*)leaf;
        MvpFunction* f = mvp_function(c->module, ast_direct_call_function(call));
        MirValue inputs[LAMBDA_MAX_FUNCTION_ARGS]; int arg = 0;
        for (AstNode* a = call->argument; a; a = a->next)
            inputs[arg++] = string_recipe_argument(c, a, scope, actuals);
        string_recipe_parts(c, string_recipe_body(f), f, inputs, parts, count);
    }
}

static String* string_fragment_literal(MirValue fragment) {
    TypeString* literal = ast_string_literal_type((AstNode*)fragment.provenance_node);
    return literal ? literal->string : NULL;
}

static int coalesce_string_literals(MvpCompiler* c, MirValue* fragments, int count) {
    int written = 0;
    for (int first = 0; first < count;) {
        int last = first;
        size_t bytes = 0;
        while (last < count) {
            String* text = string_fragment_literal(fragments[last]);
            if (!text || text->len > UINT32_MAX - bytes) break;
            bytes += text->len;
            last++;
        }
        if (last - first < 2) { fragments[written++] = fragments[first++]; continue; }
        // recipe parameters retain literal provenance; adjacent immutable bytes
        // share one Input-owned constant without changing dynamic evaluation order.
        StrBuf* buffer = strbuf_new_cap(bytes + 1);
        if (!buffer) { mvp_reject(c->module, NULL, "string constant allocation failed"); return 0; }
        for (int i = first; i < last; i++) {
            String* text = string_fragment_literal(fragments[i]);
            strbuf_append_str_n(buffer, text->chars, text->len);
        }
        MarkBuilder builder(c->module->tp);
        String* joined = builder.createString(buffer->str, buffer->length);
        strbuf_free(buffer);
        if (!joined) { mvp_reject(c->module, NULL, "string constant allocation failed"); return 0; }
        MIR_reg_t r = em_load_const(&c->em, em_add_const(&c->em, joined), MIR_T_P);
        fragments[written++] = value(c, r, LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING);
        first = last;
    }
    return written;
}

static MirValue checked_join_parts(MvpCompiler* c, AstNode* node, MirValue* parts, int* next) {
    AstNode* leaf = ast_unwrap_primary_to_leaf(node);
    if (!leaf || leaf->node_type != AST_NODE_BINARY || ((AstBinaryNode*)leaf)->op != OPERATOR_JOIN)
        return convert(c, parts[(*next)++], VALUE_REP_ITEM);
    AstBinaryNode* join = (AstBinaryNode*)leaf;
    MirValue left = checked_join_parts(c, join->left, parts, next);
    MirValue right = checked_join_parts(c, join->right, parts, next);
    return value(c, em_call_2(&c->em, "fn_join", MIR_T_I64, MIR_T_I64, reg(c, left.reg),
        MIR_T_I64, reg(c, right.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
}

static MirValue string_concat(MvpCompiler* c, AstNode* node, MvpBinding* owner,
        MvpBinding* replacement, AstNode* initializer) {
    AstNode* leaves[LAMBDA_STRING_CONCAT_MAX_PARTS]; int count = 0;
    if (!ast_collect_join_leaves(node, leaves, &count) || !count) return {};
    if (owner && mvp_local_binding(c->module, leaves[0]) != owner) return {};
    bool has_string = false, dynamic = false;
    for (int i = 0; i < count; i++) {
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(lambda_type_remove_error(c->module->tp->pool,
            mvp_value_contract(c->module, leaves[i])), &optional);
        if (!base || (base->type_id != LMD_TYPE_STRING &&
                (base->type_id != LMD_TYPE_DECIMAL || optional))) {
            if (!owner || replacement) return {};
            dynamic = true;
        }
        has_string |= base && base->type_id == LMD_TYPE_STRING && !optional;
    }
    if (!has_string) return {};
    for (int i = 0; replacement && i < count; i++)
        if (mvp_local_binding(c->module, leaves[i]) == replacement) leaves[i] = initializer;
    int recipes[LAMBDA_STRING_CONCAT_MAX_PARTS], expanded = 0;
    for (int i = 0; i < count; i++) {
        recipes[i] = string_recipe_size(c->module, leaves[i], NULL);
        expanded += recipes[i] ? recipes[i] : 1;
    }
    if (dynamic || expanded > LAMBDA_STRING_CONCAT_MAX_PARTS) memset(recipes, 0, sizeof(recipes));
    if (count == 1 && (!recipes[0] || expanded < 2)) return {};
    MirValue fragments[LAMBDA_STRING_CONCAT_MAX_PARTS]; int parts = 0;
    for (int i = 0; i < count; i++) {
        if (recipes[i]) {
            string_recipe_parts(c, leaves[i], NULL, NULL, fragments, &parts);
            continue;
        }
        fragments[parts++] = owner && i == 0 ? read_binding(c, owner, leaves[i], false) :
            string_recipe_argument(c, leaves[i], NULL, NULL);
    }
    MirValue checked_parts[LAMBDA_STRING_CONCAT_MAX_PARTS];
    if (dynamic) {
        for (int i = 0; i < parts; i++) fragments[i] = convert(c, fragments[i], VALUE_REP_ITEM);
        memcpy(checked_parts, fragments, parts * sizeof(MirValue));
    }
    MIR_reg_t outcome = dynamic ? em_new_reg(&c->em, "string_outcome", MIR_T_I64) : 0;
    MIR_label_t done = NULL;
    if (dynamic) done = label(c);
    for (int i = 0; i < parts; i++) {
        MirValue* part = &fragments[i];
        // unknown kinds replay the original tree on any non-text value,
        // including errors, so an earlier join failure keeps precedence.
        if (dynamic || part->rep != VALUE_REP_ITEM || !lambda_type_accepts_error(part->semantic_contract)) continue;
        if (!outcome) { outcome = em_new_reg(&c->em, "string_outcome", MIR_T_I64); done = label(c); }
        // every fragment has already run in source order. The first error wins,
        // while a clean owner retains its append buffer without a capturing read.
        move(c, outcome, reg(c, part->reg));
        MIR_reg_t tag = operation(c, MIR_URSH, reg(c, part->reg), imm(c, 56));
        branch(c, MIR_BEQ, done, reg(c, tag), imm(c, LMD_TYPE_ERROR));
        part->semantic_contract = lambda_type_remove_error(c->module->tp->pool, part->semantic_contract);
        bool optional = false;
        part->semantic_type = lambda_type_nullable_lane_base(part->semantic_contract, &optional)->type_id;
    }
    MIR_label_t checked = dynamic ? label(c) : NULL;
    for (int i = 0; i < parts; i++) {
        MirValue* part = &fragments[i];
        if (dynamic && part->rep == VALUE_REP_ITEM) {
            MIR_label_t text = label(c);
            branch(c, MIR_BEQ, text, reg(c, part->reg), imm(c, ITEM_NULL));
            MIR_reg_t tag = operation(c, MIR_URSH, reg(c, part->reg), imm(c, 56));
            branch(c, MIR_BNE, checked, reg(c, tag), imm(c, LMD_TYPE_STRING)); mark(c, text);
            part->semantic_type = LMD_TYPE_STRING;
            part->semantic_contract = mvp_nullable_contract(c->module, &TYPE_STRING);
        }
        if (part->semantic_type == LMD_TYPE_STRING) *part = convert(c, *part, VALUE_REP_RAW_GC_POINTER);
    }
    count = coalesce_string_literals(c, fragments, parts);
    if (!count) return {};
    if (count == 1) return fragments[0];
    for (int i = 0; i < count; i++) if (nullable(fragments[i])) {
        MIR_reg_t present = em_new_reg(&c->em, "string_or_empty", MIR_T_P);
        MIR_label_t done = label(c);
        move(c, present, reg(c, fragments[i].reg));
        branch(c, MIR_BNE, done, reg(c, present), imm(c, 0));
        // null is the join identity; another non-null string guarantees the
        // complete result remains text (S10.6.1).
        MarkBuilder builder(c->module->tp);
        move(c, present, reg(c, em_load_const(&c->em, em_add_const(&c->em, builder.createString("")), MIR_T_P)));
        mark(c, done);
        fragments[i] = value(c, present, LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING);
    }
    bool formatted = false;
    for (int i = 0; i < count; i++) formatted |= fragments[i].semantic_type == LMD_TYPE_INT ||
        fragments[i].semantic_type == LMD_TYPE_DECIMAL;
    MIR_op_t args[LAMBDA_STRING_CONCAT_MAX_PARTS];
    for (int i = 0; i < count; i++) args[i] = reg(c, convert(c, fragments[i],
        formatted ? VALUE_REP_ITEM : VALUE_REP_RAW_GC_POINTER).reg);
    // only assignment may consume its prefix. Ordinary reads freeze buffers before any alias escapes.
    MIR_reg_t joined;
    if (owner && count == 2 && !formatted) joined = em_call_2(&c->em, "fn_strcat", MIR_T_P,
        MIR_T_P, args[0], MIR_T_P, args[1], true);
    else if (owner && count <= 6 && !formatted) {
        static const char* names[] = {NULL, NULL, NULL, "fn_strcat3", "fn_strcat4", "fn_strcat5", "fn_strcat6"};
        MIR_type_t types[LAMBDA_STRING_CONCAT_MAX_PARTS];
        for (int i = 0; i < count; i++) types[i] = MIR_T_P;
        joined = em_call_with_args(&c->em, names[count], MIR_T_P, count, types, args, true);
    }
    else {
        MIR_var_t fixed[] = {{MIR_T_I64, "owned", 0}, {MIR_T_I64, "count", 0}};
        MIR_op_t values[] = {imm(c, owner != NULL), imm(c, count)};
        joined = em_emit_vararg_call(&c->em, formatted ? "fn_strcat_values" : "fn_strcat_many",
            MIR_T_P, 1, 2, fixed, values, count, args);
    }
    MirValue result = value(c, joined, LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING, node);
    if (!outcome) return result;
    move(c, outcome, reg(c, convert(c, result, VALUE_REP_ITEM).reg));
    if (dynamic) {
        jump(c, done); mark(c, checked);
        // replay the exact join tree over already evaluated values. Sequence
        // shape, validation and operand effects stay unchanged (S10.6.1).
        int next = 0;
        move(c, outcome, reg(c, checked_join_parts(c, node, checked_parts, &next).reg));
    }
    mark(c, done);
    result = value(c, outcome, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    if (!dynamic) result.admitted_success_contract = &TYPE_STRING;
    return result;
}


static bool array_index_in_extent(MvpCompiler* c, MvpBinding* b, AstNode* key) {
    MvpIntRange range = mvp_int_range(c->module, key);
    return range.finite && range.lower >= 0 && range.upper < b->array_extent;
}

static MvpBinding* loop_store_array(MvpCompiler* c, AstLoopControlNode* loop) {
    if (!loop->body || loop->body->node_type != AST_NODE_CONTENT || !mvp_pure_key(loop->cond) ||
            mvp_value_contract(c->module, loop->cond)->type_id != LMD_TYPE_BOOL) return NULL;
    AstNode* first = ((AstListNode*)loop->body)->item;
    AstNode* second = first ? first->next : NULL;
    if (!first || first->node_type != AST_NODE_INDEX_ASSIGN_STAM || !second || second->next ||
            second->node_type != AST_NODE_ASSIGN_STAM) return NULL;
    AstAssignStamNode* store = (AstAssignStamNode*)first;
    AstAssignStamNode* step = (AstAssignStamNode*)second;
    MvpBinding* array = mvp_local_binding(c->module, store->object);
    MvpBinding* scalar = mvp_binding(c->module, step->target_entry);
    Type* element = array ? mvp_array_element(array->contract) : NULL;
    if (!element || !scalar || scalar->rep != VALUE_REP_INT_LANE ||
            !mvp_pure_key(store->key) || !mvp_pure_key(store->value) || !mvp_pure_key(step->value) ||
            mvp_value_contract(c->module, store->key)->type_id != LMD_TYPE_INT ||
            mvp_value_contract(c->module, store->value)->type_id != element->type_id ||
            mvp_value_contract(c->module, step->value)->type_id != LMD_TYPE_INT) return NULL;
    // only the checked store can collect; it refreshes this buffer before the
    // backedge. There are no calls, captures or alternate array writes here.
    return array;
}

static MirValue array_read(MvpCompiler* c, AstFieldNode* node,
        MIR_label_t boolean_target = NULL, bool when_true = false) {
    MvpBinding* binding = mvp_local_binding(c->module, node->object);
    // immutable lookup-table keys cannot mutate or retain this owner. Reuse
    // the indexed-region proof instead of freezing it before a nested read.
    bool pure_key = mvp_index_key(c->module, node->field);
    MirValue owner = binding && pure_key ? read_binding(c, binding, node->object, false)
        : expression(c, node->object);
    MirValue key = expression(c, node->field);
    if (boolean_target && (mvp_array_element(owner.semantic_contract) != &TYPE_BOOL ||
            key.rep != VALUE_REP_INT_LANE || c->dense_arrays || (binding && binding->array_present)))
        boolean_target = NULL;
    if (lambda_array_contract_element(owner.semantic_contract) == &TYPE_STRING && key.rep == VALUE_REP_INT_LANE) {
        MvpIntRange fixed = mvp_int_range(c->module, node->field);
        if (binding && binding->constant_ready && fixed.finite && fixed.lower == fixed.upper &&
                fixed.lower >= 0 && fixed.upper < binding->constant_item.array->length) {
            Item member = array_get(binding->constant_item.array, fixed.lower);
            if (get_type_id(member) == LMD_TYPE_STRING) {
                AstNode* source = ((AstDeclaratorNode*)binding->entry->node)->init;
                AstNode* literal = ((AstArrayNode*)ast_unwrap_primary_to_leaf(source))->item;
                for (int64_t i = 0; literal && i < fixed.lower; i++) literal = literal->next;
                MIR_reg_t r = em_load_const(&c->em, em_add_const(&c->em, member.get_safe_string()), MIR_T_P);
                return value(c, r, LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, literal->type, literal);
            }
        }
        Type* contract = mvp_nullable_contract(c->module, &TYPE_STRING);
        MIR_reg_t result = em_new_reg(&c->em, "string_element", MIR_T_P);
        MIR_label_t slow = label(c), absent = label(c), done = label(c);
        owner = convert(c, owner, VALUE_REP_ITEM);
        MIR_reg_t pointer = em_guard_container(&c->em, owner.reg, LMD_TYPE_ARRAY, slow);
        MIR_reg_t flags = em_load_at(&c->em, pointer, LAMBDA_GC_OFF_CONTAINER_ARRAY_FLAGS, MIR_T_U8, "string_array_flags");
        // enforced string[] stores eight-byte words in either carrier: the
        // mask strips an Item tag and leaves a native String* unchanged.
        branch(c, MIR_BNE, slow, reg(c, operation(c, MIR_AND, reg(c, flags),
            imm(c, CONTAINER_ARRAY_FLAG_NDIM))), imm(c, 0));
        em_array_num_bounds_check(&c->em, pointer, key.reg, absent);
        MIR_reg_t address = em_array_element_address(&c->em, pointer, key.reg, 8);
        MIR_reg_t item = em_load_at(&c->em, address, 0, MIR_T_I64, "string_item");
        move(c, result, reg(c, operation(c, MIR_AND, reg(c, item), imm(c, 0x00ffffffffffffffULL))));
        jump(c, done); mark(c, absent); move(c, result, imm(c, 0)); jump(c, done);
        mark(c, slow);
        MirValue boxed_key = convert(c, key, VALUE_REP_ITEM);
        MIR_reg_t item_result = em_call_2(&c->em, "fn_index", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, boxed_key.reg), true);
        move(c, result, reg(c, convert(c, value(c, item_result, LMD_TYPE_STRING, VALUE_REP_ITEM, contract),
            VALUE_REP_RAW_GC_POINTER).reg)); mark(c, done);
        return value(c, result, LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, contract, node);
    }
    Type* element = mvp_array_element(owner.semantic_contract);
    if (binding && (c->dense_arrays || binding->array_present)) {
        ArrayNumElemType lane;
        lambda_array_num_elem_type_for_contract(element, &lane);
        // allocating bodies retain rooted owners and reload the buffer for each
        // access; only noncollecting regions may keep an interior pointer.
        MIR_reg_t address = c->dense_arrays
            ? em_element_address(&c->em, binding->dense_items, key.reg, element == &TYPE_BOOL ? 1 : 8)
            : em_array_element_address(&c->em, owner.reg, key.reg, element == &TYPE_BOOL ? 1 : 8);
        return value(c, em_load_at(&c->em, address, 0, em_numeric_storage_type(lane), "dense_element"),
            element->type_id, mvp_rep(element), element, node);
    }
    // primitive reads carry absence in their existing nullable lane (S7.1.1v3).
    bool native = element && key.rep == VALUE_REP_INT_LANE;
    bool present = native && mvp_array_index_present(c->module, binding, node->field);
    ValueRep rep = native ? mvp_rep(element) : VALUE_REP_ITEM;
    Type* contract = !native ? &TYPE_ANY : present ? element : element == &TYPE_INT ? c->module->nullable_int :
        element == &TYPE_FLOAT ? c->module->nullable_float : c->module->nullable_bool;
    MIR_reg_t output = em_new_reg(&c->em, "indexed", em_mir_type_for_rep(rep));
    MIR_label_t slow = label(c), done = label(c);
    if (!native && binding && binding->array_native && key.rep == VALUE_REP_INT_LANE && pure_key) {
        branch(c, MIR_BEQ, slow, reg(c, binding->array_native), imm(c, 0));
        branch(c, MIR_UBGE, slow, reg(c, key.reg), reg(c, binding->array_length));
        MIR_reg_t address = em_array_element_address(&c->em, owner.reg, key.reg, 8);
        MirValue member = value(c, em_load_at(&c->em, address, 0, MIR_T_I64, "dynamic_int_element"),
            LMD_TYPE_INT, VALUE_REP_INT_LANE, c->module->nullable_int);
        move(c, output, reg(c, convert(c, member, VALUE_REP_ITEM).reg)); jump(c, done);
    }
    if (native) {
        MIR_label_t absent = label(c);
        ArrayNumElemType lane;
        lambda_array_num_elem_type_for_contract(element, &lane);
        if (binding && binding->array_native && pure_key) {
            branch(c, MIR_BEQ, slow, reg(c, binding->array_native), imm(c, 0));
            if (!present) branch(c, MIR_UBGE, absent, reg(c, key.reg), reg(c, binding->array_length));
        } else {
            em_array_num_elem_guard(&c->em, owner.reg, lane, slow);
            em_array_num_bounds_check(&c->em, owner.reg, key.reg, absent);
        }
        MIR_reg_t address = em_array_element_address(&c->em, owner.reg, key.reg, element == &TYPE_BOOL ? 1 : 8);
        MIR_reg_t member = em_load_at(&c->em, address, 0, em_numeric_storage_type(lane), "element");
        if (boolean_target) branch(c, when_true ? MIR_BEQ : MIR_BNE, boolean_target, reg(c, member), imm(c, 1));
        else move(c, output, reg(c, member), em_mir_type_for_rep(rep));
        jump(c, done); mark(c, absent);
        if (boolean_target) {
            if (!when_true) jump(c, boolean_target);
        } else move(c, output, element == &TYPE_FLOAT ? reg(c, null_float(c)) :
            imm(c, element == &TYPE_INT ? INT_LANE_NULL : BOOL_ERROR), em_mir_type_for_rep(rep));
        jump(c, done);
    }
    mark(c, slow);
    owner = convert(c, owner, VALUE_REP_ITEM); key = convert(c, key, VALUE_REP_ITEM);
    MirValue fallback = value(c, em_call_2(&c->em, "fn_index", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, key.reg), true),
        native ? element->type_id : LMD_TYPE_ANY, VALUE_REP_ITEM, contract);
    fallback = convert(c, fallback, rep);
    if (boolean_target) branch(c, when_true ? MIR_BEQ : MIR_BNE, boolean_target, reg(c, fallback.reg), imm(c, 1));
    else move(c, output, reg(c, fallback.reg), fallback.mir_type);
    mark(c, done);
    if (boolean_target) return {};
    return value(c, output, native ? element->type_id : LMD_TYPE_ANY, rep, contract, node);
}

static bool positive_power_two(int64_t value) {
    return value > 0 && (uint64_t)value <= SIZE_MAX && math_size_is_power_of_two((size_t)value);
}

static MIR_label_t modular_array_update(MvpCompiler* c, AstAssignStamNode* node) {
    if (c->dense_arrays || !mvp_pure_key(node->key) ||
            mvp_value_contract(c->module, node->key)->type_id != LMD_TYPE_INT) return NULL;
    MvpBinding* owner = mvp_local_binding(c->module, node->object);
    Type* element = owner ? mvp_array_element(owner->contract) : NULL;
    if (!owner || !owner->array_writable || (element && element != &TYPE_INT)) return NULL;
    AstNode* rhs = ast_unwrap_primary_to_leaf(node->value);
    if (rhs && rhs->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* cast = (AstCallNode*)rhs;
        AstNode* target = ast_unwrap_primary_to_leaf(cast->function);
        if (target && target->node_type == AST_NODE_SYS_FUNC &&
                ((AstSysFuncNode*)target)->fn_info->fn == SYSFUNC_INT)
            rhs = ast_unwrap_primary_to_leaf(cast->argument);
    }
    if (!rhs || rhs->node_type != AST_NODE_BINARY || ((AstBinaryNode*)rhs)->op != OPERATOR_MOD) return NULL;
    AstBinaryNode* modulo = (AstBinaryNode*)rhs;
    MvpIntRange divisor = mvp_int_range(c->module, modulo->right);
    if (!mvp_pure_key(modulo->right) || !divisor.finite ||
            divisor.lower != divisor.upper || divisor.lower <= 0) return NULL;
    int64_t modulus = divisor.lower, offset, factor = 1;
    AstNode* left = ast_unwrap_primary_to_leaf(modulo->left);
    if (!left || left->node_type != AST_NODE_BINARY) return NULL;
    AstBinaryNode* sum = (AstBinaryNode*)left;
    MvpIntRange increment = mvp_int_range(c->module, sum->right);
    if ((sum->op != OPERATOR_ADD && sum->op != OPERATOR_SUB) ||
            !mvp_pure_key(sum->right) || !increment.finite || increment.lower != increment.upper) return NULL;
    offset = increment.lower;
    if (sum->op == OPERATOR_SUB) offset = -offset;
    AstNode* source = ast_unwrap_primary_to_leaf(sum->left);
    if (source && source->node_type == AST_NODE_BINARY && ((AstBinaryNode*)source)->op == OPERATOR_MUL) {
        AstBinaryNode* product = (AstBinaryNode*)source;
        MvpIntRange scale = mvp_int_range(c->module, product->right);
        if (!mvp_pure_key(product->right) || !scale.finite ||
                scale.lower != scale.upper || scale.lower <= 0) return NULL;
        factor = scale.lower;
        source = ast_unwrap_primary_to_leaf(product->left);
    }
    if (!source || source->node_type != AST_NODE_INDEX_EXPR) return NULL;
    AstFieldNode* read = (AstFieldNode*)source;
    if (mvp_local_binding(c->module, read->object) != owner ||
            !mvp_same_integer_expression(c->module, read->field, node->key)) return NULL;
    MIR_label_t checked = label(c), done = label(c);
    MirValue index = expression(c, node->key);
    branch(c, MIR_BEQ, checked, reg(c, owner->array_writable), imm(c, 0));
    branch(c, MIR_UBGE, checked, reg(c, index.reg), reg(c, owner->array_length));
    MIR_reg_t address = em_array_element_address(&c->em, owner->reg, index.reg, 8);
    MIR_reg_t input = em_load_at(&c->em, address, 0, MIR_T_I64, "modular_input");
    int64_t lower = offset < 0 ? (-offset + factor - 1) / factor : 0;
    int64_t upper = (offset > 0 ? INT53_MAX - offset : INT53_MAX) / factor;
    // guard both affine intermediates before the store: saturation must not
    // disappear merely because a later remainder is small (S4.1.2, S9.1.2).
    branch(c, MIR_UBGT, checked, reg(c, input), imm(c, upper));
    if (lower) branch(c, MIR_BLT, checked, reg(c, input), imm(c, lower));
    MIR_reg_t product = factor == 1 ? input : operation(c, MIR_MUL, reg(c, input), imm(c, factor));
    MIR_reg_t result = operation(c, MIR_ADD, reg(c, product), imm(c, offset));
    result = positive_power_two(modulus)
        ? operation(c, MIR_AND, reg(c, result), imm(c, modulus - 1))
        : operation(c, MIR_MOD, reg(c, result), imm(c, modulus));
    em_store_at(&c->em, address, 0, MIR_T_I64, result);
    jump(c, done); mark(c, checked);
    return done;
}

static void array_write(MvpCompiler* c, AstAssignStamNode* node) {
    MIR_label_t updated = modular_array_update(c, node);
    MvpBinding* binding = mvp_local_binding(c->module, node->object);
    MirValue key = expression(c, node->key), input = expression(c, node->value);
    Type* element = mvp_array_element(binding->contract);
    if (c->dense_arrays) {
        MirValue member = convert(c, input, mvp_rep(element));
        ArrayNumElemType lane;
        lambda_array_num_elem_type_for_contract(element, &lane);
        MIR_reg_t address = em_element_address(&c->em, binding->dense_items, key.reg, element == &TYPE_BOOL ? 1 : 8);
        em_store_at(&c->em, address, 0, em_numeric_storage_type(lane), member.reg);
        return;
    }
    MIR_label_t slow = label(c), done = label(c);
    MirValue checked_input = input;
    if (element && key.rep == VALUE_REP_INT_LANE && input.admitted_success_contract &&
            lambda_static_boundary_relation(input.admitted_success_contract, element, false) == STATIC_BOUNDARY_PROVEN) {
        // a direct call's error remains the checked store's error; its admitted success uses the native lane.
        branch(c, MIR_BEQ, slow, reg(c, operation(c, MIR_URSH, reg(c, input.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        input.semantic_contract = input.admitted_success_contract;
        input.semantic_type = input.semantic_contract->type_id;
        input = convert(c, input, mvp_rep(element));
    }
    bool nullable_number = nullable(input) && ((element == &TYPE_FLOAT && input.rep == VALUE_REP_F64) ||
        (element == &TYPE_INT && input.rep == VALUE_REP_INT_LANE));
    bool native = element && key.rep == VALUE_REP_INT_LANE && (nullable_number ||
        lambda_static_boundary_relation(input.semantic_contract, element, false) == STATIC_BOUNDARY_PROVEN);
    bool dynamic = !element && binding->array_writable && key.rep == VALUE_REP_INT_LANE &&
        (input.rep == VALUE_REP_INT_LANE || input.rep == VALUE_REP_ITEM);
    if (native || dynamic) {
        MirValue lane_value;
        if (dynamic) lane_value = value(c, em_guard_finite_int(&c->em, input, slow),
            LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT);
        else { branch_null(c, input, slow); lane_value = convert(c, input, mvp_rep(element)); }
        ArrayNumElemType lane;
        lambda_array_num_elem_type_for_contract(dynamic ? &TYPE_INT : element, &lane);
        branch(c, MIR_BEQ, slow, reg(c, binding->array_writable), imm(c, 0));
        branch(c, MIR_UBGE, slow, reg(c, key.reg), reg(c, binding->array_length));
        int width = element == &TYPE_BOOL ? 1 : 8;
        MIR_reg_t address = c->loop_array == binding ?
            em_element_address(&c->em, c->loop_array_items, key.reg, width) :
            em_array_element_address(&c->em, binding->reg, key.reg, width);
        em_store_at(&c->em, address, 0, em_numeric_storage_type(lane), lane_value.reg);
        jump(c, done);
    }
    mark(c, slow);
    key = convert(c, key, VALUE_REP_ITEM); input = convert(c, checked_input, VALUE_REP_ITEM);
    MIR_reg_t result;
    if (ast_declared_type_is_map(binding->admission_contract) ||
            lambda_type_nonnull_map_contract(binding->admission_contract)) {
        // computed map keys share the checked map writer; array_set_cow
        // cannot publish a map replacement (S9.2.2, D3.2.4v4).
        result = em_call_5(&c->em, "lambda_map_set_checked", MIR_T_I64,
            MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, input.reg),
            MIR_T_P, imm(c, (intptr_t)binding->admission_contract),
            MIR_T_P, imm(c, (intptr_t)binding->entry->name->chars), true);
    } else if (lambda_array_contract_element(binding->contract)) {
        MIR_reg_t type = em_load_const(&c->em, em_add_const(&c->em, binding->contract), MIR_T_P);
        if (!native && element && lambda_array_contract_is_plain(binding->contract) &&
                !lambda_type_counts_array_length(binding->contract)) {
            // compiler-owned certificates need not share the runtime interner's
            // identity. Reuse its primitive carrier proof, not a type walk on
            // every erased-value write (D3.3.3v3, S11.4.1v5).
            MIR_type_t types[] = {MIR_T_I64, MIR_T_I64, MIR_T_I64, MIR_T_P, MIR_T_P,
                MIR_T_P, MIR_T_P, MIR_T_I64, MIR_T_I64};
            MIR_op_t args[] = {reg(c, binding->reg), reg(c, key.reg), reg(c, input.reg), reg(c, type),
                imm(c, (intptr_t)element), imm(c, 0), imm(c, (intptr_t)binding->entry->name->chars), imm(c, 0), imm(c, 1)};
            result = em_call_with_args(&c->em, "lambda_array_set_checked_preplanned", MIR_T_I64, 9, types, args, true);
        } else {
            result = em_call_5(&c->em, "lambda_array_set_checked_item", MIR_T_I64,
                MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, input.reg),
                MIR_T_P, reg(c, type), MIR_T_P, imm(c, (intptr_t)binding->entry->name->chars), true);
        }
    } else {
        // inferred owners may change element kind; the shared COW setter roots and returns their replacement.
        result = em_call_3(&c->em, "array_set_cow", MIR_T_I64,
            MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, input.reg), true);
    }
    MIR_label_t accepted = label(c);
    branch(c, MIR_BNE, accepted, reg(c, operation(c, MIR_URSH, reg(c, result), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
    stage_error(c, result); mark(c, accepted);
    move(c, binding->reg, reg(c, result));
    refresh_array(c, binding);
    if (c->loop_array == binding)
        move(c, c->loop_array_items, reg(c, em_load_at(&c->em, binding->reg,
            LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "refreshed_loop_items")));
    if (binding->var_home) em_store_at(&c->em, binding->var_home, 0, MIR_T_I64, binding->reg);
    mark(c, done);
    if (updated) mark(c, updated);
}

static MirValue literal(MvpCompiler* c, AstPrimaryNode* node) {
    if (node->expr) return expression(c, node->expr);
    Type* t = node->type;
    if (!t) { mvp_reject(c->module, node, "literal has no type"); return null_value(c); }
    MIR_reg_t r = 0;
    if (t->type_id == LMD_TYPE_NULL) return null_value(c);
    if (node->literal_value_kind == AST_PRIMARY_LITERAL_VALUE_INT ||
            node->literal_value_kind == AST_PRIMARY_LITERAL_VALUE_BOOL) r = constant(c, node->literal_value);
    else if (t->is_literal && t->type_id == LMD_TYPE_FLOAT) {
        r = float_constant(c, ((TypeFloat*)t)->double_val);
    } else if (t->is_literal && (t->type_id == LMD_TYPE_STRING || t->type_id == LMD_TYPE_DECIMAL || t->type_id == LMD_TYPE_SYMBOL)) {
        r = em_load_const(&c->em, ((TypeConst*)t)->const_index, MIR_T_P);
    } else if (t->is_literal && t->type_id == LMD_TYPE_NUM_SIZED) {
        TypeNumSized* sized = (TypeNumSized*)t;
        r = constant(c, NUM_SIZED_PACK(sized->num_type, sized->raw_bits));
    } else if (t->is_literal && t->type_id == LMD_TYPE_INT && t != &LIT_INT) {
        r = em_load_at(&c->em, em_load_const(&c->em, ((TypeConst*)t)->const_index, MIR_T_P),
            0, MIR_T_I64, "int_literal");
    }
    if (!r) { mvp_reject(c->module, node, "literal is outside the scalar MVP"); return null_value(c); }
    return value(c, r, t->type_id, mvp_rep(t), t, node);
}

static MIR_reg_t truth(MvpCompiler* c, MirValue v) {
    if (v.semantic_type != LMD_TYPE_BOOL) {
        v = convert(c, v, VALUE_REP_ITEM);
        return em_call_1(&c->em, "is_truthy", MIR_T_I64, MIR_T_I64, reg(c, v.reg), true);
    }
    v = convert(c, v, VALUE_REP_I64);
    return nullable(v) ? operation(c, MIR_EQ, reg(c, v.reg), imm(c, 1)) : v.reg;
}

static MIR_reg_t zero_residue(MvpCompiler* c, AstBinaryNode* n) {
    AstNode* residue = ast_unwrap_primary_to_leaf(n->left);
    int64_t zero, divisor;
    if ((n->op != OPERATOR_EQ && n->op != OPERATOR_NE) || !residue || residue->node_type != AST_NODE_BINARY ||
            ((AstBinaryNode*)residue)->op != OPERATOR_MOD ||
            !mir_decimal_int_literal(c->module->tp->source, n->right, &zero) || zero != 0) return 0;
    AstBinaryNode* mod = (AstBinaryNode*)residue;
    if (!mir_decimal_int_literal(c->module->tp->source, mod->right, &divisor) ||
            !positive_power_two(divisor) || !mvp_int_range(c->module, mod->left).finite) return 0;
    // zero residue is a bit test for both signs; the signed remainder never escapes (S6.1.2v2).
    MirValue a = expression(c, mod->left);
    return operation(c, MIR_AND, reg(c, a.reg), imm(c, divisor - 1));
}

static unsigned integer_comparison_guard(Operator op, bool left_finite, bool right_finite) {
    // NaN is the least signed lane. The other operand's ordered predicate
    // already rejects it; equality needs only one witness (S4.2.1).
    if (op == OPERATOR_EQ || op == OPERATOR_NE) return left_finite || right_finite ? 0 : 1;
    if (op == OPERATOR_LT || op == OPERATOR_LE) return left_finite ? 0 : 1;
    if (op == OPERATOR_GT || op == OPERATOR_GE) return right_finite ? 0 : 2;
    return 3;
}

static void boolean_branch_body(MvpCompiler*, AstNode*, MIR_label_t, bool);
static MIR_label_t guarded_array_condition(MvpCompiler*, AstNode*, MIR_label_t, bool);
static bool float_array_comparison(MvpCompiler*, AstBinaryNode*, MirValue*, MIR_label_t = NULL, bool = false);

static void boolean_branch(MvpCompiler* c, AstNode* source, MIR_label_t target, bool when_true) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    if (node && node->node_type == AST_NODE_BINARY &&
            float_array_comparison(c, (AstBinaryNode*)node, NULL, target, when_true)) return;
    if (node && node->node_type == AST_NODE_BINARY &&
            (((AstBinaryNode*)node)->op == OPERATOR_AND || ((AstBinaryNode*)node)->op == OPERATOR_OR)) {
        // guard each selected operand: a whole-condition version evaluates
        // immutable keys in skipped arms and repeats them in the hot body.
        boolean_branch_body(c, source, target, when_true);
        return;
    }
    MIR_label_t done = guarded_array_condition(c, source, target, when_true);
    boolean_branch_body(c, source, target, when_true);
    if (done) mark(c, done);
}

static void boolean_branch_body(MvpCompiler* c, AstNode* source, MIR_label_t target, bool when_true) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    bool optional = false;
    Type* contract = lambda_type_nullable_lane_base(mvp_value_contract(c->module, node), &optional);
    if (node && node->node_type == AST_NODE_UNARY && ((AstUnaryNode*)node)->op == OPERATOR_NOT) {
        // predicate demand complements the selected branch; errors still
        // reach the lexical condition target before truth is consumed (S3.4).
        boolean_branch(c, ((AstUnaryNode*)node)->operand, target, !when_true);
        return;
    }
    if (node && node->node_type == AST_NODE_BINARY &&
            (((AstBinaryNode*)node)->op == OPERATOR_AND || ((AstBinaryNode*)node)->op == OPERATOR_OR)) {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        bool conjunction = binary->op == OPERATOR_AND;
        // branch demand needs truth, not the selected Item. Each leaf still
        // propagates an encountered error and preserves short circuiting (S3.4).
        if (conjunction != when_true) {
            boolean_branch(c, binary->left, target, when_true);
            boolean_branch(c, binary->right, target, when_true);
        } else {
            MIR_label_t skipped = label(c);
            boolean_branch(c, binary->left, skipped, !when_true);
            boolean_branch(c, binary->right, target, when_true);
            mark(c, skipped);
        }
        return;
    }
    if (node && node->node_type == AST_NODE_BINARY && contract && contract->type_id == LMD_TYPE_BOOL) {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        MirValue character_left, character_right;
        if (string_comparison_inputs(c, binary, &character_left, &character_right)) {
            branch(c, (binary->op == OPERATOR_EQ) == when_true ? MIR_BEQ : MIR_BNE,
                target, reg(c, character_left.reg), reg(c, character_right.reg));
            return;
        }
        if (MIR_reg_t masked = zero_residue(c, binary)) {
            branch(c, (binary->op == OPERATOR_EQ) == when_true ? MIR_BEQ : MIR_BNE,
                target, reg(c, masked), imm(c, 0));
            return;
        }
        TypeId left = mvp_value_contract(c->module, binary->left, 0, c->dense_arrays)->type_id;
        TypeId right = mvp_value_contract(c->module, binary->right, 0, c->dense_arrays)->type_id;
        bool equality = binary->op == OPERATOR_EQ || binary->op == OPERATOR_NE;
        bool ints = left == LMD_TYPE_INT && right == LMD_TYPE_INT;
        bool floats = (left == LMD_TYPE_FLOAT || left == LMD_TYPE_INT) &&
            (right == LMD_TYPE_FLOAT || right == LMD_TYPE_INT) && !ints;
        bool booleans = left == LMD_TYPE_BOOL && right == LMD_TYPE_BOOL;
        bool finite_left = ints && mvp_int_range(c->module, binary->left).finite;
        bool finite_right = ints && mvp_int_range(c->module, binary->right).finite;
        if (booleans || ints || (floats && (when_true || equality))) {
            MIR_insn_code_t op = MIR_MOV;
            switch (binary->op) {
            case OPERATOR_EQ: op = floats ? MIR_DBEQ : MIR_BEQ; break;
            case OPERATOR_NE: op = floats ? MIR_DBNE : MIR_BNE; break;
            case OPERATOR_LT: op = floats ? MIR_DBLT : MIR_BLT; break;
            case OPERATOR_LE: op = floats ? MIR_DBLE : MIR_BLE; break;
            case OPERATOR_GT: op = floats ? MIR_DBGT : MIR_BGT; break;
            case OPERATOR_GE: op = floats ? MIR_DBGE : MIR_BGE; break;
            default: break;
            }
            if (op != MIR_MOV) {
                MirValue a = expression(c, binary->left), b = expression(c, binary->right);
                ValueRep rep = floats ? VALUE_REP_F64 : ints ? VALUE_REP_INT_LANE : VALUE_REP_I64;
                a = convert(c, a, rep); b = convert(c, b, rep);
                unsigned guard = ints ? integer_comparison_guard(binary->op, finite_left, finite_right) : 0;
                MIR_label_t unordered = NULL;
                if (guard) {
                    // branch demand consumes unorderedness directly; no boolean
                    // mask needs to survive into the ordered comparison (S6.1.2v2).
                    bool nan_result = binary->op == OPERATOR_NE;
                    MIR_label_t nan_target = when_true == nan_result ? target : (unordered = label(c));
                    branch(c, MIR_BEQ, nan_target, reg(c, guard == 1 ? a.reg : b.reg), imm(c, INT_LANE_NAN));
                }
                // ordered float predicates cannot be complemented when NaN is possible (S4.2.1).
                if (!when_true) op = floats ? (op == MIR_DBEQ ? MIR_DBNE : MIR_DBEQ) : MIR_reverse_branch_code(op);
                branch(c, op, target, reg(c, a.reg), reg(c, b.reg));
                if (unordered) mark(c, unordered);
                return;
            }
        }
    }
    // branch demand consumes boolean short-circuiting without a materialized phi (S3.4).
    MirValue v = {};
    if (node && node->node_type == AST_NODE_INDEX_EXPR && contract && contract->type_id == LMD_TYPE_BOOL &&
            mvp_index_key(c->module, ((AstFieldNode*)node)->field)) {
        // consume the selected byte or checked absence at its branch target;
        // value-demand reads keep the ordinary nullable Boolean join.
        v = array_read(c, (AstFieldNode*)node, target, when_true);
        if (!v.reg) return;
    } else v = expression(c, source);
    if (c->condition_failure && v.semantic_type == LMD_TYPE_ANY) {
        v = convert(c, v, VALUE_REP_ITEM);
        MIR_label_t valid = label(c);
        branch(c, MIR_BNE, valid, reg(c, operation(c, MIR_URSH, reg(c, v.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        move(c, c->condition_failure->error, reg(c, v.reg)); jump(c, c->condition_failure->done); mark(c, valid);
    }
    branch(c, when_true ? MIR_BNE : MIR_BEQ, target, reg(c, truth(c, v)), imm(c, 0));
}

static MIR_label_t indexed_loop(MvpCompiler* c, AstLoopControlNode* node) {
    MvpIndexLoopPlan plan = {};
    if (c->dense_arrays || !mvp_index_loop_plan(c->module, node, &plan) || !plan.array->array_writable) return NULL;
    LambdaMvpModule* m = c->module;
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) return NULL;
    MIR_label_t checked = label(c);
    MvpControl loop = {c->control, label(c), label(c)};
    branch(c, MIR_BEQ, checked, reg(c, plan.array->array_writable), imm(c, 0));
    branch(c, MIR_UBGT, checked, reg(c, plan.array->array_length), imm(c, INT53_MAX - 1));
    branch(c, MIR_UBGT, checked, reg(c, plan.limit->reg), reg(c, plan.array->array_length));
    branch(c, MIR_UBGT, checked, reg(c, plan.counter->reg), reg(c, plan.limit->reg));
    if (plan.follower) branch(c, MIR_UBGT, checked, reg(c, plan.follower->reg), reg(c, plan.counter->reg));
    plan.array->dense_items = em_load_at(&c->em, plan.array->reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "indexed_items");
    plan.counter->range = plan.limit->range = {true, 0, INT53_MAX - 1};
    if (plan.follower) plan.follower->range = {true, 0, INT53_MAX - 1};
    c->control = &loop; c->dense_arrays = true;
    uint32_t calls = c->em.frame.may_gc_call_count;
    mark(c, loop.next);
    boolean_branch(c, node->cond, loop.done, false);
    expression(c, node->body, true); jump(c, loop.next);
    if (c->em.frame.may_gc_call_count != calls)
        mvp_reject(m, node, "guarded indexed iteration emitted a collecting call");
    c->dense_arrays = false; c->control = loop.parent;
    restore_bindings(m, saved); mem_free(saved);
    mark(c, checked);
    return loop.done;
}

static MIR_label_t numeric_loop(MvpCompiler* c, AstLoopControlNode* node) {
    MvpNumericLoopPlan plan = {};
    if (!mvp_numeric_loop_plan(c->module, node, &plan)) return NULL;
    MvpControl loop = {c->control, label(c), label(c)};
    MIR_label_t checked = label(c);
    mark(c, loop.next);
    for (uint32_t i = 0; i < plan.count; i++) {
        MvpBinding* b = plan.guards[i];
        MIR_reg_t offset = operation(c, MIR_ADD, reg(c, b->reg), imm(c, plan.radius));
        branch(c, MIR_UBGT, checked, reg(c, offset), imm(c, 2 * plan.radius));
        b->range = {true, -plan.radius, plan.radius};
    }
    c->control = &loop;
    uint32_t calls = c->em.frame.may_gc_call_count;
    // amortize the guards over a small group, retaining every source test and
    // the exact statement order. Interval transfer proves all four iterations.
    for (unsigned i = 0; i < plan.iterations; i++) {
        boolean_branch(c, node->cond, loop.done, false);
        expression(c, node->body, true);
        if (!mvp_numeric_region_advance(c->module, node, &plan))
            mvp_reject(c->module, node, "numeric iteration proof changed during emission");
    }
    jump(c, loop.next);
    if (c->em.frame.may_gc_call_count != calls)
        mvp_reject(c->module, node, "finite numeric iteration emitted a collecting call");
    // on a miss, resume the checked loop with the current values; no completed
    // iteration is replayed and every original error/saturation path remains.
    for (uint32_t i = 0; i < plan.count; i++) plan.guards[i]->range = {};
    mem_free(plan.guards);
    c->control = loop.parent;
    mark(c, checked);
    return loop.done;
}

static MIR_reg_t finite_int_arithmetic(MvpCompiler* c, AstBinaryNode* n, MirValue a, MirValue b,
        bool checked_multiply = false) {
    bool division = n->op == OPERATOR_IDIV || n->op == OPERATOR_MOD;
    MvpIntRange divisor = division ? mvp_int_range(c->module, n->right) : MvpIntRange{};
    if (divisor.finite && divisor.lower == divisor.upper && positive_power_two(divisor.lower)) {
        MvpIntRange dividend = mvp_int_range(c->module, n->left);
        MIR_reg_t bias = 0, adjusted = a.reg;
        if (!dividend.finite || dividend.lower < 0) {
            // finite negative dividends need a bias to retain truncation toward zero (S4.1.2).
            bias = operation(c, MIR_AND, reg(c, operation(c, MIR_RSH, reg(c, a.reg), imm(c, 63))),
                imm(c, divisor.lower - 1));
            adjusted = operation(c, MIR_ADD, reg(c, a.reg), reg(c, bias));
        }
        if (n->op == OPERATOR_MOD) {
            MIR_reg_t remainder = operation(c, MIR_AND, reg(c, adjusted), imm(c, divisor.lower - 1));
            return bias ? operation(c, MIR_SUB, reg(c, remainder), reg(c, bias)) : remainder;
        }
        unsigned shift = 0;
        for (int64_t remaining = divisor.lower; remaining > 1; remaining >>= 1) shift++;
        return operation(c, MIR_RSH, reg(c, adjusted), imm(c, shift));
    }
    MIR_insn_code_t op = n->op == OPERATOR_ADD ? MIR_ADD : n->op == OPERATOR_SUB ? MIR_SUB :
        n->op == OPERATOR_MUL ? (checked_multiply ? MIR_MULO : MIR_MUL) :
        n->op == OPERATOR_IDIV ? MIR_DIV : MIR_MOD;
    return operation(c, op, reg(c, a.reg), reg(c, b.reg));
}

static MirValue int_arithmetic(MvpCompiler* c, AstBinaryNode* n, MirValue a, MirValue b, bool finite_inputs = false) {
    if (mvp_int_range(c->module, n).finite) {
        return value(c, finite_int_arithmetic(c, n, a, b),
            LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, n);
    }
    MIR_reg_t r = em_new_reg(&c->em, "int_result", MIR_T_I64);
    MIR_label_t slow = label(c), done = label(c);
    MvpIntRange left_range = mvp_int_range(c->module, (AstNode*)a.provenance_node);
    MvpIntRange right_range = mvp_int_range(c->module, (AstNode*)b.provenance_node);
    bool left_finite = finite_inputs || left_range.finite;
    bool right_finite = finite_inputs || right_range.finite;
    bool add_sub = n->op == OPERATOR_ADD || n->op == OPERATOR_SUB;
    // a finite add/sub result plus one finite operand excludes every sentinel partner (S4.1.1).
    if (!add_sub) {
        // a nonzero finite multiplier cannot hide a sentinel behind a finite,
        // non-overflowing product; zero must still preserve 0 * inf/nan (S4.1.2).
        bool left_implied = n->op == OPERATOR_MUL && right_range.finite &&
            (right_range.lower > 0 || right_range.upper < 0);
        bool right_implied = n->op == OPERATOR_MUL && left_range.finite &&
            (left_range.lower > 0 || left_range.upper < 0);
        if (!left_finite && !left_implied) em_branch_int53_outside(&c->em, a.reg, slow);
        if (!right_finite && !right_implied) em_branch_int53_outside(&c->em, b.reg, slow);
    }
    bool division = n->op == OPERATOR_IDIV || n->op == OPERATOR_MOD;
    if (division) branch(c, MIR_BEQ, slow, reg(c, b.reg), imm(c, 0));
    MIR_reg_t native = finite_int_arithmetic(c, n, a, b, true);
    if (n->op == OPERATOR_MUL)
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_BO, MIR_new_label_op(c->em.ctx, slow)));
    move(c, r, reg(c, native));
    if (!division) em_branch_int53_outside(&c->em, r, slow);
    if (add_sub && !left_finite && !right_finite) em_branch_int53_outside(&c->em, a.reg, slow);
    jump(c, done); mark(c, slow);
    MIR_reg_t fallback;
    if (division) fallback = em_call_3(&c->em, "lambda_int_lane_divmod_slow", MIR_T_I64,
        MIR_T_I64, reg(c, a.reg), MIR_T_I64, reg(c, b.reg), MIR_T_I64, imm(c, n->op == OPERATOR_MOD), true);
    else fallback = em_call_2(&c->em, n->op == OPERATOR_ADD ? "lambda_int_lane_add_slow" :
        n->op == OPERATOR_SUB ? "lambda_int_lane_sub_slow" : "lambda_int_lane_mul_slow", MIR_T_I64,
        MIR_T_I64, reg(c, a.reg), MIR_T_I64, reg(c, b.reg), true);
    move(c, r, reg(c, fallback)); mark(c, done);
    return value(c, r, LMD_TYPE_INT, VALUE_REP_INT_LANE,
        nullable(a) || nullable(b) ? c->module->nullable_int : &TYPE_INT, n);
}

static MirValue numeric_binary(MvpCompiler* c, AstBinaryNode* n, MirValue a, MirValue b, bool finite_inputs = false) {
    bool booleans = a.semantic_type == LMD_TYPE_BOOL && b.semantic_type == LMD_TYPE_BOOL;
    bool ints = a.semantic_type == LMD_TYPE_INT && b.semantic_type == LMD_TYPE_INT && n->op != OPERATOR_DIV;
    ValueRep rep = booleans ? VALUE_REP_I64 : ints ? VALUE_REP_INT_LANE : VALUE_REP_F64;
    bool nullable_float = rep == VALUE_REP_F64 && (nullable(a) || nullable(b));
    MIR_label_t absent = label(c), done = label(c);
    // Test absence before widening an int lane: its null sentinel is not a double.
    if (nullable_float) { branch_null(c, a, absent); branch_null(c, b, absent); }
    a = convert(c, a, rep); b = convert(c, b, rep);
    MirNumericOpPlan plan = {};
    if (ints && (n->op == OPERATOR_ADD || n->op == OPERATOR_SUB || n->op == OPERATOR_MUL ||
            n->op == OPERATOR_IDIV || n->op == OPERATOR_MOD)) return int_arithmetic(c, n, a, b, finite_inputs);
    if (!em_numeric_op_plan(n->op, &plan, ints)) {
        mvp_reject(c->module, n, "numeric operator is outside the scalar MVP"); return null_value(c);
    }
    MIR_reg_t r;
    MIR_type_t result_type = plan.is_comparison || booleans ? MIR_T_I64 : MIR_T_D;
    MvpIntRange divisor = n->op == OPERATOR_DIV ? mvp_int_range(c->module, n->right) : MvpIntRange{};
    bool power_two = divisor.finite && divisor.lower == divisor.upper && positive_power_two(divisor.lower);
    // An exact power-of-two reciprocal preserves IEEE rounding, signed zero
    // and subnormals; this does not reassociate any floating expression (S4.3).
    if (power_two) r = operation(c, MIR_DMUL, reg(c, a.reg),
        reg(c, float_constant(c, 1.0 / (double)divisor.lower)), MIR_T_D);
    else if (plan.helper_name) r = em_call_2(&c->em, plan.helper_name, MIR_T_D,
        MIR_T_D, reg(c, a.reg), MIR_T_D, reg(c, b.reg), true);
    else r = operation(c, ints || booleans ? plan.i64_opcode : plan.f64_opcode,
        reg(c, a.reg), reg(c, b.reg), result_type);
    if (ints && plan.is_comparison) {
        bool left_finite = finite_inputs || mvp_int_range(c->module, n->left).finite;
        bool right_finite = finite_inputs || mvp_int_range(c->module, n->right).finite;
        unsigned guard = integer_comparison_guard(n->op, left_finite, right_finite);
        if (guard) {
            // predicate masks avoid control-flow joins in every native integer comparison.
            MIR_reg_t ordered = operation(c, MIR_NE, reg(c, guard == 1 ? a.reg : b.reg), imm(c, INT_LANE_NAN));
            if (n->op == OPERATOR_NE) {
                MIR_reg_t unordered = operation(c, MIR_EQ, reg(c, ordered), imm(c, 0));
                r = operation(c, MIR_OR, reg(c, r), reg(c, unordered));
            } else r = operation(c, MIR_AND, reg(c, r), reg(c, ordered));
        }
    }
    if (nullable_float) {
        jump(c, done); mark(c, absent); move(c, r, reg(c, null_float(c)), MIR_T_D); mark(c, done);
    }
    return value(c, r, plan.is_comparison ? LMD_TYPE_BOOL : LMD_TYPE_FLOAT,
        plan.is_comparison ? VALUE_REP_I64 : VALUE_REP_F64,
        plan.is_comparison ? &TYPE_BOOL : nullable_float ? c->module->nullable_float : &TYPE_FLOAT, n);
}

static TypeId numeric_success(MirValue v) {
    Type* t = v.admitted_success_contract ? v.admitted_success_contract : v.semantic_contract;
    if (v.rep == VALUE_REP_INT_LANE) return LMD_TYPE_INT;
    if (v.rep == VALUE_REP_F64) return LMD_TYPE_FLOAT;
    return t ? t->type_id : v.semantic_type;
}

static MirValue nullable_comparison(MvpCompiler* c, AstBinaryNode* n, MirValue a, MirValue b) {
    bool equality = n->op == OPERATOR_EQ || n->op == OPERATOR_NE;
    MIR_reg_t missing_a = number_null_flag(c, a), missing_b = number_null_flag(c, b);
    MIR_reg_t r = em_new_reg(&c->em, "nullable_compare", MIR_T_I64);
    MIR_label_t absent = label(c), done = label(c);
    branch(c, MIR_BNE, absent, reg(c, missing_a), imm(c, 0));
    branch(c, MIR_BNE, absent, reg(c, missing_b), imm(c, 0));
    a.semantic_contract = a.rep == VALUE_REP_F64 ? &TYPE_FLOAT : &TYPE_INT;
    b.semantic_contract = b.rep == VALUE_REP_F64 ? &TYPE_FLOAT : &TYPE_INT;
    move(c, r, reg(c, numeric_binary(c, n, a, b).reg)); jump(c, done);
    mark(c, absent);
    // equality compares absence; ordered relations propagate it (S5.1, S6.1.2v2).
    move(c, r, equality ? reg(c, operation(c, n->op == OPERATOR_EQ ? MIR_EQ : MIR_NE,
        reg(c, missing_a), reg(c, missing_b))) : imm(c, BOOL_ERROR));
    mark(c, done);
    return value(c, r, LMD_TYPE_BOOL, VALUE_REP_I64,
        equality ? &TYPE_BOOL : c->module->nullable_bool, n);
}

static MIR_reg_t scaled_integer(MvpCompiler* c, AstNode* node) {
    if (mvp_int_range(c->module, node).finite) return expression(c, node).reg;
    AstBinaryNode* n = (AstBinaryNode*)ast_unwrap_primary_to_leaf(node);
    MIR_reg_t a = scaled_integer(c, n->left);
    if (n->op == OPERATOR_DIV) {
        unsigned before = mvp_scaled_integer(c->module, n->left).fraction_bits;
        for (int64_t d = mvp_int_range(c->module, n->right).lower; d > 1; d >>= 1) before++;
        unsigned after = mvp_scaled_integer(c->module, n).fraction_bits;
        return before == after ? a : operation(c, MIR_RSH, reg(c, a), imm(c, before - after));
    }
    MIR_reg_t b = scaled_integer(c, n->right);
    unsigned left = mvp_scaled_integer(c->module, n->left).fraction_bits;
    unsigned right = mvp_scaled_integer(c->module, n->right).fraction_bits;
    if (left < right) a = operation(c, MIR_LSH, reg(c, a), imm(c, right - left));
    if (right < left) b = operation(c, MIR_LSH, reg(c, b), imm(c, left - right));
    return operation(c, n->op == OPERATOR_ADD ? MIR_ADD : MIR_SUB, reg(c, a), reg(c, b));
}

static MirValue string_character(MvpCompiler* c, AstFieldNode* n, bool ordinal) {
    MvpBinding* binding = mvp_local_binding(c->module, n->object);
    if (binding && binding->dense_items) {
        // the closed loop guards ASCII, both index endpoints and owner stability.
        MIR_reg_t byte = 0;
        for (MvpStringByte* p = c->string_bytes; p; p = p->next)
            if (p->owner == binding && mvp_same_integer_expression(c->module, p->key, n->field)) { byte = p->byte; break; }
        if (!byte) byte = em_ascii_string_byte_at_pointer(&c->em, binding->dense_items, expression(c, n->field).reg);
        MIR_reg_t word = ordinal ? byte : operation(c, MIR_OR, reg(c, byte), imm(c, UINT64_C(1) << 32));
        return value(c, word, LMD_TYPE_INT, ordinal ? VALUE_REP_INT_LANE : VALUE_REP_I64, &TYPE_INT, n);
    }
    MirValue owner = binding && mvp_pure_key(n->field) ? read_binding(c, binding, n->object, false) : expression(c, n->object);
    MirValue key = expression(c, n->field);
    owner = convert(c, owner, VALUE_REP_RAW_GC_POINTER);
    MIR_reg_t result = em_new_reg(&c->em, "character", MIR_T_I64);
    MIR_label_t slow = label(c), done = label(c);
    MIR_reg_t byte = em_ascii_string_byte(&c->em, owner.reg, key.reg, slow);
    // S7.1.1v3: a byte plus its length distinguishes NUL from an absent character.
    move(c, result, ordinal ? reg(c, byte) : reg(c, operation(c, MIR_OR, reg(c, byte), imm(c, UINT64_C(1) << 32))));
    jump(c, done); mark(c, slow);
    owner = convert(c, owner, VALUE_REP_ITEM);
    MIR_reg_t fallback = em_call_2(&c->em, ordinal ? "fn_string_ord_at" : "fn_string_char_word", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, key.reg), true);
    if (ordinal) fallback = convert(c, value(c, fallback, LMD_TYPE_INT, VALUE_REP_ITEM,
        c->module->nullable_int), VALUE_REP_INT_LANE).reg;
    move(c, result, reg(c, fallback)); mark(c, done);
    return value(c, result, LMD_TYPE_INT, ordinal ? VALUE_REP_INT_LANE : VALUE_REP_I64,
        ordinal ? c->module->nullable_int : &TYPE_INT, n);
}

static bool string_comparison_inputs(MvpCompiler* c, AstBinaryNode* n, MirValue* left, MirValue* right) {
    if (n->op != OPERATOR_EQ && n->op != OPERATOR_NE) return false;
    AstFieldNode* a = mvp_string_index(c->module, n->left);
    AstFieldNode* b = mvp_string_index(c->module, n->right);
    if (!a || !b) return false;
    MvpBinding* x = mvp_local_binding(c->module, a->object);
    MvpBinding* y = mvp_local_binding(c->module, b->object);
    // present ASCII bytes need no presence word; checked siblings keep NUL
    // distinct from absence and compare complete Unicode code points (S7.1.1v3).
    bool bytes = x && y && x->dense_items && y->dense_items;
    *left = string_character(c, a, bytes); *right = string_character(c, b, bytes);
    return true;
}

static MirValue binary(MvpCompiler* c, AstBinaryNode* n);

static bool float_array_comparison(MvpCompiler* c, AstBinaryNode* n, MirValue* output,
        MIR_label_t target, bool when_true) {
    MirNumericOpPlan plan = {};
    if (c->checked_float_compare || !em_numeric_op_plan(n->op, &plan) || !plan.is_comparison) return false;
    AstNode* array_side = ast_unwrap_primary_to_leaf(n->left);
    AstNode* other = n->right;
    bool reversed = !array_side || array_side->node_type != AST_NODE_INDEX_EXPR;
    if (reversed) { array_side = ast_unwrap_primary_to_leaf(n->right); other = n->left; }
    MvpBinding* threshold_binding = mvp_local_binding(c->module, other);
    bool float_outcome = threshold_binding && threshold_binding->producer_success_contract == &TYPE_FLOAT;
    if (!array_side || array_side->node_type != AST_NODE_INDEX_EXPR || !mvp_pure_key(other) ||
            (mvp_value_contract(c->module, other)->type_id != LMD_TYPE_FLOAT && !float_outcome)) return false;
    AstFieldNode* access = (AstFieldNode*)array_side;
    MvpBinding* binding = mvp_local_binding(c->module, access->object);
    if (!binding || binding->rep != VALUE_REP_ITEM || mvp_array_element(binding->contract) ||
            !mvp_pure_key(access->field) ||
            mvp_value_contract(c->module, access->field)->type_id != LMD_TYPE_INT) return false;
    MIR_label_t checked = label(c), done = label(c);
    MirValue owner = read_binding(c, binding, access->object, false);
    MirValue key = expression(c, access->field);
    MIR_reg_t pointer = owner.reg;
    if (binding->owner == c->function->id && binding->array_float_native) {
        branch(c, MIR_BEQ, checked, reg(c, binding->array_float_native), imm(c, 0));
        branch(c, MIR_UBGE, checked, reg(c, key.reg), reg(c, binding->array_length));
    } else {
        pointer = em_guard_container(&c->em, owner.reg, LMD_TYPE_ARRAY_NUM, checked);
        em_array_num_elem_guard(&c->em, pointer, ELEM_FLOAT, checked);
        em_array_num_bounds_check(&c->em, pointer, key.reg, checked);
    }
    MIR_reg_t address = em_array_element_address(&c->em, pointer, key.reg, 8);
    MIR_reg_t member = em_load_at(&c->em, address, 0, MIR_T_D, "compared_float");
    // ArrayNum float slots are non-nullable; nullable native lanes use Array,
    // which the carrier guard excludes (D2.4.3, D3.3.3v3).
    MirValue threshold = expression(c, other);
    if (float_outcome && threshold.rep == VALUE_REP_ITEM) {
        // an unchanged float/error threshold needs one outcome guard, not a
        // generic comparison. Replay still owns errors and absence (S6.1.2v2).
        branch(c, MIR_BEQ, checked, reg(c, operation(c, MIR_URSH,
            reg(c, threshold.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        threshold.semantic_type = LMD_TYPE_FLOAT;
        threshold.semantic_contract = &TYPE_FLOAT;
    }
    threshold = convert(c, threshold, VALUE_REP_F64);
    MIR_reg_t predicate = operation(c, plan.f64_opcode, reg(c, reversed ? threshold.reg : member),
        reg(c, reversed ? member : threshold.reg));
    MIR_reg_t result = 0;
    if (output) result = operation(c, MIR_OR, reg(c, predicate), imm(c, (uint64_t)LMD_TYPE_BOOL << 56));
    else branch(c, when_true ? MIR_BNE : MIR_BEQ, target, reg(c, predicate), imm(c, 0));
    jump(c, done); mark(c, checked);
    // pure operands permit replay before any effect. Alternate carriers,
    // absence and errors retain the original comparison (S6.1.2v2, S7.1.1v3).
    c->checked_float_compare = true;
    if (output) {
        MirValue fallback = convert(c, binary(c, n), VALUE_REP_ITEM);
        move(c, result, reg(c, fallback.reg));
    } else boolean_branch_body(c, n, target, when_true);
    c->checked_float_compare = false;
    mark(c, done);
    if (output) *output = value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
    return true;
}

static int decimal_operation(Operator op) {
    static const struct { Operator op; LambdaNumericOpFamily family; } operations[] = {
        {OPERATOR_ADD, LAMBDA_NUM_OP_ADD}, {OPERATOR_SUB, LAMBDA_NUM_OP_SUB},
        {OPERATOR_MUL, LAMBDA_NUM_OP_MUL}, {OPERATOR_DIV, LAMBDA_NUM_OP_TRUE_DIV},
        {OPERATOR_IDIV, LAMBDA_NUM_OP_IDIV}, {OPERATOR_MOD, LAMBDA_NUM_OP_MOD},
    };
    for (const auto& operation : operations) if (operation.op == op) return operation.family;
    return -1;
}

static MirValue binary_operand(MvpCompiler* c, AstNode* source, AstNode* other, Operator op) {
    MvpBinding* binding = mvp_local_binding(c->module, source);
    // a pure null predicate never retains its operand. Borrow the owner while
    // leaving real binding/return snapshots intact (S9.1.2).
    bool observes = (op == OPERATOR_EQ || op == OPERATOR_NE) && mvp_pure_key(other) &&
        mvp_value_contract(c->module, other)->type_id == LMD_TYPE_NULL;
    return binding && observes ? read_binding(c, binding, source, false) : expression(c, source);
}

static MirValue binary(MvpCompiler* c, AstBinaryNode* n) {
    if (n->op == OPERATOR_IS_NAN) {
        MirValue subject = convert(c, expression(c, n->left), VALUE_REP_ITEM);
        return value(c, em_call_1(&c->em, "fn_is_nan", MIR_T_I64, MIR_T_I64, reg(c, subject.reg), true),
            LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
    }
    if (n->op == OPERATOR_IS) {
        Type* type = mvp_match_type(n->right);
        if (!type && n->right->type && n->right->type->type_id == LMD_TYPE_FLOAT && n->right->type->is_literal &&
                isnan(((TypeFloat*)n->right->type)->double_val)) {
            MirValue subject = convert(c, expression(c, n->left), VALUE_REP_ITEM);
            return value(c, em_call_1(&c->em, "fn_is_nan", MIR_T_I64, MIR_T_I64, reg(c, subject.reg), true),
                LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
        }
        if (!type) { mvp_reject(c->module, n, "membership requires a base-type contract"); return null_value(c); }
        MirValue subject = convert(c, expression(c, n->left), VALUE_REP_ITEM);
        return value(c, em_call_2(&c->em, "lambda_value_type_is_exact", MIR_T_I64,
            MIR_T_I64, reg(c, subject.reg), MIR_T_P, imm(c, (intptr_t)type), true),
            LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
    }
    MirValue compared = {};
    if (float_array_comparison(c, n, &compared)) return compared;
    MirValue character_left, character_right;
    if (string_comparison_inputs(c, n, &character_left, &character_right))
        return value(c, operation(c, n->op == OPERATOR_EQ ? MIR_EQ : MIR_NE,
            reg(c, character_left.reg), reg(c, character_right.reg)), LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
    if (n->op == OPERATOR_JOIN) {
        MirValue joined = string_concat(c, n);
        if (joined.reg) return joined;
    }
    MvpScaledInteger scaled = mvp_scaled_integer(c->module, n);
    if (scaled.numerator.finite &&
            mvp_value_contract(c->module, n)->type_id == LMD_TYPE_FLOAT) {
        MIR_reg_t numerator = scaled_integer(c, n), r = em_new_reg(&c->em, "exact_fraction", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, r), reg(c, numerator)));
        if (scaled.fraction_bits) r = operation(c, MIR_DMUL, reg(c, r),
            reg(c, float_constant(c, 1.0 / (double)(UINT64_C(1) << scaled.fraction_bits))), MIR_T_D);
        return value(c, r, LMD_TYPE_FLOAT, VALUE_REP_F64, &TYPE_FLOAT, n);
    }
    if (MIR_reg_t masked = zero_residue(c, n))
        return value(c, operation(c, n->op == OPERATOR_EQ ? MIR_EQ : MIR_NE, reg(c, masked), imm(c, 0)),
            LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
    MirValue a = binary_operand(c, n->left, n->right, n->op);
    if (n->op == OPERATOR_AND || n->op == OPERATOR_OR) {
        bool right_null = false;
        Type* right_base = lambda_type_nullable_lane_base(mvp_value_contract(c->module, n->right, 0, c->dense_arrays), &right_null);
        bool booleans = a.semantic_type == LMD_TYPE_BOOL && right_base && right_base->type_id == LMD_TYPE_BOOL;
        // keep the selected bool? sentinel intact; logical selection does not
        // turn an encountered absence into false (S3.4).
        Type* logical_contract = nullable(a) || right_null ? c->module->nullable_bool : &TYPE_BOOL;
        MIR_reg_t r = em_new_reg(&c->em, "logical", MIR_T_I64);
        if (!booleans) a = convert(c, a, VALUE_REP_ITEM);
        move(c, r, reg(c, a.reg));
        MIR_label_t done = label(c);
        if (!booleans) branch(c, MIR_BEQ, done,
            reg(c, operation(c, MIR_URSH, reg(c, r), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        branch(c, n->op == OPERATOR_AND ? MIR_BEQ : MIR_BNE, done, reg(c, truth(c, a)), imm(c, 0));
        MirValue b = expression(c, n->right);
        b = convert(c, b, booleans ? VALUE_REP_I64 : VALUE_REP_ITEM);
        move(c, r, reg(c, b.reg)); mark(c, done);
        return value(c, r, booleans ? LMD_TYPE_BOOL : LMD_TYPE_ANY,
            booleans ? VALUE_REP_I64 : VALUE_REP_ITEM, booleans ? logical_contract : &TYPE_ANY, n);
    }
    MirValue b = binary_operand(c, n->right, n->left, n->op);
    if (n->op == OPERATOR_AT || n->op == OPERATOR_IN || n->op == OPERATOR_OF) {
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        MIR_reg_t result = em_new_reg(&c->em, "membership", MIR_T_I64);
        MIR_label_t done = label(c), invalid = label(c);
        // membership's C ABI returns a byte, while errors retain their Item
        // identity in left-to-right order (S7.9.3, S8.1.1v2).
        MirValue operands[] = {a, b};
        for (MirValue operand : operands) {
            if (!lambda_type_accepts_error(operand.semantic_contract)) continue;
            move(c, result, reg(c, operand.reg));
            branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_URSH, reg(c, operand.reg), imm(c, 56))),
                imm(c, LMD_TYPE_ERROR));
        }
        const char* helper = n->op == OPERATOR_AT ? "fn_at" : n->op == OPERATOR_IN ? "fn_in" : "fn_of";
        MIR_reg_t raw = em_call_2(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, a.reg), MIR_T_I64, reg(c, b.reg), true);
        raw = operation(c, MIR_AND, reg(c, raw), imm(c, UINT8_MAX));
        branch(c, MIR_BGE, invalid, reg(c, raw), imm(c, BOOL_ERROR));
        move(c, result, reg(c, convert(c, value(c, raw, LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL), VALUE_REP_ITEM).reg));
        jump(c, done); mark(c, invalid); move(c, result, imm(c, ITEM_ERROR)); mark(c, done);
        MirValue membership = value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
        membership.admitted_success_contract = &TYPE_BOOL;
        return membership;
    }
    if (n->op == OPERATOR_TO) {
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        return value(c, em_call_2(&c->em, "fn_to", MIR_T_I64, MIR_T_I64, reg(c, a.reg),
            MIR_T_I64, reg(c, b.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
    }
    // map absence is the zero pointer in both Item and nullable pointer lanes.
    if ((n->op == OPERATOR_EQ || n->op == OPERATOR_NE) &&
            ((a.semantic_type == LMD_TYPE_NULL && lambda_type_nonnull_map_contract(b.semantic_contract)) ||
             (b.semantic_type == LMD_TYPE_NULL && lambda_type_nonnull_map_contract(a.semantic_contract)))) {
        MirValue pointer = a.semantic_type == LMD_TYPE_NULL ? b : a;
        return value(c, operation(c, n->op == OPERATOR_EQ ? MIR_EQ : MIR_NE,
            reg(c, pointer.reg), imm(c, 0)), LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
    }
    if (n->op == OPERATOR_JOIN) {
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        return value(c, em_call_2(&c->em, "fn_join", MIR_T_I64, MIR_T_I64, reg(c, a.reg),
            MIR_T_I64, reg(c, b.reg), true), n->type->type_id, VALUE_REP_ITEM, n->type, n);
    }
    TypeId left = numeric_success(a), right = numeric_success(b);
    bool numeric = (left == LMD_TYPE_INT || left == LMD_TYPE_FLOAT) &&
        (right == LMD_TYPE_INT || right == LMD_TYPE_FLOAT);
    MirNumericOpPlan comparison = {};
    if (numeric && (nullable(a) || nullable(b)) &&
            (a.rep == VALUE_REP_INT_LANE || a.rep == VALUE_REP_F64) &&
            (b.rep == VALUE_REP_INT_LANE || b.rep == VALUE_REP_F64) &&
            em_numeric_op_plan(n->op, &comparison, left == LMD_TYPE_INT && right == LMD_TYPE_INT) &&
            comparison.is_comparison) return nullable_comparison(c, n, a, b);
    // Nullable arithmetic absorbs absence; comparisons keep their runtime null rules.
    if (nullable(a) || nullable(b)) numeric &= n->op == OPERATOR_ADD || n->op == OPERATOR_SUB ||
        n->op == OPERATOR_MUL || n->op == OPERATOR_DIV ||
        (left == LMD_TYPE_INT && right == LMD_TYPE_INT && (n->op == OPERATOR_IDIV || n->op == OPERATOR_MOD));
    bool booleans = !nullable(a) && !nullable(b) && a.semantic_type == LMD_TYPE_BOOL && b.semantic_type == LMD_TYPE_BOOL &&
        (n->op == OPERATOR_EQ || n->op == OPERATOR_NE);
    if (!numeric && !booleans) {
        const char* helper = n->op == OPERATOR_ADD ? "fn_add" : n->op == OPERATOR_SUB ? "fn_sub" :
            n->op == OPERATOR_MUL ? "fn_mul" : n->op == OPERATOR_DIV ? "fn_div" :
            n->op == OPERATOR_IDIV ? "fn_idiv" : n->op == OPERATOR_MOD ? "fn_mod" :
            n->op == OPERATOR_EQ ? "fn_eq_value" : n->op == OPERATOR_NE ? "fn_ne_value" :
            n->op == OPERATOR_LT ? "fn_lt" : n->op == OPERATOR_LE ? "fn_le" :
            n->op == OPERATOR_GT ? "fn_gt" : n->op == OPERATOR_GE ? "fn_ge" :
            n->op == OPERATOR_AT ? "fn_at" : n->op == OPERATOR_IN ? "fn_in" : n->op == OPERATOR_OF ? "fn_of" :
            n->op == OPERATOR_TO ? "fn_to" : NULL;
        if (!helper) {
            mvp_reject(c->module, n, "comparison requires proven numeric or bool contracts"); return null_value(c);
        }
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        MIR_reg_t result = 0;
        MIR_label_t done = NULL;
        // known decimal/string/container carriers cannot satisfy an int tag
        // guard. Keep speculation only where both operand domains allow it.
        if ((left == LMD_TYPE_ANY || left == LMD_TYPE_TYPE || left == LMD_TYPE_INT) &&
                (right == LMD_TYPE_ANY || right == LMD_TYPE_TYPE || right == LMD_TYPE_INT)) {
            result = em_new_reg(&c->em, "dynamic_numeric", MIR_T_I64);
            MIR_label_t slow = label(c); done = label(c);
            MirValue ai = value(c, em_guard_finite_int(&c->em, a, slow), LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT);
            MirValue bi = value(c, em_guard_finite_int(&c->em, b, slow), LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT);
            // the dominating tag/band guards already exclude poison in both operands.
            move(c, result, reg(c, convert(c, numeric_binary(c, n, ai, bi, true), VALUE_REP_ITEM).reg));
            jump(c, done); mark(c, slow);
        }
        int decimal_op = left == LMD_TYPE_DECIMAL && right == LMD_TYPE_DECIMAL
            ? decimal_operation(n->op) : -1;
        MIR_reg_t checked = decimal_op >= 0
            ? em_call_3(&c->em, "lambda_decimal_binary", MIR_T_I64,
                MIR_T_I64, reg(c, a.reg), MIR_T_I64, reg(c, b.reg), MIR_T_I64, imm(c, decimal_op), true)
            : em_call_2(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, a.reg),
                MIR_T_I64, reg(c, b.reg), true);
        if (result) move(c, result, reg(c, checked));
        else result = checked;
        if (done) mark(c, done);
        MirValue computed = value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
        // decimal arithmetic closes in its admitted domain, including poison;
        // retain the error arm while avoiding repeated successful admission.
        if (left == LMD_TYPE_DECIMAL && right == LMD_TYPE_DECIMAL &&
                (n->op == OPERATOR_ADD || n->op == OPERATOR_SUB || n->op == OPERATOR_MUL ||
                 n->op == OPERATOR_DIV || n->op == OPERATOR_IDIV || n->op == OPERATOR_MOD))
            computed.admitted_success_contract = &TYPE_DECIMAL;
        return computed;
    }
    if (!a.admitted_success_contract && !b.admitted_success_contract) return numeric_binary(c, n, a, b);
    MIR_reg_t joined = em_new_reg(&c->em, "numeric_join", MIR_T_I64);
    MIR_label_t done = label(c);
    MirValue operands[] = {a, b};
    for (int i = 0; i < 2; i++) {
        MirValue* v = &operands[i];
        if (!v->admitted_success_contract) continue;
        // preserve left-to-right error selection after both operands have been evaluated (S7.9.3).
        *v = convert(c, *v, VALUE_REP_ITEM);
        move(c, joined, reg(c, v->reg));
        MIR_reg_t tag = operation(c, MIR_URSH, reg(c, v->reg), imm(c, 56));
        branch(c, MIR_BEQ, done, reg(c, tag), imm(c, LMD_TYPE_ERROR));
        v->semantic_type = i == 0 ? left : right;
        v->semantic_contract = v->admitted_success_contract;
    }
    MirValue result = numeric_binary(c, n, operands[0], operands[1]);
    Type* success = result.semantic_contract;
    result = convert(c, result, VALUE_REP_ITEM);
    move(c, joined, reg(c, result.reg)); mark(c, done);
    result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
    result.admitted_success_contract = success;
    return result;
}

static void function_body(MvpCompiler* c);
static bool dense_region_eligible(MvpCompiler*, AstNodeId, MvpIndexLoopPlan*, bool, bool = false);

struct MvpDenseCalls {
    MvpFunction* functions[16];
    bool errors[16];
    unsigned count;
};
static bool dense_body_eligible(MvpCompiler*, MvpIndexLoopPlan*, MvpDenseCalls* calls = NULL, bool allocating = false);
static bool dense_call_eligible(MvpCompiler*, AstCallNode*);
static bool dense_call_arguments(MvpCompiler*, MvpFunction*, AstNode*);

static bool function_has_arrays(MvpFunction* f) {
    for (AstNamedNode* p = f ? f->ast->param : NULL; p; p = (AstNamedNode*)p->next)
        if (mvp_array_element(mvp_contract(p))) return true;
    return false;
}

static bool function_has_snapshots(LambdaMvpModule* m, MvpFunction* f) {
    for (AstNamedNode* p = f ? f->ast->param : NULL; p; p = (AstNamedNode*)p->next)
        if (mvp_binding(m, p->entry)->scalar_snapshot) return true;
    return false;
}

static bool inline_eligible(MvpCompiler* c, MvpFunction* f, bool discard = false,
        bool closed_body = false) {
    // expansions bind already-native operands; boxed/default receivers must
    // execute their admission and defaults at the actual callee entry.
    if (f->imported || mvp_parameter_receiver(f->ast)) return false;
    bool record_result = f->abi.result.normal.abi_rep == VALUE_REP_RAW_GC_POINTER &&
        lambda_type_nonnull_map_contract(f->result_contract);
    unsigned budget = !f->abi.effects.may_gc && !f->abi.effects.may_return_error ? 192 : 96;
    if (!f->inline_cost || f->inline_cost > budget || c->inline_cost + f->inline_cost > 256 ||
            (f->abi.effects.may_reenter && !closed_body) ||
            (f->abi.result.normal.abi_rep == VALUE_REP_ITEM && !discard) ||
            // scalar-argument record producers have no caller container to
            // write back; the ordinary return path retains snapshots (S9.3.1).
            (f->abi.result.normal.abi_rep == VALUE_REP_RAW_GC_POINTER &&
             f->result_contract != &TYPE_STRING && f->result_contract != &TYPE_DECIMAL && !record_result)) return false;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next) {
        ValueRep parameter_rep = mvp_rep(mvp_contract(p));
        TypeParam* parameter = lambda_type_param(p->type);
        if (record_result && ((parameter && parameter->is_var_param) ||
                (parameter_rep != VALUE_REP_INT_LANE && parameter_rep != VALUE_REP_F64 &&
                 parameter_rep != VALUE_REP_I64))) return false;
        // primitive array contracts may use Item transport while retaining a proven container domain.
        if (parameter_rep == VALUE_REP_ITEM && !mvp_array_element(mvp_contract(p))) return false;
    }
    if (function_has_arrays(f)) {
        // keep loop kernels at their own entry; a single recursive expansion
        // can still reuse caller witnesses without bypassing the next real edge.
        AstIndex* index = &c->module->tp->ast_index;
        AstNodeId root = ast_index_find(index, f->ast->body);
        for (AstNodeId i = root; i < ast_index_subtree_end(index, root); i++)
            if ((index->nodes[i]->node_type == AST_NODE_LOOP || index->nodes[i]->node_type == AST_NODE_FOR_EXPR) &&
                    (f != c->function || c->inlining || f->inline_cost > 96)) return false;
    }
    unsigned depth = 0;
    for (MvpInline* p = c->inlining; p; p = p->parent)
        if (p->function == f) depth++;
    // tiny scalar recursion can amortize entry checks over two expansions; real calls still guard the stack.
    unsigned limit = f->inline_cost <= 32 && !f->abi.effects.may_gc && !f->abi.effects.may_return_error ? 2 : 1;
    if (depth >= limit) return false;
    return true;
}

struct MvpArrayWitness {
    MIR_reg_t native, writable, length, items;
};

static void snapshot_parameter(MvpCompiler* c, MvpBinding* b) {
    if (!b->scalar_snapshot) return;
    // the shared confinement proof keeps scratch payloads inside this native
    // activation; the allocating helper and its header retain precise roots.
    MIR_reg_t scratch = em_new_reg(&c->em, "scalar_snapshot_data", MIR_T_P);
    // an expansion can execute inside a loop. Reserve its scratch once at the
    // containing entry rather than growing the native stack on each iteration.
    MIR_insn_t saved_cursor = c->em.insert_after;
    c->em.insert_after = c->em.frame.anchor;
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_ALLOCA,
        reg(c, scratch), imm(c, 512)));
    c->em.insert_after = saved_cursor;
    MIR_reg_t copy = em_call_3(&c->em, "lambda_array_scalar_snapshot", MIR_T_I64,
        MIR_T_I64, reg(c, b->reg), MIR_T_P, reg(c, scratch), MIR_T_I64, imm(c, 512), true);
    MirValue result = value(c, copy, LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR);
    MIR_label_t clean = label(c);
    branch(c, MIR_BNE, clean, reg(c, operation(c, MIR_URSH, reg(c, copy), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
    return_value(c, result, NULL);
    mark(c, clean);
    move(c, b->reg, reg(c, copy));
}

static MirCallResult inline_call(MvpCompiler* c, MvpFunction* f, MIR_op_t* args,
        MvpArrayWitness* witnesses) {
    LambdaMvpModule* m = c->module;
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) { mvp_reject(m, f->ast, "inline binding allocation failed"); return {}; }
    MvpFunction* caller = c->function;
    MvpScope* scope = c->scope;
    MvpErrorTarget* condition_failure = c->condition_failure;
    MvpControl* control = c->control;
    MIR_label_t tail_entry = c->tail_entry;
    MvpBinding* loop_array = c->loop_array;
    MIR_reg_t loop_items = c->loop_array_items;
    bool dense_arrays = c->dense_arrays;
    bool dense_call = c->dense_callee == f;
    MvpInline frame = {c->inlining, f,
        em_new_reg(&c->em, "inline_result", em_mir_type_for_rep(f->abi.result.normal.abi_rep)), label(c),
        f->abi.effects.may_return_error ? em_new_reg(&c->em, "inline_error", MIR_T_I64) : 0};
    if (frame.error) {
        move(c, frame.error, imm(c, ITEM_NULL));
        value(c, frame.error, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    }
    c->inlining = &frame; c->function = f; c->scope = NULL; c->control = NULL;
    c->condition_failure = NULL;
    c->loop_array = NULL; c->loop_array_items = 0; c->dense_arrays = dense_call;
    c->inline_cost += f->inline_cost;
    if (dense_call)
        // the kernel plan proved every outcome initializer/write under the
        // entry guards. Choose native locals before allocating their registers.
        narrow_dense_locals(m, ast_index_find(&m->tp->ast_index, f->ast->body), true);
    // every expansion owns fresh locals; recursive expansion must not overwrite the caller's bindings.
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (b->owner != f->id) continue;
        b->reg = em_new_reg(&c->em, "inline_binding", em_mir_type_for_rep(b->rep));
        value(c, b->reg, b->contract->type_id, b->rep, b->contract);
        b->scalar_home = 0; b->scope = NULL;
        b->var_home = b->array_native = b->array_float_native = b->array_writable = b->array_length = b->dense_items = 0;
        b->record_writable = b->record_readable = 0;
    }
    int i = 0;
    bool snapshots = function_has_snapshots(m, f);
    if (snapshots) {
        // a failed snapshot jumps to the common inline exit, which writes all
        // borrowed arguments back. Initialize every parameter before that call.
        for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, i++)
            move(c, mvp_binding(m, p->entry)->reg, args[i],
                em_mir_type_for_rep(mvp_binding(m, p->entry)->rep));
    }
    i = 0;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, i++) {
        MvpBinding* b = mvp_binding(m, p->entry);
        if (!snapshots) move(c, b->reg, args[i], em_mir_type_for_rep(b->rep));
        snapshot_parameter(c, b);
        if (dense_call && mvp_array_element(b->contract)) {
            // a closed caller already owns this stable, noncollecting buffer;
            // carry its witness through accessors instead of reloading headers.
            b->dense_items = witnesses[i].items ? witnesses[i].items :
                em_load_at(&c->em, b->reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "kernel_items");
        } else if (!b->scalar_snapshot && witnesses[i].native && b->array_accessed) {
            // admitted unchanged owners can carry their existing physical witness into a leaf expansion.
            b->array_native = em_new_reg(&c->em, "inline_native", MIR_T_I64);
            b->array_writable = em_new_reg(&c->em, "inline_writable", MIR_T_I64);
            b->array_length = em_new_reg(&c->em, "inline_length", MIR_T_I64);
            move(c, b->array_native, reg(c, witnesses[i].native));
            move(c, b->array_writable, reg(c, witnesses[i].writable));
            move(c, b->array_length, reg(c, witnesses[i].length));
        } else refresh_array(c, b);
    }
    MvpIndexLoopPlan unused = {};
    bool closed = dense_region_eligible(c, ast_index_find(&m->tp->ast_index, f->ast->body), &unused, true);
    if (dense_call) narrow_dense_locals(m, ast_index_find(&m->tp->ast_index, f->ast->body));
    MvpNumericLoopPlan numeric = {};
    if (mvp_numeric_inline_plan(m, f, &numeric)) {
        MIR_label_t checked = label(c);
        // guard actual lanes once, then preserve every intermediate's finite proof through the expansion.
        for (uint32_t g = 0; g < numeric.count; g++) {
            MvpBinding* b = numeric.guards[g];
            MIR_reg_t biased = operation(c, MIR_ADD, reg(c, b->reg), imm(c, numeric.radius));
            branch(c, MIR_UBGT, checked, reg(c, biased), imm(c, numeric.radius * 2));
            b->range = {true, -numeric.radius, numeric.radius};
        }
        function_body(c);
        for (uint32_t g = 0; g < numeric.count; g++) numeric.guards[g]->range = {};
        mem_free(numeric.guards);
        mark(c, checked);
    }
    function_body(c);
    mark(c, frame.done);
    i = 0;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, i++) {
        MvpBinding* b = mvp_binding(m, p->entry);
        TypeParam* param = lambda_type_param(p->type);
        // expansions cannot reenter or capture caller locals; rooted registers
        // carry replacements to this common exit, including partial error exits.
        if (param && param->is_var_param && !dense_call) args[i] = reg(c, b->reg);
        // capture-free numeric accessors update their own witnesses on every checked write.
        witnesses[i] = closed ? MvpArrayWitness{b->array_native, b->array_writable, b->array_length, 0} : MvpArrayWitness{};
    }
    c->inlining = frame.parent; c->function = caller; c->scope = scope; c->control = control;
    c->condition_failure = condition_failure;
    c->tail_entry = tail_entry;
    c->loop_array = loop_array; c->loop_array_items = loop_items; c->dense_arrays = dense_arrays;
    restore_bindings(m, saved);
    mem_free(saved);
    MirCallResult result = {};
    result.normal = value(c, frame.result, f->result_contract->type_id,
        f->abi.result.normal.abi_rep, f->result_contract);
    if (frame.error) result.error = value(c, frame.error, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    return result;
}

struct MvpPlace {
    MvpBinding* root;
    MIR_reg_t path;
};

static MvpPlace place_path(MvpCompiler* c, AstNode* source, AstNode* final_key = NULL,
        bool final_member = false) {
    AstCowPath path = {};
    if (!ast_collect_cow_path(&path, source)) return {};
    MvpBinding* root = mvp_local_binding(c->module, path.root);
    if (!root || root->owner != c->function->id) return {};
    if (final_key) {
        if (path.count == AST_COW_PATH_MAX) return {};
        path.segment[path.count] = final_key;
        path.is_member[path.count++] = final_member;
    }
    if (!path.count) return {root, 0};
    MIR_reg_t keys = em_call_0(&c->em, "array_plain", MIR_T_P, true);
    value(c, keys, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
    for (int i = 0; i < path.count; i++) {
        MirValue key;
        if (path.is_member[i]) {
            AstIdentNode* name = (AstIdentNode*)ast_unwrap_primary_to_leaf(path.segment[i]);
            key = value(c, em_load_const(&c->em, em_add_const(&c->em, name->name), MIR_T_P),
                LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING);
        } else key = expression(c, path.segment[i]);
        key = convert(c, key, VALUE_REP_ITEM);
        em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P, reg(c, keys),
            MIR_T_I64, reg(c, key.reg), true);
    }
    return {root, keys};
}

static void publish_place(MvpCompiler* c, MvpBinding* binding, MIR_reg_t owner) {
    MirValue updated = convert(c, value(c, owner, binding->contract->type_id,
        VALUE_REP_ITEM, binding->contract), binding->rep);
    move(c, binding->reg, reg(c, updated.reg), updated.mir_type);
    refresh_array(c, binding);
    if (binding->var_home) em_store_at(&c->em, binding->var_home, 0, MIR_T_I64,
        convert(c, updated, VALUE_REP_ITEM).reg);
}

static void write_place(MvpCompiler* c, MvpPlace place, MirValue input) {
    input = convert(c, input, VALUE_REP_ITEM);
    MirValue owner = convert(c, read_binding(c, place.root, NULL, false), VALUE_REP_ITEM);
    const char* helper = lambda_type_nonnull_map_contract(place.root->admission_contract)
        ? "lambda_map_path_set_checked" : mvp_array_contract(place.root->admission_contract)
        ? "lambda_array_path_set_checked" : "cow_path_set";
    MIR_reg_t result = strcmp(helper, "cow_path_set") == 0
        ? em_call_3(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, owner.reg),
            MIR_T_I64, reg(c, place.path), MIR_T_I64, reg(c, input.reg), true)
        : em_call_5(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, owner.reg),
            MIR_T_I64, reg(c, place.path), MIR_T_I64, reg(c, input.reg),
            MIR_T_P, imm(c, (intptr_t)place.root->admission_contract),
            MIR_T_P, imm(c, (intptr_t)place.root->entry->name->chars), true);
    MIR_label_t accepted = label(c);
    branch(c, MIR_BNE, accepted, reg(c, operation(c, MIR_URSH, reg(c, result), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
    stage_error(c, result); mark(c, accepted);
    publish_place(c, place.root, result);
}

static MirValue direct_call(MvpCompiler* c, MvpFunction* f, AstNode* args, bool discard,
        MvpBinding* destination, bool propagate) {
    MIR_type_t types[LAMBDA_MAX_FUNCTION_ARGS]; MIR_op_t ops[LAMBDA_MAX_FUNCTION_ARGS];
    AstNode* actuals[LAMBDA_MAX_FUNCTION_ARGS] = {};
    int provided = 0;
    for (AstNode* arg = args; arg; arg = arg->next) actuals[provided++] = arg;
    AstNamedNode* param = f->ast->param;
    MvpErrorTarget failure = {};
    MvpBinding* borrowed[LAMBDA_MAX_FUNCTION_ARGS] = {};
    MvpBinding projected[LAMBDA_MAX_FUNCTION_ARGS] = {};
    MvpPlace places[LAMBDA_MAX_FUNCTION_ARGS] = {};
    MvpBinding* snapshots[LAMBDA_MAX_FUNCTION_ARGS] = {};
    MvpArrayWitness witnesses[LAMBDA_MAX_FUNCTION_ARGS] = {};
    bool pure_arguments = true;
    // pure builtin allocations cannot mutate an earlier actual; its precise root still survives GC.
    for (int n = 0; n < f->abi.param_count; n++) pure_arguments &= mvp_pure_key(actuals[n], 0, true);
    int i = 0;
    for (; i < f->abi.param_count; param = (AstNamedNode*)param->next, i++) {
        AstNode* arg = actuals[i];
        if (!arg) {
            // defaults run in the callee after preceding parameters are bound.
            types[i] = MIR_T_I64; ops[i] = imm(c, ITEM_MISSING_ARGUMENT);
            continue;
        }
        TypeParam* p = lambda_type_param(param->type);
        if (p && p->is_var_param) {
            borrowed[i] = mvp_local_binding(c->module, arg);
            if (!borrowed[i]) {
                places[i] = place_path(c, arg);
                MvpBinding* local = &projected[i];
                local->entry = places[i].root->entry;
                local->owner = c->function->id;
                local->contract = local->admission_contract = mvp_contract(param);
                local->rep = mvp_rep(local->contract);
                MirValue original = convert(c, expression(c, arg), VALUE_REP_ITEM);
                original = value(c, em_call_1(&c->em, "cow_capture_value", MIR_T_I64,
                    MIR_T_I64, reg(c, original.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
                original = admit(c, original, local->contract, &failure);
                local->reg = em_new_reg(&c->em, "place_argument", original.mir_type);
                move(c, local->reg, reg(c, original.reg), original.mir_type);
                borrowed[i] = local;
            }
        }
        MvpBinding* source = mvp_local_binding(c->module, arg);
        // use the shared AST's retention/mutation effects rather than rebuilding a separate call graph.
        bool readonly_parameter = pure_arguments &&
            mvp_parameter_borrowable(c->module, param);
        // a confined callee copies at entry. Pure actuals and absence of a
        // simultaneous var borrow preserve the argument-evaluation snapshot.
        MvpBinding* parameter = mvp_binding(c->module, param->entry);
        bool entry_snapshot = pure_arguments && parameter && parameter->scalar_snapshot;
        bool read_only = source && (readonly_parameter || entry_snapshot);
        // a simultaneous borrow of the same owner can mutate the plain argument's snapshot (S9.1.2).
        AstNamedNode* other = f->ast->param;
        for (int a = 0; read_only && a < f->abi.param_count; a++, other = (AstNamedNode*)other->next) {
            TypeParam* type = lambda_type_param(other->type);
            if (type && type->is_var_param && mvp_local_binding(c->module, actuals[a]) == source) read_only = false;
        }
        if (read_only && entry_snapshot) snapshots[i] = source;
        if (borrowed[i] == &projected[i]) source = borrowed[i];
        MirValue v = {};
        if (!borrowed[i] && !read_only)
            v = contextual_fill(c, arg, mvp_contract(param), parameter ? parameter->array_cert : NULL,
                "MVP scalar boundary", readonly_parameter && mvp_parameter_borrowable(c->module, param, false));
        if (!v.reg) v = borrowed[i] || read_only ? read_binding(c, source, arg, false) : expression(c, arg);
        v = admit(c, v, mvp_contract(param), &failure);
        v = convert(c, v, f->abi.params[i].canonical_rep);
        types[i] = v.mir_type; ops[i] = reg(c, v.reg);
        if (pure_arguments && source && source->array_native &&
                mvp_array_element(source->contract) == mvp_array_element(mvp_contract(param)) &&
                mvp_boundary_proven(source->contract, mvp_contract(param)))
            witnesses[i] = {source->array_native, source->array_writable, source->array_length, 0};
        if (c->dense_arrays && source) witnesses[i].items = source->dense_items;
    }
    // the closed body already guards unchanged owners and every access extent.
    // Self recursion reuses that proof; bounded leaf accessors expand natively.
    bool allocating_leaf = c->guarded_allocating && dense_call_arguments(c, f, args);
    bool closed_call = c->dense_calls && c->dense_arrays && function_has_arrays(f);
    bool recursive = closed_call && f == c->function;
    bool dense_call = c->dense_callee == f || closed_call || allocating_leaf;
    // one bounded recursive expansion amortizes entry guards and the checked
    // sibling's frame; its next self edge remains a stack-guarded real call.
    bool inlined = recursive ? !c->inlining && f->inline_cost <= 256 :
        dense_call || inline_eligible(c, f, discard);
    MIR_reg_t span = 0;
    MIR_reg_t homes[LAMBDA_MAX_FUNCTION_ARGS] = {};
    int active = c->argument_active;
    // all argument evaluation precedes publishing the rooted borrow cells.
    for (int n = 0; n < i; n++) if (borrowed[n] && !inlined && !dense_call) {
        bool forwarding = borrowed[n]->var_home != 0;
        for (int a = 0; forwarding && a < i; a++)
            if (a != n && borrowed[a] == borrowed[n]) forwarding = false;
        if (forwarding) {
            // forward the enclosing precise home rather than copying its owner
            // through another frame. Aliased actuals keep the ordinary cells.
            homes[n] = borrowed[n]->var_home;
            em_publish_var_home(&c->em, n, homes[n]);
            continue;
        }
        if (!span) {
            // nested expansions must not overwrite their enclosing call's live owner cells.
            c->argument_active = active + i;
            if (c->argument_active > c->argument_count) c->argument_count = c->argument_active;
            span = em_deferred_root_span_base(&c->em, &c->argument_span, &c->argument_fixup);
            if (active) span = operation(c, MIR_ADD, reg(c, span), imm(c, active * sizeof(Item)));
        }
        homes[n] = operation(c, MIR_ADD, reg(c, span), imm(c, n * sizeof(Item)));
        em_store_at(&c->em, homes[n], 0, MIR_T_I64, ops[n].u.reg);
        em_publish_var_home(&c->em, n, homes[n]);
    }
    MirCallResult result = {};
    if (inlined) {
        // admission failures skip the inlined body and join through the same call boundary below.
        MvpFunction* saved_callee = c->dense_callee;
        if (closed_call || allocating_leaf) c->dense_callee = f;
        result = inline_call(c, f, ops, witnesses);
        c->dense_callee = saved_callee;
    } else {
        MirCallOptions options = {true, false, 0};
        FnVariantAnalysis abi = f->abi;
        if (recursive) abi.effects.may_gc = abi.effects.may_return_error = false;
        result = em_call_direct(&c->em, f->name, f->target, &abi, i, types, ops, &options);
    }
    param = f->ast->param;
    for (int n = 0; n < i; n++, param = (AstNamedNode*)param->next) if (borrowed[n] && !dense_call) {
        move(c, borrowed[n]->reg, inlined ? ops[n] :
            reg(c, em_load_at(&c->em, homes[n], 0, MIR_T_I64, "var_reloaded")));
        if (inlined && witnesses[n].native && borrowed[n]->array_native) {
            move(c, borrowed[n]->array_native, reg(c, witnesses[n].native));
            move(c, borrowed[n]->array_writable, reg(c, witnesses[n].writable));
            move(c, borrowed[n]->array_length, reg(c, witnesses[n].length));
        } else if (!mvp_parameter_storage_stable(c->module, param)) refresh_array(c, borrowed[n]);
        if (borrowed[n]->var_home) em_store_at(&c->em, borrowed[n]->var_home, 0, MIR_T_I64, borrowed[n]->reg);
        if (!inlined && homes[n] != borrowed[n]->var_home)
            em_store_at(&c->em, homes[n], 0, MIR_T_I64, constant(c, ITEM_NULL));
        if (places[n].root)
            write_place(c, places[n], value(c, borrowed[n]->reg, borrowed[n]->contract->type_id,
                borrowed[n]->rep, borrowed[n]->contract));
    }
    c->argument_active = active;
    // a snapshot miss shares the original before detaching. Reload the caller's
    // COW witness instead of retaining its pre-call exclusivity fact.
    for (int n = 0; n < i; n++) if (snapshots[n]) refresh_array(c, snapshots[n]);
    if (propagate) {
        // discharge the companion before boxing a successful native result.
        // Borrow write-back above still runs on both outcomes (S7.6.3v3).
        if (f->abi.effects.may_return_error && result.error.reg) {
            MIR_label_t clean = label(c);
            branch(c, MIR_BEQ, clean, reg(c, result.error.reg),
                imm(c, inlined || em_error_lane_in_register(f->abi.result.companion) ? ITEM_NULL : 0));
            return_value(c, value(c, result.error.reg, LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR), NULL);
            mark(c, clean);
        }
        if (failure.done) {
            MIR_label_t done = label(c); jump(c, done); mark(c, failure.done);
            return_value(c, value(c, failure.error, LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR), NULL);
            mark(c, done);
        }
        MirValue normal = inlined ? result.normal :
            em_finish_direct_call_normal(&c->em, result, MIR_PENDING_REASON_REP_CONVERSION);
        normal.semantic_contract = f->result_contract;
        if (normal.rep == VALUE_REP_ITEM && f->abi.effects.may_return_error)
            normal = value(c, normal.reg, LMD_TYPE_ANY, VALUE_REP_ITEM,
                lambda_type_union_normalized(c->module->tp->pool, f->result_contract, &TYPE_ERROR));
        if (dense_call && !f->abi.effects.may_return_error && normal.rep == VALUE_REP_ITEM)
            normal.semantic_contract = &TYPE_ANY_NO_ERROR;
        return propagated_value(c, normal, NULL);
    }
    if (discard) {
        em_apply_value_demand(&c->em, result.normal, MIR_VALUE_DISCARD);
        if (failure.done) mark(c, failure.done);
        return null_value(c);
    }
    // stack exhaustion is the sole native error exit in the admitted subset; the recovery guard owns it.
    MirValue v = inlined ? result.normal : em_finish_direct_call_normal(&c->em, result, MIR_PENDING_REASON_REP_CONVERSION);
    // the shared physical call result has no frontend contract. Restore the
    // callee's full nullable contract before boxing its native pointer lane.
    v.semantic_contract = f->result_contract;
    if (v.rep == VALUE_REP_ITEM && f->abi.effects.may_return_error)
        v = value(c, v.reg, LMD_TYPE_ANY, VALUE_REP_ITEM,
            lambda_type_union_normalized(c->module->tp->pool, f->result_contract, &TYPE_ERROR));
    if (destination) {
        MvpErrorTarget* target = destination->scope ? &destination->scope->failure : NULL;
        if (f->abi.effects.may_return_error && result.error.reg) {
            MIR_label_t clean = label(c);
            branch(c, MIR_BEQ, clean, reg(c, result.error.reg),
                imm(c, inlined || em_error_lane_in_register(f->abi.result.companion) ? ITEM_NULL : 0));
            fail_boundary(c, result.error.reg, target); mark(c, clean);
        }
        if (failure.done) {
            MIR_label_t done = label(c); jump(c, done); mark(c, failure.done);
            fail_boundary(c, failure.error, target); mark(c, done);
        }
        return value(c, v.reg, v.semantic_type, v.rep, f->result_contract);
    }
    if (failure.done || (f->abi.effects.may_return_error && result.error.reg)) {
        MIR_reg_t joined = em_new_reg(&c->em, "call_join", MIR_T_I64);
        MIR_label_t clean = label(c), done = label(c);
        if (f->abi.effects.may_return_error && result.error.reg) {
            branch(c, MIR_BEQ, clean, reg(c, result.error.reg),
                imm(c, inlined || em_error_lane_in_register(f->abi.result.companion) ? ITEM_NULL : 0));
            move(c, joined, reg(c, result.error.reg)); jump(c, done);
        }
        mark(c, clean);
        v = convert(c, v, VALUE_REP_ITEM); move(c, joined, reg(c, v.reg)); mark(c, done);
        if (failure.done) {
            MIR_label_t completed = label(c); jump(c, completed); mark(c, failure.done);
            move(c, joined, reg(c, failure.error)); mark(c, completed);
        }
        MirValue joined_value = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
        // call-site failures join only errors; the callee's success value
        // passed its return contract even with Item transport (D8.4.2v2, S7.7.3).
        if (!lambda_type_accepts_error(f->result_contract))
            joined_value.admitted_success_contract = f->result_contract;
        return joined_value;
    }
    return v;
}
static AstCallNode* bitwise_expression(AstNode* source) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    if (!node || node->node_type != AST_NODE_CALL_EXPR) return NULL;
    AstCallNode* call = (AstCallNode*)node;
    AstNode* target = ast_unwrap_primary_to_leaf(call->function);
    if (!target || target->node_type != AST_NODE_SYS_FUNC) return NULL;
    SysFunc fn = ((AstSysFuncNode*)target)->fn_info->fn;
    return fn == SYSFUNC_BOR || fn == SYSFUNC_BXOR || fn == SYSFUNC_SHL || fn == SYSFUNC_SHR ? call : NULL;
}

static bool bitwise_tree_plan(MvpCompiler* c, AstNode* source, unsigned* operations, unsigned depth = 0) {
    if (depth > 8) return false;
    if (AstCallNode* call = bitwise_expression(source)) {
        if (++*operations > 8) return false;
        return bitwise_tree_plan(c, call->argument, operations, depth + 1) &&
            bitwise_tree_plan(c, call->argument->next, operations, depth + 1);
    }
    return mvp_pure_key(source) && mvp_value_contract(c->module, source)->type_id == LMD_TYPE_INT;
}

static MIR_reg_t native_bitwise_tree(MvpCompiler* c, AstNode* source, MIR_label_t checked) {
    AstCallNode* call = bitwise_expression(source);
    if (!call) return em_guard_finite_int(&c->em, expression(c, source), checked);
    SysFunc fn = ((AstSysFuncNode*)ast_unwrap_primary_to_leaf(call->function))->fn_info->fn;
    MIR_reg_t left = native_bitwise_tree(c, call->argument, checked);
    MIR_reg_t right = native_bitwise_tree(c, call->argument->next, checked);
    if (fn == SYSFUNC_SHL || fn == SYSFUNC_SHR)
        branch(c, MIR_UBGE, checked, reg(c, right), imm(c, 64));
    MIR_reg_t result = operation(c, em_integer_bitwise_opcode(fn), reg(c, left), reg(c, right));
    // every intermediate must remain finite: saturation may make a later
    // bitwise call fail. The checked tree retains that exact behavior (S4.1.2).
    em_branch_int53_outside(&c->em, result, checked);
    return result;
}

static MirValue mutate_collection(MvpCompiler* c, AstCallNode* node, SysFuncInfo* info) {
    MvpPlace place = place_path(c, node->argument);
    if (!place.root) {
        mvp_reject(c->module, node, "collection mutation requires a resolved place");
        return null_value(c);
    }
    MirValue args[2]; int count = 0;
    for (AstNode* arg = node->argument->next; arg; arg = arg->next)
        args[count++] = convert(c, expression(c, arg), VALUE_REP_ITEM);
    MirValue owner = convert(c, read_binding(c, place.root, node->argument, false), VALUE_REP_ITEM);
    const char* boundary = place.root->entry->name->chars;
    MIR_reg_t result;
    if (place.path) {
        // detach and install the spine before entering an in-place mutator;
        // every key and value is already rooted (S9.2.2).
        MIR_reg_t detached = em_call_1(&c->em, "cow_prepare_write", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), true);
        publish_place(c, place.root, detached);
        MIR_reg_t leaf = em_call_2(&c->em, "cow_place_leaf", MIR_T_I64,
            MIR_T_I64, reg(c, detached), MIR_T_I64, reg(c, place.path), true);
        value(c, leaf, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
        result = info->fn == SYSPROC_PUSH
            ? em_call_2(&c->em, "pn_push", MIR_T_I64, MIR_T_I64, reg(c, leaf), MIR_T_I64, reg(c, args[0].reg), true)
            : em_call_3(&c->em, "pn_splice", MIR_T_I64, MIR_T_I64, reg(c, leaf),
                MIR_T_I64, reg(c, args[0].reg), MIR_T_I64, reg(c, args[1].reg), true);
    } else {
        bool typed = lambda_array_contract_element(place.root->admission_contract) != NULL;
        result = info->fn == SYSPROC_PUSH && typed
            ? em_call_4(&c->em, "lambda_array_push_checked", MIR_T_I64,
                MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, args[0].reg),
                MIR_T_P, imm(c, (intptr_t)place.root->admission_contract), MIR_T_P, imm(c, (intptr_t)boundary), true)
            : info->fn == SYSPROC_PUSH
            ? em_call_2(&c->em, "pn_push_cow", MIR_T_I64, MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, args[0].reg), true)
            : em_call_3(&c->em, "pn_splice_cow", MIR_T_I64, MIR_T_I64, reg(c, owner.reg),
                MIR_T_I64, reg(c, args[0].reg), MIR_T_I64, reg(c, args[1].reg), true);
        MIR_label_t rejected = label(c);
        branch(c, MIR_BEQ, rejected, reg(c, operation(c, MIR_URSH, reg(c, result), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        publish_place(c, place.root, result); mark(c, rejected);
    }
    return value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
}

static MirValue call(MvpCompiler* c, AstCallNode* node, bool discard) {
    MvpFunction* f = mvp_function(c->module, ast_direct_call_function(node));
    if (f) {
        if (f->result_contract == &TYPE_STRING) {
            MirValue joined = string_concat(c, node, NULL, NULL, NULL);
            if (joined.reg) return joined;
        }
        return direct_call(c, f, node->argument, discard, NULL, node->propagate);
    }
    SysFuncInfo* info = ((AstSysFuncNode*)ast_unwrap_primary_to_leaf(node->function))->fn_info;
    if (info->fn == SYSPROC_PUSH || info->fn == SYSPROC_SPLICE)
        return mutate_collection(c, node, info);
    if (info->fn == SYSFUNC_ORD) {
        AstFieldNode* index = mvp_string_index(c->module, node->argument);
        if (index) return string_character(c, index, true);
    }
    Type* input_contract = mvp_value_contract(c->module, node->argument, 0, c->dense_arrays);
    TypeId input = input_contract->type_id;
    bool optional = false, identity = false;
    Type* base = lambda_type_nullable_lane_base(input_contract, &optional);
    if (base == &TYPE_INT && sysfunc_native_math_type_preserving(info->fn, &identity)) {
        MirValue operand = convert(c, expression(c, node->argument), VALUE_REP_INT_LANE);
        MIR_reg_t r = identity ? operand.reg : em_int_lane_abs(&c->em, operand.reg);
        return value(c, r, LMD_TYPE_INT, VALUE_REP_INT_LANE, input_contract, node);
    }
    if (info->fn == SYSFUNC_LEN && mvp_value_contract(c->module, node)->type_id == LMD_TYPE_INT) {
        MvpBinding* b = mvp_local_binding(c->module, node->argument);
        MirValue owner = b ? read_binding(c, b, node->argument, false) : expression(c, node->argument);
        if (owner.rep == VALUE_REP_ITEM && lambda_type_accepts_error(owner.semantic_contract)) {
            // an argument-admission failure can join a statically typed call;
            // preserve that Item instead of treating it as a pointer (S7.9.3).
            MIR_reg_t joined = em_new_reg(&c->em, "length_outcome", MIR_T_I64);
            MIR_label_t done = label(c);
            move(c, joined, reg(c, owner.reg));
            branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_URSH, reg(c, owner.reg), imm(c, 56))),
                imm(c, LMD_TYPE_ERROR));
            MIR_reg_t length = em_call_1(&c->em, "fn_len", MIR_T_I64, MIR_T_I64, reg(c, owner.reg), true);
            MirValue result = convert(c, value(c, length, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT), VALUE_REP_ITEM);
            move(c, joined, reg(c, result.reg)); mark(c, done);
            result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
            result.admitted_success_contract = &TYPE_INT;
            return result;
        }
        owner = convert(c, owner, VALUE_REP_RAW_GC_POINTER);
        MIR_reg_t r = input == LMD_TYPE_STRING ? em_call_1(&c->em, "fn_len_s", MIR_T_I64,
            MIR_T_P, reg(c, owner.reg), true) : em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "length");
        return value(c, r, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, node);
    }
    if ((info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_INT) && base &&
            (base->type_id == LMD_TYPE_FLOAT || base->type_id == LMD_TYPE_INT)) {
        Type* output = info->fn == SYSFUNC_FLOAT ? &TYPE_FLOAT : &TYPE_INT;
        MirValue operand = expression(c, node->argument);
        MIR_label_t absent = optional ? label(c) : NULL, done = optional ? label(c) : NULL;
        if (optional) branch_null(c, operand, absent);
        // numeric conversions preserve poison, but null still takes the
        // ordinary failing conversion. Keep the present arm native (S4.1.2).
        operand.semantic_contract = base;
        MirValue converted = convert(c, operand, mvp_rep(output));
        converted = value(c, converted.reg, output->type_id, converted.rep, output, node);
        if (!optional) return converted;
        MIR_reg_t joined = convert(c, converted, VALUE_REP_ITEM).reg;
        jump(c, done); mark(c, absent);
        move(c, joined, reg(c, em_call_1(&c->em, info->c_func_name, MIR_T_I64,
            MIR_T_I64, imm(c, ITEM_NULL), true)));
        mark(c, done);
        MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        result.admitted_success_contract = output;
        return result;
    }
    if (info->fn == SYSFUNC_BNOT) {
        MirValue operand = convert(c, expression(c, node->argument), VALUE_REP_ITEM);
        return value(c, em_call_1(&c->em, "fn_bnot_item", MIR_T_I64, MIR_T_I64, reg(c, operand.reg), true),
            LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    if (info->fn == SYSFUNC_BAND || info->fn == SYSFUNC_BOR || info->fn == SYSFUNC_BXOR || info->fn == SYSFUNC_SHL || info->fn == SYSFUNC_SHR) {
        unsigned operations = 0;
        if (!c->checked_bitwise && bitwise_tree_plan(c, node, &operations) && operations > 1) {
            MIR_label_t checked = label(c), done = label(c);
            MIR_reg_t native = native_bitwise_tree(c, node, checked);
            MIR_reg_t joined = em_box_finite_int_item(&c->em, native);
            jump(c, done); mark(c, checked);
            // pure leaves permit replay on a guard miss; no fast-path write or
            // observable call precedes it. Keep intermediate Items only here.
            c->checked_bitwise = true;
            MirValue fallback = convert(c, call(c, node, discard), VALUE_REP_ITEM);
            c->checked_bitwise = false;
            move(c, joined, reg(c, fallback.reg)); mark(c, done);
            MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
            result.admitted_success_contract = &TYPE_INT;
            return result;
        }
        MirValue a = expression(c, node->argument), b = expression(c, node->argument->next);
        if (info->fn == SYSFUNC_SHR && mvp_int_range(c->module, node).finite)
            return value(c, operation(c, MIR_RSH, reg(c, a.reg), reg(c, b.reg)),
                LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, node);
        // raw bitwise registry entries need their Item classifiers on the fallback arm.
        MIR_reg_t r = em_guarded_int_bitwise(&c->em, info->fn, a, b,
            info->fn == SYSFUNC_BAND ? "fn_band_item" : info->fn == SYSFUNC_BOR ? "fn_bor_item" :
            info->fn == SYSFUNC_BXOR ? "fn_bxor_item" : info->c_func_name);
        MirValue result = value(c, r, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        // the error arm remains a value; only successful native-int results bypass a repeated contract check.
        if (a.rep == VALUE_REP_INT_LANE && b.rep == VALUE_REP_INT_LANE)
            result.admitted_success_contract = &TYPE_INT;
        return result;
    }
    const char* name = info->c_func_name;
    MIR_type_t types[LAMBDA_MAX_FUNCTION_ARGS]; MIR_op_t ops[LAMBDA_MAX_FUNCTION_ARGS];
    ValueRep result_rep = VALUE_REP_ITEM;
    TypeId tid = node->type ? node->type->type_id : LMD_TYPE_ANY;
    bool native_math = mvp_native_math(info, input);
    if (native_math) { name = info->native_c_name; result_rep = VALUE_REP_F64; tid = LMD_TYPE_FLOAT; }
    else if (info->c_ret_type == C_RET_DOUBLE) { result_rep = VALUE_REP_F64; tid = LMD_TYPE_FLOAT; }
    else if (info->c_ret_type == C_RET_STRING) { result_rep = VALUE_REP_RAW_GC_POINTER; tid = LMD_TYPE_STRING; }
    else if (info->c_ret_type == C_RET_INT64) { result_rep = VALUE_REP_INT_LANE; tid = LMD_TYPE_INT; }
    else if (info->c_ret_type == C_RET_BOOL) { result_rep = VALUE_REP_I64; tid = LMD_TYPE_BOOL; }
    else if (info->c_ret_type == C_RET_CONTAINER || info->c_ret_type == C_RET_SYMBOL || info->c_ret_type == C_RET_TYPE_PTR) {
        tid = sysfunc_c_ret_type_id(info);
        result_rep = info->c_ret_type == C_RET_TYPE_PTR ? VALUE_REP_RAW_NON_GC_POINTER : VALUE_REP_RAW_GC_POINTER;
        if (tid == LMD_TYPE_ANY && info->success_type) tid = info->success_type->type_id;
    }
    MirValue arguments[LAMBDA_MAX_FUNCTION_ARGS];
    int i = 0;
    for (AstNode* arg = node->argument; arg; arg = arg->next, i++) {
        arguments[i] = expression(c, arg);
    }
    MIR_label_t done = NULL;
    MIR_reg_t joined = 0;
    if (result_rep != VALUE_REP_ITEM && !sysfunc_observes_error(info)) for (int n = 0; n < i; n++) {
        MirValue arg = arguments[n];
        if (arg.rep != VALUE_REP_ITEM || !lambda_type_accepts_error(arg.semantic_contract)) continue;
        // raw-return C helpers cannot carry ItemError. Classify staged
        // arguments before entering that ABI (S7.9.3, D2.4.3).
        if (!done) { done = label(c); joined = em_new_reg(&c->em, "builtin_outcome", MIR_T_I64); }
        move(c, joined, reg(c, arg.reg));
        branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_URSH, reg(c, arg.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
    }
    for (int n = 0; n < i; n++) {
        MirValue v = convert(c, arguments[n], native_math ? VALUE_REP_F64 :
            sysfunc_arg_required_rep(info, n));
        types[n] = v.mir_type; ops[n] = reg(c, v.reg);
    }
    MIR_reg_t r = em_call_with_args(&c->em, name, em_mir_type_for_rep(result_rep), i, types, ops, true);
    if (info->c_ret_type == C_RET_BOOL) r = operation(c, MIR_AND, reg(c, r), imm(c, UINT8_MAX));
    if (result_rep == VALUE_REP_ITEM && info->fn != SYSPROC_PRINT) tid = LMD_TYPE_ANY;
    MirValue result = value(c, r, tid, result_rep,
        tid == LMD_TYPE_ANY ? &TYPE_ANY : lambda_canonical_type_for_id(tid), node);
    if (done) {
        MirValue boxed = convert(c, result, VALUE_REP_ITEM);
        move(c, joined, reg(c, boxed.reg)); mark(c, done);
        result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        result.admitted_success_contract = lambda_canonical_type_for_id(tid);
    }
    return result;
}

static MirValue array_literal(MvpCompiler* c, AstArrayNode* node, Type* destination) {
    if (!node->item) return value(c, em_call_0(&c->em, "array_plain", MIR_T_I64, true),
        LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY, node);
    TypeId tid = mvp_value_contract(c->module, node->item)->type_id;
    bool packed = tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL;
    for (AstNode* item = node->item; packed && item; item = item->next) {
        bool optional = false;
        Type* contract = mvp_value_contract(c->module, item);
        lambda_type_nullable_lane_base(contract, &optional);
        packed = contract->type_id == tid && !optional;
    }
    if (!packed) {
        MIR_reg_t array = em_call_0(&c->em, "array_plain", MIR_T_P, true);
        MirValue result = value(c, array, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY, node);
        for (AstNode* item = node->item; item; item = item->next) {
            MirValue member = convert(c, expression(c, item), VALUE_REP_ITEM);
            em_call_void_2(&c->em, "array_push_capture", MIR_T_P, reg(c, array),
                MIR_T_I64, reg(c, member.reg), true);
        }
        return result;
    }
    Type* element = tid == LMD_TYPE_FLOAT ? &TYPE_FLOAT : tid == LMD_TYPE_INT ? &TYPE_INT : &TYPE_BOOL;
    ArrayNumElemType lane;
    lambda_array_num_elem_type_for_contract(element, &lane);
    ArrayRepCert* certificate = mvp_array_element(destination) == element && lambda_array_contract_is_plain(destination)
        ? lambda_array_rep_cert_create(c->module->tp->pool, destination) : NULL;
    if (certificate && (certificate->flags & ARRAY_REP_CERT_COUNTED)) certificate = NULL;
    int64_t count = ast_linked_node_count(node->item);
    MIR_reg_t array = certificate
        ? em_call_4(&c->em, "lambda_fill_primitive_bits", MIR_T_I64,
            MIR_T_I64, imm(c, count), MIR_T_I64, imm(c, 0), MIR_T_I64, imm(c, lane),
            MIR_T_P, imm(c, (intptr_t)certificate), true)
        : em_call_2(&c->em, "array_num_new", MIR_T_I64, MIR_T_I64, imm(c, lane), MIR_T_I64, imm(c, count), true);
    MirValue result = value(c, array, LMD_TYPE_ARRAY, VALUE_REP_ITEM, certificate ? destination : &TYPE_ARRAY, node);
    int64_t index = 0;
    for (AstNode* item = node->item; item; item = item->next, index++) {
        MirValue member = convert(c, expression(c, item), mvp_rep(element));
        // Evaluate first, then reload the rooted owner's buffer after any GC.
        MIR_reg_t address = em_array_element_address(&c->em, array, constant(c, index), element == &TYPE_BOOL ? 1 : 8);
        em_store_at(&c->em, address, 0, em_numeric_storage_type(lane), member.reg);
    }
    return result;
}

// native map descriptors determine the packed memory width for every access.
static MIR_type_t native_field_storage_type(const LaneStorageDesc* storage) {
    return storage->kind == LANE_STORAGE_FLOAT64 ? MIR_T_D :
        storage->kind == LANE_STORAGE_BOOL ? MIR_T_U8 : MIR_T_I64;
}

static MirValue map_literal(MvpCompiler* c, AstMapNode* node, TypeMap* destination) {
    int count = ast_linked_node_count(node->item);
    MIR_op_t* members = (MIR_op_t*)mem_alloc(count * sizeof(MIR_op_t), MEM_CAT_TEMP);
    MirValue* staged = (MirValue*)mem_alloc(count * sizeof(MirValue), MEM_CAT_TEMP);
    MIR_reg_t* keys = node->has_computed_key ? (MIR_reg_t*)mem_calloc(count, sizeof(MIR_reg_t), MEM_CAT_TEMP) : NULL;
    int i = 0;
    // stage every expression before construction; precise register roots keep
    // earlier fields alive across recursive producers (D5.3, S7.7.1).
    for (AstNode* item = node->item; item; item = item->next) {
        if (keys) {
            AstNamedNode* named = (AstNamedNode*)item;
            if (!named->is_spread) {
                MirValue key = named->key ? expression(c, named->key) : value(c,
                    em_load_const(&c->em, em_add_const(&c->em, named->name), MIR_T_P),
                    LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING);
                keys[i] = convert(c, key, VALUE_REP_ITEM).reg;
            }
        }
        AstNode* source = item->node_type == AST_NODE_KEY_EXPR ? ((AstNamedNode*)item)->as : item;
        MirValue member = expression(c, source);
        staged[i++] = capture_projection(c, member, source);
    }
    if (keys) {
        // keys and values retain source order before the fresh map is exposed;
        // the shared literal helpers enforce NameKey and spread semantics.
        MIR_reg_t owner = em_call_0(&c->em, "map_literal_begin", MIR_T_I64, true);
        value(c, owner, LMD_TYPE_MAP, VALUE_REP_ITEM, &TYPE_MAP);
        i = 0;
        for (AstNode* item = node->item; item; item = item->next, i++) {
            MirValue member = convert(c, staged[i], VALUE_REP_ITEM);
            MIR_reg_t result = keys[i]
                ? em_call_3(&c->em, "map_literal_put", MIR_T_I64, MIR_T_I64, reg(c, owner),
                    MIR_T_I64, reg(c, keys[i]), MIR_T_I64, reg(c, member.reg), true)
                : em_call_2(&c->em, "map_literal_spread", MIR_T_I64, MIR_T_I64, reg(c, owner), MIR_T_I64, reg(c, member.reg), true);
            MIR_label_t accepted = label(c);
            branch(c, MIR_BNE, accepted, reg(c, operation(c, MIR_URSH, reg(c, result), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
            stage_error(c, result); mark(c, accepted);
        }
        mem_free(keys); mem_free(members); mem_free(staged);
        return value(c, owner, LMD_TYPE_MAP, VALUE_REP_ITEM, &TYPE_MAP, node);
    }
    TypeMap* shape = (TypeMap*)node->type;
    MvpErrorTarget failure = {};
    if (destination && ast_map_literal_keys_follow_contract(node, destination)) {
        shape = destination;
        ShapeEntry* field = shape->shape;
        for (int n = 0; n < count; n++, field = typemap_next_field(shape, field)) {
            staged[n] = admit(c, staged[n], field->type, &failure);
        }
    }
    bool native = ast_map_literal_keys_follow_contract(node, shape);
    int field_index = 0;
    for (ShapeEntry* field = shape->shape; native && field;
            field = typemap_next_field(shape, field), field_index++) {
        const LaneStorageDesc* storage = shape_entry_storage(field);
        // an inferred field layout is not admission of an error-bearing
        // producer. Keep those Items in the shared descriptor fill (D3.3.3v3).
        native = storage->native && shape_entry_storage_fits_data(field, shape->byte_size) &&
            mvp_boundary_proven(staged[field_index].semantic_contract, field->type);
    }
    ShapeEntry* prepared_field = shape->shape;
    for (int n = 0; n < count; n++) {
        // keep scalar fields in their native lanes across the allocation;
        // only the generic fill ABI needs a staged Item (D2.4.3, D5.3).
        staged[n] = convert(c, staged[n], native ? mvp_rep(prepared_field->type) : VALUE_REP_ITEM);
        if (native) prepared_field = typemap_next_field(shape, prepared_field);
        else members[n] = reg(c, staged[n].reg);
    }
    MIR_reg_t map = em_call_3(&c->em, "map_alloc_for_type", MIR_T_P,
        MIR_T_P, imm(c, (intptr_t)shape), MIR_T_P, imm(c, 0), MIR_T_I64, imm(c, 0), true);
    value(c, map, LMD_TYPE_MAP, VALUE_REP_RAW_GC_POINTER, shape);
    MIR_reg_t filled = map;
    if (native) {
        ShapeEntry* field = shape->shape;
        for (int n = 0; n < count; n++, field = typemap_next_field(shape, field)) {
            const LaneStorageDesc* storage = shape_entry_storage(field);
            // descriptors authorize the exact slot width. The allocation
            // precedes loading the owner data, so no interior pointer crosses GC.
            MIR_reg_t data = em_load_at(&c->em, map, LAMBDA_GC_OFF_MAP_DATA, MIR_T_P, "map_data");
            MIR_type_t lane = native_field_storage_type(storage);
            em_store_at(&c->em, data, field->byte_offset, lane, staged[n].reg);
        }
    } else {
        MIR_var_t fixed[] = {{MIR_T_P, "owner", 0}};
        MIR_op_t owner[] = {reg(c, map)};
        filled = em_emit_vararg_call(&c->em, "map_fill", MIR_T_P, 1, 1, fixed, owner, count, members);
    }
    mem_free(members);
    mem_free(staged);
    if (failure.done) {
        MIR_reg_t joined = em_new_reg(&c->em, "map_join", MIR_T_I64);
        MIR_label_t done = label(c);
        move(c, joined, reg(c, filled)); jump(c, done);
        mark(c, failure.done); move(c, joined, reg(c, failure.error)); mark(c, done);
        MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        result.admitted_success_contract = shape;
        return result;
    }
    return value(c, filled, LMD_TYPE_MAP, VALUE_REP_RAW_GC_POINTER, shape, node);
}

static MirValue member_read(MvpCompiler* c, AstFieldNode* node) {
    MvpBinding* binding = mvp_local_binding(c->module, node->object);
    MirValue owner = binding ? read_binding(c, binding, node->object, false) : expression(c, node->object);
    TypeMap* shape = NULL;
    ShapeEntry* field = mvp_member_field(c->module, node, &shape);
    Type* contract = mvp_value_contract(c->module, node);
    ValueRep rep = mvp_rep(contract);
    MIR_reg_t joined = em_new_reg(&c->em, "member", em_mir_type_for_rep(rep));
    MIR_label_t slow = label(c), done = label(c);
    const LaneStorageDesc* storage = field ? shape_entry_storage(field) : NULL;
    bool container_slot = storage && storage->kind == LANE_STORAGE_POINTER &&
        (storage->value_domain == LMD_TYPE_MAP || storage->value_domain == LMD_TYPE_ARRAY ||
         storage->value_domain == LMD_TYPE_ARRAY_NUM || storage->value_domain == LMD_TYPE_ELEMENT ||
         storage->value_domain == LMD_TYPE_RANGE);
    if (field && shape && shape->is_trusted_contract &&
            (storage->native || container_slot) && shape_entry_storage_fits_data(field, shape->byte_size)) {
        owner = convert(c, owner, VALUE_REP_ITEM);
        // the enforced receiver contract already proves map-or-null; retain
        // null/layout guards without repeating tagged-kind classification.
        MIR_reg_t pointer = owner.reg;
        if (binding && (binding->record_readable || binding->record_writable))
            branch(c, MIR_BEQ, slow,
                reg(c, binding->record_readable ? binding->record_readable : binding->record_writable), imm(c, 0));
        else pointer = em_guard_map_shape(&c->em, owner.reg, constant(c, (intptr_t)shape), slow,
            lambda_type_nonnull_map_contract(owner.semantic_contract) != NULL, true);
        MIR_type_t lane = native_field_storage_type(storage);
        MIR_reg_t data = em_load_at(&c->em, pointer, LAMBDA_GC_OFF_MAP_DATA, MIR_T_P, "member_data");
        Type* field_contract = type_field_unwrap_simple_decl(field->type);
        // packed container slots use raw pointers even when the array contract
        // itself has Item transport. Decode the descriptor, not the type AST.
        MirValue member = value(c, em_load_at(&c->em, data, field->byte_offset, lane, "member_lane"),
            container_slot ? (TypeId)storage->value_domain : field_contract->type_id,
            container_slot ? VALUE_REP_RAW_GC_POINTER : mvp_rep(field_contract), field_contract, node);
        member = convert(c, member, rep);
        move(c, joined, reg(c, member.reg), member.mir_type); jump(c, done);
    }
    mark(c, slow);
    AstIdentNode* key = (AstIdentNode*)ast_unwrap_primary_to_leaf(node->field);
    MIR_reg_t name = em_load_const(&c->em, em_add_const(&c->em, key->name), MIR_T_P);
    MirValue key_value = value(c, name, LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING);
    owner = convert(c, owner, VALUE_REP_ITEM); key_value = convert(c, key_value, VALUE_REP_ITEM);
    MIR_reg_t result = em_call_2(&c->em, "fn_member", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, key_value.reg), true);
    MirValue member = convert(c, value(c, result, contract->type_id, VALUE_REP_ITEM, contract, node), rep);
    move(c, joined, reg(c, member.reg), member.mir_type); mark(c, done);
    return value(c, joined, contract->type_id, rep, contract, node);
}

static void member_write(MvpCompiler* c, AstAssignStamNode* node) {
    MvpBinding* binding = mvp_local_binding(c->module, node->object);
    AstIdentNode* key = (AstIdentNode*)ast_unwrap_primary_to_leaf(node->key);
    MIR_reg_t name = em_load_const(&c->em, em_add_const(&c->em, key->name), MIR_T_P);
    MirValue key_value = convert(c, value(c, name, LMD_TYPE_STRING,
        VALUE_REP_RAW_GC_POINTER, &TYPE_STRING), VALUE_REP_ITEM);
    MirValue input = expression(c, node->value);
    AstFieldNode access = {}; access.object = node->object; access.field = node->key;
    TypeMap* shape = NULL;
    ShapeEntry* field = mvp_member_field(c->module, &access, &shape);
    MIR_label_t slow = label(c), done = label(c);
    if (field && shape->is_trusted_contract && shape_entry_storage(field)->native &&
            shape_entry_storage_fits_data(field, shape->byte_size) &&
            mvp_boundary_proven(input.semantic_contract, field->type)) {
        const LaneStorageDesc* storage = shape_entry_storage(field);
        // pointer captures and wide scalar homes retain the checked writer.
        if (storage->kind == LANE_STORAGE_INT || storage->kind == LANE_STORAGE_FLOAT64 ||
                storage->kind == LANE_STORAGE_BOOL) {
            MIR_reg_t pointer = binding->reg;
            if (binding->record_writable) branch(c, MIR_BEQ, slow, reg(c, binding->record_writable), imm(c, 0));
            else {
                pointer = em_guard_map_shape(&c->em, binding->reg, constant(c, (intptr_t)shape), slow, true, true);
                array_writable_guard(c, binding, slow);
            }
            MirValue member = convert(c, input, mvp_rep(field->type));
            MIR_reg_t data = em_load_at(&c->em, pointer, LAMBDA_GC_OFF_MAP_DATA, MIR_T_P, "member_write_data");
            MIR_type_t lane = native_field_storage_type(storage);
            em_store_at(&c->em, data, field->byte_offset, lane, member.reg);
            jump(c, done);
        }
    }
    mark(c, slow);
    input = convert(c, input, VALUE_REP_ITEM);
    MIR_reg_t type = em_load_const(&c->em, em_add_const(&c->em, binding->contract), MIR_T_P);
    MIR_reg_t result = em_call_5(&c->em, "lambda_map_set_checked", MIR_T_I64,
        MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, key_value.reg),
        MIR_T_I64, reg(c, input.reg), MIR_T_P, reg(c, type),
        MIR_T_P, imm(c, (intptr_t)binding->entry->name->chars), true);
    MIR_label_t accepted = label(c);
    branch(c, MIR_BNE, accepted, reg(c, operation(c, MIR_URSH, reg(c, result), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
    stage_error(c, result); mark(c, accepted);
    move(c, binding->reg, reg(c, result));
    refresh_record(c, binding);
    if (binding->var_home) em_store_at(&c->em, binding->var_home, 0, MIR_T_I64, binding->reg);
    mark(c, done);
}
static void return_value(MvpCompiler* c, MirValue v, AstNode* node) {
    Type* contract = c->function->result_contract;
    v = capture_projection(c, v, node, true);
    // native lanes cannot hold ItemError. Admit their nullable success domain
    // directly so only absence reaches the validator (D2.4.3).
    if (c->function->declared_result && c->function->declared_result != contract &&
            v.rep == VALUE_REP_ITEM && lambda_type_accepts_error(v.semantic_contract) &&
            !mvp_boundary_proven(v.semantic_contract, contract)) {
        v = admit(c, v, c->function->declared_result);
        if (v.rep == VALUE_REP_ITEM) {
            MIR_label_t clean = label(c);
            branch(c, MIR_BNE, clean, reg(c, operation(c, MIR_URSH, reg(c, v.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
            stage_error(c, v.reg); mark(c, clean);
        }
        v = value(c, v.reg, contract->type_id, v.rep, contract, node);
    }
    v = admit(c, v, contract);
    v = convert(c, v, c->function->abi.result.normal.abi_rep);
    if (c->inlining) {
        move(c, c->inlining->result, reg(c, v.reg), v.mir_type);
        jump(c, c->inlining->done);
        return;
    }
    em_stage_function_return(&c->em, reg(c, v.reg));
}

static MirValue propagated_value(MvpCompiler* c, MirValue v, AstNode* node) {
    if (v.rep != VALUE_REP_ITEM || !lambda_type_accepts_error(v.semantic_contract)) return v;
    MIR_label_t clean = label(c);
    branch(c, MIR_BNE, clean, reg(c, operation(c, MIR_URSH, reg(c, v.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
    return_value(c, value(c, v.reg, LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR), node);
    mark(c, clean);
    Type* success = v.admitted_success_contract ? v.admitted_success_contract :
        lambda_type_remove_error(c->module->tp->pool, node ? node->type : v.semantic_contract);
    return convert(c, value(c, v.reg, success->type_id, VALUE_REP_ITEM, success, node), mvp_rep(success));
}
static bool tail_call(MvpCompiler* c, AstNode* node) {
    node = ast_unwrap_primary_to_leaf(node);
    MvpFunction* f = c->function;
    if (!c->tail_entry || mvp_parameter_receiver(f->ast) || !node || node->node_type != AST_NODE_CALL_EXPR ||
            ast_direct_call_function((AstCallNode*)node) != f->ast || f->abi.effects.may_reenter ||
            f->abi.result.normal.abi_rep == VALUE_REP_ITEM) return false;
    AstNamedNode* p = f->ast->param;
    AstNode* args = ((AstCallNode*)node)->argument;
    bool stable_arrays = false;
    for (AstNode* a = args; a; a = a->next, p = (AstNamedNode*)p->next) {
        MvpBinding* b = mvp_binding(c->module, p->entry);
        if (b->rep == VALUE_REP_ITEM || b->rep == VALUE_REP_RAW_GC_POINTER) {
            if (!mvp_array_element(b->contract) || mvp_local_binding(c->module, a) != b ||
                    !mvp_parameter_borrowable(c->module, p)) return false;
            stable_arrays = true;
        } else if (lambda_static_boundary_relation(mvp_value_contract(c->module, a), b->contract, true)
                != STATIC_BOUNDARY_PROVEN) return false;
    }
    if (!stable_arrays && (f->abi.effects.may_gc || f->abi.effects.may_return_error)) return false;
    MirValue values[LAMBDA_MAX_FUNCTION_ARGS]; int i = 0;
    p = f->ast->param;
    // evaluate all arguments before replacing any parameter, including on an inlined self edge.
    for (AstNode* a = args; a; a = a->next, p = (AstNamedNode*)p->next, i++) {
        MvpBinding* b = mvp_binding(c->module, p->entry);
        // unchanged read-only owners remain in the same precise root frame;
        // only scalar arguments change, so storage witnesses stay valid (D5.3.1).
        values[i] = admit(c, mvp_array_element(b->contract) ? read_binding(c, b, a, false) :
            expression(c, a), mvp_contract(p));
    }
    i = 0;
    for (p = f->ast->param; p; p = (AstNamedNode*)p->next, i++)
        move(c, mvp_binding(c->module, p->entry)->reg, reg(c, values[i].reg), values[i].mir_type);
    jump(c, c->tail_entry);
    return true;
}
static bool match_subject_arm(MvpCompiler* c, AstMatchNode* match, AstMatchArm* arm) {
    MvpBinding* subject = mvp_local_binding(c->module, match->scrutinee);
    return subject && mvp_local_binding(c->module, arm->body) == subject;
}

static Type* native_match_contract(MvpCompiler* c, AstMatchNode* match, MirValue subject) {
    Type* contract = mvp_contextual_contract(c->module, match);
    // an explicit scalar boundary owns any promotion between arm carriers;
    // an ordinary match must preserve the selected value's original tag.
    if (!contract) return NULL;
    ValueRep rep = mvp_rep(contract);
    if (rep != VALUE_REP_F64 && rep != VALUE_REP_INT_LANE &&
            !(rep == VALUE_REP_I64 && contract == &TYPE_BOOL)) return NULL;
    Type* success = subject.admitted_success_contract ? subject.admitted_success_contract : subject.semantic_contract;
    bool error_covered = !lambda_type_accepts_error(subject.semantic_contract), success_covered = false;
    for (AstMatchArm* arm = match->first_arm; arm; arm = (AstMatchArm*)arm->next) {
        Type* pattern = arm->pattern ? mvp_match_type(arm->pattern) : NULL;
        Type* body = pattern && match_subject_arm(c, match, arm)
            ? pattern : mvp_value_contract(c->module, arm->body);
        if (!mvp_boundary_proven(body, contract)) return NULL;
        if (!pattern) return contract;
        error_covered |= pattern == &TYPE_ERROR;
        success_covered |= type_field_unwrap_simple_decl(success) == pattern;
    }
    // an unhandled scrutinee keeps the generic nullable completion. Only a
    // complete numeric outcome may select native result lanes (S11.2.1).
    return error_covered && success_covered ? contract : NULL;
}

static void bind_iteration(MvpCompiler* c, NameEntry* entry, MirValue input) {
    MvpBinding* binding = mvp_binding(c->module, entry);
    if (!binding) return;
    input.semantic_contract = binding->contract;
    input.semantic_type = binding->contract->type_id;
    input = convert(c, input, binding->rep);
    move(c, binding->reg, reg(c, input.reg), input.mir_type);
}

static MIR_reg_t query_key(MvpCompiler* c, AstNode* first, bool group) {
    MIR_reg_t tuple = first && first->next ? em_call_0(&c->em, "array_plain", MIR_T_P, true) : 0;
    if (tuple) value(c, tuple, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
    for (AstNode* key = first; key; key = key->next) {
        MirValue part = convert(c, expression(c, group ? ((AstGroupKey*)key)->expr : ((AstOrderSpec*)key)->expr), VALUE_REP_ITEM);
        if (!tuple) return part.reg;
        em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P, reg(c, tuple), MIR_T_I64, reg(c, part.reg), true);
    }
    return tuple;
}

static void collect_query_body(MvpCompiler* c, AstForNode* node, MIR_reg_t output, MIR_reg_t order_keys) {
    if (node->discard_result) expression(c, node->then, true);
    else {
        MirValue result = convert(c, expression(c, node->then), VALUE_REP_ITEM);
        em_call_void_2(&c->em, node->order ? "array_push_verbatim" : "array_push_spread", MIR_T_P,
            reg(c, output), MIR_T_I64, reg(c, result.reg), true);
    }
    if (node->order) {
        MIR_reg_t key = query_key(c, node->order, false);
        if (!node->discard_result) em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P,
            reg(c, order_keys), MIR_T_I64, reg(c, key), true);
    }
}

static bool for_item_position(MvpCompiler* c, AstNode* source) {
    AstIndex* index = &c->module->tp->ast_index;
    for (AstNodeId id = ast_index_find(index, source); id < index->count;) {
        AstNodeId parent = ast_index_parent_id(index, id);
        if (parent >= index->count) return false;
        AstNode* node = index->nodes[parent];
        if (node->node_type == AST_NODE_ARRAY) return true;
        if (node->node_type == AST_NODE_FOR_EXPR) return ((AstForNode*)node)->then == index->nodes[id];
        if (node->node_type != AST_NODE_PRIMARY && node->node_type != AST_NODE_IF_EXPR) return false;
        id = parent;
    }
    return false;
}

static MirValue collection_for(MvpCompiler* c, AstForNode* node) {
    AstLoopNode* source = (AstLoopNode*)node->loop;
    MirValue owner = convert(c, expression(c, source->as), VALUE_REP_ITEM);
    // the source is one retained snapshot, including when the loop body
    // replaces a binding or mutates a nested place (S9.2.3).
    owner = value(c, em_call_1(&c->em, "cow_capture_value", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    MIR_reg_t keys = em_call_1(&c->em, "item_keys", MIR_T_P, MIR_T_I64, reg(c, owner.reg), true);
    MIR_reg_t length = em_call_3(&c->em, "iter_len", MIR_T_I64, MIR_T_I64, reg(c, owner.reg),
        MIR_T_P, reg(c, keys), MIR_T_I64, imm(c, source->key_filter), true);
    MIR_reg_t group_rows = 0, group_keys = 0, order_keys = 0;
    if (node->group) {
        group_rows = em_call_0(&c->em, "array_plain", MIR_T_P, true);
        value(c, group_rows, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
        group_keys = em_call_0(&c->em, "array_plain", MIR_T_P, true);
        value(c, group_keys, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
    }
    MIR_reg_t index = constant(c, 0), output = 0;
    if (!node->discard_result) {
        if (node->order) {
            order_keys = em_call_0(&c->em, "array_plain", MIR_T_P, true);
            value(c, order_keys, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
        }
        // query streams retain nulls and adjacent strings; content lists use
        // different append rules and cannot represent these rows (S2.5.2v2).
        output = em_call_0(&c->em, node->order ? "array_plain" : "array_spreadable", MIR_T_P, true);
        value(c, output, LMD_TYPE_ARRAY, VALUE_REP_RAW_GC_POINTER, &TYPE_ARRAY);
    }
    MvpControl loop = {c->control, label(c), label(c)};
    MIR_label_t test = label(c), body = label(c);
    jump(c, test); mark(c, body); c->control = &loop;
    MIR_reg_t item = em_call_4(&c->em, source->key_only ? "iter_key_at" : "iter_val_at", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), MIR_T_P, reg(c, keys), MIR_T_I64, reg(c, index),
        MIR_T_I64, imm(c, source->key_filter), true);
    bind_iteration(c, source->entry, value(c, item, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY));
    if (source->index_entry) {
        MIR_reg_t key = em_call_4(&c->em, "iter_key_at", MIR_T_I64, MIR_T_I64, reg(c, owner.reg),
            MIR_T_P, reg(c, keys), MIR_T_I64, reg(c, index), MIR_T_I64, imm(c, source->key_filter), true);
        bind_iteration(c, source->index_entry, value(c, key, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY));
    }
    if (node->where) boolean_branch(c, node->where, loop.next, false);
    if (node->group) {
        MIR_reg_t key = query_key(c, (AstNode*)node->group->keys, true);
        em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P, reg(c, group_rows), MIR_T_I64, reg(c, item), true);
        em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P, reg(c, group_keys), MIR_T_I64, reg(c, key), true);
    } else collect_query_body(c, node, output, order_keys);
    mark(c, loop.next); move(c, index, reg(c, operation(c, MIR_ADD, reg(c, index), imm(c, 1))));
    mark(c, test); branch(c, MIR_BLT, body, reg(c, index), reg(c, length));
    mark(c, loop.done); c->control = loop.parent;
    em_call_void_1(&c->em, "symbol_key_list_free", MIR_T_P, reg(c, keys), true);
    if (node->group) {
        // the common runtime groups stable source rows; post-group bindings
        // receive each record [members, key...] (S14.1.1v2).
        MIR_reg_t groups = em_call_3(&c->em, "fn_group_by_keys", MIR_T_P, MIR_T_I64, reg(c, group_rows),
            MIR_T_I64, reg(c, group_keys), MIR_T_I64, imm(c, node->group->key_count), true);
        value(c, groups, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
        MIR_reg_t size = em_call_1(&c->em, "fn_len", MIR_T_I64, MIR_T_I64, reg(c, groups), true);
        MIR_reg_t cursor = constant(c, 0);
        MvpControl grouped = {c->control, label(c), label(c)};
        MIR_label_t head = label(c);
        c->control = &grouped; mark(c, head);
        branch(c, MIR_BGE, grouped.done, reg(c, cursor), reg(c, size));
        MIR_reg_t record = em_call_2(&c->em, "item_at", MIR_T_I64, MIR_T_I64, reg(c, groups), MIR_T_I64, reg(c, cursor), true);
        value(c, record, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
        int position = 0;
        auto bind_part = [&](NameEntry* entry) {
            MIR_reg_t part = em_call_2(&c->em, "item_at", MIR_T_I64, MIR_T_I64, reg(c, record), MIR_T_I64, imm(c, position++), true);
            bind_iteration(c, entry, value(c, part, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY));
        };
        bind_part(node->group->entry);
        for (AstGroupKey* key = node->group->keys; key; key = (AstGroupKey*)key->next) bind_part(key->entry);
        collect_query_body(c, node, output, order_keys);
        mark(c, grouped.next); move(c, cursor, reg(c, operation(c, MIR_ADD, reg(c, cursor), imm(c, 1))));
        jump(c, head); mark(c, grouped.done); c->control = grouped.parent;
    }
    if (node->discard_result) return null_value(c);
    if (node->order) {
        int64_t count = 0, mask = ast_order_desc_mask(node->order, &count);
        output = em_call_4(&c->em, "fn_order_rows", MIR_T_P, MIR_T_I64, reg(c, output), MIR_T_I64, reg(c, order_keys),
            MIR_T_I64, imm(c, mask), MIR_T_I64, imm(c, count), true);
        value(c, output, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
    }
    return value(c, em_call_1(&c->em, for_item_position(c, node) ? "list_collapse_item" : "list_collapse_value",
        MIR_T_I64, MIR_T_I64, reg(c, output), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
}

static MirValue expression(MvpCompiler* c, AstNode* node, bool discard) {
    if (!node || c->module->failed) return null_value(c);
    switch (node->node_type) {
    case AST_NODE_PRIMARY:
        return ((AstPrimaryNode*)node)->expr ? expression(c, ((AstPrimaryNode*)node)->expr, discard)
            : literal(c, (AstPrimaryNode*)node);
    case AST_NODE_IDENT: {
        NameEntry* entry = ((AstIdentNode*)node)->entry;
        if (entry && ast_definition_denotes_type(entry->node))
            return value(c, constant(c, (intptr_t)entry->node->type),
                LMD_TYPE_TYPE, VALUE_REP_RAW_NON_GC_POINTER, &TYPE_TYPE, node);
        MvpBinding* b = mvp_binding(c->module, entry);
        if (!b && entry && entry->node && !entry->is_mutable && entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            Script* owner = entry->import_owner ? entry->import_owner : entry->import ? entry->import->script : NULL;
            MirValue imported = owner ? pooled_constant(c, ((AstDeclaratorNode*)entry->node)->init, owner->const_list) : MirValue{};
            if (imported.reg) return imported;
            if (owner) {
                StrBuf* name = strbuf_new_cap(64);
                write_var_name_for_script(name, entry->node, owner);
                MIR_item_t reference = find_import(c->em.ctx, name->str);
                if (!reference) reference = MIR_new_import(c->em.ctx, name->str);
                strbuf_free(name);
                MIR_reg_t slot;
                MIR_reg_t state = module_binding_location(c, reference, &slot);
                MIR_reg_t item = em_call_2(&c->em, "lambda_module_var_at", MIR_T_I64,
                    MIR_T_P, reg(c, state), MIR_T_I64, reg(c, slot), true);
                value(c, item, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
                return value(c, em_call_1(&c->em, "cow_capture_value", MIR_T_I64, MIR_T_I64, reg(c, item), true),
                    LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
            }
        }
        return read_binding(c, b, node, true);
    }
    case AST_NODE_TYPE: {
        TypeId type_id;
        TypeType* singleton = lambda_type_node_singleton(node->type, &type_id);
        MIR_reg_t type = singleton ? constant(c, (intptr_t)singleton) :
            em_call_1(&c->em, "base_type", MIR_T_P, MIR_T_I64, imm(c, type_id), true);
        return value(c, type, LMD_TYPE_TYPE, VALUE_REP_RAW_NON_GC_POINTER, &TYPE_TYPE, node);
    }
    case AST_NODE_PATH_EXPR:
        return value(c, em_literal_path(&c->em, (AstPathNode*)node), LMD_TYPE_PATH,
            VALUE_REP_RAW_GC_POINTER, &TYPE_PATH, node);
    case AST_NODE_FUNC_EXPR: {
        MvpFunction* function = mvp_function(c->module, (AstFuncNode*)node);
        MIR_reg_t address = em_new_reg(&c->em, "function_address", MIR_T_P);
        move(c, address, MIR_new_ref_op(c->em.ctx, function->target));
        MIR_reg_t object = em_plain_function_value(&c->em, address, function->abi.param_count,
            function->ast->name ? function->ast->name->chars : NULL, function->ast->type, false, true,
            LAMBDA_MIR_PUBLIC_RETURN_ITEM_COMPANION);
        return value(c, object, LMD_TYPE_FUNC, VALUE_REP_RAW_GC_POINTER, &TYPE_FUNC, node);
    }
    case AST_NODE_RAISE_EXPR: {
        AstRaiseNode* raised = (AstRaiseNode*)node;
        MirValue operand = convert(c, expression(c, raised->value), VALUE_REP_ITEM);
        MIR_reg_t error = em_call_4(&c->em, "lambda_raise_operand", MIR_T_I64,
            MIR_T_I64, reg(c, operand.reg), MIR_T_P, imm(c, (intptr_t)c->module->tp->reference),
            MIR_T_I64, imm(c, raised->site_line), MIR_T_I64, imm(c, raised->site_column), true);
        return_value(c, value(c, error, LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR), node);
        return null_value(c);
    }
    case AST_NODE_CURRENT_ERROR:
        if (!c->current_error) { mvp_reject(c->module, node, "current error outside a handler"); return null_value(c); }
        return value(c, c->current_error, LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR, node);
    case AST_NODE_INDEX_EXPR: return array_read(c, (AstFieldNode*)node);
    case AST_NODE_MEMBER_EXPR: return member_read(c, (AstFieldNode*)node);
    case AST_NODE_ARRAY: return array_literal(c, (AstArrayNode*)node);
    case AST_NODE_MAP: return map_literal(c, (AstMapNode*)node);
    case AST_NODE_BINARY: return binary(c, (AstBinaryNode*)node);
    case AST_NODE_CALL_EXPR: {
        MirValue result = call(c, (AstCallNode*)node, discard);
        return ((AstCallNode*)node)->propagate ? propagated_value(c, result, node) : result;
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* n = (AstUnaryNode*)node;
        MirValue v = expression(c, n->operand);
        if (n->op == OPERATOR_PROPAGATE) return propagated_value(c, v, node);
        if (n->op == OPERATOR_NOT) {
            if (v.semantic_type == LMD_TYPE_BOOL) return value(c, operation(c, MIR_EQ,
                reg(c, truth(c, v)), imm(c, 0)), LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, node);
            v = convert(c, v, VALUE_REP_ITEM);
            return value(c, em_call_1(&c->em, "fn_not_value", MIR_T_I64, MIR_T_I64, reg(c, v.reg), true),
                LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        }
        if (n->op == OPERATOR_POS && (v.semantic_type == LMD_TYPE_INT || v.semantic_type == LMD_TYPE_FLOAT)) return v;
        if (v.semantic_type == LMD_TYPE_INT) {
            AstBinaryNode subtraction = {}; subtraction.op = OPERATOR_SUB; subtraction.type = node->type;
            MirValue result = int_arithmetic(c, &subtraction, value(c, constant(c, 0), LMD_TYPE_INT, VALUE_REP_INT_LANE),
                convert(c, v, VALUE_REP_INT_LANE));
            // provenance must name the retained unary AST, never the temporary lowering descriptor.
            result.provenance_node = node;
            return result;
        }
        if (v.semantic_type == LMD_TYPE_FLOAT) {
            v = convert(c, v, VALUE_REP_F64);
            MIR_reg_t r = em_new_reg(&c->em, "negate", MIR_T_D);
            MIR_label_t done = label(c);
            move(c, r, reg(c, v.reg), MIR_T_D);
            branch_null(c, v, done);
            em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_DNEG, reg(c, r), reg(c, v.reg)));
            mark(c, done);
            return value(c, r, LMD_TYPE_FLOAT, VALUE_REP_F64, v.semantic_contract, node);
        }
        v = convert(c, v, VALUE_REP_ITEM);
        return value(c, em_call_1(&c->em, n->op == OPERATOR_NEG ? "fn_neg" : "fn_pos", MIR_T_I64,
            MIR_T_I64, reg(c, v.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    case AST_NODE_EXPR_STMT: return expression(c, ((AstExprStmtNode*)node)->expression, discard);
    case AST_NODE_CONTENT: return statements(c, ((AstListNode*)node)->item, discard);
    case AST_NODE_BLOCK: return statements(c, ((AstBlockNode*)node)->statements, discard);
    case AST_SCRIPT: return statements(c, ((AstScript*)node)->child, discard);
    case AST_NODE_FUNC: case AST_NODE_PROC: case AST_NODE_TYPE_STAM: case AST_NODE_IMPORT: return null_value(c);
    case AST_NODE_PUB_STAM:
        for (AstNode* d = ((AstLetNode*)node)->declare; d; d = d->next) expression(c, d);
        return null_value(c);
    case AST_NODE_LET_STAM: case AST_NODE_VAR_STAM:
        for (AstNode* d = ((AstVarDeclNode*)node)->declarations; d; d = d->next) expression(c, d);
        return null_value(c);
    case AST_NODE_VARIABLE_DECLARATOR: {
        AstDeclaratorNode* n = (AstDeclaratorNode*)node;
        if (n->is_type_definition) return null_value(c);
        MvpBinding* b = mvp_binding(c->module, n->entry);
        if (!b || !b->reg) {
            mvp_reject(c->module, node, "declaration requires a local scalar binding"); return null_value(c);
        }
        b->scope = c->scope;
        store_binding(c, b, binding_value(c, b, n->init)); return null_value(c);
    }
    case AST_NODE_ASSIGN_STAM: case AST_NODE_INDEX_ASSIGN_STAM: {
        AstAssignStamNode* n = (AstAssignStamNode*)node;
        if (n->object) {
            if (!mvp_local_binding(c->module, n->object)) {
                MvpPlace place = place_path(c, n->object, n->key);
                if (!place.root) mvp_reject(c->module, node, "assignment requires a resolved place");
                else write_place(c, place, expression(c, n->value));
            } else array_write(c, n);
            return null_value(c);
        }
        MvpBinding* b = mvp_binding(c->module, n->target_entry);
        if (!b || b->owner != c->function->id) {
            mvp_reject(c->module, node, "assignment requires a local scalar binding"); return null_value(c);
        }
        store_binding(c, b, binding_value(c, b, n->value)); return null_value(c);
    }
    case AST_NODE_MEMBER_ASSIGN_STAM: {
        AstAssignStamNode* n = (AstAssignStamNode*)node;
        if (mvp_local_binding(c->module, n->object)) member_write(c, n);
        else {
            MvpPlace place = place_path(c, n->object, n->key, true);
            if (!place.root) mvp_reject(c->module, node, "assignment requires a resolved place");
            else write_place(c, place, expression(c, n->value));
        }
        return null_value(c);
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* n = (AstIfNode*)node;
        MIR_label_t otherwise = label(c), done = label(c);
        bool optional_condition = false;
        Type* condition_contract = lambda_type_nullable_lane_base(mvp_value_contract(c->module, n->cond), &optional_condition);
        bool bool_condition = condition_contract && condition_contract->type_id == LMD_TYPE_BOOL;
        MirValue condition = bool_condition ? MirValue{} : expression(c, n->cond);
        bool error_condition = !bool_condition && condition.semantic_type == LMD_TYPE_ANY;
        ValueRep rep = error_condition || ast_expr_may_defect(node, 0) ? VALUE_REP_ITEM : mvp_rep(node->type);
        // pointer and Item joins share the i64 width, so reserve the original
        // result register before lowering either branch.
        MIR_reg_t r = em_new_reg(&c->em, "if_result", em_mir_type_for_rep(rep));
        if (error_condition) {
            condition = convert(c, condition, VALUE_REP_ITEM); move(c, r, reg(c, condition.reg));
            branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_URSH, reg(c, condition.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        }
        if (bool_condition) boolean_branch(c, n->cond, otherwise, false);
        else branch(c, MIR_BEQ, otherwise, reg(c, truth(c, condition)), imm(c, 0));
        // discarded branches retain evaluation and scope exits, without converting
        // their unused null results into the inferred numeric join (D8.2.6).
        // guard a closed array arm even when a surrounding loop also updates its index elsewhere.
        MirValue yes = null_value(c);
        if (discard) array_iteration(c, n->then);
        else yes = expression(c, n->then);
        MIR_insn_t yes_end = c->em.insert_after ? c->em.insert_after :
            DLIST_TAIL(MIR_insn_t, c->em.func_item->u.func->insns);
        jump(c, done); mark(c, otherwise);
        MirValue no = null_value(c);
        if (discard) array_iteration(c, n->otherwise);
        else no = expression(c, n->otherwise);
        if (!discard) {
            // select the join from emitted carriers: a call's checked actuals
            // may add an error arm absent from its inferred result (D2.4.3).
            if ((rep == VALUE_REP_RAW_GC_POINTER || rep == VALUE_REP_RAW_NON_GC_POINTER) &&
                    ((yes.rep == VALUE_REP_ITEM && mvp_rep(yes.semantic_contract) == VALUE_REP_ITEM) ||
                     (no.rep == VALUE_REP_ITEM && mvp_rep(no.semantic_contract) == VALUE_REP_ITEM))) rep = VALUE_REP_ITEM;
            MirValue arm = convert(c, no, rep); move(c, r, reg(c, arm.reg), arm.mir_type);
            MIR_insn_t no_end = c->em.insert_after;
            c->em.insert_after = yes_end;
            arm = convert(c, yes, rep); move(c, r, reg(c, arm.reg), arm.mir_type);
            c->em.insert_after = no_end;
        }
        mark(c, done);
        if (discard) return null_value(c);
        Type* joined = rep == VALUE_REP_ITEM
            ? error_condition ? &TYPE_ANY : lambda_type_union_normalized(c->module->tp->pool,
                yes.semantic_contract, no.semantic_contract) : node->type;
        MirValue result = value(c, r, joined->type_id, rep, joined, node);
        Type* yes_success = lambda_type_accepts_error(yes.semantic_contract)
            ? yes.admitted_success_contract : yes.semantic_contract;
        Type* no_success = lambda_type_accepts_error(no.semantic_contract)
            ? no.admitted_success_contract : no.semantic_contract;
        if (rep == VALUE_REP_ITEM && yes_success && no_success)
            result.admitted_success_contract = lambda_type_union_normalized(c->module->tp->pool, yes_success, no_success);
        return result;
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)node;
        // ordered base-type cases consume one rooted scrutinee, including an
        // error value handled by an explicit error arm (S11.2.1).
        MirValue subject = convert(c, expression(c, match->scrutinee), VALUE_REP_ITEM);
        Type* contract = native_match_contract(c, match, subject);
        ValueRep rep = contract ? mvp_rep(contract) : VALUE_REP_ITEM;
        MIR_type_t lane = em_mir_type_for_rep(rep);
        MIR_reg_t result = em_new_reg(&c->em, "match_result", lane);
        move(c, result, lane == MIR_T_D ? MIR_new_double_op(c->em.ctx, 0.0) : imm(c, ITEM_NULL), lane);
        value(c, result, contract ? contract->type_id : LMD_TYPE_ANY, rep, contract ? contract : &TYPE_ANY);
        MIR_label_t done = label(c);
        for (AstMatchArm* arm = match->first_arm; arm; arm = (AstMatchArm*)arm->next) {
            MIR_label_t next = label(c);
            Type* pattern = arm->pattern ? mvp_match_type(arm->pattern) : NULL;
            if (arm->pattern) {
                if (pattern == &TYPE_ERROR) {
                    branch(c, MIR_BNE, next, reg(c, operation(c, MIR_URSH,
                        reg(c, subject.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
                } else if (subject.admitted_success_contract &&
                        type_field_unwrap_simple_decl(subject.admitted_success_contract) == pattern) {
                    if (pattern != &TYPE_ERROR)
                        branch(c, MIR_BEQ, next, reg(c, operation(c, MIR_URSH,
                            reg(c, subject.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
                } else {
                    MIR_reg_t matched = em_call_2(&c->em, "lambda_value_type_is_exact", MIR_T_I64,
                        MIR_T_I64, reg(c, subject.reg), MIR_T_P, imm(c, (intptr_t)pattern), true);
                    branch(c, MIR_BEQ, next, reg(c, matched), imm(c, 0));
                }
            }
            MirValue selected = contract && pattern && match_subject_arm(c, match, arm)
                ? value(c, subject.reg, pattern->type_id, VALUE_REP_ITEM, pattern, arm->body)
                : expression(c, arm->body, discard);
            if (!discard) {
                selected = convert(c, selected, rep);
                move(c, result, reg(c, selected.reg), selected.mir_type);
            }
            jump(c, done); mark(c, next);
        }
        mark(c, done);
        return discard ? null_value(c) : value(c, result,
            contract ? contract->type_id : LMD_TYPE_ANY, rep, contract ? contract : &TYPE_ANY, node);
    }
    case AST_NODE_HANDLER_EXPR: case AST_NODE_HANDLER_STAM: {
        AstHandlerNode* handler = (AstHandlerNode*)node;
        // ordinary recovery branches from one evaluated outcome; a clean
        // operand skips the handler and keeps its precise owner (S7.6.1v9).
        MirValue operand = convert(c, expression(c, handler->operand), VALUE_REP_ITEM);
        MIR_reg_t result = em_new_reg(&c->em, "handled", MIR_T_I64);
        value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
        MIR_label_t caught = label(c), done = label(c);
        move(c, result, reg(c, operand.reg));
        branch(c, MIR_BEQ, caught, reg(c, operation(c, MIR_URSH, reg(c, operand.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        if (handler->catches_falsy) branch(c, MIR_BEQ, caught, reg(c, truth(c, operand)), imm(c, 0));
        jump(c, done); mark(c, caught);
        MIR_reg_t enclosing_error = c->current_error;
        c->current_error = operand.reg;
        MirValue recovery = convert(c, expression(c, handler->body, discard), VALUE_REP_ITEM);
        c->current_error = enclosing_error;
        move(c, result, reg(c, recovery.reg)); mark(c, done);
        return value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    case AST_NODE_LOOP: {
        AstLoopControlNode* n = (AstLoopControlNode*)node;
        MIR_label_t fast_done = array_sum_loop(c, n);
        if (!fast_done) fast_done = array_copy_loop(c, n);
        if (!fast_done) fast_done = array_reverse_loop(c, n);
        if (!fast_done) fast_done = countdown_loop(c, n);
        if (!fast_done) fast_done = indexed_loop(c, n);
        if (!fast_done) fast_done = numeric_loop(c, n);
        if (!fast_done) fast_done = dense_call_loop(c, n);
        if (!fast_done) fast_done = strided_array_loop(c, n);
        if (!fast_done) fast_done = dense_array_loop(c, n);
        MvpBinding* saved_array = c->loop_array; MIR_reg_t saved_items = c->loop_array_items;
        c->loop_array = loop_store_array(c, n);
        c->loop_array_items = c->loop_array ? em_load_at(&c->em, c->loop_array->reg,
            LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "loop_items") : 0;
        MvpControl loop = {c->control, label(c), label(c)};
        MIR_label_t body = label(c);
        c->control = &loop;
        // test before the first iteration, then use a conditional backedge; continue still reaches the test.
        jump(c, loop.next); mark(c, body);
        array_iteration(c, n->body); mark(c, loop.next);
        MIR_reg_t completion = 0;
        if (mvp_value_contract(c->module, n->cond)->type_id == LMD_TYPE_ANY) {
            completion = constant(c, ITEM_NULL);
            // predicate errors are this loop's outcome, not a hidden return
            // from the containing callable (S3.5, S12.1.2v3).
            MvpErrorTarget failure = {loop.done, completion};
            MvpErrorTarget* saved_failure = c->condition_failure;
            c->condition_failure = &failure;
            boolean_branch(c, n->cond, body, true);
            c->condition_failure = saved_failure;
        } else boolean_branch(c, n->cond, body, true);
        mark(c, loop.done); c->control = loop.parent;
        c->loop_array = saved_array; c->loop_array_items = saved_items;
        if (fast_done) mark(c, fast_done);
        return completion ? value(c, completion, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node) : null_value(c);
    }
    case AST_NODE_FOR_EXPR: {
        AstForNode* n = (AstForNode*)node;
        AstLoopNode* source = (AstLoopNode*)n->loop;
        AstNode* collection = ast_unwrap_primary_to_leaf(source->as);
        bool range_source = collection && collection->node_type == AST_NODE_BINARY && ((AstBinaryNode*)collection)->op == OPERATOR_TO;
        TypeId kind = mvp_value_contract(c->module, source->as)->type_id;
        bool positions = source->key_only && source->key_filter == LOOP_KEY_INT &&
            (mvp_array_contract(mvp_value_contract(c->module, source->as)) || kind == LMD_TYPE_STRING);
        if (!n->discard_result || n->where || n->group || n->order || source->index_entry || (!range_source && !positions)) return collection_for(c, n);
        MIR_label_t fast_done = source->key_only ? NULL : dense_array_loop(c, n);
        AstBinaryNode* range = source->key_only ? NULL : (AstBinaryNode*)ast_unwrap_primary_to_leaf(source->as);
        MirValue first, last;
        if (source->key_only) {
            // capture the collection once; body writes cannot change this key sequence (S9.2.3).
            MirValue owner = expression(c, source->as);
            first = value(c, constant(c, 0), LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT);
            bool text = mvp_value_contract(c->module, source->as)->type_id == LMD_TYPE_STRING;
            owner = convert(c, owner, VALUE_REP_RAW_GC_POINTER);
            MIR_reg_t length = text ? em_call_1(&c->em, "fn_len_s", MIR_T_I64, MIR_T_P, reg(c, owner.reg), true)
                : em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "key_count");
            last = value(c, operation(c, MIR_SUB, reg(c, length), imm(c, 1)), LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT);
        } else { first = expression(c, range->left); last = expression(c, range->right); }
        bool finite = source->key_only || (mvp_int_range(c->module, range->left).finite && mvp_int_range(c->module, range->right).finite);
        MvpBinding* counter = mvp_binding(c->module, source->entry);
        MvpControl loop = {c->control, label(c), label(c)};
        MIR_label_t invalid = label(c), body = label(c);
        if (!finite) {
            em_branch_int53_outside(&c->em, first.reg, invalid);
            em_branch_int53_outside(&c->em, last.reg, invalid);
        }
        move(c, counter->reg, reg(c, first.reg));
        branch(c, MIR_BGT, loop.done, reg(c, first.reg), reg(c, last.reg));
        c->control = &loop; mark(c, body);
        MvpIntRange saved = counter->range;
        if (!counter->range.finite) counter->range = {true, INT53_MIN, INT53_MAX};
        array_iteration(c, n->then); counter->range = saved;
        mark(c, loop.next);
        // the hidden post-loop counter fits i64 even at INT53_MAX; it is never
        // exposed after leaving the range binding's scope.
        move(c, counter->reg, reg(c, operation(c, MIR_ADD, reg(c, counter->reg), imm(c, 1))));
        branch(c, MIR_BLE, body, reg(c, counter->reg), reg(c, last.reg));
        jump(c, loop.done); c->control = loop.parent;
        if (!finite) {
            mark(c, invalid);
            first = convert(c, first, VALUE_REP_ITEM); last = convert(c, last, VALUE_REP_ITEM);
            em_call_2(&c->em, "fn_range_bound_error", MIR_T_I64, MIR_T_I64, reg(c, first.reg),
                MIR_T_I64, reg(c, last.reg), true);
        }
        mark(c, loop.done);
        if (fast_done) mark(c, fast_done);
        return null_value(c);
    }
    case AST_NODE_RETURN_STAM: {
        AstNode* result = ((AstReturnNode*)node)->value;
        if (tail_call(c, result)) return null_value(c);
        AstNode* leaf = ast_unwrap_primary_to_leaf(result);
        Type* destination = lambda_type_nonnull_map_contract(c->function->result_contract);
        MvpBinding* owner = mvp_local_binding(c->module, result);
        AstNode* initial = owner && owner->entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR
            ? ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)owner->entry->node)->init) : NULL;
        // a fresh local observed only by this return transfers its owner;
        // retaining another alias keeps the ordinary snapshot capture.
        bool moving = initial && (initial->node_type == AST_NODE_MAP || initial->node_type == AST_NODE_ARRAY) &&
            mvp_binding_return_contract(c->module, owner->entry);
        MirValue returned = moving ? read_binding(c, owner, result, false) : destination && leaf && leaf->node_type == AST_NODE_MAP
            ? map_literal(c, (AstMapNode*)leaf, (TypeMap*)destination)
            : leaf && leaf->node_type == AST_NODE_ARRAY ? array_literal(c, (AstArrayNode*)leaf, c->function->result_contract)
            : expression(c, result);
        return_value(c, returned, result); return null_value(c);
    }
    case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM:
        if (!c->control) mvp_reject(c->module, node, "loop transfer outside a loop");
        else jump(c, node->node_type == AST_NODE_BREAK_STAM ? c->control->done : c->control->next);
        return null_value(c);
    default: mvp_reject(c->module, node, "unsupported scalar expression"); return null_value(c);
    }
}
static AstNode* owner_permutation(MvpCompiler* c, AstNode* first) {
    AstOwnerPermutationPlan plan = {};
    if (!ast_owner_permutation_plan(&c->module->tp->ast_index, first, &plan)) return NULL;
    MvpBinding* owners[8];
    for (int i = 0; i < plan.count; i++) {
        MvpBinding* b = mvp_binding(c->module, plan.owners[i]);
        if (!b || !b->reg || b->owner != c->function->id || b->module_slot || b->var_home ||
                !b->entry->is_mutable || b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR ||
                b->entry->view_base || !((AstDeclaratorNode*)b->entry->node)->declared_type ||
                !mvp_array_element(b->contract)) return NULL;
        owners[i] = b;
    }
    for (int i = 0; i < plan.count; i++) {
        if (!mvp_boundary_proven(owners[(i + 1) % plan.count]->contract, owners[i]->contract)) return NULL;
    }
    // the dead scratch creates no observer; rotate precise owner roots without capturing snapshots.
    MIR_reg_t saved = em_new_reg(&c->em, "owner_cycle", MIR_T_I64);
    move(c, saved, reg(c, owners[0]->reg));
    for (int i = 0; i < plan.count; i++)
        move(c, owners[i]->reg, reg(c, i + 1 < plan.count ? owners[i + 1]->reg : saved));
    for (int i = 0; i < plan.count; i++) refresh_array(c, owners[i]);
    return plan.last;
}

static AstNode* append_string_recipe(MvpCompiler* c, AstNode* first) {
    if (first->node_type != AST_NODE_VAR_STAM && first->node_type != AST_NODE_LET_STAM) return NULL;
    AstNode* declaration = ((AstVarDeclNode*)first)->declarations;
    AstNode* next = first->next;
    if (!declaration || declaration->next || declaration->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            !next || next->node_type != AST_NODE_ASSIGN_STAM) return NULL;
    AstDeclaratorNode* d = (AstDeclaratorNode*)declaration;
    AstAssignStamNode* assignment = (AstAssignStamNode*)next;
    MvpBinding* temporary = mvp_binding(c->module, d->entry);
    MvpBinding* owner = mvp_binding(c->module, assignment->target_entry);
    if (!temporary || !owner || temporary == owner || temporary->contract != &TYPE_STRING ||
            owner->contract != &TYPE_STRING || !string_recipe_size(c->module, d->init, NULL)) return NULL;
    AstNode* leaves[LAMBDA_STRING_CONCAT_MAX_PARTS]; int size = 0;
    if (!ast_collect_join_leaves(assignment->value, leaves, &size) || size != 2 ||
            mvp_local_binding(c->module, leaves[0]) != owner || mvp_local_binding(c->module, leaves[1]) != temporary)
        return NULL;
    AstIndex* index = &c->module->tp->ast_index;
    uint32_t count = 0; unsigned reads = 0;
    const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, d->entry), &count);
    AstNode* leaf = ast_unwrap_primary_to_leaf(leaves[1]);
    for (uint32_t i = 0; i < count; i++) {
        AstNode* use = index->nodes[uses[i]];
        if (use == declaration || use == d->id) continue;
        if (use != leaf) return NULL;
        reads++;
    }
    if (reads != 1) return NULL;
    // the adjacent, single-use constructor contributes its pieces directly;
    // prefix reading is effect-free and constructor arguments retain their order.
    MirValue joined = string_concat(c, assignment->value, owner, temporary, d->init);
    if (!joined.reg) return NULL;
    store_binding(c, owner, joined);
    return next;
}

static AstNode* exclusive_conditions(MvpCompiler* c, AstNode* first, AstNode* consumed) {
    AstIfNode* cases[16]; int64_t keys[16]; unsigned count = 0;
    MvpBinding* selector = NULL;
    AstIndex* index = &c->module->tp->ast_index;
    for (AstNode* n = first; n && n != consumed && count < 16; n = n->next) {
        if (n->node_type != AST_NODE_IF_EXPR || ((AstIfNode*)n)->otherwise) break;
        AstNode* condition = ast_unwrap_primary_to_leaf(((AstIfNode*)n)->cond);
        if (!condition || condition->node_type != AST_NODE_BINARY) break;
        AstBinaryNode* equal = (AstBinaryNode*)condition;
        MvpBinding* b = mvp_local_binding(c->module, equal->left);
        int64_t key;
        if (equal->op != OPERATOR_EQ || !b || b->owner != c->function->id || !b->reg || b->rep != VALUE_REP_INT_LANE ||
                !mvp_boundary_proven(b->contract, &TYPE_INT) || (selector && selector != b) ||
                !mir_decimal_int_literal(c->module->tp->source, equal->right, &key) || key < INT53_MIN || key > INT53_MAX) break;
        bool duplicate = false;
        for (unsigned i = 0; i < count; i++) duplicate |= keys[i] == key;
        if (duplicate) break;
        AstNodeId root = ast_index_find(index, n);
        uint32_t uses_count = 0;
        const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, b->entry), &uses_count);
        for (uint32_t i = 0; i < uses_count; i++)
            if (index->nodes[uses[i]]->node_type == AST_NODE_ASSIGN_STAM && ast_index_node_descends(index, uses[i], root)) return NULL;
        selector = b; cases[count] = (AstIfNode*)n; keys[count++] = key;
    }
    if (count < 3) return NULL;
    MIR_label_t targets[16], done = label(c);
    // distinct finite equality tests on an unchanged local cannot match twice, including poison inputs.
    for (unsigned i = 0; i < count; i++) {
        targets[i] = label(c);
        branch(c, MIR_BEQ, targets[i], reg(c, selector->reg), imm(c, keys[i]));
    }
    jump(c, done);
    for (unsigned i = 0; i < count; i++) {
        mark(c, targets[i]); expression(c, cases[i]->then, true); jump(c, done);
    }
    mark(c, done);
    return cases[count - 1];
}

struct MvpConditionSet {
    AstBinaryNode* atoms[16];
    unsigned count;
};

static bool condition_terms(MvpCompiler* c, AstNode* source, MvpConditionSet* set,
        uint32_t* mask, uint32_t* bits, int depth = 0) {
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n || depth > 16 || n->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* test = (AstBinaryNode*)n;
    if (test->op == OPERATOR_AND)
        return condition_terms(c, test->left, set, mask, bits, depth + 1) &&
            condition_terms(c, test->right, set, mask, bits, depth + 1);
    if ((test->op != OPERATOR_EQ && test->op != OPERATOR_NE) || !mvp_pure_key(test->left) || !mvp_pure_key(test->right) ||
            mvp_value_contract(c->module, test->left)->type_id != LMD_TYPE_INT ||
            mvp_value_contract(c->module, test->right)->type_id != LMD_TYPE_INT) return false;
    unsigned a = 0;
    for (; a < set->count; a++) {
        AstBinaryNode* known = set->atoms[a];
        if ((mvp_same_integer_expression(c->module, test->left, known->left) &&
             mvp_same_integer_expression(c->module, test->right, known->right)) ||
            (mvp_same_integer_expression(c->module, test->right, known->left) &&
             mvp_same_integer_expression(c->module, test->left, known->right))) break;
    }
    if (a == set->count) {
        if (a == 16) return false;
        set->atoms[set->count++] = test;
    }
    uint32_t key = UINT32_C(1) << a, expected = test->op == OPERATOR_EQ ? key : 0;
    if ((*mask & key) && (*bits & key) != expected) return false;
    *mask |= key; *bits |= expected;
    return true;
}

static AstNode* disjoint_conditions(MvpCompiler* c, AstNode* first, AstNode* consumed) {
    MvpConditionSet set = {};
    AstIfNode* cases[16]; uint32_t masks[16] = {}, bits[16] = {}; unsigned count = 0;
    for (AstNode* n = first; n && n != consumed && count < 16; n = n->next) {
        if (n->node_type != AST_NODE_IF_EXPR || ((AstIfNode*)n)->otherwise ||
                !condition_terms(c, ((AstIfNode*)n)->cond, &set, &masks[count], &bits[count])) break;
        bool disjoint = true;
        for (unsigned i = 0; i < count; i++)
            disjoint &= ((bits[i] ^ bits[count]) & masks[i] & masks[count]) != 0;
        if (!disjoint) break;
        cases[count++] = (AstIfNode*)n;
    }
    if (count < 3) return NULL;
    AstIndex* index = &c->module->tp->ast_index;
    AstNodeId begin = ast_index_find(index, first), end = ast_index_subtree_end(index, ast_index_find(index, cases[count - 1]));
    for (unsigned a = 0; a < count; a++) {
        AstNodeId cond = ast_index_find(index, cases[a]->cond);
        for (AstNodeId i = cond; i < ast_index_subtree_end(index, cond); i++) {
            MvpBinding* b = mvp_local_binding(c->module, index->nodes[i]);
            if (!b) continue;
            uint32_t uses_count = 0;
            const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, b->entry), &uses_count);
            for (uint32_t u = 0; u < uses_count; u++)
                if (uses[u] >= begin && uses[u] < end && index->nodes[uses[u]]->node_type == AST_NODE_ASSIGN_STAM) return NULL;
        }
    }
    // opposite equality literals make the conjunctions exclusive; unchanged
    // scalar operands let a taken arm skip the remaining pure predicates.
    MIR_label_t done = label(c);
    for (unsigned a = 0; a < count; a++) {
        MIR_label_t next = label(c);
        boolean_branch(c, cases[a]->cond, next, false);
        expression(c, cases[a]->then, true); jump(c, done); mark(c, next);
    }
    mark(c, done);
    return cases[count - 1];
}

static AstNode* discharged_declaration(MvpCompiler* c, AstNode* first) {
    if (!c->function->ast || !first || first->node_type != AST_NODE_LET_STAM || !first->next ||
            (first->next->node_type != AST_NODE_LET_STAM && first->next->node_type != AST_NODE_VAR_STAM)) return NULL;
    AstDeclaratorNode* source = (AstDeclaratorNode*)((AstVarDeclNode*)first)->declarations;
    AstDeclaratorNode* destination = (AstDeclaratorNode*)((AstVarDeclNode*)first->next)->declarations;
    if (!source || source->next || !destination || destination->next) return NULL;
    MvpBinding* from = mvp_binding(c->module, source->entry);
    MvpBinding* to = mvp_binding(c->module, destination->entry);
    AstNode* init = ast_unwrap_primary_to_leaf(destination->init);
    if (!from || !to || !from->entry->admission_may_fail || from->owner != c->function->id ||
            !init || init->node_type != AST_NODE_UNARY || ((AstUnaryNode*)init)->op != OPERATOR_PROPAGATE ||
            mvp_local_binding(c->module, ((AstUnaryNode*)init)->operand) != from) return NULL;
    AstIndex* index = &c->module->tp->ast_index;
    uint32_t count = 0, reads = 0;
    const AstNodeId* uses = ast_index_binding_uses(index,
        ast_index_find_binding_entry(index, from->entry), &count);
    for (uint32_t i = 0; i < count; i++) {
        AstNode* use = index->nodes[uses[i]];
        if (use == source || use == source->id) continue;
        if (use != ast_unwrap_primary_to_leaf(((AstUnaryNode*)init)->operand)) return NULL;
        reads++;
    }
    if (reads != 1) return NULL;
    // the sole observation immediately discharges the validation outcome.
    // No effect can observe a boxed intermediate between these declarations
    // (S7.6.3v3, S7.7.2v2); failure still validates the enclosing return.
    MvpErrorTarget failure = {};
    MirValue admitted = admit(c, binding_value(c, from, source->init), from->admission_contract,
        &failure, from->entry->name->chars);
    admitted = propagated_value(c, admitted, init);
    to->scope = c->scope;
    store_binding(c, to, admitted);
    if (failure.done) {
        MIR_label_t done = label(c); jump(c, done); mark(c, failure.done);
        return_value(c, value(c, failure.error, LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR), init);
        mark(c, done);
    }
    return first->next;
}

static MirValue statements(MvpCompiler* c, AstNode* node, bool discard) {
    MvpScope scope = {c->scope, {}}; c->scope = &scope;
    MirValue result = null_value(c);
    AstNode* last = NULL;
    bool procedural = c->function->ast && c->function->ast->node_type == AST_NODE_PROC;
    if (!discard) for (AstNode* n = node; n; n = n->next)
        // fn-local statements must not replace an earlier collected value.
        if (!is_declaration_node(n) && (procedural || !ast_block_item_is_statement(n, false))) last = n;
    for (; node && !c->module->failed; node = node->next) {
        if (AstNode* discharged = discharged_declaration(c, node)) { node = discharged; continue; }
        if (AstNode* appended = append_string_recipe(c, node)) { node = appended; continue; }
        if (AstNode* moved = owner_permutation(c, node)) { node = moved; continue; }
        if (AstNode* dispatched = exclusive_conditions(c, node, last)) { node = dispatched; continue; }
        if (AstNode* dispatched = disjoint_conditions(c, node, last)) { node = dispatched; continue; }
        MirValue current = expression(c, node, node != last);
        if (node == last) result = current;
    }
    if (scope.failure.done && discard) mark(c, scope.failure.done);
    else if (scope.failure.done) {
        result = convert(c, result, VALUE_REP_ITEM);
        MIR_reg_t output = em_new_reg(&c->em, "block_result", MIR_T_I64);
        MIR_label_t done = label(c);
        move(c, output, reg(c, result.reg)); jump(c, done);
        mark(c, scope.failure.done); move(c, output, reg(c, scope.failure.error)); mark(c, done);
        result = value(c, output, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    }
    c->scope = scope.parent;
    return result;
}

static void epilogue(MvpCompiler* c) {
    MirFrameState* f = &c->em.frame;
    MIR_reg_t result = f->return_reg, companion = f->error_return_reg;
    if (f->item_return) em_build_pending_pair(&c->em, result, &result, &companion);
    if (em_returns_companion_slot(f->plan.companion))
        em_store_frame_top(&c->em, f->runtime, offsetof(Context, mir_companion_slot), companion);
    em_store_frame_top(&c->em, f->runtime, offsetof(Context, side_root_top), f->root_base);
    if (f->scalar_home_count || f->fixed_number_slots || f->number_frame_required || f->number_extent_dirty)
        em_store_frame_top(&c->em, f->runtime, offsetof(Context, side_number_top), f->number_base);
    em_emit_insn(&c->em, em_returns_result_pair(f->plan.companion)
        ? MIR_new_ret_insn(c->em.ctx, 2, reg(c, result), reg(c, companion))
        : MIR_new_ret_insn(c->em.ctx, 1, reg(c, result)));
}

// Version only closed numeric bodies: no captures, array replacement or
// collecting/reentrant calls can invalidate the entry guards (D4.4.4v4).
static bool fused_character_index(MvpCompiler* c, AstNode* node) {
    if (!mvp_string_index(c->module, node)) return false;
    AstIndex* index = &c->module->tp->ast_index;
    AstNodeId parent = ast_index_parent_id(index, ast_index_find(index, node));
    while (parent < index->count && index->nodes[parent]->node_type == AST_NODE_PRIMARY)
        parent = ast_index_parent_id(index, parent);
    if (parent >= index->count || index->nodes[parent]->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* comparison = (AstBinaryNode*)index->nodes[parent];
    // fused character comparison uses the audited noncollecting helper on Unicode/absence misses.
    return (comparison->op == OPERATOR_EQ || comparison->op == OPERATOR_NE) &&
        mvp_string_index(c->module, comparison->left) && mvp_string_index(c->module, comparison->right);
}

static bool dense_region_eligible(MvpCompiler* c, AstNodeId root,
        MvpIndexLoopPlan* indices, bool guarded_indices, bool allocating) {
    LambdaMvpModule* m = c->module;
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId i = root; i < ast_index_subtree_end(index, root); i++) {
        if (index->owner_functions[i] != c->function->id) continue;
        AstNode* n = index->nodes[i];
        AstNode* object = NULL; AstNode* key = NULL; AstNode* input = NULL;
        Type* destination = NULL;
        if (n->node_type == AST_NODE_INDEX_EXPR) {
            if (fused_character_index(c, n)) continue;
            object = ((AstFieldNode*)n)->object; key = ((AstFieldNode*)n)->field;
        } else if (n->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
            AstAssignStamNode* a = (AstAssignStamNode*)n;
            object = a->object; key = a->key; input = a->value;
        } else if (n->node_type == AST_NODE_ASSIGN_STAM || n->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            NameEntry* entry;
            if (n->node_type == AST_NODE_ASSIGN_STAM) {
                entry = ((AstAssignStamNode*)n)->target_entry; input = ((AstAssignStamNode*)n)->value;
            } else { entry = ((AstDeclaratorNode*)n)->entry; input = ((AstDeclaratorNode*)n)->init; }
            MvpBinding* b = mvp_binding(m, entry);
            if (!b || mvp_array_element(b->contract)) return false;
            destination = b->contract;
        } else if (n->node_type == AST_NODE_RETURN_STAM) {
            input = ((AstReturnNode*)n)->value; destination = c->function->result_contract;
        } else if (n->node_type == AST_NODE_CALL_EXPR) {
            AstCallNode* call = (AstCallNode*)n;
            MvpFunction* f = mvp_function(m, ast_direct_call_function(call));
            if (f) {
                if (f != c->dense_callee && (!inline_eligible(c, f) || f->abi.effects.may_gc) &&
                        (!c->dense_calls || !dense_call_eligible(c, call))) return false;
            } else {
                SysFuncInfo* info = ((AstSysFuncNode*)ast_unwrap_primary_to_leaf(call->function))->fn_info;
                TypeId arg = mvp_value_contract(m, call->argument, 0, true)->type_id;
                if (!mvp_native_math(info, arg) &&
                    !(arg == LMD_TYPE_INT && sysfunc_native_math_type_preserving(info->fn, NULL)) &&
                    !((info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_INT) && (arg == LMD_TYPE_FLOAT || arg == LMD_TYPE_INT))) return false;
            }
        } else if (n->node_type == AST_NODE_IDENT) {
            MvpBinding* b = mvp_local_binding(m, n);
            if (b && mvp_array_element(b->contract)) {
                AstNodeId parent = ast_index_parent_id(index, i);
                while (parent < index->count && index->nodes[parent]->node_type == AST_NODE_PRIMARY)
                    parent = ast_index_parent_id(index, parent);
                AstNode* p = parent < index->count ? index->nodes[parent] : NULL;
                bool kernel_argument = p && p->node_type == AST_NODE_CALL_EXPR &&
                    ((c->dense_callee && ast_direct_call_function((AstCallNode*)p) == c->dense_callee->ast) ||
                        (c->dense_calls && dense_call_eligible(c, (AstCallNode*)p)));
                if (!p || (!kernel_argument && p->node_type != AST_NODE_INDEX_EXPR && p->node_type != AST_NODE_INDEX_ASSIGN_STAM)) return false;
            }
        } else if (n->node_type == AST_NODE_FOR_EXPR) {
            AstLoopNode* loop = (AstLoopNode*)((AstForNode*)n)->loop;
            if (loop->key_only) return false;
            AstBinaryNode* range = (AstBinaryNode*)ast_unwrap_primary_to_leaf(loop->as);
            // finite endpoints cannot enter the collecting range-error path.
            if (!mvp_int_range(m, range->left).finite || !mvp_int_range(m, range->right).finite) return false;
        } else if (n->node_type == AST_NODE_ARRAY) {
            // a typed primitive literal may allocate while all input owners
            // stay rooted; its element proofs still precede publication.
            if (!allocating || !mvp_array_element(mvp_value_contract(m, n, 0, true))) return false;
        }
        if (allocating && (n->node_type == AST_NODE_MEMBER_ASSIGN_STAM || n->node_type == AST_NODE_MAP ||
                n->node_type == AST_NODE_MATCH_EXPR || n->node_type == AST_NODE_FOR_EXPR ||
                n->node_type == AST_NODE_LOOP)) return false;
        if (object) {
            MvpBinding* b = mvp_local_binding(m, object);
            MvpBinding* k = mvp_local_binding(m, key);
            bool bounded = indices->array == b && k && (k == indices->counter || k == indices->follower || k == indices->limit);
            if (!b || b->owner != c->function->id || !mvp_array_element(b->contract) ||
                    (!mvp_pure_key(key) && !(guarded_indices && mvp_index_key(m, key))) ||
                    (!guarded_indices && !bounded && !array_index_in_extent(c, b, key))) return false;
            if (input) {
                if (allocating) return false;
                b->array_written = true; destination = mvp_array_element(b->contract);
            }
        }
        if (destination && !mvp_boundary_proven(mvp_value_contract(m, input, 0, true), destination)) return false;
        if (((n->node_type == AST_NODE_BINARY && ((AstBinaryNode*)n)->op != OPERATOR_TO) || n->node_type == AST_NODE_UNARY) &&
                mvp_value_contract(m, n, 0, true)->type_id == LMD_TYPE_ANY) return false;
    }
    return true;
}

static void dense_call_effects(MvpDenseCalls* calls, bool guarded) {
    for (unsigned i = 0; i < calls->count; i++)
        calls->functions[i]->abi.effects.may_return_error = guarded ? false : calls->errors[i];
}

static bool dense_add_call(MvpDenseCalls* calls, MvpFunction* f) {
    for (unsigned i = 0; i < calls->count; i++) if (calls->functions[i] == f) return true;
    if (calls->count == 16) return false;
    calls->functions[calls->count] = f;
    calls->errors[calls->count++] = f->abi.effects.may_return_error;
    f->abi.effects.may_return_error = false;
    return true;
}

static bool dense_body_eligible(MvpCompiler* c, MvpIndexLoopPlan* indices, MvpDenseCalls* calls, bool allocating) {
    if (mvp_parameter_receiver(c->function->ast)) return false;
    LambdaMvpModule* m = c->module;
    AstIndex* index = &m->tp->ast_index;
    MvpIndexLoopPlan dynamic = {};
    if (mvp_index_function_plan(m, c->function, &dynamic)) *indices = dynamic;
    bool has_array = false;
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (b->owner != c->function->id) continue;
        if (!b->entry || !b->entry->node || (b->entry->node->node_type != AST_NODE_PARAM &&
                b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR)) continue;
        if (mvp_array_element(b->contract)) {
            if (b->entry->node->node_type != AST_NODE_PARAM || (!b->array_extent && b != indices->array)) return false;
            has_array = true; b->array_written = false;
        } else if (b->rep != VALUE_REP_F64 && b->rep != VALUE_REP_INT_LANE && b->rep != VALUE_REP_I64) return false;
    }
    if (!has_array) return false;
    bool saved = c->dense_calls; c->dense_calls = calls != NULL || allocating;
    AstNodeId root = ast_index_find(index, c->function->ast->body);
    bool eligible = true;
    if (calls) {
        // assume a clean self edge while proving the complete guarded body.
        // Leaf results become infallible only after their own bodies pass.
        dense_add_call(calls, c->function);
        for (AstNodeId i = root; eligible && i < ast_index_subtree_end(index, root); i++) {
            if (index->nodes[i]->node_type != AST_NODE_CALL_EXPR) continue;
            AstCallNode* call = (AstCallNode*)index->nodes[i];
            MvpFunction* target = mvp_function(m, ast_direct_call_function(call));
            // dynamic first/limit guards must be re-proved on a self edge;
            // only fixed access extents are inherited by recursive bodies.
            if (function_has_arrays(target))
                eligible = !(target == c->function && indices->array) &&
                    dense_call_eligible(c, call) && dense_add_call(calls, target);
        }
    }
    eligible = eligible && dense_region_eligible(c, root, indices, false, allocating);
    if (calls) dense_call_effects(calls, false);
    c->dense_calls = saved;
    return eligible;
}

static bool dense_call_eligible(MvpCompiler* c, AstCallNode* call) {
    return dense_call_arguments(c, mvp_function(c->module, ast_direct_call_function(call)), call->argument);
}

static bool dense_call_arguments(MvpCompiler* c, MvpFunction* f, AstNode* arguments) {
    LambdaMvpModule* m = c->module;
    if (!f || mvp_parameter_receiver(f->ast)) return false;
    bool recursive = f == c->function;
    if (recursive && f->abi.effects.may_reenter) return false;
    if (!recursive) {
        // the checked ABI may conservatively classify boxed math as reentrant;
        // only the complete closed-body proof below can replace that effect.
        if (!inline_eligible(c, f, true, true)) return false;
        MvpFunction* caller = c->function; c->function = f;
        MvpIndexLoopPlan indices = {};
        bool eligible = dense_body_eligible(c, &indices) && !indices.array;
        c->function = caller;
        if (!eligible) return false;
    }
    AstNamedNode* param = f->ast->param;
    for (AstNode* a = arguments; a; a = a->next, param = (AstNamedNode*)param->next) {
        if (!mvp_pure_key(a) || !mvp_boundary_proven(mvp_value_contract(m, a, 0, true), mvp_contract(param))) return false;
        MvpBinding* formal = mvp_binding(m, param->entry);
        if (!mvp_array_element(formal->contract)) continue;
        MvpBinding* owner = mvp_local_binding(m, a);
        TypeParam* type = lambda_type_param(param->type);
        if (!owner || owner->owner != c->function->id || !mvp_array_element(owner->contract) ||
                owner->array_extent < formal->array_extent || (recursive && owner != formal)) return false;
        if (type->is_var_param) {
            if (!mvp_parameter_storage_stable(m, param)) return false;
            owner->array_written = true;
        } else {
            if (!mvp_parameter_borrowable(m, param)) return false;
            AstNamedNode* other = f->ast->param;
            for (AstNode* arg = arguments; arg; arg = arg->next, other = (AstNamedNode*)other->next)
                if (lambda_type_param(other->type)->is_var_param && mvp_local_binding(m, arg) == owner) return false;
        }
    }
    return true;
}

static void emit_closed_region(MvpCompiler* c, AstNode* node, bool arrays) {
    uint32_t gc_calls = c->em.frame.may_gc_call_count, inline_cost = c->inline_cost;
    bool dense_arrays = c->dense_arrays;
    c->dense_arrays = arrays;
    if (node) expression(c, node, true);
    else function_body(c);
    c->dense_arrays = dense_arrays;
    // alternative bodies have independent expansion budgets; emitting the hot
    // version must not deprive its checked sibling of ordinary leaf inlining.
    c->inline_cost = inline_cost;
    if (c->em.frame.may_gc_call_count != gc_calls)
        mvp_reject(c->module, node ? node : c->function->ast, "closed numeric region emitted a collecting call");
}

static MIR_reg_t dense_owner_guard(MvpCompiler* c, MvpBinding* owner,
        int64_t extent, bool written, MIR_label_t checked) {
    ArrayNumElemType lane;
    lambda_array_num_elem_type_for_contract(mvp_array_element(owner->contract), &lane);
    em_array_num_elem_guard(&c->em, owner->reg, lane, checked);
    MIR_reg_t length = em_load_at(&c->em, owner->reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "dense_length");
    if (extent) branch(c, MIR_BLT, checked, reg(c, length), imm(c, extent));
    if (written) array_writable_guard(c, owner, checked);
    return length;
}

static MIR_label_t dense_array_loop(MvpCompiler* c, AstNode* loop) {
    if (c->dense_arrays || c->dense_callee) return NULL;
    LambdaMvpModule* m = c->module;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, loop), end = ast_index_subtree_end(index, root);
    if (end - root > 512) return NULL;
    MvpBinding* arrays[16]; unsigned count = 0, accesses = 0;
    for (AstNodeId i = root; i < end; i++) {
        AstNode* n = index->nodes[i];
        AstNode* object = n->node_type == AST_NODE_INDEX_EXPR ? ((AstFieldNode*)n)->object :
            n->node_type == AST_NODE_INDEX_ASSIGN_STAM ? ((AstAssignStamNode*)n)->object : NULL;
        if (!object) continue;
        MvpBinding* b = mvp_local_binding(m, object);
        if (!b || !b->array_native || !b->array_extent || !mvp_array_element(b->contract) ||
                ast_index_node_descends(index, ast_index_find(index, b->entry->node), root)) return NULL;
        accesses++;
        unsigned a = 0;
        while (a < count && arrays[a] != b) a++;
        if (a == count) {
            if (count == 16) return NULL;
            arrays[count++] = b;
        }
    }
    if (accesses < 2) return NULL;
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) { mvp_reject(m, loop, "array loop binding allocation failed"); return NULL; }
    for (unsigned a = 0; a < count; a++) arrays[a]->array_written = false;
    narrow_dense_locals(m, root);
    MvpIndexLoopPlan unused = {};
    MIR_label_t checked = NULL, done = NULL;
    if (dense_region_eligible(c, root, &unused, false)) {
        checked = label(c); done = label(c);
        for (unsigned a = 0; a < count; a++) {
            MvpBinding* b = arrays[a];
            dense_owner_guard(c, b, b->array_extent, b->array_written, checked);
            b->dense_items = em_load_at(&c->em, b->reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "loop_dense_items");
        }
        // the closed region preserves every owner and extent across all backedges;
        // guard misses enter the original loop before any source effect has run.
        emit_closed_region(c, loop, true);
        jump(c, done);
    }
    restore_bindings(m, saved); mem_free(saved);
    if (checked) mark(c, checked);
    return done;
}

struct MvpKernelLoopPlan {
    MvpFunction* callee;
    MvpBinding* arrays[LAMBDA_MAX_FUNCTION_ARGS];
    int64_t extents[LAMBDA_MAX_FUNCTION_ARGS];
    bool written[LAMBDA_MAX_FUNCTION_ARGS];
    unsigned count;
};

static bool dense_call_loop_plan(MvpCompiler* c, AstLoopControlNode* loop, MvpKernelLoopPlan* plan) {
    LambdaMvpModule* m = c->module;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, loop);
    AstCallNode* call = NULL;
    for (AstNodeId id = root + 1; id < ast_index_subtree_end(index, root); id++) {
        AstNode* n = index->nodes[id];
        if (n->node_type == AST_NODE_LOOP || n->node_type == AST_NODE_FOR_EXPR ||
                n->node_type == AST_NODE_INDEX_EXPR || n->node_type == AST_NODE_INDEX_ASSIGN_STAM) return false;
        if (n->node_type == AST_NODE_CALL_EXPR && ast_direct_call_function((AstCallNode*)n)) {
            if (call) return false;
            call = (AstCallNode*)n;
        }
    }
    MvpFunction* callee = call ? mvp_function(m, ast_direct_call_function(call)) : NULL;
    // the complete closed-body proof below excludes reentry on the guarded
    // path even when the original generic conversions may reenter.
    if (!callee || callee == c->function || callee->inline_cost > 512) return false;
    AstNamedNode* param = callee->ast->param;
    for (AstNode* arg = call->argument; arg; arg = arg->next, param = (AstNamedNode*)param->next) {
        if (!mvp_pure_key(arg) || !mvp_boundary_proven(mvp_value_contract(m, arg), mvp_contract(param))) return false;
        if (!mvp_array_element(mvp_contract(param))) continue;
        MvpBinding* owner = mvp_local_binding(m, arg);
        TypeParam* type = lambda_type_param(param->type);
        if (!owner || owner->owner != c->function->id || !mvp_array_element(owner->contract) ||
                ast_index_node_descends(index, ast_index_find(index, owner->entry->node), root)) return false;
        if (!type->is_var_param) {
            if (!mvp_parameter_borrowable(m, param)) return false;
            AstNamedNode* other = callee->ast->param;
            for (AstNode* a = call->argument; a; a = a->next, other = (AstNamedNode*)other->next)
                if (lambda_type_param(other->type)->is_var_param && mvp_local_binding(m, a) == owner) return false;
        }
        plan->arrays[plan->count++] = owner;
    }
    if (!plan->count) return false;
    MvpFunction* caller = c->function;
    c->function = callee;
    narrow_dense_locals(m, ast_index_find(index, callee->ast->body), true);
    MvpIndexLoopPlan indices = {};
    bool eligible = dense_body_eligible(c, &indices) && !indices.array;
    c->function = caller;
    if (!eligible) return false;
    unsigned a = 0;
    for (param = callee->ast->param; param; param = (AstNamedNode*)param->next) {
        MvpBinding* b = mvp_binding(m, param->entry);
        if (!mvp_array_element(b->contract)) continue;
        plan->extents[a] = b->array_extent; plan->written[a++] = b->array_written;
    }
    plan->callee = callee;
    return true;
}

static MIR_label_t dense_call_loop(MvpCompiler* c, AstLoopControlNode* loop) {
    if (c->dense_arrays || c->dense_callee || c->inlining) return NULL;
    LambdaMvpModule* m = c->module;
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) { mvp_reject(m, loop, "kernel loop binding allocation failed"); return NULL; }
    MvpKernelLoopPlan plan = {};
    bool eligible = dense_call_loop_plan(c, loop, &plan);
    restore_bindings(m, saved);
    MIR_label_t checked = NULL, done = NULL;
    if (eligible) {
        auto effects = plan.callee->abi.effects;
        // the guarded expansion has no checked stores or owner replacements; its original ABI stays intact.
        plan.callee->abi.effects.may_gc = false;
        plan.callee->abi.effects.may_return_error = false;
        c->dense_callee = plan.callee;
        MvpIndexLoopPlan unused = {};
        eligible = dense_region_eligible(c, ast_index_find(&m->tp->ast_index, loop), &unused, false);
        if (eligible) {
            checked = label(c); done = label(c);
            for (unsigned a = 0; a < plan.count; a++) {
                MvpBinding* b = plan.arrays[a];
                dense_owner_guard(c, b, plan.extents[a], plan.written[a], checked);
            }
            // no capture, allocation or owner assignment can invalidate these witnesses between calls.
            emit_closed_region(c, loop, false);
            jump(c, done);
        }
        c->dense_callee = NULL;
        plan.callee->abi.effects = effects;
    }
    restore_bindings(m, saved); mem_free(saved);
    if (checked) mark(c, checked);
    return done;
}

struct MvpIterationPlan {
    AstNodeId root;
    size_t last_access;
    MvpBinding* arrays[16];
    AstNode* keys[64];
    MvpBinding* key_arrays[64];
    MvpBinding* hoisted[16];
    unsigned array_count, key_count, hoisted_count, access_count;
};

static bool iteration_key(MvpCompiler* c, AstNode* source, MvpIterationPlan* plan, int depth = 0) {
    if (depth > 16) return false;
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    bool optional = false;
    Type* base = n ? lambda_type_nullable_lane_base(mvp_value_contract(c->module, n), &optional) : NULL;
    if (!n || !mvp_index_key(c->module, n) || !base || base->type_id != LMD_TYPE_INT) return false;
    if (n->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* read = (AstFieldNode*)n;
        MvpBinding* owner = mvp_local_binding(c->module, read->object);
        AstIndex* index = &c->module->tp->ast_index;
        return !ast_index_node_descends(index, ast_index_find(index, owner->entry->node), plan->root) &&
            iteration_key(c, read->field, plan, depth + 1);
    }
    if (n->node_type == AST_NODE_BINARY) {
        AstBinaryNode* b = (AstBinaryNode*)n;
        return iteration_key(c, b->left, plan, depth + 1) && iteration_key(c, b->right, plan, depth + 1);
    }
    if (n->node_type == AST_NODE_UNARY) return iteration_key(c, ((AstUnaryNode*)n)->operand, plan, depth + 1);
    if (n->node_type != AST_NODE_IDENT) return true;
    MvpBinding* b = mvp_local_binding(c->module, n);
    if (!b || b->rep != VALUE_REP_INT_LANE) return false;
    AstIndex* index = &c->module->tp->ast_index;
    uint32_t count = 0;
    const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, b->entry), &count);
    for (uint32_t i = 0; i < count; i++) {
        AstNode* use = index->nodes[uses[i]];
        if (use->node_type == AST_NODE_ASSIGN_STAM && ast_index_node_descends(index, uses[i], plan->root) &&
                use->source_span.start_byte < plan->last_access) return false;
    }
    AstNodeId declaration = ast_index_find(index, b->entry->node);
    if (!ast_index_node_descends(index, declaration, plan->root)) return true;
    for (unsigned i = 0; i < plan->hoisted_count; i++) if (plan->hoisted[i] == b) return true;
    if (b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR || plan->hoisted_count == 16) return false;
    AstNodeId parent = ast_index_parent_id(index, declaration);
    if (parent < index->count && (index->nodes[parent]->node_type == AST_NODE_VAR_STAM ||
            index->nodes[parent]->node_type == AST_NODE_LET_STAM)) parent = ast_index_parent_id(index, parent);
    // only unconditional pure declarations can be evaluated ahead of the guards.
    if (parent != plan->root || !iteration_key(c, ((AstDeclaratorNode*)b->entry->node)->init, plan, depth + 1)) return false;
    plan->hoisted[plan->hoisted_count++] = b;
    return true;
}

static void narrow_dense_locals(LambdaMvpModule* m, AstNodeId root, bool outcomes) {
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId i = root; i < ast_index_subtree_end(index, root); i++) {
        AstNode* n = index->nodes[i];
        if (n->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        MvpBinding* b = mvp_binding(m, ((AstDeclaratorNode*)n)->entry);
        if (outcomes && b && b->entry->admission_may_fail &&
                (b->admission_contract == &TYPE_INT || b->admission_contract == &TYPE_BOOL ||
                 b->admission_contract == &TYPE_FLOAT)) {
            // the closed-region proof checks every initializer and write
            // against this hypothesis before emission; guarded regions
            // contain no collecting calls.
            b->contract = b->admission_contract; b->rep = mvp_rep(b->contract);
        }
        if (((AstDeclaratorNode*)n)->declared_type) continue;
        bool optional = false;
        Type* base = b ? lambda_type_nullable_lane_base(b->contract, &optional) : NULL;
        if (optional && (base == &TYPE_FLOAT || base == &TYPE_INT || base == &TYPE_BOOL)) b->contract = base;
    }
}

static bool array_iteration_plan(MvpCompiler* c, AstNode* body, MvpIterationPlan* plan, bool strings = false) {
    // a single-store loop already hoists its payload and checks each store; a second version adds no shared checks.
    if (c->dense_arrays || c->loop_array || !body ||
            (body->node_type != AST_NODE_CONTENT && body->node_type != AST_NODE_BINARY)) return false;
    LambdaMvpModule* m = c->module;
    AstIndex* index = &m->tp->ast_index;
    plan->root = ast_index_find(index, body);
    AstNodeId end = ast_index_subtree_end(index, plan->root);
    if (end - plan->root > 256) return false;
    for (AstNodeId i = plan->root; i < end; i++) {
        AstNode* n = index->nodes[i];
        if (n->node_type == AST_NODE_LOOP || n->node_type == AST_NODE_FOR_EXPR ||
                n->node_type == AST_NODE_RETURN_STAM || n->node_type == AST_NODE_BREAK_STAM ||
                n->node_type == AST_NODE_CONTINUE_STAM) return false;
        AstNode* object = NULL; AstNode* key = NULL;
        if (n->node_type == AST_NODE_INDEX_EXPR) {
            if (!strings && fused_character_index(c, n)) continue;
            object = ((AstFieldNode*)n)->object; key = ((AstFieldNode*)n)->field;
        } else if (n->node_type == AST_NODE_INDEX_ASSIGN_STAM) {
            object = ((AstAssignStamNode*)n)->object; key = ((AstAssignStamNode*)n)->key;
        }
        if (!object) continue;
        plan->access_count++;
        MvpBinding* b = mvp_local_binding(m, object);
        bool character = strings && b && b->contract->type_id == LMD_TYPE_STRING && fused_character_index(c, n);
        if (!character && (!b || !mvp_array_element(b->contract) || !b->array_native)) return false;
        if (character) {
            if (ast_index_node_descends(index, ast_index_find(index, b->entry->node), plan->root)) return false;
            uint32_t count = 0;
            const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, b->entry), &count);
            for (uint32_t u = 0; u < count; u++)
                if (index->nodes[uses[u]]->node_type == AST_NODE_ASSIGN_STAM &&
                        ast_index_node_descends(index, uses[u], plan->root)) return false;
        }
        unsigned a = 0;
        while (a < plan->array_count && plan->arrays[a] != b) a++;
        if (a == plan->array_count) {
            if (a == 16) return false;
            plan->arrays[plan->array_count++] = b; b->array_written = false;
        }
        unsigned k = 0;
        while (k < plan->key_count && (plan->key_arrays[k] != b ||
                !mvp_same_integer_expression(m, key, plan->keys[k]))) k++;
        if (k == plan->key_count) {
            if (k == 64) return false;
            plan->key_arrays[k] = b; plan->keys[plan->key_count++] = key;
        }
        if (n->source_span.end_byte > plan->last_access) plan->last_access = n->source_span.end_byte;
    }
    // a proven lone access has no absence checks to discharge; uncertain reads can still benefit from native consumers.
    if (!plan->access_count || (plan->access_count == 1 &&
            mvp_array_index_present(m, plan->key_arrays[0], plan->keys[0]))) return false;
    for (unsigned k = 0; k < plan->key_count; k++) if (!iteration_key(c, plan->keys[k], plan)) return false;
    narrow_dense_locals(m, plan->root);
    MvpIndexLoopPlan unused = {};
    return dense_region_eligible(c, plan->root, &unused, true);
}

static bool loop_key_slope(MvpCompiler* c, AstNode* source, MvpIterationPlan* plan,
        MvpBinding* counter, int* slope, int depth = 0) {
    AstNode* n = ast_unwrap_primary_to_leaf(source);
    if (!n || depth > 16 || mvp_value_contract(c->module, n)->type_id != LMD_TYPE_INT) return false;
    if (n->node_type == AST_NODE_IDENT) {
        MvpBinding* b = mvp_local_binding(c->module, n);
        if (!b) return false;
        if (b == counter) { *slope = 1; return true; }
        AstIndex* index = &c->module->tp->ast_index;
        uint32_t count = 0;
        const AstNodeId* uses = ast_index_binding_uses(index, ast_index_find_binding_entry(index, b->entry), &count);
        for (uint32_t i = 0; i < count; i++)
            if (index->nodes[uses[i]]->node_type == AST_NODE_ASSIGN_STAM &&
                    ast_index_node_descends(index, uses[i], plan->root)) return false;
        if (!ast_index_node_descends(index, ast_index_find(index, b->entry->node), plan->root)) {
            *slope = 0; return true;
        }
        // iteration_key already proved these declarations unconditional and pure.
        for (unsigned i = 0; i < plan->hoisted_count; i++) if (plan->hoisted[i] == b)
            return loop_key_slope(c, ((AstDeclaratorNode*)b->entry->node)->init, plan, counter, slope, depth + 1);
        return false;
    }
    if (n->node_type == AST_NODE_BINARY) {
        AstBinaryNode* b = (AstBinaryNode*)n;
        int left, right;
        if ((b->op != OPERATOR_ADD && b->op != OPERATOR_SUB) ||
                !loop_key_slope(c, b->left, plan, counter, &left, depth + 1) ||
                !loop_key_slope(c, b->right, plan, counter, &right, depth + 1)) return false;
        *slope = b->op == OPERATOR_ADD ? left + right : left - right;
        return true;
    }
    int64_t literal;
    if (!mir_decimal_int_literal(c->module->tp->source, n, &literal)) return false;
    *slope = 0; return true;
}

static MIR_label_t strided_array_loop(MvpCompiler* c, AstLoopControlNode* loop) {
    if (c->dense_arrays || c->dense_callee || c->loop_array || loop->form != LOOP_FORM_WHILE) return NULL;
    LambdaMvpModule* m = c->module;
    AstNode* condition = ast_unwrap_primary_to_leaf(loop->cond);
    if (!condition || condition->node_type != AST_NODE_BINARY) return NULL;
    AstBinaryNode* test = (AstBinaryNode*)condition;
    if (test->op != OPERATOR_LT && test->op != OPERATOR_LE) return NULL;
    MvpBinding* counter = mvp_local_binding(m, test->left);
    if (!counter || !counter->range.finite || !mvp_int_range(m, test->right).finite ||
            !loop->body || loop->body->node_type != AST_NODE_CONTENT) return NULL;
    AstNode* last = ((AstListNode*)loop->body)->item;
    while (last && last->next) last = last->next;
    if (!last || last->node_type != AST_NODE_ASSIGN_STAM) return NULL;
    AstAssignStamNode* update = (AstAssignStamNode*)last;
    if (update->target_entry != counter->entry || !mvp_int_range(m, update->value).finite) return NULL;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, loop->body);
    for (AstNodeId i = root; i < ast_index_subtree_end(index, root); i++) {
        AstNode* n = index->nodes[i];
        if (n->node_type == AST_NODE_ASSIGN_STAM && n != last &&
                ((AstAssignStamNode*)n)->target_entry == counter->entry) return NULL;
        if (n->node_type == AST_NODE_CALL_EXPR) {
            AstFuncNode* fn = ast_direct_call_function((AstCallNode*)n);
            for (AstNamedNode* p = fn ? fn->param : NULL; p; p = (AstNamedNode*)p->next)
                if (lambda_type_param(p->type)->is_var_param) return NULL;
        }
    }
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) { mvp_reject(m, loop, "strided loop binding allocation failed"); return NULL; }
    MvpIterationPlan plan = {};
    int slope = 0;
    bool eligible = array_iteration_plan(c, loop->body, &plan, true) && plan.access_count >= 2 &&
        iteration_key(c, test->right, &plan) && loop_key_slope(c, test->right, &plan, counter, &slope) && slope == 0 &&
        iteration_key(c, update->value, &plan) && loop_key_slope(c, update->value, &plan, counter, &slope) && slope == 1;
    for (unsigned k = 0; eligible && k < plan.key_count; k++)
        eligible = loop_key_slope(c, plan.keys[k], &plan, counter, &slope);
    MIR_label_t checked = NULL, done = NULL;
    if (eligible) {
        checked = label(c); done = label(c);
        MIR_reg_t first = counter->reg, limit = expression(c, test->right).reg;
        branch(c, test->op == OPERATOR_LT ? MIR_BGE : MIR_BGT, done, reg(c, first), reg(c, limit));
        MvpIntRange range = counter->range;
        counter->reg = constant(c, 0); counter->range = {true, 0, 0};
        MIR_reg_t step = expression(c, update->value).reg;
        counter->reg = first; counter->range = range;
        em_branch_int53_outside(&c->em, step, checked);
        branch(c, MIR_BLE, checked, reg(c, step), imm(c, 0));
        MIR_reg_t end = test->op == OPERATOR_LT ? operation(c, MIR_SUB, reg(c, limit), imm(c, 1)) : limit;
        MIR_reg_t distance = operation(c, MIR_SUB, reg(c, end), reg(c, first));
        MIR_reg_t remainder = operation(c, MIR_MOD, reg(c, distance), reg(c, step));
        MIR_reg_t final = operation(c, MIR_SUB, reg(c, end), reg(c, remainder));
        for (unsigned a = 0; a < plan.array_count; a++) {
            MvpBinding* b = plan.arrays[a];
            if (b->contract->type_id == LMD_TYPE_STRING) b->array_length = em_guard_ascii_string(&c->em, b->reg, checked);
            else branch(c, MIR_BEQ, checked, reg(c, b->array_written ? b->array_writable : b->array_native), imm(c, 0));
        }
        // affine keys stay between their first and final visited values, even
        // when the stride does not divide the limit (S7.1.1v3). Check both before effects.
        MIR_reg_t endpoints[] = {first, final};
        for (unsigned e = 0; e < 2; e++) {
            counter->reg = endpoints[e];
            for (unsigned h = 0; h < plan.hoisted_count; h++) {
                MvpBinding* b = plan.hoisted[h];
                move(c, b->reg, reg(c, expression(c, ((AstDeclaratorNode*)b->entry->node)->init).reg));
            }
            for (unsigned k = 0; k < plan.key_count; k++) {
                MIR_reg_t key = expression(c, plan.keys[k]).reg;
                branch(c, MIR_UBGE, checked, reg(c, key), reg(c, plan.key_arrays[k]->array_length));
            }
        }
        counter->reg = first;
        for (unsigned a = 0; a < plan.array_count; a++) {
            MvpBinding* b = plan.arrays[a];
            b->dense_items = b->contract->type_id == LMD_TYPE_STRING ? b->reg :
                em_load_at(&c->em, b->reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "stride_items");
        }
        MvpStringByte bytes[64]; unsigned byte_count = 0;
        MvpStringByte* saved_bytes = c->string_bytes;
        for (unsigned k = 0; k < plan.key_count; k++) {
            MvpBinding* b = plan.key_arrays[k];
            if (b->contract->type_id != LMD_TYPE_STRING ||
                    !loop_key_slope(c, plan.keys[k], &plan, counter, &slope) || slope) continue;
            // immutable owners and invariant keys permit one byte load for all iterations.
            MIR_reg_t byte = em_ascii_string_byte_at_pointer(&c->em, b->reg, expression(c, plan.keys[k]).reg);
            bytes[byte_count] = {c->string_bytes, b, plan.keys[k], byte};
            c->string_bytes = &bytes[byte_count++];
        }
        emit_closed_region(c, loop, true);
        c->string_bytes = saved_bytes;
        jump(c, done);
    }
    restore_bindings(m, saved); mem_free(saved);
    if (checked) mark(c, checked);
    return done;
}

static void guard_array_iteration(MvpCompiler* c, MvpIterationPlan* plan, MIR_label_t checked) {
    for (unsigned a = 0; a < plan->array_count; a++) {
        MvpBinding* b = plan->arrays[a];
        branch(c, MIR_BEQ, checked, reg(c, b->array_written ? b->array_writable : b->array_native), imm(c, 0));
    }
    for (unsigned h = 0; h < plan->hoisted_count; h++) {
        MvpBinding* b = plan->hoisted[h];
        MirValue key = expression(c, ((AstDeclaratorNode*)b->entry->node)->init);
        move(c, b->reg, reg(c, key.reg));
    }
    for (unsigned k = 0; k < plan->key_count; k++) {
        MirValue key = expression(c, plan->keys[k]);
        branch(c, MIR_UBGE, checked, reg(c, key.reg), reg(c, plan->key_arrays[k]->array_length));
    }
    for (unsigned a = 0; a < plan->array_count; a++) {
        MvpBinding* b = plan->arrays[a];
        b->dense_items = em_load_at(&c->em, b->reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "iteration_items");
    }
}

static MIR_label_t guarded_array_condition(MvpCompiler* c, AstNode* source, MIR_label_t target, bool when_true) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    if (c->dense_arrays || !node || node->node_type != AST_NODE_BINARY ||
            mvp_value_contract(c->module, node, 0, true) != &TYPE_BOOL) return NULL;
    LambdaMvpModule* m = c->module;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, node);
    unsigned reads = 0;
    for (AstNodeId i = root; i < ast_index_subtree_end(index, root) && reads < 2; i++)
        reads += index->nodes[i]->node_type == AST_NODE_INDEX_EXPR;
    if (reads < 2) return NULL;
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) { mvp_reject(m, node, "condition binding allocation failed"); return NULL; }
    MvpIterationPlan plan = {};
    MIR_label_t checked = NULL, done = NULL;
    if (array_iteration_plan(c, node, &plan) && plan.access_count >= 2) {
        checked = label(c); done = label(c);
        guard_array_iteration(c, &plan, checked);
        uint32_t gc_calls = c->em.frame.may_gc_call_count;
        c->dense_arrays = true;
        // a condition can have a closed read set even when its taken arm
        // changes search indices. Preserve the ordinary short-circuit branches.
        boolean_branch_body(c, source, target, when_true);
        c->dense_arrays = false;
        if (c->em.frame.may_gc_call_count != gc_calls)
            mvp_reject(m, node, "guarded condition emitted a collecting call");
        jump(c, done);
    }
    restore_bindings(m, saved); mem_free(saved);
    if (checked) mark(c, checked);
    return done;
}

static void array_iteration(MvpCompiler* c, AstNode* body) {
    if (c->dense_arrays || c->loop_array || !body || body->node_type != AST_NODE_CONTENT) {
        expression(c, body, true);
        return;
    }
    LambdaMvpModule* m = c->module;
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) { mvp_reject(m, body, "iteration binding allocation failed"); return; }
    MvpIterationPlan plan = {};
    bool dense = array_iteration_plan(c, body, &plan);
    MIR_label_t checked = NULL, done = NULL;
    if (dense) {
        checked = label(c); done = label(c);
        guard_array_iteration(c, &plan, checked);
        // guards run before effects; a miss executes this iteration once through the checked body.
        emit_closed_region(c, body, true);
        jump(c, done);
    }
    restore_bindings(m, saved); mem_free(saved);
    if (checked) mark(c, checked);
    expression(c, body, true);
    if (done) mark(c, done);
}

static void function_body(MvpCompiler* c) {
    AstFuncNode* fn = c->function->ast;
    c->tail_entry = label(c); mark(c, c->tail_entry);
    MirValue result = expression(c, fn->body);
    // explicit terminal returns also end fn bodies; only a boundary failure
    // can still need the block's ordinary completion path (S12.1.2v3).
    if (!mvp_proc_has_final_return(fn) || result.semantic_type == LMD_TYPE_ANY)
        return_value(c, result, NULL);
}

static void take_var_homes(MvpCompiler* c, bool retain) {
    int i = 0;
    for (AstNamedNode* param = c->function->ast->param; param; param = (AstNamedNode*)param->next, i++) {
        TypeParam* p = lambda_type_param(param->type);
        if (!p || !p->is_var_param) continue;
        MIR_reg_t home = em_take_var_home(&c->em, c->em.frame.runtime, i);
        // a dense body cannot replace an owner. Consume its transient cell,
        // but keep write-back addresses live only in the checked sibling.
        if (retain) mvp_binding(c->module, param->entry)->var_home = home;
    }
}

static void receive_parameter(MvpCompiler* c, MvpBinding* binding, MIR_reg_t input,
        int position) {
    ValueRep rep = c->function->abi.params[position].canonical_rep;
    MirValue incoming = value(c, input, rep == VALUE_REP_ITEM ? LMD_TYPE_ANY : binding->contract->type_id,
        rep, rep == VALUE_REP_ITEM ? &TYPE_ANY : binding->contract);
    TypeParam* parameter = lambda_type_param(binding->entry->node->type);
    if (parameter && parameter->is_optional) {
        MIR_reg_t selected = em_new_reg(&c->em, "default_argument", MIR_T_I64);
        MIR_label_t provided = label(c);
        move(c, selected, reg(c, input));
        branch(c, MIR_BNE, provided, reg(c, input), imm(c, ITEM_MISSING_ARGUMENT));
        MirValue fallback = convert(c, expression(c, parameter->default_value), VALUE_REP_ITEM);
        move(c, selected, reg(c, fallback.reg)); mark(c, provided);
        incoming = value(c, selected, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    }
    if (mvp_boxed_entry(c->function->ast) || (parameter && parameter->is_optional))
        incoming = admit(c, incoming, binding->contract);
    incoming = convert(c, incoming, binding->rep);
    move(c, binding->reg, reg(c, incoming.reg), incoming.mir_type);
}

bool mvp_emit_function(LambdaMvpModule* m, MvpFunction* fn) {
    MvpCompiler c = {}; c.module = m; c.function = fn;
    // construct caller witnesses once for owners repeatedly indexed through inlined accessors.
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId id = 0; id < index->count; id++) {
        if (index->owner_functions[id] != fn->id || index->nodes[id]->node_type != AST_NODE_CALL_EXPR) continue;
        AstCallNode* call = (AstCallNode*)index->nodes[id];
        MvpFunction* callee = mvp_function(m, ast_direct_call_function(call));
        if (!callee || !inline_eligible(&c, callee, true)) continue;
        AstNamedNode* param = callee->ast->param;
        for (AstNode* arg = call->argument; arg; arg = arg->next, param = (AstNamedNode*)param->next) {
            MvpBinding* source = mvp_local_binding(m, arg), *target = mvp_binding(m, param->entry);
            if (source && source->owner == fn->id && target && target->array_accessed && mvp_array_element(source->contract))
                source->array_accessed = true;
        }
    }
    c.em.ctx = m->ctx; c.em.import_cache = m->imports; c.em.call_owner = &c;
    c.em.label_counter = m->label_counter;
    c.em.root_call_value = root_value; c.em.convert_rep = convert;
    c.em.lookup_import_metadata = jit_import_get_metadata;
    c.em.name_pool = m->tp->name_pool; c.em.const_list = m->tp->const_list; c.em.consts_bss = m->consts;
    c.em.helper_results_skip_rehome = true;
    MIR_var_t args[LAMBDA_MAX_FUNCTION_ARGS + 1] = {{MIR_T_P, "runtime", 0}};
    char names[LAMBDA_MAX_FUNCTION_ARGS][16];
    for (int i = 0; i < fn->abi.param_count; i++) {
        snprintf(names[i], sizeof(names[i]), "arg%d", i);
        args[i + 1] = {em_mir_type_for_rep(fn->abi.params[i].canonical_rep), names[i], 0};
    }
    MIR_type_t returns[] = {em_mir_type_for_rep(fn->abi.result.normal.abi_rep), MIR_T_I64};
    fn->target = MIR_new_func_arr(m->ctx, fn->name, em_return_nres(fn->abi.result.companion),
        returns, fn->abi.param_count + 1, args);
    c.em.func_item = fn->target; c.em.func = MIR_get_item_func(m->ctx, fn->target);
    MirFrameState* f = &c.em.frame;
    f->active = true; f->number_active = true; f->item_return = fn->abi.result.normal.abi_rep == VALUE_REP_ITEM;
    f->return_type = returns[0]; f->scalar_return_mode = fn->abi.result.normal.scalar_class;
    f->return_lane_kind = f->item_return ? RETURN_LANE_SCALAR :
        fn->abi.result.shape == RETURN_SHAPE_NATIVE_ERROR ? RETURN_LANE_ERROR : RETURN_LANE_NONE;
    f->runtime = MIR_reg(m->ctx, "runtime", c.em.func);
    for (int i = 0; i <= fn->abi.param_count; i++)
        em_function_argument_register(&c.em, MIR_reg(m->ctx, args[i].name, c.em.func));
    // public admission may collect before later operands are converted.
    // All incoming Item owners are precise candidates from the entry.
    if (fn->ast && mvp_boxed_entry(fn->ast))
        for (int i = 1; i <= fn->abi.param_count; i++)
            root_value(&c, MIR_reg(m->ctx, args[i].name, c.em.func), JIT_VALUE_BOXED_ITEM);
    f->root_base = em_new_reg(&c.em, "roots", MIR_T_I64);
    f->root_end = em_new_reg(&c.em, "roots_end", MIR_T_I64);
    f->number_base = em_new_reg(&c.em, "numbers", MIR_T_I64);
    f->return_reg = em_new_reg(&c.em, "result", f->return_type);
    if (f->return_lane_kind == RETURN_LANE_ERROR) f->error_return_reg = em_new_reg(&c.em, "error", MIR_T_I64);
    if (f->item_return) root_value(&c, f->return_reg, JIT_VALUE_BOXED_ITEM);
    f->anchor = label(&c); f->return_label = label(&c); f->native_stack_overflow = label(&c);
    f->plan.entry_kind = fn->abi.entry.kind;
    f->plan.entry_mode = fn == &m->entry ? MIR_ENTRY_CHECKED : MIR_ENTRY_BOUND_INTERNAL;
    f->plan.debug_name = fn->name;
    em_plan_bind_return(&f->plan, &fn->abi.result, fn == &m->entry);
    mark(&c, f->anchor);
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (b->owner != fn->id || !b->entry || !b->entry->node ||
                (b->entry->node->node_type != AST_NODE_PARAM &&
                 b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR &&
                 b->entry->node->node_type != AST_NODE_FOR_CLAUSE &&
                 b->entry->node->node_type != AST_NODE_FOR_INDEX && !b->iteration_binding)) continue;
        b->reg = em_new_reg(&c.em, "binding", em_mir_type_for_rep(b->rep));
        value(&c, b->reg, b->contract->type_id, b->rep, b->contract);
    }
    if (fn->ast) {
        bool snapshots = function_has_snapshots(m, fn);
        if (snapshots) {
            int i = 0;
            for (AstNamedNode* p = fn->ast->param; p; p = (AstNamedNode*)p->next, i++)
                receive_parameter(&c, mvp_binding(m, p->entry), MIR_reg(m->ctx, args[i + 1].name, c.em.func), i);
            // entry allocation may return an error before the body starts;
            // consume borrow homes while all parameter owners are initialized.
            take_var_homes(&c, true);
        }
        MvpIndexLoopPlan indices = {};
        MvpBinding* saved = snapshot_bindings(m);
        if (!saved) { mvp_reject(m, fn->ast, "dense binding allocation failed"); return false; }
        // eligibility proves every initializer and write before narrowed lanes are emitted.
        narrow_dense_locals(m, ast_index_find(&m->tp->ast_index, fn->ast->body), true);
        MvpDenseCalls calls = {};
        bool dense = dense_body_eligible(&c, &indices, &calls);
        if (!dense) {
            restore_bindings(m, saved);
            narrow_dense_locals(m, ast_index_find(&m->tp->ast_index, fn->ast->body));
        }
        // allocating leaf bodies use the same extent proof, but keep owner
        // roots rather than raw buffers live across their result allocation.
        bool allocating = !dense && fn->inline_cost <= 256 && dense_body_eligible(&c, &indices, NULL, true);
        if (!dense && !allocating) { restore_bindings(m, saved); mem_free(saved); }
        if (dense) for (uint32_t i = 0; i < m->binding_count; i++) {
            MvpBinding* b = &m->bindings[i];
            if (b->owner == fn->id && em_mir_type_for_rep(b->rep) != em_mir_type_for_rep(saved[i].rep))
                // a proved float outcome needs a D register; restoring the
                // saved bindings keeps the checked sibling's Item register.
                b->reg = em_new_reg(&c.em, "guarded_binding", em_mir_type_for_rep(b->rep));
        }
        int i = 0;
        for (AstNamedNode* param = fn->ast->param; param; param = (AstNamedNode*)param->next, i++) {
            MvpBinding* b = mvp_binding(m, param->entry);
            if (!b || !b->reg) { mvp_reject(m, param, "parameter has no indexed binding"); break; }
            if (!snapshots)
                receive_parameter(&c, b, MIR_reg(m->ctx, args[i + 1].name, c.em.func), i);
            snapshot_parameter(&c, b);
            if (!dense && !allocating) refresh_array(&c, b);
        }
        if (dense || allocating) {
            MIR_label_t checked = label(&c);
            // consume each witness directly instead of retaining all arrays'
            // flags across the guard chain; the checked sibling builds metadata.
            for (uint32_t b = 0; b < m->binding_count; b++) {
                MvpBinding* binding = &m->bindings[b];
                Type* element = mvp_array_element(binding->contract);
                if (binding->owner != fn->id || !element) continue;
                binding->array_length = dense_owner_guard(&c, binding, binding->array_extent, binding->array_written, checked);
            }
            if (indices.array) {
                branch(&c, MIR_UBGE, checked, reg(&c, indices.limit->reg), reg(&c, indices.array->array_length));
                branch(&c, MIR_UBGT, checked, reg(&c, indices.array->array_length), imm(&c, INT53_MAX - 1));
                branch(&c, MIR_UBGT, checked, reg(&c, indices.first->reg), reg(&c, indices.limit->reg));
            }
            if (!snapshots) take_var_homes(&c, allocating);
            for (uint32_t b = 0; b < m->binding_count; b++) {
                MvpBinding* binding = &m->bindings[b];
                if (binding->owner == fn->id && mvp_array_element(binding->contract)) {
                    if (allocating) binding->array_present = true;
                    else binding->dense_items = em_load_at(&c.em, binding->reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "dense_items");
                }
            }
            if (indices.array) {
                // equal initial counters and at most one follower step preserve
                // first <= follower <= counter <= limit, including the final swap.
                indices.first->range = indices.limit->range = {true, 0, INT53_MAX - 1};
                indices.counter->range = indices.follower->range = {true, 0, INT53_MAX - 1};
            }
            if (allocating) {
                uint32_t cost = c.inline_cost;
                c.guarded_allocating = true;
                function_body(&c);
                c.guarded_allocating = false;
                c.inline_cost = cost;
            } else {
                dense_call_effects(&calls, true);
                c.dense_calls = true;
                emit_closed_region(&c, NULL, true);
                c.dense_calls = false;
                dense_call_effects(&calls, false);
            }
            restore_bindings(m, saved); mem_free(saved);
            mark(&c, checked);
            for (AstNamedNode* param = fn->ast->param; param; param = (AstNamedNode*)param->next)
                refresh_array(&c, mvp_binding(m, param->entry));
        }
        if (!snapshots) take_var_homes(&c, true);
        function_body(&c);
    } else {
        MirValue result = expression(&c, m->tp->ast_root);
        if (m->main && m->main->ast->node_type == AST_NODE_PROC) {
            result = convert(&c, result, VALUE_REP_ITEM);
            MIR_reg_t output = em_new_reg(&c.em, "module_result", MIR_T_I64);
            move(&c, output, reg(&c, result.reg));
            MIR_label_t done = label(&c);
            branch(&c, MIR_BEQ, done, reg(&c, operation(&c, MIR_URSH, reg(&c, output), imm(&c, 56))), imm(&c, LMD_TYPE_ERROR));
            MIR_reg_t run = em_load_at(&c.em, f->runtime, offsetof(Context, run_main), MIR_T_U8, "run_main");
            branch(&c, MIR_BEQ, done, reg(&c, run), imm(&c, 0));
            MirValue main = convert(&c, direct_call(&c, m->main, NULL), VALUE_REP_ITEM);
            move(&c, output, reg(&c, main.reg)); mark(&c, done);
            result = value(&c, output, LMD_TYPE_ANY, VALUE_REP_ITEM);
        }
        return_value(&c, result, NULL);
    }
    mark(&c, f->return_label); epilogue(&c);
    if (!fn->abi.effects.may_gc && f->may_gc_call_count)
        mvp_reject(m, fn->ast, "emitted helper effects exceed the planned noncollecting contract");
    MirRootWriteBackResult roots = {};
    f->fixed_root_slots = c.argument_count;
    if (!em_finalize_semantic_root_write_back(&c.em, f->root_base, f->anchor, false, 0,
            &f->gc_candidates, &f->gc_candidate_count, &f->gc_candidate_capacity,
            &f->gc_candidate_by_reg, &f->gc_candidate_by_reg_capacity,
            f->gc_call_sites, f->gc_call_site_count, &roots, fn->name))
        mvp_reject(m, fn->ast, "precise root planning failed");
    f->root_slot_count = roots.stable_slots + roots.scratch_slots; f->root_store_count = roots.inserted_stores;
    if (c.argument_fixup) c.argument_fixup->ops[2].u.i = (int64_t)f->root_slot_count * sizeof(Item);
    f->root_slot_count += c.argument_count;
    em_finalize_scalar_homes(&c.em);
    em_finalize_frame_prologue(&c.em, f->plan.entry_mode,
        offsetof(Context, side_root_top), offsetof(Context, side_root_limit),
        offsetof(Context, side_number_top), offsetof(Context, side_number_limit),
        offsetof(Context, side_root_commit_limit), offsetof(Context, side_number_commit_limit));
    // this cold exit invokes the shared recovery guard before any frame reservation was published.
    MIR_item_t debug_name = MIR_new_string_data(m->ctx, NULL,
        (MIR_str_t){strlen(fn->name) + 1, fn->name});
    em_call_void_1(&c.em, "lambda_stack_overflow_error", MIR_T_P,
        MIR_new_ref_op(m->ctx, debug_name), true);
    // distinct dead lanes avoid MIR coalescing a pair into ret r,r during return normalization.
    MIR_op_t dead = f->return_type == MIR_T_D ? MIR_new_double_op(m->ctx, 0)
        : imm(&c, f->item_return ? ITEM_ERROR : 0);
    MIR_op_t dead_companion = imm(&c, f->item_return ? 0 : ITEM_ERROR);
    em_emit_insn(&c.em, em_returns_result_pair(f->plan.companion)
        ? MIR_new_ret_insn(m->ctx, 2, dead, dead_companion) : MIR_new_ret_insn(m->ctx, 1, dead));
    em_finalize_function_metadata(&c.em);
    m->stats.functions++;
    m->stats.instructions += c.em.last_function.mir_instruction_count;
    if (m->stats.largest_function < c.em.last_function.mir_instruction_count)
        m->stats.largest_function = c.em.last_function.mir_instruction_count;
    log_debug("lambda-mvp-function: name=%s instructions=%u roots=%u homes=%u", fn->name,
        c.em.last_function.mir_instruction_count, c.em.last_function.root_slot_count,
        c.em.last_function.scalar_home_count);
    m->label_counter = c.em.label_counter;
    MIR_finish_func(m->ctx); em_frame_dispose(&c.em);
    while (c.float_constants) {
        MvpFloatConstant* next = c.float_constants->next;
        mem_free(c.float_constants); c.float_constants = next;
    }
    return !m->failed;
}
