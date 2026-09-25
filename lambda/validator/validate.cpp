#include "validator_internal.hpp"
#include "../core/mark_reader.hpp"  // MarkReader API for type-safe traversal
#include "../runtime/re2_wrapper.hpp"  // for pattern_full_match
#include "../core/lambda-decimal.hpp"  // for decimal literal comparison

// Note: Helper functions (should_stop_for_timeout, should_stop_for_max_errors,
// init_validation_session) are now in validate_helpers.cpp

ValidationResult* validate_against_pattern_type(SchemaValidator* validator, ConstItem item, TypePattern* pattern) {
    log_debug("[VALIDATOR] Validating pattern: is_symbol=%d, item type=%d", pattern->is_symbol, item.type_id());
    ValidationResult* result = create_validation_result(validator->get_pool());

    // Use ItemReader for type-safe access
    ItemReader item_reader(item);

    // Pattern validates strings (or symbols if pattern->is_symbol)
    if (pattern->is_symbol) {
        if (!item_reader.isSymbol()) {
            add_type_mismatch_error(result, validator, "symbol", item_reader.getType());
            return result;
        }
    } else {
        if (!item_reader.isString()) {
            add_type_mismatch_error(result, validator, "string", item_reader.getType());
            return result;
        }
    }

    // Get the string/symbol value
    const char* chars;
    uint32_t len;
    if (pattern->is_symbol) {
        Symbol* sym = item_reader.asSymbol();
        chars = sym ? sym->chars : nullptr;
        len = sym ? sym->len : 0;
    } else {
        String* s = item_reader.asString();
        chars = s ? s->chars : nullptr;
        len = s ? s->len : 0;
    }

    // Use existing pattern_full_match from re2_wrapper
    if (chars && pattern_full_match_chars(pattern, chars, len)) {
        result->valid = true;
    } else {
        char error_msg[512];
        const char* pattern_source = pattern->source ? pattern->source->chars : "<unknown>";
        int pattern_len = pattern->source ? (int)pattern->source->len : 9;
        snprintf(error_msg, sizeof(error_msg),
            "String '%.*s' does not match pattern '%.*s'",
            (int)len, chars ? chars : "",
            pattern_len, pattern_source);
        add_validation_error(result, create_validation_error(
            VALID_ERROR_PATTERN_MISMATCH, error_msg,
            validator->get_current_path(), validator->get_pool()));
    }

    return result;
}

static bool array_pattern_simple_type_matches(Item item, Type* type_pattern, bool* handled) {
    *handled = false;
    if (!type_pattern || type_pattern->type_id != LMD_TYPE_TYPE) return false;
    Type* expected = ((TypeType*)type_pattern)->type;
    if (!expected) return false;
    if (expected == &TYPE_NUMBER || expected == &TYPE_INTEGER || IS_NUMERIC_ID(expected->type_id)) {
        *handled = true;
        // array tuple type patterns use the same exact-embedding rule as scalar `is`.
        return validator_numeric_item_embeds(item.to_const(), expected);
    }
    if (expected->kind != TYPE_KIND_SIMPLE) return false;
    // Only a bare kind (`element`, `map`, `array`, `function`) reduces to a
    // TypeId test. A structural pattern -- `<p>`, `{y: int}`, `[int, int]`,
    // `int[]`, `fn (int) int` -- shares its kind's TypeId but must be validated
    // in full: testing the TypeId alone let `[<li>]` match `[<p>]`, and element
    // content match any children of the right kinds (S11.1.6v3).
    if (lambda_type_is_concrete_attr_shape(expected)) return false;
    if (expected->type_id == LMD_TYPE_ARRAY && expected != &TYPE_LIST &&
            expected != (Type*)&TYPE_ARRAY) return false;
    if (lambda_type_func_signature(expected)) return false;
    *handled = true;
    TypeId actual = get_type_id(item);
    if (expected->type_id == LMD_TYPE_ANY) return actual != LMD_TYPE_ERROR;
    if (expected->type_id == LMD_TYPE_ARRAY) {
        return actual == LMD_TYPE_ARRAY || actual == LMD_TYPE_ARRAY_NUM || actual == LMD_TYPE_RANGE;
    }
    return actual == expected->type_id;
}

static bool array_pattern_decimal_items_equal(Item item, Item pattern) {
    int comparison = 0;
    return decimal_cmp_items(item, pattern, &comparison) && comparison == 0;
}

// The occurrence slot of a sequence pattern: a run of the sequence's items
// (S11.1.6v2). The array family is not a run -- `[1, int[], 2]` still wants one
// array in the middle -- so only these four operators consume many items.
static TypeUnary* array_pattern_slot_run(const TypeArray* array_type, int64_t slot) {
    if (!array_type->item_is_type_pattern || !array_type->item_is_type_pattern[slot]) {
        return NULL;
    }
    Type* pattern = unwrap_type(array_type->item_patterns[slot].type);
    if (!pattern || pattern->type_id != LMD_TYPE_TYPE ||
            pattern->kind != TYPE_KIND_UNARY) return NULL;
    TypeUnary* unary = (TypeUnary*)pattern;
    switch (unary->op) {
    case OPERATOR_OPTIONAL: case OPERATOR_ONE_MORE:
    case OPERATOR_ZERO_MORE: case OPERATOR_REPEAT:
        return unary;
    default:
        return NULL;
    }
}

