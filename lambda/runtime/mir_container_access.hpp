#pragma once
#include "mir_emitter_shared.hpp"

typedef MIR_reg_t (*MirIndexCoordinateReader)(void* owner, AstNode* key);

// raw coordinates have activation lifetime across collecting key expressions.
static inline MIR_reg_t em_index_coordinates(MirEmitter* em,
        AstNode* keys, MirIndexCoordinateReader read, void* owner) {
    int count = em_linked_node_count(keys);
    // data-zone bytes have no independent GC owner. Raw coordinates instead
    // live once per activation, across collecting keys and repeated loop reads.
    MIR_reg_t indices = em_fixed_stack_storage(em, "nd_indices", (int64_t)count * sizeof(int64_t));
    int position = 0;
    for (AstNode* key = keys; key; key = key->next, position++) {
        MIR_reg_t coordinate = read(owner, key);
        em_store_at(em, indices, (MIR_disp_t)position * sizeof(int64_t), MIR_T_I64, coordinate);
    }
    return indices;
}

// S8.2.4v3: all coordinates address one receiver; a miss stays a boxed null.
static inline MIR_reg_t em_index_nd(MirEmitter* em, MIR_reg_t receiver,
        AstNode* keys, MirIndexCoordinateReader read, void* owner) {
    if (em->root_call_value)
        em->root_call_value(em->call_owner, receiver, JIT_VALUE_BOXED_ITEM);
    MIR_reg_t indices = em_index_coordinates(em, keys, read, owner);
    return em_call_3(em, "fn_index_nd", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, receiver),
        MIR_T_I64, MIR_new_int_op(em->ctx, em_linked_node_count(keys)),
        MIR_T_P, MIR_new_reg_op(em->ctx, indices), true);
}

// a nested subscript binds its key's `last`; only its receiver can still
// refer to the enclosing subscript's last index (S7.2.2).
static inline void em_scan_index_last(AstNode* child, AstNode* parent, void* context) {
    (void)parent;
    bool* found = (bool*)context;
    if (*found || !child) return;
    if (child->node_type == AST_NODE_LAST_INDEX) { *found = true; return; }
    if (child->node_type == AST_NODE_INDEX_EXPR) {
        em_scan_index_last(((AstFieldNode*)child)->object, child, context);
        em_scan_index_last(child->next, child, context);
        return;
    }
    ast_visit_core_children(child, em_scan_index_last, context);
}

static inline bool em_index_keys_use_last(AstNode* keys) {
    bool found = false;
    em_scan_index_last(keys, NULL, &found);
    return found;
}

// coarse array/map admission proves only the container kind, never an element
// certificate or record layout. Other carriers retain runtime admission.
static inline void em_guard_broad_container(MirEmitter* em, MIR_reg_t item,
        TypeId expected, MIR_label_t miss, bool proven_pointer = false) {
    MIR_reg_t pointer = proven_pointer ? item : em_guard_container(em, item, LMD_TYPE_RAW_POINTER, miss);
    MIR_reg_t kind = em_load_at(em, pointer, LAMBDA_GC_OFF_CONTAINER_TYPE_ID, MIR_T_U8, "container_admit_kind");
    MIR_label_t done = expected == LMD_TYPE_ARRAY ? em_new_label(em) : NULL;
    if (done) em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BEQ, MIR_new_label_op(em->ctx, done),
        MIR_new_reg_op(em->ctx, kind), MIR_new_int_op(em->ctx, LMD_TYPE_ARRAY_NUM)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, miss),
        MIR_new_reg_op(em->ctx, kind), MIR_new_int_op(em->ctx, expected)));
    if (done) em_emit_label(em, done);
}

// Physical ArrayNum guards shared by both Lambda MIR lowerings. The caller
// already owns an admitted container; views and alternate lanes use its fallback.
static inline void em_array_num_elem_guard(MirEmitter* em, MIR_reg_t arr_ptr,
        uint8_t expected_elem, MIR_label_t l_bail, uint8_t expected_layout = 0) {
    // fold adjacent immutable header bytes without dropping kind, view/rank
    // or lane checks; build the constant in host byte order (D4.3.3).
    uint8_t expected_bytes[] = {LMD_TYPE_ARRAY_NUM, 0, expected_layout, expected_elem};
    uint8_t mask_bytes[] = {UINT8_MAX, 0, 0x03, UINT8_MAX};
    uint32_t expected, mask;
    memcpy(&expected, expected_bytes, sizeof(expected));
    memcpy(&mask, mask_bytes, sizeof(mask));
    MIR_reg_t header = em_load_at(em, arr_ptr, 0, MIR_T_U32, "array_header");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, header),
        MIR_new_reg_op(em->ctx, header), MIR_new_uint_op(em->ctx, mask)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, l_bail),
        MIR_new_reg_op(em->ctx, header), MIR_new_uint_op(em->ctx, expected)));
}

