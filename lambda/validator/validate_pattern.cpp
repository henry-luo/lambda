/**
 * @file validate_pattern.cpp
 * @brief Pattern validation for Lambda Validator
 *
 * Handles validation of:
 * - Occurrence patterns: ?, +, *, [n], [n+], [n,m]
 * - Union types: T1 | T2 | ...
 */

#include "validator_internal.hpp"
#include "../core/mark_reader.hpp"

// ==================== Forward Declarations ====================

// From validate.cpp
ValidationResult* validate_against_type(SchemaValidator* validator, ConstItem item, Type* type);

// ==================== Packed Sequence Elements ====================

// A packed array's element tag (or a range's integers) decides a plain element
// contract by itself. A structured one -- a union or optional (`int?`,
// `int | null`), a literal, a constraint, a nested occurrence or array -- has
// to see the values: `[1, 2] is int?[]` answered false because `int?` embeds
// no tag, and `[3] is 1[]` true because a literal's tag is just `int`.
static bool element_contract_is_plain(Type* operand) {
    operand = unwrap_type(operand);
    if (!operand) return false;
    if (type_is_global_meta_type(operand)) return true;
    return operand->kind == TYPE_KIND_SIMPLE && !operand->is_literal;
}

// The rows of an N-D array are (ndim-1)-D arrays of one leaf type. Only when
// the element contract is exactly that many count-free `[]` layers over a plain
// leaf does the leaf tag decide it (S11.1.1v3: rank is preserved, so a 2-D
// `[[1]]` is no `int[]`); any other shape is checked row by row.
static Type* ndim_plain_leaf(Type* operand, int layers) {
    Type* cur = unwrap_type(operand);
    for (int i = 0; i < layers; i++) {
        if (!cur || cur->kind != TYPE_KIND_UNARY) return NULL;
        TypeUnary* layer = (TypeUnary*)cur;
        if (layer->op != OPERATOR_ARRAY || layer->min_count != 0 ||
                layer->max_count != -1) return NULL;
        cur = unwrap_type(layer->operand);
    }
    return cur && element_contract_is_plain(cur) ? cur : NULL;
}

// S11.1.1v3: `T[]` holds when every element satisfies T. A packed lane or a
// range has no Item slots, so each element -- an N-D array's leading-axis row
// -- is read through item_at and validated like a generic array's. A row view
// or a character allocates, so the container stays rooted across the walk.
static ValidationResult* validate_sequence_elements(SchemaValidator* validator,
        Item container, int64_t count, Type* operand_type) {
    TypeType wrapper;
    wrapper.type_id = LMD_TYPE_TYPE;
    wrapper.type = operand_type;
    RootFrame roots(1);
    Rooted<Item> rooted(roots, container);
    if (validator->is_fast_mode()) {
        for (int64_t i = 0; i < count; i++) {
            ValidationResult* elem = validate_against_base_type(validator,
                item_at(rooted.get(), i).to_const(), &wrapper);
            if (!elem || !elem->valid) return validation_verdict(false);
        }
        return validation_verdict(true);
    }
    ValidationResult* result = create_validation_result(validator->get_pool());
    for (int64_t i = 0; i < count; i++) {
        PathScope scope(validator, (long)i);
        ValidationResult* elem = validate_against_base_type(validator,
            item_at(rooted.get(), i).to_const(), &wrapper);
        if (elem && !elem->valid) merge_errors(result, elem, validator);
    }
    if (result->error_count == 0) result->valid = true;
    return result;
}

// ==================== Occurrence Validation ====================

/**
 * Validate a single item against TypeUnary when it's not a container
 * (handles the case where a single value matches occurrence of 1)
 */