static bool array_pattern_literal_matches(Item item, Item pattern);

static bool array_pattern_slot_matches(SchemaValidator* validator,
        Item child_item, const TypeArray* array_type, int64_t slot) {
    if (array_type->item_is_type_pattern && array_type->item_is_type_pattern[slot]) {
        bool handled = false;
        bool matched = array_pattern_simple_type_matches(child_item,
            array_type->item_patterns[slot].type, &handled);
        if (handled) return matched;
        ValidationResult* slot_result = validate_against_type(validator,
            child_item.to_const(), array_type->item_patterns[slot].type);
        return slot_result && slot_result->valid;
    }
    return array_pattern_literal_matches(child_item, array_type->item_patterns[slot]);
}

static bool array_pattern_has_run(const TypeArray* array_type) {
    for (int64_t slot = 0; slot < array_type->length; slot++) {
        if (array_pattern_slot_run(array_type, slot)) return true;
    }
    return false;
}

// One non-run slot against one item: the same two tests the exact-length loop
// below makes, as a verdict.
static bool array_pattern_slot_matches(SchemaValidator* validator,
        Item child_item, const TypeArray* array_type, int64_t slot);

// Match slots [slot..] against items [index..] (S11.1.6v2). A run consumes
// between its bounds, so the matcher backtracks over the counts a run can
// take; patterns are small and each item is tested at most once per count.
static bool array_pattern_runs_match(SchemaValidator* validator,
        ArrayReader& array, int64_t length, const TypeArray* array_type,
        int64_t slot, int64_t index) {
    if (slot >= array_type->length) return index == length;
    TypeUnary* run = array_pattern_slot_run(array_type, slot);
    if (!run) {
        if (index >= length) return false;
        if (!array_pattern_slot_matches(validator, array.get(index).item(),
                array_type, slot)) return false;
        return array_pattern_runs_match(validator, array, length, array_type,
            slot + 1, index + 1);
    }
    CountConstraint bounds = get_count_constraint(run);
    int64_t available = length - index;
    int64_t most = bounds.max < 0 ? available : bounds.max;
    if (most > available) most = available;
    Type* operand = unwrap_type(run->operand);
    TypeType wrapper;
    wrapper.type_id = LMD_TYPE_TYPE;
    wrapper.type = operand;
    for (int64_t taken = 0; taken <= most; taken++) {
        // Each item the run swallows is tested once, as `taken` grows past it;
        // the first item that fails ends the run's reach.
        if (taken > 0) {
            Item last = array.get(index + taken - 1).item();
            ValidationResult* elem = validate_against_base_type(validator,
                last.to_const(), &wrapper);
            if (!elem || !elem->valid) break;
        }
        if (taken >= bounds.min && array_pattern_runs_match(validator, array,
                length, array_type, slot + 1, index + taken)) return true;
    }
    return false;
}

static bool array_pattern_literal_matches(Item item, Item pattern) {
    TypeId item_type = get_type_id(item);
    TypeId pattern_type = get_type_id(pattern);
    if (item_type != pattern_type) {
        if (item_type == LMD_TYPE_COMPLEX || pattern_type == LMD_TYPE_COMPLEX) {
            // Complex equality has a real-axis bridge; raw it2d() would inspect
            // a pointer payload instead of applying that value-level rule.
            return fn_eq(item, pattern) == BOOL_TRUE;
        }
        if (IS_NUMERIC_ID(item_type) && IS_NUMERIC_ID(pattern_type)) {
            if (item_type == LMD_TYPE_DECIMAL || pattern_type == LMD_TYPE_DECIMAL) {
                return array_pattern_decimal_items_equal(item, pattern);
            }
            double item_val = item_type == LMD_TYPE_NUM_SIZED ? item.get_num_sized_as_double() : it2d(item);
            double pattern_val = pattern_type == LMD_TYPE_NUM_SIZED ? pattern.get_num_sized_as_double() : it2d(pattern);
            return item_val == pattern_val;
        }
        return false;
    }

    switch (item_type) {
    case LMD_TYPE_NULL:
        return true;
    case LMD_TYPE_BOOL:
        return item.bool_val == pattern.bool_val;
    case LMD_TYPE_INT:
        return lambda_int_item_to_i64(item) == lambda_int_item_to_i64(pattern);
    case LMD_TYPE_INT64:
        return item.get_int64() == pattern.get_int64();
    case LMD_TYPE_UINT64:
        return item.get_uint64() == pattern.get_uint64();
    case LMD_TYPE_FLOAT:
        return item.get_double() == pattern.get_double();
    case LMD_TYPE_NUM_SIZED:
        return item.num_type == pattern.num_type && item.num_value == pattern.num_value;
    case LMD_TYPE_COMPLEX:
        return fn_eq(item, pattern) == BOOL_TRUE;
    case LMD_TYPE_DECIMAL:
        return array_pattern_decimal_items_equal(item, pattern);
    case LMD_TYPE_DTIME: {
        DateTime item_dt = item.get_datetime();
        DateTime pattern_dt = pattern.get_datetime();
        return datetime_compare(&item_dt, &pattern_dt) == 0;
    }
    case LMD_TYPE_STRING:
    case LMD_TYPE_BINARY:
        if (item.string_ptr == pattern.string_ptr) return true;
        if (item.get_len() != pattern.get_len()) return false;
        return item.get_len() == 0 || memcmp(item.get_chars(), pattern.get_chars(), item.get_len()) == 0;
    case LMD_TYPE_SYMBOL: {
        Symbol* item_sym = (Symbol*)item.symbol_ptr;
        Symbol* pattern_sym = (Symbol*)pattern.symbol_ptr;
        if (item_sym == pattern_sym) return true;
        if (!item_sym || !pattern_sym || symbol_has_js_identity(item_sym) ||
                symbol_has_js_identity(pattern_sym) ||
                symbol_lambda_namespace(item_sym) != symbol_lambda_namespace(pattern_sym) ||
                item_sym->len != pattern_sym->len) return false;
        return item_sym->len == 0 || memcmp(item_sym->chars, pattern_sym->chars, item_sym->len) == 0;
    }
    default:
        return item.item == pattern.item;
    }
}