// caller policy selects whether immutable-storage flags need their own guard.
static inline void em_container_cow_guard(MirEmitter* em, MIR_reg_t pointer,
        MIR_label_t miss, bool immutable_flags = false, MIR_reg_t* raw_state = NULL) {
    uint64_t mask = COW_STATE_SHARED;
    MIR_disp_t offset = offsetof(Container, cow_state);
    MIR_type_t width = MIR_T_U8;
    if (immutable_flags) {
        uint8_t bytes[sizeof(Container)] = {};
        bytes[offsetof(Container, flags)] = (1u << 4) | (1u << 5);
        bytes[offsetof(Container, cow_state)] = COW_STATE_SHARED;
        static_assert(sizeof(bytes) == sizeof(mask), "container guard uses one header word");
        memcpy(&mask, bytes, sizeof(mask)); offset = 0; width = MIR_T_I64;
    }
    MIR_reg_t state = em_load_at(em, pointer, offset, width, "cow_header");
    if (raw_state) *raw_state = state;
    MIR_reg_t masked = raw_state ? em_new_reg(em, "cow_mask", MIR_T_I64) : state;
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, masked),
        MIR_new_reg_op(em->ctx, state), MIR_new_uint_op(em->ctx, mask)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, miss),
        MIR_new_reg_op(em->ctx, masked)));
}

// test the runtime's COW-kind policy without calling or materializing a value.
static inline void em_cow_kind_guard(MirEmitter* em, MIR_reg_t pointer, MIR_label_t miss) {
    uint64_t kinds = 0;
    static_assert(LMD_TYPE_COUNT <= 64, "COW kinds fit one MIR word");
    for (int i = 0; i < LMD_TYPE_COUNT; i++)
        if (cow_type_is_container((TypeId)i)) kinds |= UINT64_C(1) << i;
    MIR_reg_t kind = em_load_at(em, pointer, LAMBDA_GC_OFF_CONTAINER_TYPE_ID, MIR_T_U8, "capture_kind");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UBGE, MIR_new_label_op(em->ctx, miss),
        MIR_new_reg_op(em->ctx, kind), MIR_new_int_op(em->ctx, 64)));
    MIR_reg_t allowed = em_new_reg(em, "capture_allowed", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, allowed),
        MIR_new_uint_op(em->ctx, kinds)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_URSH, MIR_new_reg_op(em->ctx, allowed),
        MIR_new_reg_op(em->ctx, allowed), MIR_new_reg_op(em->ctx, kind)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, allowed),
        MIR_new_reg_op(em->ctx, allowed), MIR_new_int_op(em->ctx, 1)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF, MIR_new_label_op(em->ctx, miss),
        MIR_new_reg_op(em->ctx, allowed)));
}

// the unprofiled capture is one idempotent header update, never an allocation.
// Shared headers stay read-only; tagged scalars and non-COW carriers are unchanged.
static inline void em_container_capture(MirEmitter* em, MIR_reg_t pointer,
        MIR_reg_t state, bool proven_container, MIR_label_t done) {
    if (!proven_container) em_cow_kind_guard(em, pointer, done);
    MIR_reg_t shared = em_new_reg(em, "capture_shared", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_OR, MIR_new_reg_op(em->ctx, shared),
        MIR_new_reg_op(em->ctx, state), MIR_new_int_op(em->ctx, COW_STATE_SHARED)));
    em_store_at(em, pointer, offsetof(Container, cow_state), MIR_T_U8, shared);
}

// owned shaped arrays carry one stride table, rather than nested row pointers.
// callers retain admission/COW policy; all guards here are allocation-free.
static inline MIR_reg_t em_array_ndim_element_address(MirEmitter* em, MIR_reg_t array,
        int rank, const MIR_reg_t* indices, ArrayNumElemType lane, MIR_label_t miss) {
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BEQ, MIR_new_label_op(em->ctx, miss),
        MIR_new_reg_op(em->ctx, array), MIR_new_int_op(em->ctx, 0)));
    em_array_num_elem_guard(em, array, lane, miss, 1);
    MIR_reg_t shape = em_load_at(em, array, LAMBDA_GC_OFF_LIST_EXTRA, MIR_T_I64, "nd_shape");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BEQ, MIR_new_label_op(em->ctx, miss),
        MIR_new_reg_op(em->ctx, shape), MIR_new_int_op(em->ctx, 0)));
    MIR_reg_t actual_rank = em_load_at(em, shape, offsetof(ArrayNumShape, ndim), MIR_T_U8, "nd_rank");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, miss),
        MIR_new_reg_op(em->ctx, actual_rank), MIR_new_int_op(em->ctx, rank)));
    MIR_reg_t offset = em_new_reg(em, "nd_offset", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, offset),
        MIR_new_int_op(em->ctx, 0)));
    for (int i = 0; i < rank; i++) {
        MIR_reg_t extent = em_load_at(em, shape, offsetof(ArrayNumShape, data) + i * sizeof(int64_t),
            MIR_T_I64, "nd_extent");
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UBGE, MIR_new_label_op(em->ctx, miss),
            MIR_new_reg_op(em->ctx, indices[i]), MIR_new_reg_op(em->ctx, extent)));
        MIR_reg_t stride = em_load_at(em, shape,
            offsetof(ArrayNumShape, data) + (rank + i) * sizeof(int64_t), MIR_T_I64, "nd_stride");
        MIR_reg_t scaled = em_new_reg(em, "nd_scaled", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MUL, MIR_new_reg_op(em->ctx, scaled),
            MIR_new_reg_op(em->ctx, indices[i]), MIR_new_reg_op(em->ctx, stride)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ADD, MIR_new_reg_op(em->ctx, offset),
            MIR_new_reg_op(em->ctx, offset), MIR_new_reg_op(em->ctx, scaled)));
    }
    return em_array_element_address(em, array, offset, lane == ELEM_BOOL ? 1 : sizeof(int64_t));
}