static ValidationResult* validate_single_item_occurrence(
    SchemaValidator* validator,
    ConstItem item,
    TypeUnary* type_unary
) {
    ValidationResult* result = create_validation_result(validator->get_pool());
    CountConstraint constraint = get_count_constraint(type_unary);

    // S11.1.6v2: a run of zero is `null`, whatever spells the zero -- `T?`,
    // `T*` and `T{0}` all admit it, and `T+` and `T{1,}` do not.
    if (item.type_id() == LMD_TYPE_NULL) {
        result->valid = constraint.min == 0;
        if (!result->valid) {
            add_type_mismatch_error(result, validator, "a value", item.type_id());
        }
        return result;
    }

    // A run of one is the bare value.
    if (constraint.min <= 1 && (constraint.max == -1 || constraint.max >= 1)) {
        // Validate the single item against the operand type
        Type* operand_type = unwrap_type(type_unary->operand);
        if (operand_type) {
            TypeType temp_wrapper;
            temp_wrapper.type_id = LMD_TYPE_TYPE;
            temp_wrapper.type = operand_type;
            return validate_against_base_type(validator, item, &temp_wrapper);
        }
    }

    result->valid = false;
    add_type_mismatch_error(result, validator, "array/list", item.type_id());
    return result;
}

/**
 * Validate ArrayNum against occurrence type
 * ArrayNum stores raw numeric values (int/int64/float), not tagged Items
 */
static ValidationResult* validate_array_num_occurrence(
    SchemaValidator* validator,
    const ArrayNum* arr_num,
    TypeUnary* type_unary
) {
    if (validator->is_fast_mode()) {
        // Same rules as below, verdict only: count constraint, then the O(1)
        // representation check. No allocation, and no error text to build.
        int fast_count = 0;
        if (arr_num) {
            if (arr_num->is_ndim && arr_num->extra) {
                ArrayNumShape* s = (ArrayNumShape*)(uintptr_t)arr_num->extra;
                fast_count = (s && s->ndim >= 1) ? (int)array_num_shape_dims(s)[0]
                                                 : (int)arr_num->length;
            } else {
                fast_count = (int)arr_num->length;
            }
        }
        CountConstraint fast_c = get_container_count_constraint(type_unary);
        if (fast_count < fast_c.min) return validation_verdict(false);
        if (fast_c.max >= 0 && fast_count > fast_c.max) return validation_verdict(false);
        Type* fast_operand = unwrap_type(type_unary->operand);
        if (!fast_operand) return validation_verdict(false);
        if (fast_operand->type_id == LMD_TYPE_ANY) return validation_verdict(true);
        if (!arr_num) return validation_verdict(false);
        Item fast_container = {.array_num = (ArrayNum*)arr_num};
        if (arr_num->is_ndim) {
            Type* leaf = ndim_plain_leaf(fast_operand, array_num_rank(arr_num) - 1);
            if (leaf) {
                return validation_verdict(
                    validator_array_elem_embeds(arr_num->get_elem_type(), leaf));
            }
            return validate_sequence_elements(validator, fast_container, fast_count,
                fast_operand);
        }
        if (validator_array_elem_embeds(arr_num->get_elem_type(), fast_operand)) {
            return validation_verdict(true);
        }
        if (element_contract_is_plain(fast_operand)) return validation_verdict(false);
        return validate_sequence_elements(validator, fast_container, fast_count,
            fast_operand);
    }

    ValidationResult* result = create_validation_result(validator->get_pool());

    // For N-D arrays use the leading-axis count (shape[0]) to match the
    // logical row-count of nested-array semantics.  Pattern (int*)[2+] means
    // "at least 2 inner arrays of ints" — a 2-D ArrayNum with shape[0]=2 satisfies that.
    int count = 0;
    bool is_ndim = false;
    if (arr_num) {
        is_ndim = arr_num->is_ndim;
        if (is_ndim && arr_num->extra) {
            ArrayNumShape* s = (ArrayNumShape*)(uintptr_t)arr_num->extra;
            if (s && s->ndim >= 1) {
                count = (int)array_num_shape_dims(s)[0];
            } else {
                count = (int)arr_num->length;
            }
        } else {
            count = (int)arr_num->length;
        }
    }
    CountConstraint constraint = get_container_count_constraint(type_unary);

    log_debug("[PATTERN] ArrayNum occurrence: count=%d, ndim=%d, min=%d, max=%d, elem_type=%d",
              count, is_ndim, constraint.min, constraint.max,
              arr_num ? (int)arr_num->get_elem_type() : -1);

    // Check count constraints
    if (!check_count_constraint(count, constraint, result, validator, "Array")) {
        return result;
    }

    Type* operand_type = unwrap_type(type_unary->operand);

    if (!operand_type) {
        result->valid = false;
        add_constraint_error_fmt(result, validator,
            "ArrayNum elements have no operand type to match");
        return result;
    }

    // Validator `any` is the non-error data top type. A numeric carrier has
    // no error members, including for N-D arrays, so it satisfies `any[]`
    // without imposing a fabricated numeric element constraint.
    if (operand_type->type_id == LMD_TYPE_ANY) {
        result->valid = true;
        return result;
    }

    // N-D ArrayNum: leading-axis slices are themselves arrays. A contract that
    // is exactly (ndim-1) `[]` layers over a plain leaf is decided by the leaf
    // tag; anything else -- a rank that does not match, a count, a union or
    // optional -- checks each row as the (ndim-1)-D array it is. Stripping
    // every wrapper down to the leaf let a 2-D `[[1]]` pass `int[]`.
    if (is_ndim) {
        Item container = {.array_num = (ArrayNum*)arr_num};
        Type* leaf = ndim_plain_leaf(operand_type, array_num_rank(arr_num) - 1);
        if (!leaf) return validate_sequence_elements(validator, container, count, operand_type);
        if (validator_array_elem_embeds(arr_num->get_elem_type(), leaf)) {
            result->valid = true;
        } else {
            result->valid = false;
            add_constraint_error_fmt(result, validator,
                "N-D ArrayNum elements do not embed exactly into expected type_id=%d",
                leaf->type_id);
        }
        return result;
    }

    if (arr_num && validator_array_elem_embeds(arr_num->get_elem_type(), operand_type)) {
        // ArrayNum occurrence checks are covariant because validation reads/copies elements.
        result->valid = true;
    } else if (arr_num && !element_contract_is_plain(operand_type)) {
        // a structured element contract needs the values, not the lane's tag
        return validate_sequence_elements(validator, {.array_num = (ArrayNum*)arr_num},
            count, operand_type);
    } else {
        result->valid = false;
        add_constraint_error_fmt(result, validator,
            "ArrayNum elements do not embed exactly into expected type_id=%d",
            operand_type->type_id);
    }

    return result;
}