// Match a sequence against a sequence pattern's slots: an array's items, or an
// element's children against its declared content pattern (S11.1.6v3). An
// occurrence is a *run* of the sequence's items and zero items is void --
// `[1, int*, 2]` matches `[1, 2]` and `[1, 5, 6, 2]`. Every other slot consumes
// exactly one item, so a pattern with no run keeps the exact-length check and
// reports each mismatching position. `what` names the sequence in errors.
static void validate_sequence_pattern(SchemaValidator* validator,
        ValidationResult* result, ArrayReader& seq, int64_t length,
        const TypeArray* pattern, const char* what) {
    if (array_pattern_has_run(pattern)) {
        if (!array_pattern_runs_match(validator, seq, length, pattern, 0, 0)) {
            add_constraint_error_fmt(result, validator,
                "%s does not match the sequence pattern", what);
        }
        return;
    }
    if (length != pattern->length) {
        add_constraint_error_fmt(result, validator,
            "%s length mismatch: expected %lld, got %lld",
            what, (long long)pattern->length, (long long)length);
        return;
    }

    auto iter = seq.items();
    ItemReader child;
    int64_t index = 0;
    while (iter.next(&child)) {
        PathScope scope(validator, index);
        Item child_item = child.item();
        if (pattern->item_is_type_pattern && pattern->item_is_type_pattern[index]) {
            bool handled = false;
            bool matched = array_pattern_simple_type_matches(
                child_item, pattern->item_patterns[index].type, &handled);
            if (handled) {
                if (!matched) add_constraint_error_fmt(result, validator,
                    "%s item does not match positional type pattern", what);
            } else {
                ValidationResult* item_result = validate_against_type(
                    validator, child_item.to_const(), pattern->item_patterns[index].type);
                if (item_result && !item_result->valid) merge_errors(result, item_result, validator);
            }
        }
        else if (!array_pattern_literal_matches(child_item, pattern->item_patterns[index])) {
            // validator DLLs do not link the evaluator; exact tuple literals must compare locally.
            add_constraint_error_fmt(result, validator,
                "%s item does not match positional pattern", what);
        }
        index++;
    }
}

ValidationResult* validate_against_primitive_type(SchemaValidator* validator, ConstItem item, Type* type) {
    log_debug("[VALIDATOR] Validating primitive: expected=%d, actual=%d", type->type_id, item.type_id());
    ValidationResult* result = create_validation_result(validator->get_pool());

    if (type->type_id == item.type_id()) {
        Item literal = {.item = 0};
        if (lambda_literal_contract_value(type, &literal)) {
            // Literal-union members are value singletons, numeric ones included
            // (S11.2.1, LR03-11); a primitive TypeId check alone would admit
            // every value of the literal's carrier into the union.
            Item actual = {.item = item.item};
            result->valid = array_pattern_literal_matches(actual, literal);
        } else {
            result->valid = true;
        }
    } else {
        result->valid = false;
        add_type_mismatch_error_ex(result, validator, type, item);
    }
    return result;
}