static inline void em_array_num_bounds_check(MirEmitter* em, MIR_reg_t arr_ptr,
        MIR_reg_t idx_int, MIR_label_t oob_label, bool index_nonnegative = false) {
    // indices are 64-bit: MIR's S suffix truncates to 32 bits, admitting 2^32.
    MIR_reg_t arr_len = em_new_reg(em, "alen", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, arr_len),
        MIR_new_mem_op(em->ctx, MIR_T_I64, LAMBDA_GC_OFF_LIST_LENGTH, arr_ptr, 0, 1)));
    if (!index_nonnegative) {
        MIR_reg_t neg_check = em_new_reg(em, "negc", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_LT, MIR_new_reg_op(em->ctx, neg_check),
            MIR_new_reg_op(em->ctx, idx_int), MIR_new_int_op(em->ctx, 0)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, oob_label),
            MIR_new_reg_op(em->ctx, neg_check)));
    }
    MIR_reg_t ge_check = em_new_reg(em, "gec", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_GE, MIR_new_reg_op(em->ctx, ge_check),
        MIR_new_reg_op(em->ctx, idx_int), MIR_new_reg_op(em->ctx, arr_len)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, oob_label),
        MIR_new_reg_op(em->ctx, ge_check)));
}

static inline void em_array_cert_word_guard(MirEmitter* em, MIR_reg_t cert,
        MIR_type_t word_type, int offset, uint64_t expected, uint64_t mask,
        MIR_label_t bail) {
    MIR_reg_t word = em_new_reg(em, "array_cert_word", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, word),
        MIR_new_mem_op(em->ctx, word_type, offset, cert, 0, 1)));
    if (mask != UINT64_MAX) {
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
            MIR_new_reg_op(em->ctx, word), MIR_new_reg_op(em->ctx, word),
            MIR_new_int_op(em->ctx, (int64_t)mask)));
    }
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE,
        MIR_new_label_op(em->ctx, bail), MIR_new_reg_op(em->ctx, word),
        MIR_new_int_op(em->ctx, (int64_t)expected)));
}