/**
 * Validate Range against occurrence type
 * Range is an integer sequence from start to end
 */
static ValidationResult* validate_range_occurrence(
    SchemaValidator* validator,
    const Range* range,
    TypeUnary* type_unary
) {
    ValidationResult* result = create_validation_result(validator->get_pool());

    int count = range ? (int)range->length : 0;
    CountConstraint constraint = get_container_count_constraint(type_unary);

    log_debug("[PATTERN] Range occurrence: count=%d, min=%d, max=%d",
              count, constraint.min, constraint.max);

    // Check count constraints
    if (!check_count_constraint(count, constraint, result, validator, "Range")) {
        return result;
    }

    // An integer range's elements are ints; a character range's are
    // one-codepoint strings (S11.1.3). A structured element contract, or any
    // character range, needs the element values themselves.
    Type* operand_type = unwrap_type(type_unary->operand);
    if (range && operand_type &&
            (range->is_char || !element_contract_is_plain(operand_type))) {
        return validate_sequence_elements(validator, {.range = (Range*)range}, count,
            operand_type);
    }

    if (operand_type && validator_numeric_type_embeds(LMD_TYPE_INT, NUM_INT8, operand_type)) {
        result->valid = true;
    } else {
        result->valid = false;
        add_constraint_error_fmt(result, validator,
            "Range elements are integers, but expected type_id=%d",
            operand_type ? operand_type->type_id : -1);
    }

    return result;
}

/**
 * Validate List/Array against occurrence type
 */