ValidationResult* validate_against_base_type(SchemaValidator* validator, ConstItem item, TypeType* type) {
    // A compact global meta-type (`number`, `integer`, `type`) carries ONLY the
    // two-byte Type prefix -- there is no TypeType payload, so reading
    // `type->type` runs off the end of the global. ASAN caught it as a
    // global-buffer-overflow 8 bytes before TYPE_STRING, reached from admitting
    // a `{q: number}` map field, which aborted construction outright.
    //
    // `type_is_global_meta_type` exists for exactly this and its comment says
    // callers must test it before reading extended fields; both arms below
    // instead dereferenced FIRST and only compared the UNWRAPPED result against
    // TYPE_NUMBER/TYPE_INTEGER -- a comparison a compact global never reaches,
    // because the deref that precedes it is already out of bounds.
    if (type_is_global_meta_type((const Type*)type)) {
        Type* meta = (Type*)type;
        bool ok = meta == &TYPE_NUMBER ? IS_NUMERIC_ID(item.type_id())
            : meta == &TYPE_INTEGER ? validator_numeric_item_embeds(item, meta)
            : item.type_id() == LMD_TYPE_TYPE;
        if (validator->is_fast_mode()) return validation_verdict(ok);
        ValidationResult* meta_result = create_validation_result(validator->get_pool());
        meta_result->valid = ok;
        if (!ok) add_type_mismatch_error_ex(meta_result, validator, meta, item);
        return meta_result;
    }
    if (validator->is_fast_mode()) {
        // This is the leaf the element walk calls once per item, so the
        // allocation at the top of full mode is exactly what fast mode must
        // avoid. Delegating kinds recurse into already-converted functions and
        // return their singleton directly; anything not handled here falls
        // through to full mode, which is correct, just allocating.
        Type* fast_base = unwrap_type(type->type);
        if (!fast_base) return validation_verdict(false);
        if (fast_base->type_id == LMD_TYPE_ANY) {
            return validation_verdict(item.type_id() != LMD_TYPE_ERROR);
        }
        if (fast_base->kind == TYPE_KIND_UNARY) {
            return validate_occurrence_type(validator, item, (TypeUnary*)fast_base);
        }
        if (fast_base->kind == TYPE_KIND_BINARY) {
            return validate_binary_type(validator, item, (TypeBinary*)fast_base);
        }
        if (fast_base == &TYPE_NUMBER) {
            return validation_verdict(IS_NUMERIC_ID(item.type_id()));
        }
        if (fast_base == &TYPE_INTEGER || IS_NUMERIC_ID(fast_base->type_id)) {
            return validation_verdict(validator_numeric_item_embeds(item, fast_base));
        }
        if (fast_base == &TYPE_MAP) {
            return validation_verdict(item.type_id() == LMD_TYPE_MAP);
        }
        if (fast_base == &TYPE_ELMT) {
            return validation_verdict(item.type_id() == LMD_TYPE_ELEMENT);
        }
        if (fast_base->type_id == LMD_TYPE_MAP) {
            return validate_against_map_type(validator, item, (TypeMap*)fast_base);
        }
        if (fast_base->type_id == LMD_TYPE_ELEMENT) {
            return validate_against_element_type(validator, item, (TypeElmt*)fast_base);
        }
        if (fast_base->kind != TYPE_KIND_PATTERN &&
                fast_base->type_id != LMD_TYPE_ARRAY && !fast_base->is_literal) {
            // plain nominal match; patterns/arrays/literals keep the full path
            return validation_verdict(fast_base->type_id == item.type_id());
        }
        // fall through to full mode for the remaining kinds
    }

    ValidationResult* result = create_validation_result(validator->get_pool());
    Type* base_type = type->type;

    // Safety check for null base_type
    if (!base_type) {
        log_error("[VALIDATOR] Base type is null in TypeType wrapper");
        result->valid = false;
        return result;
    }

    log_debug("[VALIDATOR] validate_against_base_type: base_type->type_id=%d, item type_id=%d",
              base_type->type_id, item.type_id());

    // Unwrap nested TypeType wrappers
    base_type = unwrap_type(base_type);

    if (!base_type) {
        log_error("[VALIDATOR] Base type is null after unwrapping");
        result->valid = false;
        return result;
    }

    // Handle 'any' type — matches everything except error
    if (base_type->type_id == LMD_TYPE_ANY) {
        result->valid = (item.type_id() != LMD_TYPE_ERROR);
        if (!result->valid) {
            add_type_mismatch_error(result, validator, "any", item.type_id());
        }
        return result;
    }

    // Handle TypeUnary (occurrence operators: ?, +, *, [n], [n+], [n,m])
    if (base_type->kind == TYPE_KIND_UNARY) {
        return validate_occurrence_type(validator, item, (TypeUnary*)base_type);
    }

    // Handle TypeBinary (union/intersection: |, &, \)
    if (base_type->kind == TYPE_KIND_BINARY) {
        return validate_binary_type(validator, item, (TypeBinary*)base_type);
    }

    // Handle Pattern type (string/symbol pattern)
    if (base_type->kind == TYPE_KIND_PATTERN) {
        return validate_against_pattern_type(validator, item, (TypePattern*)base_type);
    }

    if (base_type == &TYPE_NUMBER) {
        // `number` has no runtime tag; validation expands it to all concrete numeric tags.
        result->valid = IS_NUMERIC_ID(item.type_id());
        if (!result->valid) add_type_mismatch_error_ex(result, validator, base_type, item);
        return result;
    }
    if (base_type == &TYPE_INTEGER) {
        result->valid = validator_numeric_item_embeds(item, base_type);
        if (!result->valid) add_type_mismatch_error_ex(result, validator, base_type, item);
        return result;
    }

    // Handle numeric types with promotion
    if (IS_NUMERIC_ID(base_type->type_id)) {
        // Numeric validation follows the same exact-embedding lattice as `is`.
        if (validator_numeric_item_embeds(item, base_type)) {
            result->valid = true;
        } else {
            result->valid = false;
            add_type_mismatch_error_ex(result, validator, base_type, item);
        }
        return result;
    }

    // Handle compound types
    // Note: Must check for generic types (TYPE_MAP, TYPE_ELMT, TYPE_ARRAY) which are
    // simple Type structs, not TypeMap/TypeElmt/TypeArray. Casting them would read garbage.
    extern Type TYPE_MAP;
    extern Type TYPE_ELMT;
    extern TypeArray TYPE_ARRAY;

    if (base_type->type_id == LMD_TYPE_MAP) {
        if (base_type == &TYPE_MAP) {
            // Generic map type - just check if item is a map
            if (item.type_id() == LMD_TYPE_MAP) {
                result->valid = true;
            } else {
                add_type_mismatch_error(result, validator, "map", item.type_id());
            }
            return result;
        }
        return validate_against_map_type(validator, item, (TypeMap*)base_type);
    }
    if (base_type->type_id == LMD_TYPE_ELEMENT) {
        if (base_type == &TYPE_ELMT) {
            // Generic element type - just check if item is an element
            if (item.type_id() == LMD_TYPE_ELEMENT) {
                result->valid = true;
            } else {
                add_type_mismatch_error(result, validator, "element", item.type_id());
            }
            return result;
        }
        return validate_against_element_type(validator, item, (TypeElmt*)base_type);
    }
    if (base_type->type_id == LMD_TYPE_ARRAY) {
        extern Type TYPE_LIST;
        if (base_type == (Type*)&TYPE_ARRAY) {
            // Generic array type - just check if item is an array/list
            if (item.type_id() == LMD_TYPE_ARRAY ||
                item.type_id() == LMD_TYPE_ARRAY_NUM) {
                result->valid = true;
            } else {
                add_type_mismatch_error(result, validator, "array", item.type_id());
            }
            return result;
        }
        if (base_type == &TYPE_LIST) {
            // Generic list type - just check if item is a list
            result->valid = (item.type_id() == LMD_TYPE_ARRAY);
            if (!result->valid) {
                add_type_mismatch_error(result, validator, "list", item.type_id());
            }
            return result;
        }
        return validate_against_array_type(validator, item, (TypeArray*)base_type);
    }

    // Direct type match
    if (base_type->type_id == item.type_id()) {
        result->valid = true;
    } else {
        result->valid = false;
        add_type_mismatch_error_ex(result, validator, base_type, item);
    }
    return result;
}

