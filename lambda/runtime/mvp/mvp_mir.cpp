#include "mvp_internal.hpp"
#include "../type_contract.hpp"
#include "../mir_loop_analysis.hpp"
#include "../mir_bitwise.hpp"

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
static MirValue value(MvpCompiler* c, MIR_reg_t r, TypeId type, ValueRep rep,
        Type* contract = NULL, AstNode* source = NULL) {
    MirValue v = em_value_for_rep(r, type, rep, contract, source);
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
static MirValue convert(void* owner, MirValue v, ValueRep to) {
    MvpCompiler* c = (MvpCompiler*)owner;
    if (v.maybe_pending) v = em_materialize_pending_value(&c->em, v, MIR_PENDING_REASON_REP_CONVERSION);
    if (v.rep == to) return v;
    MIR_reg_t r = 0;
    if (to == VALUE_REP_ITEM) {
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
            r = em_box_f64_to_item(&c->em, c, box_float, v.reg);
        } else if (v.rep == VALUE_REP_I64 && v.semantic_type == LMD_TYPE_BOOL) {
            r = operation(c, MIR_OR, reg(c, v.reg), imm(c, (uint64_t)LMD_TYPE_BOOL << 56));
        } else if (v.rep == VALUE_REP_RAW_GC_POINTER && v.semantic_type == LMD_TYPE_STRING) {
            r = operation(c, MIR_OR, reg(c, v.reg), imm(c, (uint64_t)LMD_TYPE_STRING << 56));
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
        r = em_unbox_f64_item(&c->em, c, unbox_float, v.reg);
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
        r = em_call_1(&c->em, "lambda_double_to_int_lane_c", MIR_T_I64, MIR_T_D, reg(c, v.reg), true);
    } else if (to == VALUE_REP_I64 && v.rep == VALUE_REP_ITEM && v.semantic_type == LMD_TYPE_BOOL) {
        r = operation(c, MIR_AND, reg(c, v.reg), imm(c, 1));
    } else if (to == VALUE_REP_RAW_GC_POINTER && v.rep == VALUE_REP_ITEM && v.semantic_type == LMD_TYPE_STRING) {
        r = operation(c, MIR_AND, reg(c, v.reg), imm(c, 0x00ffffffffffffffULL));
    }
    if (!r) {
        mvp_reject(c->module, (AstNode*)v.provenance_node, "unsupported scalar representation conversion");
        // finish a well-typed, unpublished MIR function so ordinary teardown can report the rejection.
        r = em_new_reg(&c->em, "rejected", em_mir_type_for_rep(to));
        move(c, r, to == VALUE_REP_F64 ? MIR_new_double_op(c->em.ctx, 0) : imm(c, 0), em_mir_type_for_rep(to));
    }
    return value(c, r, v.semantic_type, to, v.semantic_contract, (AstNode*)v.provenance_node);
}
static MirValue expression(MvpCompiler* c, AstNode* node);
static MirValue statements(MvpCompiler* c, AstNode* node);

static MIR_label_t countdown_loop(MvpCompiler* c, AstLoopControlNode* node) {
    MirFiniteDescendingSumPlan plan = {};
    if (!mir_descending_sum_pattern(c->module->tp->source, node, &plan)) return NULL;
    MvpBinding* n = mvp_binding(c->module, plan.counter);
    MvpBinding* sum = mvp_binding(c->module, plan.accumulator);
    if (!n || !sum || n->owner != c->function->id || sum->owner != c->function->id ||
            n->rep != VALUE_REP_INT_LANE || sum->rep != VALUE_REP_INT_LANE) return NULL;
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
    MirFrameState* f = &c->em.frame;
    MIR_op_t normal = f->item_return ? reg(c, error) : f->return_type == MIR_T_D
        ? MIR_new_double_op(c->em.ctx, 0) : imm(c, 0);
    em_stage_function_return(&c->em, normal, error);
}

static MirValue admit(MvpCompiler* c, MirValue v, Type* contract, MvpErrorTarget* failure = NULL,
        const char* site = "MVP scalar boundary") {
    contract = type_field_unwrap_simple_decl(contract);
    if (!contract || contract->type_id == LMD_TYPE_ANY) return convert(c, v, VALUE_REP_ITEM);
    // a static success contract cannot erase a boxed error join (D2.8.2, S11.4.1v3).
    bool proven = v.semantic_contract &&
        lambda_static_boundary_relation(v.semantic_contract, contract, false) == STATIC_BOUNDARY_PROVEN;
    if (!proven) {
        bool success_proven = v.admitted_success_contract &&
            lambda_static_boundary_relation(v.admitted_success_contract, contract, false) == STATIC_BOUNDARY_PROVEN;
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
        if (failure) {
            if (!failure->done) { failure->done = label(c); failure->error = em_new_reg(&c->em, "boundary_error", MIR_T_I64); }
            move(c, failure->error, reg(c, checked)); jump(c, failure->done);
        } else stage_error(c, checked);
        mark(c, accepted);
        v = value(c, checked, contract->type_id, VALUE_REP_ITEM, contract);
    }
    v = convert(c, v, mvp_rep(contract));
    v.semantic_type = contract->type_id; v.semantic_contract = contract;
    return v;
}

static void store_binding(MvpCompiler* c, MvpBinding* binding, MirValue v) {
    AstNode* node = binding->entry->node;
    bool inferred = node->node_type == AST_NODE_VARIABLE_DECLARATOR && !((AstDeclaratorNode*)node)->declared_type;
    // the declaration's pooled name outlives the JIT artifact and identifies failed assignments (S7.7.4).
    const char* site = binding->entry->name ? binding->entry->name->chars : "MVP scalar binding";
    v = inferred && binding->rep == VALUE_REP_ITEM ? convert(c, v, VALUE_REP_ITEM)
        : admit(c, v, binding->contract, binding->scope ? &binding->scope->failure : NULL, site);
    if (binding->rep == VALUE_REP_ITEM) {
        if (!binding->scalar_home) binding->scalar_home = em_scalar_home_new(&c->em);
        MIR_reg_t home = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, binding->scalar_home));
        // mutable boxed bindings own their cold scalar bytes, independently of a reusable producer home (D5.2.3).
        v.reg = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_DYNAMIC, v.reg, home);
    }
    move(c, binding->reg, reg(c, v.reg), v.mir_type);
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
    } else if (t->is_literal && t->type_id == LMD_TYPE_STRING) {
        r = em_load_const(&c->em, ((TypeConst*)t)->const_index, MIR_T_P);
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
    return convert(c, v, VALUE_REP_I64).reg;
}