// the inherited list prefix is shared by ArrayNum; Full asserts this offset.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
static inline void em_array_rep_cert_guard(MirEmitter* em, MIR_reg_t arr_ptr,
        Type* contract, MIR_label_t l_bail, bool primitive_admission = false) {
    LambdaArrayContractInfo info = {};
    if (!lambda_array_contract_info(contract, &info)) return;
    MIR_reg_t cert = em_new_reg(em, "array_cert", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, cert),
        MIR_new_mem_op(em->ctx, MIR_T_I64, offsetof(List, rep_cert),
            arr_ptr, 0, 1)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BEQ, MIR_new_label_op(em->ctx, l_bail),
        MIR_new_reg_op(em->ctx, cert), MIR_new_int_op(em->ctx, 0)));

    // Adjacent immutable bytes form one guard, in host byte order. This keeps
    // every rank/layout/nullability check while avoiding per-byte branches.
    static_assert(offsetof(ArrayRepCert, flags) == offsetof(ArrayRepCert, rank) + 1,
        "certificate rank and flags must be adjacent");
    static_assert(offsetof(LaneStorageDesc, nullable) == offsetof(LaneStorageDesc, kind) + 1,
        "certificate lane and nullability must be adjacent");
    uint8_t flags = ARRAY_REP_CERT_EXACT | ARRAY_REP_CERT_ERROR_FREE;
    if (primitive_admission) flags |= ARRAY_REP_CERT_PRIMITIVE_VALUES;
    if (info.leaf_element && (info.leaf_element->type_id == LMD_TYPE_MAP ||
            info.leaf_element->type_id == LMD_TYPE_ELEMENT)) {
        flags |= ARRAY_REP_CERT_REIFIED;
    }
    uint8_t rank_flags_bytes[] = {info.rank, flags};
    // admission may reuse only the same uncounted primitive domain. Counted,
    // reified and future certificate variants retain runtime admission.
    uint8_t rank_flags_mask_bytes[] = {UINT8_MAX,
        (uint8_t)(primitive_admission ? UINT8_MAX : flags)};
    uint8_t lane_bytes[] = {info.leaf_lane.kind, info.leaf_lane.nullable};
    uint16_t rank_flags, rank_flags_mask, lane;
    memcpy(&rank_flags, rank_flags_bytes, sizeof(rank_flags));
    memcpy(&rank_flags_mask, rank_flags_mask_bytes, sizeof(rank_flags_mask));
    memcpy(&lane, lane_bytes, sizeof(lane));
    em_array_cert_word_guard(em, cert, MIR_T_U16, (int)offsetof(ArrayRepCert, rank),
        rank_flags, rank_flags_mask, l_bail);
    em_array_cert_word_guard(em, cert, MIR_T_I64, (int)offsetof(ArrayRepCert, leaf_element),
        (uint64_t)(uintptr_t)info.leaf_element, UINT64_MAX, l_bail);
    em_array_cert_word_guard(em, cert, MIR_T_U16,
        (int)(offsetof(ArrayRepCert, leaf_lane) + offsetof(LaneStorageDesc, kind)),
        lane, UINT64_MAX, l_bail);
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// callers prove the lane/certificate and exclusive ownership before entering.
// No interior buffer pointer survives the growth fallback (D5.3, D4.3.3).
static inline void em_array_num_append(MirEmitter* em, MIR_reg_t array,
        MIR_reg_t value, ArrayNumElemType lane, MIR_label_t cold) {
    MIR_reg_t length = em_load_at(em, array, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "append_length");
    MIR_reg_t capacity = em_load_at(em, array, LAMBDA_GC_OFF_LIST_CAPACITY, MIR_T_I64, "append_capacity");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_UBGE, MIR_new_label_op(em->ctx, cold),
        MIR_new_reg_op(em->ctx, length), MIR_new_reg_op(em->ctx, capacity)));
    MIR_reg_t address = em_array_element_address(em, array, length, lane == ELEM_BOOL ? 1 : sizeof(int64_t));
    em_store_at(em, address, 0, em_numeric_storage_type(lane), value);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ADD, MIR_new_reg_op(em->ctx, length),
        MIR_new_reg_op(em->ctx, length), MIR_new_int_op(em->ctx, 1)));
    em_store_at(em, array, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, length);
}