ValidationResult* validate_against_array_type(SchemaValidator* validator, ConstItem item, TypeArray* array_type) {
    log_debug("[VALIDATOR] Validating array type");
    ValidationResult* result = create_validation_result(validator->get_pool());

    // Use MarkReader for type-safe access
    ItemReader item_reader(item);

    // Check if item is actually an array/list
    if (!item_reader.isArray() && !item_reader.isList()) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
            "Type mismatch: expected array/list, got type %d", item_reader.getType());
        add_validation_error(result, create_validation_error(
            AST_VALID_ERROR_TYPE_MISMATCH, error_msg, validator->get_current_path(), validator->get_pool()));
        return result;
    }

    // An N-D typed array's reader walks slab cursors, and a cursor's item() is
    // the whole base array, so the patterns below tested the matrix where
    // they meant a row. Its elements are its leading-axis rows (S11.1.1v3):
    // walk them as the Items they are, rooted, since a row view allocates.
    RootFrame roots(1);
    Rooted<Item> rows(roots, ItemNull);
    if (item.type_id() == LMD_TYPE_ARRAY_NUM && array_num_rank(item.array_num) > 1) {
        rows.set({.array = (Array*)ensure_typed_array({.item = item.item}, LMD_TYPE_ANY)});
        if (rows.get().array) item_reader = ItemReader(rows.get().to_const());
    }

    // Get ArrayReader for type-safe iteration
    ArrayReader array = item_reader.asArray();
    int64_t length = array.length();

    log_debug("Validating array with length: %ld", length);

    if (array_type->item_patterns) {
        validate_sequence_pattern(validator, result, array, length, array_type, "Array");
        if (result->error_count == 0) result->valid = true;
        return result;
    }

    // Check for occurrence operators on nested type
    // Nested type is TypeType* wrapping the actual type (which could be TypeUnary for occurrence operators)
    // We need to unwrap TypeType to check if it's a TypeUnary with occurrence operator
    log_debug("[AST_VALIDATOR] Checking array nested type at %p, type_id=%d",
              (void*)array_type->nested, array_type->nested ? array_type->nested->type_id : -1);

    if (array_type->nested && array_type->nested->type_id == LMD_TYPE_TYPE) {
        TypeType* type_wrapper = (TypeType*)array_type->nested;
        Type* unwrapped = type_wrapper->type;
        log_debug("[AST_VALIDATOR] Array nested is TypeType wrapper, unwrapped type at %p, type_id=%d",
                  (void*)unwrapped, unwrapped ? unwrapped->type_id : -1);

        // Check if unwrapped type is TypeUnary (occurrence operator)
        if (unwrapped && unwrapped->kind == TYPE_KIND_UNARY) {
            TypeUnary* possible_unary = (TypeUnary*)unwrapped;
            if (possible_unary->op == OPERATOR_ONE_MORE && length < 1) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                        "Array with '+' occurrence operator requires at least one element, got %lld", (long long)length);
                add_validation_error(result, create_validation_error(
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, validator->get_current_path(), validator->get_pool()));
                return result;
            }
            else if (possible_unary->op == OPERATOR_OPTIONAL && length > 1) {
                char error_msg[256];
                snprintf(error_msg, sizeof(error_msg),
                        "Array with '?' occurrence operator requires at most one element, got %lld", (long long)length);
                add_validation_error(result, create_validation_error(
                    AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, validator->get_current_path(), validator->get_pool()));
                return result;
            }
            // OPERATOR_ZERO_MORE (*) has no length constraint
        }
    }

    // Validate array length if specified
    // if (array_type->length >= 0 && length != array_type->length) {
    //     char error_msg[256];
    //     snprintf(error_msg, sizeof(error_msg),
    //             "Array length mismatch: expected %ld, got %ld",
    //             array_type->length, length);
    //     add_validation_error(result, create_validation_error(
    //         AST_VALID_ERROR_CONSTRAINT_VIOLATION, error_msg, validator->get_current_path(), validator->get_pool()));
    // }

    // T20-4. An ArrayNum IS its element representation: every slot is stored in
    // the packed lane its `elem_type` names, so walking the elements to learn
    // what the header already states cannot discover a disagreement. Without
    // this, admitting a `float[]` at a boundary costs O(n) -- cube3d crosses
    // `float[]` boundaries constantly and spent ~20% of its run inside this
    // loop, re-deriving a fact its storage guarantees. Same argument as the
    // map shape-identity short-circuit (Tune19 §7.13); fast mode only, because
    // the reporting validator owes a per-element diagnosis rather than a
    // verdict [D2.6.1, D3.2.1].
    if (validator->is_fast_mode() && array_type->nested && length > 0 &&
            item.type_id() == LMD_TYPE_ARRAY_NUM && item.array_num) {
        Type* nested = unwrap_type(array_type->nested);
        const ArrayNum* packed = item.array_num;
        // ndim and view layouts index through a stride table and may alias a
        // wider buffer, so only a plain packed array speaks for its elements.
        bool plain_layout = !packed->is_ndim && !packed->is_view;
        if (nested && nested->kind == TYPE_KIND_SIMPLE && plain_layout) {
            TypeId lane = LMD_TYPE_ERROR;
            switch (packed->get_elem_type()) {
            case ELEM_INT:     lane = LMD_TYPE_INT; break;
            case ELEM_FLOAT64: lane = LMD_TYPE_FLOAT; break;
            case ELEM_INT64:   lane = LMD_TYPE_INT64; break;
            default: break;  // compact sized lanes keep the element walk
            }
            if (lane != LMD_TYPE_ERROR && lane == nested->type_id) {
                log_debug("[VALIDATOR] array elem lane %d matches contract; skipping walk", (int)lane);
                result->valid = true;
                return result;
            }
        }
    }

    // Validate each array element against nested type using iterator
    if (array_type->nested && length > 0) {
        auto iter = array.items();
        ItemReader child;
        int64_t index = 0;

        while (iter.next(&child)) {
            PathScope scope(validator, index);

            // Recursively validate element
            log_debug("[VALIDATOR] Validating array item at index %ld", index);
            ConstItem child_item = child.item().to_const();
            ValidationResult* item_result = validate_against_type(
                validator, child_item, array_type->nested);

            // Merge validation results
            if (item_result && !item_result->valid) {
                merge_errors(result, item_result, validator);
            }

            index++;
        }
    }

    return result;
}

