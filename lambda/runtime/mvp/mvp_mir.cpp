#include "mvp_internal.hpp"
#include "../lambda-number-types.hpp"
#include "../type_contract.hpp"
#include "../mir_loop_analysis.hpp"
#include "../mir_bitwise.hpp"
#include "../mir_container_access.hpp"
#include "../mir_cow_analysis.hpp"
#include "../mir_path.hpp"
#include "../mir_function_value.hpp"
#include "../output_demand.hpp"
#include "lambda/io/mark_builder.hpp"
#include "lambda/io/mark_output_builder.hpp"
#include "lib/math_checked.hpp"

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
static const char* cow_import(const char* ordinary, const char* profiled) {
    // select diagnostic attribution at compile time; ordinary MIR keeps its fast path.
    return cow_profile_sites_enabled() ? profiled : ordinary;
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

static MirPipeContext context_save(MvpCompiler* c) {
    return {c->current_item != 0, c->current_item, c->current_index,
        c->current_parent, c->current_root};
}

static void context_restore(MvpCompiler* c, MirPipeContext saved) {
    c->current_item = saved.item; c->current_index = saved.index;
    c->current_parent = saved.parent; c->current_root = saved.root;
}

static MirValue value(MvpCompiler* c, MIR_reg_t r, TypeId type, ValueRep rep,
        Type* contract = NULL, AstNode* source = NULL) {
    // error-bearing unions use the generic outcome consumers, not the
    // semantic category of the type-expression object that describes them.
    if (rep == VALUE_REP_ITEM && contract && lambda_type_accepts_error(contract) && type != LMD_TYPE_ERROR)
        type = LMD_TYPE_ANY;
    if (rep == VALUE_REP_ITEM && lambda_type_contract_has_binder(contract, true)) type = LMD_TYPE_ANY;
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

static MirValue read_binder_type(MvpCompiler* c, NameEntry* entry, AstNode* source = NULL) {
    TypeFunc* signature = c->function->ast ? (TypeFunc*)c->function->ast->type : NULL;
    // optional normalization can copy the site's descriptor. Lexical ownership,
    // not pointer equality, identifies this activation's stable slot.
    if (c->binder_env && signature && entry->binder_slot < signature->binder_count &&
            ast_scope_function(entry->scope) == c->function->ast)
        return value(c, em_binder_type_value(&c->em, c->binder_env, entry->binder_slot, entry->binder->bound),
            LMD_TYPE_TYPE, VALUE_REP_ITEM, &TYPE_TYPE, source);
    int index = 0;
    for (FnCapture* capture = c->function->ast ? c->function->ast->captures : NULL;
            capture; capture = capture->next, index++) {
        if (capture->entry != entry) continue;
        return value(c, em_capture_slot_read(&c->em, c->environment,
            ast_function_capture_count(c->function->ast), index),
            LMD_TYPE_TYPE, VALUE_REP_ITEM, &TYPE_TYPE, source);
    }
    mvp_reject(c->module, source, "binder type requires its declaring activation or capture");
    return null_value(c);
}
static Type* normal_join_contract(MvpCompiler* c, MirValue left, MirValue right) {
    Type* a = lambda_type_accepts_error(left.semantic_contract)
        ? left.admitted_success_contract : left.semantic_contract;
    Type* b = lambda_type_accepts_error(right.semantic_contract)
        ? right.admitted_success_contract : right.semantic_contract;
    // recovering a nullable comparison with false still selects only bool
    // or null, even if union normalization retains the false literal arm.
    if (a && b && mvp_boundary_proven(a, c->module->nullable_bool) &&
            mvp_boundary_proven(b, c->module->nullable_bool))
        return lambda_type_accepts_null(a) || lambda_type_accepts_null(b)
            ? c->module->nullable_bool : &TYPE_BOOL;
    return a && b ? lambda_type_union_normalized(c->module->tp->pool, a, b) : NULL;
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
    if (!nullable(v)) return;
    if (v.rep == VALUE_REP_F64)
        branch(c, MIR_BNE, target, reg(c, em_float_lane_null_test(&c->em, v.reg)), imm(c, 0));
    else branch(c, MIR_BEQ, target, reg(c, v.reg), imm(c,
        v.rep == VALUE_REP_RAW_GC_POINTER ? 0 : v.rep == VALUE_REP_I64 ? BOOL_ERROR : ITEM_NULL));
}
static MirValue convert(void* owner, MirValue v, ValueRep to) {
    MvpCompiler* c = (MvpCompiler*)owner;
    if (v.maybe_pending) v = em_materialize_pending_value(&c->em, v, MIR_PENDING_REASON_REP_CONVERSION);
    if (v.rep == to) return v;
    bool optional_pointer = false;
    Type* pointer_contract = lambda_type_nullable_lane_base(v.semantic_contract, &optional_pointer);
    TypeId pointer_type = pointer_contract ? pointer_contract->type_id : v.semantic_type;
    bool container_pointer = is_container_type_id(pointer_type) ||
        lambda_type_nonnull_map_contract(v.semantic_contract) || mvp_array_contract(pointer_contract) ||
        pointer_type == LMD_TYPE_FUNC || pointer_type == LMD_TYPE_TYPE || pointer_type == LMD_TYPE_PATH;
    bool pointer_lane = container_pointer || lambda_type_id_has_pointer_lane(pointer_type);
    MIR_reg_t r = 0;
    if (v.semantic_type == LMD_TYPE_NULL) {
        // null has a distinct sentinel in each native nullable lane (D2.3.5).
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
        } else if ((v.rep == VALUE_REP_I64 || v.rep == VALUE_REP_U64) &&
                (pointer_type == LMD_TYPE_INT64 || pointer_type == LMD_TYPE_UINT64)) {
            AstNode* source = ast_unwrap_primary_to_leaf((AstNode*)v.provenance_node);
            if (source && source->node_type == AST_NODE_PRIMARY && source->type &&
                    source->type->is_literal && source->type->type_id == pointer_type) {
                // literal bits already have an immutable Input owner. Reuse
                // that owner; publication still adopts destination storage.
                MIR_reg_t home = em_load_const(&c->em, ((TypeConst*)source->type)->const_index, MIR_T_P);
                r = operation(c, MIR_OR, reg(c, home), imm(c, (uint64_t)pointer_type << 56));
            } else {
                int home = em_scalar_home_new(&c->em);
                r = em_store_scalar_home(&c->em, v.reg,
                    em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home)), MIR_T_I64, pointer_type);
            }
        } else if (v.rep == VALUE_REP_I64 && v.semantic_type == LMD_TYPE_BOOL) {
            r = operation(c, MIR_OR, reg(c, v.reg), imm(c, (uint64_t)LMD_TYPE_BOOL << 56));
            if (nullable(v)) {
                MIR_label_t done = label(c);
                branch(c, MIR_BNE, done, reg(c, v.reg), imm(c, BOOL_ERROR));
                move(c, r, imm(c, ITEM_NULL)); mark(c, done);
            }
        } else if (v.rep == VALUE_REP_RAW_GC_POINTER && pointer_lane) {
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
        bool optional = nullable(v);
        MIR_label_t absent = optional ? label(c) : NULL, joined = optional ? label(c) : NULL;
        // preserve absence when admitting an optional integer into a float lane.
        if (optional) branch_null(c, v, absent);
        if (mvp_int_range(c->module, (AstNode*)v.provenance_node).finite) {
            em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, r), reg(c, v.reg)));
        } else {
            MIR_label_t slow = label(c), done = label(c);
            em_branch_int53_outside(&c->em, v.reg, slow);
            em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, r), reg(c, v.reg)));
            jump(c, done); mark(c, slow);
            move(c, r, reg(c, em_call_1(&c->em, "lambda_int_lane_to_double_c", MIR_T_D,
                MIR_T_I64, reg(c, v.reg), true)), MIR_T_D);
            mark(c, done);
        }
        if (optional) {
            jump(c, joined); mark(c, absent);
            move(c, r, reg(c, null_float(c)), MIR_T_D); mark(c, joined);
        }
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
    } else if ((to == VALUE_REP_I64 || to == VALUE_REP_U64) && v.rep == VALUE_REP_ITEM &&
            (pointer_type == LMD_TYPE_INT64 || pointer_type == LMD_TYPE_UINT64) && !nullable(v)) {
        MIR_reg_t pointer = operation(c, MIR_AND, reg(c, v.reg), imm(c, ITEM_INT_PAYLOAD_MASK));
        r = em_load_at(&c->em, pointer, 0, MIR_T_I64, "wide_integer");
    } else if (to == VALUE_REP_I64 && v.rep == VALUE_REP_ITEM && pointer_type == LMD_TYPE_BOOL) {
        r = operation(c, MIR_AND, reg(c, v.reg), imm(c, 1));
        if (nullable(v)) {
            MIR_label_t done = label(c);
            branch(c, MIR_BNE, done, reg(c, v.reg), imm(c, ITEM_NULL));
            move(c, r, imm(c, BOOL_ERROR)); mark(c, done);
        }
    } else if (to == VALUE_REP_RAW_GC_POINTER && v.rep == VALUE_REP_ITEM && pointer_lane) {
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
static MirValue expression(MvpCompiler* c, AstNode* node, bool discard = false, bool tail = false,
    bool item_position = false, MIR_reg_t output_destination = 0);
static MirValue output_expression(MvpCompiler* c, AstNode* node, MIR_reg_t destination);
static MirValue mvp_lower_value(void* owner, AstNode* node, uint32_t demand) {
    return expression((MvpCompiler*)owner, node, (demand & MIR_VALUE_DISCARD) != 0);
}
static MirValue read_binding(MvpCompiler*, MvpBinding*, AstNode*, bool);
static MirValue literal(MvpCompiler* c, AstPrimaryNode* node);
static MirValue array_literal(MvpCompiler* c, AstArrayNode* node, Type* destination = NULL);
static MirValue map_literal(MvpCompiler* c, AstMapNode* node, TypeMap* destination = NULL);
static MirValue string_concat(MvpCompiler* c, AstNode* node, MvpBinding* owner = NULL,
    MvpBinding* replacement = NULL, AstNode* initializer = NULL);
enum MvpSequenceMode { MVP_BLOCK_RESULTS, MVP_LIST_RESULTS, MVP_CONTENT_RESULTS };
static MirValue statements(MvpCompiler* c, AstNode* node, bool discard = false, bool tail = false,
    bool item_position = false, MvpSequenceMode mode = MVP_BLOCK_RESULTS, AstNode* declarations = NULL,
    MIR_reg_t output_destination = 0);
static void array_iteration(MvpCompiler* c, AstNode* body);
static MIR_label_t dense_call_loop(MvpCompiler* c, AstLoopControlNode* loop);
static MIR_label_t dense_array_loop(MvpCompiler* c, AstNode* loop);
static MIR_label_t strided_array_loop(MvpCompiler* c, AstLoopControlNode* loop);
static bool string_comparison_inputs(MvpCompiler* c, AstBinaryNode* node, MirValue* left, MirValue* right);
static void narrow_dense_locals(MvpCompiler* c, AstNodeId root, bool outcomes = true);
static void receive_parameter(MvpCompiler* c, MvpBinding* binding, MIR_reg_t input, int position,
    Type* incoming_contract = NULL);
static bool guarded_scalar_body(MvpCompiler* c);
static MirValue direct_call(MvpCompiler* c, MvpFunction* f, AstCallNode* call,
    bool discard = false, MvpBinding* destination = NULL, bool propagate = false,
    MIR_reg_t output_destination = 0);
static void return_value(MvpCompiler* c, MirValue v, AstNode* node, bool error_exit = false);
static MirValue propagated_value(MvpCompiler* c, MirValue v, AstNode* node);

static MIR_label_t array_sum_loop(MvpCompiler* c, AstLoopControlNode* node) {
    MvpArraySumPlan plan = {};
    if (!mvp_array_sum_plan(c->module, node, &plan) || !plan.array->array_native) return NULL;
    MIR_reg_t index = plan.counter->reg;
    bool boxed_sum = plan.sum->rep == VALUE_REP_ITEM;
    // shared homes need per-store publication; keep their ordinary assignment path.
    if (boxed_sum && (plan.sum->owner != c->function->id || plan.sum->var_home ||
            plan.sum->capture_home || plan.sum->state_name || plan.sum->module_ref || plan.sum->module_slot)) return NULL;
    MIR_reg_t limit = expression(c, plan.limit).reg;
    MIR_label_t generic = label(c), done = label(c), publish = label(c), remainder = label(c), body = label(c);
    branch(c, MIR_BEQ, generic, reg(c, plan.array->array_native), imm(c, 0));
    // admission errors stay boxed; only exact integer zero enters the finite reduction (S7.7.4v2).
    branch(c, MIR_BNE, generic, reg(c, plan.sum->reg),
        imm(c, boxed_sum ? (uint64_t)LMD_TYPE_INT << 56 : 0));
    branch(c, MIR_BLT, generic, reg(c, index), imm(c, 0));
    branch(c, MIR_UBGT, generic, reg(c, limit), imm(c, plan.max_count - plan.inclusive));
    if (plan.inclusive) limit = operation(c, MIR_ADD, reg(c, limit), imm(c, 1));
    branch(c, MIR_UBGT, generic, reg(c, limit), reg(c, plan.array->array_length));
    branch(c, MIR_BGE, done, reg(c, index), reg(c, limit));
    MIR_reg_t sum = boxed_sum ? constant(c, 0) : plan.sum->reg;
    // the complete element domain and trip bound prove every partial/merged sum
    // finite. This allocation-free region can retain its buffer (D5.4.4, S4.1.2).
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
    branch(c, MIR_BGE, publish, reg(c, index), reg(c, limit));
    MIR_label_t tail = label(c); mark(c, tail);
    MIR_reg_t term = em_load_at(&c->em, em_element_address(&c->em, items, index, width), 0, storage, "sum_tail");
    move(c, sum, reg(c, operation(c, MIR_ADD, reg(c, sum), reg(c, term))));
    move(c, index, reg(c, operation(c, MIR_ADD, reg(c, index), imm(c, 1))));
    branch(c, MIR_BLT, tail, reg(c, index), reg(c, limit));
    mark(c, publish);
    if (boxed_sum) move(c, plan.sum->reg, reg(c, em_box_finite_int_item(&c->em, sum)));
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

static void stage_error(MvpCompiler* c, MIR_reg_t error);

static bool has_open_cleanup(MvpCompiler* c) {
    for (MvpCleanup* cleanup = c->cleanups; cleanup; cleanup = cleanup->parent)
        if (cleanup->kind == MVP_CLEANUP_TRANSACTION) return true;
    return false;
}

static void cleanup_to(MvpCompiler* c, MvpCleanup* base, bool error_exit) {
    for (MvpCleanup* cleanup = c->cleanups; cleanup && cleanup != base; cleanup = cleanup->parent) {
        if (cleanup->kind == MVP_CLEANUP_TASK) {
            em_task_scope_finish(&c->em, cleanup->record, imm(c, error_exit ? 1 : 0));
            continue;
        }
        if (cleanup->kind == MVP_CLEANUP_RECOVERY) {
            (void)em_call_1(&c->em, "lambda_recovery_frame_end", MIR_T_I64,
                MIR_T_P, reg(c, cleanup->record), true);
            continue;
        }
        if (cleanup->kind == MVP_CLEANUP_KEYS) {
            em_call_void_1(&c->em, "symbol_key_list_free", MIR_T_P, reg(c, cleanup->record), true);
            continue;
        }
        MIR_reg_t closed = em_call_1(&c->em, "fn_open_end", MIR_T_I64,
            MIR_T_I64, imm(c, error_exit ? 1 : 0), true);
        value(c, closed, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
        if (error_exit) continue;
        MIR_label_t committed = label(c);
        branch(c, MIR_BNE, committed,
            reg(c, operation(c, MIR_URSH, reg(c, closed), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        // the failed commit already closed this record. Abort only its
        // ancestors, preserving the compiler chain for the successful edge.
        MvpCleanup* saved = c->cleanups;
        c->cleanups = cleanup->parent;
        stage_error(c, closed);
        c->cleanups = saved;
        mark(c, committed);
    }
}

static MvpControl loop_control(MvpCompiler* c) {
    return {c->control, label(c), label(c), c->cleanups, c->cleanups};
}

static void stage_error(MvpCompiler* c, MIR_reg_t error) {
    if (c->output_failure) {
        MvpErrorTarget* failure = c->output_failure;
        cleanup_to(c, failure->cleanup_base, true);
        move(c, failure->error, reg(c, error)); jump(c, failure->done);
        return;
    }
    cleanup_to(c, NULL, true);
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
    if (failure->unwinds) cleanup_to(c, failure->cleanup_base, true);
    move(c, failure->error, reg(c, error)); jump(c, failure->done);
}

static void check_item_result(MvpCompiler* c, MIR_reg_t result, MvpErrorTarget* failure,
        MvpBinding* binding = NULL, AstNode* source = NULL) {
    value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    MIR_label_t accepted = label(c);
    branch(c, MIR_BNE, accepted,
        reg(c, operation(c, MIR_URSH, reg(c, result), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
    if (binding) {
        AstListNode* block = ast_binding_scope_block(&c->module->tp->ast_index, binding->entry);
        if (!block && c->function->ast && c->function->ast->body &&
                c->function->ast->body->node_type == AST_NODE_CONTENT)
            block = (AstListNode*)c->function->ast->body;
        result = em_call_4(&c->em, "lambda_report_boundary_failure", MIR_T_I64,
            MIR_T_I64, reg(c, result), MIR_T_P, imm(c, (intptr_t)binding->entry->name->chars),
            MIR_T_I64, imm(c, block ? ast_block_tail_count(block, source) : 0),
            MIR_T_I64, imm(c, 1), true);
    }
    fail_boundary(c, result, failure);
    mark(c, accepted);
}

static void mvp_check_item_result(void* owner, MIR_reg_t result) {
    MvpCompiler* c = (MvpCompiler*)owner;
    // stores abandon only their containing block; an explicit return keeps
    // its callable exit even inside a nested block (S7.7.6v2, S12.4.4v4).
    check_item_result(c, result, c->scope && c->scope->parent ? &c->scope->failure : NULL);
}

static void check_store_result(MvpCompiler* c, MIR_reg_t result,
        MvpBinding* binding, AstNode* source) {
    // store containment follows the receiver's declaring block, not the
    // current loop body whose counter update could be skipped (S7.7.6v2).
    MvpScope* scope = binding->owner == c->function->id ? binding->scope : NULL;
    check_item_result(c, result, scope && scope->parent ? &scope->failure : NULL, binding, source);
}

static void mvp_check_task_arguments(void* owner, MIR_reg_t arguments) {
    MvpCompiler* c = (MvpCompiler*)owner;
    MIR_label_t ready = label(c);
    branch(c, MIR_BNE, ready, reg(c, arguments), imm(c, 0));
    stage_error(c, constant(c, ITEM_ERROR));
    mark(c, ready);
}

static MirValue admit(MvpCompiler* c, MirValue v, Type* contract, MvpErrorTarget* failure = NULL,
        const char* site = "MVP scalar boundary", bool boxed_outcome = false,
        const AstNode* source = NULL) {
    if (!source) source = (AstNode*)v.provenance_node;
    contract = type_field_unwrap_simple_decl(contract);
    if (!contract || contract == &TYPE_ANY) return convert(c, v, VALUE_REP_ITEM);
    if (contract == &TYPE_ANY_NO_ERROR) {
        v = convert(c, v, VALUE_REP_ITEM);
        // the implicit open boundary only excludes Error (S11.4.3). Preserve
        // its payload and existing call/outcome target without a validator.
        if (!v.semantic_contract || lambda_type_accepts_error(v.semantic_contract))
            check_item_result(c, v.reg, failure);
        // the tag check discharges a producer's Error arm; keep its proved
        // success domain when the transport itself was an erased Item.
        Type* success = lambda_type_remove_error(c->module->tp->pool,
            v.admitted_success_contract ? v.admitted_success_contract : v.semantic_contract);
        v.semantic_contract = success && success != &TYPE_NONE ? success : &TYPE_ANY_NO_ERROR;
        v.semantic_type = v.semantic_contract->type_id;
        return v;
    }
    bool dependent = lambda_type_contract_has_binder(contract, true);
    // an Error-only boundary can admit an Error as its successful value;
    // the outcome tag alone cannot distinguish rejection (S11.4.2v3).
    bool error_only = lambda_type_accepts_error(contract) &&
        !lambda_type_normal_admission_contract(c->module->tp->pool, contract);
    if (dependent || error_only) {
        if (dependent && !c->binder_env && !c->captured_types) {
            mvp_reject(c->module, (AstNode*)v.provenance_node, "dependent boundary requires its lexical binder environment");
            return null_value(c);
        }
        v = convert(c, v, VALUE_REP_ITEM);
        MIR_reg_t type = em_load_const(&c->em, em_add_const(&c->em, contract), MIR_T_P);
        if (!c->admission_status) {
            // a checked assignment may run in a loop; allocate one native
            // status cell at entry rather than growing the stack per check.
            c->admission_status = em_fixed_stack_storage(&c->em, "admission_status", sizeof(uint64_t));
        }
        MIR_op_t local = c->binder_env ? reg(c, c->binder_env) : imm(c, 0);
        MIR_reg_t checked;
        if (c->captured_types) {
            MIR_reg_t references = em_load_const(&c->em,
                em_add_const(&c->em, c->function->captured_type_references), MIR_T_P);
            checked = em_call_8(&c->em, "lambda_type_check_captured_env_status", MIR_T_I64,
                MIR_T_I64, reg(c, v.reg), MIR_T_P, reg(c, type), MIR_T_P, local,
                MIR_T_P, reg(c, references), MIR_T_P, reg(c, c->captured_types),
                MIR_T_I64, imm(c, c->function->captured_type_count),
                MIR_T_P, imm(c, (intptr_t)site), MIR_T_P, reg(c, c->admission_status), true);
        } else checked = em_call_5(&c->em, "lambda_type_check_env_status", MIR_T_I64,
            MIR_T_I64, reg(c, v.reg), MIR_T_P, reg(c, type), MIR_T_P, local,
            MIR_T_P, imm(c, (intptr_t)site), MIR_T_P, reg(c, c->admission_status), true);
        MIR_reg_t accepted = em_load_at(&c->em, c->admission_status, 0, MIR_T_U8, "admitted");
        MIR_label_t ready = label(c);
        branch(c, MIR_BNE, ready, reg(c, accepted), imm(c, 0));
        fail_boundary(c, checked, failure); mark(c, ready);
        return value(c, checked, LMD_TYPE_ANY, VALUE_REP_ITEM, contract, (AstNode*)v.provenance_node);
    }
    if (lambda_type_accepts_error(contract)) {
        // an admitted Error needs no normal-arm conversion or validator (S11.4.2v3).
        if (mvp_boundary_proven(v.semantic_contract, &TYPE_ERROR) &&
                mvp_boundary_proven(v.semantic_contract, contract))
            return convert(c, v, VALUE_REP_ITEM);
        Type* normal = lambda_type_normal_admission_contract(c->module->tp->pool, contract);
        if (normal && normal != contract) {
            MIR_label_t done = NULL;
            MIR_reg_t outcome = 0;
            if (v.rep == VALUE_REP_ITEM && lambda_type_accepts_error(v.semantic_contract)) {
                done = label(c); outcome = em_new_reg(&c->em, "admitted_error_union", MIR_T_I64);
                move(c, outcome, reg(c, v.reg));
                branch(c, MIR_BEQ, done,
                    reg(c, operation(c, MIR_URSH, reg(c, v.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
            }
            // membership can be proven while normal admission still changes
            // the value's numeric lane or record layout (S11.4.6v4/D4.2.5).
            MirValue admitted = convert(c, admit(c, v, normal, failure, site, false, source), VALUE_REP_ITEM);
            if (done) {
                move(c, outcome, reg(c, admitted.reg)); mark(c, done);
                admitted.reg = outcome;
            }
            return value(c, admitted.reg, contract->type_id, VALUE_REP_ITEM, contract,
                (AstNode*)v.provenance_node);
        }
    }
    // a static success contract cannot erase a boxed error join (D2.5.2, S11.4.1v3).
    bool proven = mvp_admission_proven(c->module, v.semantic_contract, contract);
    // an open receiving contract does not erase the actual's private domain;
    // a closed callee can still need its proven array/string carrier.
    if (proven && mvp_open_value_contract(contract)) return convert(c, v, VALUE_REP_ITEM);
    Type* scalar_success = mvp_scalar_success_contract(c->module, v.semantic_contract);
    // a nullable native lane needs ordinary admission only on its absence arm.
    MIR_label_t native_admitted = NULL;
    MIR_reg_t native_result = 0;
    bool optional_pointer = false;
    Type* pointer_base = lambda_type_nullable_lane_base(v.semantic_contract, &optional_pointer);
    if (!proven && nullable(v) && ((v.rep == VALUE_REP_INT_LANE && contract == &TYPE_INT) ||
            (v.rep == VALUE_REP_F64 && contract == &TYPE_FLOAT) ||
            (v.rep == VALUE_REP_I64 && contract == &TYPE_BOOL) ||
            (v.rep == VALUE_REP_RAW_GC_POINTER && optional_pointer && mvp_admission_proven(c->module, pointer_base, contract)))) {
        ValueRep target = boxed_outcome ? VALUE_REP_ITEM : mvp_rep(contract);
        native_admitted = label(c); native_result = em_new_reg(&c->em, "admitted_scalar", em_mir_type_for_rep(target));
        MIR_label_t checked = label(c);
        // each native nullable lane retains its own absence marker. Only
        // the present arm avoids the destination validator (S11.4.1v5).
        branch_null(c, v, checked);
        MirValue present = value(c, v.reg, contract->type_id, v.rep, contract, (AstNode*)v.provenance_node);
        present = convert(c, present, target);
        move(c, native_result, reg(c, present.reg), present.mir_type);
        jump(c, native_admitted); mark(c, checked);
    }
    if (!proven && v.rep == VALUE_REP_INT_LANE && contract->type_id == LMD_TYPE_NUM_SIZED &&
            !contract->is_literal && !contract->is_const && type_num_sized_kind(contract) == NUM_UINT32) {
        native_admitted = label(c); native_result = em_new_reg(&c->em, "admitted_u32", MIR_T_I64);
        MIR_label_t checked = label(c);
        move(c, native_result, reg(c, em_admit_int_lane_u32(&c->em, v.reg, checked)));
        jump(c, native_admitted); mark(c, checked);
    }
    Type* outcome_success = v.admitted_success_contract ? v.admitted_success_contract :
        scalar_success ? scalar_success : v.semantic_contract;
    bool optional_success = false;
    Type* success_base = lambda_type_nullable_lane_base(outcome_success, &optional_success);
    if (!proven && v.rep == VALUE_REP_ITEM && optional_success && success_base &&
            mvp_admission_proven(c->module, success_base, contract)) {
        ValueRep target = boxed_outcome ? VALUE_REP_ITEM : mvp_rep(contract);
        native_admitted = label(c);
        native_result = em_new_reg(&c->em, "present_outcome", em_mir_type_for_rep(target));
        MIR_label_t checked = label(c);
        // a nullable producer proves only the non-null success arm. Errors and
        // absence retain the ordinary boundary diagnostic (S7.7.3).
        branch(c, MIR_BEQ, checked,
            reg(c, operation(c, MIR_URSH, reg(c, v.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        branch(c, MIR_BEQ, checked, reg(c, v.reg), imm(c, ITEM_NULL));
        // the present scalar may need a double/pointer lane; an Item register
        // cannot also be the native join destination (D2.3.5).
        MirValue present = convert(c, value(c, v.reg, success_base->type_id,
            VALUE_REP_ITEM, success_base), target);
        move(c, native_result, reg(c, present.reg), present.mir_type);
        jump(c, native_admitted);
        mark(c, checked);
    }
    if (!proven) {
        // typed constructors already admitted their success value; primitive
        // array invariance must use the same proof as ordinary boundaries.
        bool success_proven = mvp_admission_proven(c->module, v.admitted_success_contract, contract) ||
            mvp_admission_proven(c->module, scalar_success, contract);
        v = convert(c, v, VALUE_REP_ITEM);
        MIR_reg_t checked = v.reg;
        if (!success_proven) {
            bool optional = false;
            Type* base = lambda_type_nullable_lane_base(contract, &optional);
            bool scalar_guard = base == &TYPE_INT || base == &TYPE_BOOL ||
                base == &TYPE_STRING || base == &TYPE_FLOAT;
            LambdaNumericKind sized_kind = lambda_numeric_kind_from_type(base);
            int sized_bits = lambda_numeric_sized_bits(sized_kind);
            bool sized_guard = base && base->type_id == LMD_TYPE_NUM_SIZED &&
                !base->is_literal && !base->is_const && lambda_numeric_is_sized_integer(sized_kind) && sized_bits <= 32;
            TypeMap* record = (TypeMap*)lambda_type_nonnull_map_contract(contract);
            bool record_guard = record && record->is_trusted_contract &&
                !lambda_type_contract_has_binder(contract, true);
            bool broad_guard = base == &TYPE_ARRAY || base == &TYPE_MAP;
            Type* element = lambda_array_contract_element(contract);
            bool array_guard = element &&
                lambda_array_contract_is_plain(contract) && !lambda_type_counts_array_length(contract) &&
                !lambda_type_contract_has_binder(contract, true);
            bool guarded = scalar_guard || sized_guard || record_guard || broad_guard || array_guard;
            MIR_label_t done = guarded ? label(c) : NULL;
            MIR_reg_t joined = guarded ? em_new_reg(&c->em, "value_admission", MIR_T_I64) : 0;
            if (guarded) {
                // exact scalar membership cannot convert or invoke a constraint;
                // trusted record layouts prove their reified prefix (D4.2.5).
                move(c, joined, reg(c, v.reg));
                if (optional) branch(c, MIR_BEQ, done, reg(c, v.reg), imm(c, ITEM_NULL));
                if (record_guard || broad_guard || array_guard) {
                    MIR_label_t miss = label(c);
                    if (array_guard) {
                        ArrayNumElemType lane;
                        bool numeric = lambda_array_num_elem_type_for_contract(element, &lane);
                        MIR_reg_t array = em_guard_container(&c->em, v.reg,
                            numeric ? LMD_TYPE_ARRAY_NUM : LMD_TYPE_ARRAY, miss);
                        if (numeric) em_array_num_elem_guard(&c->em, array, lane, miss);
                        // an exact error-free primitive certificate already
                        // proves this unchanged rank/lane domain (D4.3.3).
                        em_array_rep_cert_guard(&c->em, array, contract, miss, numeric);
                    } else if (record_guard)
                        em_guard_map_shape(&c->em, v.reg, constant(c, (intptr_t)record), miss, false, true);
                    else em_guard_broad_container(&c->em, v.reg, base->type_id, miss);
                    jump(c, done); mark(c, miss);
                } else if (sized_guard) {
                    uint64_t mask = (UINT64_C(1) << sized_bits) - 1;
                    uint64_t prefix = ((uint64_t)LMD_TYPE_NUM_SIZED << 56) | ((uint64_t)type_num_sized_kind(base) << 48);
                    // canonical payloads retain every reserved-bit check.
                    branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_AND, reg(c, v.reg), imm(c, ~mask))), imm(c, prefix));
                } else {
                    MIR_reg_t accepted = em_exact_item_type_test(&c->em, v.reg, base->type_id);
                    // a dynamic int satisfies the same proven float boundary
                    // as a native int; the F64 conversion still runs below.
                    if (base == &TYPE_FLOAT && !boxed_outcome && mvp_rep(contract) == VALUE_REP_F64 &&
                            mvp_boundary_proven(&TYPE_INT, contract))
                        accepted = operation(c, MIR_OR, reg(c, accepted),
                            reg(c, em_exact_item_type_test(&c->em, v.reg, LMD_TYPE_INT)));
                    branch(c, MIR_BNE, done, reg(c, accepted), imm(c, 0));
                    if (base == &TYPE_INT && contract == &TYPE_INT && !boxed_outcome) {
                        MIR_label_t miss = label(c);
                        branch(c, MIR_BEQ, miss, reg(c, em_exact_item_type_test(&c->em, v.reg, LMD_TYPE_FLOAT)), imm(c, 0));
                        MIR_reg_t number = em_call_1(&c->em, "it2d", MIR_T_D, MIR_T_I64, reg(c, v.reg), true);
                        MIR_reg_t lane = em_double_to_int_lane(&c->em, number, false, true, miss);
                        MIR_reg_t restored = em_new_reg(&c->em, "admission_roundtrip", MIR_T_D);
                        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, restored), reg(c, lane)));
                        // the shared range guard excludes poison before D2I;
                        // only an exact round trip proves int membership.
                        branch(c, MIR_DBNE, miss, reg(c, number), reg(c, restored));
                        move(c, joined, reg(c, em_box_finite_int_item(&c->em, lane)));
                        jump(c, done); mark(c, miss);
                    }
                }
            }
            checked = em_type_boundary_check(&c->em, c->module->tp->pool,
                c->module->tp->source, c->module->tp->reference, source, v.reg, contract, site);
            if (guarded) {
                move(c, joined, reg(c, checked)); mark(c, done); checked = joined;
            }
        }
        MIR_reg_t tag = operation(c, MIR_URSH, reg(c, checked), imm(c, 56));
        MIR_label_t accepted = label(c);
        branch(c, MIR_BNE, accepted, reg(c, tag), imm(c, LMD_TYPE_ERROR));
        fail_boundary(c, checked, failure);
        mark(c, accepted);
        v = value(c, checked, contract->type_id, VALUE_REP_ITEM, contract);
    }
    ValueRep target = boxed_outcome ? VALUE_REP_ITEM : mvp_rep(contract);
    // a proven union of pointer carriers can have no single source lane;
    // the admitted receiver supplies that lane for Item transport (D2.3.5).
    if (proven && v.rep == VALUE_REP_ITEM && mvp_rep(contract) == VALUE_REP_RAW_GC_POINTER)
        v = value(c, v.reg, contract->type_id, v.rep, contract, (AstNode*)v.provenance_node);
    // boxing is transport after admission; it cannot skip a normal int-to-
    // float conversion merely because a later store can also hold Error.
    v = convert(c, v, mvp_rep(contract));
    if (boxed_outcome) v = convert(c, v, VALUE_REP_ITEM);
    if (native_admitted) {
        move(c, native_result, reg(c, v.reg), v.mir_type); mark(c, native_admitted);
        v = value(c, native_result, contract->type_id, target, contract);
    }
    Type* admitted_contract = mvp_open_value_contract(contract)
        ? lambda_type_remove_error(c->module->tp->pool, outcome_success) : contract;
    return value(c, v.reg, admitted_contract->type_id, v.rep, admitted_contract,
        (AstNode*)v.provenance_node);
}

static const char* parameter_boundary_site(MvpCompiler* c, AstNamedNode* parameter) {
    if (lambda_array_contract_canonical(mvp_contract(parameter))) return "typed array call argument";
    AstFuncNode* function = parameter->entry ? ast_scope_function(parameter->entry->scope) : NULL;
    if (!function) return "function argument";
    int position = 1;
    for (AstNamedNode* p = function->param; p && p != parameter; p = (AstNamedNode*)p->next) position++;
    StrBuf* text = strbuf_new_cap(96);
    strbuf_append_format(text, "argument %d of ", position);
    write_fn_name(text, function, NULL);
    const char* result = mir_em_persist_cstr(&c->em, text->str).str;
    strbuf_free(text);
    return result;
}

static MirValue admit_parameter_argument(MvpCompiler* c, AstNamedNode* parameter,
        MirValue input, MvpErrorTarget* failure = NULL, const AstNode* source = NULL) {
    MvpBinding* binding = mvp_binding(c->module, parameter->entry);
    // a proven private domain satisfies either open source contract. Keep
    // its carrier instead of boxing and immediately unboxing a private call.
    if (binding && binding->closed_parameter &&
            mvp_private_domain_proven(input.semantic_contract, binding->contract)) return input;
    return admit(c, input, mvp_contract(parameter), failure,
        parameter_boundary_site(c, parameter), false, source);
}

static void refresh_record(MvpCompiler* c, MvpBinding* binding) {
    TypeMap* shape = mvp_record_contract(binding->contract, false);
    // forwarding-only activations need the owner home, not another witness.
    if (!shape || (!binding->record_accessed && !binding->record_written) ||
            !binding->entry || !mvp_binding_node(binding) ||
            mvp_binding_node(binding)->node_type != AST_NODE_PARAM) return;
    AstNamedNode* parameter = (AstNamedNode*)mvp_binding_node(binding);
    bool writes = lambda_type_param(parameter->type)->is_var_param;
    // a read-only enforced prefix already proves these bytes. Writable
    // owners still need their live COW permission (D4.2.5, S11.4.10).
    bool admitted = mvp_admission_proven(c->module, binding->contract, shape);
    if (!writes && admitted) return;
    if (writes ? !mvp_parameter_storage_stable(c->module, parameter) :
            !mvp_parameter_borrowable(c->module, parameter)) return;
    if (!binding->record_readable) binding->record_readable = em_new_reg(&c->em, "record_layout", MIR_T_I64);
    if (writes && !binding->record_writable) binding->record_writable = em_new_reg(&c->em, "record_witness", MIR_T_I64);
    move(c, binding->record_readable, imm(c, 0));
    if (writes) move(c, binding->record_writable, imm(c, 0));
    MIR_label_t done = label(c);
    if (!admitted) em_guard_map_shape(&c->em, binding->reg, constant(c, (intptr_t)shape), done, true, true);
    // snapshots remove write permission without changing the admitted prefix.
    // Cache readability separately, including mixed records (D4.2.5).
    move(c, binding->record_readable, imm(c, 1));
    if (writes) {
        em_container_cow_guard(&c->em, binding->reg, done, true);
        move(c, binding->record_writable, imm(c, 1));
    }
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
    if (dynamic && !binding->array_float_native) {
        binding->array_float_native = em_new_reg(&c->em, "array_float_native", MIR_T_I64);
        binding->array_bool_native = em_new_reg(&c->em, "array_bool_native", MIR_T_I64);
        binding->array_boxed = em_new_reg(&c->em, "array_boxed", MIR_T_I64);
    }
    move(c, binding->array_native, imm(c, 0)); move(c, binding->array_writable, imm(c, 0));
    if (dynamic) {
        move(c, binding->array_float_native, imm(c, 0));
        move(c, binding->array_bool_native, imm(c, 0));
        move(c, binding->array_boxed, imm(c, 0));
    }
    MIR_label_t done = label(c);
    MIR_label_t boxed = dynamic ? label(c) : done;
    // inferred owners can change kind; guard before reading any container header.
    if (dynamic) em_guard_container(&c->em, binding->reg, LMD_TYPE_ARRAY_NUM, boxed);
    move(c, binding->array_length, reg(c, em_load_at(&c->em, binding->reg,
        LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "length")));
    if (dynamic) {
        const struct { ArrayNumElemType lane; MIR_reg_t flag; } lanes[] = {
            {ELEM_INT, binding->array_native}, {ELEM_FLOAT, binding->array_float_native},
            {ELEM_BOOL, binding->array_bool_native}};
        for (const auto& lane : lanes) {
            MIR_label_t next = label(c);
            em_array_num_elem_guard(&c->em, binding->reg, lane.lane, next);
            move(c, lane.flag, imm(c, 1));
            // the existing erased-store witness describes only int storage.
            if (lane.lane == ELEM_INT) {
                em_container_cow_guard(&c->em, binding->reg, done, true);
                move(c, binding->array_writable, imm(c, 1));
            }
            jump(c, done); mark(c, next);
        }
        jump(c, done); mark(c, boxed);
        MIR_reg_t pointer = em_physical_array_owner(&c->em, binding->reg, done);
        move(c, binding->array_length, reg(c, em_load_at(&c->em, pointer,
            LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "boxed_length")));
        move(c, binding->array_boxed, imm(c, 1)); mark(c, done);
        return;
    }
    ArrayNumElemType lane = ELEM_INT;
    if (element) lambda_array_num_elem_type_for_contract(element, &lane);
    if (!fresh) em_array_num_elem_guard(&c->em, binding->reg, lane, done);
    // storage eligibility is independent of a conservative index bound. A bound
    // wider than this array must not send every valid access through helpers.
    move(c, binding->array_native, imm(c, 1));
    if (!fresh) em_container_cow_guard(&c->em, binding->reg, done, true);
    move(c, binding->array_writable, imm(c, 1));
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
        mvp_value_contract(c->module, mvp_call_argument(fill, 0))->type_id == LMD_TYPE_INT &&
        mvp_value_contract(c->module, mvp_call_argument(fill, 1))->type_id == element->type_id;
}

static bool binding_needs_number_home(MvpCompiler* c, MvpBinding* binding) {
    Type* scalar = lambda_type_remove_error_and_null(c->module->tp->pool, binding->contract);
    return mvp_needs_number_home(scalar);
}

static void register_shared_binding(MvpCompiler* c, MvpBinding* binding) {
    if (!c->shared_bindings) c->shared_bindings = arraylist_new(4);
    if (!c->shared_bindings || !arraylist_append(c->shared_bindings, binding))
        mvp_reject(c->module, mvp_binding_node(binding), "shared-home binding allocation failed");
}

static void initialize_shared_binding(MvpCompiler* c, MvpBinding* binding) {
    if (!binding->entry->lambda_shared_local) return;
    binding->capture_home = em_capture_home_new(&c->em, binding->reg);
    register_shared_binding(c, binding);
}

static void refresh_shared_bindings(void* owner) {
    MvpCompiler* c = (MvpCompiler*)owner;
    for (int i = 0; c->shared_bindings && i < c->shared_bindings->length; i++) {
        MvpBinding* binding = (MvpBinding*)c->shared_bindings->data[i];
        move(c, binding->reg, reg(c, em_capture_home_peek(&c->em, binding->capture_home)));
        // a sibling can replace the owner or its COW state during reentry.
        MIR_reg_t witnesses[] = {binding->array_native, binding->array_float_native, binding->array_bool_native,
            binding->array_boxed, binding->array_writable, binding->record_readable, binding->record_writable};
        for (MIR_reg_t witness : witnesses) if (witness) move(c, witness, imm(c, 0));
    }
}

static void publish_var_binding(MvpCompiler* c, MvpBinding* binding) {
    if (!binding->var_home && !binding->capture_home && !binding->state_name) return;
    // borrow cells are precise Item roots even when the local is a native
    // integer or Boolean. Publishing the raw lane would lose its tag.
    MirValue item = convert(c, value(c, binding->reg, binding->contract->type_id,
        binding->rep, binding->contract), VALUE_REP_ITEM);
    if (binding->var_home) em_store_at(&c->em, binding->var_home, 0, MIR_T_I64, item.reg);
    if (binding->capture_home)
        em_call_void_2(&c->em, "lambda_capture_home_store", MIR_T_P, reg(c, binding->capture_home),
            MIR_T_I64, reg(c, item.reg), true);
    if (binding->state_name && c->function->view)
        em_call_void_4(&c->em, "tmpl_state_set", MIR_T_I64, reg(c, c->view_model),
            MIR_T_P, imm(c, (intptr_t)c->function->template_ref),
            MIR_T_P, imm(c, (intptr_t)binding->state_name), MIR_T_I64, reg(c, item.reg), true);
}

static MIR_reg_t capture_item(MvpCompiler* c, MIR_reg_t item, Type* contract) {
    // capture only marks container headers and returns the original Item.
    // Tagged scalars need no call; wide reads acquire their homes separately.
    MIR_label_t done = label(c);
    MIR_reg_t pointer = em_guard_container(&c->em, item, LMD_TYPE_RAW_POINTER, done);
    bool profiling = cow_profile_enabled();
    MIR_reg_t state = 0;
    em_container_cow_guard(&c->em, pointer, done, false, profiling ? NULL : &state);
    if (profiling) em_call_1(&c->em, cow_import("cow_capture_value", "cow_capture_value_profiled"),
        MIR_T_I64, MIR_T_I64, reg(c, item), true);
    else {
        Type* normal = lambda_type_remove_error(c->module->tp->pool, contract);
        em_container_capture(&c->em, pointer, state,
            lambda_type_nonnull_map_contract(normal) || mvp_array_contract(normal), done);
    }
    mark(c, done);
    return item;
}

static MirValue capture_projection(MvpCompiler* c, MirValue v, AstNode* source) {
    if (!source) source = (AstNode*)v.provenance_node;
    Type* domain = v.admitted_success_contract ? v.admitted_success_contract : v.semantic_contract;
    if (!source || mvp_local_binding(c->module, source) ||
            !ast_expr_insertion_needs_capture(source) ||
            (v.rep != VALUE_REP_ITEM && v.rep != VALUE_REP_RAW_GC_POINTER) ||
            (!ast_type_may_be_container(domain) && !mvp_array_contract(domain))) return v;
    // retaining a projected child creates a second observer. Capture at the
    // ownership boundary, leaving transient reads as borrows (S9.1.2, S9.3.1).
    v = convert(c, v, VALUE_REP_ITEM);
    v.reg = capture_item(c, v.reg, domain);
    MirValue result = value(c, v.reg, v.semantic_type, v.rep, v.semantic_contract, source);
    result.admitted_success_contract = v.admitted_success_contract;
    return result;
}

static MvpModuleLocation* module_location(MvpCompiler* c, MIR_item_t reference,
        MIR_disp_t unit_offset) {
    for (MvpModuleLocation* p = c->module_locations; p; p = p->next)
        if (p->reference == reference) return p;
    MvpModuleLocation* location = (MvpModuleLocation*)mem_calloc(1, sizeof(*location), MEM_CAT_TEMP);
    if (!location) { mvp_reject(c->module, NULL, "module location allocation failed"); return NULL; }
    location->next = c->module_locations; location->reference = reference;
    c->module_locations = location;
    // immutable descriptors resolve context-owned slab identities at entry,
    // so first use in a branch cannot leave the cached register undefined.
    MIR_insn_t saved = c->em.insert_after;
    c->em.insert_after = c->constant_cursor ? c->constant_cursor : c->em.frame.anchor;
    location->descriptor = em_new_reg(&c->em, "module_descriptor", MIR_T_I64);
    move(c, location->descriptor, MIR_new_ref_op(c->em.ctx, reference));
    MIR_reg_t unit = em_load_at(&c->em, location->descriptor, unit_offset, MIR_T_U32, "module_unit");
    location->state = em_module_state_for_unit(&c->em, unit, true);
    c->constant_cursor = c->em.insert_after; c->em.insert_after = saved;
    return location;
}

static MIR_reg_t module_binding_location(MvpCompiler* c, MIR_item_t reference, MIR_reg_t* slot) {
    MvpModuleLocation* location = module_location(c, reference, offsetof(LambdaModuleVarRef, module_id));
    if (!location) { *slot = 0; return 0; }
    if (!location->slot) {
        MIR_insn_t saved = c->em.insert_after;
        c->em.insert_after = c->constant_cursor;
        location->slot = em_load_at(&c->em, location->descriptor, offsetof(LambdaModuleVarRef, slot),
            MIR_T_U32, "module_index");
        c->constant_cursor = c->em.insert_after; c->em.insert_after = saved;
    }
    *slot = location->slot;
    return location->state;
}

static MIR_reg_t property_key(MvpCompiler* c, NameRef name) {
    uint32_t index = em_module_property_key_index(&c->module->property_keys, c->module->tp->pool, name);
    if (index == UINT32_MAX) { mvp_reject(c->module, NULL, "module property key allocation failed"); return 0; }
    MirPropertyKeyEntry* entry = (MirPropertyKeyEntry*)arraylist_get(c->module->property_keys, (int)index);
    // a predefined immediate emitted on a cold field arm cannot define a
    // cached register used by another arm. Materialize it at each use.
    if (entry->predefined_id != NAME_ID_NONE) return constant(c, entry->predefined_id);
    if (entry->cached_func == c->em.func && entry->cached_reg) return entry->cached_reg;
    MIR_reg_t key;
    {
        MvpModuleLocation* location = module_location(c, c->module->layout, offsetof(LambdaModuleLayout, module_id));
        if (!location) return 0;
        MIR_insn_t saved = c->em.insert_after;
        c->em.insert_after = c->constant_cursor;
        if (c->module->tp->compile_against_interp_slab) {
            // each published image owns a relocated suffix of the T0 key table.
            MIR_reg_t base = em_load_at(&c->em, location->descriptor,
                offsetof(LambdaModuleLayout, property_key_base), MIR_T_U32, "key_base");
            MIR_reg_t selected = operation(c, MIR_ADD, reg(c, base), imm(c, index));
            key = em_call_2(&c->em, "lambda_module_name_id_at", MIR_T_I64,
                MIR_T_P, reg(c, location->state), MIR_T_I64, reg(c, selected), true);
        } else key = em_module_property_key_at(&c->em, location->state, index);
        c->constant_cursor = c->em.insert_after; c->em.insert_after = saved;
    }
    entry->cached_func = c->em.func; entry->cached_reg = key;
    return key;
}

static MirValue cow_path_key(MvpCompiler* c, AstNode* segment, bool member) {
    if (!member) return expression(c, segment);
    AstNode* key = ast_unwrap_primary_to_leaf(segment);
    if (!key || key->node_type != AST_NODE_IDENT) return expression(c, segment);
    AstIdentNode* name = (AstIdentNode*)key;
    return value(c, em_load_const(&c->em, em_add_const(&c->em, name->name), MIR_T_P),
        LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING);
}

static MirValue bind_rmw_initializer(MvpCompiler* c, MvpBinding* binding,
        MirValue v, bool* borrowed) {
    AstNode* definition = mvp_binding_node(binding);
    AstNode* source = (AstNode*)v.provenance_node;
    if (!binding->entry->cow_borrow_lowered || !definition ||
            definition->node_type != AST_NODE_VARIABLE_DECLARATOR ||
            ast_cow_value_source(source) != ast_cow_value_source(((AstDeclaratorNode*)definition)->init)) return v;
    AstCowPath path = {};
    if (!ast_collect_cow_path(&path, source) || path.count < 1 || path.count > 3) return v;
    MvpBinding* root = mvp_local_binding(c->module, path.root);
    if (!root || root->owner != c->function->id) return v;
    for (int i = 0; i + 1 < path.count; i++)
        if (!path.is_member[i] && !mvp_pure_key(path.segment[i])) return v;
    MirValue owner = convert(c, read_binding(c, root, path.root, false), VALUE_REP_ITEM);
    MIR_reg_t keys[2] = {constant(c, ITEM_NULL), constant(c, ITEM_NULL)};
    for (int i = 0; i + 1 < path.count; i++) {
        MirValue key = cow_path_key(c, path.segment[i], path.is_member[i]);
        keys[i] = convert(c, key, VALUE_REP_ITEM).reg;
    }
    MirValue child = convert(c, v, VALUE_REP_ITEM);
    child.reg = em_bind_rmw_handle(&c->em, owner.reg, child.reg, path.count - 1, keys, true);
    child = value(c, child.reg, v.semantic_type, VALUE_REP_ITEM, v.semantic_contract, source);
    child.admitted_success_contract = v.admitted_success_contract;
    *borrowed = true;
    return convert(c, child, v.rep);
}

static MirValue admit_outcome(MvpCompiler* c, MirValue input, Type* contract,
        const char* site, Type* outcome_contract = NULL, const AstNode* source = NULL) {
    MvpErrorTarget failure = {};
    MirValue result = admit(c, input, contract, &failure, site, true, source);
    if (!failure.done) return result;
    MIR_reg_t joined = em_new_reg(&c->em, "admission_outcome", MIR_T_I64);
    MIR_label_t done = label(c);
    move(c, joined, reg(c, result.reg)); jump(c, done); mark(c, failure.done);
    move(c, joined, reg(c, failure.error)); mark(c, done);
    if (!outcome_contract) outcome_contract = lambda_type_union_normalized(
        c->module->tp->pool, contract, &TYPE_ERROR);
    result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, outcome_contract);
    result.admitted_success_contract = lambda_type_remove_error(c->module->tp->pool, contract);
    return result;
}

static MirValue own_binding_scalar(MvpCompiler* c, MvpBinding* binding, MirValue v) {
    if (binding->rep == VALUE_REP_ITEM && !mvp_array_element(binding->contract) &&
            binding_needs_number_home(c, binding)) {
        if (!binding->scalar_home) binding->scalar_home = em_scalar_home_new(&c->em);
        MIR_reg_t home = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, binding->scalar_home));
        // the binding owns cold bytes independently of a borrowed producer home.
        v.reg = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_DYNAMIC, v.reg, home);
    }
    return v;
}

static void store_binding(MvpCompiler* c, MvpBinding* binding, MirValue v,
        const char* site_kind = "declaration") {
    AstNode* node = mvp_binding_node(binding);
    bool borrowed = false;
    v = bind_rmw_initializer(c, binding, v, &borrowed);
    bool observed_projection = node->node_type == AST_NODE_VARIABLE_DECLARATOR &&
        binding->entry->is_place_copy && !mir_place_copy_snapshot_required(binding->entry) &&
        ast_cow_value_source((AstNode*)v.provenance_node) ==
            ast_cow_value_source(((AstDeclaratorNode*)node)->init);
    if (!borrowed && !observed_projection) v = capture_projection(c, v, NULL);
    bool fresh = fresh_array_value(c, v, binding->contract);
    bool inferred = node->node_type == AST_NODE_VARIABLE_DECLARATOR && !((AstDeclaratorNode*)node)->declared_type;
    // the declaration's pooled name outlives the JIT artifact and identifies failed assignments (S7.7.4).
    const char* site = em_binding_boundary_site(&c->em, site_kind, binding->entry->name);
    if (inferred) {
        // private carrier inference adds no receiving contract (D4.3.1).
        // An array's first literal must not reject its later literal members.
        v = convert(c, v, binding->rep);
    } else if (binding->entry->admission_may_fail && binding->rep == VALUE_REP_ITEM) {
        // deferred admission stores its outcome and continues; only an
        // explicit caret chooses an early exit (S7.7.2v2, S7.7.4v2).
        v = admit_outcome(c, v, binding->admission_contract, site, binding->contract, node);
    } else v = admit(c, v, lambda_type_contract_has_binder(binding->admission_contract, true)
            ? binding->admission_contract : binding->contract,
            binding->scope ? &binding->scope->failure : NULL, site, false, node);
    v = own_binding_scalar(c, binding, convert(c, v, binding->rep));
    move(c, binding->reg, reg(c, v.reg), v.mir_type);
    if (binding->method_field_present) move(c, binding->method_field_present, imm(c, 1));
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
    publish_var_binding(c, binding);
}

static MirValue constant_fill(MvpCompiler* c, AstCallNode* fill, Type* contract, ArrayRepCert* certificate) {
    if (!certificate || (certificate->flags & ARRAY_REP_CERT_COUNTED) ||
            mvp_array_element(contract) != &TYPE_INT || !mvp_pure_key(mvp_call_argument(fill, 0)) ||
            !mvp_pure_key(mvp_call_argument(fill, 1))) return {};
    MvpIntRange count = mvp_int_range(c->module, mvp_call_argument(fill, 0));
    MvpIntRange scalar = mvp_int_range(c->module, mvp_call_argument(fill, 1));
    int64_t samples[64];
    if (!count.finite || count.lower != count.upper || count.lower < 0 || count.lower > 64 ||
            !scalar.finite || scalar.lower != scalar.upper) return {};
    for (int64_t i = 0; i < count.lower; i++) samples[i] = scalar.lower;
    // read-only, nonescaping actuals may share immutable Input storage with the compiled image (D5.1.1).
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
            !mvp_boundary_proven(mvp_value_contract(c->module, mvp_call_argument(fill, 1)), mvp_array_element(contract))))
        // constructor errors propagate at the caret; a later destination
        // admission failure must remain the binding's outcome (S7.7.2v2).
        return {};
    if (read_only) {
        MirValue folded = constant_fill(c, fill, contract, certificate);
        if (folded.reg) return folded;
    }
    MirValue count = expression(c, mvp_call_argument(fill, 0));
    MirValue scalar = expression(c, mvp_call_argument(fill, 1));
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
    Type* outcome = lambda_type_union_normalized(c->module->tp->pool, contract, &TYPE_ERROR);
    MirValue v = value(c, result, outcome->type_id, VALUE_REP_ITEM, outcome, source);
    v.admitted_success_contract = contract;
    return fill->propagate ? propagated_value(c, v, source) : v;
}

// only direct, in-bounds scalar projections can erase aggregate identity.
// Audit every use before evaluating members so misses keep ordinary admission.
static bool scalar_projection_uses(MvpCompiler* c, MvpBinding* binding, AstDeclaratorNode* declaration,
        int count, String** keys, bool record) {
    if (!c->function->ast || binding->module_slot || binding->module_ref || binding->capture_binding || binding->entry->lambda_shared_local ||
            ast_body_may_write_entry(c->function->ast->body, binding->entry, true)) return false;
    AstIndex* index = &c->module->tp->ast_index; uint32_t uses_count = 0;
    const AstNodeId* uses = ast_index_binding_uses(index,
        ast_index_find_binding_entry(index, binding->entry), &uses_count);
    for (uint32_t i = 0; i < uses_count; i++) {
        AstNode* use = index->nodes[uses[i]];
        if (use == (AstNode*)declaration || use == declaration->id) continue;
        if (use->node_type != AST_NODE_IDENT || mvp_node_owner(c->module, uses[i]) != binding->owner) return false;
        AstNode* parent = ast_index_parent(index, use);
        while (parent && parent->node_type == AST_NODE_PRIMARY) parent = ast_index_parent(index, parent);
        if (!parent || parent->node_type != (record ? AST_NODE_MEMBER_EXPR : AST_NODE_INDEX_EXPR)) return false;
        AstFieldNode* access = (AstFieldNode*)parent;
        if (mvp_local_binding(c->module, access->object) != binding || !access->field || access->field->next) return false;
        if (record) {
            AstNode* key = ast_unwrap_primary_to_leaf(access->field);
            if (!key || key->node_type != AST_NODE_IDENT) return false;
            String* name = ((AstIdentNode*)key)->name; bool found = false;
            for (int j = 0; j < count; j++) found |= string_eq(name, keys[j]);
            if (!found) return false;
        } else {
            int64_t position;
            if (!mir_decimal_int_literal(c->module->tp->source, access->field, &position) ||
                    position < 0 || position >= count) return false;
        }
    }
    return true;
}

static MvpScalarAggregate* new_scalar_aggregate(MvpCompiler* c, int count) {
    MvpScalarAggregate* aggregate = (MvpScalarAggregate*)mem_calloc(1, sizeof(*aggregate), MEM_CAT_TEMP);
    aggregate->count = count; aggregate->next = c->scalar_aggregates; c->scalar_aggregates = aggregate;
    return aggregate;
}

static bool scalarize_binding(MvpCompiler* c, MvpBinding* binding, AstDeclaratorNode* declaration) {
    binding->scalar_aggregate = NULL;
    AstNode* source = ast_unwrap_primary_to_leaf(declaration->init);
    bool record = source && source->node_type == AST_NODE_MAP;
    if (!source || (!record && source->node_type != AST_NODE_ARRAY)) return false;
    AstNode* items = record ? ((AstMapNode*)source)->item : ((AstArrayNode*)source)->item;
    int count = ast_linked_node_count(items);
    if (!count || count > 16) return false;
    TypeMap* shape = record ? mvp_record_contract(binding->contract, false) : NULL;
    Type* element = record ? NULL : mvp_array_element(binding->contract);
    if ((record && (!shape || !shape->is_trusted_contract)) || (!record && !element)) return false;
    String* keys[16] = {}; Type* domains[16] = {}; AstNode* values[16] = {};
    int n = 0;
    for (AstNode* item = items; item; item = item->next, n++) {
        AstNamedNode* named = record && item->node_type == AST_NODE_KEY_EXPR ? (AstNamedNode*)item : NULL;
        if (record && (!named || named->is_spread || named->key || !named->name)) return false;
        ShapeEntry* field = record ? find_shape_read_field_by_name(shape, named->name->chars, named->name->len) : NULL;
        Type* domain = record && field ? type_field_unwrap_simple_decl(field->type) : element;
        if (domain != &TYPE_INT && domain != &TYPE_FLOAT && domain != &TYPE_BOOL) return false;
        values[n] = record ? named->as : item; domains[n] = domain; keys[n] = record ? named->name : NULL;
        if (!mvp_boundary_proven(mvp_value_contract(c->module, values[n]), domain)) return false;
    }
    if (record) {
        int fields = 0;
        for (ShapeEntry* field = shape->shape; field; field = typemap_next_field(shape, field)) {
            if (!field->name || field->ns || field->key_kind != NAME_KEY_STRING) return false;
            int matches = 0;
            for (int j = 0; j < count; j++)
                matches += field->name->length == keys[j]->len &&
                    memcmp(field->name->str, keys[j]->chars, keys[j]->len) == 0;
            if (matches != 1) return false;
            fields++;
        }
        if (fields != count) return false;
    }
    if (!scalar_projection_uses(c, binding, declaration, count, keys, record)) return false;
    MvpScalarAggregate* aggregate = new_scalar_aggregate(c, count);
    for (int j = 0; j < count; j++) {
        aggregate->members[j] = convert(c, expression(c, values[j]), mvp_rep(domains[j]));
        aggregate->keys[j] = keys[j];
    }
    binding->scalar_aggregate = aggregate;
    return true;
}

static int recipe_parameter(MvpFunction*, AstNode*);

static bool scalar_recipe_extents(MvpCompiler* c, MvpFunction* function, AstNode* source,
        int64_t* extents, int depth = 0) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    if (!node || depth > 32) return false;
    if (node->node_type == AST_NODE_PRIMARY)
        return node->type && node->type->type_id == LMD_TYPE_FLOAT && node->type->is_literal;
    if (node->node_type == AST_NODE_INDEX_EXPR) {
        AstFieldNode* read = (AstFieldNode*)node;
        int position = recipe_parameter(function, read->object); int64_t index;
        if (position < 0 || !read->field || read->field->next ||
                !mir_decimal_int_literal(c->module->tp->source, read->field, &index) || index < 0 || index >= INT53_MAX) return false;
        if (index + 1 > extents[position]) extents[position] = index + 1;
        return true;
    }
    if (node->node_type == AST_NODE_UNARY) {
        AstUnaryNode* unary = (AstUnaryNode*)node;
        return (unary->op == OPERATOR_NEG || unary->op == OPERATOR_POS) &&
            scalar_recipe_extents(c, function, unary->operand, extents, depth + 1);
    }
    if (node->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* binary = (AstBinaryNode*)node;
    return (binary->op == OPERATOR_ADD || binary->op == OPERATOR_SUB ||
        binary->op == OPERATOR_MUL || binary->op == OPERATOR_DIV) &&
        scalar_recipe_extents(c, function, binary->left, extents, depth + 1) &&
        scalar_recipe_extents(c, function, binary->right, extents, depth + 1);
}

static bool scalarize_call_binding(MvpCompiler* c, MvpBinding* binding, AstDeclaratorNode* declaration) {
    AstNode* source = ast_unwrap_primary_to_leaf(declaration->init);
    if (!source || source->node_type != AST_NODE_CALL_EXPR || mvp_array_element(binding->admission_contract) != &TYPE_FLOAT) return false;
    AstCallNode* call = (AstCallNode*)source;
    MvpFunction* function = mvp_function(c->module, ast_direct_call_function(call));
    if (!function || function == c->function || function->imported || function->boxed_entry || function->ast->captures ||
            function->method_owner || function->owns_open_scopes || function->ast->is_async || function->ast->is_generator ||
            mvp_array_element(function->result_contract) != &TYPE_FLOAT || !call->propagate) return false;
    AstNode* result = mvp_function_result_expression(function);
    if (!result || result->node_type != AST_NODE_ARRAY) return false;
    int count = ast_linked_node_count(((AstArrayNode*)result)->item), parameters = function->abi.param_count;
    if (!count || count > 16 || parameters > 16 || mvp_call_argument_count(call) != parameters ||
            !scalar_projection_uses(c, binding, declaration, count, NULL, false)) return false;
    int64_t extents[16] = {}; MvpBinding* actuals[16] = {}; MIR_reg_t pointers[16] = {};
    for (AstNode* item = ((AstArrayNode*)result)->item; item; item = item->next)
        if (!scalar_recipe_extents(c, function, item, extents)) return false;
    AstNamedNode* parameter = function->ast->param;
    for (int i = 0; i < parameters; i++, parameter = (AstNamedNode*)parameter->next) {
        TypeParam* contract = lambda_type_param(parameter->type);
        actuals[i] = mvp_local_binding(c->module, mvp_call_argument(call, i));
        if (!contract || contract->is_var_param || contract->is_optional || contract->default_value ||
                mvp_array_element(mvp_contract(parameter)) != &TYPE_FLOAT || !actuals[i] ||
                actuals[i]->owner != c->function->id || actuals[i]->module_ref || actuals[i]->module_slot || actuals[i]->state_name ||
                actuals[i]->capture_home || actuals[i]->var_home || actuals[i]->entry->lambda_shared_local ||
                !mvp_admission_proven(c->module, actuals[i]->contract, mvp_contract(parameter))) return false;
    }
    // read-only actuals have no evaluation effects. Guard every source extent
    // before scalar arithmetic; a short source invokes the original checked call.
    MvpScalarAggregate* aggregate = new_scalar_aggregate(c, count);
    MIR_label_t checked = label(c), done = label(c);
    for (int i = 0; i < parameters; i++) {
        MirValue actual = convert(c, read_binding(c, actuals[i], mvp_call_argument(call, i), false), VALUE_REP_ITEM);
        pointers[i] = em_guard_container(&c->em, actual.reg, LMD_TYPE_ARRAY_NUM, checked);
        em_array_num_elem_guard(&c->em, pointers[i], ELEM_FLOAT, checked);
        MIR_reg_t length = em_load_at(&c->em, pointers[i], LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "recipe_length");
        branch(c, MIR_BLT, checked, reg(c, length), imm(c, extents[i]));
    }
    MvpBinding* saved = snapshot_bindings(c->module); bool dense = c->dense_arrays;
    c->dense_arrays = true; parameter = function->ast->param;
    for (int i = 0; i < parameters; i++, parameter = (AstNamedNode*)parameter->next) {
        MvpBinding* local = mvp_binding(c->module, parameter->entry);
        local->owner = c->function->id; local->contract = mvp_contract(parameter); local->producer_success_contract = NULL;
        local->rep = VALUE_REP_ITEM; local->reg = pointers[i]; local->scalar_aggregate = NULL;
        local->dense_items = em_load_at(&c->em, pointers[i], LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "recipe_items");
    }
    int position = 0;
    for (AstNode* item = ((AstArrayNode*)result)->item; item; item = item->next, position++) {
        MirValue member = convert(c, expression(c, item), VALUE_REP_F64);
        MIR_reg_t scalar = em_new_reg(&c->em, "recipe_member", MIR_T_D);
        move(c, scalar, reg(c, member.reg), MIR_T_D);
        aggregate->members[position] = value(c, scalar, LMD_TYPE_FLOAT, VALUE_REP_F64, &TYPE_FLOAT);
    }
    restore_bindings(c->module, saved); mem_free(saved); c->dense_arrays = dense;
    jump(c, done); mark(c, checked);
    MirValue ordinary = convert(c, direct_call(c, function, call, false, NULL, true), VALUE_REP_ITEM);
    MIR_reg_t items = em_load_at(&c->em, ordinary.reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_P, "checked_recipe_items");
    for (int i = 0; i < count; i++)
        move(c, aggregate->members[i].reg, reg(c, em_load_at(&c->em, items, i * 8, MIR_T_D, "checked_recipe_member")), MIR_T_D);
    mark(c, done); binding->scalar_aggregate = aggregate;
    return true;
}

static MirValue binding_value(MvpCompiler* c, MvpBinding* binding, AstNode* source) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    // an inferred primitive domain proves consumers, not a receiving schema.
    // Keep ordinary constructors and avoid certificate maintenance on raw
    // stores into an unrestricted owner (D4.3.1, S12.2.1v3).
    if (binding->inferred_array) return binding->inferred_append
        ? array_literal(c, (AstArrayNode*)node, binding->contract) : expression(c, source);
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
            return direct_call(c, f, call, false, binding, call->propagate);
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
    Type* element = mvp_module_array_literal_element(c->module, b, source);
    if (!element) return {};
    if (!b->constant_ready) {
        // immutable literal tables belong to the Input arena; writes still
        // detach through the ordinary COW boundary (D5.1.2, S9.1.2).
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
    Script* owner; int slot;
    if (b && mvp_interp_binding(c->module, b->entry, &owner, &slot)) {
        MIR_reg_t state = em_module_state_for_unit(&c->em, constant(c, script_module_layout_id(owner)), true);
        MIR_reg_t item = em_module_slot_item(&c->em, state, imm(c, slot), true);
        if (capture) item = capture_item(c, item, b->contract);
        return convert(c, value(c, item, b->contract->type_id, VALUE_REP_ITEM, b->contract, node), b->rep);
    }
    if (b && b->module_ref && b->owner != c->function->id) {
        MIR_reg_t slot;
        MIR_reg_t state = module_binding_location(c, b->module_ref, &slot);
        MIR_reg_t item = em_module_slot_item(&c->em, state, reg(c, slot), true);
        MirValue loaded = value(c, item, b->contract->type_id, VALUE_REP_ITEM, b->contract, node);
        if (capture && ast_type_may_be_container(b->contract))
            loaded = value(c, capture_item(c, item, b->contract),
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
            b->entry && !b->entry->is_mutable && mvp_binding_node(b)->node_type == AST_NODE_VARIABLE_DECLARATOR) {
        MirValue folded = module_literal(c, b, ((AstDeclaratorNode*)mvp_binding_node(b))->init);
        if (!folded.reg) folded = pooled_constant(c, ((AstDeclaratorNode*)mvp_binding_node(b))->init, c->module->tp->const_list);
        if (folded.reg) return folded;
    }
    if (!b || !b->reg || b->owner != c->function->id) {
        NameEntry* entry = node && node->node_type == AST_NODE_IDENT ? ((AstIdentNode*)node)->entry : NULL;
        AstNode* init = entry && entry->node && entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR
            ? ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)entry->node)->init) : NULL;
        char reason[256];
        snprintf(reason, sizeof(reason), "a value read requires a local binding: %s definition=%d imported=%d constant=%d owner=%u function=%u site=%d register=%u",
            entry && entry->name ? entry->name->chars : "missing", entry && entry->node ? entry->node->node_type : -1,
            entry && (entry->import || entry->import_owner), init ? init->const_kind : -1,
            b ? b->owner : AST_FUNCTION_ID_INVALID, c->function->id,
            b && mvp_binding_node(b) ? mvp_binding_node(b)->node_type : -1, b ? (unsigned)b->reg : 0);
        mvp_reject(c->module, node, reason); return null_value(c);
    }
    MIR_reg_t r = em_new_reg(&c->em, "read", em_mir_type_for_rep(b->rep));
    move(c, r, reg(c, b->reg), em_mir_type_for_rep(b->rep));
    bool optional_string = false;
    Type* string_base = lambda_type_nullable_lane_base(
        lambda_type_remove_error(c->module->tp->pool, b->contract), &optional_string);
    bool optional_success = false;
    Type* scalar_success = mvp_scalar_success_contract(c->module, b->contract);
    Type* success = b->producer_success_contract ? b->producer_success_contract : scalar_success;
    Type* success_base = success ? lambda_type_nullable_lane_base(success, &optional_success) : NULL;
    bool scalar_value = success_base && (success_base->type_id == LMD_TYPE_FLOAT ||
        success_base->type_id == LMD_TYPE_INT || success_base->type_id == LMD_TYPE_BOOL ||
        success_base->type_id == LMD_TYPE_DECIMAL || success_base->type_id == LMD_TYPE_NUM_SIZED ||
        success_base->type_id == LMD_TYPE_SYMBOL ||
        (success_base->type_id == LMD_TYPE_STRING && !b->string_buffer_owned));
    // a mutable wide scalar still needs its read snapshot: a later sibling
    // expression can overwrite the binding's owned number-home bytes.
    bool stable_scalar = scalar_value && (b->producer_success_contract || !b->entry->is_mutable);
    if (capture && b->string_buffer_owned && string_base &&
            string_base->type_id == LMD_TYPE_STRING && b->rep == VALUE_REP_RAW_GC_POINTER)
        r = em_call_1(&c->em, "fn_string_freeze", MIR_T_P, MIR_T_P, reg(c, r), true);
    Type* map_contract = lambda_type_nonnull_map_contract(b->contract);
    // unchanged scalar/error bindings already own their number-home bytes;
    // reads reuse that owner and have no mutable buffer to snapshot (D6.2.3).
    if (capture && !stable_scalar && ((((b->rep == VALUE_REP_ITEM || b->rep == VALUE_REP_RAW_GC_POINTER) &&
            ast_type_may_be_container(b->contract)) ||
            (b->rep == VALUE_REP_ITEM && string_base && string_base->type_id == LMD_TYPE_STRING)) ||
            mvp_array_element(b->contract) || map_contract ||
            (string_base && string_base->type_id == LMD_TYPE_MAP) ||
            mvp_array_contract(b->contract))) {
        r = capture_item(c, r, b->contract);
        if (b->array_writable) move(c, b->array_writable, imm(c, 0));
        if (b->record_writable) move(c, b->record_writable, imm(c, 0));
    }
    bool stable_home = b->stable_scalar_home && b->scalar_home && !b->capture_home && !b->var_home;
    if (b->rep == VALUE_REP_ITEM && !stable_scalar && !stable_home &&
            !mvp_array_element(b->contract) && binding_needs_number_home(c, b)) {
        int home = em_scalar_home_new(&c->em);
        r = em_adopt_scalar_item_value(&c->em, SCALAR_RETURN_DYNAMIC, r,
            em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home)));
    }
    MirValue result = value(c, r, b->contract->type_id, b->rep, b->contract, node);
    // native carriers already encode their domain; this fact describes only
    // the success alternative of a boxed outcome (D2.3.5).
    if (b->rep == VALUE_REP_ITEM && success)
        result.admitted_success_contract = success;
    return result;
}

static MirValue observed_expression(MvpCompiler* c, AstNode* source) {
    MvpBinding* binding = mvp_local_binding(c->module, source);
    // a scalar observation cannot publish or mutate the container it inspects.
    return binding ? read_binding(c, binding, source, false) : expression(c, source);
}

static AstNode* string_recipe_body(MvpFunction* f) {
    if (!f || mvp_parameter_receiver(f) || f->result_contract != &TYPE_STRING ||
            f->abi.effects.may_return_error || f->abi.effects.may_reenter)
        return NULL;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next)
        if (mvp_contract(p)->type_id != LMD_TYPE_STRING) return NULL;
    return mvp_function_result_expression(f);
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
            for (int a_index = 0; a_index < mvp_call_argument_count(call); a_index++) {
                AstNode* a = mvp_call_argument(call, a_index);
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
        AstNode* body = mvp_function_result_expression(f);
        // the generic string() effect includes reentry; this exact int-only
        // wrapper can only invoke the canonical scalar renderer (S4.1.2).
        if (f->result_contract != &TYPE_STRING || !p || p->next || lambda_type_param(p->type)->is_var_param ||
                mvp_contract(p) != &TYPE_INT || !body || body->node_type != AST_NODE_CALL_EXPR) return NULL;
        AstCallNode* converted = (AstCallNode*)body;
        if (recipe_parameter(f, mvp_call_argument(converted, 0)) != 0) return NULL;
        target = ast_unwrap_primary_to_leaf(converted->function);
    }
    if (!target || target->node_type != AST_NODE_SYS_FUNC ||
            ((AstSysFuncNode*)target)->fn_info->fn != SYSFUNC_STRING ||
            mvp_inferred_scalar_contract(mvp_value_contract(c->module, mvp_call_argument(call, 0))) != &TYPE_INT) return NULL;
    return mvp_call_argument(call, 0);
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
        MirValue inputs[LAMBDA_MAX_FUNCTION_ARGS];
        for (int arg = 0; arg < mvp_call_argument_count(call); arg++)
            inputs[arg] = string_recipe_argument(c, mvp_call_argument(call, arg), scope, actuals);
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
        bool formatted_scalar = base && !optional &&
            (base->type_id == LMD_TYPE_INT || base->type_id == LMD_TYPE_DECIMAL);
        if (!base || (base->type_id != LMD_TYPE_STRING && !formatted_scalar)) {
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
        part->semantic_contract = part->admitted_success_contract ? part->admitted_success_contract :
            lambda_type_remove_error(c->module->tp->pool, part->semantic_contract);
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

static MirValue array_read_values(MvpCompiler* c, AstFieldNode* node,
        MvpBinding* binding, MirValue owner, MirValue key, bool pure_key,
        MIR_label_t boolean_target, bool when_true) {
    if (boolean_target && (mvp_array_element(owner.semantic_contract) != &TYPE_BOOL ||
            key.rep != VALUE_REP_INT_LANE || c->dense_arrays || (binding && binding->array_present)))
        boolean_target = NULL;
    Type* pointer_element = lambda_array_contract_element(owner.semantic_contract);
    Type* map_element = lambda_type_nonnull_map_contract(pointer_element);
    bool pointer_lane = pointer_element == &TYPE_STRING || map_element || mvp_array_contract(pointer_element);
    if (pointer_lane && key.rep == VALUE_REP_INT_LANE) {
        MvpIntRange fixed = mvp_int_range(c->module, node->field);
        if (pointer_element == &TYPE_STRING && binding && binding->constant_ready && fixed.finite && fixed.lower == fixed.upper &&
                fixed.lower >= 0 && fixed.upper < binding->constant_item.array->length) {
            Item member = array_get(binding->constant_item.array, fixed.lower);
            if (get_type_id(member) == LMD_TYPE_STRING) {
                AstNode* source = ((AstDeclaratorNode*)mvp_binding_node(binding))->init;
                AstNode* literal = ((AstArrayNode*)ast_unwrap_primary_to_leaf(source))->item;
                for (int64_t i = 0; literal && i < fixed.lower; i++) literal = literal->next;
                MIR_reg_t r = em_load_const(&c->em, em_add_const(&c->em, member.get_safe_string()), MIR_T_P);
                return value(c, r, LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, literal->type, literal);
            }
        }
        Type* contract = mvp_nullable_contract(c->module, pointer_element);
        TypeId kind = pointer_element == &TYPE_STRING ? LMD_TYPE_STRING : map_element ? LMD_TYPE_MAP : LMD_TYPE_ARRAY;
        MIR_reg_t result = em_new_reg(&c->em, "pointer_element", MIR_T_P);
        MIR_label_t slow = label(c), absent = label(c), done = label(c);
        owner = convert(c, owner, VALUE_REP_ITEM);
        move(c, result, reg(c, em_pointer_array_index(&c->em, owner.reg, key.reg, slow, absent)));
        jump(c, done); mark(c, absent); move(c, result, imm(c, 0)); jump(c, done);
        mark(c, slow);
        MirValue boxed_key = convert(c, key, VALUE_REP_ITEM);
        MIR_reg_t item_result = em_call_2(&c->em, "fn_index", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, boxed_key.reg), true);
        move(c, result, reg(c, convert(c, value(c, item_result, kind, VALUE_REP_ITEM, contract),
            VALUE_REP_RAW_GC_POINTER).reg)); mark(c, done);
        return value(c, result, kind, VALUE_REP_RAW_GC_POINTER, contract, node);
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
    MIR_reg_t cached_index = 0;
    if (!native && binding && (binding->array_native || binding->array_boxed) &&
            pure_key && key.rep == VALUE_REP_F64) {
        cached_index = em_double_to_int_lane(&c->em, key.reg, false, true, slow);
        MIR_reg_t restored = em_new_reg(&c->em, "position_roundtrip", MIR_T_D);
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_I2D, reg(c, restored), reg(c, cached_index)));
        // positions admit integral floats, with no truncation (S8.2.4).
        branch(c, MIR_DBNE, slow, reg(c, key.reg), reg(c, restored));
    }
    if (!native && binding && (binding->array_native || binding->array_boxed) && pure_key &&
            (key.rep == VALUE_REP_INT_LANE || key.rep == VALUE_REP_ITEM))
        // an erased key can still select the cached physical lane. Guard it
        // once, keeping poison and other key domains in fn_index (S7.1.1v3).
        cached_index = key.rep == VALUE_REP_INT_LANE ? key.reg : em_guard_finite_int(&c->em, key, slow);
    if (cached_index && binding->array_native) {
        // erased owners retain cached physical lanes without acquiring an
        // inferred element contract; nullable lane markers still box as null.
        const struct { MIR_reg_t flag; Type* element; } lanes[] = {
            {binding->array_native, element ? element : &TYPE_INT},
            {element ? 0 : binding->array_float_native, &TYPE_FLOAT},
            {element ? 0 : binding->array_bool_native, &TYPE_BOOL}};
        for (const auto& lane : lanes) {
            if (!lane.flag) continue;
            MIR_label_t next = label(c);
            branch(c, MIR_BEQ, next, reg(c, lane.flag), imm(c, 0));
            branch(c, MIR_UBGE, slow, reg(c, cached_index), reg(c, binding->array_length));
            MIR_reg_t address = em_array_element_address(&c->em, owner.reg, cached_index, lane.element == &TYPE_BOOL ? 1 : 8);
            MirValue member = value(c, em_load_at(&c->em, address, 0,
                lane.element == &TYPE_FLOAT ? MIR_T_D : lane.element == &TYPE_BOOL ? MIR_T_U8 : MIR_T_I64, "dynamic_element"),
                lane.element->type_id, mvp_rep(lane.element), mvp_nullable_contract(c->module, lane.element));
            move(c, output, reg(c, convert(c, member, VALUE_REP_ITEM).reg)); jump(c, done);
            mark(c, next);
        }
    }
    if (cached_index && binding->array_boxed) {
        MIR_label_t next = label(c);
        branch(c, MIR_BEQ, next, reg(c, binding->array_boxed), imm(c, 0));
        branch(c, MIR_UBGE, slow, reg(c, cached_index), reg(c, binding->array_length));
        MIR_reg_t address = em_array_element_address(&c->em, owner.reg, cached_index, sizeof(Item));
        move(c, output, reg(c, em_load_at(&c->em, address, 0, MIR_T_I64, "boxed_element")));
        jump(c, done); mark(c, next);
    }
    if (native) {
        MIR_label_t absent = label(c);
        ArrayNumElemType lane;
        lambda_array_num_elem_type_for_contract(element, &lane);
        if (binding && binding->array_native && pure_key) {
            // boxed owners cache int and float eligibility separately. A
            // float success proof must use its own flag, not the int flag.
            MIR_reg_t native = binding->array_native;
            if (!mvp_array_element(binding->contract)) {
                if (element == &TYPE_FLOAT && binding->array_float_native) native = binding->array_float_native;
                if (element == &TYPE_BOOL && binding->array_bool_native) native = binding->array_bool_native;
            }
            branch(c, MIR_BEQ, slow, reg(c, native), imm(c, 0));
            if (!present) branch(c, MIR_UBGE, absent, reg(c, key.reg), reg(c, binding->array_length));
        } else {
            em_array_num_elem_guard(&c->em, owner.reg, lane, slow);
            if (!present) em_array_num_bounds_check(&c->em, owner.reg, key.reg, absent);
        }
        MIR_reg_t address = em_array_element_address(&c->em, owner.reg, key.reg, element == &TYPE_BOOL ? 1 : 8);
        MIR_reg_t member = em_load_at(&c->em, address, 0, em_numeric_storage_type(lane), "element");
        if (boolean_target) branch(c, when_true ? MIR_BEQ : MIR_BNE, boolean_target, reg(c, member), imm(c, 1));
        else move(c, output, reg(c, member), em_mir_type_for_rep(rep));
        jump(c, done);
        if (!present) {
            mark(c, absent);
            if (boolean_target) {
                if (!when_true) jump(c, boolean_target);
            } else move(c, output, element == &TYPE_FLOAT ? reg(c, null_float(c)) :
                imm(c, element == &TYPE_INT ? INT_LANE_NULL : BOOL_ERROR), em_mir_type_for_rep(rep));
            jump(c, done);
        }
    }
    bool array_family = owner.semantic_type == LMD_TYPE_ANY || owner.semantic_type == LMD_TYPE_TYPE ||
        mvp_array_contract(owner.semantic_contract);
    if (!native && (key.rep == VALUE_REP_INT_LANE || (key.rep == VALUE_REP_ITEM && array_family))) {
        // a physical boxed Array needs no element contract. Views, native
        // pointer lanes, poison/noninteger keys and other receivers retain
        // fn_index. Erased keys can still pass the exact finite-int guard;
        // known non-array receivers need no dead physical-array alternative.
        MIR_label_t absent = label(c);
        MIR_reg_t index = em_guard_finite_int(&c->em, key, slow);
        MirValue boxed_owner = convert(c, owner, VALUE_REP_ITEM);
        move(c, output, reg(c, em_physical_array_index(&c->em, boxed_owner.reg,
            index, slow, absent, false)));
        jump(c, done); mark(c, absent);
        move(c, output, imm(c, ITEM_NULL)); jump(c, done);
    }
    mark(c, slow);
    if (native && !c->function->body_may_gc) {
        // use the lane fallback when it eliminates the collecting frame.
        // already-collecting bodies retain their established boxed cold path.
        ArrayNumElemType lane;
        lambda_array_num_elem_type_for_contract(element, &lane);
        MirValue boxed = convert(c, owner, VALUE_REP_ITEM);
        MIR_reg_t bits = em_call_3(&c->em, "lambda_array_primitive_index", MIR_T_I64,
            MIR_T_I64, reg(c, boxed.reg), MIR_T_I64, reg(c, key.reg), MIR_T_I64, imm(c, lane), true);
        MIR_reg_t member = element == &TYPE_FLOAT ? em_emit_bits_double(&c->em, bits) : bits;
        if (boolean_target) branch(c, when_true ? MIR_BEQ : MIR_BNE, boolean_target, reg(c, member), imm(c, 1));
        else move(c, output, reg(c, member), em_mir_type_for_rep(rep));
        mark(c, done);
        return boolean_target ? MirValue{} : value(c, output, element->type_id, rep, contract, node);
    }
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

static MirValue subscript_key(MvpCompiler* c, AstNode* key, MirValue owner) {
    MIR_reg_t previous = c->last_index_item;
    if (em_index_keys_use_last(key)) c->last_index_item = convert(c, owner, VALUE_REP_ITEM).reg;
    MirValue result = expression(c, key);
    c->last_index_item = previous;
    return result;
}

static MirValue array_read(MvpCompiler* c, AstFieldNode* node,
        MIR_label_t boolean_target = NULL, bool when_true = false) {
    if (node->field && node->field->next) {
        MirValue owner = convert(c, expression(c, node->object), VALUE_REP_ITEM);
        struct CoordinateReader { MvpCompiler* compiler; MirValue receiver; } reader = {c, owner};
        MIR_reg_t result = em_index_nd(&c->em, owner.reg, node->field,
            [](void* state, AstNode* key) -> MIR_reg_t {
                CoordinateReader* reader = (CoordinateReader*)state;
                MvpCompiler* c = reader->compiler;
                MirValue coordinate = convert(c, subscript_key(c, key, reader->receiver), VALUE_REP_ITEM);
                return em_call_1(&c->em, "fn_int64_index", MIR_T_I64,
                    MIR_T_I64, reg(c, coordinate.reg), true);
            }, &reader);
        return value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    MvpBinding* binding = mvp_local_binding(c->module, node->object);
    if (binding && binding->scalar_aggregate) {
        int64_t position;
        if (mir_decimal_int_literal(c->module->tp->source, node->field, &position) &&
                position >= 0 && position < binding->scalar_aggregate->count)
            return binding->scalar_aggregate->members[position];
    }
    // immutable lookup-table keys cannot mutate or retain this owner. Reuse
    // the indexed-region proof instead of freezing it before a nested read.
    bool pure_key = mvp_index_key(c->module, node->field);
    MirValue owner = binding && pure_key ? read_binding(c, binding, node->object, false)
        : expression(c, node->object);
    MirValue key = subscript_key(c, node->field, owner);
    if (owner.semantic_type == LMD_TYPE_STRING && !nullable(owner) &&
            key.rep == VALUE_REP_INT_LANE && !nullable(key)) {
        MIR_label_t cold = label(c), done = label(c);
        MIR_reg_t output = em_new_reg(&c->em, "string_index", MIR_T_I64);
        owner = convert(c, owner, VALUE_REP_ITEM);
        em_branch_int53_outside(&c->em, key.reg, cold);
        move(c, output, reg(c, em_call_2(&c->em, "fn_string_ascii_at", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, key.reg), true)));
        jump(c, done); mark(c, cold);
        MirValue boxed = convert(c, key, VALUE_REP_ITEM);
        move(c, output, reg(c, em_call_2(&c->em, "fn_index", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, boxed.reg), true)));
        mark(c, done);
        // ASCII uses the shared character table; Unicode/absence retain its
        // exact character reader and poison indexes keep boxed dispatch.
        MirValue result = value(c, output, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        result.admitted_success_contract = mvp_nullable_contract(c->module, &TYPE_STRING);
        return result;
    }
    Type* owner_contract = owner.admitted_success_contract ? owner.admitted_success_contract : owner.semantic_contract;
    Type* element = lambda_array_contract_element(owner_contract);
    bool optional_key = false;
    Type* key_contract = lambda_type_nullable_lane_base(key.admitted_success_contract
        ? key.admitted_success_contract : key.semantic_contract, &optional_key);
    bool typed = mvp_array_element(owner_contract) || (element && (element == &TYPE_STRING ||
        lambda_type_nonnull_map_contract(element) || mvp_array_contract(element)));
    // a success fact also describes clean immutable arrays. Only a boxed
    // error alternative needs this join; native regions retain their proof.
    bool owner_outcome = owner.rep == VALUE_REP_ITEM && lambda_type_accepts_error(owner.semantic_contract);
    if (typed && key_contract && key_contract->type_id == LMD_TYPE_INT &&
            (key.rep == VALUE_REP_ITEM || owner_outcome)) {
        // retain original operands for ordered errors/absence on the checked
        // arm; an admitted integer success can use the ordinary native reader.
        MirValue original_owner = convert(c, owner, VALUE_REP_ITEM);
        MirValue original_key = convert(c, key, VALUE_REP_ITEM);
        MIR_label_t checked = label(c), done = label(c);
        MIR_reg_t joined = em_new_reg(&c->em, "index_outcome", MIR_T_I64);
        em_guard_container(&c->em, original_owner.reg, LMD_TYPE_RAW_POINTER, checked);
        MIR_reg_t index = em_guard_finite_int(&c->em, key, checked);
        owner.semantic_contract = owner_contract; owner.semantic_type = owner_contract->type_id;
        MirValue fast = array_read_values(c, node, binding, owner,
            value(c, index, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, node->field), pure_key, NULL, false);
        fast = convert(c, fast, VALUE_REP_ITEM);
        move(c, joined, reg(c, fast.reg)); jump(c, done); mark(c, checked);
        move(c, joined, reg(c, em_call_2(&c->em, "fn_index", MIR_T_I64,
            MIR_T_I64, reg(c, original_owner.reg), MIR_T_I64, reg(c, original_key.reg), true)));
        mark(c, done);
        Type* success = mvp_nullable_contract(c->module, element);
        Type* contract = owner_outcome || lambda_type_accepts_error(key.semantic_contract)
            ? lambda_type_union_normalized(c->module->tp->pool, success, &TYPE_ERROR) : success;
        MirValue result = value(c, joined, contract->type_id, VALUE_REP_ITEM, contract, node);
        result.admitted_success_contract = success;
        return result;
    }
    return array_read_values(c, node, binding, owner, key, pure_key, boolean_target, when_true);
}

static bool positive_power_two(int64_t value) {
    return value > 0 && (uint64_t)value <= SIZE_MAX && math_size_is_power_of_two((size_t)value);
}

struct MvpModularUpdatePlan {
    MvpBinding* owner;
    int64_t modulus, offset, factor;
};

static bool modular_array_update_plan(MvpCompiler* c, AstAssignStamNode* node,
        MvpModularUpdatePlan* plan) {
    if (c->dense_arrays || !mvp_pure_key(node->key) ||
            mvp_value_contract(c->module, node->key)->type_id != LMD_TYPE_INT) return false;
    MvpBinding* owner = mvp_local_binding(c->module, node->object);
    Type* element = owner ? mvp_array_element(owner->contract) : NULL;
    if (!owner || !owner->array_writable || (element && element != &TYPE_INT)) return false;
    AstNode* rhs = ast_unwrap_primary_to_leaf(node->value);
    if (rhs && rhs->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* cast = (AstCallNode*)rhs;
        AstNode* target = ast_unwrap_primary_to_leaf(cast->function);
        if (target && target->node_type == AST_NODE_SYS_FUNC &&
                ((AstSysFuncNode*)target)->fn_info->fn == SYSFUNC_INT)
            rhs = ast_unwrap_primary_to_leaf(mvp_call_argument(cast, 0));
    }
    if (!rhs || rhs->node_type != AST_NODE_BINARY || ((AstBinaryNode*)rhs)->op != OPERATOR_MOD) return false;
    AstBinaryNode* modulo = (AstBinaryNode*)rhs;
    MvpIntRange divisor = mvp_int_range(c->module, modulo->right);
    if (!mvp_pure_key(modulo->right) || !divisor.finite ||
            divisor.lower != divisor.upper || divisor.lower <= 0) return false;
    int64_t modulus = divisor.lower, offset, factor = 1;
    AstNode* left = ast_unwrap_primary_to_leaf(modulo->left);
    if (!left || left->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* sum = (AstBinaryNode*)left;
    MvpIntRange increment = mvp_int_range(c->module, sum->right);
    if ((sum->op != OPERATOR_ADD && sum->op != OPERATOR_SUB) ||
            !mvp_pure_key(sum->right) || !increment.finite || increment.lower != increment.upper) return false;
    offset = increment.lower;
    if (sum->op == OPERATOR_SUB) offset = -offset;
    AstNode* source = ast_unwrap_primary_to_leaf(sum->left);
    if (source && source->node_type == AST_NODE_BINARY && ((AstBinaryNode*)source)->op == OPERATOR_MUL) {
        AstBinaryNode* product = (AstBinaryNode*)source;
        MvpIntRange scale = mvp_int_range(c->module, product->right);
        if (!mvp_pure_key(product->right) || !scale.finite ||
                scale.lower != scale.upper || scale.lower <= 0) return false;
        factor = scale.lower;
        source = ast_unwrap_primary_to_leaf(product->left);
    }
    if (!source || source->node_type != AST_NODE_INDEX_EXPR) return false;
    AstFieldNode* read = (AstFieldNode*)source;
    if (mvp_local_binding(c->module, read->object) != owner ||
            !mvp_same_integer_expression(c->module, read->field, node->key)) return false;
    *plan = {owner, modulus, offset, factor};
    return true;
}

static MIR_label_t modular_array_update(MvpCompiler* c, AstAssignStamNode* node) {
    MvpModularUpdatePlan plan = {};
    if (!modular_array_update_plan(c, node, &plan)) return NULL;
    MvpBinding* owner = plan.owner;
    int64_t modulus = plan.modulus, offset = plan.offset, factor = plan.factor;
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

static MirValue assignment_value(MvpCompiler* c, AstAssignStamNode* node) {
    // a proven store-back releases its handle into the same place, without
    // creating another observer. Ordinary stores retain insertion capture.
    return node->cow_borrow_release ? observed_expression(c, node->value) : expression(c, node->value);
}

static void compact_array_write(MvpCompiler* c, MvpBinding* binding,
        MirValue key, MirValue input, MIR_label_t slow, MIR_label_t done) {
    Type* element = lambda_array_contract_element(binding->admission_contract);
    if (!element || key.rep != VALUE_REP_INT_LANE || element->is_literal || element->is_const ||
            (element->type_id != LMD_TYPE_NUM_SIZED && element->type_id != LMD_TYPE_INT64 &&
             element->type_id != LMD_TYPE_UINT64)) return;
    Type* source = input.admitted_success_contract ? input.admitted_success_contract : input.semantic_contract;
    if (lambda_numeric_kind_from_type(source) != lambda_numeric_kind_from_type(element) ||
            !mvp_boundary_proven(source, element)) return;
    ArrayNumElemType lane;
    if (!lambda_array_num_elem_type_for_contract(element, &lane)) return;
    if (input.admitted_success_contract) {
        branch(c, MIR_BEQ, slow, reg(c, operation(c, MIR_URSH, reg(c, input.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        input.semantic_contract = source; input.semantic_type = source->type_id;
    }
    MIR_reg_t owner = em_guard_container(&c->em, binding->reg, LMD_TYPE_ARRAY_NUM, slow);
    em_array_num_elem_guard(&c->em, owner, lane, slow);
    em_container_cow_guard(&c->em, owner, slow, true);
    em_array_num_bounds_check(&c->em, owner, key.reg, slow);
    int width = ELEM_TYPE_SIZE[lane >> 4];
    MIR_reg_t bits = element->type_id == LMD_TYPE_NUM_SIZED ? convert(c, input, VALUE_REP_ITEM).reg :
        convert(c, input, element->type_id == LMD_TYPE_INT64 ? VALUE_REP_I64 : VALUE_REP_U64).reg;
    // an admitted sized Item already owns the correctly rounded low bits;
    // raw stores preserve f16/f32 without another floating conversion.
    MIR_type_t storage = width == 1 ? MIR_T_U8 : width == 2 ? MIR_T_U16 : width == 4 ? MIR_T_U32 : MIR_T_I64;
    em_store_at(&c->em, em_array_element_address(&c->em, owner, key.reg, width), 0, storage, bits);
    jump(c, done);
}

static void array_write(MvpCompiler* c, AstAssignStamNode* node) {
    MvpBinding* binding = mvp_local_binding(c->module, node->object);
    MIR_label_t updated = modular_array_update(c, node);
    MirValue key = em_index_keys_use_last(node->key)
        ? subscript_key(c, node->key, read_binding(c, binding, node->object, false))
        : expression(c, node->key);
    MirValue input = assignment_value(c, node);
    input = capture_projection(c, input, node->value);
    Type* element = mvp_array_element(binding->contract);
    if (c->dense_arrays) {
        MirValue member = convert(c, input, mvp_rep(element));
        ArrayNumElemType lane;
        lambda_array_num_elem_type_for_contract(element, &lane);
        MIR_reg_t address = em_element_address(&c->em, binding->dense_items, key.reg, element == &TYPE_BOOL ? 1 : 8);
        em_store_at(&c->em, address, 0, em_numeric_storage_type(lane), member.reg);
        return;
    }
    AstNode* declaration = mvp_binding_node(binding);
    AstNode* initial = declaration && declaration->node_type == AST_NODE_VARIABLE_DECLARATOR
        ? ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)declaration)->init) : NULL;
    bool fresh_local = initial && initial->node_type == AST_NODE_ARRAY;
    if (initial && initial->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* fill = (AstCallNode*)initial;
        AstNode* target = ast_unwrap_primary_to_leaf(fill->function);
        fresh_local = fill->propagate && target && target->node_type == AST_NODE_SYS_FUNC &&
            ((AstSysFuncNode*)target)->fn_info->fn == SYSFUNC_FILL;
    }
    if (fresh_local && element && key.rep == VALUE_REP_INT_LANE &&
            !lambda_type_accepts_error(binding->contract) &&
            mvp_boundary_proven(input.semantic_contract, element) &&
            mvp_array_index_present(c->module, binding, node->key) &&
            mvp_local_storage_stable(c->module, binding, true)) {
        MirValue member = convert(c, input, mvp_rep(element));
        ArrayNumElemType lane; lambda_array_num_elem_type_for_contract(element, &lane);
        em_store_at(&c->em, em_array_element_address(&c->em, binding->reg, key.reg,
            element == &TYPE_BOOL ? 1 : 8), 0, em_numeric_storage_type(lane), member.reg);
        return;
    }
    MIR_label_t slow = label(c), done = label(c);
    compact_array_write(c, binding, key, input, slow, done);
    MirValue checked_input = input;
    Type* fast_element = element;
    MIR_reg_t writable = binding->array_writable;
    bool dynamic_cow = false;
    if (!element && key.rep == VALUE_REP_INT_LANE) {
        bool optional = false;
        Type* produced = lambda_type_nullable_lane_base(input.admitted_success_contract
            ? input.admitted_success_contract : input.semantic_contract, &optional);
        const struct { Type* type; MIR_reg_t witness; } lanes[] = {
            {&TYPE_BOOL, binding->array_bool_native}, {&TYPE_FLOAT, binding->array_float_native}};
        for (const auto& lane : lanes) if (produced == lane.type && lane.witness) {
            fast_element = lane.type; writable = lane.witness; dynamic_cow = true; break;
        }
    }
    if (fast_element && key.rep == VALUE_REP_INT_LANE && input.admitted_success_contract &&
            lambda_static_boundary_relation(input.admitted_success_contract, fast_element, false) == STATIC_BOUNDARY_PROVEN) {
        // a direct call's error remains the checked store's error; its admitted success uses the native lane.
        branch(c, MIR_BEQ, slow, reg(c, operation(c, MIR_URSH, reg(c, input.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        input.semantic_contract = input.admitted_success_contract;
        input.semantic_type = input.semantic_contract->type_id;
        input = convert(c, input, mvp_rep(fast_element));
    }
    bool nullable_number = nullable(input) && ((fast_element == &TYPE_FLOAT && input.rep == VALUE_REP_F64) ||
        (fast_element == &TYPE_INT && input.rep == VALUE_REP_INT_LANE));
    bool native = fast_element && key.rep == VALUE_REP_INT_LANE && (nullable_number ||
        lambda_static_boundary_relation(input.semantic_contract, fast_element, false) == STATIC_BOUNDARY_PROVEN);
    bool dynamic = !fast_element && binding->array_writable && key.rep == VALUE_REP_INT_LANE &&
        (input.rep == VALUE_REP_INT_LANE || input.rep == VALUE_REP_ITEM);
    if (native || dynamic) {
        MirValue lane_value;
        if (dynamic) lane_value = value(c, em_guard_finite_int(&c->em, input, slow),
            LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT);
        else { branch_null(c, input, slow); lane_value = convert(c, input, mvp_rep(fast_element)); }
        ArrayNumElemType lane;
        lambda_array_num_elem_type_for_contract(dynamic ? &TYPE_INT : fast_element, &lane);
        branch(c, MIR_BEQ, slow, reg(c, writable), imm(c, 0));
        // erased bool/float owners retain their receiving contract. A live
        // physical lane plus exclusive writable storage permits this store.
        if (dynamic_cow) em_container_cow_guard(&c->em, binding->reg, slow, true);
        branch(c, MIR_UBGE, slow, reg(c, key.reg), reg(c, binding->array_length));
        int width = fast_element == &TYPE_BOOL ? 1 : 8;
        MIR_reg_t address = c->loop_array == binding ?
            em_element_address(&c->em, c->loop_array_items, key.reg, width) :
            em_array_element_address(&c->em, binding->reg, key.reg, width);
        em_store_at(&c->em, address, 0, em_numeric_storage_type(lane), lane_value.reg);
        jump(c, done);
    }
    mark(c, slow);
    key = convert(c, key, VALUE_REP_ITEM); input = convert(c, checked_input, VALUE_REP_ITEM);
    MIR_reg_t result;
    if (binding->admission_contract == &TYPE_MAP) {
        result = em_call_3(&c->em, "index_assign_cow", MIR_T_I64,
            MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, input.reg), true);
    } else if (ast_declared_type_is_map(binding->admission_contract) ||
            lambda_type_nonnull_map_contract(binding->admission_contract)) {
        // computed map keys share the checked map writer; array_set_cow
        // cannot publish a map replacement (S9.2.2, D4.2.5).
        result = em_call_5(&c->em, "lambda_map_set_checked", MIR_T_I64,
            MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, input.reg),
            MIR_T_P, imm(c, (intptr_t)binding->admission_contract),
            MIR_T_P, imm(c, (intptr_t)"typed map member assignment"), true);
    } else if (lambda_array_contract_element(binding->admission_contract)) {
        MIR_reg_t type = em_load_const(&c->em, em_add_const(&c->em, binding->admission_contract), MIR_T_P);
        if (!native && element && lambda_array_contract_is_plain(binding->contract) &&
                !lambda_type_counts_array_length(binding->contract)) {
            // compiler-owned certificates need not share the runtime interner's
            // identity. Reuse its primitive carrier proof, not a type walk on
            // every erased-value write (D4.3.3, S11.4.1v5).
            MIR_type_t types[] = {MIR_T_I64, MIR_T_I64, MIR_T_I64, MIR_T_P, MIR_T_P,
                MIR_T_P, MIR_T_P, MIR_T_I64, MIR_T_I64};
            MIR_op_t args[] = {reg(c, binding->reg), reg(c, key.reg), reg(c, input.reg), reg(c, type),
                imm(c, (intptr_t)element), imm(c, 0), imm(c, (intptr_t)"typed array element assignment"), imm(c, 0), imm(c, 1)};
            result = em_call_with_args(&c->em, "lambda_array_set_checked_preplanned", MIR_T_I64, 9, types, args, true);
        } else {
            result = em_call_5(&c->em, "lambda_array_set_checked_item", MIR_T_I64,
                MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, input.reg),
                MIR_T_P, reg(c, type), MIR_T_P, imm(c, (intptr_t)"typed array element assignment"), true);
        }
    } else {
        // an erased receiver can be a map or either element face as well as
        // an array. Dispatch by its actual key domain (S8.2.1v4, S12.2.1v3).
        result = em_call_3(&c->em, "index_assign_cow", MIR_T_I64,
            MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, input.reg), true);
    }
    check_store_result(c, result, binding, node);
    move(c, binding->reg, reg(c, result));
    refresh_array(c, binding);
    if (c->loop_array == binding)
        move(c, c->loop_array_items, reg(c, em_load_at(&c->em, binding->reg,
            LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_I64, "refreshed_loop_items")));
    publish_var_binding(c, binding);
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
    } else if (t->is_literal && (t->type_id == LMD_TYPE_INT64 || t->type_id == LMD_TYPE_UINT64)) {
        // native literals need only their bits; boxed uses retain the Input
        // constant owner or adopt a destination home (D2.3.4, D6.2.3).
        MIR_reg_t raw = constant(c, t->type_id == LMD_TYPE_INT64
            ? (uint64_t)((TypeInt64*)t)->int64_val : ((TypeUint64*)t)->uint64_val);
        return value(c, raw, t->type_id, mvp_rep(t), t, node);
    } else if (t->is_literal && t->type_id == LMD_TYPE_COMPLEX) {
        TypeComplex* complex = (TypeComplex*)t;
        r = em_call_2(&c->em, "complex_new", MIR_T_I64,
            MIR_T_D, MIR_new_double_op(c->em.ctx, complex->real),
            MIR_T_D, MIR_new_double_op(c->em.ctx, complex->imag), true);
        return convert(c, value(c, r, LMD_TYPE_COMPLEX, VALUE_REP_ITEM, t, node), mvp_rep(t));
    } else if (t->is_literal && (t->type_id == LMD_TYPE_STRING || t->type_id == LMD_TYPE_DECIMAL ||
            t->type_id == LMD_TYPE_SYMBOL || t->type_id == LMD_TYPE_BINARY)) {
        r = em_load_const(&c->em, ((TypeConst*)t)->const_index, MIR_T_P);
    } else if (t->is_literal && t->type_id == LMD_TYPE_DTIME) {
        // publish packed parser bits through the owned datetime ABI (D2.3.4-D2.3.5).
        r = em_call_1(&c->em, "push_k", MIR_T_I64, MIR_T_I64,
            imm(c, ((TypeDateTime*)t)->datetime.int64_val), true);
        return value(c, r, LMD_TYPE_DTIME, VALUE_REP_ITEM, t, node);
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
    bool optional = false;
    Type* success = lambda_type_nullable_lane_base(v.admitted_success_contract, &optional);
    // callers preserve the Error arm before consuming truth; bool? has only
    // one truthy Item, including when its normal arm is absent (S3.4).
    if (v.rep == VALUE_REP_ITEM && success == &TYPE_BOOL)
        return operation(c, MIR_EQ, reg(c, v.reg), imm(c, ITEM_TRUE));
    if (v.semantic_type != LMD_TYPE_BOOL) {
        v = convert(c, v, VALUE_REP_ITEM);
        MIR_reg_t classified = operation(c, MIR_EQ, reg(c, v.reg), imm(c, ITEM_TRUE));
        MIR_label_t done = label(c);
        branch(c, MIR_BNE, done, reg(c, classified), imm(c, 0));
        // erased primitive reads commonly produce boxed bools. Their exact
        // words decide truth without changing other families or Error (S3.1v2).
        branch(c, MIR_BEQ, done, reg(c, v.reg), imm(c, ITEM_FALSE));
        move(c, classified, reg(c, em_call_1(&c->em, "is_truthy", MIR_T_I64,
            MIR_T_I64, reg(c, v.reg), true)));
        // C Bool returns occupy one byte; predicate selection compares the
        // exact BOOL_TRUE value, including after an ordinary Error result.
        em_emit_insn(&c->em, MIR_new_insn(c->em.ctx, MIR_UEXT8, reg(c, classified), reg(c, classified)));
        mark(c, done);
        return classified;
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
            !positive_power_two(divisor) || mvp_value_contract(c->module, mod->left) != &TYPE_INT) return 0;
    // zero residue is a bit test for both signs; the signed remainder never escapes (S6.1.2v2).
    MirValue a = expression(c, mod->left);
    bool finite = mvp_int_range(c->module, mod->left).finite;
    MIR_label_t slow = finite ? NULL : label(c), done = finite ? NULL : label(c);
    if (slow) em_branch_int53_outside(&c->em, a.reg, slow);
    MIR_reg_t masked = operation(c, MIR_AND, reg(c, a.reg), imm(c, divisor - 1));
    if (slow) {
        jump(c, done); mark(c, slow);
        // sentinels retain the shared remainder semantics; its NaN is nonzero
        // for the enclosing equality-to-zero test (S4.1.2/S6.1.2v2).
        move(c, masked, reg(c, em_call_3(&c->em, "lambda_int_lane_divmod_slow", MIR_T_I64,
            MIR_T_I64, reg(c, a.reg), MIR_T_I64, imm(c, divisor), MIR_T_I64, imm(c, 1), true)));
        mark(c, done);
    }
    return masked;
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

static MIR_label_t erased_bool_array_branch(MvpCompiler* c, AstNode* node,
        MIR_label_t target, bool when_true) {
    if (c->dense_arrays || !node || node->node_type != AST_NODE_INDEX_EXPR) return NULL;
    AstFieldNode* read = (AstFieldNode*)node;
    MvpBinding* b = mvp_local_binding(c->module, read->object);
    if (!b || !b->array_bool_native || (read->field && read->field->next) ||
            !mvp_pure_key(read->field) || mvp_value_contract(c->module, read->field)->type_id != LMD_TYPE_INT) return NULL;
    MIR_label_t checked = label(c), absent = label(c), done = label(c);
    branch(c, MIR_BEQ, checked, reg(c, b->array_bool_native), imm(c, 0));
    MirValue key = convert(c, expression(c, read->field), VALUE_REP_INT_LANE);
    branch(c, MIR_UBGE, absent, reg(c, key.reg), reg(c, b->array_length));
    MIR_reg_t byte = em_load_at(&c->em, em_array_element_address(&c->em, b->reg, key.reg, 1),
        0, MIR_T_U8, "bool_element");
    branch(c, when_true ? MIR_BNE : MIR_BEQ, target, reg(c, byte), imm(c, 0));
    jump(c, done); mark(c, absent);
    // a guarded physical bool lane contains no Error; an index miss is falsy
    // absence. Other storage and owner outcomes keep ordinary truth demand.
    if (!when_true) jump(c, target);
    else jump(c, done);
    mark(c, checked);
    return done;
}

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
    MIR_label_t done = erased_bool_array_branch(c, node, target, when_true);
    if (!done) done = guarded_array_condition(c, source, target, when_true);
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
    MvpControl loop = loop_control(c);
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
    LambdaMvpModule* m = c->module;
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) return NULL;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, node);
    bool boxed = false;
    for (AstNodeId id = root; id < ast_index_subtree_end(index, root); id++) {
        MvpBinding* b = mvp_local_binding(m, index->nodes[id]);
        if (!b || b->owner != c->function->id || b->rep != VALUE_REP_ITEM || !b->reg ||
                !b->entry || b->entry->lambda_shared_local || b->capture_home || b->var_home ||
                b->module_ref || b->module_slot || b->state_name ||
                mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR ||
                ast_index_node_descends(index, ast_index_find(index, mvp_binding_node(b)), root) ||
                ((AstDeclaratorNode*)mvp_binding_node(b))->declared_type) continue;
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(lambda_type_remove_error(m->tp->pool, b->contract), &optional);
        if (base != &TYPE_INT) continue;
        // this is an iteration hypothesis, not a receiving contract. Entry
        // guards discharge absence/Error before the existing finite proof.
        // Only declarations outside this loop have an initialized entry value.
        b->contract = &TYPE_INT; b->rep = VALUE_REP_INT_LANE; b->range = {}; boxed = true;
    }
    MvpNumericLoopPlan plan = {};
    if (!mvp_numeric_loop_plan(m, node, &plan)) {
        restore_bindings(m, saved); mem_free(saved); return NULL;
    }
    MvpControl loop = loop_control(c);
    MIR_label_t checked = label(c);
    MIR_label_t entry_miss = boxed ? label(c) : checked;
    MIR_label_t joined = boxed ? label(c) : loop.done;
    MIR_label_t publish = boxed ? label(c) : NULL;
    MIR_reg_t completed = boxed ? em_new_reg(&c->em, "numeric_completed", MIR_T_I64) : 0;
    if (boxed) for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (saved[i].rep != VALUE_REP_ITEM || b->rep != VALUE_REP_INT_LANE) continue;
        b->reg = em_guard_finite_int(&c->em,
            value(c, saved[i].reg, LMD_TYPE_ANY, VALUE_REP_ITEM, saved[i].contract), entry_miss);
        value(c, b->reg, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT);
    }
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
    if (boxed) {
        mark(c, loop.done); move(c, completed, imm(c, 1)); jump(c, publish);
        mark(c, checked); move(c, completed, imm(c, 0));
        mark(c, publish);
        for (uint32_t i = 0; i < m->binding_count; i++) {
            MvpBinding* b = &m->bindings[i];
            if (saved[i].rep != VALUE_REP_ITEM || b->rep != VALUE_REP_INT_LANE) continue;
            // every completed native step stays finite. Publish once on
            // exit or before resuming the checked loop at its current state.
            move(c, saved[i].reg, reg(c, em_box_finite_int_item(&c->em, b->reg)));
        }
        branch(c, MIR_BNE, joined, reg(c, completed), imm(c, 0));
    }
    restore_bindings(m, saved); mem_free(saved);
    mark(c, entry_miss);
    return joined;
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
    if (nullable_float) {
        MirValue* inputs[] = {&a, &b};
        for (MirValue* input : inputs) {
            branch_null(c, *input, absent);
            bool optional = false;
            Type* present = lambda_type_nullable_lane_base(input->semantic_contract, &optional);
            // conversion consumes the present arm without repeating its null guard.
            if (optional) input->semantic_contract = present;
        }
    }
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
    bool optional = false;
    t = lambda_type_nullable_lane_base(t, &optional);
    return t ? t->type_id : v.semantic_type;
}

static LambdaNumericKind numeric_kind(MirValue value) {
    return lambda_numeric_kind_from_type(type_field_unwrap_simple_decl(
        value.admitted_success_contract ? value.admitted_success_contract : value.semantic_contract));
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
        ordinal && !mvp_array_index_present(c->module, binding, n->field) ? c->module->nullable_int : &TYPE_INT, n);
}

static bool string_comparison_inputs(MvpCompiler* c, AstBinaryNode* n, MirValue* left, MirValue* right) {
    if (n->op != OPERATOR_EQ && n->op != OPERATOR_NE) return false;
    AstFieldNode* a = mvp_string_index(c->module, n->left);
    AstFieldNode* b = mvp_string_index(c->module, n->right);
    if (!a || !b) {
        AstFieldNode* indexed = a ? a : b;
        TypeString* literal = indexed ? ast_string_literal_type(a ? n->right : n->left) : NULL;
        if (!literal || !literal->string->len) return false;
        uint64_t word = fn_string_char_word({.item = s2it(literal->string)}, 0);
        if ((word >> 32) != literal->string->len) return false;
        // ASCII literals compare directly with an ordinal lane; its null
        // sentinel still distinguishes absence from NUL. Unicode literals
        // retain the complete UTF-8 word (S7.1.1v3).
        bool ordinal = literal->string->len == 1 && (uint8_t)literal->string->chars[0] < 128;
        MirValue character = string_character(c, indexed, ordinal);
        MirValue expected = value(c, constant(c, ordinal ? (uint8_t)literal->string->chars[0] : word),
            LMD_TYPE_INT, ordinal ? VALUE_REP_INT_LANE : VALUE_REP_I64, &TYPE_INT);
        *left = a ? character : expected; *right = a ? expected : character;
        return true;
    }
    MvpBinding* x = mvp_local_binding(c->module, a->object);
    MvpBinding* y = mvp_local_binding(c->module, b->object);
    // present ASCII bytes need no presence word; checked siblings keep NUL
    // distinct from absence and compare complete Unicode code points (S7.1.1v3).
    bool bytes = x && y && x->dense_items && y->dense_items;
    *left = string_character(c, a, bytes); *right = string_character(c, b, bytes);
    return true;
}

static MirValue binary(MvpCompiler* c, AstBinaryNode* n);

static void join_operand_errors(MvpCompiler* c, MirValue* operands, int count,
        MIR_reg_t joined, MIR_label_t done) {
    for (int i = 0; i < count; i++) {
        MirValue* value = &operands[i];
        if (!value->admitted_success_contract && !lambda_type_accepts_error(value->semantic_contract)) continue;
        *value = convert(c, *value, VALUE_REP_ITEM);
        move(c, joined, reg(c, value->reg));
        branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_URSH, reg(c, value->reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
    }
}

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
    // which the carrier guard excludes (D2.3.5, D4.3.3).
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

static LambdaNumericKind numeric_node_kind(MvpCompiler* c, AstNode* source, int depth = 0) {
    AstNode* node = ast_unwrap_primary_to_leaf(source);
    if (!node || depth > 32) return LAMBDA_NUM_INVALID;
    MvpBinding* binding = mvp_local_binding(c->module, node);
    Type* contract = binding ? binding->producer_success_contract
        ? binding->producer_success_contract : binding->admission_contract
        : mvp_value_contract(c->module, node);
    LambdaNumericKind kind = lambda_numeric_kind_from_type(type_field_unwrap_simple_decl(contract));
    if (kind != LAMBDA_NUM_INVALID) return kind;
    // this is a success-domain query only; emitted guards keep producer
    // errors before any packed payload is decoded (D2.3.5, S7.9.3).
    if (node->node_type == AST_NODE_UNARY && ((AstUnaryNode*)node)->op == OPERATOR_PROPAGATE)
        return numeric_node_kind(c, ((AstUnaryNode*)node)->operand, depth + 1);
    LambdaNumericOpFamily family;
    AstNode* left; AstNode* right;
    if (node->node_type == AST_NODE_BINARY) {
        AstBinaryNode* binary = (AstBinaryNode*)node;
        int operation = mvp_decimal_operation(binary->op);
        if (operation < 0) return LAMBDA_NUM_INVALID;
        family = (LambdaNumericOpFamily)operation; left = binary->left; right = binary->right;
    } else if (node->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)node;
        AstNode* target = ast_unwrap_primary_to_leaf(call->function);
        if (!target || target->node_type != AST_NODE_SYS_FUNC) return LAMBDA_NUM_INVALID;
        SysFunc fn = ((AstSysFuncNode*)target)->fn_info->fn;
        left = mvp_call_argument(call, 0); right = mvp_call_argument(call, 1);
        if (fn == SYSFUNC_BNOT) return numeric_node_kind(c, left, depth + 1);
        if (fn == SYSFUNC_INT) return LAMBDA_NUM_INT;
        if (fn == SYSFUNC_FLOAT) return LAMBDA_NUM_FLOAT;
        if (fn == SYSFUNC_BAND || fn == SYSFUNC_BOR || fn == SYSFUNC_BXOR) family = LAMBDA_NUM_OP_BITWISE;
        else if (fn == SYSFUNC_SHL || fn == SYSFUNC_SHR) family = LAMBDA_NUM_OP_SHIFT;
        else return LAMBDA_NUM_INVALID;
    } else return LAMBDA_NUM_INVALID;
    LambdaNumericDecision decision = lambda_numeric_classify(family,
        numeric_node_kind(c, left, depth + 1), numeric_node_kind(c, right, depth + 1));
    return decision.valid ? decision.result : LAMBDA_NUM_INVALID;
}

static void guarded_binary_inputs(MvpCompiler* c, const MirValue* original,
        const MirValue* boxed, Type* domain, MIR_label_t miss, MirValue* inputs) {
    ValueRep rep = mvp_rep(domain);
    for (int i = 0; i < 2; i++) {
        if (domain == &TYPE_INT) {
            inputs[i] = value(c, em_guard_finite_int(&c->em, original[i], miss),
                LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT);
            continue;
        }
        branch(c, MIR_BEQ, miss,
            reg(c, em_exact_item_type_test(&c->em, boxed[i].reg, domain->type_id)), imm(c, 0));
        // the tag excludes absence/Error before reusing a native carrier.
        MIR_reg_t lane = original[i].rep == rep ? original[i].reg :
            convert(c, value(c, boxed[i].reg, domain->type_id, VALUE_REP_ITEM, domain), rep).reg;
        inputs[i] = value(c, lane, domain->type_id, rep, domain);
    }
}

static MirValue binary_operand(MvpCompiler* c, AstNode* source, AstNode* other, Operator op) {
    // predicates publish no operand. A pure sibling cannot mutate an earlier
    // borrowed owner before the comparison observes it (S7.9.3, S9.1.2).
    bool predicate = op == OPERATOR_EQ || op == OPERATOR_NE || op == OPERATOR_LT ||
        op == OPERATOR_LE || op == OPERATOR_GT || op == OPERATOR_GE ||
        op == OPERATOR_AT || op == OPERATOR_IN || op == OPERATOR_OF;
    bool observes = predicate && mvp_pure_key(other);
    return observes ? observed_expression(c, source) : expression(c, source);
}

static MirValue bool_call_outcome(MvpCompiler* c, MIR_reg_t raw, AstNode* source,
        MIR_reg_t result = 0, MIR_label_t done = NULL) {
    if (!result) result = em_new_reg(&c->em, "bool_outcome", MIR_T_I64);
    if (!done) done = label(c);
    MIR_label_t invalid = label(c);
    // C Bool defines one byte; its error sentinel is an Error Item, not bool? absence.
    raw = operation(c, MIR_AND, reg(c, raw), imm(c, UINT8_MAX));
    branch(c, MIR_BGE, invalid, reg(c, raw), imm(c, BOOL_ERROR));
    move(c, result, reg(c, operation(c, MIR_OR, reg(c, raw), imm(c, (uint64_t)LMD_TYPE_BOOL << 56))));
    jump(c, done); mark(c, invalid); move(c, result, imm(c, ITEM_ERROR)); mark(c, done);
    MirValue outcome = value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, source);
    outcome.admitted_success_contract = &TYPE_BOOL;
    return outcome;
}

static MIR_reg_t constrained_type_test(MvpCompiler* c, TypeConstrained* type, MIR_reg_t subject);

static MirValue binary(MvpCompiler* c, AstBinaryNode* n) {
    if (n->op == OPERATOR_IS_NAN) {
        MirValue subject = convert(c, observed_expression(c, n->left), VALUE_REP_ITEM);
        return value(c, em_call_1(&c->em, "fn_is_nan", MIR_T_I64, MIR_T_I64, reg(c, subject.reg), true),
            LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
    }
    if (n->op == OPERATOR_IS) {
        Type* type = mvp_match_type(n->right);
        if (!type && n->right->type && n->right->type->type_id == LMD_TYPE_FLOAT && n->right->type->is_literal &&
                isnan(((TypeFloat*)n->right->type)->double_val)) {
            MirValue subject = convert(c, observed_expression(c, n->left), VALUE_REP_ITEM);
            return value(c, em_call_1(&c->em, "fn_is_nan", MIR_T_I64, MIR_T_I64, reg(c, subject.reg), true),
                LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
        }
        if (TypeConstrained* constrained = ast_constrained_type(n->right)) {
            MirValue subject = convert(c, observed_expression(c, n->left), VALUE_REP_ITEM);
            return value(c, constrained_type_test(c, constrained, subject.reg),
                LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
        }
        MirValue subject = convert(c, observed_expression(c, n->left), VALUE_REP_ITEM);
        if (type) {
            MIR_reg_t matches;
            Type* normal = subject.admitted_success_contract ? subject.admitted_success_contract :
                lambda_type_remove_error(c->module->tp->pool, subject.semantic_contract);
            if (type != &TYPE_ERROR && mvp_membership_proven(normal, type)) {
                // the proved success domain is contained in this pattern;
                // an unresolved Error still fails a non-error membership test.
                matches = operation(c, MIR_NE,
                    reg(c, operation(c, MIR_URSH, reg(c, subject.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
            } else if (mvp_exact_membership_domain(type))
                matches = em_semantic_value_type_test(&c->em, subject.reg, type);
            else {
                // a carrier guard cannot implement numeric subtyping or the
                // range member of array (S4.4.1/S11.1.4).
                MirValue pattern = convert(c, expression(c, n->right), VALUE_REP_ITEM);
                matches = em_pattern_value_test(&c->em, subject.reg, pattern.reg, LMD_TYPE_TYPE);
            }
            return value(c, matches, LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
        }
        // first-class and structural types keep their full graph and the shared membership semantics.
        MirValue pattern = convert(c, expression(c, n->right), VALUE_REP_ITEM);
        return bool_call_outcome(c, em_call_2(&c->em, "fn_is", MIR_T_I64,
            MIR_T_I64, reg(c, subject.reg), MIR_T_I64, reg(c, pattern.reg), true), n);
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
        AstNode* right = ast_unwrap_primary_to_leaf(n->right);
        AstNode* element = right && right->node_type == AST_NODE_ARRAY ? ((AstArrayNode*)right)->item : NULL;
        int count = ast_linked_node_count(element);
        bool fresh = count > 0 && count <= LAMBDA_MAX_FUNCTION_ARGS;
        for (AstNode* item = element; item && fresh; item = item->next)
            fresh = item->node_type != AST_NODE_SPREAD && item->node_type != AST_NODE_FOR_EXPR &&
                item->node_type != AST_NODE_VARIABLE_DECLARATOR;
        if (c->consuming_array_join != (AstNode*)n && fresh) {
            MirValue owner = convert(c, expression(c, n->left), VALUE_REP_ITEM);
            MirValue members[LAMBDA_MAX_FUNCTION_ARGS]; int i = 0;
            for (AstNode* item = element; item; item = item->next)
                members[i++] = convert(c, capture_projection(c, expression(c, item), item), VALUE_REP_ITEM);
            // the shared helper copies directly into the final owner. Its
            // checked fallback preserves list splicing and numeric promotion
            // without allocating a disposable one-item RHS (S10.6.1).
            // all RHS members run before publication, including their errors
            // and observers of the old owner (S7.9.3, S9.1.2).
            for (int j = 0; j < count; j++) owner.reg = em_call_2(&c->em, "fn_join_fresh_array_item", MIR_T_I64,
                MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, members[j].reg), true);
            Type* result = lambda_type_union_normalized(c->module->tp->pool, &TYPE_ARRAY, &TYPE_ERROR);
            return value(c, owner.reg, LMD_TYPE_ANY, VALUE_REP_ITEM, result, n);
        }
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
    MirValue a = c->consuming_array_join == (AstNode*)n
        ? observed_expression(c, n->left) : binary_operand(c, n->left, n->right, n->op);
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
        // selection preserves both complete operand domains, including their
        // Error alternatives; a clean text join can keep its pointer carrier.
        Type* selected = booleans ? logical_contract :
            mvp_join_value_contracts(c->module, a.semantic_contract, b.semantic_contract);
        MirValue result = value(c, r, selected->type_id,
            booleans ? VALUE_REP_I64 : VALUE_REP_ITEM, selected, n);
        // either selected normal value keeps its producer domain; the Error
        // branch above still bypasses truth and preserves the original Item.
        if (!booleans) result.admitted_success_contract = normal_join_contract(c, a, b);
        return result;
    }
    MirValue b = binary_operand(c, n->right, n->left, n->op);
    LambdaNumericKind left_wide = numeric_kind(a), right_wide = numeric_kind(b);
    if ((n->op == OPERATOR_ADD || n->op == OPERATOR_SUB || n->op == OPERATOR_MUL) &&
            left_wide == right_wide && (left_wide == LAMBDA_NUM_I64 || left_wide == LAMBDA_NUM_U64) &&
            !nullable(a) && !nullable(b)) {
        // equal wide domains wrap in one machine word (S4.1.5). Only a
        // boxed observer needs payload storage; loop-carried values stay raw.
        ValueRep rep = left_wide == LAMBDA_NUM_I64 ? VALUE_REP_I64 : VALUE_REP_U64;
        Type* contract = left_wide == LAMBDA_NUM_I64 ? &TYPE_INT64 : &TYPE_UINT64;
        MirValue operands[] = {a, b};
        bool outcome = a.admitted_success_contract || b.admitted_success_contract;
        MIR_reg_t joined = outcome ? em_new_reg(&c->em, "wide_outcome", MIR_T_I64) : 0;
        MIR_label_t done = outcome ? label(c) : NULL;
        if (outcome) join_operand_errors(c, operands, 2, joined, done);
        for (MirValue& operand : operands) {
            operand.semantic_contract = contract; operand.semantic_type = contract->type_id;
            operand = convert(c, operand, rep);
        }
        MirValue computed = value(c, operation(c, n->op == OPERATOR_ADD ? MIR_ADD :
            n->op == OPERATOR_SUB ? MIR_SUB : MIR_MUL, reg(c, operands[0].reg), reg(c, operands[1].reg)),
            contract->type_id, rep, contract, n);
        if (!outcome) return computed;
        move(c, joined, reg(c, convert(c, computed, VALUE_REP_ITEM).reg)); mark(c, done);
        MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
        result.admitted_success_contract = contract;
        return result;
    }
    if (n->op == OPERATOR_EQ || n->op == OPERATOR_NE) {
        bool left_optional = false, right_optional = false;
        Type* left = lambda_type_nullable_lane_base(a.admitted_success_contract ? a.admitted_success_contract : a.semantic_contract, &left_optional);
        Type* right = lambda_type_nullable_lane_base(b.admitted_success_contract ? b.admitted_success_contract : b.semantic_contract, &right_optional);
        if (left && right && left->type_id == right->type_id &&
                (left->type_id == LMD_TYPE_STRING || left->type_id == LMD_TYPE_SYMBOL)) {
            bool outcome = a.admitted_success_contract || b.admitted_success_contract;
            MIR_reg_t joined = outcome ? em_new_reg(&c->em, "text_outcome", MIR_T_I64) : 0;
            MIR_label_t done = outcome ? label(c) : NULL;
            MirValue operands[] = {a, b};
            if (outcome) join_operand_errors(c, operands, 2, joined, done);
            // success facts select the pointer comparison only after the
            // error lane has been preserved in source order (S7.9.3).
            operands[0].semantic_contract = a.admitted_success_contract ? a.admitted_success_contract : a.semantic_contract;
            operands[1].semantic_contract = b.admitted_success_contract ? b.admitted_success_contract : b.semantic_contract;
            a = convert(c, operands[0], VALUE_REP_RAW_GC_POINTER);
            b = convert(c, operands[1], VALUE_REP_RAW_GC_POINTER);
            TypeString* literal = left->type_id == LMD_TYPE_STRING ? ast_string_literal_type(n->right) : NULL;
            MIR_reg_t compared;
            if (literal && literal->string->len <= MIR_LITERAL_STRING_COMPARE_MAX_BYTES) {
                compared = em_string_literal_pointer_eq(&c->em, a.reg, b.reg, literal->string);
                if (n->op == OPERATOR_NE) compared = operation(c, MIR_EQ, reg(c, compared), imm(c, 0));
            } else compared = em_text_pointer_compare(&c->em, a.reg, b.reg, left->type_id, n->op);
            if (!outcome) return value(c, compared, LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
            move(c, joined, reg(c, operation(c, MIR_OR, reg(c, compared), imm(c, (uint64_t)LMD_TYPE_BOOL << 56))));
            mark(c, done);
            MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
            result.admitted_success_contract = &TYPE_BOOL;
            return result;
        }
        TypeString* literal = ast_string_literal_type(n->right);
        bool literal_right = literal != NULL;
        if (!literal) literal = ast_string_literal_type(n->left);
        if (literal && literal->string->len <= MIR_LITERAL_STRING_COMPARE_MAX_BYTES) {
            a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
            MIR_reg_t text = literal_right ? b.reg : a.reg;
            MirValue expected = value(c, text, LMD_TYPE_STRING, VALUE_REP_ITEM, &TYPE_STRING);
            expected = convert(c, expected, VALUE_REP_RAW_GC_POINTER);
            MIR_reg_t compared = em_string_literal_item_compare(&c->em,
                literal_right ? a.reg : b.reg, expected.reg, literal->string, n->op, true);
            MirValue result = value(c, compared, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
            result.admitted_success_contract = &TYPE_BOOL;
            return result;
        }
    }
    if ((n->op == OPERATOR_ADD || n->op == OPERATOR_SUB || n->op == OPERATOR_MUL) &&
            numeric_kind(a) == LAMBDA_NUM_U32 && numeric_kind(b) == LAMBDA_NUM_U32) {
        bool outcomes = a.admitted_success_contract || b.admitted_success_contract;
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        MirValue result = value(c, em_u32_binary(&c->em, n->op, a.reg, b.reg, true, true),
            outcomes ? LMD_TYPE_ANY : LMD_TYPE_NUM_SIZED, VALUE_REP_ITEM, outcomes ? &TYPE_ANY : &TYPE_U32, n);
        if (outcomes) result.admitted_success_contract = &TYPE_U32;
        return result;
    }
    if (n->op == OPERATOR_AT || n->op == OPERATOR_IN || n->op == OPERATOR_OF || n->op == OPERATOR_SUBTYPE) {
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        MIR_reg_t result = em_new_reg(&c->em, "membership", MIR_T_I64);
        MIR_label_t done = label(c);
        // membership's C ABI returns a byte, while errors retain their Item
        // identity in left-to-right order (S7.9.3, S8.1.1v2).
        MirValue operands[] = {a, b};
        join_operand_errors(c, operands, 2, result, done);
        const char* helper = em_boxed_binary_helper(n->op);
        MIR_reg_t raw = em_call_2(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, a.reg), MIR_T_I64, reg(c, b.reg), true);
        return bool_call_outcome(c, raw, n, result, done);
    }
    bool bool_left = mvp_boundary_proven(a.semantic_contract, &TYPE_BOOL);
    bool bool_right = mvp_boundary_proven(b.semantic_contract, &TYPE_BOOL);
    if ((n->op == OPERATOR_EQ || n->op == OPERATOR_NE) &&
            ((bool_left && numeric_success(b) != LMD_TYPE_BOOL) ||
             (bool_right && numeric_success(a) != LMD_TYPE_BOOL))) {
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        MIR_reg_t result = em_new_reg(&c->em, "bool_equality", MIR_T_I64);
        MIR_label_t done = label(c);
        MirValue operands[] = {a, b};
        // bool is a separate equality family (S5.1.1v2). Keep direct Error
        // precedence before comparing the complete tagged scalar word.
        join_operand_errors(c, operands, 2, result, done);
        MIR_reg_t compared = operation(c, n->op == OPERATOR_EQ ? MIR_EQ : MIR_NE, reg(c, a.reg), reg(c, b.reg));
        move(c, result, reg(c, operation(c, MIR_OR, reg(c, compared), imm(c, (uint64_t)LMD_TYPE_BOOL << 56))));
        mark(c, done);
        MirValue answer = value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
        answer.admitted_success_contract = &TYPE_BOOL;
        return answer;
    }
    bool vector_comparison = n->op >= OPERATOR_ELEM_EQ && n->op <= OPERATOR_ELEM_GE;
    if (vector_comparison || n->op == OPERATOR_POW || n->op == OPERATOR_UNION ||
            n->op == OPERATOR_INTERSECT || n->op == OPERATOR_EXCLUDE || n->op == OPERATOR_REF_EQ) {
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        // keyword comparisons and scalar equality have separate kernels;
        // masks must stay containers rather than implicit truth (S10.2.2v2).
        MIR_reg_t result = vector_comparison
            ? em_call_3(&c->em, "vec_cmp", MIR_T_I64, MIR_T_I64, reg(c, a.reg),
                MIR_T_I64, reg(c, b.reg), MIR_T_I64, imm(c, n->op - OPERATOR_ELEM_EQ), true)
            : em_call_2(&c->em, em_boxed_binary_helper(n->op), MIR_T_I64,
                MIR_T_I64, reg(c, a.reg), MIR_T_I64, reg(c, b.reg), true);
        return value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
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
    if ((n->op == OPERATOR_EQ || n->op == OPERATOR_NE) &&
            (a.semantic_type == LMD_TYPE_NULL || b.semantic_type == LMD_TYPE_NULL)) {
        MirValue subject = a.semantic_type == LMD_TYPE_NULL ? b : a;
        if (subject.rep != VALUE_REP_ITEM && subject.rep != VALUE_REP_RAW_NON_GC_POINTER) {
            // a native scalar cannot be an array comparison or Error. Test
            // its lane marker without boxing a dead collecting fallback.
            MIR_reg_t absent = !nullable(subject) ? constant(c, 0) : subject.rep == VALUE_REP_F64
                ? em_float_lane_null_test(&c->em, subject.reg)
                : operation(c, MIR_EQ, reg(c, subject.reg), imm(c, subject.rep == VALUE_REP_INT_LANE
                    ? INT_LANE_NULL : subject.rep == VALUE_REP_I64 && subject.semantic_type == LMD_TYPE_BOOL ? BOOL_ERROR : 0));
            if (n->op == OPERATOR_NE) absent = operation(c, MIR_EQ, reg(c, absent), imm(c, 0));
            return value(c, absent, LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, n);
        }
        subject = convert(c, subject, VALUE_REP_ITEM);
        Type* contract = mvp_value_contract(c->module, n);
        MirValue compared = value(c, em_item_null_compare(&c->em, subject.reg, n->op, true),
            contract->type_id, VALUE_REP_ITEM, contract, n);
        compared.admitted_success_contract = &TYPE_BOOL;
        return compared;
    }
    if (n->op == OPERATOR_JOIN) {
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        MirValue result = value(c, em_call_2(&c->em, c->consuming_array_join == (AstNode*)n
            ? "fn_join_consume_open_array" : "fn_join", MIR_T_I64, MIR_T_I64, reg(c, a.reg),
            MIR_T_I64, reg(c, b.reg), true), n->type->type_id, VALUE_REP_ITEM, n->type, n);
        Type* left = lambda_type_remove_error(c->module->tp->pool, a.semantic_contract);
        Type* right = lambda_type_remove_error(c->module->tp->pool, b.semantic_contract);
        if (mvp_array_contract(left) && mvp_array_contract(right)) result.admitted_success_contract = &TYPE_ARRAY;
        return result;
    }
    TypeId left = numeric_success(a), right = numeric_success(b);
    bool numeric = (left == LMD_TYPE_INT || left == LMD_TYPE_FLOAT) &&
        (right == LMD_TYPE_INT || right == LMD_TYPE_FLOAT);
    MirNumericOpPlan comparison = {};
    // a numeric domain can still reject an operator (for example float div).
    // Its boxed kernel owns that ordinary Error outcome, not compiler admission.
    if (numeric && !em_numeric_op_plan(n->op, &comparison, left == LMD_TYPE_INT && right == LMD_TYPE_INT))
        numeric = left == LMD_TYPE_INT && right == LMD_TYPE_INT &&
            (n->op == OPERATOR_IDIV || n->op == OPERATOR_MOD);
    if (numeric && (nullable(a) || nullable(b)) &&
            (a.rep == VALUE_REP_INT_LANE || a.rep == VALUE_REP_F64) &&
            (b.rep == VALUE_REP_INT_LANE || b.rep == VALUE_REP_F64) &&
            em_numeric_op_plan(n->op, &comparison, left == LMD_TYPE_INT && right == LMD_TYPE_INT) &&
            comparison.is_comparison) return nullable_comparison(c, n, a, b);
    // Nullable arithmetic absorbs absence; comparisons keep their runtime null rules.
    if (nullable(a) || nullable(b)) numeric &= n->op == OPERATOR_ADD || n->op == OPERATOR_SUB ||
        n->op == OPERATOR_MUL || n->op == OPERATOR_DIV ||
        (left == LMD_TYPE_INT && right == LMD_TYPE_INT && (n->op == OPERATOR_IDIV || n->op == OPERATOR_MOD));
    // bool? reserves lane 2 for absence; equality can compare that sentinel
    // directly, including null == null, without boxed dispatch (S5.1).
    bool booleans = left == LMD_TYPE_BOOL && right == LMD_TYPE_BOOL &&
        (n->op == OPERATOR_EQ || n->op == OPERATOR_NE);
    if (!numeric && !booleans) {
        const char* helper = em_boxed_binary_helper(n->op);
        if (!helper) {
            mvp_reject(c->module, n, "comparison requires proven numeric or bool contracts"); return null_value(c);
        }
        MirValue original[] = {a, b};
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        MIR_reg_t result = 0;
        MIR_label_t done = NULL;
        if ((n->op == OPERATOR_EQ || n->op == OPERATOR_NE) &&
                (left == LMD_TYPE_ANY || left == LMD_TYPE_TYPE || left == LMD_TYPE_STRING) &&
                (right == LMD_TYPE_ANY || right == LMD_TYPE_TYPE || right == LMD_TYPE_STRING)) {
            result = em_new_reg(&c->em, "dynamic_equality", MIR_T_I64);
            MIR_label_t slow = label(c); done = label(c);
            MirValue boxed[] = {a, b}, strings[2];
            guarded_binary_inputs(c, original, boxed, &TYPE_STRING, slow, strings);
            // exact string tags exclude Error and retain content equality;
            // every other family still reaches its ordinary boxed kernel.
            MIR_reg_t compared = em_text_pointer_compare(&c->em,
                strings[0].reg, strings[1].reg, LMD_TYPE_STRING, n->op);
            move(c, result, reg(c, operation(c, MIR_OR, reg(c, compared), imm(c, (uint64_t)LMD_TYPE_BOOL << 56))));
            jump(c, done); mark(c, slow);
        }
        // known decimal/string/container carriers cannot satisfy an int tag
        // guard. Keep speculation only where both operand domains allow it.
        if ((left == LMD_TYPE_ANY || left == LMD_TYPE_TYPE || left == LMD_TYPE_INT) &&
                (right == LMD_TYPE_ANY || right == LMD_TYPE_TYPE || right == LMD_TYPE_INT)) {
            if (!result) { result = em_new_reg(&c->em, "dynamic_numeric", MIR_T_I64); done = label(c); }
            MIR_label_t slow = label(c);
            MirValue boxed[] = {a, b}, integers[2];
            guarded_binary_inputs(c, original, boxed, &TYPE_INT, slow, integers);
            // the dominating tag/band guards already exclude poison in both operands.
            move(c, result, reg(c, convert(c, numeric_binary(c, n, integers[0], integers[1], true), VALUE_REP_ITEM).reg));
            jump(c, done); mark(c, slow);
        }
        MirNumericOpPlan floating = {};
        if ((left == LMD_TYPE_ANY || left == LMD_TYPE_TYPE || left == LMD_TYPE_FLOAT) &&
                (right == LMD_TYPE_ANY || right == LMD_TYPE_TYPE || right == LMD_TYPE_FLOAT) &&
                em_numeric_op_plan(n->op, &floating) &&
                (floating.is_comparison || n->op == OPERATOR_ADD || n->op == OPERATOR_SUB ||
                 n->op == OPERATOR_MUL || n->op == OPERATOR_DIV)) {
            if (!result) { result = em_new_reg(&c->em, "dynamic_numeric", MIR_T_I64); done = label(c); }
            MIR_label_t slow = label(c);
            MirValue boxed[] = {a, b}, operands[2];
            guarded_binary_inputs(c, original, boxed, &TYPE_FLOAT, slow, operands);
            move(c, result, reg(c, convert(c, numeric_binary(c, n, operands[0], operands[1]), VALUE_REP_ITEM).reg));
            jump(c, done); mark(c, slow);
        }
        int decimal_op = left == LMD_TYPE_DECIMAL && right == LMD_TYPE_DECIMAL
            ? mvp_decimal_operation(n->op) : -1;
        MIR_reg_t checked = decimal_op >= 0
            ? em_call_3(&c->em, "lambda_decimal_binary", MIR_T_I64,
                MIR_T_I64, reg(c, a.reg), MIR_T_I64, reg(c, b.reg), MIR_T_I64, imm(c, decimal_op), true)
            : em_call_2(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, a.reg),
                MIR_T_I64, reg(c, b.reg), true);
        if (result) move(c, result, reg(c, checked));
        else result = checked;
        if (done) mark(c, done);
        MirValue computed = value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, n);
        // comparison helpers return bool or Error; ordered comparisons can
        // additionally return null. Keep the normal domain through a caret
        // instead of redispatching truth through is_truthy (S5.1, S7.9.3).
        MirNumericOpPlan comparison = {};
        if (em_numeric_op_plan(n->op, &comparison) && comparison.is_comparison)
            computed.admitted_success_contract = n->op == OPERATOR_EQ || n->op == OPERATOR_NE
                ? &TYPE_BOOL : c->module->nullable_bool;
        // decimal arithmetic closes in its admitted domain, including poison;
        // retain the error arm while avoiding repeated successful admission.
        if (decimal_op >= 0) computed.admitted_success_contract =
            mvp_scalar_success_contract(c->module, mvp_value_contract(c->module, (AstNode*)n));
        return computed;
    }
    if (!a.admitted_success_contract && !b.admitted_success_contract) return numeric_binary(c, n, a, b);
    MIR_reg_t joined = em_new_reg(&c->em, "numeric_join", MIR_T_I64);
    MIR_label_t done = label(c);
    MirValue operands[] = {a, b};
    join_operand_errors(c, operands, 2, joined, done);
    for (int i = 0; i < 2; i++) {
        MirValue* v = &operands[i];
        if (!v->admitted_success_contract) continue;
        // preserve left-to-right error selection after both operands have been evaluated (S7.9.3).
        v->semantic_type = i == 0 ? left : right;
        v->semantic_contract = v->admitted_success_contract;
    }
    MirValue result;
    if (numeric && (nullable(operands[0]) || nullable(operands[1])) &&
            em_numeric_op_plan(n->op, &comparison) && comparison.is_comparison) {
        for (int i = 0; i < 2; i++) operands[i] = convert(c, operands[i],
            numeric_success(operands[i]) == LMD_TYPE_INT ? VALUE_REP_INT_LANE : VALUE_REP_F64);
        result = nullable_comparison(c, n, operands[0], operands[1]);
    } else result = numeric_binary(c, n, operands[0], operands[1]);
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
    bool collecting[16];
    bool noncollecting[16];
    unsigned count;
};
static bool dense_body_eligible(MvpCompiler*, MvpIndexLoopPlan*, MvpDenseCalls* calls = NULL, bool allocating = false);
static bool dense_call_eligible(MvpCompiler*, AstCallNode*);
static bool dense_call_arguments(MvpCompiler*, MvpFunction*, AstCallNode*);

static bool function_has_arrays(LambdaMvpModule* m, MvpFunction* f) {
    for (AstNamedNode* p = f ? f->ast->param : NULL; p; p = (AstNamedNode*)p->next) {
        // guarded inferred carriers obey the same recursive extent proof as
        // declared arrays. Imported formals have no binding in this module.
        MvpBinding* binding = mvp_binding(m, p->entry);
        if (mvp_array_element(binding ? binding->contract : mvp_contract(p))) return true;
    }
    return false;
}

static bool function_has_snapshots(LambdaMvpModule* m, MvpFunction* f) {
    for (AstNamedNode* p = f ? f->ast->param : NULL; p; p = (AstNamedNode*)p->next)
        if (mvp_binding(m, p->entry)->scalar_snapshot) return true;
    return false;
}

static bool inline_eligible(MvpCompiler* c, MvpFunction* f, bool discard = false,
        bool closed_body = false) {
    // direct_call admits actuals before expansion; public/default receivers
    // still execute their boxed protocol at the actual callee entry.
    if (f->imported || mvp_parameter_receiver(f) || f->owns_open_scopes ||
            (f->ast->analysis && (f->ast->analysis->owns_task_scopes ||
             f->ast->analysis->lambda_uses_shared_homes))) return false;
    bool record_result = f->abi.result.normal.abi_rep == VALUE_REP_RAW_GC_POINTER &&
        lambda_type_nonnull_map_contract(f->result_contract);
    bool boxed_value_result = f->abi.result.normal.abi_rep == VALUE_REP_ITEM && !discard && !f->no_value_result;
    if (boxed_value_result && (f->ast->captures || f->method_owner || mvp_function_binder_count(f))) return false;
    // array producers own native construction/snapshot entry versions. Generic
    // expansion loses those kernels; a proved dense call selects its own path.
    if (boxed_value_result && mvp_array_contract(f->result_contract)) return false;
    unsigned budget = !f->abi.effects.may_gc && !f->abi.effects.may_return_error ? 192 : 96;
    if (!f->inline_cost || f->inline_cost > budget || c->inline_cost + f->inline_cost > 256 ||
            (f->abi.effects.may_reenter && !closed_body) ||
            // ordinary return lowering retains projected snapshots (S9.3.1).
            (f->abi.result.normal.abi_rep == VALUE_REP_RAW_GC_POINTER &&
             f->result_contract != &TYPE_STRING && f->result_contract != &TYPE_DECIMAL && !record_result)) return false;
    // an erased input can own a proved array-loop entry too. Apply the same
    // expansion policy as declared array inputs so callers retain its guards.
    bool indexed_inputs = f->guarded_array_entry, var_arrays = false, borrowed_arrays = false;
    unsigned position = 0;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, position++) {
        Type* input = mvp_contract(p);
        indexed_inputs |= mvp_array_contract(input) || input == &TYPE_STRING;
        TypeParam* parameter = lambda_type_param(p->type);
        MvpBinding* binding = mvp_binding(c->module, p->entry);
        // a guarded body's parameter lanes belong to that activation. A
        // fresh self expansion must reenter the original ABI and entry guards.
        if (parameter && !parameter->is_var_param && binding &&
                binding->rep != f->abi.params[position].canonical_rep) return false;
        var_arrays |= boxed_value_result && parameter && parameter->is_var_param &&
            (mvp_open_value_contract(input) || lambda_type_nonnull_map_contract(input));
        if (parameter && mvp_array_contract(input)) {
            var_arrays |= parameter->is_var_param;
            borrowed_arrays |= !parameter->is_var_param && mvp_parameter_borrowable(c->module, p);
        }
        // plain record/array actuals already carry their call-site captures;
        // preserve the established restriction on record-returning var calls.
        if (record_result && parameter && parameter->is_var_param) return false;
    }
    // larger collecting mutators repeat their cold admission graph when
    // expanded. Closed kernels, borrowed array witnesses and caller-reserved
    // snapshot scratch retain their established expansion paths.
    if (!discard && var_arrays && !borrowed_arrays && f->abi.effects.may_gc &&
            f->inline_cost > 32 && !function_has_snapshots(c->module, f)) return false;
    if (indexed_inputs) {
        // keep indexed loop kernels at their own entry; a single recursive expansion
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

static bool guarded_integer_body(MvpCompiler* c, bool guarded_inputs) {
    MvpNumericLoopPlan numeric = {};
    if (!mvp_numeric_inline_plan(c->module, c->function, &numeric, guarded_inputs)) return false;
    MIR_label_t checked = label(c);
    for (uint32_t g = 0; g < numeric.count; g++) {
        MvpBinding* b = numeric.guards[g];
        MIR_reg_t biased = operation(c, MIR_ADD, reg(c, b->reg), imm(c, numeric.radius));
        branch(c, MIR_UBGT, checked, reg(c, biased), imm(c, numeric.radius * 2));
        b->range = {true, -numeric.radius, numeric.radius};
    }
    // the interval proves every arithmetic intermediate. Calls and exits keep
    // the original ABI/effects, including errors outside this finite version.
    function_body(c);
    for (uint32_t g = 0; g < numeric.count; g++) numeric.guards[g]->range = {};
    mem_free(numeric.guards);
    mark(c, checked);
    return true;
}

static MirCallResult inline_call(MvpCompiler* c, MvpFunction* f, MIR_op_t* args,
        MirValue* actuals, MvpArrayWitness* witnesses, MIR_reg_t output_destination = 0) {
    LambdaMvpModule* m = c->module;
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) { mvp_reject(m, f->ast, "inline binding allocation failed"); return {}; }
    MvpFunction* caller = c->function;
    MirPipeContext caller_context = context_save(c);
    MvpScope* scope = c->scope;
    MvpCleanup* cleanups = c->cleanups;
    MvpErrorTarget* condition_failure = c->condition_failure;
    MvpErrorTarget* output_failure = c->output_failure;
    MvpControl* control = c->control;
    MIR_label_t tail_entry = c->tail_entry;
    MvpBinding* loop_array = c->loop_array;
    MIR_reg_t loop_items = c->loop_array_items;
    bool dense_arrays = c->dense_arrays;
    bool dense_call = c->dense_callee == f;
    bool refined = false;
    Type* incoming_contracts[LAMBDA_MAX_FUNCTION_ARGS] = {};
    if (!dense_call && !output_destination && !mvp_variadic_function(f)) {
        unsigned position = 0;
        for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, position++) {
            MvpBinding* binding = mvp_binding(m, p->entry);
            MirValue actual = actuals[position];
            if (!binding->stable_scalar_home || binding->rep != VALUE_REP_ITEM ||
                    !mvp_open_value_contract(binding->contract) ||
                    !actual.semantic_contract || lambda_type_accepts_error(actual.semantic_contract)) continue;
            bool optional = false;
            Type* domain = lambda_type_nullable_lane_base(actual.admitted_success_contract
                ? actual.admitted_success_contract : actual.semantic_contract, &optional);
            if (!domain || optional) continue;
            domain = mvp_inferred_scalar_contract(domain);
            if (domain != &TYPE_INT && domain != &TYPE_FLOAT && domain != &TYPE_BOOL &&
                    domain != &TYPE_STRING && domain != &TYPE_DECIMAL) continue;
            // call-site admission already proved this unchanged scalar. These
            // activation facts never replace the parameter's receiving contract.
            incoming_contracts[position] = domain;
            binding->contract = binding->producer_success_contract = domain;
            binding->rep = mvp_rep(domain); binding->range = {};
            refined = true;
        }
        if (refined) mvp_refine_guarded_locals(m, f);
    }
    MvpInline frame = {c->inlining, f,
        em_new_reg(&c->em, "inline_result", output_destination ? MIR_T_I64 : em_mir_type_for_rep(f->abi.result.normal.abi_rep)), label(c),
        f->abi.effects.may_return_error ? em_new_reg(&c->em, "inline_error", MIR_T_I64) : 0};
    // an expansion shares the caller's number frame; MOV liveness retains
    // any cold scalar home through its boxed result (D6.2.2/D6.2.3). Error
    // exits can publish var homes before joining, so initialize the root.
    if (output_destination || f->abi.result.normal.abi_rep == VALUE_REP_ITEM)
        move(c, frame.result, imm(c, ITEM_NULL));
    if (frame.error) {
        move(c, frame.error, imm(c, ITEM_NULL));
        value(c, frame.error, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    }
    c->inlining = &frame; c->function = f; c->scope = NULL; c->control = NULL;
    c->cleanups = NULL;
    // S10.1.3: an ordinary activation cannot inherit a caller's lexical
    // pipe/match bindings merely because its body is expanded here.
    context_restore(c, {});
    c->condition_failure = NULL;
    // a value call owns its propagation; only an explicit output expansion
    // receives the caller's byte destination (S2.6.6v3).
    c->output_failure = NULL;
    c->loop_array = NULL; c->loop_array_items = 0; c->dense_arrays = dense_call;
    c->inline_cost += f->inline_cost;
    if (dense_call)
        // the kernel plan proved every outcome initializer/write under the
        // entry guards. Choose native locals before allocating their registers.
        narrow_dense_locals(c, ast_index_find(&m->tp->ast_index, f->ast->body), true);
    // every expansion owns fresh locals; recursive expansion must not overwrite the caller's bindings.
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (b->owner != f->id) continue;
        b->reg = em_new_reg(&c->em, "inline_binding", em_mir_type_for_rep(b->rep));
        value(c, b->reg, b->contract->type_id, b->rep, b->contract);
        b->scalar_home = 0; b->scope = NULL;
        b->var_home = b->array_native = b->array_float_native = b->array_bool_native = b->array_boxed =
            b->array_writable = b->array_length = b->dense_items = 0;
        b->record_writable = b->record_readable = 0;
    }
    int i = 0;
    bool snapshots = function_has_snapshots(m, f);
    if (snapshots) {
        // a failed snapshot jumps to the common inline exit, which writes all
        // borrowed arguments back. Initialize every parameter before that call.
        for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, i++)
            receive_parameter(c, mvp_binding(m, p->entry), args[i].u.reg, i, incoming_contracts[i]);
    }
    i = 0;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, i++) {
        MvpBinding* b = mvp_binding(m, p->entry);
        if (!snapshots) receive_parameter(c, b, args[i].u.reg, i, incoming_contracts[i]);
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
    if (dense_call) narrow_dense_locals(c, ast_index_find(&m->tp->ast_index, f->ast->body));
    bool numeric = !output_destination && guarded_integer_body(c, false);
    if (!numeric && !output_destination && !dense_call && !snapshots &&
            f->abi.result.normal.abi_rep == VALUE_REP_ITEM && !f->no_value_result &&
            c->inline_cost + f->inline_cost <= 256 && guarded_scalar_body(c)) {
        // opaque actuals need the same entry version as a standalone call;
        // charge its second body against the containing expansion budget.
        c->inline_cost += f->inline_cost;
    }
    if (output_destination) {
        c->output_inline_depth++;
        MirValue status = output_expression(c, f->ast->body, output_destination);
        c->output_inline_depth--;
        move(c, frame.result, reg(c, status.reg));
    } else function_body(c);
    mark(c, frame.done);
    i = 0;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next, i++) {
        MvpBinding* b = mvp_binding(m, p->entry);
        TypeParam* param = lambda_type_param(p->type);
        // expansions cannot reenter or capture caller locals; rooted registers
        // carry replacements to this common exit, including partial error exits.
        if (param && param->is_var_param && !dense_call) {
            // var publication always carries an Item outcome; the entry ABI's
            // native argument lane says nothing about a later failed store.
            MirValue outgoing = convert(c, value(c, b->reg, b->contract->type_id,
                b->rep, b->contract), VALUE_REP_ITEM);
            args[i] = reg(c, outgoing.reg);
        }
        // capture-free numeric accessors update their own witnesses on every checked write.
        witnesses[i] = closed ? MvpArrayWitness{b->array_native, b->array_writable, b->array_length, 0} : MvpArrayWitness{};
    }
    c->inlining = frame.parent; c->function = caller; c->scope = scope; c->control = control;
    c->cleanups = cleanups;
    c->condition_failure = condition_failure;
    c->output_failure = output_failure;
    context_restore(c, caller_context);
    c->tail_entry = tail_entry;
    c->loop_array = loop_array; c->loop_array_items = loop_items; c->dense_arrays = dense_arrays;
    restore_bindings(m, saved);
    mem_free(saved);
    MirCallResult result = {};
    result.normal = output_destination ? value(c, frame.result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY)
        : value(c, frame.result, f->result_contract->type_id, f->abi.result.normal.abi_rep, f->result_contract);
    if (frame.error) result.error = value(c, frame.error, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    return result;
}

struct MvpPlace {
    MvpBinding* root;
    MIR_reg_t keys[LAMBDA_PATH_KEYS_MAX];
    int count;
    int64_t index_mask;
    bool all_indices;
    Type* leaf_contract;
    MIR_reg_t borrowed_original;
    TypeMap* field_shape;
    ShapeEntry* field;
    Type* containers[LAMBDA_PATH_KEYS_MAX];
    ShapeEntry* fields[LAMBDA_PATH_KEYS_MAX];
    TypeMap* observed_shapes[LAMBDA_PATH_KEYS_MAX];
};

static Type* inferred_field_domain(ShapeEntry* field);
static Type* inferred_scalar_field(ShapeEntry* field);
static MirValue inferred_scalar_input(MvpCompiler* c, ShapeEntry* field, MirValue input, MIR_label_t miss);
static void guard_inferred_field_descriptor(MvpCompiler* c, ShapeEntry* field, MIR_label_t miss);
static MirValue inferred_field_value(MvpCompiler* c, ShapeEntry* field,
    MIR_reg_t data, AstNode* source, MIR_label_t miss);

static MirValue read_place(MvpCompiler* c, const MvpPlace* place, int count) {
    MirValue current = convert(c, read_binding(c, place->root, NULL, false), VALUE_REP_ITEM);
    for (int i = 0; i < count; i++)
        current = value(c, em_call_2(&c->em, "fn_index", MIR_T_I64,
            MIR_T_I64, reg(c, current.reg), MIR_T_I64, reg(c, place->keys[i]), true),
            LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    return current;
}

static MvpPlace place_path(MvpCompiler* c, AstNode* source, AstNode* final_key = NULL,
        bool final_member = false) {
    AstCowPath path = {};
    if (!ast_collect_cow_path(&path, source)) return {};
    MvpBinding* root = mvp_local_binding(c->module, path.root);
    if (!root || root->owner != c->function->id) return {};
    int coordinate_start = path.count;
    bool coordinates = final_key && final_key->next && !final_member;
    for (AstNode* key = final_key; key; key = final_member ? NULL : key->next) {
        if (path.count == AST_COW_PATH_MAX) return {};
        path.segment[path.count] = key;
        path.is_member[path.count++] = final_member;
    }
    MvpPlace place = {}; place.root = root; place.count = path.count; place.all_indices = true;
    if (!path.count) return place;
    path.count--;
    place.leaf_contract = ast_map_path_leaf_contract(root->admission_contract, &path,
        path.segment[path.count], path.is_member[path.count], &place.index_mask);
    path.count++;
    if (path.count == 1 && path.is_member[0]) {
        AstFieldNode field = {}; field.object = path.root; field.field = path.segment[0];
        place.field = mvp_member_field(c->module, &field, &place.field_shape, true);
    }
    Type* parent = root->admission_contract;
    Type* observed_parent = NULL;
    bool open = mvp_open_value_contract(parent);
    AstNode* receivers[LAMBDA_PATH_KEYS_MAX] = {};
    if (open) {
        if (final_key) receivers[coordinate_start] = source;
        AstNode* receiver = source;
        for (int i = coordinate_start - 1; i >= 0; i--) {
            AstFieldNode* access = (AstFieldNode*)ast_unwrap_primary(receiver);
            receivers[i] = receiver = access->object;
        }
    }
    for (int i = 0; i < path.count; i++) {
        place.all_indices &= !path.is_member[i];
        place.containers[i] = parent;
        if (path.is_member[i]) {
            AstNode* key = ast_unwrap_primary_to_leaf(path.segment[i]);
            AstIdentNode* name = key && key->node_type == AST_NODE_IDENT ? (AstIdentNode*)key : NULL;
            Type* map = lambda_type_nonnull_map_contract(parent);
            place.fields[i] = name && map && map != &TYPE_MAP && ((TypeMap*)map)->is_trusted_contract
                ? find_shape_read_field_by_name((TypeMap*)map, name->name->chars, name->name->len) : NULL;
            parent = place.fields[i] ? place.fields[i]->type : NULL;
            if (open) {
                // physical hints stay separate from receiving contracts. The
                // store walk guards each live shape, descriptor and COW owner.
                TypeMap* candidate = (TypeMap*)lambda_type_nonnull_map_contract(observed_parent);
                if (!candidate || candidate == &TYPE_MAP || candidate->is_trusted_contract ||
                        !name || !find_shape_read_field_by_name(candidate, name->name->chars, name->name->len))
                    candidate = name ? mvp_member_candidate(c->module, receivers[i], name->name) : NULL;
                if (candidate && !candidate->is_trusted_contract) {
                    place.fields[i] = find_shape_read_field_by_name(candidate, name->name->chars, name->name->len);
                    place.observed_shapes[i] = candidate;
                }
                observed_parent = place.fields[i] ? place.fields[i]->type : NULL;
            }
        } else {
            parent = lambda_array_contract_element(parent);
            observed_parent = lambda_array_contract_element(observed_parent);
        }
        MIR_reg_t previous_last = c->last_index_item;
        if (!path.is_member[i] && em_index_keys_use_last(path.segment[i])) {
            // one multi-axis subscript owns one last scope, independently
            // of the path depth used by the checked COW setter (S7.2.2).
            c->last_index_item = read_place(c, &place,
                coordinates && i >= coordinate_start ? coordinate_start : i).reg;
        }
        MirValue key = cow_path_key(c, path.segment[i], path.is_member[i]);
        c->last_index_item = previous_last;
        key = convert(c, key, VALUE_REP_ITEM);
        place.keys[i] = key.reg;
    }
    return place;
}

static MIR_reg_t argument_span(MvpCompiler* c, int count) {
    int active = c->argument_active;
    c->argument_active += count;
    if (c->argument_active > c->argument_count) c->argument_count = c->argument_active;
    MIR_reg_t span = em_deferred_root_span_base(&c->em, &c->argument_span, &c->argument_fixup);
    return active ? operation(c, MIR_ADD, reg(c, span), imm(c, active * sizeof(Item))) : span;
}

static MIR_reg_t place_keys_span(MvpCompiler* c, const MvpPlace* place) {
    MIR_reg_t span = argument_span(c, place->count);
    for (int i = 0; i < place->count; i++)
        em_store_at(&c->em, span, i * sizeof(Item), MIR_T_I64, place->keys[i]);
    return span;
}

static void release_argument_span(MvpCompiler* c, MIR_reg_t span, int count) {
    for (int i = 0; i < count; i++)
        em_store_at(&c->em, span, i * sizeof(Item), MIR_T_I64, constant(c, ITEM_NULL));
    c->argument_active -= count;
}

static void publish_place(MvpCompiler* c, MvpBinding* binding, MIR_reg_t owner) {
    MirValue updated = convert(c, value(c, owner, binding->contract->type_id,
        VALUE_REP_ITEM, binding->contract), binding->rep);
    move(c, binding->reg, reg(c, updated.reg), updated.mir_type);
    refresh_array(c, binding);
    publish_var_binding(c, binding);
}

static MIR_reg_t prepare_place_owner(MvpCompiler* c, MvpBinding* binding) {
    MirValue root = convert(c, read_binding(c, binding, NULL, false), VALUE_REP_ITEM);
    MIR_reg_t prepared = em_new_reg(&c->em, "prepared_owner", MIR_T_I64);
    MIR_label_t slow = label(c), done = label(c);
    // a unique physical map already owns its write permission. Other carriers,
    // including virtual containers, retain the shared ownership helper.
    if (lambda_type_nonnull_map_contract(binding->admission_contract)) {
        MIR_reg_t pointer = em_guard_container(&c->em, root.reg, LMD_TYPE_MAP, slow);
        em_container_cow_guard(&c->em, pointer, slow, true);
        move(c, prepared, reg(c, root.reg)); jump(c, done);
    }
    mark(c, slow);
    move(c, prepared, reg(c, em_call_1(&c->em, cow_import("cow_prepare_write", "cow_prepare_write_profiled"), MIR_T_I64,
        MIR_T_I64, reg(c, root.reg), true)));
    publish_place(c, binding, prepared);
    mark(c, done);
    return prepared;
}

static MIR_reg_t borrow_place_field(MvpCompiler* c, const MvpPlace* place, MIR_reg_t owner) {
    MIR_reg_t child = em_new_reg(&c->em, "borrowed_field", MIR_T_I64);
    MIR_label_t slow = label(c), done = label(c);
    const LaneStorageDesc* storage = shape_entry_storage(place->field);
    if (storage->kind == LANE_STORAGE_POINTER && mvp_array_element(place->leaf_contract) &&
            shape_entry_storage_fits_data(place->field, place->field_shape->byte_size)) {
        MIR_reg_t pointer = em_guard_map_shape(&c->em, owner,
            constant(c, (intptr_t)place->field_shape), slow, false, true);
        MIR_reg_t data = em_load_at(&c->em, pointer, LAMBDA_GC_OFF_MAP_DATA, MIR_T_P, "borrow_data");
        MIR_reg_t field = em_load_at(&c->em, data, place->field->byte_offset, MIR_T_I64, "borrow_child");
        pointer = em_guard_container(&c->em, field, LMD_TYPE_ARRAY_NUM, slow);
        em_container_cow_guard(&c->em, pointer, slow, true);
        move(c, child, reg(c, pointer)); jump(c, done);
    }
    mark(c, slow);
    move(c, child, reg(c, em_call_4(&c->em, "cow_path_borrow_typed_map_field", MIR_T_I64,
        MIR_T_I64, reg(c, owner), MIR_T_P, imm(c, (intptr_t)place->field_shape),
        MIR_T_P, imm(c, (intptr_t)place->field), MIR_T_I64, reg(c, place->keys[0]), true)));
    mark(c, done);
    return child;
}

static bool field_has_direct_lane(ShapeEntry* field) {
    if (!field) return false;
    const LaneStorageDesc* storage = shape_entry_storage(field);
    LaneStorageKind lane = (LaneStorageKind)storage->kind;
    // admitted pointer fields share the scalar store proof; projected
    // containers still acquire their insertion snapshot before any write.
    bool native_pointer = lane == LANE_STORAGE_POINTER &&
        (mvp_rep(field->type) == VALUE_REP_RAW_GC_POINTER ||
         lambda_type_nonnull_map_contract(field->type) || mvp_array_contract(field->type));
    return (storage->native && (lane == LANE_STORAGE_INT || lane == LANE_STORAGE_FLOAT64 ||
        lane == LANE_STORAGE_BOOL)) || native_pointer;
}

static bool field_input_proven(ShapeEntry* field, MirValue input) {
    if (!field_has_direct_lane(field)) return false;
    bool optional = false;
    Type* normal = lambda_type_nullable_lane_base(input.admitted_success_contract
        ? input.admitted_success_contract : input.semantic_contract, &optional);
    return mvp_boundary_proven(input.semantic_contract, field->type) ||
        mvp_boundary_proven(input.admitted_success_contract, field->type) ||
        (optional && mvp_boundary_proven(normal, field->type));
}

static MirValue admitted_field_input(MvpCompiler* c, ShapeEntry* field,
        MirValue input, MIR_label_t miss) {
    if (input.admitted_success_contract) {
        if (input.rep == VALUE_REP_ITEM) branch(c, MIR_BEQ, miss,
            reg(c, operation(c, MIR_URSH, reg(c, input.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        input.semantic_contract = input.admitted_success_contract;
        input.semantic_type = input.semantic_contract->type_id;
    }
    bool optional = false;
    Type* normal = lambda_type_nullable_lane_base(input.semantic_contract, &optional);
    if (optional) {
        // reject absence before changing any owner; the checked writer retains
        // its admission error and transactional behavior (S11.4.1v5).
        branch_null(c, input, miss);
        input.semantic_contract = normal;
        input.semantic_type = normal->type_id;
    }
    bool pointer = shape_entry_storage(field)->kind == LANE_STORAGE_POINTER;
    return convert(c, input, pointer ? VALUE_REP_RAW_GC_POINTER : mvp_rep(field->type));
}

static bool scalar_place_plan(const MvpPlace* place, MirValue input, Type* indexed_element) {
    if (place->count < 2) return false;
    int leaf = place->count - 1;
    if (place->observed_shapes[leaf]) {
        if (!inferred_scalar_field(place->fields[leaf])) return false;
    } else if (place->fields[leaf] ? !field_input_proven(place->fields[leaf], input) : !indexed_element) return false;
    for (int i = 0; i < place->count; i++) {
        if (place->fields[i]) {
            TypeMap* map = place->observed_shapes[i] ? place->observed_shapes[i]
                : (TypeMap*)lambda_type_nonnull_map_contract(place->containers[i]);
            if (!map || !shape_entry_storage_fits_data(place->fields[i], map->byte_size)) return false;
            if (i < leaf && (place->observed_shapes[i] ? !inferred_field_domain(place->fields[i])
                    : shape_entry_storage(place->fields[i])->kind != LANE_STORAGE_POINTER)) return false;
        } else {
            Type* child = lambda_array_contract_element(place->containers[i]);
            if (i != leaf && !mvp_open_value_contract(place->root->admission_contract) &&
                    !lambda_type_nonnull_map_contract(child) && !mvp_array_contract(child)) return false;
        }
    }
    return true;
}

static MIR_type_t native_field_storage_type(const LaneStorageDesc* storage);

static MirValue primitive_place_input(MvpCompiler* c, MirValue input, Type* element, MIR_label_t miss) {
    if (input.admitted_success_contract) {
        MirValue boxed = convert(c, input, VALUE_REP_ITEM);
        branch(c, MIR_BEQ, miss, reg(c, operation(c, MIR_URSH, reg(c, boxed.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        input.semantic_contract = input.admitted_success_contract;
        input.semantic_type = input.semantic_contract->type_id;
    }
    // null cannot enter a primitive lane; poison ints retain the checked writer.
    branch_null(c, input, miss);
    return element == &TYPE_INT
        ? value(c, em_guard_finite_int(&c->em, input, miss), LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT)
        : convert(c, input, mvp_rep(element));
}

static void scalar_place_store(MvpCompiler* c, const MvpPlace* place,
        MirValue input, MIR_reg_t owner, MIR_label_t miss, Type* indexed_element) {
    ShapeEntry* leaf = place->fields[place->count - 1];
    // conversions precede interior pointers; the guarded walk cannot collect.
    bool inferred = place->observed_shapes[place->count - 1] != NULL;
    if (!leaf) input = primitive_place_input(c, input, indexed_element, miss);
    else if (inferred) input = inferred_scalar_input(c, leaf, input, miss);
    else input = admitted_field_input(c, leaf, input, miss);
    MIR_reg_t current = owner;
    bool admitted = mvp_admission_proven(c->module, place->root->contract, place->root->admission_contract) &&
        !mvp_open_value_contract(place->root->admission_contract);
    // Item transport tags nullable pointer absence. Decode before the raw
    // pointer null test; otherwise null could reach a header load (D2.3.5).
    if (admitted && lambda_type_accepts_null(place->root->contract))
        current = operation(c, MIR_AND, reg(c, current), imm(c, ITEM_INT_PAYLOAD_MASK));
    for (int i = 0; i < place->count; i++) {
        if (place->fields[i]) {
            TypeMap* observed = place->observed_shapes[i];
            TypeMap* map = observed ? observed
                : (TypeMap*)lambda_type_nonnull_map_contract(place->containers[i]);
            if (!admitted || observed || !ast_map_contract_storage_valid(map))
                current = em_guard_map_shape(&c->em, current, constant(c, (intptr_t)map), miss, false, !observed);
            else if (lambda_type_accepts_null(place->containers[i]))
                branch(c, MIR_BEQ, miss, reg(c, current), imm(c, 0));
            // mixed/dynamic slots can retag through a checked write. Their live
            // prefix must still match, even inside a certified array (D4.2.5).
            // permission to mutate remains independent on every spine owner.
            em_container_cow_guard(&c->em, current, miss, true);
            MIR_reg_t data = em_load_at(&c->em, current, LAMBDA_GC_OFF_MAP_DATA, MIR_T_P, "place_data");
            ShapeEntry* field = place->fields[i];
            if (i + 1 == place->count) {
                if (observed) guard_inferred_field_descriptor(c, field, miss);
                em_store_at(&c->em, data, field->byte_offset, native_field_storage_type(shape_entry_storage(field)), input.reg);
            } else if (observed) current = convert(c,
                inferred_field_value(c, field, data, NULL, miss), VALUE_REP_ITEM).reg;
            else current = em_load_at(&c->em, data, field->byte_offset, MIR_T_I64, "place_child");
        } else {
            MIR_reg_t index = em_guard_finite_int(&c->em,
                value(c, place->keys[i], LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY), miss);
            if (i + 1 == place->count) {
                ArrayNumElemType lane;
                lambda_array_num_elem_type_for_contract(indexed_element, &lane);
                MIR_reg_t pointer = em_guard_container(&c->em, current, LMD_TYPE_ARRAY_NUM, miss);
                em_array_num_elem_guard(&c->em, pointer, lane, miss);
                em_container_cow_guard(&c->em, pointer, miss, true);
                em_array_num_bounds_check(&c->em, pointer, index, miss);
                // an erased write cannot retain an unrelated refined certificate.
                branch(c, MIR_BNE, miss, reg(c, em_array_rep_cert(&c->em, pointer)), imm(c, 0));
                MIR_reg_t address = em_array_element_address(&c->em, pointer, index,
                    indexed_element == &TYPE_BOOL ? 1 : 8);
                em_store_at(&c->em, address, 0, em_numeric_storage_type(lane), input.reg);
                continue;
            }
            MIR_reg_t pointer = em_guard_container(&c->em, current, LMD_TYPE_ARRAY, miss);
            em_container_cow_guard(&c->em, pointer, miss, true);
            // unknown children retain their Item tag; stripping a scalar tag
            // could make its payload look like a container header (D2.6.2).
            if (admitted) {
                // a live certificate proves every child's reified prefix;
                // an optional child keeps its null check at the next step.
                em_array_rep_cert_guard(&c->em, pointer, place->containers[i], miss);
                em_array_num_bounds_check(&c->em, pointer, index, miss);
                MIR_reg_t item = em_load_at(&c->em,
                    em_array_element_address(&c->em, pointer, index, sizeof(Item)), 0, MIR_T_I64, "certified_child");
                current = operation(c, MIR_AND, reg(c, item), imm(c, ITEM_INT_PAYLOAD_MASK));
            } else current = mvp_open_value_contract(place->root->admission_contract)
                ? em_physical_array_index(&c->em, current, index, miss, miss, false)
                : em_pointer_array_index(&c->em, current, index, miss, miss);
        }
    }
}

static void write_place(MvpCompiler* c, MvpPlace place, MirValue input, AstNode* source) {
    // checked setters also need the projected RHS snapshot; they only own
    // the destination walk and do not capture a borrowed child (S9.3.1).
    input = capture_projection(c, input, NULL);
    bool typed = ast_declared_type_is_map(place.root->admission_contract) ||
        lambda_array_contract_canonical(place.root->admission_contract);
    // open array constrains only the root kind. Its descendants have no
    // element contract; the generic COW path still validates every key.
    Type* stored_type = place.leaf_contract;
    // erased paths may specialize their physical scalar lane; a declared
    // container still requires its resolved leaf admission before any write.
    if (!typed) {
        TypeId kind = numeric_success(input);
        stored_type = kind == LMD_TYPE_INT ? &TYPE_INT : kind == LMD_TYPE_FLOAT ? &TYPE_FLOAT :
            (input.semantic_type == LMD_TYPE_BOOL ? &TYPE_BOOL : NULL);
    }
    bool primitive = (stored_type == &TYPE_INT || stored_type == &TYPE_FLOAT || stored_type == &TYPE_BOOL) &&
        (mvp_boundary_proven(input.semantic_contract, stored_type) ||
         mvp_boundary_proven(input.admitted_success_contract, stored_type));
    bool packed = place.count >= 2 && place.count <= 3 && place.all_indices && primitive;
    Type* indexed_element = primitive ? stored_type : NULL;
    MirValue native_input = input;
    bool scalar = scalar_place_plan(&place, input, indexed_element);
    input = convert(c, input, VALUE_REP_ITEM);
    MIR_label_t done = place.borrowed_original || packed || scalar ? label(c) : NULL;
    if (place.borrowed_original) branch(c, MIR_BEQ, done, reg(c, input.reg), reg(c, place.borrowed_original));
    MirValue owner = convert(c, read_binding(c, place.root, NULL, false), VALUE_REP_ITEM);
    if (packed) {
        // guard the whole coordinate before touching a packed owner; shared,
        // view and invalid-value paths keep the transactional checked setter.
        MIR_label_t checked = label(c);
        MIR_reg_t indices[LAMBDA_PATH_KEYS_MAX];
        for (int i = 0; i < place.count; i++)
            indices[i] = em_guard_finite_int(&c->em,
                value(c, place.keys[i], LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY), checked);
        ArrayNumElemType lane;
        lambda_array_num_elem_type_for_contract(stored_type, &lane);
        MirValue member = primitive_place_input(c, native_input, stored_type, checked);
        MIR_reg_t pointer = em_guard_container(&c->em, owner.reg, LMD_TYPE_ARRAY_NUM, checked);
        MIR_reg_t address = em_array_ndim_element_address(&c->em, pointer,
            place.count, indices, lane, checked);
        em_container_cow_guard(&c->em, pointer, checked, true);
        em_store_at(&c->em, address, 0, em_numeric_storage_type(lane), member.reg);
        jump(c, done); mark(c, checked);
    }
    if (scalar) {
        MIR_label_t checked = label(c);
        scalar_place_store(c, &place, native_input, owner.reg, checked, indexed_element);
        jump(c, done); mark(c, checked);
    }
    // fixed keys avoid a heap descriptor per write. The reserved span is part
    // of the precise frame, including keys retained across a collecting walk.
    MIR_reg_t span = place_keys_span(c, &place);
    MIR_reg_t result = typed
        ? em_call_6(&c->em, "lambda_map_path_set_checked_keys", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, input.reg),
            MIR_T_P, reg(c, span), MIR_T_I64, imm(c, place.count | (place.index_mask << 16)),
            MIR_T_P, imm(c, (intptr_t)place.root->admission_contract),
            MIR_T_P, imm(c, (intptr_t)place.leaf_contract), true)
        : em_call_5(&c->em, "cow_path_set_keys", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, input.reg),
            MIR_T_P, reg(c, span), MIR_T_I64, imm(c, place.count), MIR_T_I64, imm(c, 0), true);
    release_argument_span(c, span, place.count);
    check_store_result(c, result, place.root, source);
    publish_place(c, place.root, result);
    if (done) mark(c, done);
}

static MIR_reg_t mvp_read_capture(void* owner, FnCapture* capture) {
    MvpCompiler* c = (MvpCompiler*)owner;
    if (capture->entry && capture->entry->is_binder)
        return read_binder_type(c, capture->entry).reg;
    if (mvp_static_type_capture(capture))
        return convert(c, value(c, constant(c, (intptr_t)capture->entry->node->type),
            LMD_TYPE_TYPE, VALUE_REP_RAW_NON_GC_POINTER, &TYPE_TYPE), VALUE_REP_ITEM).reg;
    MvpBinding* binding = mvp_binding(c->module, capture->entry);
    if (capture->lambda_live_local) return binding->capture_home;
    return convert(c, read_binding(c, binding, NULL, true), VALUE_REP_ITEM).reg;
}

static MvpBinding* named_function_binding(MvpCompiler* c, MvpFunction* function) {
    AstFuncNode* ast = function ? function->ast : NULL;
    return ast && ast->captures && ast->name && ast->analysis && !function->method_owner
        ? mvp_binding(c->module, ast->analysis->decl_entry) : NULL;
}

static MIR_reg_t function_environment(MvpCompiler* c, MvpFunction* function) {
    if (function == c->function && c->method_environment) return c->method_environment;
    if (function->method_owner) {
        if (!function->ast->captures) return c->current_root;
        // sibling methods can have different lexical slots; bind through the
        // instance rather than reusing this method's declaring Function.
        MIR_reg_t bound = em_call_2(&c->em, "lambda_object_member", MIR_T_I64,
            MIR_T_I64, reg(c, c->current_root), MIR_T_P,
            imm(c, (intptr_t)function->ast->name->chars), true);
        mvp_check_item_result(c, bound);
        root_value(c, bound, JIT_VALUE_BOXED_ITEM);
        MIR_reg_t environment = em_load_at(&c->em, bound, offsetof(Function, closure_env),
            MIR_T_P, "sibling_method_environment");
        root_value(c, environment, JIT_VALUE_RAW_GC_POINTER);
        return environment;
    }
    // recursive calls retain this activation's declaration snapshot. Sibling
    // calls read the captured Function, not the sibling's original locals.
    if (function == c->function) return c->environment;
    MvpBinding* snapshot = named_function_binding(c, function);
    if (snapshot && snapshot->reg && snapshot->owner == c->function->id) {
        MirValue object = convert(c, read_binding(c, snapshot, NULL, false), VALUE_REP_RAW_GC_POINTER);
        MIR_reg_t environment = em_load_at(&c->em, object.reg,
            offsetof(Function, closure_env), MIR_T_P, "named_environment");
        root_value(c, environment, JIT_VALUE_RAW_GC_POINTER);
        return environment;
    }
    return em_capture_environment(&c->em, function->ast->captures, mvp_read_capture);
}

static MIR_reg_t constrained_predicate_call(void* owner, TypeConstrained* layer, MIR_reg_t subject) {
    MvpCompiler* c = (MvpCompiler*)owner;
    MvpFunction* function = mvp_function(c->module, layer->predicate_fn);
    if (!function) {
        mvp_reject(c->module, layer->predicate_fn, "constrained predicate was not planned");
        return constant(c, ITEM_FALSE);
    }
    if (function->imported) {
        MIR_reg_t answer = em_imported_constraint_call(&c->em, function->target, subject);
        return value(c, answer, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY).reg;
    }
    MIR_reg_t environment = em_capture_environment(&c->em, function->ast->captures, mvp_read_capture);
    MirCallOptions options = {true, environment != 0, environment};
    MIR_type_t types[1] = {MIR_T_I64};
    MIR_op_t operands[1] = {reg(c, subject)};
    MirCallResult result = em_call_direct(&c->em, function->name, function->target,
        &function->abi, 1, types, operands, &options);
    MIR_reg_t answer = em_new_reg(&c->em, "predicate_answer", MIR_T_I64);
    MIR_label_t done = label(c);
    if (function->abi.effects.may_return_error && result.error.reg) {
        MIR_label_t clean = label(c);
        move(c, answer, imm(c, ITEM_FALSE));
        branch(c, MIR_BEQ, clean, reg(c, result.error.reg),
            imm(c, em_error_lane_in_register(function->abi.result.companion) ? ITEM_NULL : 0));
        jump(c, done); mark(c, clean);
    }
    MirValue normal = em_finish_direct_call_normal(&c->em, result, MIR_PENDING_REASON_REP_CONVERSION);
    normal.semantic_contract = function->result_contract;
    move(c, answer, reg(c, convert(c, normal, VALUE_REP_ITEM).reg));
    mark(c, done);
    return value(c, answer, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY).reg;
}

static MIR_reg_t constrained_predicate_truth(void* owner, MIR_reg_t answer) {
    MvpCompiler* c = (MvpCompiler*)owner;
    MIR_reg_t classified = truth(c, value(c, answer, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY));
    return operation(c, MIR_EQ, reg(c, classified), imm(c, BOOL_TRUE));
}

static MIR_reg_t constrained_type_test(MvpCompiler* c, TypeConstrained* type, MIR_reg_t subject) {
    MirValue pattern = convert(c, value(c, constant(c, (intptr_t)type),
        LMD_TYPE_TYPE, VALUE_REP_RAW_NON_GC_POINTER, &TYPE_TYPE), VALUE_REP_ITEM);
    return em_constrained_type_test(&c->em, type, subject, pattern.reg,
        constrained_predicate_call, constrained_predicate_truth);
}

struct MvpArgumentReader {
    MvpCompiler* compiler;
    AstCallNode* call;
    MirValue receiver;
};

static MIR_reg_t mvp_read_argument_item(void* owner, int position) {
    MvpArgumentReader* reader = (MvpArgumentReader*)owner;
    MvpCompiler* c = reader->compiler;
    if (!position && reader->receiver.reg) return reader->receiver.reg;
    AstNode* arg = mvp_call_argument(reader->call, position);
    if (!arg || ast_is_omitted_argument(arg)) return constant(c, ITEM_MISSING_ARGUMENT);
    return convert(c, capture_projection(c, expression(c, arg), arg), VALUE_REP_ITEM).reg;
}

static MirValue direct_call(MvpCompiler* c, MvpFunction* f, AstCallNode* call, bool discard,
        MvpBinding* destination, bool propagate, MIR_reg_t output_destination) {
    if (f->js_export_name) {
        // the synthetic export supplies only arity; its Core adapter performs
        // the foreign call without treating the JsFunction object as code.
        MIR_type_t types[LAMBDA_MAX_FUNCTION_ARGS]; MIR_op_t operands[LAMBDA_MAX_FUNCTION_ARGS];
        MvpArgumentReader reader = {c, call, {}};
        for (int i = 0; i < f->abi.param_count; i++) {
            types[i] = MIR_T_I64; operands[i] = reg(c, mvp_read_argument_item(&reader, i));
        }
        MirCallOptions options = {true, false, 0};
        MirCallResult called = em_call_direct(&c->em, f->name, f->target, &f->abi,
            f->abi.param_count, types, operands, &options);
        MirValue result = em_finish_direct_call_normal(&c->em, called, MIR_PENDING_REASON_REP_CONVERSION);
        return value(c, result.reg, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, call);
    }
    MIR_reg_t environment = f->interp_binding ? 0 : function_environment(c, f);
    MIR_type_t types[LAMBDA_MAX_FUNCTION_ARGS + 1]; MIR_op_t ops[LAMBDA_MAX_FUNCTION_ARGS + 1];
    MirValue values[LAMBDA_MAX_FUNCTION_ARGS] = {};
    AstNode* actuals[LAMBDA_MAX_FUNCTION_ARGS] = {};
    int provided = mvp_call_argument_count(call);
    for (int i = 0; i < provided; i++) actuals[i] = mvp_call_argument(call, i);
    AstNamedNode* param = f->ast->param;
    MvpErrorTarget failure = {};
    MvpBinding* borrowed[LAMBDA_MAX_FUNCTION_ARGS] = {};
    MvpBinding projected[LAMBDA_MAX_FUNCTION_ARGS] = {};
    MvpPlace places[LAMBDA_MAX_FUNCTION_ARGS] = {};
    MvpBinding* snapshots[LAMBDA_MAX_FUNCTION_ARGS] = {};
    MvpArrayWitness witnesses[LAMBDA_MAX_FUNCTION_ARGS] = {};
    bool pure_arguments = true;
    // pure builtin allocations cannot mutate an earlier actual; its precise root still survives GC.
    for (int n = 0; n < provided || n < f->abi.param_count; n++)
        pure_arguments &= mvp_pure_key(actuals[n], 0, true);
    // a single borrow with pure scalar siblings cannot retain an ancestor or
    // change the selected place while later arguments are evaluated (S9.1.3).
    bool exclusive_place = pure_arguments;
    int var_count = 0;
    for (AstNamedNode* p = f->ast->param; p; p = (AstNamedNode*)p->next) {
        TypeParam* type = lambda_type_param(p->type);
        if (type && type->is_var_param) var_count++;
        else exclusive_place &= !ast_type_may_be_container(mvp_contract(p));
    }
    exclusive_place &= var_count == 1;
    int i = 0;
    for (; i < f->abi.param_count; param = (AstNamedNode*)param->next, i++) {
        AstNode* arg = actuals[i];
        if (!arg || ast_is_omitted_argument(arg)) {
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
                MirValue original;
                bool open_parameter = mvp_open_value_contract(local->contract);
                if (exclusive_place && (open_parameter || (places[i].leaf_contract &&
                        mvp_boundary_proven(places[i].leaf_contract, local->contract)))) {
                    // an exclusive open borrow needs ownership, not a layout
                    // proof. Capturing its leaf would force a copy per store.
                    MIR_reg_t prepared = prepare_place_owner(c, places[i].root);
                    MIR_reg_t leaf;
                    if (places[i].field && places[i].field_shape->is_trusted_contract) {
                        leaf = borrow_place_field(c, &places[i], prepared);
                    } else {
                        MIR_reg_t span = place_keys_span(c, &places[i]);
                        leaf = em_call_3(&c->em, "cow_path_borrow_keys", MIR_T_I64,
                            MIR_T_I64, reg(c, prepared), MIR_T_P, reg(c, span),
                            MIR_T_I64, imm(c, places[i].count), true);
                        release_argument_span(c, span, places[i].count);
                    }
                    MIR_label_t accepted = label(c);
                    branch(c, MIR_BNE, accepted, reg(c, operation(c, MIR_URSH, reg(c, leaf), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
                    stage_error(c, leaf); mark(c, accepted);
                    // an indexed terminal can be absent even when the root
                    // contract proves its present elements; retain admission.
                    Type* selected = !places[i].leaf_contract ? &TYPE_ANY : places[i].index_mask
                        ? lambda_type_nullable_normalized(c->module->tp->pool, places[i].leaf_contract)
                        : places[i].leaf_contract;
                    original = value(c, leaf, selected->type_id, VALUE_REP_ITEM, selected);
                    places[i].borrowed_original = leaf;
                } else {
                    // place_path already evaluated the keys. A snapshot read
                    // must consume those keys once, including effectful calls.
                    original = read_place(c, &places[i], places[i].count);
                    original = value(c, em_call_1(&c->em, cow_import("cow_capture_value", "cow_capture_value_profiled"), MIR_T_I64,
                        MIR_T_I64, reg(c, original.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
                }
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
        // snapshot elision needs no physical layout proof. An unchanged
        // boundary and the shared effects suffice for a nonretaining reader.
        bool observes_parameter = pure_arguments && source && param->entry &&
            !lambda_type_param(param->type)->is_var_param &&
            // a retained projection captures its own child. Without reentry,
            // that boundary suffices; a whole-root escape still needs capture.
            !param->entry->cow_param_mutated && (!param->entry->cow_param_retained ||
                (!param->entry->cow_param_root_retained && !f->abi.effects.may_reenter)) &&
            mvp_boundary_proven(source->contract, mvp_contract(param));
        // a confined callee copies at entry. Pure actuals and absence of a
        // simultaneous var borrow preserve the argument-evaluation snapshot.
        MvpBinding* parameter = mvp_binding(c->module, param->entry);
        bool entry_snapshot = pure_arguments && parameter && parameter->scalar_snapshot;
        bool read_only = source && (readonly_parameter || observes_parameter || entry_snapshot);
        // a simultaneous borrow of the same owner can mutate the plain argument's snapshot (S9.1.2).
        AstNamedNode* other = f->ast->param;
        for (int a = 0; read_only && a < f->abi.param_count; a++, other = (AstNamedNode*)other->next) {
            TypeParam* type = lambda_type_param(other->type);
            if (type && type->is_var_param && mvp_place_root(c->module, actuals[a]) == source) read_only = false;
        }
        if (read_only && entry_snapshot) snapshots[i] = source;
        if (borrowed[i] == &projected[i]) source = borrowed[i];
        MirValue v = {};
        if (!borrowed[i] && !read_only)
            v = contextual_fill(c, arg, mvp_contract(param), parameter ? parameter->array_cert : NULL,
                parameter_boundary_site(c, param), readonly_parameter && mvp_parameter_borrowable(c->module, param, false));
        if (!v.reg) v = borrowed[i] || read_only ? read_binding(c, source, arg, false) : expression(c, arg);
        if (call && (call->fn_colour_guard & (1u << i))) {
            // a statically resolved polymorphic slot still needs its runtime
            // colour check before the callee enters (S12.1.4v4).
            MirValue boxed = convert(c, v, VALUE_REP_ITEM);
            MIR_reg_t checked = em_call_1(&c->em, "lambda_fn_colour_arg_check", MIR_T_I64,
                MIR_T_I64, reg(c, boxed.reg), true);
            check_item_result(c, checked, &failure);
            v = value(c, checked, boxed.semantic_type, VALUE_REP_ITEM, boxed.semantic_contract, arg);
        }
        v = mvp_function_binder_count(f) ? convert(c, v, VALUE_REP_ITEM)
            : admit_parameter_argument(c, param, v, &failure, (AstNode*)call);
        if (borrowed[i] && borrowed[i]->array_native && !places[i].root && pure_arguments &&
                !c->dense_arrays && c->dense_callee != f &&
                f->abi.effects.may_gc && ast_body_may_write_entry(f->ast->body, param->entry, false) &&
                mvp_array_element(borrowed[i]->contract) &&
                mvp_admission_proven(c->module, borrowed[i]->contract, mvp_contract(param)) &&
                mvp_parameter_storage_stable(c->module, param)) {
            // detach an indexed owner once before its closed var borrow.
            // Dense regions and forwarding-only owners keep their existing
            // noncollecting re-borrow path and exclusivity proof.
            MIR_label_t ready = label(c);
            if (borrowed[i]->array_writable)
                branch(c, MIR_BNE, ready, reg(c, borrowed[i]->array_writable), imm(c, 0));
            prepare_place_owner(c, borrowed[i]);
            mark(c, ready);
            // either arm observes the binding, whose precise home now owns
            // any detached storage; never join on a slow-arm-only register.
            v = read_binding(c, borrowed[i], arg, false);
        }
        v = convert(c, v, f->abi.params[i].canonical_rep);
        values[i] = v;
        types[i] = v.mir_type; ops[i] = reg(c, v.reg);
        if (pure_arguments && source && source->array_native &&
                mvp_array_element(source->contract) == mvp_array_element(mvp_contract(param)) &&
                mvp_boundary_proven(source->contract, mvp_contract(param)))
            witnesses[i] = {source->array_native, source->array_writable, source->array_length, 0};
        if (c->dense_arrays && source) witnesses[i].items = source->dense_items;
    }
    int call_count = i;
    param = f->ast->param;
    for (int n = 0; n < i; n++, param = (AstNamedNode*)param->next) {
        if (!borrowed[n]) continue;
        MvpBinding* root = places[n].root ? places[n].root : borrowed[n];
        bool changed = false;
        for (int a = n + 1; a < provided; a++) if (actuals[a])
            changed |= ast_body_may_write_entry(actuals[a], root->entry, true, actuals[a]->next) ||
                (root->capture_home && !mvp_pure_key(actuals[a], 0, true));
        if (!changed) continue;
        // later arguments can re-borrow and replace this owner. Publish the
        // live place after their effects, keeping plain actuals' snapshots (S9.1.3v3).
        MirValue current = places[n].root ? read_place(c, &places[n], places[n].count)
            : read_binding(c, root, actuals[n], false);
        current = mvp_function_binder_count(f) ? convert(c, current, VALUE_REP_ITEM)
            : admit_parameter_argument(c, param, current, &failure, (AstNode*)call);
        current = convert(c, current, f->abi.params[n].canonical_rep);
        values[n] = current; types[n] = current.mir_type; ops[n] = reg(c, current.reg);
        witnesses[n] = {};
    }
    if (mvp_variadic_function(f)) {
        MvpArgumentReader reader = {c, call, {}};
        types[i] = MIR_T_I64;
        ops[i] = provided > i
            ? reg(c, em_argument_array(&c->em, i, provided, mvp_read_argument_item, &reader))
            : imm(c, ITEM_NULL);
        call_count++;
    }
    // the closed body already guards unchanged owners and every access extent.
    // Self recursion reuses that proof; bounded leaf accessors expand natively.
    bool allocating_leaf = c->guarded_allocating && dense_call_arguments(c, f, call);
    bool closed_call = c->dense_calls && c->dense_arrays && function_has_arrays(c->module, f);
    bool scalar_closed = c->dense_calls && c->dense_arrays && !function_has_arrays(c->module, f) &&
        dense_call_eligible(c, call);
    bool recursive = closed_call && f == c->function;
    bool dense_call = c->dense_callee == f || closed_call || allocating_leaf;
    // bound small recursive expansions by the existing inline chain. The next
    // self edge retains a stack-guarded real call instead of growing MIR forever.
    int recursive_depth = 0;
    for (MvpInline* p = c->inlining; p; p = p->parent)
        if (p->function == f) recursive_depth++;
    bool inlined = output_destination || (recursive ? recursive_depth < 2 && f->inline_cost <= 256 :
        dense_call || scalar_closed || inline_eligible(c, f, discard));
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
            span = argument_span(c, i);
        }
        homes[n] = operation(c, MIR_ADD, reg(c, span), imm(c, n * sizeof(Item)));
        MirValue item = convert(c, values[n], VALUE_REP_ITEM);
        em_store_at(&c->em, homes[n], 0, MIR_T_I64, item.reg);
        em_publish_var_home(&c->em, n, homes[n]);
    }
    MirCallResult result = {};
    if (inlined) {
        // admission failures skip the inlined body and join through the same call boundary below.
        MvpFunction* saved_callee = c->dense_callee;
        if (closed_call || allocating_leaf) c->dense_callee = f;
        result = inline_call(c, f, ops, values, witnesses, output_destination);
        c->dense_callee = saved_callee;
    } else if (f->interp_binding) {
        // reuse ordinary argument admission, precise homes and write-back;
        // only the invocation crosses into the resident T0/boxed entry.
        Script* owner; int slot;
        mvp_interp_binding(c->module, f->interp_binding, &owner, &slot);
        MIR_reg_t state = em_module_state_for_unit(&c->em, constant(c, script_module_layout_id(owner)), true);
        MIR_reg_t callable = em_module_slot_item(&c->em, state, imm(c, slot), true);
        value(c, callable, LMD_TYPE_FUNC, VALUE_REP_ITEM, &TYPE_FUNC);
        struct PreparedArguments { MvpCompiler* compiler; MIR_op_t* values; int fixed; } prepared = {c, ops, i};
        // direct lowering already evaluated the rest array. The dynamic
        // dispatcher receives its individual actuals, not another rest wrapper.
        MIR_reg_t arguments = em_argument_array(&c->em, 0, provided > i ? provided : i,
            [](void* data, int index) -> MIR_reg_t {
                PreparedArguments* p = (PreparedArguments*)data;
                MvpCompiler* c = p->compiler;
                if (index < p->fixed) {
                    MIR_op_t operand = p->values[index];
                    if (operand.mode == MIR_OP_REG) return operand.u.reg;
                    // omitted optionals are immediate markers, not register
                    // IDs; preserve them through the resident-call adapter.
                    MIR_reg_t staged = em_new_reg(&c->em, "resident_argument", MIR_T_I64);
                    move(c, staged, operand);
                    return staged;
                }
                return em_call_2(&c->em, "array_get", MIR_T_I64,
                    MIR_T_P, p->values[p->fixed], MIR_T_I64, imm(c, index - p->fixed), true);
            }, &prepared);
        int home = em_scalar_home_new(&c->em);
        MIR_reg_t destination = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, home));
        MIR_reg_t called = em_call_3(&c->em, "fn_call_borrowed_into", MIR_T_I64,
            MIR_T_I64, reg(c, callable), MIR_T_P, reg(c, arguments), MIR_T_P, reg(c, destination), true);
        result.normal = value(c, called, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    } else {
        MirCallOptions options = {true, environment != 0, environment};
        FnVariantAnalysis abi = f->abi;
        if (recursive) abi.effects.may_gc = abi.effects.may_return_error = false;
        result = em_call_direct(&c->em, f->name, f->target, &abi, call_count, types, ops, &options);
    }
    param = f->ast->param;
    for (int n = 0; n < i; n++, param = (AstNamedNode*)param->next) if (borrowed[n] && !dense_call) {
        MirValue updated = value(c, inlined ? ops[n].u.reg :
            em_load_at(&c->em, homes[n], 0, MIR_T_I64, "var_reloaded"),
            mvp_contract(param)->type_id,
            VALUE_REP_ITEM, mvp_contract(param));
        updated = convert(c, updated, borrowed[n]->rep);
        move(c, borrowed[n]->reg, reg(c, updated.reg), updated.mir_type);
        if (inlined && witnesses[n].native && borrowed[n]->array_native) {
            move(c, borrowed[n]->array_native, reg(c, witnesses[n].native));
            move(c, borrowed[n]->array_writable, reg(c, witnesses[n].writable));
            move(c, borrowed[n]->array_length, reg(c, witnesses[n].length));
        } else if (!mvp_parameter_storage_stable(c->module, param)) refresh_array(c, borrowed[n]);
        publish_var_binding(c, borrowed[n]);
        if (!inlined && homes[n] != borrowed[n]->var_home)
            em_store_at(&c->em, homes[n], 0, MIR_T_I64, constant(c, ITEM_NULL));
        if (places[n].root)
            write_place(c, places[n], value(c, borrowed[n]->reg, borrowed[n]->contract->type_id,
                borrowed[n]->rep, borrowed[n]->contract), call);
    }
    c->argument_active = active;
    // a snapshot miss shares the original before detaching. Reload the caller's
    // COW witness instead of retaining its pre-call exclusivity fact.
    for (int n = 0; n < i; n++) if (snapshots[n]) refresh_array(c, snapshots[n]);
    Type* result_contract = inlined ? mvp_integer_leaf_result(c->module, call) : NULL;
    if (!result_contract) result_contract = f->result_contract;
    if (output_destination) {
        MIR_reg_t status = result.normal.reg;
        MIR_label_t done = label(c);
        if (result.error.reg) {
            branch(c, MIR_BEQ, done, reg(c, result.error.reg), imm(c, ITEM_NULL));
            move(c, status, reg(c, result.error.reg));
        }
        mark(c, done);
        if (failure.done) {
            MIR_label_t completed = label(c); jump(c, completed); mark(c, failure.done);
            move(c, status, reg(c, failure.error)); mark(c, completed);
        }
        return value(c, status, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, call);
    }
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
        normal.semantic_contract = result_contract;
        if (normal.rep == VALUE_REP_ITEM && f->abi.effects.may_return_error)
            normal = value(c, normal.reg, LMD_TYPE_ANY, VALUE_REP_ITEM,
                lambda_type_union_normalized(c->module->tp->pool, result_contract, &TYPE_ERROR));
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
    // optional descriptors are type-expression nodes; restore their native
    // scalar category too, especially Bool? before Item boxing (D2.3.5).
    v = value(c, v.reg, result_contract->type_id, v.rep, result_contract, call);
    if (v.rep == VALUE_REP_ITEM && f->abi.effects.may_return_error)
        v = value(c, v.reg, LMD_TYPE_ANY, VALUE_REP_ITEM,
            lambda_type_union_normalized(c->module->tp->pool, result_contract, &TYPE_ERROR));
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
        return value(c, v.reg, v.semantic_type, v.rep, result_contract);
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
        MirValue joined_value = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM,
            lambda_type_union_normalized(c->module->tp->pool, result_contract, &TYPE_ERROR));
        // call-site failures join only errors; the callee's success value
        // passed its return contract even with Item transport (D7.4.2, S7.7.3).
        if (!lambda_type_accepts_error(result_contract))
            joined_value.admitted_success_contract = result_contract;
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
    return mvp_binary_bitwise(fn) ? call : NULL;
}

static bool bitwise_tree_plan(MvpCompiler* c, AstNode* source, unsigned* operations, unsigned depth = 0) {
    if (depth > 8) return false;
    if (AstCallNode* call = bitwise_expression(source)) {
        if (++*operations > 8) return false;
        return bitwise_tree_plan(c, mvp_call_argument(call, 0), operations, depth + 1) &&
            bitwise_tree_plan(c, mvp_call_argument(call, 1), operations, depth + 1);
    }
    return mvp_pure_key(source) && mvp_value_contract(c->module, source)->type_id == LMD_TYPE_INT;
}

static MIR_reg_t native_bitwise_tree(MvpCompiler* c, AstNode* source, MIR_label_t checked) {
    AstCallNode* call = bitwise_expression(source);
    if (!call) return em_guard_finite_int(&c->em, expression(c, source), checked);
    SysFunc fn = ((AstSysFuncNode*)ast_unwrap_primary_to_leaf(call->function))->fn_info->fn;
    MIR_reg_t left = native_bitwise_tree(c, mvp_call_argument(call, 0), checked);
    MIR_reg_t right = native_bitwise_tree(c, mvp_call_argument(call, 1), checked);
    if (fn == SYSFUNC_SHL || fn == SYSFUNC_SHR)
        branch(c, MIR_UBGE, checked, reg(c, right), imm(c, 64));
    MIR_reg_t result = operation(c, em_integer_bitwise_opcode(fn), reg(c, left), reg(c, right));
    // every intermediate must remain finite: saturation may make a later
    // bitwise call fail. The checked tree retains that exact behavior (S4.1.2).
    em_branch_int53_outside(&c->em, result, checked);
    return result;
}

static MIR_reg_t collection_mutation_call(MvpCompiler* c, SysFunc fn, MIR_reg_t owner,
        MirValue* args, bool cow) {
    if (fn == SYSPROC_PUSH)
        return em_call_2(&c->em, cow ? "pn_push_cow" : "pn_push", MIR_T_I64,
            MIR_T_I64, reg(c, owner), MIR_T_I64, reg(c, args[0].reg), true);
    const char* helper = fn == SYSPROC_SPLICE ? (cow ? "pn_splice_cow" : "pn_splice") :
        (cow ? cow_import("member_set_cow", "member_set_cow_profiled") : "fn_index_set");
    return em_call_3(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, owner),
        MIR_T_I64, reg(c, args[0].reg), MIR_T_I64, reg(c, args[1].reg), true);
}

static MirValue mutate_collection(MvpCompiler* c, AstCallNode* node, SysFuncInfo* info) {
    MvpPlace place = place_path(c, mvp_call_argument(node, 0));
    if (!place.root) {
        mvp_reject(c->module, node, "collection mutation requires a resolved place");
        return null_value(c);
    }
    MirValue args[2], first = {}; int count = 0;
    for (int i = 1; i < mvp_call_argument_count(node); i++) {
        AstNode* arg = mvp_call_argument(node, i);
        MirValue input = expression(c, arg);
        if (!count) first = input;
        args[count++] = convert(c, input, VALUE_REP_ITEM);
    }
    MirValue owner = convert(c, read_binding(c, place.root, mvp_call_argument(node, 0), false), VALUE_REP_ITEM);
    const char* boundary = place.root->entry->name->chars;
    MIR_reg_t result;
    if (place.count) {
        // detach and install the spine before entering an in-place mutator;
        // every key and value is already rooted (S9.2.2).
        MIR_reg_t detached = em_call_1(&c->em, cow_import("cow_prepare_write", "cow_prepare_write_profiled"), MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), true);
        publish_place(c, place.root, detached);
        MIR_reg_t path = em_call_0(&c->em, "array_plain", MIR_T_P, true);
        value(c, path, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
        for (int i = 0; i < place.count; i++)
            em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P, reg(c, path),
                MIR_T_I64, reg(c, place.keys[i]), true);
        MIR_reg_t leaf = em_call_2(&c->em, "cow_place_leaf", MIR_T_I64,
            MIR_T_I64, reg(c, detached), MIR_T_I64, reg(c, path), true);
        value(c, leaf, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
        result = collection_mutation_call(c, info->fn, leaf, args, false);
    } else {
        Type* contract = place.root->inferred_append ? place.root->contract : place.root->admission_contract;
        bool typed = lambda_array_contract_element(contract) != NULL;
        bool native = info->fn == SYSPROC_PUSH && mvp_array_element(contract) == &TYPE_INT &&
            lambda_array_contract_is_plain(contract) && !lambda_type_counts_array_length(contract) &&
            first.rep == VALUE_REP_INT_LANE && mvp_boundary_proven(first.semantic_contract, &TYPE_INT);
        Type* normal = first.admitted_success_contract ? first.admitted_success_contract :
            lambda_type_remove_error(c->module->tp->pool, first.semantic_contract);
        bool optional_string = false;
        Type* string_base = lambda_type_nullable_lane_base(normal, &optional_string);
        bool string_push = info->fn == SYSPROC_PUSH && !typed && string_base && string_base->type_id == LMD_TYPE_STRING;
        MIR_label_t appended = native || string_push ? label(c) : NULL;
        MIR_reg_t fast = appended ? em_new_reg(&c->em, "appended_owner", MIR_T_I64) : 0;
        if (native) {
            MIR_label_t cold = label(c);
            MIR_reg_t array = em_guard_container(&c->em, owner.reg, LMD_TYPE_ARRAY_NUM, cold);
            em_array_num_elem_guard(&c->em, array, ELEM_INT, cold);
            em_array_rep_cert_guard(&c->em, array, contract, cold);
            em_container_cow_guard(&c->em, array, cold, true);
            em_branch_int53_outside(&c->em, first.reg, cold);
            // spare capacity needs one lane store; growth, snapshots and
            // unproven values keep the checked append's error path.
            em_array_num_append(&c->em, array, first.reg, ELEM_INT, cold);
            move(c, fast, reg(c, owner.reg)); jump(c, appended); mark(c, cold);
        }
        if (string_push) {
            MIR_label_t cold = label(c);
            em_open_string_append(&c->em, owner.reg, args[0].reg, cold);
            move(c, fast, reg(c, owner.reg)); jump(c, appended); mark(c, cold);
        }
        result = info->fn == SYSPROC_PUSH && typed
            ? em_call_4(&c->em, "lambda_array_push_checked", MIR_T_I64,
                MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, args[0].reg),
                MIR_T_P, imm(c, (intptr_t)contract), MIR_T_P, imm(c, (intptr_t)boundary), true)
            : collection_mutation_call(c, info->fn, owner.reg, args, true);
        if (appended) { move(c, fast, reg(c, result)); mark(c, appended); result = fast; }
        MIR_label_t rejected = label(c);
        branch(c, MIR_BEQ, rejected, reg(c, operation(c, MIR_URSH, reg(c, result), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        publish_place(c, place.root, result); mark(c, rejected);
    }
    return value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
}

static MirValue dynamic_call(MvpCompiler* c, AstCallNode* node) {
    // a pipe's receiver is evaluated before its callable expression, once.
    MirValue receiver = {};
    if (node->pipe_inject) receiver = convert(c,
        capture_projection(c, expression(c, node->pipe_receiver), node->pipe_receiver), VALUE_REP_ITEM);
    AstNode* target = ast_unwrap_primary_to_leaf(node->function);
    bool member = target && (target->node_type == AST_NODE_MEMBER_EXPR ||
        target->node_type == AST_NODE_INDEX_EXPR) &&
        ((AstFieldNode*)target)->field && !((AstFieldNode*)target)->field->next &&
        !ast_member_wildcard((AstFieldNode*)target);
    MirValue callee;
    if (member) {
        AstFieldNode* access = (AstFieldNode*)target;
        MvpPlace place = place_path(c, access->object);
        MirValue owner = place.root ? read_place(c, &place, place.count)
            : convert(c, expression(c, access->object), VALUE_REP_ITEM);
        MirValue key = target->node_type == AST_NODE_MEMBER_EXPR
            ? cow_path_key(c, access->field, true) : subscript_key(c, access->field, owner);
        key = convert(c, key, VALUE_REP_ITEM);
        MIR_reg_t selected = em_call_2(&c->em,
            target->node_type == AST_NODE_MEMBER_EXPR ? "fn_member" : "fn_index", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, key.reg), true);
        callee = value(c, selected, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
        bool writable = place.root && place.root->entry->is_mutable;
        MIR_reg_t home = 0, keys = 0;
        if (writable) {
            home = argument_span(c, 1);
            MirValue root = convert(c, read_binding(c, place.root, NULL, false), VALUE_REP_ITEM);
            em_store_at(&c->em, home, 0, MIR_T_I64, root.reg);
            if (place.count) keys = place_keys_span(c, &place);
        }
        // dynamic receiver kinds need the same writable-place boundary as
        // statically resolved pn methods, with every projection evaluated once.
        MIR_reg_t prepared = em_call_4(&c->em, "lambda_object_prepare_method_call", MIR_T_I64,
            MIR_T_I64, reg(c, callee.reg), MIR_T_P, home ? reg(c, home) : imm(c, 0),
            MIR_T_P, keys ? reg(c, keys) : imm(c, 0), MIR_T_I64, imm(c, place.count), true);
        callee = value(c, prepared, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
        if (writable) {
            publish_place(c, place.root, em_load_at(&c->em, home, 0, MIR_T_I64, "method_owner"));
            if (keys) release_argument_span(c, keys, place.count);
            release_argument_span(c, home, 1);
        }
    } else callee = convert(c, expression(c, node->function), VALUE_REP_ITEM);
    MIR_reg_t result = em_new_reg(&c->em, "dynamic_result", MIR_T_I64);
    MIR_label_t done = label(c);
    move(c, result, reg(c, callee.reg));
    // preserve a callee error before evaluating argument effects.
    branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_URSH, reg(c, callee.reg), imm(c, 56))),
        imm(c, LMD_TYPE_ERROR));
    MvpArgumentReader reader = {c, node, receiver};
    MIR_reg_t arguments = em_argument_array(&c->em, 0, mvp_call_argument_count(node),
        mvp_read_argument_item, &reader);
    if (node->fn_colour_guard) {
        MIR_reg_t refused = em_call_3(&c->em, "lambda_fn_colour_guard_list", MIR_T_I64,
            MIR_T_I64, reg(c, callee.reg), MIR_T_P, reg(c, arguments),
            MIR_T_I64, imm(c, node->fn_colour_guard), true);
        move(c, result, reg(c, refused));
        branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_URSH, reg(c, refused), imm(c, 56))),
            imm(c, LMD_TYPE_ERROR));
    }
    // the dispatcher uses the same interpreted/native ABI decision as Full;
    // this frame owns any wide scalar returned by the callback (D6.2.3/D7.4.2).
    int slot = em_scalar_home_new(&c->em);
    MIR_reg_t home = em_materialize_frame_ref(&c->em, em_scalar_home_ref(&c->em, slot));
    MIR_reg_t called = em_call_3(&c->em, "fn_call_into", MIR_T_I64,
        MIR_T_I64, reg(c, callee.reg), MIR_T_P, reg(c, arguments), MIR_T_P, reg(c, home), true);
    MirValue outcome = value(c, called, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    if (Type* contract = ast_call_contract_return(node))
        outcome = admit_outcome(c, outcome, contract, "function return");
    move(c, result, reg(c, outcome.reg)); mark(c, done);
    return value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
}

static MIR_reg_t ascii_slice_character(MvpCompiler* c, MirValue* arguments, MIR_label_t miss) {
    MirValue owner = convert(c, arguments[0], VALUE_REP_ITEM);
    branch(c, MIR_BEQ, miss, reg(c, em_exact_item_type_test(&c->em, owner.reg, LMD_TYPE_STRING)), imm(c, 0));
    MIR_reg_t start = em_guard_finite_int(&c->em, arguments[1], miss);
    MIR_reg_t end = em_guard_finite_int(&c->em, arguments[2], miss);
    branch(c, MIR_BNE, miss, reg(c, operation(c, MIR_SUB, reg(c, end), reg(c, start))), imm(c, 1));
    owner = convert(c, value(c, owner.reg, LMD_TYPE_STRING, VALUE_REP_ITEM, &TYPE_STRING), VALUE_REP_RAW_GC_POINTER);
    branch(c, MIR_BEQ, miss, reg(c, owner.reg), imm(c, 0));
    MIR_reg_t byte = em_ascii_string_byte(&c->em, owner.reg, start, miss);
    MIR_reg_t text = em_call_1(&c->em, "get_ascii_char_string", MIR_T_P, MIR_T_I64, reg(c, byte), true);
    branch(c, MIR_BEQ, miss, reg(c, text), imm(c, 0));
    // only a present ASCII character uses the table. Clamping, empty slices,
    // Unicode, absent arguments and alternate receivers keep fn_slice3.
    return convert(c, value(c, text, LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING), VALUE_REP_ITEM).reg;
}

static void hoist_loop_calls(MvpCompiler* c, AstNode* loop) {
    AstIndex* index = &c->module->tp->ast_index;
    AstNodeId root = ast_index_find(index, loop), end = ast_index_subtree_end(index, root);
    for (AstNodeId id = root; id < end; id++) {
        AstNode* node = index->nodes[id];
        if (node->node_type != AST_NODE_CALL_EXPR || mvp_node_owner(c->module, id) != c->function->id) continue;
        AstCallNode* call = (AstCallNode*)node;
        AstNode* target = ast_unwrap_primary_to_leaf(call->function);
        if (!target || target->node_type != AST_NODE_SYS_FUNC) continue;
        SysFuncInfo* info = ((AstSysFuncNode*)target)->fn_info;
        MvpBinding* argument = mvp_local_binding(c->module, mvp_call_argument(call, 0));
        if (!argument || argument->contract != &TYPE_FLOAT || argument->owner != c->function->id ||
                !mvp_native_math(info, LMD_TYPE_FLOAT) || argument->var_home || argument->capture_home ||
                argument->entry->lambda_shared_local ||
                ast_index_node_descends(index, ast_index_find(index, mvp_binding_node(argument)), root) ||
                ast_body_may_write_entry(loop, argument->entry, true)) continue;
        JitImportMetadata metadata = {};
        // only audited total native math can run on a zero-trip path.
        if (!jit_import_get_metadata(info->native_c_name, &metadata) || metadata.gc_effect != JIT_EFFECT_NO_GC ||
                metadata.reentry_effect != JIT_REENTRY_NO) continue;
        bool present = false;
        for (MvpHoistedCall* h = c->hoisted_calls; h; h = h->next) present |= h->call == call;
        if (present) continue;
        MvpHoistedCall* h = (MvpHoistedCall*)mem_calloc(1, sizeof(*h), MEM_CAT_TEMP);
        if (!h) continue;
        h->next = c->hoisted_calls; h->call = call; h->argument = argument; h->helper = info->native_c_name;
        for (MvpHoistedCall* previous = c->hoisted_calls; previous; previous = previous->next)
            if (previous->argument == argument && !strcmp(previous->helper, h->helper)) { h->result = previous->result; break; }
        if (!h->result.reg) {
            MirValue input = convert(c, read_binding(c, argument, mvp_call_argument(call, 0), false), VALUE_REP_F64);
            h->result = value(c, em_call_1(&c->em, h->helper, MIR_T_D, MIR_T_D, reg(c, input.reg), true),
                LMD_TYPE_FLOAT, VALUE_REP_F64, &TYPE_FLOAT, call);
        }
        c->hoisted_calls = h;
    }
}

static void restore_hoisted_calls(MvpCompiler* c, MvpHoistedCall* saved) {
    while (c->hoisted_calls != saved) {
        MvpHoistedCall* current = c->hoisted_calls;
        c->hoisted_calls = current->next; mem_free(current);
    }
}

static MirValue call(MvpCompiler* c, AstCallNode* node, bool discard) {
    for (MvpHoistedCall* h = c->hoisted_calls; h; h = h->next)
        if (h->call == node) return h->result;
    if (Type* target = ast_numeric_type_call_target(node)) {
        MirValue input = convert(c, expression(c, node->argument), VALUE_REP_ITEM);
        return value(c, em_numeric_type_call(&c->em, target, input.reg),
            LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    MvpFunction* f = mvp_function(c->module, ast_direct_call_function(node));
    if (f) {
        if (f->result_contract == &TYPE_STRING) {
            MirValue joined = string_concat(c, node, NULL, NULL, NULL);
            if (joined.reg) return joined;
        }
        return direct_call(c, f, node, discard, NULL, node->propagate);
    }
    AstNode* target = ast_unwrap_primary_to_leaf(node->function);
    if (!target || target->node_type != AST_NODE_SYS_FUNC) return dynamic_call(c, node);
    SysFuncInfo* info = ((AstSysFuncNode*)target)->fn_info;
    if (info->fn == SYSPROC_PRINT) {
        Type* contract = mvp_value_contract(c->module, node);
        return value(c, em_print_arguments(&c->em, node->argument, true,
            node->pipe_inject ? node->pipe_receiver : NULL),
            contract->type_id, VALUE_REP_ITEM, contract, node);
    }
    if (info->fn == SYSPROC_SELECT)
        return value(c, em_select_tasks(&c->em, node), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    if (info->fn == SYSPROC_PUSH || info->fn == SYSPROC_SPLICE || info->fn == SYSPROC_VMAP_SET)
        return mutate_collection(c, node, info);
    if (info->fn == SYSFUNC_ORD) {
        AstFieldNode* index = mvp_string_index(c->module, mvp_call_argument(node, 0));
        if (index) return string_character(c, index, true);
    }
    if (info->fn == SYSFUNC_LEN) {
        AstNode* source = ast_unwrap_primary_to_leaf(mvp_call_argument(node, 0));
        AstCallNode* find = source && source->node_type == AST_NODE_CALL_EXPR ? (AstCallNode*)source : NULL;
        AstNode* target = find ? ast_unwrap_primary_to_leaf(find->function) : NULL;
        SysFuncInfo* search = target && target->node_type == AST_NODE_SYS_FUNC ? ((AstSysFuncNode*)target)->fn_info : NULL;
        if (find && !find->propagate && search &&
                (search->fn == SYSFUNC_FIND || search->fn == SYSFUNC_FIND3) &&
                mvp_call_argument_count(find) == (search->fn == SYSFUNC_FIND ? 2 : 3)) {
            // the intermediate match array has no observer. Keep argument
            // evaluation and checked errors while omitting its records.
            MIR_op_t args[3]; MIR_type_t types[3];
            int count = 0;
            for (; count < mvp_call_argument_count(find); count++) {
                AstNode* arg = mvp_call_argument(find, count);
                MirValue input = convert(c, expression(c, arg), VALUE_REP_ITEM);
                args[count] = reg(c, input.reg); types[count] = MIR_T_I64;
            }
            MIR_reg_t result = em_call_with_args(&c->em, count == 2 ? "fn_len_find2" : "fn_len_find3",
                MIR_T_I64, count, types, args, true);
            MirValue v = value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM,
                mvp_value_contract(c->module, node), node);
            v.admitted_success_contract = &TYPE_INT;
            return v;
        }
    }
    Type* input_contract = mvp_value_contract(c->module, mvp_call_argument(node, 0), 0, c->dense_arrays);
    TypeId input = input_contract->type_id;
    if (info->fn == SYSFUNC_STRING && input == LMD_TYPE_STRING)
        return expression(c, mvp_call_argument(node, 0));
    bool optional = false, identity = false;
    Type* base = lambda_type_nullable_lane_base(input_contract, &optional);
    Type* compact = lambda_type_remove_error(c->module->tp->pool, input_contract);
    LambdaNumericKind compact_kind = lambda_numeric_kind_from_type(compact);
    int compact_bits = lambda_numeric_sized_bits(compact_kind);
    if (info->fn == SYSFUNC_INT && compact && compact->type_id == LMD_TYPE_NUM_SIZED &&
            lambda_numeric_is_sized_integer(compact_kind) && compact_bits > 0 && compact_bits <= 32) {
        MirValue operand = convert(c, expression(c, mvp_call_argument(node, 0)), VALUE_REP_ITEM);
        bool outcome = lambda_type_accepts_error(input_contract);
        MIR_reg_t joined = outcome ? em_new_reg(&c->em, "compact_int_outcome", MIR_T_I64) : 0;
        MIR_label_t done = outcome ? label(c) : NULL;
        if (outcome) {
            move(c, joined, reg(c, operand.reg));
            branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_URSH, reg(c, operand.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        }
        MIR_reg_t raw = operation(c, MIR_AND, reg(c, operand.reg), imm(c, (UINT64_C(1) << compact_bits) - 1));
        if (!lambda_numeric_sized_is_unsigned(compact_kind)) {
            raw = operation(c, MIR_LSH, reg(c, raw), imm(c, 64 - compact_bits));
            raw = operation(c, MIR_RSH, reg(c, raw), imm(c, 64 - compact_bits));
        }
        if (!outcome) return value(c, raw, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, node);
        // every compact integer is finite in int53; preserve its error sibling
        // instead of redispatching the successful cast (S4.4.1).
        move(c, joined, reg(c, em_box_finite_int_item(&c->em, raw))); mark(c, done);
        MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        result.admitted_success_contract = &TYPE_INT;
        return result;
    }
    if ((info->fn == SYSFUNC_INT || info->fn == SYSFUNC_FLOAT) &&
            numeric_node_kind(c, mvp_call_argument(node, 0)) == LAMBDA_NUM_U32) {
        MirValue operand = expression(c, mvp_call_argument(node, 0));
        bool exact = numeric_kind(operand) == LAMBDA_NUM_U32;
        bool outcome = operand.admitted_success_contract != NULL;
        operand = convert(c, operand, VALUE_REP_ITEM);
        if (exact) {
            // an admitted u32 is inline and nonnegative; int/float conversion
            // needs neither a number home nor another numeric dispatcher.
            MIR_label_t slow = outcome ? label(c) : NULL, done = outcome ? label(c) : NULL;
            if (slow) em_guard_u32_item(&c->em, operand.reg, slow);
            MIR_reg_t lane = operation(c, MIR_AND, reg(c, operand.reg), imm(c, UINT32_MAX));
            MirValue number = value(c, lane, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, node);
            if (info->fn == SYSFUNC_FLOAT) number = convert(c, number, VALUE_REP_F64);
            if (!outcome) return number;
            MIR_reg_t joined = convert(c, number, VALUE_REP_ITEM).reg;
            jump(c, done); mark(c, slow);
            move(c, joined, reg(c, em_call_1(&c->em, info->c_func_name, MIR_T_I64,
                MIR_T_I64, reg(c, operand.reg), true)));
            mark(c, done);
            MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
            result.admitted_success_contract = info->fn == SYSFUNC_INT ? &TYPE_INT : &TYPE_FLOAT;
            return result;
        }
        return value(c, em_call_1(&c->em, info->c_func_name, MIR_T_I64,
            MIR_T_I64, reg(c, operand.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    if (base == &TYPE_INT && sysfunc_native_math_type_preserving(info->fn, &identity)) {
        MirValue operand = convert(c, expression(c, mvp_call_argument(node, 0)), VALUE_REP_INT_LANE);
        MIR_reg_t r = identity ? operand.reg : em_int_lane_abs(&c->em, operand.reg);
        return value(c, r, LMD_TYPE_INT, VALUE_REP_INT_LANE, operand.semantic_contract, node);
    }
    Type* normal_input = lambda_type_remove_error(c->module->tp->pool, input_contract);
    MvpBinding* length_owner = info->fn == SYSFUNC_LEN
        ? mvp_local_binding(c->module, mvp_call_argument(node, 0)) : NULL;
    if (length_owner && length_owner->array_length && mvp_array_element(normal_input) &&
            !lambda_type_accepts_error(length_owner->contract) && !length_owner->entry->lambda_shared_local) {
        // collection moves storage, not logical extent. Every owner replacement
        // and var writeback already refreshes this cached rank-one length.
        return value(c, length_owner->array_length, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, node);
    }
    if (length_owner && length_owner->rep == VALUE_REP_ITEM && length_owner->array_boxed) {
        MIR_label_t slow = label(c), done = label(c);
        MIR_reg_t plain = operation(c, MIR_OR, reg(c, length_owner->array_boxed), reg(c, length_owner->array_native));
        plain = operation(c, MIR_OR, reg(c, plain), reg(c, length_owner->array_float_native));
        plain = operation(c, MIR_OR, reg(c, plain), reg(c, length_owner->array_bool_native));
        branch(c, MIR_BEQ, slow, reg(c, plain), imm(c, 0));
        MIR_reg_t joined = convert(c, value(c, length_owner->array_length,
            LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT), VALUE_REP_ITEM).reg;
        jump(c, done); mark(c, slow);
        MirValue input = convert(c, read_binding(c, length_owner, mvp_call_argument(node, 0), false), VALUE_REP_ITEM);
        move(c, joined, reg(c, em_call_1(&c->em, info->c_func_name, MIR_T_I64,
            MIR_T_I64, reg(c, input.reg), true)));
        mark(c, done);
        MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        result.admitted_success_contract = &TYPE_INT;
        return result;
    }
    if (info->fn == SYSFUNC_LEN && mvp_array_contract(normal_input) && !mvp_array_element(normal_input)) {
        MirValue owner = length_owner ? read_binding(c, length_owner, mvp_call_argument(node, 0), false)
            : expression(c, mvp_call_argument(node, 0));
        owner = convert(c, owner, VALUE_REP_ITEM);
        MIR_label_t checked = label(c), numeric = label(c), have_length = label(c), done = label(c);
        MIR_reg_t joined = em_new_reg(&c->em, "array_len_outcome", MIR_T_I64);
        MIR_reg_t pointer = em_guard_container(&c->em, owner.reg, LMD_TYPE_RAW_POINTER, checked);
        MIR_reg_t kind = em_load_at(&c->em, pointer, 0, MIR_T_U8, "length_kind");
        branch(c, MIR_BEQ, numeric, reg(c, kind), imm(c, LMD_TYPE_ARRAY_NUM));
        MIR_reg_t flat = em_physical_array_owner(&c->em, owner.reg, checked, true);
        MIR_reg_t length = em_load_at(&c->em, flat, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "flat_length");
        jump(c, have_length); mark(c, numeric);
        move(c, length, reg(c, em_call_1(&c->em, "fn_len_l", MIR_T_I64, MIR_T_P, reg(c, pointer), true)));
        mark(c, have_length);
        move(c, joined, reg(c, em_box_finite_int_item(&c->em, length)));
        jump(c, done); mark(c, checked);
        move(c, joined, reg(c, em_call_1(&c->em, "fn_len_checked", MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), true)));
        mark(c, done);
        MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        result.admitted_success_contract = &TYPE_INT;
        return result;
    }
    if (info->fn == SYSFUNC_LEN && (normal_input == &TYPE_STRING || mvp_array_element(normal_input))) {
        MvpBinding* b = mvp_local_binding(c->module, mvp_call_argument(node, 0));
        MirValue owner = b ? read_binding(c, b, mvp_call_argument(node, 0), false) : expression(c, mvp_call_argument(node, 0));
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
        // only rank-one numeric storage proves header length == public len;
        // N-D values count their outer axis through the shared runtime helper.
        MIR_reg_t r = input == LMD_TYPE_STRING || !mvp_array_element(normal_input)
            ? em_call_1(&c->em, input == LMD_TYPE_STRING ? "fn_len_s" : "fn_len_l",
                MIR_T_I64, MIR_T_P, reg(c, owner.reg), true)
            : em_load_at(&c->em, owner.reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "length");
        return value(c, r, LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, node);
    }
    if ((info->fn == SYSFUNC_FLOAT || info->fn == SYSFUNC_INT) && base &&
            (base->type_id == LMD_TYPE_FLOAT || base->type_id == LMD_TYPE_INT)) {
        Type* output = info->fn == SYSFUNC_FLOAT ? &TYPE_FLOAT : &TYPE_INT;
        MirValue operand = expression(c, mvp_call_argument(node, 0));
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
        MirValue operand = expression(c, mvp_call_argument(node, 0));
        if (operand.rep == VALUE_REP_U64)
            return value(c, operation(c, MIR_XOR, reg(c, operand.reg), imm(c, -1)),
                LMD_TYPE_UINT64, VALUE_REP_U64, &TYPE_UINT64, node);
        if (operand.rep == VALUE_REP_INT_LANE && !nullable(operand)) {
            MIR_label_t slow = label(c), done = label(c);
            MIR_reg_t joined = em_new_reg(&c->em, "bnot_int", MIR_T_I64);
            em_branch_int53_outside(&c->em, operand.reg, slow);
            MIR_reg_t word = operation(c, MIR_XOR, reg(c, operand.reg), imm(c, -1));
            em_branch_int53_outside(&c->em, word, slow);
            move(c, joined, reg(c, em_box_finite_int_item(&c->em, word)));
            jump(c, done); mark(c, slow);
            MirValue boxed = convert(c, operand, VALUE_REP_ITEM);
            move(c, joined, reg(c, em_call_1(&c->em, "fn_bnot_item", MIR_T_I64,
                MIR_T_I64, reg(c, boxed.reg), true)));
            mark(c, done);
            MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
            result.admitted_success_contract = &TYPE_INT;
            return result;
        }
        if (numeric_kind(operand) == LAMBDA_NUM_U32) {
            bool outcome = operand.admitted_success_contract != NULL;
            MIR_reg_t joined = outcome ? em_new_reg(&c->em, "not_outcome", MIR_T_I64) : 0;
            MIR_label_t done = outcome ? label(c) : NULL;
            if (outcome) join_operand_errors(c, &operand, 1, joined, done);
            operand = convert(c, operand, VALUE_REP_ITEM);
            MIR_reg_t payload = operation(c, MIR_AND, reg(c, operand.reg), imm(c, UINT32_MAX));
            payload = operation(c, MIR_XOR, reg(c, payload), imm(c, UINT32_MAX));
            MIR_reg_t packed = em_pack_u32_payload(&c->em, payload);
            if (!outcome) return value(c, packed, LMD_TYPE_NUM_SIZED, VALUE_REP_ITEM, &TYPE_U32, node);
            move(c, joined, reg(c, packed)); mark(c, done);
            MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
            result.admitted_success_contract = &TYPE_U32;
            return result;
        }
        operand = convert(c, operand, VALUE_REP_ITEM);
        return value(c, em_call_1(&c->em, "fn_bnot_item", MIR_T_I64, MIR_T_I64, reg(c, operand.reg), true),
            LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    bool wide_words = info->fn == SYSFUNC_BAND || info->fn == SYSFUNC_BOR || info->fn == SYSFUNC_BXOR;
    int64_t wide_count = -1;
    bool fixed_shift = info->fn == SYSFUNC_USHR && mir_decimal_int_literal(c->module->tp->source,
        mvp_call_argument(node, 1), &wide_count) && wide_count >= 0;
    if ((wide_words || fixed_shift) && numeric_node_kind(c, mvp_call_argument(node, 0)) == LAMBDA_NUM_U64 &&
            ((wide_words && numeric_node_kind(c, mvp_call_argument(node, 1)) == LAMBDA_NUM_U64) || fixed_shift)) {
        MirValue a = expression(c, mvp_call_argument(node, 0)), b = expression(c, mvp_call_argument(node, 1));
        if (a.rep == VALUE_REP_U64 && (fixed_shift || b.rep == VALUE_REP_U64)) {
            // wide shifts saturate to zero at the word width; MIR masks
            // its count, so never emit an oversized machine shift (S4.4.4).
            MIR_reg_t raw = fixed_shift && wide_count >= 64 ? constant(c, 0) :
                operation(c, fixed_shift ? MIR_URSH : em_integer_bitwise_opcode(info->fn),
                    reg(c, a.reg), reg(c, b.reg));
            return value(c, raw, LMD_TYPE_UINT64, VALUE_REP_U64, &TYPE_UINT64, node);
        }
        a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
        MIR_reg_t raw = em_call_2(&c->em, em_integer_bitwise_helper(info->fn), MIR_T_I64,
            MIR_T_I64, reg(c, a.reg), MIR_T_I64, reg(c, b.reg), true);
        return value(c, raw, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    if (mvp_binary_bitwise(info->fn)) {
        if (numeric_node_kind(c, mvp_call_argument(node, 0)) == LAMBDA_NUM_U32 ||
                numeric_node_kind(c, mvp_call_argument(node, 1)) == LAMBDA_NUM_U32) {
            MirValue a = expression(c, mvp_call_argument(node, 0)), b = expression(c, mvp_call_argument(node, 1));
            bool left_sized = numeric_kind(a) == LAMBDA_NUM_U32;
            bool right_sized = numeric_kind(b) == LAMBDA_NUM_U32;
            bool right_int = b.rep == VALUE_REP_INT_LANE && !nullable(b);
            LambdaNumericDecision decision = lambda_numeric_classify(
                info->fn == SYSFUNC_SHL || info->fn == SYSFUNC_SHR ? LAMBDA_NUM_OP_SHIFT : LAMBDA_NUM_OP_BITWISE,
                left_sized ? LAMBDA_NUM_U32 : a.rep == VALUE_REP_INT_LANE && !nullable(a) ? LAMBDA_NUM_INT : LAMBDA_NUM_INVALID,
                right_sized ? LAMBDA_NUM_U32 : right_int ? LAMBDA_NUM_INT : LAMBDA_NUM_INVALID);
            if (decision.valid && decision.result == LAMBDA_NUM_U32) {
                a = convert(c, a, VALUE_REP_ITEM);
                if (right_sized) b = convert(c, b, VALUE_REP_ITEM);
                MIR_reg_t packed = em_u32_bitwise(&c->em, info->fn, a.reg, b.reg,
                    true, right_sized, true, right_sized);
                MirValue result = value(c, packed, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
                result.admitted_success_contract = &TYPE_U32;
                return result;
            }
            if (decision.valid && decision.result == LAMBDA_NUM_INT) {
                bool outcome = a.admitted_success_contract || b.admitted_success_contract;
                MIR_reg_t joined = outcome ? em_new_reg(&c->em, "bitwise_outcome", MIR_T_I64) : 0;
                MIR_label_t done = outcome ? label(c) : NULL;
                MirValue operands[] = {a, b};
                if (outcome) join_operand_errors(c, operands, 2, joined, done);
                bool sized[] = {left_sized, right_sized};
                for (int i = 0; i < 2; i++) if (sized[i]) {
                    operands[i] = convert(c, operands[i], VALUE_REP_ITEM);
                    operands[i] = value(c, operation(c, MIR_AND, reg(c, operands[i].reg), imm(c, UINT32_MAX)),
                        LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT);
                }
                MIR_reg_t computed = em_guarded_int_bitwise(&c->em, info->fn, operands[0], operands[1],
                    em_integer_bitwise_helper(info->fn));
                if (outcome) { move(c, joined, reg(c, computed)); mark(c, done); }
                MirValue result = value(c, outcome ? joined : computed, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
                result.admitted_success_contract = &TYPE_INT;
                return result;
            }
            a = convert(c, a, VALUE_REP_ITEM); b = convert(c, b, VALUE_REP_ITEM);
            const char* helper = em_integer_bitwise_helper(info->fn);
            return value(c, em_call_2(&c->em, helper, MIR_T_I64, MIR_T_I64, reg(c, a.reg),
                MIR_T_I64, reg(c, b.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        }
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
            Type* contract = mvp_value_contract(c->module, node);
            MirValue result = value(c, joined, contract->type_id, VALUE_REP_ITEM, contract, node);
            result.admitted_success_contract = &TYPE_INT;
            return result;
        }
        MirValue a = expression(c, mvp_call_argument(node, 0)), b = expression(c, mvp_call_argument(node, 1));
        if (info->fn == SYSFUNC_SHR && mvp_int_range(c->module, node).finite)
            return value(c, operation(c, MIR_RSH, reg(c, a.reg), reg(c, b.reg)),
                LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, node);
        // raw bitwise registry entries need their Item classifiers on the fallback arm.
        MIR_reg_t r = em_guarded_int_bitwise(&c->em, info->fn, a, b,
            em_integer_bitwise_helper(info->fn));
        Type* contract = mvp_value_contract(c->module, node);
        MirValue result = value(c, r, contract->type_id, VALUE_REP_ITEM, contract, node);
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
    for (; i < mvp_call_argument_count(node); i++) arguments[i] = expression(c, mvp_call_argument(node, i));
    MIR_label_t slice_done = NULL;
    MIR_reg_t slice_result = 0;
    if (info->fn == SYSFUNC_SLICE && i == 3 && result_rep == VALUE_REP_ITEM) {
        MIR_label_t checked = label(c);
        slice_done = label(c); slice_result = em_new_reg(&c->em, "slice_character", MIR_T_I64);
        move(c, slice_result, reg(c, ascii_slice_character(c, arguments, checked)));
        jump(c, slice_done); mark(c, checked);
    }
    MIR_label_t done = NULL;
    MIR_reg_t joined = 0;
    if (result_rep != VALUE_REP_ITEM && !sysfunc_observes_error(info)) for (int n = 0; n < i; n++) {
        MirValue arg = arguments[n];
        if (arg.rep != VALUE_REP_ITEM || !lambda_type_accepts_error(arg.semantic_contract)) continue;
        // raw-return C helpers cannot carry ItemError. Classify staged
        // arguments before entering that ABI (S7.9.3, D2.3.5).
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
    if (slice_done) { move(c, slice_result, reg(c, r)); mark(c, slice_done); r = slice_result; }
    if (info->fn == SYSFUNC_ERROR || info->fn == SYSFUNC_ERROR2)
        r = em_error_stamp_site(&c->em, r, c->module->tp->reference, (AstSysFuncNode*)target);
    if (info->c_ret_type == C_RET_DTIME) {
        // datetime constructors return packed bits; only push_k publishes a datetime Item.
        r = em_call_1(&c->em, "push_k", MIR_T_I64, MIR_T_I64, reg(c, r), true);
    }
    bool boxed_bool = info->c_ret_type == C_RET_BOOL &&
        (node->propagate || !mvp_boundary_proven(node->type, &TYPE_BOOL));
    if (info->c_ret_type == C_RET_BOOL && !boxed_bool)
        r = operation(c, MIR_AND, reg(c, r), imm(c, UINT8_MAX));
    if (result_rep == VALUE_REP_ITEM && info->fn != SYSPROC_PRINT) tid = LMD_TYPE_ANY;
    // the registry's instantiated result relation owns Bool's error arm;
    // BOOL_ERROR cannot enter a successful native Boolean lane (S7.8.1).
    MirValue result = boxed_bool ? bool_call_outcome(c, r, node) : value(c, r, tid, result_rep,
        tid == LMD_TYPE_ANY ? &TYPE_ANY : lambda_canonical_type_for_id(tid), node);
    // a raw-pointer C result can use zero for absence (for example, name()
    // on an anonymous function). Preserve it before adding a scalar tag.
    if (result_rep == VALUE_REP_RAW_GC_POINTER)
        result.semantic_contract = mvp_nullable_contract(c->module, result.semantic_contract);
    if (result_rep == VALUE_REP_ITEM && info->fn != SYSPROC_PRINT) {
        Type* contract = mvp_value_contract(c->module, node);
        // a caret narrows the call's AST type after discharge. The producer
        // descriptor must retain Error until propagated_value emits its guard.
        if (node->propagate) contract = lambda_type_union_normalized(c->module->tp->pool, contract, &TYPE_ERROR);
        result = value(c, r, contract->type_id, result_rep, contract, node);
    }
    // the checked helper retains virtual-content and argument errors; only
    // its successful result has the audited int domain (S7.9.3).
    if (info->fn == SYSFUNC_LEN) result.admitted_success_contract = &TYPE_INT;
    if (done) {
        MirValue boxed = convert(c, result, VALUE_REP_ITEM);
        move(c, joined, reg(c, boxed.reg)); mark(c, done);
        result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        result.admitted_success_contract = lambda_canonical_type_for_id(tid);
    }
    return result;
}

static MirValue primitive_array_storage(MvpCompiler* c, AstNode* node, Type* element,
        Type* destination, int64_t count) {
    ArrayNumElemType lane;
    lambda_array_num_elem_type_for_contract(element, &lane);
    ArrayRepCert* certificate = mvp_array_element(destination) == element && lambda_array_contract_is_plain(destination)
        ? lambda_array_rep_cert_create(c->module->tp->pool, destination) : NULL;
    if (certificate && (certificate->flags & ARRAY_REP_CERT_COUNTED)) certificate = NULL;
    MIR_reg_t array = certificate
        ? em_call_4(&c->em, "lambda_fill_primitive_bits", MIR_T_I64,
            MIR_T_I64, imm(c, count), MIR_T_I64, imm(c, 0), MIR_T_I64, imm(c, lane),
            MIR_T_P, imm(c, (intptr_t)certificate), true)
        : em_call_2(&c->em, "array_num_new", MIR_T_I64, MIR_T_I64, imm(c, lane), MIR_T_I64, imm(c, count), true);
    return value(c, array, LMD_TYPE_ARRAY, VALUE_REP_ITEM, certificate ? destination : &TYPE_ARRAY, node);
}

static MirValue nullable_array_literal(MvpCompiler* c, AstArrayNode* node, Type* destination) {
    int count = ast_linked_node_count(node->item);
    if (!count || count > LAMBDA_MAX_FUNCTION_ARGS) return {};
    Type* element = NULL; bool nullable_members = false;
    for (AstNode* item = node->item; item; item = item->next) {
        Type* contract = mvp_value_contract(c->module, item);
        bool optional = false;
        Type* base = lambda_type_nullable_lane_base(contract, &optional);
        if (!base || (base->type_id != LMD_TYPE_INT && base->type_id != LMD_TYPE_FLOAT && base->type_id != LMD_TYPE_BOOL)) return {};
        base = lambda_canonical_type_for_id(base->type_id);
        if (element && element != base) return {};
        element = base; nullable_members |= optional;
    }
    if (!nullable_members) return {};
    MirValue members[LAMBDA_MAX_FUNCTION_ARGS]; int i = 0;
    for (AstNode* item = node->item; item; item = item->next) members[i++] = expression(c, item);
    MIR_reg_t joined = em_new_reg(&c->em, "nullable_array", MIR_T_I64);
    value(c, joined, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
    MIR_label_t generic = label(c), done = label(c);
    // evaluate every member before testing absence; NaN remains a number and
    // only the lane's distinct null marker selects generic storage (D4.3.3).
    for (int j = 0; j < count; j++) {
        if (members[j].rep == VALUE_REP_ITEM) {
            branch(c, MIR_BEQ, generic, reg(c, members[j].reg), imm(c, ITEM_NULL));
            branch(c, MIR_BEQ, generic, reg(c, operation(c, MIR_URSH, reg(c, members[j].reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        } else branch_null(c, members[j], generic);
    }
    MirValue packed = primitive_array_storage(c, node, element, destination, count);
    MIR_reg_t items = em_load_at(&c->em, packed.reg, LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_P, "nullable_items");
    ArrayNumElemType lane;
    lambda_array_num_elem_type_for_contract(element, &lane);
    for (int j = 0; j < count; j++) {
        MirValue member = convert(c, members[j], mvp_rep(element));
        em_store_at(&c->em, items, j * (element == &TYPE_BOOL ? 1 : 8), em_numeric_storage_type(lane), member.reg);
    }
    move(c, joined, reg(c, packed.reg)); jump(c, done); mark(c, generic);
    MIR_reg_t array = em_call_0(&c->em, "array_plain", MIR_T_P, true);
    value(c, array, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
    for (int j = 0; j < count; j++) {
        MirValue member = convert(c, members[j], VALUE_REP_ITEM);
        em_call_void_2(&c->em, "array_push", MIR_T_P, reg(c, array), MIR_T_I64, reg(c, member.reg), true);
    }
    move(c, joined, reg(c, em_call_1(&c->em, "array_end", MIR_T_I64, MIR_T_P, reg(c, array), true)));
    mark(c, done);
    return value(c, joined, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY, node);
}

static MirValue array_literal(MvpCompiler* c, AstArrayNode* node, Type* destination) {
    if (!destination) destination = mvp_contextual_contract(c->module, (AstNode*)node);
    MirValue nullable = nullable_array_literal(c, node, destination);
    if (nullable.reg) return nullable;
    Type* empty_element = node->item ? NULL : mvp_array_element(destination);
    if (!node->item && !empty_element) return value(c, em_call_0(&c->em, "array_plain", MIR_T_I64, true),
        LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY, node);
    // typed empty arrays and implicit array results use the same certified
    // allocation as nonempty literals, avoiding a second admission/rebuild.
    TypeId tid = empty_element ? empty_element->type_id : mvp_value_contract(c->module, node->item)->type_id;
    bool packed = tid == LMD_TYPE_INT || tid == LMD_TYPE_FLOAT || tid == LMD_TYPE_BOOL;
    for (AstNode* item = node->item; packed && item; item = item->next) {
        bool optional = false;
        Type* contract = mvp_value_contract(c->module, item);
        lambda_type_nullable_lane_base(contract, &optional);
        packed = contract->type_id == tid && !optional;
    }
    if (!packed) {
        // canonicalize unless the planner proves that row representation has
        // no observer; scalar-only private reads can avoid allocated row views.
        bool finalize_numeric = mvp_prefers_packed_tensor(c->module, (AstNode*)node);
        MIR_reg_t array = em_call_0(&c->em, "array_plain", MIR_T_P, true);
        MirValue result = value(c, array, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY, node);
        for (AstNode* item = node->item; item; item = item->next) {
            // named reads already capture; projected children capture here.
            // Fresh producers have no second observer to mark (S9.3.1).
            MirValue member = convert(c, capture_projection(c, expression(c, item, false, false, true), item), VALUE_REP_ITEM);
            em_call_void_2(&c->em, "array_push", MIR_T_P, reg(c, array),
                MIR_T_I64, reg(c, member.reg), true);
        }
        // dynamic and literal rows share the runtime's canonical finalizer.
        result = value(c, em_call_1(&c->em, finalize_numeric ? "array_end" : "array_end_flat", MIR_T_I64,
            MIR_T_P, reg(c, array), true), LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY, node);
        return result;
    }
    Type* element = tid == LMD_TYPE_FLOAT ? &TYPE_FLOAT : tid == LMD_TYPE_INT ? &TYPE_INT : &TYPE_BOOL;
    ArrayNumElemType lane;
    lambda_array_num_elem_type_for_contract(element, &lane);
    int64_t count = ast_linked_node_count(node->item);
    MirValue result = primitive_array_storage(c, node, element, destination, count);
    MIR_reg_t array = result.reg;
    bool literal_members = true;
    for (AstNode* item = node->item; item; item = item->next) {
        AstNode* leaf = ast_unwrap_primary_to_leaf(item);
        literal_members &= leaf && leaf->node_type == AST_NODE_PRIMARY &&
            !((AstPrimaryNode*)leaf)->expr && leaf->type && leaf->type->is_literal;
    }
    // literal scalar stores cannot collect or expose the fresh owner. Keep
    // one buffer address instead of reloading an aliasable header after each store.
    MIR_reg_t literal_data = literal_members ? em_load_at(&c->em, array,
        LAMBDA_GC_OFF_LIST_ITEMS, MIR_T_P, "literal_data") : 0;
    int64_t index = 0;
    for (AstNode* item = node->item; item; item = item->next, index++) {
        MirValue member = convert(c, expression(c, item), mvp_rep(element));
        // Evaluate first, then reload the rooted owner's buffer after any GC.
        int width = element == &TYPE_BOOL ? 1 : 8;
        if (literal_data) em_store_at(&c->em, literal_data, index * width, em_numeric_storage_type(lane), member.reg);
        else {
            MIR_reg_t address = em_array_element_address(&c->em, array, constant(c, index), width);
            em_store_at(&c->em, address, 0, em_numeric_storage_type(lane), member.reg);
        }
    }
    return result;
}

// native map descriptors determine the packed memory width for every access.
static MIR_type_t native_field_storage_type(const LaneStorageDesc* storage) {
    return storage->kind == LANE_STORAGE_FLOAT64 ? MIR_T_D :
        storage->kind == LANE_STORAGE_BOOL ? MIR_T_U8 : MIR_T_I64;
}

static void stage_literal_fields(MvpCompiler* c, AstMapNode* node,
        MirValue* staged, MIR_reg_t* keys) {
    int i = 0;
    // stage every expression before construction; precise register roots keep
    // earlier fields alive across recursive producers (D6.3, S7.7.1).
    for (AstNode* item = node->item; item; item = item->next) {
        AstNamedNode* named = item->node_type == AST_NODE_KEY_EXPR ? (AstNamedNode*)item : NULL;
        // nominal updates also store a spread as its raw value expression;
        // that node has no AstNamedNode key/flags payload.
        if (keys && named) {
            if (!named->is_spread) {
                MirValue key = named->key ? expression(c, named->key) : value(c,
                    em_load_const(&c->em, em_add_const(&c->em, named->name), MIR_T_P),
                    LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING);
                keys[i] = convert(c, key, VALUE_REP_ITEM).reg;
            }
        }
        AstNode* source = named ? named->as : item;
        MirValue member = expression(c, source);
        AstNode* producer = ast_unwrap_primary_to_leaf(source);
        // private success proofs can exclude lists even when the AST remains any.
        if (source && lambda_type_may_hold_list(member.semantic_contract) &&
                (!producer || producer->node_type != AST_NODE_ARRAY) &&
                !(item->node_type == AST_NODE_KEY_EXPR && ((AstNamedNode*)item)->is_spread)) {
            // fields keep a list's array image, while the source stays a list.
            member = convert(c, member, VALUE_REP_ITEM);
            MIR_reg_t image = em_slot_image(&c->em, member.reg, true);
            bool optional = false;
            Type* normal = lambda_type_nullable_lane_base(
                lambda_type_remove_error(c->module->tp->pool, member.semantic_contract), &optional);
            // imaging preserves non-list domains and unconstrained array
            // contracts; a list-specific predicate needs fresh admission.
            if (normal && (!lambda_type_may_hold_list(normal) || normal == &TYPE_ARRAY ||
                    lambda_array_contract_is_plain(normal)))
                member.reg = image;
            else member = value(c, image, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, source);
        }
        staged[i++] = capture_projection(c, member, source);
    }
}

struct MvpLiteralFields {
    int count;
    MirValue* values;
    MIR_reg_t* keys;
};

static MvpLiteralFields prepare_literal_fields(MvpCompiler* c, AstMapNode* node, bool dynamic) {
    MvpLiteralFields fields = {};
    fields.count = ast_linked_node_count(node->item);
    fields.values = (MirValue*)mem_alloc(fields.count * sizeof(MirValue), MEM_CAT_TEMP);
    fields.keys = dynamic ? (MIR_reg_t*)mem_calloc(fields.count, sizeof(MIR_reg_t), MEM_CAT_TEMP) : NULL;
    stage_literal_fields(c, node, fields.values, fields.keys);
    return fields;
}

static void dispose_literal_fields(MvpLiteralFields fields) {
    mem_free(fields.keys); mem_free(fields.values);
}

static void fill_dynamic_literal_fields(MvpCompiler* c, AstMapNode* node,
        MIR_reg_t owner, MirValue* staged, MIR_reg_t* keys) {
    int i = 0;
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
}

static MirValue map_literal(MvpCompiler* c, AstMapNode* node, TypeMap* destination) {
    // inferred field types describe layout, not admission: a homogeneous
    // array may retain its first literal's type (D4.3.1).
    if (destination == &TYPE_MAP || destination == &TYPE_OBJECT ||
            (destination && !destination->is_trusted_contract)) destination = NULL;
    if (!destination) destination = mvp_contextual_map_contract(c->module, (AstNode*)node);
    MvpLiteralFields fields = prepare_literal_fields(c, node, node->has_computed_key);
    int count = fields.count;
    MIR_op_t* members = (MIR_op_t*)mem_alloc(count * sizeof(MIR_op_t), MEM_CAT_TEMP);
    MirValue* staged = fields.values;
    MIR_reg_t* keys = fields.keys;
    if (keys) {
        // NameKey validation and spread are shared with dynamic element attributes.
        MIR_reg_t owner = em_call_0(&c->em, "map_literal_begin", MIR_T_I64, true);
        value(c, owner, LMD_TYPE_MAP, VALUE_REP_ITEM, &TYPE_MAP);
        fill_dynamic_literal_fields(c, node, owner, staged, keys);
        dispose_literal_fields(fields); mem_free(members);
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
    bool ordered = ast_map_literal_keys_follow_contract(node, shape);
    bool native = ordered;
    int field_index = 0;
    for (ShapeEntry* field = shape->shape; native && field;
            field = typemap_next_field(shape, field), field_index++) {
        const LaneStorageDesc* storage = shape_entry_storage(field);
        // an inferred field layout is not admission of an error-bearing
        // producer. Keep those Items in the shared descriptor fill (D4.3.3).
        // declared pointer contracts can use a generic descriptor with the
        // same fixed pointer width; inferred layouts retain canonical setters.
        native = (storage->native || shape->is_trusted_contract) && field_has_direct_lane(field) &&
            shape_entry_storage_fits_data(field, shape->byte_size) &&
            mvp_boundary_proven(staged[field_index].semantic_contract, field->type);
    }
    ShapeEntry* prepared_field = shape->shape;
    for (int n = 0; n < count; n++) {
        // keep scalar fields in their native lanes across the allocation;
        // only the generic fill ABI needs a staged Item (D2.3.5, D6.3).
        const LaneStorageDesc* storage = ordered ? shape_entry_storage(prepared_field) : NULL;
        bool float_item = ordered && storage->kind == LANE_STORAGE_TYPED_ITEM &&
            storage->value_domain == LMD_TYPE_ANY && staged[n].rep == VALUE_REP_F64 &&
            mvp_boundary_proven(staged[n].semantic_contract, &TYPE_FLOAT);
        staged[n] = convert(c, staged[n], native ? mvp_rep(prepared_field->type) :
            float_item ? VALUE_REP_F64 : VALUE_REP_ITEM);
        if (ordered) prepared_field = typemap_next_field(shape, prepared_field);
        if (!native) members[n] = reg(c, staged[n].reg);
    }
    MIR_reg_t map = em_call_2(&c->em, "map_alloc_for_type", MIR_T_P,
        MIR_T_P, imm(c, (intptr_t)shape), MIR_T_I64, imm(c, 0), true);
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
    } else if (ordered) {
        // map_alloc_for_type already owns the entire data buffer. Exact
        // static keys need only the audited NO_GC descriptor setters (D6.3).
        MIR_label_t done = label(c);
        branch(c, MIR_BEQ, done, reg(c, map), imm(c, 0));
        MIR_reg_t data = em_load_at(&c->em, map, LAMBDA_GC_OFF_MAP_DATA, MIR_T_P, "map_fill_data");
        ShapeEntry* field = shape->shape;
        for (int n = 0; n < count; n++, field = typemap_next_field(shape, field)) {
            MIR_reg_t slot = operation(c, MIR_ADD, reg(c, data), imm(c, field->byte_offset));
            bool scalar = staged[n].rep == VALUE_REP_F64;
            em_call_void_3(&c->em, scalar ? "lambda_set_field_f64" : "set_field_value",
                MIR_T_P, imm(c, (intptr_t)field), MIR_T_P, reg(c, slot),
                scalar ? MIR_T_D : MIR_T_I64, reg(c, staged[n].reg), true);
        }
        mark(c, done);
    } else {
        MIR_var_t fixed[] = {{MIR_T_P, "owner", 0}};
        MIR_op_t owner[] = {reg(c, map)};
        filled = em_emit_vararg_call(&c->em, "map_fill", MIR_T_P, 1, 1, fixed, owner, count, members);
    }
    mem_free(members);
    dispose_literal_fields(fields);
    if (failure.done) {
        MIR_reg_t joined = em_new_reg(&c->em, "map_join", MIR_T_I64);
        MIR_label_t done = label(c);
        move(c, joined, reg(c, filled)); jump(c, done);
        mark(c, failure.done); move(c, joined, reg(c, failure.error)); mark(c, done);
        Type* outcome = lambda_type_union_normalized(c->module->tp->pool, shape, &TYPE_ERROR);
        MirValue result = value(c, joined, outcome->type_id, VALUE_REP_ITEM, outcome, node);
        result.admitted_success_contract = shape;
        return result;
    }
    return value(c, filled, LMD_TYPE_MAP, VALUE_REP_RAW_GC_POINTER, shape, node);
}

static MirValue element_literal(MvpCompiler* c, AstElementNode* node) {
    MvpLiteralFields fields = prepare_literal_fields(c, node, node->has_computed_key);
    int count = fields.count;
    MirValue* staged = fields.values;
    MIR_reg_t* keys = fields.keys;
    // the descriptor belongs to this module; passing it directly avoids a foreign type-list index.
    MIR_reg_t descriptor = em_load_const(&c->em, em_add_const(&c->em, node->type), MIR_T_P);
    MIR_reg_t owner = em_call_1(&c->em, "elmt_literal_begin", MIR_T_P,
        MIR_T_P, reg(c, descriptor), true);
    value(c, owner, LMD_TYPE_ELEMENT, VALUE_REP_RAW_GC_POINTER, node->type);
    if (keys) fill_dynamic_literal_fields(c, node, owner, staged, keys);
    else if (count) {
        MIR_op_t* members = (MIR_op_t*)mem_alloc(count * sizeof(MIR_op_t), MEM_CAT_TEMP);
        for (int i = 0; i < count; i++) members[i] = reg(c, convert(c, staged[i], VALUE_REP_ITEM).reg);
        MIR_var_t fixed[] = {{MIR_T_P, "owner", 0}};
        MIR_op_t arguments[] = {reg(c, owner)};
        em_emit_vararg_call(&c->em, "elmt_fill", MIR_T_P, 1, 1, fixed, arguments, count, members);
        mem_free(members);
    }
    dispose_literal_fields(fields);
    bool file_destination = is_file_element_type((TypeElmt*)node->type);
    MvpErrorTarget failure = {};
    if (file_destination)
        // the evaluated format selects the output builder after attributes.
        em_call_void_1(&c->em, "elmt_content_begin", MIR_T_P, reg(c, owner), true);
    AstListNode* content = (AstListNode*)node->content;
    for (AstNode* item = content ? content->item : NULL; item; item = item->next) {
        MIR_label_t ordinary = NULL, appended = NULL;
        if (file_destination) {
            ordinary = label(c); appended = label(c);
            MIR_reg_t virtual_file = em_call_1(&c->em, "output_content_is_virtual", MIR_T_I64,
                MIR_T_P, reg(c, owner), true);
            virtual_file = operation(c, MIR_AND, reg(c, virtual_file), imm(c, UINT8_MAX));
            branch(c, MIR_BEQ, ordinary, reg(c, virtual_file), imm(c, 0));
            MIR_reg_t outcome = output_expression(c, item, owner).reg;
            // output failure is this constructor's value, so a surrounding
            // handler runs before any callable-return admission (S2.6.6v3).
            check_item_result(c, outcome, &failure);
            jump(c, appended); mark(c, ordinary);
        }
        MirValue member = convert(c, capture_projection(c,
            expression(c, item, false, false, true), item), VALUE_REP_ITEM);
        // element content normalizes null and adjacent text; lists use sequence append (D3.3.1).
        em_call_void_2(&c->em, "list_push_spread", MIR_T_P, reg(c, owner),
            MIR_T_I64, reg(c, member.reg), true);
        if (appended) mark(c, appended);
    }
    MIR_reg_t finished = em_call_1(&c->em, "list_end", MIR_T_I64, MIR_T_P, reg(c, owner), true);
    if (file_destination) {
        if (failure.done) {
            MIR_label_t done = label(c);
            jump(c, done); mark(c, failure.done);
            move(c, finished, reg(c, failure.error)); mark(c, done);
        }
        return value(c, finished, LMD_TYPE_ANY, VALUE_REP_ITEM,
            mvp_value_contract(c->module, node), node);
    }
    return value(c, owner, LMD_TYPE_ELEMENT, VALUE_REP_RAW_GC_POINTER, node->type, node);
}

static MirValue object_literal(MvpCompiler* c, AstObjectLiteralNode* node) {
    TypeObject* type = (TypeObject*)node->type;
    MvpLiteralFields fields = prepare_literal_fields(c, node, true);
    MIR_reg_t provided = em_call_0(&c->em, "map_literal_begin", MIR_T_I64, true);
    value(c, provided, LMD_TYPE_MAP, VALUE_REP_ITEM, &TYPE_MAP);
    fill_dynamic_literal_fields(c, node, provided, fields.values, fields.keys);
    dispose_literal_fields(fields);
    FOR_EACH_MAP_FIELD(type, field) {
        if (!field->default_value) continue;
        String* name = name_pool_create_strview(c->module->tp->name_pool,
            {field->name->str, field->name->length});
        MirValue key = convert(c, value(c, em_load_const(&c->em, em_add_const(&c->em, name), MIR_T_P),
            LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING), VALUE_REP_ITEM);
        MIR_reg_t present = em_call_2(&c->em, "fn_at", MIR_T_I64,
            MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, provided), true);
        MIR_label_t done = label(c);
        branch(c, MIR_BNE, done, reg(c, operation(c, MIR_AND, reg(c, present), imm(c, UINT8_MAX))), imm(c, 0));
        MirValue source = convert(c, capture_projection(c,
            expression(c, field->default_value), field->default_value), VALUE_REP_ITEM);
        MIR_reg_t member = em_slot_image(&c->em, source.reg, true);
        MIR_reg_t result = em_call_3(&c->em, "map_literal_put", MIR_T_I64,
            MIR_T_I64, reg(c, provided), MIR_T_I64, reg(c, key.reg), MIR_T_I64, reg(c, member), true);
        mvp_check_item_result(c, result); mark(c, done);
    }
    // the shared constructor resolves optional presence and enforces every
    // declared slot before publishing a nominal layout (S16.9.5/S11.4.10).
    MIR_reg_t object = em_call_2(&c->em, "object_from_fields", MIR_T_I64,
        MIR_T_P, imm(c, (intptr_t)type), MIR_T_I64, reg(c, provided), true);
    mvp_check_item_result(c, object);
    value(c, object, type->type_id, VALUE_REP_RAW_GC_POINTER, node->type);
    if (mvp_type_has_environment(node->type)) {
        MvpBinding* binding = mvp_binding(c->module, node->type_entry);
        if (!binding || !binding->reg) {
            mvp_reject(c->module, node, "object constructor requires its declaring type value");
            return null_value(c);
        }
        MirValue declared = convert(c, read_binding(c, binding, node, false), VALUE_REP_ITEM);
        object = em_call_2(&c->em, "lambda_object_copy_environment", MIR_T_I64,
            MIR_T_I64, reg(c, object), MIR_T_I64, reg(c, declared.reg), true);
        mvp_check_item_result(c, object);
    }
    AstListNode* content = (AstListNode*)node->content;
    int count = content ? ast_linked_node_count(content->item) : 0;
    if (count) {
        MIR_op_t* children = (MIR_op_t*)mem_alloc(count * sizeof(MIR_op_t), MEM_CAT_TEMP);
        int i = 0;
        for (AstNode* child = content->item; child; child = child->next)
            children[i++] = reg(c, convert(c, capture_projection(c,
                expression(c, child, false, false, true), child), VALUE_REP_ITEM).reg);
        MIR_var_t fixed[] = {{MIR_T_P, "object", 0}, {MIR_T_I64, "count", 0}};
        MIR_op_t operands[] = {reg(c, object), imm(c, count)};
        em_emit_vararg_call(&c->em, "object_content_fill", MIR_T_P, 1, 2, fixed, operands, count, children);
        mem_free(children);
    }
    return value(c, object, type->type_id, VALUE_REP_RAW_GC_POINTER, node->type, node);
}

static MirValue edit_model_store(MvpCompiler* c, AstAssignStamNode* node, bool named) {
    MirValue owner = convert(c, expression(c, node->object), VALUE_REP_ITEM);
    MIR_reg_t key;
    if (named) {
        AstIdentNode* field = (AstIdentNode*)ast_unwrap_primary_to_leaf(node->key);
        key = constant(c, (intptr_t)field->name->chars);
    } else {
        MirValue index = convert(c, expression(c, node->key), VALUE_REP_ITEM);
        key = em_call_1(&c->em, "fn_int64_index", MIR_T_I64, MIR_T_I64, reg(c, index.reg), true);
    }
    MirValue input = convert(c, capture_projection(c, assignment_value(c, node), node->value), VALUE_REP_ITEM);
    MIR_reg_t element = em_exact_item_type_test(&c->em, owner.reg, LMD_TYPE_ELEMENT);
    MIR_reg_t result = em_new_reg(&c->em, "edit_store", MIR_T_I64);
    MIR_label_t element_store = label(c), done = label(c);
    branch(c, MIR_BNE, element_store, reg(c, element), imm(c, 0));
    const char* helpers[] = {named ? "edit_map_update" : "edit_array_set",
        named ? "edit_elmt_update_attr" : "edit_elmt_replace_child"};
    for (int i = 0; i < 2; i++) {
        if (i) mark(c, element_store);
        MIR_reg_t stored = em_call_3(&c->em, helpers[i], MIR_T_I64,
            MIR_T_I64, reg(c, owner.reg), named ? MIR_T_P : MIR_T_I64, reg(c, key),
            MIR_T_I64, reg(c, input.reg), true);
        move(c, result, reg(c, stored));
        if (!i) jump(c, done);
    }
    mark(c, done);
    return value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
}

static Type* inferred_field_domain(ShapeEntry* field) {
    if (!field || field->flags || field->is_optional) return NULL;
    const LaneStorageDesc* storage = shape_entry_storage(field);
    switch (storage->kind) {
    case LANE_STORAGE_TYPED_ITEM:
        return storage->value_domain == LMD_TYPE_ANY ? &TYPE_ANY : NULL;
    case LANE_STORAGE_INT:
        return storage->value_domain == LMD_TYPE_INT ? &TYPE_INT : NULL;
    case LANE_STORAGE_BOOL:
        return storage->value_domain == LMD_TYPE_BOOL ? &TYPE_BOOL : NULL;
    case LANE_STORAGE_FLOAT64:
        return storage->value_domain == LMD_TYPE_FLOAT ? &TYPE_FLOAT : NULL;
    case LANE_STORAGE_POINTER:
        return is_container_type_id(storage->value_domain) ? &TYPE_MAP
            : storage->value_domain == LMD_TYPE_STRING ? &TYPE_STRING : NULL;
    case LANE_STORAGE_ITEM:
        return storage->value_domain == LMD_TYPE_NULL || storage->value_domain == LMD_TYPE_ERROR ? &TYPE_ANY : NULL;
    default: return NULL;
    }
}

static Type* inferred_scalar_field(ShapeEntry* field) {
    Type* domain = inferred_field_domain(field);
    return domain == &TYPE_INT || domain == &TYPE_FLOAT || domain == &TYPE_BOOL ? domain : NULL;
}

static MirValue inferred_scalar_input(MvpCompiler* c, ShapeEntry* field,
        MirValue input, MIR_label_t miss) {
    Type* domain = inferred_scalar_field(field);
    if (domain == &TYPE_INT) return value(c, em_guard_finite_int(&c->em, input, miss),
        LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT);
    if (!mvp_boundary_proven(input.semantic_contract, domain)) {
        input = convert(c, input, VALUE_REP_ITEM);
        branch(c, MIR_BEQ, miss,
            reg(c, em_exact_item_type_test(&c->em, input.reg, domain->type_id)), imm(c, 0));
        input.semantic_contract = domain; input.semantic_type = domain->type_id;
    }
    return convert(c, input, mvp_rep(domain));
}

static void guard_inferred_field_descriptor(MvpCompiler* c, ShapeEntry* field, MIR_label_t miss) {
    MIR_reg_t descriptor = constant(c, (intptr_t)field);
    branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, descriptor,
        offsetof(ShapeEntry, type), MIR_T_P, "observed_field_type")), imm(c, (intptr_t)field->type));
    branch(c, MIR_BNE, miss, reg(c, em_load_at(&c->em, descriptor,
        offsetof(ShapeEntry, flags), MIR_T_U8, "observed_field_flags")), imm(c, 0));
}

static MirValue inferred_field_value(MvpCompiler* c, ShapeEntry* field,
        MIR_reg_t data, AstNode* source, MIR_label_t miss) {
    Type* domain = inferred_field_domain(field);
    if (!domain) return {};
    const LaneStorageDesc* storage = shape_entry_storage(field);
    if (storage->kind == LANE_STORAGE_TYPED_ITEM && storage->value_domain == LMD_TYPE_ANY) {
        guard_inferred_field_descriptor(c, field, miss);
        MIR_reg_t tag = em_load_at(&c->em, data,
            field->byte_offset + offsetof(TypedItem, type_id), MIR_T_U8, "field_item_tag");
        MIR_reg_t result = constant(c, ITEM_NULL);
        MIR_label_t decode = label(c), done = label(c);
        branch(c, MIR_BEQ, done, reg(c, tag), imm(c, LMD_TYPE_NULL));
        MIR_reg_t kind = operation(c, MIR_SUB, reg(c, tag), imm(c, LMD_TYPE_CONTAINER));
        branch(c, MIR_UBGT, decode, reg(c, kind), imm(c, LMD_TYPE_VELMT - LMD_TYPE_CONTAINER));
        MIR_reg_t pointer = em_load_packed_i64(&c->em, data, field->byte_offset + offsetof(TypedItem, item));
        move(c, result, reg(c, pointer));
        branch(c, MIR_BEQ, done, reg(c, pointer), imm(c, 0));
        // the canonical decoder owns raw-container reification and every
        // scalar payload, including the double-backed integer lane (D2.6.2).
        branch(c, MIR_BNE, done, reg(c, em_load_at(&c->em, pointer, 0, MIR_T_U8, "field_item_kind")),
            imm(c, LMD_TYPE_RAW_POINTER));
        mark(c, decode);
        move(c, result, reg(c, em_call_2(&c->em, "map_shape_field_to_item", MIR_T_I64,
            MIR_T_P, reg(c, data), MIR_T_P, imm(c, (intptr_t)field), true)));
        mark(c, done);
        return value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, source);
    }
    bool container = storage->kind == LANE_STORAGE_POINTER && is_container_type_id(storage->value_domain);
    // same-width inferred retags can keep their shape. The live type and flags
    // must still select the descriptor compiled here before decoding its lane.
    guard_inferred_field_descriptor(c, field, miss);
    bool optional = storage->nullable || container || domain == &TYPE_STRING;
    Type* physical = optional ? mvp_nullable_contract(c->module, domain) : domain;
    ValueRep rep = container ? VALUE_REP_RAW_GC_POINTER : mvp_rep(physical);
    MIR_reg_t loaded = em_load_at(&c->em, data, field->byte_offset,
        native_field_storage_type(storage), "observed_field");
    if (storage->kind == LANE_STORAGE_ITEM) {
        MIR_label_t present = label(c);
        branch(c, MIR_BNE, present, reg(c, loaded), imm(c, 0));
        move(c, loaded, imm(c, storage->value_domain == LMD_TYPE_ERROR ? ITEM_ERROR : ITEM_NULL));
        mark(c, present);
    }
    if (container) {
        MIR_label_t ready = label(c);
        branch(c, MIR_BEQ, ready, reg(c, loaded), imm(c, 0));
        // foreign raw containers acquire their physical kind in the shared
        // decoder; retain that side effect on this uncommon fallback.
        branch(c, MIR_BEQ, miss, reg(c, em_load_at(&c->em, loaded, 0, MIR_T_U8, "observed_child_kind")),
            imm(c, LMD_TYPE_RAW_POINTER));
        mark(c, ready);
    }
    return value(c, loaded, domain->type_id, rep, physical, source);
}

static MirValue member_shape_value(MvpCompiler* c, AstFieldNode* node,
        MvpBinding* binding, MirValue owner, TypeMap* shape, ShapeEntry* field,
        Type* contract, bool speculative, MIR_label_t miss) {
    // sparse nominal values compact absent slots; their declared offsets do
    // not prove the physical layout of even a required field (D4.2.5).
    if (shape && type_nominal_record(shape) && typemap_has_optional_fields(shape)) return {};
    const LaneStorageDesc* storage = field ? shape_entry_storage(field) : NULL;
    bool container_slot = storage && storage->kind == LANE_STORAGE_POINTER &&
        (storage->value_domain == LMD_TYPE_MAP || storage->value_domain == LMD_TYPE_ARRAY ||
         storage->value_domain == LMD_TYPE_ARRAY_NUM || storage->value_domain == LMD_TYPE_ELEMENT ||
         storage->value_domain == LMD_TYPE_RANGE);
    // raw machine-integer/TypedItem fields need their descriptor decoder;
    // their first word is not the boxed Item selected by mvp_rep (D2.4.2).
    bool descriptor_read = field && shape && !shape->is_trusted_contract &&
        !typemap_fixed_slot_prefix_count(shape);
    bool direct = field && shape && (descriptor_read ||
        (shape->is_trusted_contract && field_has_direct_lane(field))) &&
        shape_entry_storage_fits_data(field, shape->byte_size);
    if (!direct) return {};
    owner = convert(c, owner, VALUE_REP_ITEM);
    // the enforced receiver contract already proves map-or-null; retain
    // null/layout guards without repeating tagged-kind classification.
    MIR_reg_t pointer = owner.reg;
    bool proven = !descriptor_read && !speculative &&
        mvp_admission_proven(c->module, owner.semantic_contract, shape);
    if (proven) pointer = owner.reg;
    else if (!descriptor_read && !speculative && binding && (binding->record_readable || binding->record_writable))
        branch(c, MIR_BEQ, miss,
            reg(c, binding->record_readable ? binding->record_readable : binding->record_writable), imm(c, 0));
    else pointer = em_guard_map_shape(&c->em, owner.reg, constant(c, (intptr_t)shape), miss,
        lambda_type_nonnull_map_contract(owner.semantic_contract) != NULL, shape->is_trusted_contract);
    MIR_reg_t data = em_load_at(&c->em, pointer, LAMBDA_GC_OFF_MAP_DATA, MIR_T_P, "member_data");
    MirValue member = descriptor_read ? inferred_field_value(c, field, data, node, miss) : MirValue{};
    if (descriptor_read && !member.reg) {
        // inferred null/bool fields may retag without replacing the shape;
        // decode its live descriptor, including packed TypedItems (D2.6.2).
        MIR_reg_t item = em_call_2(&c->em, "map_shape_field_to_item", MIR_T_I64,
            MIR_T_P, reg(c, data), MIR_T_P, imm(c, (intptr_t)field), true);
        member = value(c, item, contract->type_id, VALUE_REP_ITEM, contract, node);
    } else if (!descriptor_read) {
        Type* field_contract = type_field_unwrap_simple_decl(field->type);
        // packed container slots use raw pointers even when the array contract
        // itself has Item transport. Decode the descriptor, not the type AST.
        member = value(c, em_load_at(&c->em, data, field->byte_offset,
            native_field_storage_type(storage), "member_lane"),
            container_slot ? (TypeId)storage->value_domain : field_contract->type_id,
            container_slot ? VALUE_REP_RAW_GC_POINTER : mvp_rep(field_contract), field_contract, node);
    }
    return member;
}

static MirValue member_read(MvpCompiler* c, AstFieldNode* node) {
    if (int wildcard = ast_member_wildcard(node))
        return value(c, em_wildcard_step(&c->em, node, wildcard),
            LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    MvpBinding* binding = mvp_local_binding(c->module, node->object);
    AstNode* scalar_key = ast_unwrap_primary_to_leaf(node->field);
    if (binding && binding->scalar_aggregate && scalar_key && scalar_key->node_type == AST_NODE_IDENT) {
        String* name = ((AstIdentNode*)scalar_key)->name;
        for (int i = 0; i < binding->scalar_aggregate->count; i++)
            if (string_eq(name, binding->scalar_aggregate->keys[i])) return binding->scalar_aggregate->members[i];
    }
    MirValue owner = binding ? read_binding(c, binding, node->object, false) : expression(c, node->object);
    AstNode* key = ast_unwrap_primary_to_leaf(node->field);
    String* name = key && key->node_type == AST_NODE_IDENT ? ((AstIdentNode*)key)->name : NULL;
    TypeMap* shape = NULL;
    ShapeEntry* field = mvp_member_field(c->module, node, &shape, true);
    bool speculative = !field;
    if (speculative && name) {
        shape = mvp_member_candidate(c->module, node->object, name);
        if (shape) field = find_shape_read_field_by_name(shape, name->chars, name->len);
    }
    Type* member_domain = NULL;
    if (binding && binding->membership_contract && name) {
        ShapeEntry* known = find_shape_read_field_by_name((TypeMap*)binding->membership_contract, name->chars, name->len);
        if (known && !known->is_optional) member_domain = type_field_unwrap_simple_decl(known->type);
    }
    Type* contract = member_domain ? member_domain : mvp_value_contract(c->module, node);
    ValueRep rep = mvp_rep(contract);
    MIR_reg_t joined = em_new_reg(&c->em, "member", em_mir_type_for_rep(rep));
    MIR_label_t slow = label(c), done = label(c);
    MirValue fast = member_shape_value(c, node, binding, owner, shape, field, contract, speculative, slow);
    if (fast.reg && !speculative && shape->is_trusted_contract &&
            mvp_admission_proven(c->module, owner.semantic_contract, shape))
        return convert(c, fast, rep);
    bool direct = fast.reg != 0;
    bool descriptor_read = shape && !shape->is_trusted_contract;
    if (fast.reg) {
        fast = convert(c, fast, rep);
        move(c, joined, reg(c, fast.reg), fast.mir_type); jump(c, done);
    }
    mark(c, slow);
    if (speculative && shape && name) {
        int64_t cursor = 0;
        // bounded static alternatives cover different constructions of one
        // record family without a mutable per-site cache (D7.4.1).
        for (unsigned alternative = 1; alternative < 3; alternative++) {
            TypeMap* candidate = mvp_next_member_candidate(c->module, name, shape, &cursor);
            if (!candidate) break;
            ShapeEntry* candidate_field = find_shape_read_field_by_name(candidate, name->chars, name->len);
            MIR_label_t miss = label(c);
            MirValue next = member_shape_value(c, node, binding, owner, candidate, candidate_field, contract, true, miss);
            if (next.reg) {
                next = convert(c, next, rep);
                move(c, joined, reg(c, next.reg), next.mir_type); jump(c, done);
            }
            mark(c, miss);
        }
    }
    owner = convert(c, owner, VALUE_REP_ITEM);
    // typed loads normally bypass this arm. Resolve its spelling only on a
    // miss, avoiding module/key-table work in every hot function prologue.
    bool cold_typed = direct && !speculative && !descriptor_read;
    // quoted member names are values, not AstIdentNode storage.
    MIR_reg_t key_value = !name ? convert(c, expression(c, node->field), VALUE_REP_ITEM).reg : cold_typed
        ? convert(c, value(c, em_load_const(&c->em, em_add_const(&c->em, name), MIR_T_P),
            LMD_TYPE_STRING, VALUE_REP_RAW_GC_POINTER, &TYPE_STRING), VALUE_REP_ITEM).reg
        : property_key(c, name);
    MIR_reg_t result = em_call_2(&c->em, cold_typed || !name ? "fn_member" : "fn_member_by_id", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), MIR_T_I64, reg(c, key_value), true);
    MirValue member = convert(c, value(c, result, contract->type_id, VALUE_REP_ITEM, contract, node), rep);
    move(c, joined, reg(c, member.reg), member.mir_type); mark(c, done);
    MirValue result_value = value(c, joined, contract->type_id, rep, contract, node);
    if (!speculative && field && rep == VALUE_REP_ITEM && lambda_type_has_proven_error(owner.semantic_contract)) {
        bool optional = false;
        lambda_type_nullable_lane_base(lambda_type_remove_error(c->module->tp->pool,
            owner.semantic_contract), &optional);
        result_value.admitted_success_contract = optional || node->optional
            ? mvp_nullable_contract(c->module, field->type) : type_field_unwrap_simple_decl(field->type);
    }
    return result_value;
}

static void member_write(MvpCompiler* c, AstAssignStamNode* node) {
    MvpBinding* binding = mvp_local_binding(c->module, node->object);
    AstIdentNode* key = (AstIdentNode*)ast_unwrap_primary_to_leaf(node->key);
    MIR_reg_t name = em_load_const(&c->em, em_add_const(&c->em, key->name), MIR_T_P);
    MirValue key_value = convert(c, value(c, name, LMD_TYPE_STRING,
        VALUE_REP_RAW_GC_POINTER, &TYPE_STRING), VALUE_REP_ITEM);
    MirValue input = assignment_value(c, node);
    input = capture_projection(c, input, node->value);
    AstFieldNode access = {}; access.object = node->object; access.field = node->key;
    TypeMap* shape = NULL;
    ShapeEntry* field = mvp_member_field(c->module, &access, &shape, true);
    bool open = mvp_open_value_contract(binding->admission_contract) || binding->admission_contract == &TYPE_MAP;
    TypeMap* candidate = open ? mvp_member_candidate(c->module, node->object, key->name) : NULL;
    ShapeEntry* member = candidate ? find_shape_read_field_by_name(candidate,
        key->name->chars, key->name->len) : NULL;
    MIR_label_t slow = label(c), done = label(c);
    MirValue checked_input = input;
    if (field && shape->is_trusted_contract && shape_entry_storage_fits_data(field, shape->byte_size) &&
            field_input_proven(field, input)) {
        const LaneStorageDesc* storage = shape_entry_storage(field);
        input = admitted_field_input(c, field, input, slow);
        MIR_reg_t pointer = binding->reg;
        if (binding->record_writable) branch(c, MIR_BEQ, slow, reg(c, binding->record_writable), imm(c, 0));
        else {
            pointer = em_guard_map_shape(&c->em, binding->reg, constant(c, (intptr_t)shape), slow,
                lambda_type_nonnull_map_contract(binding->contract) != NULL, true);
            em_container_cow_guard(&c->em, binding->reg, slow, true);
        }
        MirValue member = input;
        MIR_reg_t data = em_load_at(&c->em, pointer, LAMBDA_GC_OFF_MAP_DATA, MIR_T_P, "member_write_data");
        MIR_type_t lane = native_field_storage_type(storage);
        em_store_at(&c->em, data, field->byte_offset, lane, member.reg);
        jump(c, done);
    } else if (member && !candidate->is_trusted_contract && inferred_scalar_field(member) &&
            shape_entry_storage_fits_data(member, candidate->byte_size)) {
        // inferred storage is a prediction, not admission. Check the live
        // descriptor as well as layout/COW before bypassing retag bookkeeping.
        MIR_reg_t pointer = em_guard_map_shape(&c->em, binding->reg,
            constant(c, (intptr_t)candidate), slow);
        em_container_cow_guard(&c->em, pointer, slow, true);
        guard_inferred_field_descriptor(c, member, slow);
        MirValue lane = inferred_scalar_input(c, member, checked_input, slow);
        MIR_reg_t data = em_load_at(&c->em, pointer, LAMBDA_GC_OFF_MAP_DATA, MIR_T_P, "inferred_write_data");
        em_store_at(&c->em, data, member->byte_offset, native_field_storage_type(shape_entry_storage(member)), lane.reg);
        jump(c, done);
    }
    mark(c, slow);
    input = convert(c, checked_input, VALUE_REP_ITEM);
    MIR_reg_t result;
    if (open) {
        // the implicit no-Error entry filter is not a record field contract.
        // Its member writes use ordinary COW, without a transactional clone.
        // named result contracts describe prepared prefixes, rather than an
        // exact anonymous construction shape for the resolved setter.
        if (member && !candidate->is_trusted_contract) result = em_call_5(&c->em,
            cow_import("map_set_cow_resolved", "map_set_cow_resolved_profiled"), MIR_T_I64,
            MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, property_key(c, key->name)),
            MIR_T_I64, reg(c, input.reg), MIR_T_P, imm(c, (intptr_t)candidate),
            MIR_T_P, imm(c, (intptr_t)member), true);
        else result = em_call_3(&c->em, "index_assign_cow", MIR_T_I64,
            MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, key_value.reg),
            MIR_T_I64, reg(c, input.reg), true);
    } else {
        MIR_reg_t type = em_load_const(&c->em, em_add_const(&c->em, binding->admission_contract), MIR_T_P);
        result = em_call_5(&c->em, "lambda_map_set_checked", MIR_T_I64,
            MIR_T_I64, reg(c, binding->reg), MIR_T_I64, reg(c, key_value.reg),
            MIR_T_I64, reg(c, input.reg), MIR_T_P, reg(c, type),
            MIR_T_P, imm(c, (intptr_t)"typed map member assignment"), true);
    }
    check_store_result(c, result, binding, node);
    move(c, binding->reg, reg(c, result));
    refresh_record(c, binding);
    publish_var_binding(c, binding);
    mark(c, done);
}
static void return_value(MvpCompiler* c, MirValue v, AstNode* node, bool error_exit) {
    if (c->output_failure) {
        v = convert(c, v, VALUE_REP_ITEM);
        fail_boundary(c, v.reg, c->output_failure);
        return;
    }
    Type* contract = c->function->result_contract;
    AstNode* source = node && node->node_type == AST_NODE_RETURN_STAM ? ((AstReturnNode*)node)->value : node;
    v = capture_projection(c, v, source);
    if (lambda_type_contract_has_binder(c->function->declared_result, true)) {
        v = admit(c, v, c->function->declared_result, NULL, "function return");
        // the outgoing boxed ABI carries the admitted value, never a slot in
        // this activation that a caller could resolve in its own environment.
        v = value(c, v.reg, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    // an Error returned through an open block rolls it back even when the
    // public return contract admits Error (S12.4.4v4).
    if (v.rep == VALUE_REP_ITEM && has_open_cleanup(c)) check_item_result(c, v.reg, NULL);
    // native lanes cannot hold ItemError. Admit their nullable success domain
    // directly so only absence reaches the validator (D2.3.5).
    if (c->function->declared_result && c->function->declared_result != contract &&
            v.rep == VALUE_REP_ITEM && lambda_type_accepts_error(v.semantic_contract) &&
            !mvp_boundary_proven(v.semantic_contract, contract)) {
        v = admit(c, v, c->function->declared_result, NULL, "function return");
        if (v.rep == VALUE_REP_ITEM) {
            MIR_label_t clean = label(c);
            branch(c, MIR_BNE, clean, reg(c, operation(c, MIR_URSH, reg(c, v.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
            stage_error(c, v.reg); mark(c, clean);
        }
        v = value(c, v.reg, contract->type_id, v.rep, contract, node);
    }
    v = admit(c, v, contract, NULL, "function return");
    v = convert(c, v, c->function->abi.result.normal.abi_rep);
    cleanup_to(c, NULL, error_exit);
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
    return_value(c, value(c, v.reg, LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR), node, true);
    mark(c, clean);
    Type* success = v.admitted_success_contract ? v.admitted_success_contract :
        lambda_type_remove_error(c->module->tp->pool, node ? node->type : v.semantic_contract);
    return convert(c, value(c, v.reg, success->type_id, VALUE_REP_ITEM, success, node), mvp_rep(success));
}
static bool tail_call(MvpCompiler* c, AstNode* node) {
    if (c->cleanups) return false;
    node = ast_unwrap_primary_to_leaf(node);
    MvpFunction* f = c->function;
    if (!c->tail_entry || !node || node->node_type != AST_NODE_CALL_EXPR ||
            ast_direct_call_function((AstCallNode*)node) != f->ast || f->ast->captures || f->method_owner ||
            f->ast->is_async || f->ast->is_generator || f->owns_open_scopes || mvp_function_binder_count(f) ||
            (f->ast->analysis && (f->ast->analysis->lambda_uses_shared_homes || f->ast->analysis->owns_task_scopes))) return false;
    bool erased_return = f->abi.result.normal.abi_rep == VALUE_REP_ITEM;
    if (erased_return && (!mvp_open_value_contract(f->declared_result) ||
            !mvp_open_value_contract(f->result_contract) || f->ast->captures ||
            f->method_owner || mvp_function_binder_count(f) || f->owns_open_scopes ||
            function_has_snapshots(c->module, f) || c->condition_failure || c->output_failure ||
            (f->ast->analysis && (f->ast->analysis->lambda_uses_shared_homes ||
             f->ast->analysis->owns_task_scopes)))) return false;
    AstNamedNode* p = f->ast->param;
    AstCallNode* call = (AstCallNode*)node;
    if (mvp_call_argument_count(call) != f->abi.param_count || mvp_variadic_function(f)) return false;
    bool stable_arrays = false, stable_borrows = false, replaced_pointers = false;
    for (int i = 0; i < mvp_call_argument_count(call); i++, p = (AstNamedNode*)p->next) {
        AstNode* a = mvp_call_argument(call, i);
        MvpBinding* b = mvp_binding(c->module, p->entry);
        TypeParam* parameter = lambda_type_param(p->type);
        if (parameter && parameter->is_optional) return false;
        if (parameter && parameter->is_var_param && mvp_local_binding(c->module, a) != b) return false;
        if (erased_return && parameter && parameter->is_var_param) {
            // the same open borrow keeps its original caller-owned home.
            // Typed/dependent admission and changed entry lanes stay calls.
            if (!mvp_open_value_contract(b->admission_contract) ||
                    b->rep != f->abi.params[i].canonical_rep || b->capture_home) return false;
            stable_borrows = true;
            continue;
        }
        if (erased_return && (b->rep != VALUE_REP_INT_LANE &&
                b->rep != VALUE_REP_F64 && b->rep != VALUE_REP_I64)) return false;
        if (b->rep == VALUE_REP_ITEM || b->rep == VALUE_REP_RAW_GC_POINTER) {
            if (b->rep == VALUE_REP_RAW_GC_POINTER && (!parameter || !parameter->is_var_param) &&
                    (b->contract == &TYPE_STRING || lambda_type_nonnull_map_contract(b->contract)))
                replaced_pointers = true;
            else {
                if (!mvp_array_contract(b->contract) || mvp_local_binding(c->module, a) != b ||
                        !mvp_parameter_borrowable(c->module, p)) return false;
                stable_arrays = true;
            }
        } else if (b->range.finite) {
            // an entry-only numeric guard must remain true after a backedge.
            MvpIntRange next = mvp_int_range(c->module, a);
            if (!next.finite || next.lower < b->range.lower || next.upper > b->range.upper) return false;
        }
        if ((erased_return || !f->abi.effects.may_return_error) &&
                !mvp_boundary_proven(mvp_value_contract(c->module, a), b->contract)) return false;
    }
    // a guarded boxed entry can keep scalar parameters in their proved
    // lanes; its checked sibling still rejects Item backedges above.
    if (!erased_return && !stable_arrays && !stable_borrows && !replaced_pointers &&
            (f->abi.effects.may_gc || f->abi.effects.may_return_error)) return false;
    MirValue values[LAMBDA_MAX_FUNCTION_ARGS]; int i = 0;
    p = f->ast->param;
    // evaluate all arguments before replacing any parameter, including on an inlined self edge.
    for (; i < mvp_call_argument_count(call); p = (AstNamedNode*)p->next, i++) {
        AstNode* a = mvp_call_argument(call, i);
        MvpBinding* b = mvp_binding(c->module, p->entry);
        TypeParam* parameter = lambda_type_param(p->type);
        // unchanged read-only owners remain in the same precise root frame;
        // only scalar arguments change, so storage witnesses stay valid (D6.3.1).
        MirValue appended = i == f->abi.param_count - 1 && b->string_buffer_owned && b->contract == &TYPE_STRING
            ? string_concat(c, a, b) : MirValue{};
        values[i] = admit_parameter_argument(c, p, appended.reg ? appended : mvp_array_contract(b->contract) ||
                (parameter && parameter->is_var_param)
            ? read_binding(c, b, a, false) : expression(c, a), NULL, (AstNode*)call);
        // source admission may box an untyped operand; the backedge receives
        // the same private carrier as the initial direct-call entry.
        values[i] = convert(c, values[i], b->rep);
    }
    i = 0;
    for (p = f->ast->param; p; p = (AstNamedNode*)p->next, i++)
        move(c, mvp_binding(c->module, p->entry)->reg, reg(c, values[i].reg), values[i].mir_type);
    if (replaced_pointers) for (p = f->ast->param; p; p = (AstNamedNode*)p->next)
        refresh_record(c, mvp_binding(c->module, p->entry));
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
    bool contextual = contract != NULL;
    if (!contract) contract = mvp_value_contract(c->module, match);
    // inferred homogeneous arms may carry the first arm's literal type;
    // their common native carrier must admit every selected scalar value.
    if (!contextual) contract = mvp_inferred_scalar_contract(contract);
    ValueRep rep = mvp_rep(contract);
    if (rep != VALUE_REP_F64 && rep != VALUE_REP_INT_LANE &&
            !(rep == VALUE_REP_I64 && contract == &TYPE_BOOL)) return NULL;
    Type* success = subject.admitted_success_contract ? subject.admitted_success_contract : subject.semantic_contract;
    bool error_covered = !lambda_type_accepts_error(subject.semantic_contract), success_covered = false;
    for (AstMatchArm* arm = match->first_arm; arm; arm = (AstMatchArm*)arm->next) {
        Type* pattern = arm->pattern ? mvp_match_type(arm->pattern) : NULL;
        if (arm->pattern && !pattern) return NULL;
        Type* body = pattern && match_subject_arm(c, match, arm)
            ? pattern : mvp_value_contract(c->module, arm->body);
        if (!(contextual ? mvp_boundary_proven(body, contract) : mvp_private_domain_proven(body, contract))) return NULL;
        if (!pattern) return contract;
        error_covered |= pattern == &TYPE_ERROR;
        success_covered |= mvp_membership_proven(success, pattern);
    }
    // an unhandled scrutinee keeps the generic nullable completion. Only a
    // complete numeric outcome may select native result lanes (S11.2.1).
    return error_covered && success_covered ? contract : NULL;
}

static void match_pattern_test(MvpCompiler* c, AstNode* source, MIR_reg_t subject, MIR_label_t miss) {
    AstNode* pattern = ast_unwrap_primary(source);
    if (pattern && pattern->node_type == AST_NODE_BINARY_TYPE && ((AstBinaryNode*)pattern)->op == OPERATOR_UNION) {
        AstBinaryNode* choice = (AstBinaryNode*)pattern;
        MIR_label_t next = label(c), done = label(c);
        match_pattern_test(c, choice->left, subject, next);
        jump(c, done); mark(c, next);
        match_pattern_test(c, choice->right, subject, miss);
        mark(c, done); return;
    }
    if (TypeConstrained* constrained = ast_constrained_type(source)) {
        branch(c, MIR_BEQ, miss, reg(c, constrained_type_test(c, constrained, subject)), imm(c, 0));
        return;
    }
    MirValue value = expression(c, source);
    TypeId known_kind = value.semantic_type;
    if (known_kind == LMD_TYPE_TYPE && value.rep != VALUE_REP_RAW_NON_GC_POINTER) known_kind = LMD_TYPE_ANY;
    value = convert(c, value, VALUE_REP_ITEM);
    branch(c, MIR_BEQ, miss, reg(c, em_pattern_value_test(&c->em, subject, value.reg, known_kind)), imm(c, 0));
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
    return em_query_key(&c->em, first, group ? em_group_key_expr : em_order_key_expr);
}

static void collect_query_body(MvpCompiler* c, AstForNode* node, MIR_reg_t output, MIR_reg_t order_keys,
        bool discard, MIR_reg_t output_destination = 0) {
    if (output_destination) {
        MirValue status = output_expression(c, node->then, output_destination);
        check_item_result(c, status.reg, c->output_failure);
    } else if (discard) expression(c, node->then, true);
    else {
        MirValue result = convert(c, expression(c, node->then, false, false, !node->order), VALUE_REP_ITEM);
        em_call_void_2(&c->em, node->order ? "array_push_verbatim" : "array_push_spread", MIR_T_P,
            reg(c, output), MIR_T_I64, reg(c, result.reg), true);
    }
    if (node->order) {
        MIR_reg_t key = query_key(c, node->order, false);
        if (!discard) em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P,
            reg(c, order_keys), MIR_T_I64, reg(c, key), true);
    }
}

struct MvpQueryCollector {
    AstForNode* node;
    MIR_reg_t output, order_keys, group_rows, group_keys;
    MIR_reg_t output_destination;
    bool discard;
    bool initialized;
    MIR_label_t done;
    MvpCleanup* cleanup_base;
    AstLoopNode* only_source;
    MIR_reg_t source_rows, source_keys, source_indices;
    MIR_reg_t tuples;
};

static void bind_join_tuple(MvpCompiler* c, AstLoopNode* first, AstLoopNode* stop, MIR_reg_t tuple) {
    for (AstLoopNode* source = first; source && source != stop; source = (AstLoopNode*)source->next) {
        auto bind = [&](NameEntry* entry, String* name) {
            if (!entry || !name) return;
            MIR_reg_t item = em_call_2(&c->em, "item_attr", MIR_T_I64,
                MIR_T_I64, reg(c, tuple), MIR_T_P, imm(c, (intptr_t)name->chars), true);
            bind_iteration(c, entry, value(c, item, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY));
        };
        bind(source->entry, source->name);
        bind(source->index_entry, source->index_name);
    }
}

static void emit_query_source(MvpCompiler* c, AstLoopNode* source, MvpQueryCollector* query) {
    AstForNode* node = query->node;
    bool discard = query->discard;
    LoopKeyFilter filter = query->tuples ? LOOP_KEY_ALL : source->key_filter;
    MirValue owner = query->tuples ? value(c, query->tuples, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY)
        : convert(c, expression(c, source->as), VALUE_REP_ITEM);
    // the source is one retained snapshot, including when the loop body
    // replaces a binding or mutates a nested place (S9.2.3).
    owner = value(c, em_call_1(&c->em, cow_import("cow_capture_value", "cow_capture_value_profiled"), MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    MIR_reg_t keys = em_call_1(&c->em, "item_keys", MIR_T_P, MIR_T_I64, reg(c, owner.reg), true);
    // key lists own precise frames, so every nonlocal exit must retire them
    // before its containing activation or outer traversal (D6.1.1).
    MvpCleanup cleanup = {c->cleanups, keys, MVP_CLEANUP_KEYS};
    c->cleanups = &cleanup;
    MIR_reg_t length = em_call_3(&c->em, "iter_len", MIR_T_I64, MIR_T_I64, reg(c, owner.reg),
        MIR_T_P, reg(c, keys), MIR_T_I64, imm(c, filter), true);
    bool first = !query->initialized && !query->only_source;
    if (first && node->group) {
        query->group_rows = em_call_0(&c->em, "array_plain", MIR_T_P, true);
        value(c, query->group_rows, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
        query->group_keys = em_call_0(&c->em, "array_plain", MIR_T_P, true);
        value(c, query->group_keys, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
    }
    MIR_reg_t index = constant(c, 0);
    if (first && node->resume_from) {
        // the resident T0 walk has already consumed this prefix (D8.3.2).
        MirValue resumed = convert(c, expression(c, node->resume_from), VALUE_REP_INT_LANE);
        move(c, index, reg(c, resumed.reg));
    }
    if (first && !discard && !query->output_destination) {
        if (node->order) {
            query->order_keys = em_call_0(&c->em, "array_plain", MIR_T_P, true);
            value(c, query->order_keys, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
        }
        query->output = em_call_0(&c->em, node->order ? "array_plain" : "array_spreadable", MIR_T_P, true);
        value(c, query->output, LMD_TYPE_ARRAY, VALUE_REP_RAW_GC_POINTER, &TYPE_ARRAY);
    }
    query->initialized = true;
    MIR_reg_t output = query->output, order_keys = query->order_keys;
    MIR_reg_t group_rows = query->group_rows, group_keys = query->group_keys;
    MvpControl loop = loop_control(c);
    loop.done = query->done; loop.break_cleanup_base = query->cleanup_base;
    MIR_label_t test = label(c), body = label(c);
    jump(c, test); mark(c, body); c->control = &loop;
    MIR_reg_t item = em_call_4(&c->em, !query->tuples && source->key_only ? "iter_key_at" : "iter_val_at", MIR_T_I64,
        MIR_T_I64, reg(c, owner.reg), MIR_T_P, reg(c, keys), MIR_T_I64, reg(c, index),
        MIR_T_I64, imm(c, filter), true);
    if (query->tuples) bind_join_tuple(c, (AstLoopNode*)node->loop, NULL, item);
    else bind_iteration(c, source->entry, value(c, item, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY));
    if (!query->tuples && source->index_entry) {
        MIR_reg_t key = em_call_4(&c->em, "iter_key_at", MIR_T_I64, MIR_T_I64, reg(c, owner.reg),
            MIR_T_P, reg(c, keys), MIR_T_I64, reg(c, index), MIR_T_I64, imm(c, source->key_filter), true);
        bind_iteration(c, source->index_entry, value(c, key, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY));
        if (query->source_indices) em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P,
            reg(c, query->source_indices), MIR_T_I64, reg(c, key), true);
    }
    if (query->only_source) {
        em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P,
            reg(c, query->source_rows), MIR_T_I64, reg(c, item), true);
        if (query->source_keys) {
            MIR_reg_t key = em_query_key(&c->em, (AstNode*)source->join_keys, em_join_new_expr);
            em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P,
                reg(c, query->source_keys), MIR_T_I64, reg(c, key), true);
        }
    } else if (!query->tuples && source->next) emit_query_source(c, (AstLoopNode*)source->next, query);
    else {
        for (AstNode* binding = node->let_clause; binding; binding = binding->next) expression(c, binding, true);
        if (node->where) boolean_branch(c, node->where, loop.next, false);
        if (node->group) {
            MIR_reg_t key = query_key(c, (AstNode*)node->group->keys, true);
            em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P, reg(c, group_rows), MIR_T_I64, reg(c, item), true);
            em_call_void_2(&c->em, "array_push_verbatim", MIR_T_P, reg(c, group_keys), MIR_T_I64, reg(c, key), true);
        } else collect_query_body(c, node, output, order_keys, discard, query->output_destination);
    }
    mark(c, loop.next); move(c, index, reg(c, operation(c, MIR_ADD, reg(c, index), imm(c, 1))));
    mark(c, test); branch(c, MIR_BLT, body, reg(c, index), reg(c, length));
    c->control = loop.parent;
    em_call_void_1(&c->em, "symbol_key_list_free", MIR_T_P, reg(c, keys), true);
    c->cleanups = cleanup.parent;
}

static void collect_join_source(void* owner, AstLoopNode* source,
        MIR_reg_t rows, MIR_reg_t keys, MIR_reg_t indices) {
    MvpCompiler* c = (MvpCompiler*)owner;
    MvpQueryCollector query = {};
    query.discard = true; query.initialized = true;
    query.done = label(c); query.cleanup_base = c->cleanups;
    query.only_source = source;
    query.source_rows = rows; query.source_keys = keys; query.source_indices = indices;
    emit_query_source(c, source, &query);
    mark(c, query.done);
}

static MIR_reg_t join_prior_key(void* owner, AstLoopNode* first,
        AstLoopNode* stop, MIR_reg_t tuple) {
    MvpCompiler* c = (MvpCompiler*)owner;
    bind_join_tuple(c, first, stop, tuple);
    return em_query_key(&c->em, (AstNode*)stop->join_keys, em_join_prior_expr);
}

static MirValue collection_for(MvpCompiler* c, AstForNode* node, bool discard, bool item_position,
        MIR_reg_t output_destination = 0) {
    MvpQueryCollector query = {};
    query.node = node; query.discard = discard;
    query.output_destination = output_destination;
    query.done = label(c); query.cleanup_base = c->cleanups;
    bool joined = node->group && node->loop->next;
    for (AstLoopNode* source = (AstLoopNode*)node->loop; source; source = (AstLoopNode*)source->next)
        joined |= source->on || source->optional;
    if (joined) query.tuples = em_join_stream(&c->em, (AstLoopNode*)node->loop,
        collect_join_source, join_prior_key);
    // an inner source is evaluated once for each enclosing row, using the
    // same retained walk as the outer source (S9.2.3/S14.1).
    emit_query_source(c, (AstLoopNode*)node->loop, &query);
    mark(c, query.done);
    MIR_reg_t output = query.output, order_keys = query.order_keys;
    MIR_reg_t group_rows = query.group_rows, group_keys = query.group_keys;
    if (node->group) {
        // the common runtime groups stable source rows; post-group bindings
        // receive each record [members, key...] (S14.1.1v2).
        MIR_reg_t groups = em_call_3(&c->em, "fn_group_by_keys", MIR_T_P, MIR_T_I64, reg(c, group_rows),
            MIR_T_I64, reg(c, group_keys), MIR_T_I64, imm(c, node->group->key_count), true);
        value(c, groups, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_ARRAY);
        MIR_reg_t size = em_call_1(&c->em, "fn_len", MIR_T_I64, MIR_T_I64, reg(c, groups), true);
        MIR_reg_t cursor = constant(c, 0);
        MvpControl grouped = loop_control(c);
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
        collect_query_body(c, node, output, order_keys, discard);
        mark(c, grouped.next); move(c, cursor, reg(c, operation(c, MIR_ADD, reg(c, cursor), imm(c, 1))));
        jump(c, head); mark(c, grouped.done); c->control = grouped.parent;
    }
    if (discard || output_destination) return null_value(c);
    int64_t count = 0, mask = ast_order_desc_mask(node->order, &count);
    return value(c, em_finalize_query_stream(&c->em, output, order_keys, mask, count,
        node->offset, node->limit, node->limit_from_end, item_position), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
}

static MirValue output_expression(MvpCompiler* c, AstNode* node, MIR_reg_t destination) {
    // each producer owns a checked status exit; ordinary bound values keep
    // their materialization boundary, while pure result paths stream (S2.6.6v3).
    MvpErrorTarget failure = {label(c), constant(c, ITEM_NULL), c->cleanups, true};
    value(c, failure.error, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    MvpErrorTarget* saved = c->output_failure;
    c->output_failure = &failure;
    AstNode* leaf = ast_unwrap_primary_to_leaf(node);
    MirValue status = {};
    if (!leaf) status = null_value(c);
    else if (leaf->node_type == AST_NODE_UNARY && ((AstUnaryNode*)leaf)->op == OPERATOR_PROPAGATE)
        status = output_expression(c, ((AstUnaryNode*)leaf)->operand, destination);
    else if (leaf->node_type == AST_NODE_FOR_EXPR && ast_output_stream_for((AstForNode*)leaf))
        status = collection_for(c, (AstForNode*)leaf, false, false, destination);
    else if (leaf->node_type == AST_NODE_LIST || leaf->node_type == AST_NODE_CONTENT ||
            leaf->node_type == AST_NODE_BLOCK || leaf->node_type == AST_NODE_IF_EXPR ||
            leaf->node_type == AST_NODE_CONDITIONAL_EXPR ||
            (leaf->node_type == AST_NODE_HANDLER_EXPR &&
                !((AstHandlerNode*)leaf)->catches_falsy && !((AstHandlerNode*)leaf)->filter))
        status = expression(c, leaf, false, false, false, destination);
    else if (leaf->node_type == AST_NODE_CALL_EXPR) {
        AstCallNode* call = (AstCallNode*)leaf;
        AstFuncNode* ast = ast_direct_call_function(call);
        MvpFunction* function = mvp_function(c->module, ast);
        if (function && !function->imported && !function->js_export_name &&
                ast_output_call(call, ast, c->output_inline_depth))
            status = direct_call(c, function, call, false, NULL, false, destination);
    }
    if (!status.reg) {
        MirValue item = convert(c, capture_projection(c, expression(c, node, false, false, true), node), VALUE_REP_ITEM);
        status = value(c, em_call_2(&c->em, "output_content_append", MIR_T_I64,
            MIR_T_P, reg(c, destination), MIR_T_I64, reg(c, item.reg), true),
            LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    move(c, failure.error, reg(c, status.reg)); mark(c, failure.done);
    c->output_failure = saved;
    return value(c, failure.error, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
}

struct MvpNullBranch {
    MvpBinding* binding;
    Type* contract;
    Type* producer;
    Type* present;
    bool null_when_true;
};

static MvpNullBranch null_branch_domain(MvpCompiler* c, AstNode* condition) {
    Type* present = NULL; bool null_when_true = false;
    MvpBinding* binding = mvp_null_condition(c->module, condition, &present, &null_when_true);
    return binding && binding->owner == c->function->id
        ? MvpNullBranch{binding, binding->contract, binding->producer_success_contract, present, null_when_true}
        : MvpNullBranch{};
}

static void select_null_branch(const MvpNullBranch* domain, int arm) {
    if (!domain->binding) return;
    // the predicate's error arm has already exited. Refine observation only;
    // the declaration's admission contract and physical carrier stay fixed.
    Type* selected = arm < 0 ? domain->contract : (arm != 0) == domain->null_when_true ? &TYPE_NULL : domain->present;
    domain->binding->contract = selected;
    domain->binding->producer_success_contract = arm < 0 ? domain->producer : selected;
}

struct MvpMembershipBranch {
    MvpBinding* binding;
    Type* previous;
    Type* selected;
};

static MvpMembershipBranch membership_branch_domain(MvpCompiler* c, AstIfNode* branch) {
    AstNode* condition = ast_unwrap_primary_to_leaf(branch->cond);
    if (!condition || condition->node_type != AST_NODE_BINARY || ((AstBinaryNode*)condition)->op != OPERATOR_IS) return {};
    AstBinaryNode* test = (AstBinaryNode*)condition;
    MvpBinding* binding = mvp_local_binding(c->module, test->left);
    AstNode* pattern = ast_unwrap_primary_to_leaf(test->right);
    Type* type = pattern ? pattern->type : NULL;
    if (pattern && pattern->node_type == AST_NODE_IDENT) {
        NameEntry* entry = ((AstIdentNode*)pattern)->entry;
        if (mvp_dynamic_type_binding(entry)) return {};
        if (entry && ast_definition_denotes_type(entry->node)) type = entry->node->type;
    }
    // union/constrained graphs also wear TYPE; only SIMPLE wrappers contain
    // a TypeType payload, so preserve every structural alternative.
    type = type_field_unwrap_simple_decl(type);
    Type* map = lambda_type_nonnull_map_contract(type);
    if (!binding || binding->entry->lambda_shared_local ||
            !map || map == &TYPE_MAP || map == &TYPE_OBJECT ||
            ast_body_may_write_entry(branch->then, binding->entry, true)) return {};
    // structural membership proves field domains and presence, never packed
    // offsets. Each native load still checks its live layout (S11.4.10/D4.2.5).
    return {binding, binding->membership_contract, map};
}

static MirValue function_value(MvpCompiler* c, MvpFunction* function, AstNode* node, bool declaration = false) {
    if (!function || !function->boxed_entry) {
        mvp_reject(c->module, node, "function value requires a planned boxed entry");
        return null_value(c);
    }
    MvpBinding* snapshot = named_function_binding(c, function);
    if (!declaration && snapshot && snapshot->reg && snapshot->owner == c->function->id)
        return read_binding(c, snapshot, node, false);
    MIR_reg_t address = em_new_reg(&c->em, "function_address", MIR_T_P);
    move(c, address, MIR_new_ref_op(c->em.ctx, function->target));
    MIR_reg_t environment = declaration
        ? em_capture_environment(&c->em, function->ast->captures, mvp_read_capture)
        : function_environment(c, function);
    MIR_reg_t object = em_function_value(&c->em, address, function->abi.param_count,
        function->ast->name ? function->ast->name->chars : NULL, function->ast->type,
        lambda_type_func_is_proc(function->ast->type), true, LAMBDA_MIR_PUBLIC_RETURN_ITEM_COMPANION,
        false, environment, ast_function_capture_count(function->ast));
    if (function->js_export_owner) {
        // S5.5.1v2: importer-local adapters retain the export's definition site.
        // JS import cones are execution-owned and never cache a MIR image.
        em_store_at(&c->em, object, offsetof(Function, def), MIR_T_P,
            constant(c, (intptr_t)function->ast));
        em_store_at(&c->em, object, offsetof(Function, def_module), MIR_T_P,
            constant(c, (intptr_t)function->js_export_owner));
    }
    MirValue result = value(c, object, LMD_TYPE_FUNC, VALUE_REP_RAW_GC_POINTER, &TYPE_FUNC, node);
    if (declaration && snapshot) {
        MirValue stored = convert(c, result, snapshot->rep);
        move(c, snapshot->reg, reg(c, stored.reg), stored.mir_type);
    }
    return result;
}

static MIR_reg_t mvp_fault_operand(void* owner, AstNode* node, MIR_reg_t frame) {
    MvpCompiler* c = (MvpCompiler*)owner;
    // lexical exits retire the native target before leaving its activation.
    // A fault landing instead restores the runtime checkpoint in the helper.
    MvpCleanup cleanup = {c->cleanups, frame, MVP_CLEANUP_RECOVERY};
    c->cleanups = &cleanup;
    MirValue result = convert(c, expression(c, node), VALUE_REP_ITEM);
    c->cleanups = cleanup.parent;
    return result.reg;
}

static MIR_reg_t mvp_crud_key(void* owner, AstNode* key) {
    MvpCompiler* c = (MvpCompiler*)owner;
    return convert(c, cow_path_key(c, key, key->node_type == AST_NODE_IDENT), VALUE_REP_ITEM).reg;
}

static MirValue open_statement(MvpCompiler* c, AstOpenNode* node) {
    MirValue target = convert(c, expression(c, node->target), VALUE_REP_ITEM);
    MIR_reg_t opened = em_call_1(&c->em, "fn_open_begin", MIR_T_I64,
        MIR_T_I64, reg(c, target.reg), true);
    mvp_check_item_result(c, opened);
    NameEntry* alias = ast_open_alias_entry(node);
    if (alias) {
        MvpBinding* binding = mvp_binding(c->module, alias);
        binding->scope = c->scope;
        store_binding(c, binding, value(c, opened, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY));
    }
    MvpCleanup cleanup = {c->cleanups, 0, MVP_CLEANUP_TRANSACTION};
    c->cleanups = &cleanup;
    MirValue body = convert(c, expression(c, node->body), VALUE_REP_ITEM);
    c->cleanups = cleanup.parent;
    MIR_label_t clean = label(c);
    branch(c, MIR_BNE, clean,
        reg(c, operation(c, MIR_URSH, reg(c, body.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
    (void)em_call_1(&c->em, "fn_open_end", MIR_T_I64, MIR_T_I64, imm(c, 1), true);
    fail_boundary(c, body.reg, c->scope && c->scope->parent ? &c->scope->failure : NULL);
    mark(c, clean);
    MIR_reg_t closed = em_call_1(&c->em, "fn_open_end", MIR_T_I64, MIR_T_I64, imm(c, 0), true);
    mvp_check_item_result(c, closed);
    return value(c, closed, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
}

static MIR_reg_t mvp_emit_pipe_body(void* owner, AstPipeNode* pipe, MirPipeContext context, MIR_reg_t keys) {
    MvpCompiler* c = (MvpCompiler*)owner;
    MirPipeContext saved = context_save(c);
    context_restore(c, context);
    MvpCleanup cleanup = {c->cleanups, keys, MVP_CLEANUP_KEYS};
    c->cleanups = &cleanup;
    MirValue produced = expression(c, pipe->right);
    MIR_reg_t result = pipe->op == OPERATOR_FILTER
        ? truth(c, produced) : convert(c, produced, VALUE_REP_ITEM).reg;
    context_restore(c, saved);
    c->cleanups = cleanup.parent;
    return result;
}

static MirValue expression(MvpCompiler* c, AstNode* node, bool discard, bool tail, bool item_position,
        MIR_reg_t output_destination) {
    if (!node || c->module->failed) return null_value(c);
    if (item_position && !ast_is_list_producer(node)) item_position = false;
    switch (node->node_type) {
    case AST_NODE_NAMED_ARG: case AST_NODE_KEY_EXPR:
        return expression(c, ((AstNamedNode*)node)->as, discard, tail, item_position);
    case AST_NODE_PRIMARY:
        return ((AstPrimaryNode*)node)->expr ? expression(c, ((AstPrimaryNode*)node)->expr, discard, tail, item_position)
            : literal(c, (AstPrimaryNode*)node);
    case AST_NODE_IDENT: {
        NameEntry* entry = ((AstIdentNode*)node)->entry;
        // the resolver permits a missing native namespace member as a value;
        // Full publishes the ordinary Error sentinel for that resolved miss.
        if (!entry) return value(c, constant(c, ITEM_ERROR), LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR, node);
        if (entry && entry->is_binder) return read_binder_type(c, entry, node);
        if (mvp_dynamic_type_binding(entry))
            return read_binding(c, mvp_binding(c->module, entry), node, false);
        if (entry && ast_definition_denotes_type(entry->node))
            return value(c, constant(c, (intptr_t)entry->node->type),
                LMD_TYPE_TYPE, VALUE_REP_RAW_NON_GC_POINTER, &TYPE_TYPE, node);
        Script* owner; int slot;
        if (mvp_interp_binding(c->module, entry, &owner, &slot) &&
                (!ast_index_node_is_function(entry->node) ||
                 !mvp_function(c->module, (AstFuncNode*)entry->node) ||
                 mvp_function(c->module, (AstFuncNode*)entry->node)->interp_binding)) {
            MvpBinding* binding = mvp_binding(c->module, entry);
            if (binding) return read_binding(c, binding, node, true);
            MIR_reg_t state = em_module_state_for_unit(&c->em, constant(c, script_module_layout_id(owner)), true);
            MIR_reg_t item = em_module_slot_item(&c->em, state, imm(c, slot), true);
            return value(c, capture_item(c, item, &TYPE_ANY), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        }
        if (entry && entry->node && ast_index_node_is_function(entry->node))
            return function_value(c, mvp_function(c->module, (AstFuncNode*)entry->node), node);
        MvpBinding* b = mvp_binding(c->module, entry);
        if (!b && entry && entry->node && !entry->is_mutable && entry->node->node_type == AST_NODE_VARIABLE_DECLARATOR) {
            Script* owner = mvp_import_owner(c->module, entry);
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
                MIR_reg_t item = em_module_slot_item(&c->em, state, reg(c, slot), true);
                value(c, item, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
                return value(c, em_call_1(&c->em, cow_import("cow_capture_value", "cow_capture_value_profiled"),
                    MIR_T_I64, MIR_T_I64, reg(c, item), true),
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
    case AST_NODE_BINARY_TYPE: case AST_NODE_UNARY_TYPE: case AST_NODE_CONTENT_TYPE:
    case AST_NODE_LIST_TYPE: case AST_NODE_ARRAY_TYPE: case AST_NODE_MAP_TYPE:
    case AST_NODE_ELMT_TYPE: case AST_NODE_FUNC_TYPE: case AST_NODE_CONSTRAINED_TYPE:
    case AST_NODE_PATTERN_ISLAND:
        // the frontend owns immutable resolved type graphs, just as in T0's value path.
        return value(c, em_load_const(&c->em, em_add_const(&c->em, node->type), MIR_T_P),
            LMD_TYPE_TYPE, VALUE_REP_RAW_NON_GC_POINTER, &TYPE_TYPE, node);
    case AST_NODE_OBJECT_TYPE: {
        AstObjectTypeNode* definition = (AstObjectTypeNode*)node;
        TypeObject* type = (TypeObject*)((TypeType*)definition->type)->type;
        MirValue declared = value(c, constant(c, (intptr_t)node->type),
            LMD_TYPE_TYPE, VALUE_REP_ITEM, &TYPE_TYPE, node);
        MvpBinding* binding = NULL;
        if (mvp_type_has_environment(node->type)) {
            MirValue base = mvp_dynamic_type_binding(definition->base_entry)
                ? read_binding(c, mvp_binding(c->module, definition->base_entry), node, false) : null_value(c);
            base = convert(c, base, VALUE_REP_ITEM);
            MIR_reg_t runtime = em_call_2(&c->em, "lambda_object_type_value", MIR_T_I64,
                MIR_T_I64, reg(c, declared.reg), MIR_T_I64, reg(c, base.reg), true);
            mvp_check_item_result(c, runtime);
            declared = value(c, runtime, LMD_TYPE_TYPE, VALUE_REP_ITEM, &TYPE_TYPE, node);
            binding = mvp_binding(c->module, definition->entry);
            // publish before building method captures so self-type references
            // retain this declaration activation, including recursive classes.
            store_binding(c, binding, declared);
        }
        for (AstNode* method = definition->methods; method; method = method->next) {
            MvpFunction* function = mvp_function(c->module, (AstFuncNode*)method);
            if (!function) continue;
            em_object_method_register(&c->em, type->type_index, function->ast, function->target);
            if (function->ast->captures) {
                MirValue captured = convert(c, function_value(c, function, method, true), VALUE_REP_ITEM);
                em_store_at(&c->em, captured.reg, offsetof(Function, def), MIR_T_P,
                    constant(c, (intptr_t)function->ast));
                em_store_at(&c->em, captured.reg, offsetof(Function, def_module), MIR_T_P,
                    constant(c, (intptr_t)c->module->tp->script_owner));
                MIR_reg_t retained = em_call_3(&c->em, "lambda_object_capture_method", MIR_T_I64,
                    MIR_T_I64, reg(c, declared.reg), MIR_T_P, imm(c, (intptr_t)function->ast->name->chars),
                    MIR_T_I64, reg(c, captured.reg), true);
                mvp_check_item_result(c, retained);
            }
        }
        return declared;
    }
    case AST_NODE_PATH_EXPR:
        return value(c, em_literal_path(&c->em, (AstPathNode*)node), LMD_TYPE_PATH,
            VALUE_REP_RAW_GC_POINTER, &TYPE_PATH, node);
    case AST_NODE_PATH_INDEX_EXPR:
        return value(c, em_computed_path(&c->em, (AstPathIndexNode*)node), LMD_TYPE_ANY,
            VALUE_REP_ITEM, &TYPE_ANY, node);
    case AST_NODE_NAVIGATION_EXPR: {
        ValueRep rep;
        MIR_reg_t result = em_navigation_value(&c->em, (AstNavigationNode*)node,
            {c->current_item != 0, c->current_parent, c->current_root}, &rep);
        return value(c, result, rep == VALUE_REP_RAW_GC_POINTER ? LMD_TYPE_PATH : LMD_TYPE_ANY,
            rep, rep == VALUE_REP_RAW_GC_POINTER ? &TYPE_PATH : &TYPE_ANY, node);
    }
    case AST_NODE_QUERY_EXPR: {
        AstQueryNode* query = (AstQueryNode*)node;
        MirValue object = convert(c, expression(c, query->object), VALUE_REP_ITEM);
        MirValue pattern = convert(c, expression(c, query->query), VALUE_REP_ITEM);
        return value(c, em_call_3(&c->em, "fn_query", MIR_T_I64,
            MIR_T_I64, reg(c, object.reg), MIR_T_I64, reg(c, pattern.reg),
            MIR_T_I64, imm(c, query->direct ? 1 : 0), true),
            LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    case AST_NODE_SYS_FUNC: {
        SysFuncInfo* info = ((AstSysFuncNode*)node)->fn_info;
        if (!info || !info->func_ptr) return null_value(c);
        MIR_reg_t object = em_function_value(&c->em, constant(c, (intptr_t)info->func_ptr),
            info->arg_count, info->name, node->type, lambda_type_func_is_proc(node->type), false, 0, true);
        return value(c, object, LMD_TYPE_FUNC, VALUE_REP_RAW_GC_POINTER, &TYPE_FUNC, node);
    }
    case AST_NODE_FUNC: case AST_NODE_FUNC_EXPR: case AST_NODE_PROC: {
        MvpFunction* function = mvp_function(c->module, (AstFuncNode*)node);
        if (!mvp_function_expression(node)) {
            if (named_function_binding(c, function)) function_value(c, function, node, true);
            return null_value(c);
        }
        return function_value(c, function, node);
    }
    case AST_NODE_RAISE_EXPR: {
        AstRaiseNode* raised = (AstRaiseNode*)node;
        MirValue operand = convert(c, expression(c, raised->value), VALUE_REP_ITEM);
        MIR_reg_t error = em_call_4(&c->em, "lambda_raise_operand", MIR_T_I64,
            MIR_T_I64, reg(c, operand.reg), MIR_T_P, imm(c, (intptr_t)c->module->tp->reference),
            MIR_T_I64, imm(c, raised->site_line), MIR_T_I64, imm(c, raised->site_column), true);
        return_value(c, value(c, error, LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR), node, true);
        return null_value(c);
    }
    case AST_NODE_CURRENT_ERROR:
        if (!c->current_error) { mvp_reject(c->module, node, "current error outside a handler"); return null_value(c); }
        return value(c, c->current_error, LMD_TYPE_ERROR, VALUE_REP_ITEM, &TYPE_ERROR, node);
    case AST_NODE_CURRENT_ITEM: case AST_NODE_CURRENT_INDEX: {
        MIR_reg_t selected = node->node_type == AST_NODE_CURRENT_ITEM ? c->current_item : c->current_index;
        return selected ? value(c, selected, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node) : null_value(c);
    }
    case AST_NODE_LAST_INDEX: {
        if (!c->last_index_item) {
            mvp_reject(c->module, node, "last requires a subscript receiver"); return null_value(c);
        }
        MIR_reg_t count = em_call_1(&c->em, "fn_seq_count", MIR_T_I64,
            MIR_T_I64, reg(c, c->last_index_item), true);
        return value(c, operation(c, MIR_SUB, reg(c, count), imm(c, 1)),
            LMD_TYPE_INT, VALUE_REP_INT_LANE, &TYPE_INT, node);
    }
    case AST_NODE_INDEX_EXPR: return array_read(c, (AstFieldNode*)node);
    case AST_NODE_MEMBER_EXPR: {
        AstFieldNode* member = (AstFieldNode*)node;
        ShapeEntry* field = c->current_item == c->method_receiver
            ? mvp_method_member(c->function, member->object, member->field) : NULL;
        // explicit receiver reads share the field locals updated by bare-name writes.
        if (field) return read_binding(c, mvp_binding(c->module, field->binding), node, true);
        return member_read(c, member);
    }
    case AST_NODE_START:
        return value(c, em_start_task(&c->em, (AstStartNode*)node, mvp_check_task_arguments),
            LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    case AST_NODE_OPEN_STAM: return open_statement(c, (AstOpenNode*)node);
    case AST_NODE_CRUD_STAM:
        return value(c, em_crud_statement(&c->em, (AstCrudNode*)node,
            mvp_crud_key, mvp_check_item_result), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    case AST_NODE_ARRAY: return array_literal(c, (AstArrayNode*)node);
    case AST_NODE_LIST: {
        AstListNode* list = (AstListNode*)node;
        return statements(c, list->item, discard, tail, item_position, MVP_LIST_RESULTS, list->declare, output_destination);
    }
    case AST_NODE_SPREAD: {
        MirValue source = convert(c, expression(c, ((AstSpreadNode*)node)->argument), VALUE_REP_ITEM);
        return value(c, em_call_1(&c->em, item_position ? "seq_spread_item" : "seq_spread_value",
            MIR_T_I64, MIR_T_I64, reg(c, source.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
    }
    case AST_NODE_MAP: return map_literal(c, (AstMapNode*)node);
    case AST_NODE_ELEMENT: return element_literal(c, (AstElementNode*)node);
    case AST_NODE_OBJECT_LITERAL: return object_literal(c, (AstObjectLiteralNode*)node);
    case AST_NODE_BINARY: return binary(c, (AstBinaryNode*)node);
    case AST_NODE_CALL_EXPR: {
        if (tail && !discard && tail_call(c, node)) {
            // the backedge has no normal completion. Keep its unreachable join
            // operand in the result lane so it cannot invent a nullable arm.
            Type* result = c->function->result_contract;
            ValueRep rep = c->function->abi.result.normal.abi_rep;
            return value(c, rep == VALUE_REP_F64 ? float_constant(c, 0) : constant(c, 0),
                result->type_id, rep, result, node);
        }
        MirValue result = call(c, (AstCallNode*)node, discard);
        return ((AstCallNode*)node)->propagate ? propagated_value(c, result, node) : result;
    }
    case AST_NODE_UNARY: {
        AstUnaryNode* n = (AstUnaryNode*)node;
        MirValue v = n->op == OPERATOR_NOT ? observed_expression(c, n->operand) : expression(c, n->operand);
        if (n->op == OPERATOR_PROPAGATE) return propagated_value(c, v, node);
        if (n->op == OPERATOR_SPREAD) return v;
        if (const char* helper = em_reference_unary_helper(n->op)) {
            v = convert(c, v, VALUE_REP_ITEM);
            return value(c, em_call_1(&c->em, helper, MIR_T_I64,
                MIR_T_I64, reg(c, v.reg), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        }
        if (n->op == OPERATOR_NOT) {
            if (v.semantic_type == LMD_TYPE_BOOL) return value(c, operation(c, MIR_EQ,
                reg(c, truth(c, v)), imm(c, 0)), LMD_TYPE_BOOL, VALUE_REP_I64, &TYPE_BOOL, node);
            bool optional = false;
            if (v.rep == VALUE_REP_ITEM &&
                    lambda_type_nullable_lane_base(v.admitted_success_contract, &optional) == &TYPE_BOOL) {
                // preserve the exact Error Item before consuming Boolean truth;
                // the optional normal arm has the same false truth as null.
                MIR_reg_t joined = em_new_reg(&c->em, "not_outcome", MIR_T_I64);
                MIR_label_t done = label(c);
                join_operand_errors(c, &v, 1, joined, done);
                MIR_reg_t inverted = operation(c, MIR_NE, reg(c, v.reg), imm(c, ITEM_TRUE));
                move(c, joined, reg(c, operation(c, MIR_OR, reg(c, inverted), imm(c, (uint64_t)LMD_TYPE_BOOL << 56))));
                mark(c, done);
                MirValue result = value(c, joined, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
                result.admitted_success_contract = &TYPE_BOOL;
                return result;
            }
            v = convert(c, v, VALUE_REP_ITEM);
            MirValue result = value(c, em_call_1(&c->em, "fn_not_value", MIR_T_I64, MIR_T_I64, reg(c, v.reg), true),
                LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
            result.admitted_success_contract = &TYPE_BOOL;
            return result;
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
    case AST_NODE_EXPR_STMT: return expression(c, ((AstExprStmtNode*)node)->expression, discard, tail, item_position);
    case AST_NODE_CONTENT:
        return statements(c, ((AstListNode*)node)->item, discard, tail, item_position,
            !c->function->ast && ((AstScript*)c->module->tp->ast_root)->child == node
                ? MVP_CONTENT_RESULTS : MVP_BLOCK_RESULTS, NULL, output_destination);
    case AST_NODE_BLOCK: return statements(c, ((AstBlockNode*)node)->statements, discard, tail, item_position,
        MVP_BLOCK_RESULTS, NULL, output_destination);
    case AST_SCRIPT:
        return statements(c, ((AstScript*)node)->child, discard, tail, item_position, MVP_CONTENT_RESULTS);
    case AST_NODE_TYPE_STAM:
        for (AstNode* declaration = ((AstLetNode*)node)->declare; declaration; declaration = declaration->next)
            if (declaration->node_type == AST_NODE_VARIABLE_DECLARATOR &&
                    mvp_dynamic_type_binding(((AstDeclaratorNode*)declaration)->entry))
                expression(c, declaration, true);
        return null_value(c);
    case AST_NODE_VIEW: case AST_NODE_IMPORT:
    case AST_NODE_STRING_PATTERN: case AST_NODE_SYMBOL_PATTERN: return null_value(c);
    case AST_NODE_PUB_STAM:
        for (AstNode* d = ((AstLetNode*)node)->declare; d; d = d->next) expression(c, d);
        return null_value(c);
    case AST_NODE_LET_STAM: case AST_NODE_VAR_STAM:
        for (AstNode* d = ((AstVarDeclNode*)node)->declarations; d; d = d->next) expression(c, d);
        return null_value(c);
    case AST_NODE_DECOMPOSE: {
        AstDecomposeNode* n = (AstDecomposeNode*)node;
        MirValue source = convert(c, expression(c, n->as), VALUE_REP_ITEM);
        capture_item(c, source.reg, &TYPE_ANY);
        for (int i = 0; i < n->name_count; i++) {
            MvpBinding* b = mvp_binding(c->module, n->entries[i]);
            MirValue item = value(c, em_decompose_item(&c->em, n, source.reg, i),
                LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
            // each child is an independent observer of its source place;
            // the binding owns cold scalar bytes across later projections.
            item.reg = capture_item(c, item.reg, &TYPE_ANY);
            b->scope = c->scope;
            store_binding(c, b, item);
            initialize_shared_binding(c, b);
        }
        return null_value(c);
    }
    case AST_NODE_VARIABLE_DECLARATOR: {
        AstDeclaratorNode* n = (AstDeclaratorNode*)node;
        if (n->is_type_definition && !mvp_dynamic_type_binding(n->entry)) return null_value(c);
        MvpBinding* b = mvp_binding(c->module, n->entry);
        if (!b || !b->reg) {
            mvp_reject(c->module, node, "declaration requires a local scalar binding"); return null_value(c);
        }
        b->scope = c->scope;
        if (scalarize_binding(c, b, n) || scalarize_call_binding(c, b, n)) return null_value(c);
        store_binding(c, b, binding_value(c, b, n->init));
        initialize_shared_binding(c, b);
        return null_value(c);
    }
    case AST_NODE_ASSIGN_STAM: case AST_NODE_INDEX_ASSIGN_STAM: {
        AstAssignStamNode* n = (AstAssignStamNode*)node;
        if (n->object) {
            if (mvp_edit_model_place(c->function, n->object)) return edit_model_store(c, n, false);
            if ((n->key && n->key->next) || !mvp_local_binding(c->module, n->object)) {
                MvpPlace place = place_path(c, n->object, n->key);
                if (!place.root) mvp_reject(c->module, node, "assignment requires a resolved place");
                else write_place(c, place, assignment_value(c, n), node);
            } else array_write(c, n);
            return null_value(c);
        }
        MvpBinding* b = mvp_binding(c->module, n->target_entry);
        if (!b || b->owner != c->function->id) {
            mvp_reject(c->module, node, "assignment requires a local scalar binding"); return null_value(c);
        }
        AstNode* previous = c->consuming_array_join;
        c->consuming_array_join = mvp_consuming_array_join(c->module, b, n->value)
            ? ast_unwrap_primary_to_leaf(n->value) : NULL;
        MirValue assigned = binding_value(c, b, n->value);
        c->consuming_array_join = previous;
        store_binding(c, b, assigned, "assignment to"); return null_value(c);
    }
    case AST_NODE_MEMBER_ASSIGN_STAM: {
        AstAssignStamNode* n = (AstAssignStamNode*)node;
        if (mvp_edit_model_place(c->function, n->object)) return edit_model_store(c, n, true);
        ShapeEntry* field = c->current_item == c->method_receiver
            ? mvp_method_member(c->function, n->object, n->key) : NULL;
        if (field) {
            MvpBinding* binding = mvp_binding(c->module, field->binding);
            // publish through the same method epilogue as a bare field assignment.
            store_binding(c, binding, assignment_value(c, n), "assignment to");
            binding->method_field_written = true;
            return null_value(c);
        }
        if (mvp_local_binding(c->module, n->object)) member_write(c, n);
        else {
            MvpPlace place = place_path(c, n->object, n->key, true);
            if (!place.root) mvp_reject(c->module, node, "assignment requires a resolved place");
            else write_place(c, place, assignment_value(c, n), node);
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
        bool error_condition = !bool_condition && lambda_type_accepts_error(condition.semantic_contract);
        Type* planned_contract = output_destination ? &TYPE_ANY : mvp_value_contract(c->module, node);
        ValueRep rep = error_condition || ast_expr_may_defect(node, 0) ? VALUE_REP_ITEM : mvp_rep(planned_contract);
        // pointer and Item joins share the i64 width, so reserve the original
        // result register before lowering either branch.
        MIR_reg_t r = em_new_reg(&c->em, "if_result", em_mir_type_for_rep(rep));
        if (error_condition) {
            condition = convert(c, condition, VALUE_REP_ITEM); move(c, r, reg(c, condition.reg));
            branch(c, MIR_BEQ, done, reg(c, operation(c, MIR_URSH, reg(c, condition.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        }
        if (bool_condition) boolean_branch(c, n->cond, otherwise, false);
        else branch(c, MIR_BEQ, otherwise, reg(c, truth(c, condition)), imm(c, 0));
        MvpNullBranch domain = null_branch_domain(c, n->cond);
        MvpMembershipBranch membership = membership_branch_domain(c, n);
        // discarded branches retain evaluation and scope exits, without converting
        // their unused null results into the inferred numeric join (D8.2.6).
        // guard a closed array arm even when a surrounding loop also updates its index elsewhere.
        MirValue yes = null_value(c);
        select_null_branch(&domain, 1);
        if (membership.binding) membership.binding->membership_contract = membership.selected;
        if (discard) array_iteration(c, n->then);
        else yes = output_destination ? output_expression(c, n->then, output_destination)
            : expression(c, n->then, false, tail, item_position);
        MIR_insn_t yes_end = c->em.insert_after ? c->em.insert_after :
            DLIST_TAIL(MIR_insn_t, c->em.func_item->u.func->insns);
        jump(c, done); mark(c, otherwise);
        MirValue no = null_value(c);
        select_null_branch(&domain, 0);
        if (membership.binding) membership.binding->membership_contract = membership.previous;
        if (discard) array_iteration(c, n->otherwise);
        else no = output_destination ? output_expression(c, n->otherwise, output_destination)
            : expression(c, n->otherwise, false, tail, item_position);
        select_null_branch(&domain, -1);
        if (!discard) {
            // select the join from emitted carriers: a call's checked actuals
            // may add an error arm absent from its inferred result (D2.3.5).
            if (rep != VALUE_REP_ITEM &&
                    ((yes.rep == VALUE_REP_ITEM && mvp_rep(yes.semantic_contract) == VALUE_REP_ITEM) ||
                     (no.rep == VALUE_REP_ITEM && mvp_rep(no.semantic_contract) == VALUE_REP_ITEM))) {
                // boxed outcomes can add Error to scalar joins too. A float
                // result needs a new I64 register before either arm is boxed.
                if (em_mir_type_for_rep(rep) != MIR_T_I64)
                    r = em_new_reg(&c->em, "if_outcome", MIR_T_I64);
                rep = VALUE_REP_ITEM;
            }
            MirValue arm = convert(c, no, rep); move(c, r, reg(c, arm.reg), arm.mir_type);
            MIR_insn_t no_end = c->em.insert_after;
            c->em.insert_after = yes_end;
            arm = convert(c, yes, rep); move(c, r, reg(c, arm.reg), arm.mir_type);
            c->em.insert_after = no_end;
        }
        mark(c, done);
        if (discard) return null_value(c);
        Type* joined = rep == VALUE_REP_ITEM
            ? mvp_join_value_contracts(c->module, yes.semantic_contract, no.semantic_contract)
            : planned_contract;
        if (error_condition)
            joined = lambda_type_union_normalized(c->module->tp->pool, joined, &TYPE_ERROR);
        MirValue result = value(c, r, joined->type_id, rep, joined, node);
        if (rep == VALUE_REP_ITEM) result.admitted_success_contract = normal_join_contract(c, yes, no);
        return result;
    }
    case AST_NODE_PIPE: {
        AstPipeNode* pipe = (AstPipeNode*)node;
        if (pipe->op == OPERATOR_PIPE && !has_current_item_ref(pipe->right)) {
            AstNode* right = ast_unwrap_primary_to_leaf(pipe->right);
            if (right && right->node_type == AST_NODE_CALL_EXPR && ((AstCallNode*)right)->pipe_inject)
                return expression(c, pipe->right, discard, tail, item_position);
            // arbitrary expressions remain callable application, including a
            // match whose own ~ is bound locally (S10.1.2v4/S10.1.7v2).
            MirValue subject = convert(c, expression(c, pipe->left), VALUE_REP_ITEM);
            MirValue callable = convert(c, expression(c, pipe->right), VALUE_REP_ITEM);
            return value(c, em_call_2(&c->em, "fn_pipe_call", MIR_T_I64,
                MIR_T_I64, reg(c, subject.reg), MIR_T_I64, reg(c, callable.reg), true),
                LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        }
        MirValue subject = convert(c, expression(c, pipe->left), VALUE_REP_ITEM);
        MirPipeContext saved = context_save(c);
        if (pipe->op != OPERATOR_THAT)
            return value(c, em_pipe_walk(&c->em, pipe, subject.reg, saved, mvp_emit_pipe_body),
                LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node);
        context_restore(c, em_subject_context(&c->em, saved, subject.reg));
        MirValue predicate = expression(c, pipe->right);
        MIR_reg_t accepted = truth(c, predicate);
        context_restore(c, saved);
        MIR_reg_t result = em_proviso_result(&c->em, subject.reg, accepted);
        Type* contract = mvp_nullable_contract(c->module, subject.semantic_contract);
        return convert(c, value(c, result, contract->type_id,
            VALUE_REP_ITEM, contract, node), mvp_rep(contract));
    }
    case AST_NODE_MATCH_EXPR: {
        AstMatchNode* match = (AstMatchNode*)node;
        // ordered base-type cases consume one rooted scrutinee, including an
        // error value handled by an explicit error arm (S11.2.1).
        MirValue subject = convert(c, expression(c, match->scrutinee), VALUE_REP_ITEM);
        MirPipeContext saved = context_save(c);
        context_restore(c, em_subject_context(&c->em, saved, subject.reg, true));
        Type* contract = native_match_contract(c, match, subject);
        Type* observed = NULL;
        bool covered = false;
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
                } else if (pattern && subject.admitted_success_contract &&
                        mvp_membership_proven(subject.admitted_success_contract, pattern)) {
                    if (pattern != &TYPE_ERROR)
                        branch(c, MIR_BEQ, next, reg(c, operation(c, MIR_URSH,
                            reg(c, subject.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
                } else if (mvp_exact_membership_domain(pattern)) {
                    MIR_reg_t matched = em_call_2(&c->em, "lambda_value_type_is_exact", MIR_T_I64,
                        MIR_T_I64, reg(c, subject.reg), MIR_T_P, imm(c, (intptr_t)pattern), true);
                    branch(c, MIR_BEQ, next, reg(c, matched), imm(c, 0));
                } else match_pattern_test(c, arm->pattern, subject.reg, next);
            }
            MirValue selected = contract && pattern && match_subject_arm(c, match, arm)
                ? value(c, subject.reg, pattern->type_id, VALUE_REP_ITEM, pattern, arm->body)
                : expression(c, arm->body, discard);
            if (!discard) {
                observed = mvp_join_value_contracts(c->module, observed, selected.semantic_contract);
                covered |= mvp_match_arm_covers(subject.semantic_contract, arm);
                selected = convert(c, selected, rep);
                move(c, result, reg(c, selected.reg), selected.mir_type);
            }
            jump(c, done); mark(c, next);
        }
        mark(c, done);
        context_restore(c, saved);
        // generic transport still has the selected arms' domains; an
        // uncovered match adds absence instead of erasing them to any.
        if (!contract) contract = covered ? observed :
            mvp_join_value_contracts(c->module, observed, &TYPE_NULL);
        return discard ? null_value(c) : value(c, result,
            contract ? contract->type_id : LMD_TYPE_ANY, rep, contract ? contract : &TYPE_ANY, node);
    }
    case AST_NODE_HANDLER_EXPR: case AST_NODE_HANDLER_STAM: {
        AstHandlerNode* handler = (AstHandlerNode*)node;
        MIR_reg_t checkpoint = output_destination
            ? em_call_1(&c->em, "output_content_checkpoint", MIR_T_I64,
                MIR_T_P, reg(c, output_destination), true) : 0;
        // ordinary recovery branches from one evaluated outcome; a clean
        // operand skips the handler and keeps its precise owner (S7.6.1v9).
        Type* operand_contract = mvp_value_contract(c->module, handler->operand);
        Type* protected_contract = lambda_type_union_normalized(c->module->tp->pool,
            operand_contract, &TYPE_ERROR);
        MirValue operand = output_destination ? output_expression(c, handler->operand, output_destination)
            : handler->is_statement && c->function->ast &&
                lambda_type_func_is_proc(c->function->ast->type)
            ? value(c, em_local_fault_expression(&c->em, handler->operand, mvp_fault_operand),
                protected_contract->type_id, VALUE_REP_ITEM, protected_contract, handler->operand)
            : convert(c, expression(c, handler->operand), VALUE_REP_ITEM);
        MIR_reg_t result = em_new_reg(&c->em, "handled", MIR_T_I64);
        value(c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
        MIR_label_t caught = label(c), done = label(c);
        move(c, result, reg(c, operand.reg));
        branch(c, MIR_BEQ, caught, reg(c, operation(c, MIR_URSH, reg(c, operand.reg), imm(c, 56))), imm(c, LMD_TYPE_ERROR));
        if (handler->catches_falsy) branch(c, MIR_BEQ, caught, reg(c, truth(c, operand)), imm(c, 0));
        jump(c, done); mark(c, caught);
        if (handler->filter) match_pattern_test(c, handler->filter, operand.reg, done);
        if (output_destination)
            // encoder errors discard only this operand's partial bytes (S2.6.6v3).
            em_call_void_2(&c->em, "output_content_rewind", MIR_T_P, reg(c, output_destination),
                MIR_T_I64, reg(c, checkpoint), true);
        MIR_reg_t enclosing_error = c->current_error;
        c->current_error = operand.reg;
        MirValue recovery = output_destination ? output_expression(c, handler->body, output_destination)
            : convert(c, expression(c, handler->body, discard), VALUE_REP_ITEM);
        c->current_error = enclosing_error;
        move(c, result, reg(c, recovery.reg)); mark(c, done);
        // the pass-through arm cannot retain an outcome consumed by this
        // handler. Keep fresh recovery errors and filtered misses (S7.6.1v9).
        Type* pass_contract = mvp_handler_pass_contract(c->module, handler, operand.semantic_contract);
        Type* contract = mvp_join_value_contracts(c->module, pass_contract, recovery.semantic_contract);
        MirValue handled = value(c, result, contract->type_id == LMD_TYPE_TYPE ? LMD_TYPE_ANY : contract->type_id,
            VALUE_REP_ITEM, contract, node);
        handled.admitted_success_contract = normal_join_contract(c, operand, recovery);
        return handled;
    }
    case AST_NODE_LOOP: {
        AstLoopControlNode* n = (AstLoopControlNode*)node;
        MvpHoistedCall* hoisted = c->hoisted_calls;
        hoist_loop_calls(c, node);
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
        MvpControl loop = loop_control(c);
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
            MvpErrorTarget failure = {loop.done, completion, loop.cleanup_base, true};
            MvpErrorTarget* saved_failure = c->condition_failure;
            c->condition_failure = &failure;
            boolean_branch(c, n->cond, body, true);
            c->condition_failure = saved_failure;
        } else boolean_branch(c, n->cond, body, true);
        mark(c, loop.done); c->control = loop.parent;
        c->loop_array = saved_array; c->loop_array_items = saved_items;
        if (fast_done) mark(c, fast_done);
        restore_hoisted_calls(c, hoisted);
        return completion ? value(c, completion, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY, node) : null_value(c);
    }
    case AST_NODE_FOR_EXPR: {
        AstForNode* n = (AstForNode*)node;
        AstLoopNode* source = (AstLoopNode*)n->loop;
        AstNode* collection = ast_unwrap_primary_to_leaf(source->as);
        MvpBinding* counter = mvp_binding(c->module, source->entry);
        bool range_source = counter && counter->rep == VALUE_REP_INT_LANE &&
            mvp_native_range_source(c->module, collection);
        TypeId kind = mvp_value_contract(c->module, source->as)->type_id;
        bool positions = source->key_only && source->key_filter == LOOP_KEY_INT &&
            (mvp_array_contract(mvp_value_contract(c->module, source->as)) || kind == LMD_TYPE_STRING);
        // the enclosing block owns result demand: a braced for still yields
        // its stream in functional content (S2.5.3v3), even outside item position.
        bool discard_stream = discard;
        if (!discard_stream || source->next || n->where || n->group || n->order || n->let_clause || n->limit || n->offset ||
                source->index_entry || (!range_source && !positions))
            return collection_for(c, n, discard_stream, item_position);
        MvpHoistedCall* hoisted = c->hoisted_calls;
        hoist_loop_calls(c, node);
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
        MvpControl loop = loop_control(c);
        MIR_label_t invalid = label(c), body = label(c);
        MirValue bounds[] = {first, last};
        bool error_bounds = lambda_type_accepts_error(first.semantic_contract) ||
            lambda_type_accepts_error(last.semantic_contract);
        MIR_reg_t completion = !finite || error_bounds ? constant(c, ITEM_NULL) : 0;
        if (error_bounds) {
            join_operand_errors(c, bounds, 2, completion, loop.done);
            move(c, completion, imm(c, ITEM_NULL));
        }
        // inferred integer bounds can still arrive as boxed call outcomes;
        // consume their actual lanes before range guards and comparisons (D2.3.5).
        first = convert(c, bounds[0], VALUE_REP_INT_LANE);
        last = convert(c, bounds[1], VALUE_REP_INT_LANE);
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
            move(c, completion, reg(c, em_call_2(&c->em, "fn_range_bound_error", MIR_T_I64,
                MIR_T_I64, reg(c, first.reg), MIR_T_I64, reg(c, last.reg), true)));
        }
        mark(c, loop.done);
        if (fast_done) mark(c, fast_done);
        restore_hoisted_calls(c, hoisted);
        return completion ? value(c, completion, LMD_TYPE_ANY, VALUE_REP_ITEM,
            lambda_type_union_normalized(c->module->tp->pool, &TYPE_NULL, &TYPE_ERROR), node) : null_value(c);
    }
    case AST_NODE_RETURN_STAM: {
        AstNode* result = ((AstReturnNode*)node)->value;
        if (tail_call(c, result)) return null_value(c);
        AstNode* leaf = ast_unwrap_primary_to_leaf(result);
        Type* destination = lambda_type_nonnull_map_contract(c->function->result_contract);
        MvpBinding* owner = mvp_local_binding(c->module, result);
        AstNode* initial = owner && mvp_binding_node(owner)->node_type == AST_NODE_VARIABLE_DECLARATOR
            ? ast_unwrap_primary_to_leaf(((AstDeclaratorNode*)mvp_binding_node(owner))->init) : NULL;
        // a fresh local observed only by this return transfers its owner;
        // retaining another alias keeps the ordinary snapshot capture.
        bool moving = initial && (initial->node_type == AST_NODE_MAP || initial->node_type == AST_NODE_ARRAY) &&
            mvp_binding_return_contract(c->module, owner->entry);
        ArrayRepCert* return_cert = mvp_array_element(c->function->result_contract)
            ? lambda_array_rep_cert_create(c->module->tp->pool, c->function->result_contract) : NULL;
        MirValue filled = contextual_fill(c, result, c->function->result_contract, return_cert, "return");
        MirValue returned = filled.reg ? filled : moving ? read_binding(c, owner, result, false) : destination && leaf && leaf->node_type == AST_NODE_MAP
            ? map_literal(c, (AstMapNode*)leaf, (TypeMap*)destination)
            : leaf && leaf->node_type == AST_NODE_ARRAY ? array_literal(c, (AstArrayNode*)leaf, c->function->result_contract)
            : expression(c, result);
        return_value(c, returned, result); return null_value(c);
    }
    case AST_NODE_BREAK_STAM: case AST_NODE_CONTINUE_STAM:
        if (!c->control) mvp_reject(c->module, node, "loop transfer outside a loop");
        else {
            cleanup_to(c, node->node_type == AST_NODE_BREAK_STAM
                ? c->control->break_cleanup_base : c->control->cleanup_base, false);
            jump(c, node->node_type == AST_NODE_BREAK_STAM ? c->control->done : c->control->next);
        }
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
                !b->entry->is_mutable || mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR ||
                b->entry->view_base || !((AstDeclaratorNode*)mvp_binding_node(b))->declared_type ||
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

static bool sole_declaration_read(MvpCompiler* c, AstDeclaratorNode* declaration, AstNode* read) {
    AstIndex* index = &c->module->tp->ast_index;
    uint32_t count = 0, reads = 0;
    const AstNodeId* uses = ast_index_binding_uses(index,
        ast_index_find_binding_entry(index, declaration->entry), &count);
    read = ast_unwrap_primary_to_leaf(read);
    for (uint32_t i = 0; i < count; i++) {
        AstNode* use = index->nodes[uses[i]];
        if (use == declaration || use == declaration->id) continue;
        if (use != read) return false;
        reads++;
    }
    return reads == 1;
}

static AstNode* append_string_recipe(MvpCompiler* c, AstNode* first) {
    if (first->node_type != AST_NODE_VAR_STAM && first->node_type != AST_NODE_LET_STAM) return NULL;
    AstNode* declaration = ((AstVarDeclNode*)first)->declarations;
    AstNode* next = first->next;
    if (!declaration || declaration->next || declaration->node_type != AST_NODE_VARIABLE_DECLARATOR || !next) return NULL;
    AstDeclaratorNode* admitted = NULL;
    if (next->node_type == AST_NODE_LET_STAM) {
        admitted = (AstDeclaratorNode*)((AstVarDeclNode*)next)->declarations;
        if (!admitted || admitted->node_type != AST_NODE_VARIABLE_DECLARATOR || admitted->next ||
                !admitted->entry || admitted->entry->admission_may_fail ||
                admitted->declared_type != &TYPE_STRING) return NULL;
        next = next->next;
    }
    if (!next || next->node_type != AST_NODE_ASSIGN_STAM) return NULL;
    AstDeclaratorNode* d = (AstDeclaratorNode*)declaration;
    AstAssignStamNode* assignment = (AstAssignStamNode*)next;
    MvpBinding* temporary = mvp_binding(c->module, d->entry);
    MvpBinding* owner = mvp_binding(c->module, assignment->target_entry);
    if (!temporary || !owner || temporary == owner || temporary->contract != &TYPE_STRING ||
            owner->contract != &TYPE_STRING || !string_recipe_size(c->module, d->init, NULL)) return NULL;
    AstNode* joined_value = admitted ? admitted->init : assignment->value;
    if (admitted && (mvp_local_binding(c->module, assignment->value) != mvp_binding(c->module, admitted->entry) ||
            !sole_declaration_read(c, admitted, assignment->value) ||
            !mvp_admission_proven(c->module, mvp_value_contract(c->module, joined_value), &TYPE_STRING))) return NULL;
    AstNode* leaves[LAMBDA_STRING_CONCAT_MAX_PARTS]; int size = 0;
    if (!ast_collect_join_leaves(joined_value, leaves, &size) || size != 2 ||
            mvp_local_binding(c->module, leaves[0]) != owner || mvp_local_binding(c->module, leaves[1]) != temporary)
        return NULL;
    if (!sole_declaration_read(c, d, leaves[1])) return NULL;
    // the adjacent, single-use constructor contributes its pieces directly;
    // prefix reading is effect-free and constructor arguments retain their order.
    // a proven, single-use admission temporary does not add an observation.
    MirValue joined = string_concat(c, joined_value, owner, temporary, d->init);
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
    if (!sole_declaration_read(c, source, ((AstUnaryNode*)init)->operand)) return NULL;
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

static MirValue statements(MvpCompiler* c, AstNode* node, bool discard, bool tail,
        bool item_position, MvpSequenceMode mode, AstNode* declarations, MIR_reg_t output_destination) {
    MvpScope scope = {c->scope, {}}; c->scope = &scope;
    int shared_count = c->shared_bindings ? c->shared_bindings->length : 0;
    MvpCleanup* cleanup_base = c->cleanups;
    scope.failure.cleanup_base = cleanup_base; scope.failure.unwinds = true;
    MvpCleanup cleanup = {cleanup_base, 0};
    if (mode != MVP_LIST_RESULTS && c->function->ast && c->function->ast->analysis &&
            c->function->ast->analysis->owns_task_scopes) {
        cleanup.record = em_call_0(&c->em, "lambda_task_scope_enter", MIR_T_P, true);
        c->cleanups = &cleanup;
    }
    MirValue result = null_value(c);
    AstNode* last = NULL;
    bool procedural = !output_destination && mode == MVP_BLOCK_RESULTS && c->function->ast &&
        lambda_type_func_is_proc(c->function->ast->type);
    unsigned values = 0;
    if (!discard) {
        int value_count = 0, decl_count = 0, statement_count = 0;
        AstNode* terminal = NULL;
        last = ast_block_last_value(node, procedural, &value_count, &decl_count, &statement_count, &terminal);
        values = (unsigned)value_count;
        // an explicit exit owns the result; earlier calls keep discard demand.
        if (procedural && terminal && terminal->node_type == AST_NODE_RETURN_STAM) { last = NULL; values = 0; }
    }
    for (AstNode* d = declarations; d; d = d->next) expression(c, d, true);
    MIR_reg_t collected = !output_destination && !procedural && values > 1
        ? em_call_0(&c->em, "list", MIR_T_P, true) : 0;
    if (collected) value(c, collected, LMD_TYPE_ARRAY, VALUE_REP_ITEM, &TYPE_LIST);
    for (; node && !c->module->failed; node = node->next) {
        if (output_destination) {
            if (is_declaration_node(node)) expression(c, node, true);
            else {
                MirValue status = output_expression(c, node, output_destination);
                check_item_result(c, status.reg, c->output_failure);
            }
            continue;
        }
        if (AstNode* discharged = discharged_declaration(c, node)) { node = discharged; continue; }
        if (AstNode* appended = append_string_recipe(c, node)) { node = appended; continue; }
        if (AstNode* moved = owner_permutation(c, node)) { node = moved; continue; }
        if (!collected) {
            if (AstNode* dispatched = exclusive_conditions(c, node, last)) { node = dispatched; continue; }
            if (AstNode* dispatched = disjoint_conditions(c, node, last)) { node = dispatched; continue; }
        }
        bool contributes = !discard && !is_declaration_node(node) &&
            (procedural || !ast_block_item_is_statement(node, false));
        MirValue current = expression(c, node, collected ? !contributes : node != last,
            !collected && tail && node == last && !node->next,
            collected ? contributes : item_position && node == last);
        if (collected && contributes) {
            // sequence collection keeps null/text boundaries; only script
            // content normalizes them (S2.5.1v2, S2.6.1v2, D3.3.1).
            current = convert(c, capture_projection(c, current, node), VALUE_REP_ITEM);
            em_call_void_2(&c->em, mode == MVP_CONTENT_RESULTS ? "list_push_spread" : "array_push_spread",
                MIR_T_P, reg(c, collected), MIR_T_I64, reg(c, current.reg), true);
        } else if (node == last) result = current;
    }
    if (collected) result = value(c, em_call_1(&c->em, item_position ? "list_end_item" : "list_end",
        MIR_T_I64, MIR_T_P, reg(c, collected), true), LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    else if (!discard && !procedural && !values && item_position)
        result = value(c, constant(c, ITEM_NULL_SPREADABLE), LMD_TYPE_NULL, VALUE_REP_ITEM, &TYPE_NULL);
    cleanup_to(c, cleanup_base, false);
    c->cleanups = cleanup_base;
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
    if (c->shared_bindings) c->shared_bindings->length = shared_count;
    return result;
}

static void epilogue(MvpCompiler* c) {
    MirFrameState* f = &c->em.frame;
    if (c->vargs_roots) {
        MIR_reg_t previous = em_load_at(&c->em, c->vargs_roots, 0, MIR_T_I64, "previous_vargs");
        em_store_at(&c->em, f->runtime, MIR_EVAL_CONTEXT_VARGS_OFFSET, MIR_T_I64, previous);
    }
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
// collecting/reentrant calls can invalidate the entry guards (D5.4.4).
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
        if (mvp_node_owner(c->module, i) != c->function->id) continue;
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
        } else if (n->node_type == AST_NODE_MEMBER_ASSIGN_STAM) {
            // member stores have no storage/effect proof in this array region;
            // their checked setter can allocate or invoke admission callbacks.
            return false;
        } else if (n->node_type == AST_NODE_CALL_EXPR) {
            AstCallNode* call = (AstCallNode*)n;
            MvpFunction* f = mvp_function(m, ast_direct_call_function(call));
            if (f) {
                if (f != c->dense_callee && (!inline_eligible(c, f) || f->abi.effects.may_gc) &&
                        (!c->dense_calls || !dense_call_eligible(c, call))) return false;
            } else {
                AstNode* target = ast_unwrap_primary_to_leaf(call->function);
                if (!target || target->node_type != AST_NODE_SYS_FUNC) return false;
                SysFuncInfo* info = ((AstSysFuncNode*)target)->fn_info;
                TypeId arg = mvp_value_contract(m, mvp_call_argument(call, 0), 0, true)->type_id;
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
            AstForNode* query = (AstForNode*)n;
            AstLoopNode* loop = (AstLoopNode*)query->loop;
            // only a native range statement has the noncollecting shape this
            // proof inspects; a general source is not a binary range node.
            // a value-producing for builds a stream even with native bounds;
            // its collecting shape cannot enter this closed alternative.
            if (!ast_for_discards_result(n) || loop->key_only || loop->next || loop->index_entry || query->where || query->group ||
                    query->order || query->let_clause || query->limit || query->offset ||
                    !mvp_native_range_source(m, loop->as)) return false;
            AstBinaryNode* range = (AstBinaryNode*)ast_unwrap_primary_to_leaf(loop->as);
            // finite endpoints cannot enter the collecting range-error path.
            if (!mvp_int_range(m, range->left).finite || !mvp_int_range(m, range->right).finite) return false;
        } else if (n->node_type == AST_NODE_ARRAY) {
            // a typed primitive literal may allocate while all input owners
            // stay rooted; its element proofs still precede publication.
            if (!allocating || !mvp_array_element(mvp_value_contract(m, n, 0, true))) return false;
        }
        if (allocating && (n->node_type == AST_NODE_MAP ||
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
    for (unsigned i = 0; i < calls->count; i++) {
        calls->functions[i]->abi.effects.may_return_error = guarded ? false : calls->errors[i];
        calls->functions[i]->abi.effects.may_gc = calls->collecting[i] && !(guarded && calls->noncollecting[i]);
    }
}

static bool dense_add_call(MvpDenseCalls* calls, MvpFunction* f, bool noncollecting = false) {
    for (unsigned i = 0; i < calls->count; i++) if (calls->functions[i] == f) return true;
    if (calls->count == 16) return false;
    calls->functions[calls->count] = f;
    calls->collecting[calls->count] = f->abi.effects.may_gc;
    calls->noncollecting[calls->count] = noncollecting;
    calls->errors[calls->count++] = f->abi.effects.may_return_error;
    f->abi.effects.may_return_error = false;
    if (noncollecting) f->abi.effects.may_gc = false;
    return true;
}

static bool dense_scalar_calls(MvpCompiler* c, AstNodeId root, MvpDenseCalls* calls) {
    AstIndex* index = &c->module->tp->ast_index;
    uint32_t cost = c->inline_cost;
    for (AstNodeId i = root; i < ast_index_subtree_end(index, root); i++) {
        if (index->nodes[i]->node_type != AST_NODE_CALL_EXPR) continue;
        AstCallNode* call = (AstCallNode*)index->nodes[i];
        if (!mvp_integer_leaf_result(c->module, call, 0, true)) continue;
        MvpFunction* f = mvp_function(c->module, ast_direct_call_function(call));
        cost += f->inline_cost;
        // the proof reserves all leaf expansions together, so no later call
        // falls back to a real collecting edge after owners are unrooted.
        if (cost > 256 || !dense_call_eligible(c, call) || !dense_add_call(calls, f, true)) return false;
    }
    return true;
}

static bool dense_bindings_eligible(MvpCompiler* c, MvpIndexLoopPlan* indices) {
    LambdaMvpModule* m = c->module;
    bool has_array = false;
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (b->owner != c->function->id) continue;
        if (!b->entry || !mvp_binding_node(b) || (mvp_binding_node(b)->node_type != AST_NODE_PARAM &&
                mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR)) continue;
        TypeParam* parameter = mvp_binding_node(b)->node_type == AST_NODE_PARAM
            ? lambda_type_param(mvp_binding_node(b)->type) : NULL;
        // the dense array entry consumes homes without retaining them;
        // scalar replacement needs the ordinary publication path.
        if (parameter && parameter->is_var_param && !mvp_array_element(b->contract)) return false;
        if (mvp_array_element(b->contract)) {
            if (mvp_binding_node(b)->node_type != AST_NODE_PARAM || (!b->array_extent && b != indices->array)) return false;
            has_array = true; b->array_written = false;
        } else if (b->rep != VALUE_REP_F64 && b->rep != VALUE_REP_INT_LANE && b->rep != VALUE_REP_I64) return false;
    }
    return has_array;
}

static bool dense_body_eligible(MvpCompiler* c, MvpIndexLoopPlan* indices, MvpDenseCalls* calls, bool allocating) {
    if (mvp_parameter_receiver(c->function)) return false;
    LambdaMvpModule* m = c->module;
    AstIndex* index = &m->tp->ast_index;
    MvpIndexLoopPlan dynamic = {};
    if (mvp_index_function_plan(m, c->function, &dynamic)) *indices = dynamic;
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
            if (function_has_arrays(m, target))
                eligible = !(target == c->function && indices->array) &&
                    dense_call_eligible(c, call) && dense_add_call(calls, target);
        }
        if (eligible) eligible = dense_scalar_calls(c, root, calls);
        if (eligible) narrow_dense_locals(c, root, true);
    }
    eligible = eligible && dense_bindings_eligible(c, indices) &&
        dense_region_eligible(c, root, indices, false, allocating);
    if (calls) dense_call_effects(calls, false);
    c->dense_calls = saved;
    return eligible;
}

static void narrow_dense_parameters(MvpCompiler* c) {
    MvpFunction* function = c->function;
    if (mvp_parameter_receiver(function) || function->ast->captures || function->method_owner ||
            function->owns_open_scopes || function->inline_cost > 512) return;
    for (AstNamedNode* param = function->ast->param; param; param = (AstNamedNode*)param->next) {
        MvpBinding* binding = mvp_binding(c->module, param->entry);
        Type* element = binding->candidates[MVP_CANDIDATE_ARRAY_ELEMENT];
        if (!element || !binding->array_accessed || binding->rep != VALUE_REP_ITEM ||
                !mvp_open_value_contract(binding->contract) || binding->capture_home ||
                binding->entry->lambda_shared_local) continue;
        // this temporary hypothesis enters the existing whole-body proof.
        // Keep Item transport and original admission until physical guards.
        binding->contract = mvp_private_array_domain(c->module, element);
    }
}

static bool dense_call_eligible(MvpCompiler* c, AstCallNode* call) {
    return dense_call_arguments(c, mvp_function(c->module, ast_direct_call_function(call)), call);
}

static bool dense_call_arguments(MvpCompiler* c, MvpFunction* f, AstCallNode* call) {
    LambdaMvpModule* m = c->module;
    if (!f || mvp_parameter_receiver(f)) return false;
    if (!function_has_arrays(m, f) && mvp_integer_leaf_result(m, call, 0, true))
        return f != c->function && inline_eligible(c, f, false, true);
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
    int count = mvp_call_argument_count(call);
    for (int i = 0; i < count; i++, param = (AstNamedNode*)param->next) {
        AstNode* a = mvp_call_argument(call, i);
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
            for (int j = 0; j < count; j++, other = (AstNamedNode*)other->next)
                if (lambda_type_param(other->type)->is_var_param &&
                        mvp_local_binding(m, mvp_call_argument(call, j)) == owner) return false;
        }
    }
    return true;
}

static void emit_closed_region(MvpCompiler* c, AstNode* node, bool arrays) {
    uint32_t gc_calls = c->em.frame.may_gc_call_count, inline_cost = c->inline_cost;
    int call_sites = c->em.frame.gc_call_site_count;
    bool dense_arrays = c->dense_arrays;
    c->dense_arrays = arrays;
    if (node) expression(c, node, true);
    else function_body(c);
    c->dense_arrays = dense_arrays;
    // alternative bodies have independent expansion budgets; emitting the hot
    // version must not deprive its checked sibling of ordinary leaf inlining.
    c->inline_cost = inline_cost;
    if (c->em.frame.may_gc_call_count != gc_calls) {
        const char* helper = "<indirect>";
        for (int i = call_sites; i < c->em.frame.gc_call_site_count; i++) {
            MirGcCallSite* site = &c->em.frame.gc_call_sites[i];
            if (site->effect == JIT_EFFECT_NO_GC) continue;
            MIR_insn_t insn = site->insn;
            if (MIR_insn_nops(c->em.ctx, insn) > 1 && insn->ops[1].mode == MIR_OP_REF)
                helper = MIR_item_name(c->em.ctx, insn->ops[1].u.ref);
            break;
        }
        char reason[192];
        snprintf(reason, sizeof(reason), "closed numeric region emitted a collecting call: %s", helper);
        mvp_reject(c->module, node ? node : c->function->ast, reason);
    }
}

static MIR_reg_t dense_owner_guard(MvpCompiler* c, MvpBinding* owner,
        int64_t extent, bool written, MIR_label_t checked) {
    ArrayNumElemType lane;
    lambda_array_num_elem_type_for_contract(mvp_array_element(owner->contract), &lane);
    em_array_num_elem_guard(&c->em, owner->reg, lane, checked);
    MIR_reg_t length = em_load_at(&c->em, owner->reg, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "dense_length");
    if (extent) branch(c, MIR_BLT, checked, reg(c, length), imm(c, extent));
    if (written) em_container_cow_guard(&c->em, owner->reg, checked, true);
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
                ast_index_node_descends(index, ast_index_find(index, mvp_binding_node(b)), root)) return NULL;
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
    narrow_dense_locals(c, root);
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
    if (!callee || mvp_parameter_receiver(callee) || callee == c->function || callee->inline_cost > 512) return false;
    AstNamedNode* param = callee->ast->param;
    for (int i = 0; i < mvp_call_argument_count(call); i++, param = (AstNamedNode*)param->next) {
        AstNode* arg = mvp_call_argument(call, i);
        if (!mvp_pure_key(arg) || !mvp_boundary_proven(mvp_value_contract(m, arg), mvp_contract(param))) return false;
        if (!mvp_array_element(mvp_contract(param))) continue;
        MvpBinding* owner = mvp_local_binding(m, arg);
        TypeParam* type = lambda_type_param(param->type);
        if (!owner || owner->owner != c->function->id || !mvp_array_element(owner->contract) ||
                ast_index_node_descends(index, ast_index_find(index, mvp_binding_node(owner)), root)) return false;
        if (!type->is_var_param) {
            if (!mvp_parameter_borrowable(m, param)) return false;
            AstNamedNode* other = callee->ast->param;
            for (int j = 0; j < mvp_call_argument_count(call); j++, other = (AstNamedNode*)other->next)
                if (lambda_type_param(other->type)->is_var_param &&
                        mvp_local_binding(m, mvp_call_argument(call, j)) == owner) return false;
        }
        plan->arrays[plan->count++] = owner;
    }
    if (!plan->count) return false;
    MvpFunction* caller = c->function;
    c->function = callee;
    narrow_dense_locals(c, ast_index_find(index, callee->ast->body), true);
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

static Type* const primitive_iteration_domains[] = {NULL, &TYPE_FLOAT, &TYPE_INT, &TYPE_BOOL};

struct MvpIterationPlan {
    AstNodeId root;
    size_t last_access;
    MvpBinding* arrays[16];
    AstNode* keys[64];
    MvpBinding* key_arrays[64];
    MvpBinding* hoisted[16];
    MvpBinding* scalar_outcomes[16];
    MIR_reg_t outcome_items[16];
    unsigned array_count, key_count, hoisted_count, access_count;
    unsigned outcome_count;
    unsigned dynamic_arrays;
    MvpDenseCalls calls;
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
        return !ast_index_node_descends(index, ast_index_find(index, mvp_binding_node(owner)), plan->root) &&
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
    AstNodeId declaration = ast_index_find(index, mvp_binding_node(b));
    if (!ast_index_node_descends(index, declaration, plan->root)) return true;
    for (unsigned i = 0; i < plan->hoisted_count; i++) if (plan->hoisted[i] == b) return true;
    if (mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR || plan->hoisted_count == 16) return false;
    AstNodeId parent = ast_index_parent_id(index, declaration);
    if (parent < index->count && (index->nodes[parent]->node_type == AST_NODE_VAR_STAM ||
            index->nodes[parent]->node_type == AST_NODE_LET_STAM)) parent = ast_index_parent_id(index, parent);
    // only unconditional pure declarations can be evaluated ahead of the guards.
    if (parent != plan->root || !iteration_key(c, ((AstDeclaratorNode*)mvp_binding_node(b))->init, plan, depth + 1)) return false;
    plan->hoisted[plan->hoisted_count++] = b;
    return true;
}

static void narrow_closed_binding(MvpCompiler* c, MvpBinding* binding, Type* contract) {
    ValueRep rep = mvp_rep(contract);
    // the closed proof checks every initializer/write before this alternative
    // runs. A new carrier can require a new MIR register class (D2.3.5).
    if (binding->reg && em_mir_type_for_rep(binding->rep) != em_mir_type_for_rep(rep))
        binding->reg = em_new_reg(&c->em, "closed_binding", em_mir_type_for_rep(rep));
    binding->contract = contract; binding->rep = rep;
}

static void narrow_dense_locals(MvpCompiler* c, AstNodeId root, bool outcomes) {
    LambdaMvpModule* m = c->module;
    AstIndex* index = &m->tp->ast_index;
    AstFunctionId owner = mvp_node_owner(m, root);
    for (AstNodeId i = root; i < ast_index_subtree_end(index, root); i++) {
        AstNode* n = index->nodes[i];
        if (n->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        MvpBinding* b = mvp_binding(m, ((AstDeclaratorNode*)n)->entry);
        if (!b || b->owner != owner) continue;
        if (outcomes && b->entry->admission_may_fail &&
                (b->admission_contract == &TYPE_INT || b->admission_contract == &TYPE_BOOL ||
                 b->admission_contract == &TYPE_FLOAT)) {
            // the closed-region proof checks every initializer and write
            // against this hypothesis before emission; guarded regions
            // contain no collecting calls.
            narrow_closed_binding(c, b, b->admission_contract);
        }
        if (((AstDeclaratorNode*)n)->declared_type) continue;
        bool optional = false;
        Type* base = b ? lambda_type_nullable_lane_base(b->contract, &optional) : NULL;
        if (optional && (base == &TYPE_FLOAT || base == &TYPE_INT || base == &TYPE_BOOL))
            narrow_closed_binding(c, b, base);
    }
    for (AstNodeId i = root; i < ast_index_subtree_end(index, root); i++) {
        AstNode* node = index->nodes[i];
        if (node->node_type != AST_NODE_VARIABLE_DECLARATOR) continue;
        AstDeclaratorNode* declaration = (AstDeclaratorNode*)node;
        MvpBinding* binding = mvp_binding(m, declaration->entry);
        if (!binding || binding->owner != owner || declaration->declared_type ||
                binding->entry->lambda_shared_local) continue;
        Type* observed = mvp_inferred_scalar_contract(mvp_value_contract(m, declaration->init, 0, true));
        if (observed != &TYPE_BOOL && observed != &TYPE_INT && observed != &TYPE_FLOAT) continue;
        narrow_closed_binding(c, binding, observed);
    }
}

static bool array_iteration_plan(MvpCompiler* c, AstNode* body, MvpIterationPlan* plan,
        bool strings = false, bool scalar_versions = false, Type* dynamic_element = NULL) {
    // a single-store loop already hoists its payload and checks each store; a second version adds no shared checks.
    if (c->dense_arrays || c->loop_array || !body ||
            (body->node_type != AST_NODE_CONTENT && body->node_type != AST_NODE_BINARY)) return false;
    LambdaMvpModule* m = c->module;
    AstIndex* index = &m->tp->ast_index;
    plan->root = ast_index_find(index, body);
    AstNodeId end = ast_index_subtree_end(index, plan->root);
    if (end - plan->root > 512) return false;
    if (scalar_versions) for (AstNodeId i = plan->root; i < end; i++) {
        MvpBinding* b = mvp_local_binding(m, index->nodes[i]);
        if (!b || b->owner != c->function->id || b->rep != VALUE_REP_ITEM ||
                !mvp_binding_node(b) || b->entry->lambda_shared_local || b->capture_home || b->var_home ||
                b->module_ref || b->module_slot || b->state_name ||
                mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR ||
                ast_index_node_descends(index, ast_index_find(index, mvp_binding_node(b)), plan->root)) continue;
        Type* domain = b->admission_contract == &TYPE_FLOAT ? &TYPE_FLOAT : NULL;
        AstDeclaratorNode* declaration = (AstDeclaratorNode*)mvp_binding_node(b);
        if (!domain && !declaration->declared_type) {
            bool optional = false;
            Type* seed = lambda_type_nullable_lane_base(lambda_type_remove_error(m->tp->pool,
                mvp_value_contract(m, declaration->init, 0, true)), &optional);
            if (mvp_inferred_scalar_contract(seed) == &TYPE_INT) domain = &TYPE_INT;
        }
        if (!domain) continue;
        if (plan->outcome_count == 16) return false;
        unsigned slot = plan->outcome_count++;
        plan->scalar_outcomes[slot] = b; plan->outcome_items[slot] = b->reg;
        // the guard below discharges an existing outcome before the closed
        // body proves every subsequent write; failures keep the checked body.
        b->contract = domain; b->rep = mvp_rep(domain); b->range = {};
    }
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
        Type* success_element = b && b->rep == VALUE_REP_ITEM && lambda_type_has_proven_error(b->contract)
            ? mvp_array_element(lambda_type_remove_error(m->tp->pool, b->contract)) : NULL;
        bool boxed_array = success_element && (!dynamic_element || success_element == dynamic_element);
        bool dynamic = b && b->rep == VALUE_REP_ITEM && b->array_native &&
            (boxed_array || (dynamic_element && mvp_open_value_contract(b->contract)));
        // this is a guarded physical observation for one closed iteration,
        // never an admission or write contract on the untyped binding.
        // a boxed array/Error outcome needs the same physical entry guard as
        // an erased owner; its error arm must not suppress the native version.
        if (dynamic) b->contract = mvp_private_array_domain(m, boxed_array ? success_element : dynamic_element);
        bool character = strings && b && b->contract->type_id == LMD_TYPE_STRING && fused_character_index(c, n);
        if (!character && (!b || !mvp_array_element(b->contract) || !b->array_native)) return false;
        if (character) {
            if (ast_index_node_descends(index, ast_index_find(index, mvp_binding_node(b)), plan->root)) return false;
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
            if (dynamic) plan->dynamic_arrays |= 1u << a;
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
    narrow_dense_locals(c, plan->root);
    MvpIndexLoopPlan unused = {};
    bool saved_calls = c->dense_calls;
    bool eligible = body->node_type != AST_NODE_CONTENT || dense_scalar_calls(c, plan->root, &plan->calls);
    if (plan->calls.count) {
        c->dense_calls = true;
        narrow_dense_locals(c, plan->root);
    }
    eligible = eligible && dense_region_eligible(c, plan->root, &unused, true);
    dense_call_effects(&plan->calls, false);
    c->dense_calls = saved_calls;
    return eligible;
}

static void emit_iteration_region(MvpCompiler* c, MvpIterationPlan* plan, AstNode* node, const MvpBinding* bindings) {
    bool saved = c->dense_calls;
    c->dense_calls |= plan->calls.count != 0;
    dense_call_effects(&plan->calls, true);
    emit_closed_region(c, node, true);
    for (unsigned i = 0; i < plan->outcome_count; i++) {
        MvpBinding* native = plan->scalar_outcomes[i];
        MvpBinding original = bindings[native - c->module->bindings];
        store_binding(c, &original, value(c, native->reg, native->contract->type_id, native->rep, native->contract));
    }
    dense_call_effects(&plan->calls, false);
    c->dense_calls = saved;
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
        if (!ast_index_node_descends(index, ast_index_find(index, mvp_binding_node(b)), plan->root)) {
            *slope = 0; return true;
        }
        // iteration_key already proved these declarations unconditional and pure.
        for (unsigned i = 0; i < plan->hoisted_count; i++) if (plan->hoisted[i] == b)
            return loop_key_slope(c, ((AstDeclaratorNode*)mvp_binding_node(b))->init, plan, counter, slope, depth + 1);
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

static void guard_iteration_owners(MvpCompiler* c, MvpIterationPlan* plan, MIR_label_t checked) {
    for (unsigned a = 0; a < plan->array_count; a++) {
        MvpBinding* b = plan->arrays[a];
        if (b->contract->type_id == LMD_TYPE_STRING)
            b->array_length = em_guard_ascii_string(&c->em, b->reg, checked);
        else if (plan->dynamic_arrays & (1u << a)) {
            em_guard_container(&c->em, b->reg, LMD_TYPE_ARRAY_NUM, checked);
            ArrayNumElemType lane;
            lambda_array_num_elem_type_for_contract(mvp_array_element(b->contract), &lane);
            em_array_num_elem_guard(&c->em, b->reg, lane, checked);
            if (b->array_written) em_container_cow_guard(&c->em, b->reg, checked, true);
        } else branch(c, MIR_BEQ, checked,
            reg(c, b->array_written ? b->array_writable : b->array_native), imm(c, 0));
    }
}

static bool strided_array_loop_version(MvpCompiler* c, AstLoopControlNode* loop,
        Type* dynamic_element, MIR_label_t* done) {
    if (c->dense_arrays || c->dense_callee || c->loop_array || loop->form != LOOP_FORM_WHILE) return false;
    LambdaMvpModule* m = c->module;
    AstNode* condition = ast_unwrap_primary_to_leaf(loop->cond);
    if (!condition || condition->node_type != AST_NODE_BINARY) return false;
    AstBinaryNode* test = (AstBinaryNode*)condition;
    if (test->op != OPERATOR_LT && test->op != OPERATOR_LE) return false;
    MvpBinding* counter = mvp_local_binding(m, test->left);
    if (!counter || !counter->range.finite || !mvp_int_range(m, test->right).finite ||
            !loop->body || loop->body->node_type != AST_NODE_CONTENT) return false;
    AstNode* last = ((AstListNode*)loop->body)->item;
    while (last && last->next) last = last->next;
    if (!last || last->node_type != AST_NODE_ASSIGN_STAM) return false;
    AstAssignStamNode* update = (AstAssignStamNode*)last;
    if (update->target_entry != counter->entry || !mvp_int_range(m, update->value).finite) return false;
    AstIndex* index = &m->tp->ast_index;
    AstNodeId root = ast_index_find(index, loop->body);
    for (AstNodeId i = root; i < ast_index_subtree_end(index, root); i++) {
        AstNode* n = index->nodes[i];
        if (n->node_type == AST_NODE_ASSIGN_STAM && n != last &&
                ((AstAssignStamNode*)n)->target_entry == counter->entry) return false;
        if (n->node_type == AST_NODE_CALL_EXPR) {
            AstFuncNode* fn = ast_direct_call_function((AstCallNode*)n);
            for (AstNamedNode* p = fn ? fn->param : NULL; p; p = (AstNamedNode*)p->next)
                if (lambda_type_param(p->type)->is_var_param) return false;
        }
    }
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) { mvp_reject(m, loop, "strided loop binding allocation failed"); return false; }
    MvpIterationPlan plan = {};
    int slope = 0;
    bool eligible = array_iteration_plan(c, loop->body, &plan, true, false, dynamic_element) &&
        plan.access_count >= (dynamic_element ? 1u : 2u) && (!dynamic_element || plan.dynamic_arrays) &&
        iteration_key(c, test->right, &plan) && loop_key_slope(c, test->right, &plan, counter, &slope) && slope == 0 &&
        iteration_key(c, update->value, &plan) && loop_key_slope(c, update->value, &plan, counter, &slope) && slope == 1;
    bool enclosing_endpoint = true;
    for (unsigned k = 0; eligible && k < plan.key_count; k++) {
        eligible = loop_key_slope(c, plan.keys[k], &plan, counter, &slope);
        if (slope && !mvp_same_integer_expression(m, plan.keys[k], test->left)) enclosing_endpoint = false;
    }
    MIR_label_t checked = NULL;
    if (eligible) {
        checked = label(c);
        if (!*done) *done = label(c);
        MIR_reg_t first = counter->reg, limit = expression(c, test->right).reg;
        branch(c, test->op == OPERATOR_LT ? MIR_BGE : MIR_BGT, *done, reg(c, first), reg(c, limit));
        MvpIntRange range = counter->range;
        counter->reg = constant(c, 0); counter->range = {true, 0, 0};
        MIR_reg_t step = expression(c, update->value).reg;
        counter->reg = first; counter->range = range;
        em_branch_int53_outside(&c->em, step, checked);
        branch(c, MIR_BLE, checked, reg(c, step), imm(c, 0));
        MIR_reg_t end = test->op == OPERATOR_LT ? operation(c, MIR_SUB, reg(c, limit), imm(c, 1)) : limit;
        MvpIntRange bound = mvp_int_range(m, test->right);
        int64_t strict = test->op == OPERATOR_LT ? 1 : 0;
        MIR_reg_t final = end;
        // an offset key can cross the owner at an unvisited endpoint. Keep
        // the exact last counter for those guards; direct counters can use
        // the enclosing interval without a division (S7.1.1v3).
        if (!enclosing_endpoint || !bound.finite || bound.lower - strict < range.lower || bound.upper - strict > range.upper) {
            MIR_reg_t distance = operation(c, MIR_SUB, reg(c, end), reg(c, first));
            MIR_reg_t remainder = operation(c, MIR_MOD, reg(c, distance), reg(c, step));
            final = operation(c, MIR_SUB, reg(c, end), reg(c, remainder));
        }
        guard_iteration_owners(c, &plan, checked);
        // affine keys stay between the checked endpoints even when a stride
        // does not divide the limit. Check both before effects (S7.1.1v3).
        MIR_reg_t endpoints[] = {first, final};
        for (unsigned e = 0; e < 2; e++) {
            counter->reg = endpoints[e];
            for (unsigned h = 0; h < plan.hoisted_count; h++) {
                MvpBinding* b = plan.hoisted[h];
                move(c, b->reg, reg(c, expression(c, ((AstDeclaratorNode*)mvp_binding_node(b))->init).reg));
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
        emit_iteration_region(c, &plan, loop, saved);
        c->string_bytes = saved_bytes;
        jump(c, *done);
    }
    restore_bindings(m, saved); mem_free(saved);
    if (checked) mark(c, checked);
    return eligible;
}

static MIR_label_t strided_array_loop(MvpCompiler* c, AstLoopControlNode* loop) {
    MIR_label_t done = NULL;
    for (Type* element : primitive_iteration_domains) {
        // one whole-loop guard amortizes erased-lane and COW checks over
        // every affine store, including single-access strides (D4.3.3).
        if (strided_array_loop_version(c, loop, element, &done) && !element) break;
    }
    return done;
}

static void guard_array_iteration(MvpCompiler* c, MvpIterationPlan* plan, MIR_label_t checked) {
    for (unsigned i = 0; i < plan->outcome_count; i++) {
        MIR_reg_t item = plan->outcome_items[i];
        MvpBinding* scalar = plan->scalar_outcomes[i];
        if (scalar->contract == &TYPE_FLOAT) {
            MIR_label_t admitted = label(c);
            branch(c, MIR_BNE, admitted, reg(c, operation(c, MIR_AND, reg(c, item), imm(c, ITEM_DBL_MASK))), imm(c, 0));
            branch(c, MIR_BNE, checked, reg(c, operation(c, MIR_URSH, reg(c, item), imm(c, 56))), imm(c, LMD_TYPE_FLOAT));
            mark(c, admitted);
        } else branch(c, MIR_BEQ, checked, reg(c, em_exact_item_type_test(&c->em, item, LMD_TYPE_INT)), imm(c, 0));
        MirValue lane = convert(c, value(c, item, scalar->contract->type_id, VALUE_REP_ITEM, scalar->contract), scalar->rep);
        scalar->reg = lane.reg;
    }
    guard_iteration_owners(c, plan, checked);
    for (unsigned h = 0; h < plan->hoisted_count; h++) {
        MvpBinding* b = plan->hoisted[h];
        MirValue key = expression(c, ((AstDeclaratorNode*)mvp_binding_node(b))->init);
        move(c, b->reg, reg(c, key.reg));
    }
    for (unsigned k = 0; k < plan->key_count; k++) {
        if (mvp_array_index_present(c->module, plan->key_arrays[k], plan->keys[k])) continue;
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
    AstNode* single = mvp_single_statement(body);
    MvpModularUpdatePlan modular = {};
    // a fused single store already shares its guards and reduces the modulus.
    // Do not put broader array versions ahead of this cheaper native path.
    if (single && single->node_type == AST_NODE_INDEX_ASSIGN_STAM &&
            modular_array_update_plan(c, (AstAssignStamNode*)single, &modular)) {
        expression(c, body, true);
        return;
    }
    LambdaMvpModule* m = c->module;
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) { mvp_reject(m, body, "iteration binding allocation failed"); return; }
    MIR_label_t done = NULL;
    for (Type* element : primitive_iteration_domains) {
        restore_bindings(m, saved);
        MvpIterationPlan plan = {};
        if (!array_iteration_plan(c, body, &plan, false, true, element) ||
                (element && !plan.dynamic_arrays)) continue;
        MIR_label_t checked = label(c);
        if (!done) done = label(c);
        guard_array_iteration(c, &plan, checked);
        // guards run before effects; a miss executes this iteration once through the checked body.
        emit_iteration_region(c, &plan, body, saved);
        jump(c, done);
        restore_bindings(m, saved);
        mark(c, checked);
        if (!element) break;
    }
    restore_bindings(m, saved); mem_free(saved);
    expression(c, body, true);
    if (done) mark(c, done);
}

static void function_body(MvpCompiler* c) {
    AstFuncNode* fn = c->function->ast;
    c->tail_entry = label(c); mark(c, c->tail_entry);
    // the parser can collapse a one-statement procedure body to its for;
    // retain the block's statement demand after that collapse (S2.5.3v3).
    bool discard = lambda_type_func_is_proc(fn->type) && ast_for_discards_result(fn->body);
    MirValue result = expression(c, fn->body, discard, true);
    // explicit terminal returns also end fn bodies; only a boundary failure
    // can still need the block's ordinary completion path (S12.1.2v3).
    if (!mvp_proc_has_final_return(fn) || result.semantic_type == LMD_TYPE_ANY)
        return_value(c, result, NULL);
}

static bool guarded_scalar_body(MvpCompiler* c) {
    MvpFunction* function = c->function;
    LambdaMvpModule* m = c->module;
    if (function->ast->captures || function->method_owner ||
            function->owns_open_scopes || function->ast->is_async || function->ast->is_generator ||
            function->inline_cost > 512) return false;
    Type* candidates[LAMBDA_MAX_FUNCTION_ARGS] = {};
    bool guarded = false;
    int position = 0;
    for (AstNamedNode* param = function->ast->param; param; param = (AstNamedNode*)param->next, position++) {
        TypeParam* type = lambda_type_param(param->type);
        // boxed callable entries have already received their arguments; only
        // optional/default and mutable receivers require their own protocol.
        if (type && (type->is_var_param || type->is_optional)) return false;
        candidates[position] = mvp_guarded_parameter_candidate(m, mvp_binding(m, param->entry));
        guarded |= candidates[position] != NULL;
    }
    if (!guarded) return false;
    MvpBinding* saved = snapshot_bindings(m);
    if (!saved) { mvp_reject(m, function->ast, "scalar version binding allocation failed"); return false; }
    position = 0;
    for (AstNamedNode* param = function->ast->param; param; param = (AstNamedNode*)param->next, position++) {
        if (!candidates[position]) continue;
        MvpBinding* binding = mvp_binding(m, param->entry);
        binding->contract = candidates[position]; binding->rep = mvp_rep(binding->contract);
    }
    mvp_refine_guarded_locals(m, function);
    for (uint32_t id = 0; id < m->binding_count; id++) {
        MvpBinding* binding = &m->bindings[id];
        if (binding->owner != function->id || !binding->reg || binding->rep == saved[id].rep) continue;
        // collecting alternatives need distinct registers even for Item ->
        // pointer/int: the old precise-root classification cannot be reused.
        binding->reg = em_new_reg(&c->em, "scalar_version", em_mir_type_for_rep(binding->rep));
        value(c, binding->reg, binding->contract->type_id, binding->rep, binding->contract);
    }
    MIR_label_t checked = label(c);
    position = 0;
    for (AstNamedNode* param = function->ast->param; param; param = (AstNamedNode*)param->next, position++) {
        if (!candidates[position]) continue;
        MvpBinding* binding = mvp_binding(m, param->entry);
        AstBindingId id = (AstBindingId)(binding - m->bindings);
        branch(c, MIR_BEQ, checked,
            reg(c, em_exact_item_type_test(&c->em, saved[id].reg, candidates[position]->type_id)), imm(c, 0));
    }
    position = 0;
    for (AstNamedNode* param = function->ast->param; param; param = (AstNamedNode*)param->next, position++) {
        if (!candidates[position]) continue;
        MvpBinding* binding = mvp_binding(m, param->entry);
        AstBindingId id = (AstBindingId)(binding - m->bindings);
        MirValue incoming = convert(c, value(c, saved[id].reg, candidates[position]->type_id,
            VALUE_REP_ITEM, candidates[position]), binding->rep);
        move(c, binding->reg, reg(c, incoming.reg), incoming.mir_type);
    }
    uint32_t inline_cost = c->inline_cost;
    // guards precede all body effects; a miss enters the original body once.
    // Keep its ABI/effects, including checked self edges and precise GC roots.
    if (!c->inlining || c->inline_cost + function->inline_cost <= 256)
        guarded_integer_body(c, true);
    function_body(c);
    c->inline_cost = inline_cost;
    restore_bindings(m, saved); mem_free(saved);
    mark(c, checked);
    return true;
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

static void receive_binder_site(MvpCompiler* c, AstNamedNode* parameter, MIR_reg_t input) {
    if (!ast_parameter_is_binder_site(parameter)) return;
    MvpBinding* binding = mvp_binding(c->module, parameter->entry);
    TypeParam* type = lambda_type_param(parameter->type);
    MIR_label_t missing = label(c), ready = label(c);
    branch(c, MIR_BEQ, missing, reg(c, input), imm(c, ITEM_MISSING_ARGUMENT));
    MirValue admitted = admit(c, value(c, input, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY),
        type->binder ? (Type*)type->binder : binding->admission_contract);
    admitted = own_binding_scalar(c, binding, convert(c, admitted, binding->rep));
    move(c, binding->reg, reg(c, admitted.reg), admitted.mir_type);
    jump(c, ready); mark(c, missing);
    move(c, binding->reg, imm(c, ITEM_MISSING_ARGUMENT)); mark(c, ready);
}

static MirValue admit_default_site(MvpCompiler* c, MirValue value, Type* contract) {
    int count = mvp_function_binder_count(c->function);
    MIR_reg_t selected = c->binder_env;
    MIR_reg_t scratch = argument_span(c, count);
    // a fired default checks its written bound and prior references without
    // publishing any binder selections into this invocation (TG18).
    for (int i = 0; i < count; i++) {
        MIR_reg_t slot = em_load_at(&c->em, selected, i * sizeof(Type*), MIR_T_I64, "default_type");
        em_store_at(&c->em, scratch, i * sizeof(Type*), MIR_T_I64, slot);
    }
    c->binder_env = scratch;
    MirValue admitted = admit(c, value, contract);
    c->binder_env = selected;
    release_argument_span(c, scratch, count);
    return admitted;
}

static void receive_parameter(MvpCompiler* c, MvpBinding* binding, MIR_reg_t input,
        int position, Type* incoming_contract) {
    ValueRep rep = c->function->abi.params[position].canonical_rep;
    // a native entry cannot carry the failed-store Error added to a var's
    // effective body domain; box its actual incoming success lane first.
    Type* entry_contract = incoming_contract ? incoming_contract : rep == VALUE_REP_ITEM ? &TYPE_ANY :
        lambda_type_remove_error(c->module->tp->pool, binding->contract);
    MirValue incoming = value(c, input, entry_contract->type_id, rep, entry_contract);
    TypeParam* parameter = lambda_type_param(mvp_binding_node(binding)->type);
    bool binder_site = c->binder_env && ast_parameter_is_binder_site((AstNamedNode*)mvp_binding_node(binding));
    if (binder_site)
        incoming = value(c, binding->reg, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    if (parameter && parameter->is_optional) {
        MIR_reg_t selected = em_new_reg(&c->em, "default_argument", MIR_T_I64);
        MIR_label_t provided = label(c);
        move(c, selected, reg(c, incoming.reg));
        branch(c, MIR_BNE, provided, reg(c, input), imm(c, ITEM_MISSING_ARGUMENT));
        MirValue fallback = convert(c, expression(c, parameter->default_value), VALUE_REP_ITEM);
        if (binder_site) fallback = admit_default_site(c, fallback,
            parameter->binder ? (Type*)parameter->binder : binding->admission_contract);
        move(c, selected, reg(c, fallback.reg)); mark(c, provided);
        incoming = value(c, selected, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
    }
    bool dependent = lambda_type_contract_has_binder(binding->admission_contract, true);
    if (!binder_site && (dependent || c->function->boxed_entry || (parameter && parameter->is_optional)))
        incoming = admit(c, incoming, dependent ? binding->admission_contract : binding->contract,
            NULL, parameter_boundary_site(c, (AstNamedNode*)mvp_binding_node(binding)));
    if (c->function->boxed_entry && !binding->scalar_snapshot &&
            ast_parameter_needs_snapshot(binding->entry)) {
        // T0/dynamic callers do not perform the native direct-call capture;
        // a mutated plain parameter still starts as a snapshot (S9.1.2/D8.3.3).
        incoming = convert(c, incoming, VALUE_REP_ITEM);
        incoming.reg = capture_item(c, incoming.reg, incoming.semantic_contract);
    }
    incoming = convert(c, incoming, binding->rep);
    // unchanged boxed parameters acquire one destination-owned cold payload;
    // their reads can reuse it without weakening container snapshot semantics.
    if (dependent || binding->stable_scalar_home) incoming = own_binding_scalar(c, binding, incoming);
    move(c, binding->reg, reg(c, incoming.reg), incoming.mir_type);
}

bool mvp_emit_function(LambdaMvpModule* m, MvpFunction* fn) {
    MvpCompiler c = {}; c.module = m; c.function = fn;
    MvpBinding* captured_state = NULL;
    if (fn->ast && (fn->ast->captures || fn->method_owner || fn->view)) {
        captured_state = snapshot_bindings(m);
        if (!captured_state) return mvp_reject(m, fn->ast, "capture binding allocation failed");
        if (fn->handler) {
            AstFunctionId view_owner = mvp_node_owner(m, ast_index_find(&m->tp->ast_index, fn->view));
            // handlers re-evaluate the view's lexical declarations for their
            // own model; they cannot reuse another activation's MIR registers.
            for (uint32_t i = 0; i < m->binding_count; i++) if (m->bindings[i].owner == view_owner) {
                m->bindings[i].owner = fn->id;
                m->bindings[i].capture_binding = true;
            }
        }
        FOR_EACH_MAP_FIELD(fn->method_owner, field) {
            MvpBinding* binding = mvp_binding(m, field->binding);
            if (!binding) continue;
            MvpBinding local = {};
            local.entry = binding->entry;
            local.contract = field->is_optional
                ? lambda_type_nullable_normalized(m->tp->pool, field->type) : field->type;
            local.admission_contract = field->type;
            local.rep = mvp_rep(local.contract); local.owner = fn->id;
            local.capture_binding = true;
            *binding = local;
        }
        for (FnCapture* cap = fn->ast->captures; cap; cap = cap->next) {
            if (mvp_static_type_capture(cap) || mvp_method_field(fn, cap->entry) ||
                    (cap->entry && cap->entry->is_binder)) continue;
            MvpBinding* binding = mvp_binding(m, cap->entry);
            // an activation receives the snapshot's domain, never the parent's
            // MIR registers, borrow cells or writable storage witnesses.
            MvpBinding local = {};
            local.entry = binding->entry; local.contract = binding->contract;
            local.admission_contract = binding->admission_contract;
            local.rep = binding->rep; local.owner = fn->id;
            // access facts belong to the captured definition; only registers
            // and ownership witnesses are fresh in this activation.
            local.array_accessed = binding->array_accessed;
            local.record_accessed = binding->record_accessed;
            local.record_written = binding->record_written;
            local.capture_binding = true;
            *binding = local;
        }
    }
    // construct caller witnesses once for owners repeatedly indexed through inlined accessors.
    AstIndex* index = &m->tp->ast_index;
    for (AstNodeId id = 0; id < index->count; id++) {
        if (mvp_node_owner(m, id) != fn->id || index->nodes[id]->node_type != AST_NODE_CALL_EXPR) continue;
        AstCallNode* call = (AstCallNode*)index->nodes[id];
        MvpFunction* callee = mvp_function(m, ast_direct_call_function(call));
        if (!callee || !inline_eligible(&c, callee, true)) continue;
        AstNamedNode* param = callee->ast->param;
        for (int i = 0; i < mvp_call_argument_count(call); i++, param = (AstNamedNode*)param->next) {
            MvpBinding* source = mvp_local_binding(m, mvp_call_argument(call, i)), *target = mvp_binding(m, param->entry);
            if (source && source->owner == fn->id && target && target->array_accessed && mvp_array_element(source->contract))
                source->array_accessed = true;
        }
    }
    c.em.ctx = m->ctx; c.em.import_cache = m->imports; c.em.call_owner = &c;
    c.em.label_counter = m->label_counter;
    c.em.root_call_value = root_value; c.em.convert_rep = convert; c.em.lower_value = mvp_lower_value;
    c.em.after_may_gc_call = refresh_shared_bindings;
    c.em.lookup_import_metadata = jit_import_get_metadata;
    // diagnostic call counters use the shared emitter and add no ordinary MIR.
    c.em.profile_call_id = lambda_exec_profile_enabled() ? lambda_exec_profile_register_call : NULL;
    const char* profile_callers = getenv("LAMBDA_EXEC_PROFILE_CALLERS");
    c.em.profile_callers = c.em.profile_call_id && profile_callers && strcmp(profile_callers, "1") == 0;
    c.em.name_pool = m->tp->name_pool; c.em.const_list = m->tp->const_list; c.em.consts_bss = m->consts;
    c.em.helper_results_skip_rehome = true;
    int hidden = fn->ast && (fn->ast->captures || fn->method_owner) ? 2 : 1;
    bool variadic = mvp_variadic_function(fn);
    int argument_count = fn->abi.param_count + hidden + (variadic ? 1 : 0);
    MIR_var_t args[LAMBDA_MAX_FUNCTION_ARGS + 3] = {{MIR_T_P, "runtime", 0}};
    if (hidden == 2) args[1] = {MIR_T_P, "environment", 0};
    char names[LAMBDA_MAX_FUNCTION_ARGS][16];
    for (int i = 0; i < fn->abi.param_count; i++) {
        snprintf(names[i], sizeof(names[i]), "arg%d", i);
        args[i + hidden] = {em_mir_type_for_rep(fn->abi.params[i].canonical_rep), names[i], 0};
    }
    if (variadic) args[fn->abi.param_count + hidden] = {MIR_T_I64, "vargs", 0};
    MIR_type_t returns[] = {em_mir_type_for_rep(fn->abi.result.normal.abi_rep), MIR_T_I64};
    fn->target = MIR_new_func_arr(m->ctx, fn->name, em_return_nres(fn->abi.result.companion),
        returns, argument_count, args);
    c.em.func_item = fn->target; c.em.func = MIR_get_item_func(m->ctx, fn->target);
    MirFrameState* f = &c.em.frame;
    f->active = true; f->number_active = true; f->item_return = fn->abi.result.normal.abi_rep == VALUE_REP_ITEM;
    f->return_type = returns[0]; f->scalar_return_mode = fn->abi.result.normal.scalar_class;
    f->return_lane_kind = f->item_return ? RETURN_LANE_SCALAR :
        fn->abi.result.shape == RETURN_SHAPE_NATIVE_ERROR ? RETURN_LANE_ERROR : RETURN_LANE_NONE;
    f->runtime = MIR_reg(m->ctx, "runtime", c.em.func);
    for (int i = 0; i < argument_count; i++)
        em_function_argument_register(&c.em, MIR_reg(m->ctx, args[i].name, c.em.func));
    // public admission may collect before later operands are converted.
    // All incoming Item owners are precise candidates from the entry.
    if (fn->ast && fn->boxed_entry)
        for (int i = 0; i < fn->abi.param_count; i++)
            root_value(&c, MIR_reg(m->ctx, args[i + hidden].name, c.em.func), JIT_VALUE_BOXED_ITEM);
    if (hidden == 2) {
        c.environment = MIR_reg(m->ctx, "environment", c.em.func);
        root_value(&c, c.environment, JIT_VALUE_RAW_GC_POINTER);
    }
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
    if (fn->method_owner) {
        c.method_receiver = c.environment;
        if (fn->ast->captures) {
            // a lexical method receives [receiver, declaring Function]; its
            // ordinary capture indices still address the Function's own env.
            c.method_environment = c.environment;
            c.method_receiver = em_capture_slot_read(&c.em, c.method_environment, 2, 0);
            root_value(&c, c.method_receiver, JIT_VALUE_BOXED_ITEM);
            MIR_reg_t source = em_capture_slot_read(&c.em, c.method_environment, 2, 1);
            root_value(&c, source, JIT_VALUE_BOXED_ITEM);
            c.environment = em_load_at(&c.em, source, offsetof(Function, closure_env),
                MIR_T_P, "method_lexical_environment");
            root_value(&c, c.environment, JIT_VALUE_RAW_GC_POINTER);
        }
    }
    if (variadic) {
        MIR_reg_t rest = MIR_reg(m->ctx, "vargs", c.em.func);
        MIR_label_t ready = label(&c);
        branch(&c, MIR_BNE, ready, reg(&c, rest), imm(&c, ITEM_NULL));
        move(&c, rest, imm(&c, 0)); mark(&c, ready);
        // Context is not a root: both rest owners occupy fixed precise slots
        // through defaults, nested calls and every completion (D6.1.1).
        c.vargs_roots = argument_span(&c, 2);
        MIR_reg_t previous = em_load_at(&c.em, f->runtime, MIR_EVAL_CONTEXT_VARGS_OFFSET, MIR_T_I64, "saved_vargs");
        em_store_at(&c.em, c.vargs_roots, 0, MIR_T_I64, previous);
        em_store_at(&c.em, c.vargs_roots, sizeof(Item), MIR_T_I64, rest);
        em_store_at(&c.em, f->runtime, MIR_EVAL_CONTEXT_VARGS_OFFSET, MIR_T_I64, rest);
    }
    if (uint16_t count = mvp_function_binder_count(fn))
        // runtime checks write directly into fixed precise roots, so no
        // native-stack mirror can fall behind a collecting check (D4.1.4).
        c.binder_env = argument_span(&c, count);
    if (fn->captured_type_count) {
        c.captured_types = argument_span(&c, fn->captured_type_count);
        for (uint16_t i = 0; i < fn->captured_type_count; i++) {
            MirValue selected = read_binder_type(&c, fn->captured_type_entries[i]);
            em_store_at(&c.em, c.captured_types, i * sizeof(Type*), MIR_T_I64, selected.reg);
        }
    }
    for (uint32_t i = 0; i < m->binding_count; i++) {
        MvpBinding* b = &m->bindings[i];
        if (b->owner != fn->id || !b->entry || !mvp_binding_node(b) ||
                (mvp_binding_node(b)->node_type != AST_NODE_PARAM &&
                 mvp_binding_node(b)->node_type != AST_NODE_VARIABLE_DECLARATOR &&
                 mvp_binding_node(b)->node_type != AST_NODE_FOR_CLAUSE &&
                 mvp_binding_node(b)->node_type != AST_NODE_FOR_INDEX && !b->iteration_binding && !b->capture_binding)) continue;
        b->reg = em_new_reg(&c.em, "binding", em_mir_type_for_rep(b->rep));
        value(&c, b->reg, b->contract->type_id, b->rep, b->contract);
    }
    if (fn->js_export_name) {
        MIR_op_t operands[LAMBDA_MAX_FUNCTION_ARGS];
        for (int i = 0; i < fn->abi.param_count; i++)
            operands[i] = reg(&c, MIR_reg(m->ctx, args[i + hidden].name, c.em.func));
        MIR_reg_t result = em_js_export_call(&c.em, fn->js_export_ref,
            fn->abi.param_count, operands);
        return_value(&c, value(&c, result, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY), NULL);
    } else if (fn->view) {
        c.view_model = MIR_reg(m->ctx, args[hidden].name, c.em.func);
        c.current_item = c.current_root = c.view_model;
        c.current_parent = c.current_index = constant(&c, ITEM_NULL);
        if (fn->handler && fn->handler->param) {
            MvpBinding* event = mvp_binding(m, fn->handler->param->entry);
            MirValue incoming = value(&c, MIR_reg(m->ctx, args[hidden + 1].name, c.em.func),
                LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
            store_binding(&c, event, incoming);
        }
        for (AstStateEntry* state = fn->view->state; state; state = state->next_state) {
            if (!state->name) continue;
            MIR_reg_t item = em_template_state_read(&c.em, c.view_model, fn->template_ref, state, !fn->handler);
            MirValue incoming = value(&c, item, LMD_TYPE_ANY, VALUE_REP_ITEM, &TYPE_ANY);
            MvpBinding* binding = mvp_binding(m, state->entry);
            if (binding) move(&c, binding->reg, reg(&c, incoming.reg));
        }
        if (fn->handler && fn->view->body && fn->view->body->node_type == AST_NODE_CONTENT)
            for (AstNode* declaration = ((AstListNode*)fn->view->body)->item; declaration; declaration = declaration->next)
                if (is_view_handler_body_binding(declaration->node_type)) expression(&c, declaration, true);
        MirValue result = expression(&c, fn->ast->body);
        return_value(&c, result, fn->ast->body);
    } else if (fn->ast) {
        int cap_index = 0, cap_count = ast_function_capture_count(fn->ast);
        for (FnCapture* cap = fn->ast->captures; cap; cap = cap->next, cap_index++) {
            if (mvp_static_type_capture(cap) || mvp_method_field(fn, cap->entry) ||
                    (cap->entry && cap->entry->is_binder)) continue;
            MvpBinding* binding = mvp_binding(m, cap->entry);
            MirValue item = value(&c, em_capture_slot_read(&c.em, c.environment, cap_count, cap_index),
                binding->contract->type_id, VALUE_REP_ITEM, binding->contract);
            if (cap->lambda_live_local) {
                binding->capture_home = item.reg;
                root_value(&c, item.reg, JIT_VALUE_RAW_GC_POINTER);
                register_shared_binding(&c, binding);
                item = value(&c, em_call_1(&c.em, "lambda_capture_home_read", MIR_T_I64,
                    MIR_T_P, reg(&c, binding->capture_home), true),
                    binding->contract->type_id, VALUE_REP_ITEM, binding->contract);
            }
            item = own_binding_scalar(&c, binding, convert(&c, item, binding->rep));
            move(&c, binding->reg, reg(&c, item.reg), item.mir_type);
            refresh_array(&c, binding);
        }
        if (fn->method_owner) {
            c.current_item = c.current_root = c.method_receiver;
            c.current_parent = c.current_index = constant(&c, ITEM_NULL);
            FOR_EACH_MAP_FIELD(fn->method_owner, field) {
                MvpBinding* binding = mvp_binding(m, field->binding);
                if (!binding || !field->name) continue;
                MirMethodField incoming = em_method_field_load(&c.em, c.method_receiver, field);
                // method binding proves the nominal receiver; its admitted
                // field contract selects the pointer/scalar decoding lane.
                MirValue item = value(&c, incoming.item, binding->contract->type_id,
                    VALUE_REP_ITEM, binding->contract);
                item = own_binding_scalar(&c, binding, convert(&c, item, binding->rep));
                move(&c, binding->reg, reg(&c, item.reg), item.mir_type);
                binding->method_field_present = incoming.present;
                refresh_array(&c, binding);
            }
        }
        // S11.4.11: each predicate starts a fresh walk context over its
        // candidate, with no parent or key inherited from its caller.
        if (fn->ast->is_that_predicate && fn->ast->param) {
            c.current_item = mvp_binding(m, fn->ast->param->entry)->reg;
            c.current_root = c.current_item;
            c.current_parent = c.current_index = constant(&c, ITEM_NULL);
        }
        bool snapshots = c.binder_env || c.captured_types || function_has_snapshots(m, fn);
        if (snapshots) {
            int i = 0;
            if (c.binder_env) {
                // supplied sites join first; ordered reference admission then
                // lets a later default observe converted earlier parameters.
                for (AstNamedNode* p = fn->ast->param; p; p = (AstNamedNode*)p->next, i++)
                    receive_binder_site(&c, p, MIR_reg(m->ctx, args[i + hidden].name, c.em.func));
                i = 0;
            }
            for (AstNamedNode* p = fn->ast->param; p; p = (AstNamedNode*)p->next, i++)
                receive_parameter(&c, mvp_binding(m, p->entry), MIR_reg(m->ctx, args[i + hidden].name, c.em.func), i);
            // entry allocation may return an error before the body starts;
            // consume borrow homes while all parameter owners are initialized.
            take_var_homes(&c, true);
        }
        MvpIndexLoopPlan indices = {};
        MvpBinding* saved = snapshot_bindings(m);
        if (!saved) {
            if (captured_state) { restore_bindings(m, captured_state); mem_free(captured_state); }
            return mvp_reject(m, fn->ast, "dense binding allocation failed");
        }
        // eligibility proves every initializer and write before narrowed lanes are emitted.
        if (!snapshots) narrow_dense_parameters(&c);
        narrow_dense_locals(&c, ast_index_find(&m->tp->ast_index, fn->ast->body), true);
        MvpDenseCalls calls = {};
        bool dense = dense_body_eligible(&c, &indices, &calls);
        if (!dense) {
            restore_bindings(m, saved);
            narrow_dense_locals(&c, ast_index_find(&m->tp->ast_index, fn->ast->body));
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
                receive_parameter(&c, b, MIR_reg(m->ctx, args[i + hidden].name, c.em.func), i);
            snapshot_parameter(&c, b);
            if (!dense && !allocating) refresh_array(&c, b);
        }
        if (!snapshots && !dense && !allocating) guarded_scalar_body(&c);
        if (dense || allocating) {
            MIR_label_t checked = label(&c);
            // consume each witness directly instead of retaining all arrays'
            // flags across the guard chain; the checked sibling builds metadata.
            for (uint32_t b = 0; b < m->binding_count; b++) {
                MvpBinding* binding = &m->bindings[b];
                Type* element = mvp_array_element(binding->contract);
                if (binding->owner != fn->id || !element) continue;
                if (mvp_open_value_contract(saved[b].contract)) {
                    fn->guarded_array_entry = true;
                    // inferred primitive owners have no admitted pointer lane;
                    // reject scalar tags before inspecting an array header.
                    binding->reg = em_guard_container(&c.em, binding->reg, LMD_TYPE_ARRAY_NUM, checked);
                }
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
        if (m->main && lambda_type_func_is_proc(m->main->ast->type)) {
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
    mark(&c, f->return_label);
    if (fn->handler && fn->view->is_edit) {
        MIR_label_t failed = label(&c);
        branch(&c, MIR_BEQ, failed, reg(&c, operation(&c, MIR_URSH,
            reg(&c, f->return_reg), imm(&c, 56))), imm(&c, LMD_TYPE_ERROR));
        // the bridge takes a nullable C description pointer, not an Item.
        em_call_1(&c.em, "edit_commit", MIR_T_I64, MIR_T_P, imm(&c, 0), true);
        mark(&c, failed);
    }
    if (fn->method_owner && lambda_type_func_is_proc(fn->ast->type)) {
        MIR_label_t saved_return = f->return_label;
        f->return_label = label(&c);
        FOR_EACH_MAP_FIELD(fn->method_owner, field) {
            MvpBinding* binding = mvp_binding(m, field->binding);
            if (!binding || !field->name || (!binding->method_field_written &&
                    !ast_body_may_write_entry(fn->ast->body, field->binding, true))) continue;
            MirValue item = convert(&c, value(&c, binding->reg, binding->contract->type_id,
                binding->rep, binding->contract), VALUE_REP_ITEM);
            em_method_field_store(&c.em, c.method_receiver, field, item.reg,
                binding->method_field_present, mvp_check_item_result);
        }
        mark(&c, f->return_label); f->return_label = saved_return;
    }
    epilogue(&c);
    c.em.after_may_gc_call = NULL;
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
    while (c.scalar_aggregates) {
        MvpScalarAggregate* next = c.scalar_aggregates->next;
        mem_free(c.scalar_aggregates); c.scalar_aggregates = next;
    }
    while (c.float_constants) {
        MvpFloatConstant* next = c.float_constants->next;
        mem_free(c.float_constants); c.float_constants = next;
    }
    while (c.module_locations) {
        MvpModuleLocation* next = c.module_locations->next;
        mem_free(c.module_locations); c.module_locations = next;
    }
    for (uint32_t i = 0; i < m->binding_count; i++) m->bindings[i].scalar_aggregate = NULL;
    if (captured_state) { restore_bindings(m, captured_state); mem_free(captured_state); }
    if (c.shared_bindings) arraylist_free(c.shared_bindings);
    return !m->failed;
}