static ValidationResult* validate_list_occurrence(
    SchemaValidator* validator,
    const List* list,
    TypeUnary* type_unary
) {
    if (validator->is_fast_mode()) {
        // The element walk is the O(n) shape this whole mode exists for. With
        // no report to assemble it can stop at the first bad element, which
        // full mode must never do (it owes every indexed path).
        int fast_count = list ? (int)list->length : 0;
        CountConstraint fast_c = get_container_count_constraint(type_unary);
        if (fast_count < fast_c.min) return validation_verdict(false);
        if (fast_c.max >= 0 && fast_count > fast_c.max) return validation_verdict(false);
        Type* fast_operand = unwrap_type(type_unary->operand);
        if (!fast_operand) return validation_verdict(false);
        if (list && fast_count > 0) {
            TypeType fast_wrapper;
            fast_wrapper.type_id = LMD_TYPE_TYPE;
            fast_wrapper.type = fast_operand;
            for (int i = 0; i < fast_count; i++) {
                ValidationResult* elem = validate_against_base_type(
                    validator, list->get(i), &fast_wrapper);
                if (!elem || !elem->valid) return validation_verdict(false);
            }
        }
        return validation_verdict(true);
    }

    ValidationResult* result = create_validation_result(validator->get_pool());

    int count = list ? (int)list->length : 0;
    CountConstraint constraint = get_container_count_constraint(type_unary);

    log_debug("[PATTERN] List occurrence: count=%d, min=%d, max=%d",
              count, constraint.min, constraint.max);

    // Check count constraints
    if (!check_count_constraint(count, constraint, result, validator, "List")) {
        return result;
    }

    // Get operand type for element validation
    Type* operand_type = unwrap_type(type_unary->operand);

    if (!operand_type) {
        log_error("[PATTERN] TypeUnary operand is null after unwrapping");
        result->valid = false;
        return result;
    }

    // Validate each list element
    if (list && count > 0) {
        TypeType temp_wrapper;
        temp_wrapper.type_id = LMD_TYPE_TYPE;
        temp_wrapper.type = operand_type;

        for (int i = 0; i < count; i++) {
            PathScope scope(validator, (long)i);

            ConstItem elem = list->get(i);
            ValidationResult* elem_result = validate_against_base_type(validator, elem, &temp_wrapper);

            if (elem_result && !elem_result->valid) {
                merge_errors(result, elem_result, validator);
            }
        }
    }

    // Occurrence validation must retain every failing element so callers can
    // report all indexed paths instead of silently stopping at the first one.
    if (result->error_count == 0) result->valid = true;
    return result;
}

ValidationResult* validate_occurrence_type(
    SchemaValidator* validator,
    ConstItem item,
    TypeUnary* type_unary
) {
    log_debug("[PATTERN] validate_occurrence_type: op=%d, min=%d, max=%d",
              type_unary->op, type_unary->min_count, type_unary->max_count);

    TypeId item_type_id = item.type_id();

    // Check if item is a list/array/range
    bool is_container = (item_type_id == LMD_TYPE_ARRAY ||
                         item_type_id == LMD_TYPE_ARRAY_NUM ||
                         item_type_id == LMD_TYPE_RANGE);

    if (!is_container) {
        // S11.1.6v2: the two families differ exactly here. `T[]` and `T[n]`
        // describe an array, so nothing else satisfies them; an occurrence
        // describes a run, which at zero is `null` and at one is a bare T.
        if (type_unary->op == OPERATOR_ARRAY) {
            ValidationResult* result = create_validation_result(validator->get_pool());
            result->valid = false;
            add_type_mismatch_error(result, validator, "array", item_type_id);
            return result;
        }
        return validate_single_item_occurrence(validator, item, type_unary);
    }

    // Handle typed arrays specially
    ValidationResult* as_run =
        item_type_id == LMD_TYPE_ARRAY_NUM
            ? validate_array_num_occurrence(validator, item.array_num, type_unary)
        : item_type_id == LMD_TYPE_RANGE
            ? validate_range_occurrence(validator, item.range, type_unary)
            : validate_list_occurrence(validator, item.array, type_unary);
    if (as_run && as_run->valid) return as_run;

    // S11.1.6v2: a run of one is the bare value, and when the operand itself
    // admits a container that bare value IS a container -- `int[]?` holds an
    // array, `[]` included. Only a failed run reading reaches this, so the
    // common path still tests each element once.
    // Only a run reads its container as its one item: `T[]` describes the
    // container itself, so `[1]` must not pass `int[][]` as one `int[]`.
    CountConstraint bounds = get_count_constraint(type_unary);
    if (type_unary->op != OPERATOR_ARRAY &&
            bounds.min <= 1 && (bounds.max < 0 || bounds.max >= 1)) {
        Type* operand = unwrap_type(type_unary->operand);
        if (operand) {
            TypeType wrapper;
            wrapper.type_id = LMD_TYPE_TYPE;
            wrapper.type = operand;
            ValidationResult* as_one = validate_against_base_type(validator, item, &wrapper);
            if (as_one && as_one->valid) return as_one;
        }
    }
    return as_run;
}