template <typename HasValueFn, typename GetValueFn>
static void validate_shape_entries(SchemaValidator* validator, ValidationResult* result,
                                   ShapeEntry* shape, HasValueFn has_value, GetValueFn get_value,
                                   bool report_missing, bool check_null, bool push_name_scope) {
    for (ShapeEntry* shape_entry = shape; shape_entry; shape_entry = shape_entry->next) {
        if (!shape_entry->name) {
            log_error("[VALIDATOR] ShapeEntry has NULL name pointer");
            continue;
        }
        const char* field_name = shape_entry->name->str;

        if (push_name_scope) {
            PathScope scope(validator, *shape_entry->name);
            bool field_exists = has_value(field_name);
            if (!field_exists) {
                if (report_missing && !is_type_optional(shape_entry->type)) {
                    add_missing_field_error(result, validator, field_name);
                    result->valid = false;
                }
                continue;
            }

            ItemReader field_value = get_value(field_name);
            ConstItem field_item = field_value.item().to_const();
            if (check_null && field_item.type_id() == LMD_TYPE_NULL) {
                if (!type_admits_null_value(shape_entry->type)) {
                    add_null_value_error(result, validator, field_name);
                    result->valid = false;
                }
                continue;
            }

            log_debug("[VALIDATOR] Validating shape field '%s'", field_name);
            ValidationResult* field_result = validate_against_type(validator, field_item, shape_entry->type);
            if (field_result && !field_result->valid) {
                merge_errors(result, field_result, validator);
            }
            continue;
        }

        bool field_exists = has_value(field_name);
        if (!field_exists) {
            if (report_missing && !is_type_optional(shape_entry->type)) {
                add_missing_field_error(result, validator, field_name);
                result->valid = false;
            }
            continue;
        }

        ItemReader field_value = get_value(field_name);
        ConstItem field_item = field_value.item().to_const();
        if (check_null && field_item.type_id() == LMD_TYPE_NULL) {
            if (!type_admits_null_value(shape_entry->type)) {
                add_null_value_error(result, validator, field_name);
                result->valid = false;
            }
            continue;
        }

        log_debug("[VALIDATOR] Validating shape field '%s'", field_name);
        ValidationResult* field_result = validate_against_type(validator, field_item, shape_entry->type);
        if (field_result && !field_result->valid) {
            merge_errors(result, field_result, validator);
        }
    }
}