static void boolean_branch(MvpCompiler* c, AstNode* source, MIR_label_t target, bool when_true) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    if (node && node->node_type == AST_NODE_BINARY && mvp_value_contract(c->module, node)->type_id == LMD_TYPE_BOOL) {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        if (binary->op == OPERATOR_AND || binary->op == OPERATOR_OR) {
            bool conjunction = binary->op == OPERATOR_AND;
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
    }
    // branch demand consumes boolean short-circuiting without a materialized phi (S3.4).
    MirValue v = expression(c, source);
    branch(c, when_true ? MIR_BNE : MIR_BEQ, target, reg(c, truth(c, v)), imm(c, 0));
}

static MirValue int_arithmetic(MvpCompiler* c, AstBinaryNode* n, MirValue a, MirValue b) {
    if (mvp_int_range(c->module, n).finite) {
        MIR_insn_code_t op = n->op == OPERATOR_ADD ? MIR_ADD : n->op == OPERATOR_SUB ? MIR_SUB : MIR_MUL;
        return value(c, operation(c, op, reg(c, a.reg), reg(c, b.reg)),
            LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, n);
    }
    MIR_reg_t r = em_new_reg(&c->em, "int_result", MIR_T_I64);
    MIR_label_t slow = label(c), done = label(c);
    bool left_finite = mvp_int_range(c->module, (AstNode*)a.provenance_node).finite;
    bool right_finite = mvp_int_range(c->module, (AstNode*)b.provenance_node).finite;
    bool add_sub = n->op == OPERATOR_ADD || n->op == OPERATOR_SUB;
    // a finite add/sub result plus one finite operand excludes every sentinel partner (S4.1.1).
    if (!add_sub) {
        if (!left_finite) em_branch_int53_outside(&c->em, a.reg, slow);
        if (!right_finite) em_branch_int53_outside(&c->em, b.reg, slow);
    }
    bool division = n->op == OPERATOR_IDIV || n->op == OPERATOR_MOD;
    if (division) branch(c, MIR_BEQ, slow, reg(c, b.reg), imm(c, 0));
    MIR_insn_code_t op = n->op == OPERATOR_ADD ? MIR_ADD : n->op == OPERATOR_SUB ? MIR_SUB :
        n->op == OPERATOR_MUL ? MIR_MULO : n->op == OPERATOR_IDIV ? MIR_DIV : MIR_MOD;
    em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, op, reg(c, r), reg(c, a.reg), reg(c, b.reg)));
    if (op == MIR_MULO) em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_BO, MIR_new_label_op(c->em.ctx, slow)));
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
    return value(c, r, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, n);
}