// ==================== Union Type Validation ====================

ValidationResult* validate_against_union_type(
    SchemaValidator* validator,
    ConstItem item,
    Type** union_types,
    int type_count
) {
    if (validator->is_fast_mode()) {
        // A union verdict is "does any member match?", so fast mode returns on
        // the first success. The min_errors scoring below exists only to pick
        // the closest member for the error message, which fast mode never
        // produces — so it is skipped entirely rather than degenerating (every
        // fast-mode failure carries error_count 0).
        if (!union_types || type_count <= 0) return validation_verdict(false);
        for (int i = 0; i < type_count; i++) {
            if (!union_types[i]) continue;
            ValidationResult* member = validate_against_type(validator, item, union_types[i]);
            if (member && member->valid) return validation_verdict(true);
        }
        return validation_verdict(false);
    }

    ValidationResult* result = create_validation_result(validator->get_pool());

    if (!union_types || type_count <= 0) {
        add_constraint_error(result, validator, "Invalid union type definition");
        return result;
    }

    // Track the best result for error reporting
    ValidationResult* best_result = nullptr;
    int min_errors = INT32_MAX;
    int best_union_index = -1;

    log_debug("[PATTERN] Validating against union type with %d members", type_count);

    for (int i = 0; i < type_count; i++) {
        if (!union_types[i]) continue;

        log_debug("[PATTERN] Trying union member %d (type_id=%d)", i, union_types[i]->type_id);

        // Create scoped path for union member
        PathScope scope(validator, PATH_UNION, (long)i);

        // Try validating against this union member
        ValidationResult* member_result = validate_against_type(validator, item, union_types[i]);

        if (member_result && member_result->valid) {
            log_debug("[PATTERN] Union member %d matched successfully", i);
            result->valid = true;
            return result;
        }

        if (member_result) {
            log_debug("[PATTERN] Union member %d failed with %d errors", i, member_result->error_count);

            // Track the result with the fewest errors (most specific/helpful)
            if (member_result->error_count < min_errors) {
                min_errors = member_result->error_count;
                best_result = member_result;
                best_union_index = i;
            }
        }
    }

    // No type in the union was valid - report the best error
    result->valid = false;

    log_debug("[PATTERN] No union member matched. Best result from member %d with %d errors",
              best_union_index, min_errors);

    if (best_result && best_result->error_count > 0) {
        // Copy errors from the best result
        merge_errors(result, best_result, validator);

        // Add a summary error at the top level
        add_constraint_error_fmt(result, validator,
            "Item does not match any type in union (%d types tried, closest match was type #%d with %d error%s)",
            type_count, best_union_index, min_errors, min_errors == 1 ? "" : "s");
    } else {
        // No useful error information from union members
        add_constraint_error_fmt(result, validator,
            "Item does not match any type in union (%d types)", type_count);
    }

    return result;
}

// ==================== Binary Type Validation ====================

/**
 * Flatten a binary type tree into an array of types.
 * For union types like A | B | C, this collects [A, B, C].
 */
static void flatten_binary_type(TypeBinary* binary, Type** types, int* count, int max_count, Operator op) {
    if (!binary || *count >= max_count) return;

    // Check if left is also a binary type with same operator
    if (binary->left && binary->left->kind == TYPE_KIND_BINARY) {
        TypeBinary* left_binary = (TypeBinary*)binary->left;
        if (left_binary->op == op) {
            flatten_binary_type(left_binary, types, count, max_count, op);
        } else {
            // Different operator, treat as a single type
            types[(*count)++] = binary->left;
        }
    } else if (binary->left) {
        types[(*count)++] = binary->left;
    }

    // Check if right is also a binary type with same operator
    if (binary->right && binary->right->kind == TYPE_KIND_BINARY) {
        TypeBinary* right_binary = (TypeBinary*)binary->right;
        if (right_binary->op == op) {
            flatten_binary_type(right_binary, types, count, max_count, op);
        } else {
            // Different operator, treat as a single type
            types[(*count)++] = binary->right;
        }
    } else if (binary->right) {
        types[(*count)++] = binary->right;
    }
}