// Fast-mode shape walk: the same field rules as validate_shape_entries, as a
// predicate. No result to populate means no PathScope bookkeeping and no
// merge, and it stops at the first bad field — full mode may never do that,
// because it owes an error for every field.
template <typename HasValueFn, typename GetValueFn>
static bool shape_entries_match_fast(SchemaValidator* validator, ShapeEntry* shape,
                                     HasValueFn has_value, GetValueFn get_value,
                                     bool report_missing, bool check_null) {
    for (ShapeEntry* shape_entry = shape; shape_entry; shape_entry = shape_entry->next) {
        if (!shape_entry->name) continue;   // full mode logs; the verdict is unaffected
        const char* field_name = shape_entry->name->str;

        if (!has_value(field_name)) {
            if (report_missing && !is_type_optional(shape_entry->type)) return false;
            continue;
        }
        ItemReader field_value = get_value(field_name);
        ConstItem field_item = field_value.item().to_const();
        if (check_null && field_item.type_id() == LMD_TYPE_NULL) {
            if (!type_admits_null_value(shape_entry->type)) return false;
            continue;
        }
        ValidationResult* field_result = validate_against_type(validator, field_item, shape_entry->type);
        if (!field_result || !field_result->valid) return false;
    }
    return true;
}

// A map that already carries this exact TypeMap was BUILT to this shape: its
// slots are that shape's slots, so walking its fields cannot discover a
// disagreement its construction did not already prevent.
//
// Without this the cost is structural and unbounded for a RECURSIVE contract.
// `type SplayNode = {key: float, left: SplayNode?, ...}` makes every declared
// boundary crossing re-walk the whole reachable structure, so an O(1) admission
// becomes O(n) and the workload becomes O(n^2) -- measured 0.63s / 1.94s / 6.44s
// / 38.0s at tree sizes 800 / 1600 / 3200 / 8000, against ~0.25s for the whole
// benchmark when the same field is declared as a bare `map?`. That cost is why
// the splay benchmark declares its links `map?` and cannot say what it means.
//
// Scoped to fast mode on purpose: that is the runtime admission predicate
// (lambda_type_matches). The reporting validator still walks, because it owes a
// per-field diagnosis rather than a verdict (D3.2.1, D4.6.1v2).
static bool map_carries_exact_shape(ConstItem item, const TypeMap* map_type) {
    if (!map_type || !item.item) return false;
    TypeId tid = item.type_id();
    const void* actual = tid == LMD_TYPE_MAP ? (const void*)((Map*)item.map)->type
        : NULL;
    return actual && actual == (const void*)map_type;
}

ValidationResult* validate_against_map_type(SchemaValidator* validator, ConstItem item, TypeMap* map_type) {
    if (validator->is_fast_mode()) {
        ItemReader fast_reader(item);
        if (!fast_reader.isMap()) return validation_verdict(false);
        if (!map_type->shape) return validation_verdict(true);
        if (map_carries_exact_shape(item, map_type)) return validation_verdict(true);
        MapReader fast_map = fast_reader.asMap();
        return validation_verdict(shape_entries_match_fast(validator, map_type->shape,
            [&fast_map](const char* name) { return fast_map.has(name); },
            [&fast_map](const char* name) { return fast_map.get(name); },
            true, true));
    }

    ValidationResult* result = create_validation_result(validator->get_pool());

    log_debug("[VALIDATOR] validate_against_map_type: item.item=0x%016lx map_type=%p",
        (unsigned long)item.item, map_type);
    // Check if item is actually a map using ItemReader
    ItemReader item_reader(item);
    if (!item_reader.isMap()) {
        add_type_mismatch_error(result, validator, "map", item.type_id());
        return result;
    }

    // If this is a generic map type (no shape defined), just check if item is a map
    if (!map_type->shape) {
        result->valid = true;
        return result;
    }

    // Use MapReader for type-safe access
    MapReader map = item_reader.asMap();

    validate_shape_entries(validator, result, map_type->shape,
        [&map](const char* name) { return map.has(name); },
        [&map](const char* name) { return map.get(name); },
        true, true, true);

    return result;
}