static MirValue numeric_binary(MvpCompiler* c, AstBinaryNode* n, MirValue a, MirValue b) {
    bool booleans = a.semantic_type == LMD_TYPE_BOOL && b.semantic_type == LMD_TYPE_BOOL;
    bool ints = a.semantic_type == LMD_TYPE_INT && b.semantic_type == LMD_TYPE_INT && n->op != OPERATOR_DIV;
    ValueRep rep = booleans ? VALUE_REP_I64 : ints ? VALUE_REP_INT_LANE : VALUE_REP_F64;
    a = convert(c, a, rep); b = convert(c, b, rep);
    MirNumericOpPlan plan = {};
    if (ints && (n->op == OPERATOR_ADD || n->op == OPERATOR_SUB || n->op == OPERATOR_MUL ||
            n->op == OPERATOR_IDIV || n->op == OPERATOR_MOD)) return int_arithmetic(c, n, a, b);
    if (!em_numeric_op_plan(n->op, &plan, ints)) {
        mvp_reject(c->module, n, "numeric operator is outside the scalar MVP"); return null_value(c);
    }
    MIR_reg_t r;
    MIR_type_t result_type = plan.is_comparison || booleans ? MIR_T_I64 : MIR_T_D;
    if (plan.helper_name) r = em_call_2(&c->em, plan.helper_name, MIR_T_D,
        MIR_T_D, reg(c, a.reg), MIR_T_D, reg(c, b.reg), true);
    else r = operation(c, ints || booleans ? plan.i64_opcode : plan.f64_opcode,
        reg(c, a.reg), reg(c, b.reg), result_type);
    if (ints && plan.is_comparison) {
        bool left_finite = mvp_int_range(c->module, n->left).finite;
        bool right_finite = mvp_int_range(c->module, n->right).finite;
        if (!left_finite || !right_finite) {
            // predicate masks avoid control-flow joins in every native integer comparison.
            MIR_reg_t ordered = 0;
            if (!left_finite) ordered = operation(c, MIR_NE, reg(c, a.reg), imm(c, INT_LANE_NAN));
            if (!right_finite) {
                MIR_reg_t right = operation(c, MIR_NE, reg(c, b.reg), imm(c, INT_LANE_NAN));
                ordered = ordered ? operation(c, MIR_AND, reg(c, ordered), reg(c, right)) : right;
            }
            if (n->op == OPERATOR_NE) {
                MIR_reg_t unordered = operation(c, MIR_EQ, reg(c, ordered), imm(c, 0));
                r = operation(c, MIR_OR, reg(c, r), reg(c, unordered));
            } else r = operation(c, MIR_AND, reg(c, r), reg(c, ordered));
        }
    }
    return value(c, r, plan.is_comparison ? LMD_TYPE_BOOL : LMD_TYPE_FLOAT,
        plan.is_comparison ? VALUE_REP_I64 : VALUE_REP_F64, plan.is_comparison ? &TYPE_BOOL : &TYPE_FLOAT, n);
}