// a unique open boxed array can append one string without allocation.
// all other carriers retain checked growth, sequence and certificate policy.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
static inline void em_open_string_append(MirEmitter* em, MIR_reg_t owner,
        MIR_reg_t value, MIR_label_t cold) {
    MIR_reg_t tag = em_new_reg(em, "string_push_tag", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_URSH, MIR_new_reg_op(em->ctx, tag),
        MIR_new_reg_op(em->ctx, value), MIR_new_int_op(em->ctx, 56)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, cold),
        MIR_new_reg_op(em->ctx, tag), MIR_new_int_op(em->ctx, LMD_TYPE_STRING)));
    MIR_reg_t array = em_guard_container(em, owner, LMD_TYPE_ARRAY, cold);
    em_container_cow_guard(em, array, cold, true);
    MIR_reg_t flags = em_load_at(em, array, LAMBDA_GC_OFF_CONTAINER_ARRAY_FLAGS, MIR_T_U8, "string_push_flags");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, flags),
        MIR_new_reg_op(em->ctx, flags), MIR_new_int_op(em->ctx,
            CONTAINER_ARRAY_FLAG_NDIM | CONTAINER_ARRAY_FLAG_NATIVE_LANE | (1u << 1) | JS_ELEMENTS_STATE_MASK)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, cold),
        MIR_new_reg_op(em->ctx, flags)));
    MIR_reg_t state = em_load_at(em, array, offsetof(Container, reserved_state), MIR_T_U8, "string_push_state");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, state),
        MIR_new_reg_op(em->ctx, state), MIR_new_int_op(em->ctx, CONTAINER_STATE_VIRTUAL_LIST)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, cold),
        MIR_new_reg_op(em->ctx, state)));
    MIR_reg_t cert = em_load_at(em, array, offsetof(List, rep_cert), MIR_T_I64, "string_push_cert");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, cold),
        MIR_new_reg_op(em->ctx, cert), MIR_new_int_op(em->ctx, 0)));
    MIR_reg_t extra = em_load_at(em, array, LAMBDA_GC_OFF_LIST_EXTRA, MIR_T_I64, "string_push_extra");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, cold),
        MIR_new_reg_op(em->ctx, extra), MIR_new_int_op(em->ctx, 0)));
    MIR_reg_t length = em_load_at(em, array, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, "string_push_length");
    MIR_reg_t capacity = em_load_at(em, array, LAMBDA_GC_OFF_LIST_CAPACITY, MIR_T_I64, "string_push_capacity");
    MIR_reg_t spare = em_new_reg(em, "string_push_spare", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_SUB, MIR_new_reg_op(em->ctx, spare),
        MIR_new_reg_op(em->ctx, capacity), MIR_new_reg_op(em->ctx, length)));
    // preserve the runtime's spare sentinel slot at the growth boundary.
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BLE, MIR_new_label_op(em->ctx, cold),
        MIR_new_reg_op(em->ctx, spare), MIR_new_int_op(em->ctx, 1)));
    em_store_at(em, em_array_element_address(em, array, length, sizeof(Item)), 0, MIR_T_I64, value);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ADD, MIR_new_reg_op(em->ctx, length),
        MIR_new_reg_op(em->ctx, length), MIR_new_int_op(em->ctx, 1)));
    em_store_at(em, array, LAMBDA_GC_OFF_LIST_LENGTH, MIR_T_I64, length);
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// physical Array words exclude shaped, JS and virtual storage. Pointer lanes
// may opt in to raw words; generic Item readers must reject those lanes.
static inline MIR_reg_t em_physical_array_owner(MirEmitter* em, MIR_reg_t owner,
        MIR_label_t miss, bool pointer_lane = false) {
    MIR_reg_t pointer = em_guard_container(em, owner, LMD_TYPE_ARRAY, miss);
    MIR_reg_t flags = em_load_at(em, pointer, LAMBDA_GC_OFF_CONTAINER_ARRAY_FLAGS, MIR_T_U8, "pointer_array_flags");
    MIR_reg_t shaped = em_new_reg(em, "pointer_array_shaped", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, shaped),
        MIR_new_reg_op(em->ctx, flags), MIR_new_int_op(em->ctx,
            CONTAINER_ARRAY_FLAG_NDIM | (1u << 1) | JS_ELEMENTS_STATE_MASK |
            (pointer_lane ? 0 : CONTAINER_ARRAY_FLAG_NATIVE_LANE))));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, miss),
        MIR_new_reg_op(em->ctx, shaped), MIR_new_int_op(em->ctx, 0)));
    MIR_label_t physical = pointer_lane ? em_new_label(em) : NULL;
    if (pointer_lane) {
        MIR_reg_t native = em_new_reg(em, "pointer_array_native", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, native),
            MIR_new_reg_op(em->ctx, flags), MIR_new_int_op(em->ctx, CONTAINER_ARRAY_FLAG_NATIVE_LANE)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, physical),
            MIR_new_reg_op(em->ctx, native)));
    }
    // native lanes own the state byte; ordinary Array can instead be a thin
    // virtual list with no physical items buffer (D2.3.4).
    MIR_reg_t state = em_load_at(em, pointer, offsetof(Container, reserved_state), MIR_T_U8, "pointer_array_state");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, state),
        MIR_new_reg_op(em->ctx, state), MIR_new_int_op(em->ctx, CONTAINER_STATE_VIRTUAL_LIST)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT, MIR_new_label_op(em->ctx, miss),
        MIR_new_reg_op(em->ctx, state)));
    if (physical) em_emit_label(em, physical);
    return pointer;
}

static inline MIR_reg_t em_physical_array_index(MirEmitter* em, MIR_reg_t owner,
        MIR_reg_t index, MIR_label_t miss, MIR_label_t absent, bool pointer_lane) {
    MIR_reg_t pointer = em_physical_array_owner(em, owner, miss, pointer_lane);
    em_array_num_bounds_check(em, pointer, index, absent);
    return em_load_at(em, em_array_element_address(em, pointer, index, sizeof(Item)),
        0, MIR_T_I64, "pointer_array_item");
}

// admitted pointer elements share the physical word reader and strip Item tags.
static inline MIR_reg_t em_pointer_array_index(MirEmitter* em, MIR_reg_t owner,
        MIR_reg_t index, MIR_label_t miss, MIR_label_t absent) {
    MIR_reg_t item = em_physical_array_index(em, owner, index, miss, absent, true);
    MIR_reg_t result = em_new_reg(em, "pointer_array_child", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, result),
        MIR_new_reg_op(em->ctx, item), MIR_new_uint_op(em->ctx, UINT64_C(0x00ffffffffffffff))));
    return result;
}

// Consume one caller-published precise home before any nested call (S9.1.3).
static inline MIR_reg_t em_take_var_home(MirEmitter* em, MIR_reg_t runtime, int position) {
    MIR_disp_t cell = offsetof(Context, mir_var_homes) + position * sizeof(uint64_t*);
    // production consumes these cells before installing MirFrameState.
    MIR_reg_t home = em_load_at(em, runtime, cell, MIR_T_I64, "var_home");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_mem_op(em->ctx, MIR_T_I64, cell, runtime, 0, 1),
        MIR_new_int_op(em->ctx, 0)));
    return home;
}
static inline void em_publish_var_home(MirEmitter* em, int position, MIR_reg_t home) {
    em_store_at(em, em->frame.runtime,
        offsetof(Context, mir_var_homes) + position * sizeof(uint64_t*), MIR_T_I64, home);
}