ValidationResult* validate_against_element_type(SchemaValidator* validator, ConstItem item, TypeElmt* element_type) {
    if (validator->is_fast_mode()) {
        ItemReader fast_reader(item);
        if (!fast_reader.isElement()) return validation_verdict(false);
        ElementReader fast_elem = fast_reader.asElement();
        if (element_type->name.length > 0 && !fast_elem.hasTag(element_type->name.str)) {
            return validation_verdict(false);
        }
        TypeMap* fast_map_part = (TypeMap*)element_type;
        if (fast_map_part->shape &&
                !shape_entries_match_fast(validator, fast_map_part->shape,
                    [&fast_elem](const char* name) { return fast_elem.has_attr(name); },
                    [&fast_elem](const char* name) { return fast_elem.get_attr(name); },
                    false, false)) {
            return validation_verdict(false);
        }
        // S11.1.6v3: the declared content pattern is a sequence-pattern slot;
        // the run matcher also decides a run-free pattern exactly
        if (element_type->content_list) {
            ArrayReader children((Array*)fast_elem.element());
            if (!array_pattern_runs_match(validator, children, fast_elem.childCount(),
                    element_type->content_list, 0, 0)) {
                return validation_verdict(false);
            }
        }
        return validation_verdict(true);
    }

    ValidationResult* result = create_validation_result(validator->get_pool());

    // Check if item is actually an element
    ItemReader item_reader(item);
    if (!item_reader.isElement()) {
        add_type_mismatch_error(result, validator, "element", item.type_id());
        return result;
    }

    ElementReader element = item_reader.asElement();

    // Validate element name if specified
    if (element_type->name.length > 0) {
        const char* expected_tag = element_type->name.str;

        if (!element.hasTag(expected_tag)) {
            PathScope scope(validator, PATH_ELEMENT, element_type->name);
            add_constraint_error_fmt(result, validator,
                "Element tag mismatch: expected '%.*s', got '%s'",
                (int)element_type->name.length, expected_tag, element.tagName());
        }

        log_debug("[VALIDATOR] Validating element with tag '%.*s'",
                (int)element_type->name.length, expected_tag);
    }

    // TypeElmt inherits from TypeMap, so we can validate attributes as map fields
    TypeMap* map_part = (TypeMap*)element_type;
    if (map_part->shape) {
        PathScope attr_scope(validator, PATH_ATTRIBUTE, (StrView){"attrs", 5});

        validate_shape_entries(validator, result, map_part->shape,
            [&element](const char* name) { return element.has_attr(name); },
            [&element](const char* name) { return element.get_attr(name); },
            false, false, false);
    }

    // S11.1.6v3: match the children against the declared content pattern
    if (element_type->content_list) {
        PathScope scope(validator, PATH_ELEMENT, (StrView){"content", 7});
        ArrayReader children((Array*)element.element());
        validate_sequence_pattern(validator, result, children, element.childCount(),
            element_type->content_list, "Element content");
    }

    return result;
}

// ==================== Main Validation Dispatcher ====================

ValidationResult* validate_against_type(SchemaValidator* validator, ConstItem item, Type* type) {
    if (!validator || !type) {
        ValidationResult* result = create_validation_result(validator ? validator->get_pool() : nullptr);
        add_constraint_error(result, validator, "Invalid validation parameters");
        return result;
    }

    // Check for timeout
    if (should_stop_for_timeout(validator)) {
        ValidationResult* result = create_validation_result(validator->get_pool());
        add_constraint_error(result, validator, "Validation timeout exceeded");
        return result;
    }

    // Check validation depth
    if (validator->get_current_depth() >= validator->get_options()->max_depth) {
        ValidationResult* result = create_validation_result(validator->get_pool());
        add_constraint_error(result, validator, "Maximum validation depth exceeded");
        return result;
    }

    // Use RAII for depth tracking
    DepthScope depth_scope(validator);

    log_debug("[VALIDATOR] Validating type_id=%d against item type_id=%d", type->type_id, item.type_id());

    ValidationResult* result = nullptr;

    switch (type->type_id) {
        case LMD_TYPE_STRING:
        case LMD_TYPE_SYMBOL:
        case LMD_TYPE_INT:
        case LMD_TYPE_FLOAT:
        case LMD_TYPE_BOOL:
        case LMD_TYPE_NULL:
        // PTH30: `path` is a surface scalar type now (`x is path`, `path` in a
        // schema), so a bare TYPE_PATH must validate by tag like every other
        // scalar instead of falling into the "unsupported type" arm below.
        case LMD_TYPE_PATH:
            result = validate_against_primitive_type(validator, item, type);
            break;

        case LMD_TYPE_ARRAY:
            result = validate_against_array_type(validator, item, (TypeArray*)type);
            break;

        case LMD_TYPE_MAP: {
            extern Type TYPE_MAP;
            if (type == &TYPE_MAP) {
                // Generic map type - just check if item is a map
                result = create_validation_result(validator->get_pool());
                if (item.type_id() == LMD_TYPE_MAP) {
                    result->valid = true;
                } else {
                    add_type_mismatch_error(result, validator, "map", item.type_id());
                }
            } else {
                result = validate_against_map_type(validator, item, (TypeMap*)type);
            }
            break;
        }

        case LMD_TYPE_ELEMENT: {
            extern Type TYPE_ELMT;
            if (type == &TYPE_ELMT) {
                // Generic element type - just check if item is an element
                result = create_validation_result(validator->get_pool());
                if (item.type_id() == LMD_TYPE_ELEMENT) {
                    result->valid = true;
                } else {
                    add_type_mismatch_error(result, validator, "element", item.type_id());
                }
            } else {
                result = validate_against_element_type(validator, item, (TypeElmt*)type);
            }
            break;
        }

        case LMD_TYPE_TYPE:
            // Dispatch on kind for type variants
            if (type->kind == TYPE_KIND_UNARY) {
                result = validate_occurrence_type(validator, item, (TypeUnary*)type);
            } else if (type->kind == TYPE_KIND_BINARY) {
                result = validate_binary_type(validator, item, (TypeBinary*)type);
            } else if (type->kind == TYPE_KIND_PATTERN) {
                result = validate_against_pattern_type(validator, item, (TypePattern*)type);
            } else {
                result = validate_against_base_type(validator, item, (TypeType*)type);
            }
            break;

        default:
            result = create_validation_result(validator->get_pool());
            add_constraint_error_fmt(result, validator, "Unsupported type for validation: %d", type->type_id);
            break;
    }

    // Check if we should stop due to max_errors
    if (should_stop_for_max_errors(result, validator->get_options()->max_errors)) {
        return result;
    }

    return result;
}