static TypeId numeric_success(MirValue v) {
    Type* t = v.admitted_success_contract ? v.admitted_success_contract : v.semantic_contract;
    return t ? t->type_id : v.semantic_type;
}

static MirValue binary(MvpCompiler* c, AstBinaryNode* n) {
    MirValue a = expression(c, n->left);
    if (n->op == OPERATOR_AND || n->op == OPERATOR_OR) {
        bool booleans = a.semantic_type == LMD_TYPE_BOOL && n->right->type &&
            n->right->type->type_id == LMD_TYPE_BOOL && !ast_expr_may_defect(n->right, 0);
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
            booleans ? VALUE_REP_I64 : VALUE_REP_ITEM, booleans ? &TYPE_BOOL : &TYPE_ANY, n);
    }
    MirValue b = expression(c, n->right);
    if (n->op == OPERATOR_JOIN) {
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        return value(c, em_call_2(&c->em, "fn_join", MIR_T_I64, MIR_T_I64, reg(c, a.reg),
            MIR_T_I64, reg(c, b.reg), true), n->type->type_id, VALUE_REP_ITEM, n->type, n);
    }
    TypeId left = numeric_success(a), right = numeric_success(b);
    bool numeric = (left == LMD_TYPE_INT || left == LMD_TYPE_FLOAT) &&
        (right == LMD_TYPE_INT || right == LMD_TYPE_FLOAT);
    bool booleans = a.semantic_type == LMD_TYPE_BOOL && b.semantic_type == LMD_TYPE_BOOL &&
        (n->op == OPERATOR_EQ || n->op == OPERATOR_NE);
    if (!numeric && !booleans) {
        const char* helper = n->op == OPERATOR_ADD ? "fn_add" : n->op == OPERATOR_SUB ? "fn_sub" :
            n->op == OPERATOR_MUL ? "fn_mul" : n->op == OPERATOR_DIV ? "fn_div" :
            n->op == OPERATOR_IDIV ? "fn_idiv" : n->op == OPERATOR_MOD ? "fn_mod" :
            n->op == OPERATOR_EQ ? "fn_eq_value" : n->op == OPERATOR_NE ? "fn_ne_value" :
            n->op == OPERATOR_LT ? "fn_lt" : n->op == OPERATOR_LE ? "fn_le" :
            n->op == OPERATOR_GT ? "fn_gt" : n->op == OPERATOR_GE ? "fn_ge" : NULL;
        if (!helper) {
            mvp_reject(c->module, n, "comparison requires proven numeric or bool contracts"); return null_value(c);
        }
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        return value(c, em_call_2(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, a.reg),
            MIR_T_I64, reg(c, b.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
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

static bool inline_eligible(MvpCompiler* c, MvpFunction* f) {
    if (!f->inline_cost || f->inline_cost > 96 || c->inline_cost + f->inline_cost > 256 ||
            f->abi.effects.may_gc || f->abi.effects.may_return_error || f->abi.effects.may_reenter ||
            f->abi.result.normal.abi_rep == VALUE_REP_ITEM ||
            f->abi.result.normal.abi_rep == VALUE_REP_RAW_GC_POINTER) return false;
    for (int i = 0; i < f->abi.param_count; i++)
        if (f->abi.params[i].canonical_rep == VALUE_REP_ITEM ||
                f->abi.params[i].canonical_rep == VALUE_REP_RAW_GC_POINTER) return false;
    // one expansion per target on a path retains real recursive activations and their stack guards.
    for (MvpInline* p = c->inlining; p; p = p->parent)
        if (p->function == f) return false;
    return true;
}

static MirValue inline_call(MvpCompiler* c, MvpFunction* f, MIR_op_t* args) {
    LambdaMvpModule* m = c->module;
    MvpBinding* saved = (MvpBinding*)mem_alloc(m->binding_count * sizeof(MvpBinding), MEM_CAT_TEMP);
    if (!saved) { mvp_reject(m, f->ast, "inline binding allocation failed"); return null_value(c); }
    memcpy(saved, m->bindings, m->binding_count * sizeof(MvpBinding));
    MvpFunction* caller = c->function;
    MvpScope* scope = c->scope;
    MvpControl* control = c->control;
    MvpInline frame = {c->inlining, f,
        em_new_reg(&c->em, "inline_result", em_mir_type_for_rep(f->abi.result.normal.abi_rep)), label(c)};
    c->inlining = &frame; c->function = f; c->scope = NULL; c->control = NULL;
    c->inline_cost += f->inline_cost;
    // every expansion owns fresh locals; recursive expansion must not overwrite the caller's bindings.
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (b->owner != f->id) continue;
        b->reg = em_new_reg(&c->em, "inline_binding", em_mir_type_for_rep(b->rep));
        b->scalar_home = 0; b->scope = NULL;
    }
    int i = 0;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, i++) {
        MvpBinding* b = mvp_binding(m, p->entry);
        move(c, b->reg, args[i], em_mir_type_for_rep(b->rep));
    }
    MirValue v = expression(c, f->ast->body);
    if (f->ast->node_type != AST_NODE_PROC) {
        v = admit(c, v, f->result_contract);
        move(c, frame.result, reg(c, v.reg), v.mir_type);
    }
    mark(c, frame.done);
    c->inlining = frame.parent; c->function = caller; c->scope = scope; c->control = control;
    memcpy(m->bindings, saved, m->binding_count * sizeof(MvpBinding));
    mem_free(saved);
    return value(c, frame.result, f->result_contract->type_id,
        f->abi.result.normal.abi_rep, f->result_contract);
}

static MirValue direct_call(MvpCompiler* c, MvpFunction* f, AstNode* args) {
    MIR_type_t types[LAMBDA_MAX_FUNCTION_ARGS]; MIR_op_t ops[LAMBDA_MAX_FUNCTION_ARGS];
    AstNamedNode* param = f->ast->param;
    MvpErrorTarget failure = {};
    int i = 0;
    for (AstNode* arg = args; arg; arg = arg->next, param = (AstNamedNode*)param->next, i++) {
        MirValue v = admit(c, expression(c, arg), mvp_contract(param), &failure);
        types[i] = v.mir_type; ops[i] = reg(c, v.reg);
    }
    if (!failure.done && inline_eligible(c, f)) return inline_call(c, f, ops);
    MirCallOptions options = {true, false, 0};
    MirCallResult result = em_call_direct(&c->em, f->name, f->target, &f->abi, i, types, ops, &options);
    // stack exhaustion is the sole native error exit in the admitted subset; the recovery guard owns it.
    MirValue v = em_finish_direct_call_normal(&c->em, result, MIR_PENDING_REASON_REP_CONVERSION);
    if (failure.done || (f->abi.effects.may_return_error && result.error.reg)) {
        MIR_reg_t joined = em_new_reg(&c->em, "call_join", MIR_T_I64);
        MIR_label_t clean = label(c), done = label(c);
        if (f->abi.effects.may_return_error && result.error.reg) {
            branch(c, MIR_BEQ, clean, reg(c, result.error.reg),
                imm(c, em_error_lane_in_register(f->abi.result.companion) ? ITEM_NULL : 0));
            move(c, joined, reg(c, result.error.reg)); jump(c, done);
        }
        mark(c, clean);
        v = convert(c, v, VALUE_REP_ITEM); move(c, joined, reg(c, v.reg)); mark(c, done);
        if (failure.done) {
            MIR_label_t completed = label(c); jump(c, completed); mark(c, failure.done);
            move(c, joined, reg(c, failure.error)); mark(c, completed);
        }
        return value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    }
    return value(c, v.reg, v.semantic_type, v.rep, f->result_contract);
}
static MirValue call(MvpCompiler* c, AstCallNode* node) {
    MvpFunction* f = mvp_function(c->module, ast_direct_call_function(node));
    if (f) return direct_call(c, f, node->argument);
    SysFuncInfo* info = ((AstSysFuncNode*)ast_unwrap_primary_to_leaf(node->function))->fn_info;
    if (info->fn == SYSFUNC_BXOR || info->fn == SYSFUNC_SHL) {
        MirValue a = expression(c, node->argument), b = expression(c, node->argument->next);
        MIR_reg_t r = em_guarded_int_bitwise(&c->em, info->fn, a, b,
            info->fn == SYSFUNC_BXOR ? "fn_bxor_item" : "fn_shl_item");
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
    bool native_math = (info->fn == SYSFUNC_ABS || info->fn == SYSFUNC_FLOOR) &&
        mvp_value_contract(c->module, node->argument)->type_id == LMD_TYPE_FLOAT;
    if (native_math) { name = info->native_c_name; result_rep = VALUE_REP_F64; tid = LMD_TYPE_FLOAT; }
    else if (info->c_ret_type == C_RET_DOUBLE) result_rep = VALUE_REP_F64;
    else if (info->c_ret_type == C_RET_STRING) result_rep = VALUE_REP_RAW_GC_POINTER;
    else if (info->c_ret_type == C_RET_INT64) result_rep = VALUE_REP_INT_LANE;
    int i = 0;
    for (AstNode* arg = node->argument; arg; arg = arg->next, i++) {
        MirValue v = convert(c, expression(c, arg), native_math ? VALUE_REP_F64 :
            sysfunc_arg_required_rep(info, i));
        types[i] = v.mir_type; ops[i] = reg(c, v.reg);
    }
    MIR_reg_t r = em_call_with_args(&c->em, name, em_mir_type_for_rep(result_rep), i, types, ops, true);
    if (result_rep == VALUE_REP_ITEM && info->fn != SYSPROC_PRINT) tid = LMD_TYPE_ANY;
    return value(c, r, tid, result_rep, tid == LMD_TYPE_ANY ? &TYPE_ANY : node->type, node);
}
static void return_value(MvpCompiler* c, MirValue v, AstNode* node) {
    Type* contract = c->function->result_contract;
    v = admit(c, v, contract);
    if (c->inlining) {
        move(c, c->inlining->result, reg(c, v.reg), v.mir_type);
        jump(c, c->inlining->done);
        return;
    }
    em_stage_function_return(&c->em, reg(c, v.reg));
}
static MirValue expression(MvpCompiler* c, AstNode* node) {
    if (!node || c->module->failed) return null_value(c);
    switch (node->node_type) {
    case AST_NODE_PRIMARY: return literal(c, (AstPrimaryNode*)node);
    case AST_NODE_IDENT: {
        MvpBinding* b = mvp_binding(c->module, ((AstIdentNode*)node)->entry);
        if (!b || !b->reg || b->owner != c->function->id) {
            mvp_reject(c->module, node, "a value read requires a local scalar binding"); return null_value(c);
        }
        // snapshot before a later operand can assign the binding.
        MIR_reg_t r = em_new_reg(&c->em, "read", em_mir_type_for_rep(b->rep));
        move(c, r, reg(c, b->reg), em_mir_type_for_rep(b->rep));
        if (b->rep == VALUE_REP_ITEM) {
            int home = em_scalar_home_new(&c->em);
            r = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_DYNAMIC, r,
                em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home)));
        }
        return value(c, r, b->contract->type_id, b->rep, b->contract, node);
    }
    case AST_NODE_BINARY: return binary(c, (AstBinaryNode*)node);
    case AST_NODE_CALL_EXPR: return call(c, (AstCallNode*)node);
    case AST_NODE_UNARY: {
        AstUnaryNode* n = (AstUnaryNode*)node;
        MirValue v = expression(c, n->operand);
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
            em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_DNEG, reg(c, r), reg(c, v.reg)));
            return value(c, r, LMD_TYPE_FLOAT, VALUE_REP_F64, node->type, node);
        }
        v = convert(c, v, VALUE_REP_ITEM);
        return value(c, em_call_1(&c->em, n->op == OPERATOR_NEG ? "fn_neg" : "fn_pos", MIR_T_I64,
            MIR_T_I64, reg(c, v.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    case AST_NODE_EXPR_STMT: return expression(c, ((AstExprStmtNode*)node)->expression);
    case AST_NODE_CONTENT: return statements(c, ((AstListNode*)node)->item);
    case AST_NODE_BLOCK: return statements(c, ((AstBlockNode*)node)->statements);
    case AST_SCRIPT: return statements(c, ((AstScript*)node)->child);
    case AST_NODE_FUNC: case AST_NODE_PROC: return null_value(c);
    case AST_NODE_LET_STAM: case AST_NODE_VAR_STAM:
        for (AstNode* d = ((AstVarDeclNode*)node)->declarations; d; d = d->next) expression(c, d);
        return null_value(c);
    case AST_NODE_VARIABLE_DECLARATOR: {
        AstDeclaratorNode* n = (AstDeclaratorNode*)node;
        MvpBinding* b = mvp_binding(c->module, n->entry);
        if (!b || !b->reg) {
            mvp_reject(c->module, node, "declaration requires a local scalar binding"); return null_value(c);
        }
        b->scope = c->scope;
        store_binding(c, b, expression(c, n->init)); return null_value(c);
    }
    case AST_NODE_ASSIGN_STAM: {
        AstAssignStamNode* n = (AstAssignStamNode*)node;
        MvpBinding* b = mvp_binding(c->module, n->target_entry);
        if (!b || b->owner != c->function->id) {
            mvp_reject(c->module, node, "assignment requires a local scalar binding"); return null_value(c);
        }
        store_binding(c, b, expression(c, n->value)); return null_value(c);
    }
    case AST_NODE_IF_EXPR: {
        AstIfNode* n = (AstIfNode*)node;
        MIR_label_t otherwise = label(c), done = label(c);
        bool bool_condition = mvp_value_contract(c->module, n->cond)->type_id == LMD_TYPE_BOOL;
        MirValue condition = bool_condition ? MirValue{} : expression(c, n->cond);
        bool error_condition = !bool_condition && condition.semantic_type == LMD_TYPE_ANY;
        ValueRep rep = error_condition || ast_expr_may_defect(node, 0) ? VALUE_REP_ITEM : mvp_rep(node->type);
        MIR_reg_t r = em_new_reg(&c->em, "if_result", em_mir_type_for_rep(rep));
        if (error_condition) {
            condition = convert(c, condition, VALUE_REP_ITEM); move(c, r, reg(c, condition.reg));
            branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_URSH, reg(c, condition.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        }
        if (bool_condition) boolean_branch(c, n->cond, otherwise, false);
        else branch(c, MIR_BEQ, otherwise, reg(c, truth(c, condition)), imm(c, 0));
        MirValue yes = convert(c, expression(c, n->then), rep);
        move(c, r, reg(c, yes.reg), yes.mir_type); jump(c, done); mark(c, otherwise);
        MirValue no = convert(c, expression(c, n->otherwise), rep);
        move(c, r, reg(c, no.reg), no.mir_type); mark(c, done);
        return value(c, r, rep == VALUE_REP_ITEM ? LMD_TYPE_ANY : node->type->type_id,
            rep, rep == VALUE_REP_ITEM ? &TYPE_ANY : node->type, node);
    }
    case AST_NODE_LOOP: {
        AstLoopControlNode* n = (AstLoopControlNode*)node;
        MIR_label_t fast_done = countdown_loop(c, n);
        MvpControl loop = {c->control, label(c), label(c)};
        MIR_label_t body = label(c);
        c->control = &loop;
        // test before the first iteration, then use a conditional backedge; continue still reaches the test.
        jump(c, loop.next); mark(c, body);
        expression(c, n->body); mark(c, loop.next);
        if (mvp_value_contract(c->module, n->cond)->type_id == LMD_TYPE_ANY)
            mvp_reject(c->module, n->cond, "fallible while conditions are not admitted yet");
        else boolean_branch(c, n->cond, body, true);
        mark(c, loop.done); c->control = loop.parent;
        if (fast_done) mark(c, fast_done);
        return null_value(c);
    }
    case AST_NODE_RETURN_STAM: {
        AstNode* result = ((AstReturnNode*)node)->value;
        return_value(c, expression(c, result), result); return null_value(c);
    }
    case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM:
        if (!c->control) mvp_reject(c->module, node, "loop transfer outside a loop");
        else jump(c, node->node_type == AST_NODE_BREAK_STAM ? c->control->done : c->control->next);
        return null_value(c);
    default: mvp_reject(c->module, node, "unsupported scalar expression"); return null_value(c);
    }
}
static MirValue statements(MvpCompiler* c, AstNode* node) {
    MvpScope scope = {c->scope, {}}; c->scope = &scope;
    MirValue result = null_value(c);
    for (; node && !c->module->failed; node = node->next) {
        MirValue current = expression(c, node);
        if (!is_declaration_node(node)) result = current;
    }
    if (scope.failure.done) {
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

bool mvp_emit_function(LambdaMvpModule* m, MvpFunction* fn) {
    MvpCompiler c = {}; c.module = m; c.function = fn;
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
                 b->entry->node->node_type != AST_NODE_VARIABLE_DECLARATOR)) continue;
        b->reg = em_new_reg(&c.em, "binding", em_mir_type_for_rep(b->rep));
        value(&c, b->reg, b->contract->type_id, b->rep, b->contract);
    }
    if (fn->ast) {
        int i = 0;
        for (AstNamedNode* param = fn->ast->param; param; param = (AstNamedNode*)param->next, i++) {
            MvpBinding* b = mvp_binding(m, param->entry);
            if (!b || !b->reg) { mvp_reject(m, param, "parameter has no indexed binding"); break; }
            move(&c, b->reg, reg(&c, MIR_reg(m->ctx, args[i + 1].name, c.em.func)), args[i + 1].type);
        }
        MirValue result = expression(&c, fn->ast->body);
        if (fn->ast->node_type != AST_NODE_PROC || !mvp_proc_has_final_return(fn->ast) || result.semantic_type == LMD_TYPE_ANY)
            return_value(&c, fn->ast->node_type == AST_NODE_PROC && result.semantic_type != LMD_TYPE_ANY ? null_value(&c) : result, NULL);
    } else {
        MirValue result = expression(&c, m->tp->ast_root);
        if (m->main && m->main->ast->node_type == AST_NODE_PROC) {
            result = convert(&c, result, VALUE_REP_ITEM);
            MIR_reg_t output = em_new_reg(&c.em, "module_result", MIR_T_I64);
            move(&c, output, reg(&c, result.reg));
            MIR_label_t done = label(&c);
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
    if (!em_finalize_semantic_root_write_back(&c.em, f->root_base, f->anchor, false, 0,
            &f->gc_candidates, &f->gc_candidate_count, &f->gc_candidate_capacity,
            &f->gc_candidate_by_reg, &f->gc_candidate_by_reg_capacity,
            f->gc_call_sites, f->gc_call_site_count, &roots, fn->name))
        mvp_reject(m, fn->ast, "precise root planning failed");
    f->root_slot_count = roots.stable_slots + roots.scratch_slots; f->root_store_count = roots.inserted_stores;
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