// shared physical string reads; the caller admits the pointer, the miss retains Unicode/absence.
static inline MIR_reg_t em_ascii_string_byte_at_pointer(MirEmitter* em,
        MIR_reg_t ptr, MIR_reg_t index) {
    MIR_reg_t address = em_new_reg(em, "char_addr", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ADD,
        MIR_new_reg_op(em->ctx, address), MIR_new_reg_op(em->ctx, ptr),
        MIR_new_reg_op(em->ctx, index)));
    MIR_reg_t byte = em_new_reg(em, "char_byte", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, byte),
        MIR_new_mem_op(em->ctx, MIR_T_U8, offsetof(String, chars), address, 0, 1)));
    return byte;
}


static inline MIR_reg_t em_guard_ascii_string(MirEmitter* em, MIR_reg_t ptr, MIR_label_t slow) {
    MIR_reg_t flags = em_new_reg(em, "char_flags", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, flags),
        MIR_new_mem_op(em->ctx, MIR_T_U8, offsetof(String, flags), ptr, 0, 1)));
    MIR_reg_t ascii = em_new_reg(em, "char_ascii", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, ascii), MIR_new_reg_op(em->ctx, flags),
        MIR_new_int_op(em->ctx, 1)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF,
        MIR_new_label_op(em->ctx, slow), MIR_new_reg_op(em->ctx, ascii)));
    MIR_reg_t length = em_new_reg(em, "char_len", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV,
        MIR_new_reg_op(em->ctx, length),
        MIR_new_mem_op(em->ctx, MIR_T_U32, offsetof(String, len), ptr, 0, 1)));
    return length;
}

static inline MIR_reg_t em_ascii_string_byte(MirEmitter* em, MIR_reg_t ptr,
        MIR_reg_t index, MIR_label_t slow) {
    MIR_reg_t length = em_guard_ascii_string(em, ptr, slow);
    MIR_reg_t in_range = em_new_reg(em, "char_in_range", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_ULT,
        MIR_new_reg_op(em->ctx, in_range), MIR_new_reg_op(em->ctx, index),
        MIR_new_reg_op(em->ctx, length)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BF,
        MIR_new_label_op(em->ctx, slow), MIR_new_reg_op(em->ctx, in_range)));
    return em_ascii_string_byte_at_pointer(em, ptr, index);
}

// immutable text compares share the pointer ABI and byte Boolean normalization.
static const uint32_t MIR_LITERAL_STRING_COMPARE_MAX_BYTES = 64;

// null equality visits no container children. Type values retain the kernel's
// type(null) comparison, and encountered errors keep their payload (S5.1.1v2).
static inline MIR_reg_t em_item_null_compare(MirEmitter* em, MIR_reg_t item,
        Operator op, bool item_result) {
    MIR_reg_t result = em_new_reg(em, "null_compare", MIR_T_I64);
    MIR_label_t scalar = em_new_label(em), done = em_new_label(em);
    MIR_reg_t tag = em_new_reg(em, "null_tag", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_URSH, MIR_new_reg_op(em->ctx, tag),
        MIR_new_reg_op(em->ctx, item), MIR_new_int_op(em->ctx, 56)));
    if (item_result) {
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, item)));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BEQ, MIR_new_label_op(em->ctx, done),
            MIR_new_reg_op(em->ctx, tag), MIR_new_int_op(em->ctx, LMD_TYPE_ERROR)));
    }
    em_guard_container(em, item, LMD_TYPE_TYPE, scalar);
    const char* helper = op == OPERATOR_EQ ? (item_result ? "fn_eq_value" : "fn_eq")
        : (item_result ? "fn_ne_value" : "fn_ne");
    MIR_reg_t fallback = em_call_2(em, helper, MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, item), MIR_T_I64, MIR_new_uint_op(em->ctx, ITEM_NULL), true);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, fallback)));
    if (!item_result) em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, result),
        MIR_new_reg_op(em->ctx, result), MIR_new_int_op(em->ctx, UINT8_MAX)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP, MIR_new_label_op(em->ctx, done)));
    em_emit_label(em, scalar);
    MIR_reg_t zero = em_new_reg(em, "null_pointer", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ, MIR_new_reg_op(em->ctx, result),
        MIR_new_reg_op(em->ctx, tag), MIR_new_int_op(em->ctx, LMD_TYPE_NULL)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ, MIR_new_reg_op(em->ctx, zero),
        MIR_new_reg_op(em->ctx, item), MIR_new_int_op(em->ctx, 0)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_OR, MIR_new_reg_op(em->ctx, result),
        MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, zero)));
    if (op == OPERATOR_NE) em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ,
        MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, result), MIR_new_int_op(em->ctx, 0)));
    if (item_result) em_emit_insn(em, MIR_new_insn(em->ctx, MIR_OR,
        MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, result), MIR_new_uint_op(em->ctx, ITEM_FALSE)));
    em_emit_label(em, done);
    return result;
}