ValidationResult* validate_binary_type(
    SchemaValidator* validator,
    ConstItem item,
    TypeBinary* type_binary
) {
    log_debug("[PATTERN] validate_binary_type: op=%d", type_binary->op);

    ValidationResult* result = create_validation_result(validator->get_pool());

    if (!type_binary) {
        add_constraint_error(result, validator, "Invalid binary type (null)");
        return result;
    }

    switch (type_binary->op) {
        case OPERATOR_UNION: {
            // Flatten union type into array
            const int MAX_UNION_TYPES = 32;
            Type* union_types[MAX_UNION_TYPES];
            int type_count = 0;

            flatten_binary_type(type_binary, union_types, &type_count, MAX_UNION_TYPES, OPERATOR_UNION);

            log_debug("[PATTERN] Union type flattened to %d types", type_count);

            if (type_count == 0) {
                add_constraint_error(result, validator, "Empty union type");
                return result;
            }

            return validate_against_union_type(validator, item, union_types, type_count);
        }

        case OPERATOR_INTERSECT:
        case OPERATOR_OR: {
            // Intersection type - item must match ALL types.
            // `&` reaches here as OPERATOR_INTERSECT from expression/annotation
            // space and historically as OPERATOR_OR from the type-pattern
            // parser; both spell the same set operation, so accept either.
            // Try left first
            ValidationResult* left_result = validate_against_type(validator, item, type_binary->left);
            if (left_result && !left_result->valid) {
                merge_errors(result, left_result, validator);
                return result;
            }

            // Then try right
            ValidationResult* right_result = validate_against_type(validator, item, type_binary->right);
            if (right_result && !right_result->valid) {
                merge_errors(result, right_result, validator);
                return result;
            }

            result->valid = true;
            return result;
        }

        case OPERATOR_EXCLUDE: {
            // Exclude type - item must match left but NOT right
            ValidationResult* left_result = validate_against_type(validator, item, type_binary->left);
            if (left_result && !left_result->valid) {
                // Item doesn't match the base type
                merge_errors(result, left_result, validator);
                return result;
            }

            // Item matches base type - now check it doesn't match excluded type
            ValidationResult* right_result = validate_against_type(validator, item, type_binary->right);
            if (right_result && right_result->valid) {
                // Item matches the excluded type - fail
                add_constraint_error(result, validator, "Item matches excluded type");
                return result;
            }

            result->valid = true;
            return result;
        }

        default:
            add_constraint_error_fmt(result, validator, "Unsupported binary type operator: %d", type_binary->op);
            return result;
    }
}

// ==================== Legacy Occurrence Validation ====================

/**
 * Legacy function for validating occurrence constraints on item arrays
 * Used by external callers that pass explicit item arrays
 */
ValidationResult* validate_against_occurrence(
    SchemaValidator* validator,
    ConstItem* items,
    long item_count,
    Type* expected_type,
    Operator occurrence_op
) {
    ValidationResult* result = create_validation_result(validator->get_pool());

    if (!expected_type) {
        add_constraint_error(result, validator, "Invalid occurrence constraint parameters");
        return result;
    }

    // Validate occurrence constraints based on operator
    switch (occurrence_op) {
        case OPERATOR_OPTIONAL: // ? (0 or 1)
            if (item_count > 1) {
                add_constraint_error_fmt(result, validator,
                    "Optional constraint violated: expected 0 or 1 items, got %ld", item_count);
            }
            break;

        case OPERATOR_ONE_MORE: // + (1 or more)
            if (item_count < 1) {
                add_constraint_error(result, validator,
                    "One-or-more constraint violated: expected at least 1 item, got 0");
            }
            break;

        case OPERATOR_ZERO_MORE: // * (0 or more)
            // Always valid for zero-or-more
            break;

        default:
            add_constraint_error_fmt(result, validator,
                "Unsupported occurrence operator: %d", occurrence_op);
            return result;
    }

    // Validate each item against the expected type
    for (long i = 0; i < item_count; i++) {
        PathScope path_scope(validator, i);
        DepthScope depth_scope(validator);

        ValidationResult* item_result = validate_against_type(validator, items[i], expected_type);

        if (item_result && !item_result->valid) {
            merge_errors(result, item_result, validator);
        }
    }

    return result;
}