// canonical type predicates need only the semantic TypeId. Named/extended
// descriptors retain the runtime identity test (S2.1.1v3, D7.3.1).
static inline MIR_reg_t em_semantic_value_type_test(MirEmitter* em, MIR_reg_t item, Type* expected) {
    bool scalar = expected == &TYPE_NULL || expected == &TYPE_BOOL || expected == &TYPE_INT ||
        expected == &TYPE_FLOAT || expected == &TYPE_STRING || expected == &TYPE_ERROR;
    static const struct { Type* type; TypeId aliases[2]; int count; } containers[] = {
        {&TYPE_MAP, {LMD_TYPE_VMAP}, 1},
        {&TYPE_ARRAY, {LMD_TYPE_ARRAY_NUM, LMD_TYPE_VARRAY}, 2},
        {&TYPE_ELMT, {LMD_TYPE_VELMT}, 1},
    };
    int alternative = -1;
    for (unsigned i = 0; i < sizeof(containers) / sizeof(containers[0]); i++)
        if (containers[i].type == expected) { alternative = (int)i; break; }
    if (!scalar && alternative < 0) return em_call_2(em, "lambda_value_type_is_exact", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, item), MIR_T_P,
        MIR_new_int_op(em->ctx, (intptr_t)expected), true);
    MIR_reg_t kind = em_call_1(em, "item_type_id", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, item), false);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, kind),
        MIR_new_reg_op(em->ctx, kind), MIR_new_int_op(em->ctx, UINT8_MAX)));
    MIR_reg_t result = em_new_reg(em, "semantic_type", MIR_T_I64);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ, MIR_new_reg_op(em->ctx, result),
        MIR_new_reg_op(em->ctx, kind), MIR_new_int_op(em->ctx, expected->type_id)));
    if (alternative >= 0) for (int i = 0; i < containers[alternative].count; i++) {
        MIR_reg_t matches = em_new_reg(em, "semantic_alias", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ, MIR_new_reg_op(em->ctx, matches),
            MIR_new_reg_op(em->ctx, kind), MIR_new_int_op(em->ctx, containers[alternative].aliases[i])));
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_OR, MIR_new_reg_op(em->ctx, result),
            MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, matches)));
    }
    return result;
}

// S11.2.1: value patterns use membership for types/ranges and equality for
// other values. A Bool error sentinel is a failed test, never a selected arm.
static inline MIR_reg_t em_pattern_value_test(MirEmitter* em, MIR_reg_t subject,
        MIR_reg_t pattern, TypeId known_kind = LMD_TYPE_ANY) {
    MIR_reg_t result = em_new_reg(em, "pattern_result", MIR_T_I64);
    MIR_label_t done = em_new_label(em);
    MIR_reg_t kind = 0;
    if (known_kind == LMD_TYPE_ANY) {
        kind = em_call_1(em, "item_type_id", MIR_T_I64, MIR_T_I64,
            MIR_new_reg_op(em->ctx, pattern), false);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, kind),
            MIR_new_reg_op(em->ctx, kind), MIR_new_int_op(em->ctx, UINT8_MAX)));
    }
    static const struct { TypeId kind; const char* helper; } tests[] = {
        {LMD_TYPE_TYPE, "fn_is"}, {LMD_TYPE_RANGE, "fn_in"}, {LMD_TYPE_ANY, "fn_eq"},
    };
    for (const auto& test : tests) {
        if (!kind && test.kind != LMD_TYPE_ANY && test.kind != known_kind) continue;
        MIR_label_t next = kind && test.kind != LMD_TYPE_ANY ? em_new_label(em) : NULL;
        if (next) em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, next),
            MIR_new_reg_op(em->ctx, kind), MIR_new_int_op(em->ctx, test.kind)));
        MIR_reg_t matched = em_call_2(em, test.helper, MIR_T_I64,
            MIR_T_I64, MIR_new_reg_op(em->ctx, subject), MIR_T_I64, MIR_new_reg_op(em->ctx, pattern), true);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result),
            MIR_new_reg_op(em->ctx, matched)));
        if (!kind) break;
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_JMP, MIR_new_label_op(em->ctx, done)));
        if (next) em_emit_label(em, next);
    }
    em_emit_label(em, done);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND, MIR_new_reg_op(em->ctx, result),
        MIR_new_reg_op(em->ctx, result), MIR_new_int_op(em->ctx, UINT8_MAX)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ, MIR_new_reg_op(em->ctx, result),
        MIR_new_reg_op(em->ctx, result), MIR_new_int_op(em->ctx, BOOL_TRUE)));
    return result;
}

// query windows apply after stable ordering, before the one positional finish
// (S2.5.2v2). Both lowerings evaluate offset then limit through their value hook.
static inline MIR_reg_t em_finalize_query_stream(MirEmitter* em, MIR_reg_t output,
        MIR_reg_t keys, int64_t order_mask, int64_t order_count,
        AstNode* offset, AstNode* limit, bool limit_from_end, bool item_position) {
    if (order_count) {
        output = em_call_4(em, "fn_order_rows", MIR_T_P,
            MIR_T_I64, MIR_new_reg_op(em->ctx, output), MIR_T_I64, MIR_new_reg_op(em->ctx, keys),
            MIR_T_I64, MIR_new_int_op(em->ctx, order_mask), MIR_T_I64, MIR_new_int_op(em->ctx, order_count), true);
        if (em->root_call_value) em->root_call_value(em->call_owner, output, JIT_VALUE_RAW_GC_POINTER);
    }
    if (offset || limit) {
        AstNode* bounds[] = {offset, limit};
        MIR_reg_t arguments[2];
        for (int i = 0; i < 2; i++) {
            if (bounds[i]) arguments[i] = em_require_rep(em,
                em_lower_value(em, bounds[i], MIR_VALUE_ANY), VALUE_REP_ITEM).reg;
            else {
                arguments[i] = em_new_reg(em, "absent_window_bound", MIR_T_I64);
                em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, arguments[i]),
                    MIR_new_int_op(em->ctx, ITEM_NULL)));
            }
        }
        int64_t flags = (offset ? FOR_WINDOW_OFFSET : 0) | (!limit ? 0 :
            limit_from_end ? FOR_WINDOW_LIMIT_LAST : FOR_WINDOW_LIMIT);
        output = em_call_4(em, "for_window", MIR_T_I64, MIR_T_P, MIR_new_reg_op(em->ctx, output),
            MIR_T_I64, MIR_new_reg_op(em->ctx, arguments[0]), MIR_T_I64, MIR_new_reg_op(em->ctx, arguments[1]),
            MIR_T_I64, MIR_new_int_op(em->ctx, flags), true);
    }
    return em_call_1(em, item_position ? "list_collapse_item" : "list_collapse_value", MIR_T_I64,
        MIR_T_I64, MIR_new_reg_op(em->ctx, output), true);
}

// admitted nullable strings compare exact UTF-8 bytes, including embedded NUL.
// Promoted from Full's literal comparator; no character decoding is needed (S5.1).
static inline MIR_reg_t em_string_literal_pointer_eq(MirEmitter* em, MIR_reg_t subject,
        MIR_reg_t literal, String* bytes, MIR_reg_t result = 0) {
    if (!result) {
        result = em_new_reg(em, "literal_equal", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result), MIR_new_int_op(em->ctx, 0)));
    }
    MIR_label_t equal = em_new_label(em), done = em_new_label(em);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BEQ, MIR_new_label_op(em->ctx, done),
        MIR_new_reg_op(em->ctx, subject), MIR_new_int_op(em->ctx, 0)));
    MIR_reg_t length = em_load_at(em, subject, offsetof(String, len), MIR_T_U32, "literal_length");
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, done),
        MIR_new_reg_op(em->ctx, length), MIR_new_uint_op(em->ctx, bytes->len)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BEQ, MIR_new_label_op(em->ctx, equal),
        MIR_new_reg_op(em->ctx, subject), MIR_new_reg_op(em->ctx, literal)));
    for (uint32_t i = 0; i < bytes->len; i++) {
        MIR_reg_t byte = em_load_at(em, subject, offsetof(String, chars) + i, MIR_T_U8, "literal_byte");
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BNE, MIR_new_label_op(em->ctx, done),
            MIR_new_reg_op(em->ctx, byte), MIR_new_uint_op(em->ctx, (uint8_t)bytes->chars[i])));
    }
    em_emit_label(em, equal);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_MOV, MIR_new_reg_op(em->ctx, result), MIR_new_int_op(em->ctx, 1)));
    em_emit_label(em, done);
    return result;
}

static inline MIR_reg_t em_text_pointer_compare(MirEmitter* em,
        MIR_reg_t left, MIR_reg_t right, TypeId text_type, Operator op) {
    MIR_reg_t result = em_new_reg(em, "text_eq", MIR_T_I64);
    MIR_label_t done = em_new_label(em);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ,
        MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, left),
        MIR_new_reg_op(em->ctx, right)));
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_BT,
        MIR_new_label_op(em->ctx, done), MIR_new_reg_op(em->ctx, result)));
    MIR_reg_t compared = em_call_2(em,
        text_type == LMD_TYPE_STRING ? "fn_str_eq_ptr" : "fn_sym_eq_ptr",
        MIR_T_I64, MIR_T_I64, MIR_new_reg_op(em->ctx, left),
        MIR_T_I64, MIR_new_reg_op(em->ctx, right), false);
    em_emit_insn(em, MIR_new_insn(em->ctx, MIR_AND,
        MIR_new_reg_op(em->ctx, result), MIR_new_reg_op(em->ctx, compared),
        MIR_new_int_op(em->ctx, 0xff)));
    em_emit_label(em, done);
    if (op == OPERATOR_NE) {
        MIR_reg_t inverted = em_new_reg(em, "text_ne", MIR_T_I64);
        em_emit_insn(em, MIR_new_insn(em->ctx, MIR_EQ,
            MIR_new_reg_op(em->ctx, inverted), MIR_new_reg_op(em->ctx, result),
            MIR_new_int_op(em->ctx, 0)));
        return inverted;
    }
    return result;
}
