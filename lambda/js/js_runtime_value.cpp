#include "js_runtime_internal.hpp"

extern "C" bool js_ordinary_has_property(Item object, const char* name, int name_len);
extern "C" bool js_dom_item_is_range(Item item);
extern "C" bool js_dom_item_is_selection(Item item);
extern "C" Item js_dom_range_to_string_value(Item item);
extern "C" Item js_dom_selection_to_string_value(Item item);
extern "C" void* js_dom_unwrap_element(Item item);
extern "C" bool js_is_proxy(Item obj);

static inline bool js_number_like_type(TypeId type) {
    return type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT ||
           type == LMD_TYPE_FLOAT64 || type == LMD_TYPE_NUM_SIZED;
}

static double js_sized_number_to_double(Item value) {
    switch (value.get_num_type()) {
    case NUM_INT8:    return (double)item_to_i8(value.item);
    case NUM_INT16:   return (double)item_to_i16(value.item);
    case NUM_INT32:   return (double)item_to_i32(value.item);
    case NUM_UINT8:   return (double)item_to_u8(value.item);
    case NUM_UINT16:  return (double)item_to_u16(value.item);
    case NUM_UINT32:  return (double)item_to_u32(value.item);
    case NUM_FLOAT16: return (double)item_to_f16(value.item);
    case NUM_FLOAT32: return (double)item_to_f32(value.item);
    default:          return NAN;
    }
}

// =============================================================================
// Type Conversion Functions
// =============================================================================

extern "C" Item js_to_number(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        // null -> 0, undefined -> NaN
        if (type == LMD_TYPE_UNDEFINED) {
            return js_make_number(NAN);
        }
        return js_make_number(0.0);

    case LMD_TYPE_BOOL: {
        int val = it2b(value) ? 1 : 0;
        return js_make_number((double)val);
    }

    case LMD_TYPE_INT:
        if (js_is_symbol(value)) {
            js_throw_type_error("Cannot convert a Symbol value to a number");
            return ItemNull;
        }
        return js_make_number((double)it2i(value));
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
        return value;
    case LMD_TYPE_NUM_SIZED:
        return js_make_number(js_sized_number_to_double(value));

    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        if (!str || str->len == 0) {
            return js_make_number(0.0);  // Empty string -> 0
        }
        // v20: Trim whitespace before parsing (ES spec: WhiteSpace + LineTerminator)
        // Includes full Unicode StrWhiteSpaceChar set
        const char* start = str->chars;
        const char* end = str->chars + str->len;
        // Trim leading whitespace
        while (start < end) {
            unsigned char c = (unsigned char)*start;
            // ASCII whitespace
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') { start++; continue; }
            // 2-byte: U+00A0 (NBSP)
            if (c == 0xC2 && start + 1 < end && (unsigned char)start[1] == 0xA0) { start += 2; continue; }
            // 3-byte sequences
            if (c == 0xE1 && start + 2 < end && (unsigned char)start[1] == 0x9A && (unsigned char)start[2] == 0x80) { start += 3; continue; } // U+1680
            if (c == 0xE2 && start + 2 < end) {
                unsigned char b1 = (unsigned char)start[1], b2 = (unsigned char)start[2];
                if (b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A) { start += 3; continue; } // U+2000-U+200A
                if (b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9)) { start += 3; continue; } // U+2028, U+2029
                if (b1 == 0x80 && b2 == 0xAF) { start += 3; continue; } // U+202F
                if (b1 == 0x81 && b2 == 0x9F) { start += 3; continue; } // U+205F
            }
            if (c == 0xE3 && start + 2 < end && (unsigned char)start[1] == 0x80 && (unsigned char)start[2] == 0x80) { start += 3; continue; } // U+3000
            if (c == 0xEF && start + 2 < end && (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF) { start += 3; continue; } // U+FEFF
            break;
        }
        // Trim trailing whitespace
        while (end > start) {
            unsigned char c = (unsigned char)*(end - 1);
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') { end--; continue; }
            if (c == 0xA0 && end - 1 > start && (unsigned char)*(end - 2) == 0xC2) { end -= 2; continue; } // U+00A0
            if (c == 0x80 && end - 2 > start) {
                unsigned char p2 = (unsigned char)*(end - 3), p1 = (unsigned char)*(end - 2);
                if (p2 == 0xE1 && p1 == 0x9A) { end -= 3; continue; } // U+1680
                if (p2 == 0xE3 && p1 == 0x80) { end -= 3; continue; } // U+3000
            }
            if (end - 2 > start && (unsigned char)*(end - 3) == 0xE2) {
                unsigned char p1 = (unsigned char)*(end - 2);
                if (p1 == 0x80) {
                    if (c >= 0x80 && c <= 0x8A) { end -= 3; continue; } // U+2000-U+200A
                    if (c == 0xA8 || c == 0xA9) { end -= 3; continue; } // U+2028, U+2029
                    if (c == 0xAF) { end -= 3; continue; } // U+202F
                }
                if (p1 == 0x81 && c == 0x9F) { end -= 3; continue; } // U+205F
            }
            if (c == 0xBF && end - 2 > start && (unsigned char)*(end - 3) == 0xEF && (unsigned char)*(end - 2) == 0xBB) { end -= 3; continue; } // U+FEFF
            break;
        }
        if (start == end) {
            return js_make_number(0.0);  // Whitespace-only string -> 0
        }
        // Copy trimmed portion to null-terminated buffer for parsing
        int trimmed_len = (int)(end - start);
        char buf[128];
        if (trimmed_len < (int)sizeof(buf)) {
            memcpy(buf, start, trimmed_len);
            buf[trimmed_len] = '\0';
        } else {
            // Fallback for very long strings
            memcpy(buf, start, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
        }
        // v20: Handle binary (0b/0B) and octal (0o/0O) literals
        if (trimmed_len > 2 && buf[0] == '0' && (buf[1] == 'b' || buf[1] == 'B')) {
            char* bp = buf + 2;
            long long val = 0;
            while (*bp == '0' || *bp == '1') { val = val * 2 + (*bp - '0'); bp++; }
            if (*bp != '\0' || bp == buf + 2) {
                double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *nan_ptr = NAN;
                return (Item){.item = d2it(nan_ptr)};
            }
            double* result = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *result = (double)val;
            return (Item){.item = d2it(result)};
        }
        if (trimmed_len > 2 && buf[0] == '0' && (buf[1] == 'o' || buf[1] == 'O')) {
            char* op = buf + 2;
            long long val = 0;
            while (*op >= '0' && *op <= '7') { val = val * 8 + (*op - '0'); op++; }
            if (*op != '\0' || op == buf + 2) {
                double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *nan_ptr = NAN;
                return (Item){.item = d2it(nan_ptr)};
            }
            double* result = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *result = (double)val;
            return (Item){.item = d2it(result)};
        }
        // v29: Handle hex (0x/0X) literals explicitly — ES spec does not allow
        // a sign prefix on hex literals. strtod() on some platforms (macOS) accepts
        // "+0x10" which violates the spec. Parse hex ourselves.
        if (trimmed_len > 2 && buf[0] == '0' && (buf[1] == 'x' || buf[1] == 'X')) {
            char* hp = buf + 2;
            long long val = 0;
            bool has_digits = false;
            while ((*hp >= '0' && *hp <= '9') || (*hp >= 'a' && *hp <= 'f') || (*hp >= 'A' && *hp <= 'F')) {
                int d;
                if (*hp >= '0' && *hp <= '9') d = *hp - '0';
                else if (*hp >= 'a' && *hp <= 'f') d = *hp - 'a' + 10;
                else d = *hp - 'A' + 10;
                val = val * 16 + d;
                has_digits = true;
                hp++;
            }
            if (*hp != '\0' || !has_digits) {
                double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *nan_ptr = NAN;
                return (Item){.item = d2it(nan_ptr)};
            }
            double* result = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *result = (double)val;
            return (Item){.item = d2it(result)};
        }
        // v29: Reject signed hex/octal/binary — strtod might accept "+0x..." on some platforms
        if (trimmed_len > 3 && (buf[0] == '+' || buf[0] == '-') && buf[1] == '0' &&
            (buf[2] == 'x' || buf[2] == 'X' || buf[2] == 'b' || buf[2] == 'B' ||
             buf[2] == 'o' || buf[2] == 'O')) {
            double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *nan_ptr = NAN;
            return (Item){.item = d2it(nan_ptr)};
        }
        // ES spec: only "Infinity" (exact case) is valid; strtod accepts case-insensitive
        if (trimmed_len >= 3 && (buf[0] == 'i' || buf[0] == 'I')) {
            if (strncmp(buf, "Infinity", 8) != 0) {
                return js_make_number(NAN);
            }
        }
        if (trimmed_len >= 4 && (buf[0] == '+' || buf[0] == '-') && (buf[1] == 'i' || buf[1] == 'I')) {
            if (strncmp(buf + 1, "Infinity", 8) != 0) {
                return js_make_number(NAN);
            }
        }
        char* endptr;
        double num = strtod(buf, &endptr);
        // Check that ALL trimmed characters were consumed
        if (endptr == buf || *endptr != '\0') {
            // Not a valid number — NaN
            return js_make_number(NAN);
        }
        return js_make_number(num);
    }

    case LMD_TYPE_DECIMAL: {
        // ES spec: ToNumber(bigint) throws TypeError
        Decimal* _dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFF);
        if (_dec && _dec->unlimited == DECIMAL_BIGINT) {
            js_throw_type_error("Cannot convert a BigInt value to a number");
            return ItemNull;
        }
        // regular decimal → float
        return push_d(decimal_to_double(value));
    }

    case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64:
        js_throw_type_error("Cannot convert a BigInt value to a number");
        return ItemNull;

    default:
        // J39-1b: route object operands through the unified js_to_primitive
        // kernel (ES §7.1.1). Returned primitive is then re-coerced via ToNumber.
        if (type == LMD_TYPE_MAP || type == LMD_TYPE_ELEMENT) {
            if (type == LMD_TYPE_MAP && value.map) {
                bool raw_proto_found = false;
                Item raw_proto = js_map_get_fast(value.map, "__proto__", 9, &raw_proto_found);
                if (raw_proto_found && raw_proto.item == ITEM_JS_UNDEFINED) {
                    bool has_vo = false, has_ts = false, has_tp = false;
                    js_map_get_fast(value.map, "valueOf", 7, &has_vo);
                    js_map_get_fast(value.map, "toString", 8, &has_ts);
                    js_map_get_fast(value.map, "__sym_2", 7, &has_tp);
                    if (!has_vo && !has_ts && !has_tp) {
                        js_throw_type_error("Cannot convert object to primitive value");
                        return ItemNull;
                    }
                }
            }
            Item prim = js_to_primitive(value, JS_HINT_NUMBER);
            if (js_check_exception()) return ItemNull;
            TypeId rt = get_type_id(prim);
            // ES spec: ToNumber(symbol) throws TypeError
            if (rt == LMD_TYPE_INT && it2i(prim) <= -(int64_t)JS_SYMBOL_BASE) {
                js_throw_type_error("Cannot convert a Symbol value to a number");
                return ItemNull;
            }
            return js_to_number(prim);
        }
        // Arrays: ToPrimitive → toString → ToNumber (e.g. +[] → +"" → 0, +[1] → +"1" → 1)
        if (type == LMD_TYPE_ARRAY) {
            Item str = js_to_string(value);
            return js_to_number(str);
        }
        // Objects, arrays, etc. -> NaN
        double* nan_ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *nan_ptr = NAN;
        return (Item){.item = d2it(nan_ptr)};
    }
}

// ES spec §7.1.3 ToNumeric(value) — like ToNumber but preserves BigInt
// Used by increment/decrement (++/--) which must work on BigInt values.
extern "C" Item js_to_numeric(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_DECIMAL) {
        Decimal* _dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFF);
        if (_dec && _dec->unlimited == DECIMAL_BIGINT) return value;
    }
    if (js_is_native_bigint_egress(value)) return js_native_bigint_to_bigint(value);
    // ES spec: Symbol → TypeError in ToNumeric (§7.1.3)
    // Symbols are encoded as LMD_TYPE_INT with value <= -(int64_t)JS_SYMBOL_BASE
    if (type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return ItemNull;
    }
    // ToPrimitive for objects (hint: number) — ES spec §7.1.3
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY ||
        type == LMD_TYPE_FUNC || type == LMD_TYPE_ELEMENT) {
        // J39-1b: route through unified js_to_primitive (ES §7.1.1).
        Item prim = js_to_primitive(value, JS_HINT_NUMBER);
        if (js_check_exception()) return ItemNull;
        TypeId rt = get_type_id(prim);
        // ES spec: ToNumeric(symbol) throws TypeError
        if (rt == LMD_TYPE_INT && it2i(prim) <= -(int64_t)JS_SYMBOL_BASE) {
            js_throw_type_error("Cannot convert a Symbol value to a number");
            return ItemNull;
        }
        return js_to_numeric(prim);
    }
    return js_to_number(value);
}

// helper: apply ToNumeric only when operand is an object type
static inline Item js_numeric_operand(Item val) {
    TypeId t = get_type_id(val);
    if (t == LMD_TYPE_MAP || t == LMD_TYPE_ARRAY || t == LMD_TYPE_FUNC || t == LMD_TYPE_ELEMENT) return js_to_numeric(val);
    if (js_is_native_bigint_egress(val)) return js_native_bigint_to_bigint(val);
    return val;
}

// ES spec §7.1.12.1 Number::toString
// Converts a double to its JavaScript string representation.
// - Uses shortest representation that round-trips
// - No scientific notation for exponents in [-6, 20]
// - Scientific notation uses 'e+' or 'e-' (no leading zeros in exponent)
void js_double_to_string(double d, char* out, int out_size) {
    if (isnan(d)) {
        snprintf(out, out_size, "NaN");
        return;
    }
    if (isinf(d)) {
        snprintf(out, out_size, "%sInfinity", d < 0 ? "-" : "");
        return;
    }
    if (d == 0.0) {
        snprintf(out, out_size, "0");
        return;
    }

    // Handle negative numbers
    int neg = 0;
    if (d < 0) { neg = 1; d = -d; }

    // Try increasing precision to find shortest round-trip representation
    char buf[64];
    int best_len = 0;
    for (int prec = 1; prec <= 21; prec++) {
        snprintf(buf, sizeof(buf), "%.*e", prec - 1, d);
        double roundtrip;
        sscanf(buf, "%lf", &roundtrip);
        if (roundtrip == d) {
            best_len = prec;
            break;
        }
    }
    if (best_len == 0) best_len = 17; // fallback: 17 digits always round-trips

    // Format with the minimal precision in scientific notation
    snprintf(buf, sizeof(buf), "%.*e", best_len - 1, d);

    // Parse the scientific notation: digits, decimal point, exponent
    // Format from snprintf: [-]d.dddde[+-]dd
    char digits[32];
    int digit_count = 0;
    int exp_val = 0;

    char* p = buf;
    // Skip sign (we handle separately)
    if (*p == '-') p++;

    // Collect digits (skip decimal point)
    while (*p && *p != 'e' && *p != 'E') {
        if (*p != '.') {
            digits[digit_count++] = *p;
        }
        p++;
    }
    digits[digit_count] = '\0';

    // Remove trailing zeros from digit string
    while (digit_count > 1 && digits[digit_count - 1] == '0') {
        digit_count--;
        digits[digit_count] = '\0';
    }

    // Parse exponent
    if (*p == 'e' || *p == 'E') {
        p++;
        exp_val = atoi(p);
    }

    // n = number of significant digits (k in ES spec)
    int k = digit_count;
    // e = exponent such that value = 0.digits * 10^e  =>  e = exp_val + 1
    int e = exp_val + 1;

    // Now format according to ES spec §7.1.12.1
    char* o = out;
    if (neg) *o++ = '-';

    if (k <= e && e <= 21) {
        // Case: integer-like, e.g. 120, 1000000
        // digits followed by (e-k) zeros
        memcpy(o, digits, k);
        o += k;
        for (int i = 0; i < e - k; i++) *o++ = '0';
        *o = '\0';
    } else if (0 < e && e <= 21) {
        // Case: decimal point within digits, e.g. 1.5, 12.34
        // first e digits, then '.', then remaining digits
        memcpy(o, digits, e);
        o += e;
        *o++ = '.';
        memcpy(o, digits + e, k - e);
        o += (k - e);
        *o = '\0';
    } else if (-6 < e && e <= 0) {
        // Case: 0.00...0digits, e.g. 0.5, 0.001
        *o++ = '0';
        *o++ = '.';
        for (int i = 0; i < -e; i++) *o++ = '0';
        memcpy(o, digits, k);
        o += k;
        *o = '\0';
    } else if (k == 1) {
        // Scientific notation with single digit
        *o++ = digits[0];
        *o++ = 'e';
        if (e - 1 >= 0) *o++ = '+';
        snprintf(o, out_size - (int)(o - out), "%d", e - 1);
    } else {
        // Scientific notation with multiple digits
        *o++ = digits[0];
        *o++ = '.';
        memcpy(o, digits + 1, k - 1);
        o += (k - 1);
        *o++ = 'e';
        if (e - 1 >= 0) *o++ = '+';
        snprintf(o, out_size - (int)(o - out), "%d", e - 1);
    }
}

extern "C" Item js_to_string(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
        return (Item){.item = s2it(heap_create_name("null"))};

    case LMD_TYPE_UNDEFINED:
        return (Item){.item = s2it(heap_create_name("undefined"))};

    case LMD_TYPE_BOOL:
        return (Item){.item = s2it(heap_create_name(it2b(value) ? "true" : "false"))};

    case LMD_TYPE_INT: {
        int64_t v = it2i(value);
        // Symbols cannot be implicitly converted to string (ES spec 7.1.12)
        if (v <= -(int64_t)JS_SYMBOL_BASE) {
            js_throw_type_error("Cannot convert a Symbol value to a string");
            return ItemNull;
        }
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%lld", (long long)v);
        return (Item){.item = s2it(heap_create_name(buffer))};
    }

    case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64: {
        Item bi = js_native_bigint_to_bigint(value);
        if (bi.item == ItemError.item) return ItemNull;
        return js_to_string(bi);
    }

    case LMD_TYPE_DECIMAL: {
        Decimal* _dec = (Decimal*)(value.item & 0x00FFFFFFFFFFFFFF);
        if (_dec && _dec->unlimited == DECIMAL_BIGINT) {
            char* s = bigint_to_cstring_radix(value, 10);
            if (!s) return ItemNull;
            Item result = (Item){.item = s2it(heap_create_name(s))};
            mem_free(s);
            return result;
        }
        // regular decimal
        char* s = decimal_to_string(value);
        Item result = (Item){.item = s2it(heap_create_name(s))};
        decimal_free_string(s);
        return result;
    }

    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        if (isnan(d)) {
            return (Item){.item = s2it(heap_create_name("NaN"))};
        } else if (isinf(d)) {
            return (Item){.item = s2it(heap_create_name(d > 0 ? "Infinity" : "-Infinity"))};
        } else if (d == 0.0) {
            // ES spec: Number::toString(-0) and +0 both return "0"
            return (Item){.item = s2it(heap_create_name("0"))};
        } else {
            char buffer[64];
            js_double_to_string(d, buffer, sizeof(buffer));
            return (Item){.item = s2it(heap_create_name(buffer))};
        }
    }

    case LMD_TYPE_STRING:
        return value;

    case LMD_TYPE_ARRAY: {
        Item to_string_key = (Item){.item = s2it(heap_create_name("toString", 8))};
        Item to_string_fn = js_property_get(value, to_string_key);
        if (js_check_exception()) return ItemNull;
        if (get_type_id(to_string_fn) == LMD_TYPE_FUNC) {
            Item result = js_call_function(to_string_fn, value, NULL, 0);
            if (js_check_exception()) return ItemNull;
            TypeId result_type = get_type_id(result);
            if (result_type == LMD_TYPE_MAP || result_type == LMD_TYPE_ARRAY ||
                result_type == LMD_TYPE_FUNC || result_type == LMD_TYPE_ELEMENT) {
                js_throw_type_error("Cannot convert object to primitive value");
                return ItemNull;
            }
            return js_to_string(result);
        }
        // JS: String([1,2,3]) => "1,2,3" (same as Array.prototype.join(","))
        Array* a = value.array;
        if (!a || a->length == 0) {
            return (Item){.item = s2it(heap_create_name(""))};
        }
        StrBuf* sb = strbuf_new();
        for (int i = 0; i < a->length; i++) {
            if (i > 0) strbuf_append_str_n(sb, ",", 1);
            TypeId etype = get_type_id(a->items[i]);
            if (etype != LMD_TYPE_NULL && etype != LMD_TYPE_UNDEFINED && a->items[i].item != JS_DELETED_SENTINEL_VAL) {
                Item elem_str = js_to_string(a->items[i]);
                String* s = it2s(elem_str);
                if (s && s->len > 0) {
                    strbuf_append_str_n(sb, s->chars, (int)s->len);
                }
            }
        }
        String* result = heap_create_name(sb->str, sb->length);
        strbuf_free(sb);
        return (Item){.item = s2it(result)};
    }

    case LMD_TYPE_MAP: {
        // v16: Check for Symbol.toPrimitive first (prototype chain lookup)
        {
            Item sym_key = (Item){.item = s2it(heap_create_name("__sym_2", 7))};
            Item to_prim = js_property_get(value, sym_key);
            if (js_check_exception()) return (Item){.item = s2it(heap_create_name(""))};
            TypeId tp_type = get_type_id(to_prim);
            bool tp_present = (to_prim.item != ItemNull.item && tp_type != LMD_TYPE_UNDEFINED && tp_type != LMD_TYPE_NULL);
            // ES spec §7.1.1 step 2.b.i: If exoticToPrim is not undefined AND not callable, throw TypeError.
            if (tp_present && tp_type != LMD_TYPE_FUNC) {
                js_throw_type_error("@@toPrimitive is not a function");
                return (Item){.item = s2it(heap_create_name(""))};
            }
            if (tp_present) {
                Item hint = (Item){.item = s2it(heap_create_name("string", 6))};
                Item args[1] = { hint };
                Item result = js_call_function(to_prim, value, args, 1);
                if (js_check_exception()) return ItemNull;
                if (get_type_id(result) == LMD_TYPE_STRING) return result;
                // Per ES spec: if Symbol.toPrimitive returns an Object, throw TypeError
                TypeId rtid = get_type_id(result);
                if (rtid == LMD_TYPE_MAP || rtid == LMD_TYPE_ARRAY || rtid == LMD_TYPE_FUNC || rtid == LMD_TYPE_ELEMENT) {
                    js_throw_type_error("Cannot convert object to primitive value");
                    return (Item){.item = s2it(heap_create_name(""))};
                }
                return js_to_string(result);
            }
        }
        if (js_dom_item_is_selection(value)) {
            return js_dom_selection_to_string_value(value);
        }
        if (js_dom_item_is_range(value)) {
            return js_dom_range_to_string_value(value);
        }
        // Check for Date objects via the typed JsClass tag (A3-T4).
        if (js_class_id(value) == JS_CLASS_DATE) {
            // delegate to js_date_method(obj, 17=toString)
            return js_date_method(value, 17);
        }
        bool bigint_wrapper = false;
        // Wrapper objects with __primitiveValue__ (e.g. new Number(42), new String("hi"))
        // Skip fast path if custom toString/valueOf/@@toPrimitive exists on the object
        {
            bool own_pv = false;
            Item pv = js_map_get_fast(value.map, "__primitiveValue__", 18, &own_pv);
            bigint_wrapper = own_pv && js_is_bigint(pv);
            if (own_pv && !js_is_bigint(pv) && !js_is_symbol(pv)) {
                bool has_own_vo = false, has_own_ts = false, has_own_tp = false;
                js_map_get_fast(value.map, "valueOf", 7, &has_own_vo);
                js_map_get_fast(value.map, "toString", 8, &has_own_ts);
                js_map_get_fast(value.map, "__sym_2", 7, &has_own_tp);
                if (!has_own_vo && !has_own_ts && !has_own_tp) {
                    return js_to_string(pv);
                }
            }
        }
        // Check for regex objects (have __rd hidden property)
        // JS: String(/pattern/flags) => "/pattern/flags"
        {
            bool own_rd = false;
            js_map_get_fast(value.map, "__rd", 4, &own_rd);
            bool own_to_string = false;
            js_map_get_fast(value.map, "toString", 8, &own_to_string);
            if (own_rd && !own_to_string) {
                bool own_src = false, own_flags = false;
                Item src_val = js_map_get_fast(value.map, "source", 6, &own_src);
                Item flags_val = js_map_get_fast(value.map, "flags", 5, &own_flags);
                String* src_s = (own_src && get_type_id(src_val) == LMD_TYPE_STRING) ? it2s(src_val) : NULL;
                String* flags_s = (own_flags && get_type_id(flags_val) == LMD_TYPE_STRING) ? it2s(flags_val) : NULL;
                StrBuf* sb = strbuf_new();
                strbuf_append_str_n(sb, "/", 1);
                if (src_s && src_s->len > 0) strbuf_append_str_n(sb, src_s->chars, (int)src_s->len);
                strbuf_append_str_n(sb, "/", 1);
                if (flags_s && flags_s->len > 0) strbuf_append_str_n(sb, flags_s->chars, (int)flags_s->len);
                String* result = heap_create_name(sb->str, sb->length);
                strbuf_free(sb);
                return (Item){.item = s2it(result)};
            }
        }
        // ES spec ToPrimitive with hint "string": try toString first, then valueOf
        // toString
        {
            Item ts_fn = ItemNull;
            // Track ownership for valueOf gating, but always route through
            // js_property_get so that accessor (getter) toString is invoked,
            // not the raw JsAccessorPair* slot value.
            bool own_ts = false;
            (void)js_map_get_fast(value.map, "toString", 8, &own_ts);
            bool ts_found = own_ts || js_ordinary_has_property(value, "toString", 8);
            Item ts_key = (Item){.item = s2it(heap_create_name("toString", 8))};
            ts_fn = js_property_get(value, ts_key);
            if (js_check_exception()) return (Item){.item = s2it(heap_create_name(""))};
            if (ts_fn.item != ItemNull.item && get_type_id(ts_fn) == LMD_TYPE_FUNC) {
                Item result = js_call_function(ts_fn, value, NULL, 0);
                if (js_check_exception()) return (Item){.item = s2it(heap_create_name(""))};
                TypeId rt = get_type_id(result);
                if (rt == LMD_TYPE_STRING) return result;
                if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY && rt != LMD_TYPE_FUNC) return js_to_string(result);
                // toString returned an object — fall through to valueOf
            }
            // valueOf fallback — only when toString was found (own or prototype)
            // If toString wasn't found, prototype chain isn't set up — use default "[object Object]"
            bool vo_found = js_ordinary_has_property(value, "valueOf", 7);
            if (ts_found || vo_found || bigint_wrapper) {
                // v90: Use js_property_get for valueOf to handle getter-defined valueOf
                // (e.g., {toString: null, get valueOf() { throw ... }})
                Item vo_key = (Item){.item = s2it(heap_create_name("valueOf", 7))};
                Item vo_fn = js_property_get(value, vo_key);
                if (js_check_exception()) return (Item){.item = s2it(heap_create_name(""))};
                if (vo_fn.item != ItemNull.item && get_type_id(vo_fn) == LMD_TYPE_FUNC) {
                    Item result = js_call_function(vo_fn, value, NULL, 0);
                    if (js_check_exception()) return (Item){.item = s2it(heap_create_name(""))};
                    TypeId rt = get_type_id(result);
                    if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_ARRAY && rt != LMD_TYPE_FUNC) return js_to_string(result);
                }
                // ES §7.1.1.1 OrdinaryToPrimitive step 6: after both methods attempted
                // (whether callable or not), if no primitive obtained, throw TypeError.
                // This covers {toString: undefined, valueOf: undefined} where both are
                // own properties but neither is callable.
                // v28: DOM elements (and other exotic objects) may have non-callable
                // toString/valueOf placeholders — fall through to default string conversion
                // instead of throwing.
                if (value.map &&
                    js_map_kind_uses_default_object_to_primitive(value.map->map_kind)) {
                    // newer clang does not model this switch fallthrough in the
                    // reverted runtime; return the intended default directly.
                    return (Item){.item = s2it(heap_create_name("[object Object]"))};
                }
                js_throw_type_error("Cannot convert object to primitive value");
                return (Item){.item = s2it(heap_create_name(""))};
            }
        }
        // Check for Error-like objects (have 'name' and 'message' properties)
        // JS: String(new Error("msg")) => "Error: msg"
        bool own_name = false, own_msg = false;
        Item name_val = js_map_get_fast(value.map, "name", 4, &own_name);
        Item msg_val = js_map_get_fast(value.map, "message", 7, &own_msg);
        if (own_name && get_type_id(name_val) == LMD_TYPE_STRING) {
            String* name_s = it2s(name_val);
            String* msg_s = (own_msg && get_type_id(msg_val) == LMD_TYPE_STRING) ? it2s(msg_val) : NULL;
            if (msg_s && msg_s->len > 0) {
                StrBuf* sb = strbuf_new();
                strbuf_append_str_n(sb, name_s->chars, (int)name_s->len);
                strbuf_append_str_n(sb, ": ", 2);
                strbuf_append_str_n(sb, msg_s->chars, (int)msg_s->len);
                String* result = heap_create_name(sb->str, sb->length);
                strbuf_free(sb);
                return (Item){.item = s2it(result)};
            }
            return name_val;
        }
        if (js_get_prototype(value).item == ITEM_JS_UNDEFINED &&
            !js_map_kind_uses_default_object_to_primitive(value.map->map_kind)) {
            js_throw_type_error("Cannot convert object to primitive value");
            return (Item){.item = s2it(heap_create_name(""))};
        }
        return (Item){.item = s2it(heap_create_name("[object Object]"))};
    }

    case LMD_TYPE_FUNC: {
        Item prim = js_to_primitive(value, JS_HINT_STRING);
        if (js_check_exception()) return ItemNull;
        TypeId prim_type = get_type_id(prim);
        if (prim_type == LMD_TYPE_FUNC || prim_type == LMD_TYPE_MAP ||
            prim_type == LMD_TYPE_ARRAY || prim_type == LMD_TYPE_ELEMENT) {
            js_throw_type_error("Cannot convert object to primitive value");
            return ItemNull;
        }
        return js_to_string(prim);
    }
    default:
        return (Item){.item = s2it(heap_create_name("[object Object]"))};
    }
}

extern "C" Item js_to_boolean(Item value) {
    return (Item){.item = b2it(js_is_truthy(value))};
}

extern "C" bool js_is_truthy(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        return false;

    case LMD_TYPE_BOOL:
        return it2b(value);

    case LMD_TYPE_INT:
        return it2i(value) != 0;

    case LMD_TYPE_DECIMAL:
        return !bigint_is_zero(value);

    case LMD_TYPE_FLOAT: {
        double d = it2d(value);
        return !isnan(d) && d != 0.0;
    }

    case LMD_TYPE_STRING: {
        String* str = it2s(value);
        return str && str->len > 0;
    }

    default:
        // Objects, arrays, functions are all truthy
        return value.item != 0;
    }
}

// js_is_nullish: returns true if value is null or undefined (for ?? operator)
extern "C" int64_t js_is_nullish(Item value) {
    TypeId type = get_type_id(value);
    return (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) ? 1 : 0;
}

// =============================================================================
// v23 Performance Facades — compound operations returning raw int64_t
// =============================================================================

// js_typeof_is: returns 1 if typeof(value) matches type_str, 0 otherwise.
// Avoids heap string allocation that js_typeof() performs.
extern "C" int64_t js_typeof_is(Item value, const char* type_str) {
    TypeId type = get_type_id(value);
    switch (type_str[0]) {
    case 'n':
        if (type_str[1] == 'u') {
            // "number"
            if (js_number_like_type(type)) {
                return js_key_is_symbol(value) ? 0 : 1;
            }
            return 0;
        }
        return 0;
    case 's':
        if (type_str[1] == 't') return (type == LMD_TYPE_STRING) ? 1 : 0;  // "string"
        if (type_str[1] == 'y') return (type == LMD_TYPE_SYMBOL ||         // "symbol"
            ((type == LMD_TYPE_INT || type == LMD_TYPE_FLOAT || type == LMD_TYPE_FLOAT64) && js_key_is_symbol(value))) ? 1 : 0;
        return 0;
    case 'b':
        if (type_str[1] == 'o') return (type == LMD_TYPE_BOOL) ? 1 : 0;      // "boolean"
        if (type_str[1] == 'i') return js_is_bigint_egress(value) ? 1 : 0;  // "bigint"
        return 0;
    case 'u': return (type == LMD_TYPE_UNDEFINED) ? 1 : 0;  // "undefined"
    case 'o':
        // "object": null, map (non-class), array, element, or other non-function
        if (type == LMD_TYPE_NULL) return 1;
        if (type == LMD_TYPE_MAP) {
            // Proxy wrapping callable → "function", not "object"
            if (js_is_proxy(value)) {
                Item t = js_typeof(value);
                String* ts = it2s(t);
                return (ts && ts->len == 6 && memcmp(ts->chars, "object", 6) == 0) ? 1 : 0;
            }
            bool own_ip = false;
            js_map_get_fast_ext(value.map, "__instance_proto__", 18, &own_ip);
            if (own_ip) return 0;  // class objects are "function"
            // Function.prototype is callable per spec
            if (js_class_id(value) == JS_CLASS_FUNCTION) {
                bool own_proto = false;
                js_map_get_fast_ext(value.map, "__is_proto__", 12, &own_proto);
                if (own_proto) return 0;  // "function", not "object"
            }
            return 1;
        }
        if (type == LMD_TYPE_FUNC || type == LMD_TYPE_UNDEFINED ||
            type == LMD_TYPE_BOOL || type == LMD_TYPE_STRING ||
            type == LMD_TYPE_SYMBOL) return 0;
        if (js_number_like_type(type) && !js_key_is_symbol(value)) return 0;
        return 1;  // arrays, elements, etc. are "object"
    case 'f':
        // "function"
        if (type == LMD_TYPE_FUNC) return 1;
        if (type == LMD_TYPE_MAP) {
            // Proxy wrapping callable → "function"
            if (js_is_proxy(value)) {
                Item t = js_typeof(value);
                String* ts = it2s(t);
                return (ts && ts->len == 8 && memcmp(ts->chars, "function", 8) == 0) ? 1 : 0;
            }
            bool own_ip = false;
            js_map_get_fast_ext(value.map, "__instance_proto__", 18, &own_ip);
            if (own_ip) return 1;
            // Function.prototype is callable per spec
            if (js_class_id(value) == JS_CLASS_FUNCTION) {
                bool own_proto = false;
                js_map_get_fast_ext(value.map, "__is_proto__", 12, &own_proto);
                if (own_proto) return 1;
            }
            return 0;
        }
        return 0;
    default: return 0;
    }
}

// v23b / Tune8 §2.1: Comparison facade returning raw int64_t 0/1 for direct use
// in MIR_BF/BT. Eliminates the box→unbox→branch cycle when a comparison is used
// as an if/for/while condition. Inlines the fast int-vs-int path; falls back to
// the full boxed comparison.
//
// Tune8 §2.1 fold: replaces 4 separate runtime entries (js_lt_raw / js_gt_raw /
// js_le_raw / js_ge_raw) with one dispatcher. The op parameter is a compile-time
// constant at every JIT call site, so the runtime-side switch resolves to a
// single predicted branch after the first call. Per Q1, MIR cannot inline
// through the C import, so the dispatch is paid — measured cost vs the
// pre-fold 4-entry form is within the microbench noise floor.
//
// op codes:  0=LT (a<b)  1=GT (a>b)  2=LE (a<=b)  3=GE (a>=b)
static Item js_abstract_relational_lt(Item left, Item right, bool leftFirst = true); // forward declaration
extern "C" int64_t js_cmp_raw(int64_t op, Item left, Item right) {
    TypeId lt = get_type_id(left), rt = get_type_id(right);
    bool l_num = js_number_like_type(lt);
    bool r_num = js_number_like_type(rt);
    if (l_num && r_num) {
        double l = (lt == LMD_TYPE_INT) ? (double)it2i(left) : it2d(left);
        double r = (rt == LMD_TYPE_INT) ? (double)it2i(right) : it2d(right);
        if (isnan(l) || isnan(r)) return 0;
        switch (op) {
        case 0: return l <  r ? 1 : 0;
        case 1: return l >  r ? 1 : 0;
        case 2: return l <= r ? 1 : 0;
        case 3: return l >= r ? 1 : 0;
        default: return 0;
        }
    }
    // Boxed fallback. The argument order and inversion are op-specific to
    // preserve original semantics (a > b is !(a < b) with NaN handling; spec
    // evaluation order matters for the leftFirst flag).
    switch (op) {
    case 0: {  // LT — uses public js_less_than path (matches pre-fold js_lt_raw)
        Item result = js_less_than(left, right);
        if (result.item == ITEM_JS_UNDEFINED) return 0;
        return (int64_t)it2b(result);
    }
    case 1: {  // GT — a > b => ARC(b, a, leftFirst=false)
        Item result = js_abstract_relational_lt(right, left, false);
        if (js_exception_pending || result.item == ITEM_JS_UNDEFINED) return 0;
        return (int64_t)it2b(result);
    }
    case 2: {  // LE — a <= b => !(b < a); NaN→false
        Item gt = js_abstract_relational_lt(right, left, false);
        if (js_exception_pending || gt.item == ITEM_JS_UNDEFINED) return 0;
        return it2b(gt) ? 0 : 1;
    }
    case 3: {  // GE — a >= b => !(a < b); NaN→false
        Item lt_result = js_abstract_relational_lt(left, right, true);
        if (js_exception_pending || lt_result.item == ITEM_JS_UNDEFINED) return 0;
        return it2b(lt_result) ? 0 : 1;
    }
    default: return 0;
    }
}

extern "C" int64_t js_eq_raw(Item left, Item right) {
    if (left.item == right.item) {
        TypeId type = get_type_id(left);
        if ((type == LMD_TYPE_FLOAT || type == LMD_TYPE_FLOAT64) && isnan(it2d(left))) return 0;
        return 1;
    }
    if (get_type_id(left) == LMD_TYPE_STRING && get_type_id(right) == LMD_TYPE_STRING) {
        String* left_str = it2s(left);
        String* right_str = it2s(right);
        return left_str->len == right_str->len &&
            memcmp(left_str->chars, right_str->chars, left_str->len) == 0;
    }
    return (int64_t)it2b(js_strict_equal(left, right));
}

// Tune8 §2.1: js_ne_raw and js_loose_ne_raw removed. The transpiler now emits
// js_eq_raw / js_loose_eq_raw followed by an inline MIR XOR-with-1 to invert.

extern "C" int64_t js_loose_eq_raw(Item left, Item right) {
    return (int64_t)it2b(js_equal(left, right));
}

// js_property_get_str: property access with C string key (avoids string boxing)
extern "C" Item js_property_get_str(Item object, const char* key, int key_len);
extern "C" bool js_typed_array_is_out_of_bounds_item(Item ta_item);
extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor);
extern "C" Item js_has_own_property(Item obj, Item key);

bool js_ta_key_canonical_numeric(Item key, double* numeric_index, bool* is_negative_zero) {
    if (is_negative_zero) *is_negative_zero = false;
    TypeId key_type = get_type_id(key);
    if (key_type == LMD_TYPE_INT) {
        int64_t iv = it2i(key);
        if (iv <= -(int64_t)JS_SYMBOL_BASE) return false;
        if (numeric_index) *numeric_index = (double)iv;
        return true;
    }
    if (key_type == LMD_TYPE_FLOAT || key_type == LMD_TYPE_FLOAT64) {
        if (numeric_index) *numeric_index = it2d(key);
        return true;
    }
    if (key_type != LMD_TYPE_STRING) return false;
    String* str = it2s(key);
    if (!str || str->len == 0 || str->len >= 128) return false;
    const char* chars = str->chars;
    int len = (int)str->len;
    if (len == 2 && chars[0] == '-' && chars[1] == '0') {
        if (numeric_index) *numeric_index = -0.0;
        if (is_negative_zero) *is_negative_zero = true;
        return true;
    }
    if (len == 3 && strncmp(chars, "NaN", 3) == 0) {
        if (numeric_index) *numeric_index = NAN;
        return true;
    }
    if (len == 8 && strncmp(chars, "Infinity", 8) == 0) {
        if (numeric_index) *numeric_index = INFINITY;
        return true;
    }
    if (len == 9 && strncmp(chars, "-Infinity", 9) == 0) {
        if (numeric_index) *numeric_index = -INFINITY;
        return true;
    }
    char buf[128];
    memcpy(buf, chars, len);
    buf[len] = '\0';
    char* endptr = NULL;
    double value = strtod(buf, &endptr);
    if (!endptr || *endptr != '\0') return false;
    char canon[128];
    if (value == 0.0) {
        snprintf(canon, sizeof(canon), "0");
    } else if (isnan(value)) {
        snprintf(canon, sizeof(canon), "NaN");
    } else if (isinf(value)) {
        snprintf(canon, sizeof(canon), value > 0 ? "Infinity" : "-Infinity");
    } else if (fabs(value) >= 0.000001 && fabs(value) < 1000000000000000000000.0) {
        snprintf(canon, sizeof(canon), "%.15f", value);
        int canon_len = (int)strlen(canon);
        while (canon_len > 0 && canon[canon_len - 1] == '0') canon[--canon_len] = '\0';
        if (canon_len > 0 && canon[canon_len - 1] == '.') canon[--canon_len] = '\0';
    } else {
        snprintf(canon, sizeof(canon), "%.15g", value);
    }
    if ((int)strlen(canon) != len || strncmp(canon, chars, len) != 0) return false;
    if (numeric_index) *numeric_index = value;
    return true;
}

bool js_ta_numeric_index_valid(Item object, double numeric_index, bool is_negative_zero, int* out_index) {
    if (is_negative_zero || !isfinite(numeric_index)) return false;
    double int_part = floor(numeric_index);
    if (int_part != numeric_index || numeric_index < 0) return false;
    if (js_typed_array_is_out_of_bounds_item(object)) return false;
    int64_t idx64 = (int64_t)numeric_index;
    int len = js_typed_array_length(object);
    if (idx64 < 0 || idx64 >= len) return false;
    if (out_index) *out_index = (int)idx64;
    return true;
}

bool js_ta_proto_chain_set(Item object, Item key, Item value) {
    if (js_skip_accessor_dispatch) return false;
    TypeId object_type = get_type_id(object);
    if (object_type != LMD_TYPE_MAP && object_type != LMD_TYPE_ARRAY) return false;
    if (js_is_proxy(object)) return false;
    if (object_type == LMD_TYPE_MAP && object.map && object.map->map_kind == MAP_KIND_TYPED_ARRAY) return false;

    double numeric_index = 0;
    bool is_negative_zero = false;
    if (!js_ta_key_canonical_numeric(key, &numeric_index, &is_negative_zero)) return false;
    if (it2b(js_has_own_property(object, key))) return false;

    Item proto = js_get_prototype_of(object);
    int depth = 0;
    while (proto.item != ItemNull.item && depth < 16) {
        // Proxy prototypes own the [[Set]] dispatch; probing them here would
        // invoke unrelated descriptor traps before OrdinarySet can forward.
        if (js_is_proxy(proto)) return false;
        if (get_type_id(proto) == LMD_TYPE_MAP && proto.map && proto.map->map_kind == MAP_KIND_TYPED_ARRAY) {
            int idx = 0;
            if (!js_ta_numeric_index_valid(proto, numeric_index, is_negative_zero, &idx)) return true;

            Item receiver = js_proxy_receiver.item ? js_proxy_receiver : object;
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, value);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(true)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
            js_object_define_property(receiver, key, desc);
            return true;
        }
        if (get_type_id(proto) == LMD_TYPE_MAP && it2b(js_has_own_property(proto, key))) return false;
        proto = js_get_prototype_of(proto);
        depth++;
    }
    return false;
}

bool js_array_ta_proto_numeric_set(Item array, Item key, bool* no_op) {
    if (no_op) *no_op = false;
    if (get_type_id(array) != LMD_TYPE_ARRAY) return false;
    double numeric_index = 0;
    bool is_negative_zero = false;
    if (!js_ta_key_canonical_numeric(key, &numeric_index, &is_negative_zero)) return false;
    if (it2b(js_has_own_property(array, key))) return false;

    Item proto = js_get_prototype_of(array);
    if (get_type_id(proto) != LMD_TYPE_MAP || !proto.map || proto.map->map_kind != MAP_KIND_TYPED_ARRAY) {
        return false;
    }
    int idx = 0;
    if (!js_ta_numeric_index_valid(proto, numeric_index, is_negative_zero, &idx)) {
        if (no_op) *no_op = true;
    }
    return true;
}

extern "C" int64_t js_discard_value(Item value) {
    (void)value;
    return 0;
}

// =============================================================================
// Helper: Get numeric value as double
// =============================================================================

double js_get_number(Item value) {
    TypeId type = get_type_id(value);

    switch (type) {
    case LMD_TYPE_INT:
        return (double)it2i(value);
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
        return it2d(value);
    case LMD_TYPE_NUM_SIZED:
        return js_sized_number_to_double(value);
    case LMD_TYPE_BOOL:
        return it2b(value) ? 1.0 : 0.0;
    case LMD_TYPE_NULL:
        return 0.0;
    case LMD_TYPE_UNDEFINED:
        return NAN;
    case LMD_TYPE_STRING: {
        Item num = js_to_number(value);
        if (js_check_exception()) return NAN;
        TypeId num_type = get_type_id(num);
        if (num_type == LMD_TYPE_INT) return (double)it2i(num);
        if (num_type == LMD_TYPE_INT64) return (double)it2l(num);
        if (num_type == LMD_TYPE_FLOAT || num_type == LMD_TYPE_FLOAT64) return it2d(num);
        return NAN;
    }
    case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT:
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_FUNC: {
        // J39-1b: route through unified js_to_primitive (ES §7.1.1, hint number).
        Item prim = js_to_primitive(value, JS_HINT_NUMBER);
        if (js_check_exception()) return NAN;
        if (js_is_symbol(prim)) {
            js_throw_type_error("Cannot convert a Symbol value to a number");
            return NAN;
        }
        return js_get_number(prim);
    }
    default:
        return NAN;
    }
}

Item js_make_number(double d) {
    // JS Number must not leak Lambda's compact-int representation; representation drift broke type-sensitive JS paths.
    double* ptr = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *ptr = d;
    return (Item){.item = d2it(ptr)};
}

// =============================================================================
// Arithmetic Operators
// =============================================================================

// Increment: handles both Number and BigInt (for ++ operator)
extern "C" Item js_increment(Item value) {
    value = js_numeric_operand(value);
    if (js_is_bigint(value)) return bigint_inc(value);
    double d = js_get_number(value);
    return js_make_number(d + 1.0);
}

// Decrement: handles both Number and BigInt (for -- operator)
extern "C" Item js_decrement(Item value) {
    value = js_numeric_operand(value);
    if (js_is_bigint(value)) return bigint_dec(value);
    double d = js_get_number(value);
    return js_make_number(d - 1.0);
}

// Number() function call: ES2020 §21.1.1.1 — calls ToNumeric, then converts BigInt to Number
extern "C" Item js_number_function(Item value) {
    // Symbol → TypeError (cannot convert to number)
    if (js_is_symbol(value)) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return ItemNull;
    }
    Item num = js_to_numeric(value);
    if (js_check_exception()) return ItemNull;
    if (js_is_bigint(num)) {
        return js_make_number(bigint_to_double(num));
    }
    return num;
}

// ES §7.1.1 ToPrimitive(value, hint) — internal helper used by abstract operations
// (==, <, >, <=, >=, +) for object/array/function operands. Returns the primitive
// value or ItemNull if an exception was thrown. For non-object inputs, returns value
// unchanged. Hint encoding: 0=default, 1=number, 2=string.
//
// J39-1: All semantics now live in js_coerce.cpp (js_to_primitive). This thin
// wrapper preserves the legacy integer-hint signature used by call sites in
// this file (js_add, comparisons) until they migrate to the JsHint enum.
static inline Item js_op_to_primitive(Item value, int hint) {
    JsHint h = (hint == 1) ? JS_HINT_NUMBER :
               (hint == 2) ? JS_HINT_STRING : JS_HINT_DEFAULT;
    return js_to_primitive(value, h);
}

static inline int js_upper_hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

extern "C" uint64_t js_get_heap_epoch();

static Item g_last_four_byte_uri_escape_string = {0};
static uint32_t g_last_four_byte_uri_escape_cp = 0;
static uint64_t g_last_four_byte_uri_escape_epoch = 0;

static bool js_percent_escape_four_byte_cp(String* s, uint32_t* cp_out) {
    if (!s || s->len != 12) return false;
    if (s->chars[0] != '%' || s->chars[3] != '%' ||
        s->chars[6] != '%' || s->chars[9] != '%') {
        return false;
    }
    int b0_high = js_upper_hex_digit_value(s->chars[1]);
    int b0_low = js_upper_hex_digit_value(s->chars[2]);
    int b1_high = js_upper_hex_digit_value(s->chars[4]);
    int b1_low = js_upper_hex_digit_value(s->chars[5]);
    int b2_high = js_upper_hex_digit_value(s->chars[7]);
    int b2_low = js_upper_hex_digit_value(s->chars[8]);
    int b3_high = js_upper_hex_digit_value(s->chars[10]);
    int b3_low = js_upper_hex_digit_value(s->chars[11]);
    if ((b0_high | b0_low | b1_high | b1_low | b2_high | b2_low | b3_high | b3_low) < 0) return false;
    unsigned int byte0 = (unsigned int)((b0_high << 4) | b0_low);
    unsigned int byte1 = (unsigned int)((b1_high << 4) | b1_low);
    unsigned int byte2 = (unsigned int)((b2_high << 4) | b2_low);
    unsigned int byte3 = (unsigned int)((b3_high << 4) | b3_low);
    if (byte0 < 0xF0 || byte0 > 0xF4) return false;
    if ((byte1 & 0xC0) != 0x80 || (byte2 & 0xC0) != 0x80 || (byte3 & 0xC0) != 0x80) return false;
    unsigned int cp = ((byte0 & 0x07) << 18) | ((byte1 & 0x3F) << 12) |
                      ((byte2 & 0x3F) << 6) | (byte3 & 0x3F);
    if (cp < 0x10000 || cp > 0x10FFFF) return false;
    *cp_out = (uint32_t)cp;
    return true;
}

extern "C" int64_t js_string_last_four_byte_uri_escape_cp(Item str_item) {
    if (str_item.item == g_last_four_byte_uri_escape_string.item &&
        g_last_four_byte_uri_escape_epoch == js_get_heap_epoch()) {
        return (int64_t)g_last_four_byte_uri_escape_cp;
    }
    return -1;
}

extern "C" void js_string_remember_four_byte_uri_escape_cp(Item str_item, int64_t cp) {
    if (cp < 0x10000 || cp > 0x10FFFF) return;
    g_last_four_byte_uri_escape_string = str_item;
    g_last_four_byte_uri_escape_cp = (uint32_t)cp;
    g_last_four_byte_uri_escape_epoch = js_get_heap_epoch();
}

static inline Item js_try_concat_percent_hex(String* left, String* right) {
    if (!left->is_ascii || !right->is_ascii || right->len != 1) return ItemNull;
    char right_ch = right->chars[0];
    int right_value = js_upper_hex_digit_value(right_ch);
    if (right_value < 0) return ItemNull;
    if (left->len == 1 && left->chars[0] == '%') {
        static Item prefix_cache[16] = {0};
        if (prefix_cache[right_value].item) return prefix_cache[right_value];
        char buf[2];
        buf[0] = '%';
        buf[1] = right_ch;
        prefix_cache[right_value] = (Item){.item = s2it(heap_create_name(buf, 2))};
        return prefix_cache[right_value];
    }
    int left_value = left->len == 2 && left->chars[0] == '%' ? js_upper_hex_digit_value(left->chars[1]) : -1;
    if (left_value >= 0) {
        int byte_value = (left_value << 4) | right_value;
        static Item byte_cache[256] = {0};
        if (byte_cache[byte_value].item) return byte_cache[byte_value];
        char buf[3];
        buf[0] = '%';
        buf[1] = left->chars[1];
        buf[2] = right_ch;
        byte_cache[byte_value] = (Item){.item = s2it(heap_create_name(buf, 3))};
        return byte_cache[byte_value];
    }
    return ItemNull;
}

static inline Item js_concat_strings_fast(String* left, String* right) {
    if (!left || !right) return ItemNull;
    int64_t left_len = left->len;
    int64_t right_len = right->len;
    Item percent_hex = js_try_concat_percent_hex(left, right);
    if (percent_hex.item != ItemNull.item) return percent_hex;
    String* result = (String*)heap_alloc(sizeof(String) + left_len + right_len + 1, LMD_TYPE_STRING);
    result->len = left_len + right_len;
    result->is_ascii = left->is_ascii && right->is_ascii;
    memcpy(result->chars, left->chars, left_len);
    memcpy(result->chars + left_len, right->chars, right_len);
    result->chars[result->len] = '\0';
    Item result_item = (Item){.item = s2it(result)};
    uint32_t cp = 0;
    if (result->len == 12 && js_percent_escape_four_byte_cp(result, &cp)) {
        g_last_four_byte_uri_escape_string = result_item;
        g_last_four_byte_uri_escape_cp = cp;
        g_last_four_byte_uri_escape_epoch = js_get_heap_epoch();
    }
    return result_item;
}

extern "C" Item js_string_concat(Item left, Item right) {
    return js_concat_strings_fast(it2s(left), it2s(right));
}

extern "C" Item js_add(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    // ES spec §13.15.3 / §7.1.1: ApplyStringOrNumericBinaryOperator for `+`
    // 1. lprim = ToPrimitive(left, default).  2. rprim = ToPrimitive(right, default).
    // js_op_to_primitive honors @@toPrimitive then OrdinaryToPrimitive (valueOf, toString)
    // and throws TypeError on object results / non-callable @@toPrimitive.
    if (left_type == LMD_TYPE_MAP || left_type == LMD_TYPE_ARRAY ||
        left_type == LMD_TYPE_ELEMENT || left_type == LMD_TYPE_FUNC) {
        left = js_op_to_primitive(left, 0);
        if (js_exception_pending) return make_js_undefined();
        left_type = get_type_id(left);
    }
    if (right_type == LMD_TYPE_MAP || right_type == LMD_TYPE_ARRAY ||
        right_type == LMD_TYPE_ELEMENT || right_type == LMD_TYPE_FUNC) {
        right = js_op_to_primitive(right, 0);
        if (js_exception_pending) return make_js_undefined();
        right_type = get_type_id(right);
    }

    // String concatenation if either operand is a string
    if (left_type == LMD_TYPE_STRING && right_type == LMD_TYPE_STRING) {
        return js_concat_strings_fast(it2s(left), it2s(right));
    }
    if (left_type == LMD_TYPE_STRING || right_type == LMD_TYPE_STRING) {
        Item left_str = js_to_string(left);
        Item right_str = js_to_string(right);
        if (js_exception_pending) return ItemNull;
        return js_concat_strings_fast(it2s(left_str), it2s(right_str));
    }

    left = js_numeric_operand(left);
    right = js_numeric_operand(right);

    // Numeric addition — use double arithmetic for JS semantics
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    // BigInt: mixed types → TypeError, same types → integer addition
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        return bigint_add(left, right);
    }
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l + r);
}

extern "C" Item js_subtract(Item left, Item right) {
    left = js_numeric_operand(left); if (js_exception_pending) return ItemNull;
    right = js_numeric_operand(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        return bigint_sub(left, right);
    }
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l - r);
}

extern "C" Item js_multiply(Item left, Item right) {
    left = js_numeric_operand(left); if (js_exception_pending) return ItemNull;
    right = js_numeric_operand(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        return bigint_mul(left, right);
    }
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l * r);
}

extern "C" Item js_divide(Item left, Item right) {
    left = js_numeric_operand(left); if (js_exception_pending) return ItemNull;
    right = js_numeric_operand(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        if (bigint_is_zero(right)) { js_throw_range_error("Division by zero"); return ItemNull; }
        return bigint_div(left, right);
    }
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(l / r);
}

extern "C" Item js_modulo(Item left, Item right) {
    left = js_numeric_operand(left); if (js_exception_pending) return ItemNull;
    right = js_numeric_operand(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        if (bigint_is_zero(right)) { js_throw_range_error("Division by zero"); return ItemNull; }
        return bigint_mod(left, right);
    }
    double l = js_get_number(left);
    double r = js_get_number(right);
    return js_make_number(fmod(l, r));
}

extern "C" Item js_power(Item left, Item right) {
    left = js_numeric_operand(left); if (js_exception_pending) return ItemNull;
    right = js_numeric_operand(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        int64_t exp = bigint_to_int64(right);
        if (exp < 0) { js_throw_range_error("Exponent must be positive"); return ItemNull; }
        return bigint_pow(left, right);
    }
    double base_d = js_get_number(js_to_number(left));
    double exp_d = js_get_number(js_to_number(right));
    return js_make_number(js_math_pow_d(base_d, exp_d));
}

// =============================================================================
// Comparison Operators
// =============================================================================

extern "C" Item js_equal(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    if (js_is_native_bigint_egress(left)) {
        left = js_native_bigint_to_bigint(left);
        left_type = get_type_id(left);
    }
    if (js_is_native_bigint_egress(right)) {
        right = js_native_bigint_to_bigint(right);
        right_type = get_type_id(right);
    }

    // Same type: use strict equality
    if (left_type == right_type) {
        return js_strict_equal(left, right);
    }

    // null == undefined
    if ((left_type == LMD_TYPE_NULL && right_type == LMD_TYPE_UNDEFINED) ||
        (left_type == LMD_TYPE_UNDEFINED && right_type == LMD_TYPE_NULL)) {
        return (Item){.item = b2it(true)};
    }

    // Number comparisons
    if (js_number_like_type(left_type) && js_number_like_type(right_type)) {
        double l = js_get_number(left);
        double r = js_get_number(right);
        return (Item){.item = b2it(l == r)};
    }

    // BigInt == Number: compare values
    bool left_bigint = js_is_bigint(left);
    bool right_bigint = js_is_bigint(right);
    if (left_bigint && js_number_like_type(right_type)) {
        double r = js_get_number(right);
        if (isnan(r) || r == INFINITY || r == -INFINITY) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(bigint_cmp_double(left, r) == 0)};
    }
    if (js_number_like_type(left_type) && right_bigint) {
        double l = js_get_number(left);
        if (isnan(l) || l == INFINITY || l == -INFINITY) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(bigint_cmp_double(right, l) == 0)};
    }
    // BigInt == String: convert string to BigInt and compare
    if (left_bigint && right_type == LMD_TYPE_STRING) {
        String* s = it2s(right);
        Item rbi = bigint_from_string(s->chars, s->len);
        if (rbi.item == ItemError.item) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(bigint_cmp(left, rbi) == 0)};
    }
    if (left_type == LMD_TYPE_STRING && right_bigint) {
        String* s = it2s(left);
        Item lbi = bigint_from_string(s->chars, s->len);
        if (lbi.item == ItemError.item) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(bigint_cmp(lbi, right) == 0)};
    }
    // BigInt == Boolean: convert boolean to BigInt
    if (left_bigint && right_type == LMD_TYPE_BOOL) {
        return (Item){.item = b2it(bigint_cmp(left, bigint_from_int64(it2b(right) ? 1 : 0)) == 0)};
    }
    if (left_type == LMD_TYPE_BOOL && right_bigint) {
        return (Item){.item = b2it(bigint_cmp(bigint_from_int64(it2b(left) ? 1 : 0), right) == 0)};
    }

    // String to number
    if ((left_type == LMD_TYPE_STRING && js_number_like_type(right_type)) ||
        (js_number_like_type(left_type) && right_type == LMD_TYPE_STRING)) {
        double l = js_get_number(left);
        double r = js_get_number(right);
        return (Item){.item = b2it(l == r)};
    }

    // Boolean to number
    if (left_type == LMD_TYPE_BOOL) {
        return js_equal(js_to_number(left), right);
    }
    if (right_type == LMD_TYPE_BOOL) {
        return js_equal(left, js_to_number(right));
    }

    // Object ToPrimitive: if one side is object/map, convert via ToPrimitive then recurse
    // ES §7.2.13 Abstract Equality steps 10-11: x is Object & y is primitive (or vice versa)
    // → ToPrimitive(object, "default") then re-compare. Hint default for ==.
    if (left_type == LMD_TYPE_MAP && (js_number_like_type(right_type) || right_type == LMD_TYPE_STRING || js_is_bigint(right) || js_is_symbol(right))) {
        Item prim = js_op_to_primitive(left, 0);
        if (js_exception_pending) return (Item){.item = b2it(false)};
        return js_equal(prim, right);
    }
    if (right_type == LMD_TYPE_MAP && (js_number_like_type(left_type) || left_type == LMD_TYPE_STRING || js_is_bigint(left) || js_is_symbol(left))) {
        Item prim = js_op_to_primitive(right, 0);
        if (js_exception_pending) return (Item){.item = b2it(false)};
        return js_equal(left, prim);
    }

    // Array ToPrimitive: arrays are objects; only coerce for object-vs-primitive
    // abstract equality cases, not for null/undefined comparisons.
    if (left_type == LMD_TYPE_ARRAY &&
        (js_number_like_type(right_type) || right_type == LMD_TYPE_STRING ||
         js_is_bigint(right) || js_is_symbol(right))) {
        Item prim = js_op_to_primitive(left, 0);
        if (js_exception_pending) return (Item){.item = b2it(false)};
        return js_equal(prim, right);
    }
    if (right_type == LMD_TYPE_ARRAY &&
        (js_number_like_type(left_type) || left_type == LMD_TYPE_STRING ||
         js_is_bigint(left) || js_is_symbol(left))) {
        Item prim = js_op_to_primitive(right, 0);
        if (js_exception_pending) return (Item){.item = b2it(false)};
        return js_equal(left, prim);
    }

    return (Item){.item = b2it(false)};
}

// Tune8 §2.1: js_not_equal removed — the transpiler emits js_equal followed
// by an inline MIR_XOR-with-1 on the boxed result (which inverts the low bit
// where b2it stores the bool, preserving the high-byte type tag).

extern "C" Item js_strict_equal(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    if (js_is_native_bigint_egress(left)) {
        left = js_native_bigint_to_bigint(left);
        left_type = get_type_id(left);
    }
    if (js_is_native_bigint_egress(right)) {
        right = js_native_bigint_to_bigint(right);
        right_type = get_type_id(right);
    }

    // Legacy compact ints can still enter from host paths, but JS-created Number values are boxed binary64.
    bool left_is_num = js_number_like_type(left_type);
    bool right_is_num = js_number_like_type(right_type);
    if (left_is_num && right_is_num) {
        double l = js_get_number(left);
        double r = js_get_number(right);
        if (isnan(l) || isnan(r)) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(l == r)};
    }

    // Different types are never strictly equal
    if (left_type != right_type) {
        return (Item){.item = b2it(false)};
    }

    switch (left_type) {
    case LMD_TYPE_NULL:
    case LMD_TYPE_UNDEFINED:
        return (Item){.item = b2it(true)};

    case LMD_TYPE_BOOL:
        return (Item){.item = b2it(it2b(left) == it2b(right))};

    case LMD_TYPE_INT:
        return (Item){.item = b2it(it2i(left) == it2i(right))};

    case LMD_TYPE_DECIMAL:
        if (js_is_bigint(left) && js_is_bigint(right)) {
            return (Item){.item = b2it(bigint_cmp(left, right) == 0)};
        }
        return (Item){.item = b2it(decimal_cmp_items(left, right) == 0)};

    case LMD_TYPE_FLOAT: {
        double l = it2d(left);
        double r = it2d(right);
        // NaN !== NaN
        if (isnan(l) || isnan(r)) {
            return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(l == r)};
    }

    case LMD_TYPE_STRING: {
        if (left.item == right.item) return (Item){.item = b2it(true)};
        String* l_str = it2s(left);
        String* r_str = it2s(right);
        if (l_str->len != r_str->len) {
            return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(memcmp(l_str->chars, r_str->chars, l_str->len) == 0)};
    }

    default:
        if (left_type == LMD_TYPE_MAP || left_type == LMD_TYPE_VMAP) {
            // DOM nodes moved from map shells to VMap carriers; strict equality
            // still follows node identity when duplicate host wrappers exist.
            void* left_dom = js_dom_unwrap_element(left);
            void* right_dom = js_dom_unwrap_element(right);
            if (left_dom || right_dom)
                return (Item){.item = b2it(left_dom && left_dom == right_dom)};
        }
        // Object/function equality is identity equality in JavaScript.
        return (Item){.item = b2it(left.item == right.item)};
    }
}

// Tune8 §2.1: js_strict_not_equal removed — see comment above js_strict_equal.

// Internal: Abstract Relational Comparison (ES spec §7.2.14)
// Returns true, false, or undefined (for NaN)
// leftFirst=true: left arg is the source-left operand (used for < and >=), check exception after it
// leftFirst=false: args are swapped by caller (used for > and <=), no early check after left arg
typedef struct JsUtf16Iter {
    const unsigned char* data;
    int64_t len;
    int64_t pos;
    int pending_low_surrogate;
} JsUtf16Iter;

static uint32_t js_next_utf8_codepoint(JsUtf16Iter* iter) {
    if (iter->pos >= iter->len) return 0;
    unsigned char lead = iter->data[iter->pos++];
    if (lead < 0x80) return lead;
    if ((lead & 0xE0) == 0xC0 && iter->pos < iter->len) {
        unsigned char second = iter->data[iter->pos++];
        return ((uint32_t)(lead & 0x1F) << 6) | (uint32_t)(second & 0x3F);
    }
    if ((lead & 0xF0) == 0xE0 && iter->pos + 1 < iter->len) {
        unsigned char second = iter->data[iter->pos++];
        unsigned char third = iter->data[iter->pos++];
        return ((uint32_t)(lead & 0x0F) << 12) |
               ((uint32_t)(second & 0x3F) << 6) |
               (uint32_t)(third & 0x3F);
    }
    if ((lead & 0xF8) == 0xF0 && iter->pos + 2 < iter->len) {
        unsigned char second = iter->data[iter->pos++];
        unsigned char third = iter->data[iter->pos++];
        unsigned char fourth = iter->data[iter->pos++];
        return ((uint32_t)(lead & 0x07) << 18) |
               ((uint32_t)(second & 0x3F) << 12) |
               ((uint32_t)(third & 0x3F) << 6) |
               (uint32_t)(fourth & 0x3F);
    }
    return lead;
}

static bool js_next_utf16_code_unit(JsUtf16Iter* iter, uint16_t* out_unit) {
    if (iter->pending_low_surrogate >= 0) {
        *out_unit = (uint16_t)iter->pending_low_surrogate;
        iter->pending_low_surrogate = -1;
        return true;
    }
    if (iter->pos >= iter->len) return false;
    uint32_t codepoint = js_next_utf8_codepoint(iter);
    if (codepoint > 0xFFFF) {
        uint32_t pair_value = codepoint - 0x10000;
        *out_unit = (uint16_t)(0xD800 + (pair_value >> 10));
        iter->pending_low_surrogate = (int)(0xDC00 + (pair_value & 0x3FF));
        return true;
    }
    *out_unit = (uint16_t)codepoint;
    return true;
}

static int js_compare_strings_utf16(String* left, String* right) {
    JsUtf16Iter left_iter = {(const unsigned char*)left->chars, left->len, 0, -1};
    JsUtf16Iter right_iter = {(const unsigned char*)right->chars, right->len, 0, -1};
    uint16_t left_unit = 0, right_unit = 0;
    while (true) {
        bool has_left = js_next_utf16_code_unit(&left_iter, &left_unit);
        bool has_right = js_next_utf16_code_unit(&right_iter, &right_unit);
        if (!has_left && !has_right) return 0;
        if (!has_left) return -1;
        if (!has_right) return 1;
        if (left_unit < right_unit) return -1;
        if (left_unit > right_unit) return 1;
    }
}

static Item js_abstract_relational_lt(Item left, Item right, bool leftFirst) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    // Symbol cannot be compared
    if (js_is_symbol(left) || js_is_symbol(right)) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return (Item){.item = b2it(false)};
    }

    // ToPrimitive for objects/arrays/functions (ES spec §7.2.14 Abstract Relational Comparison, hint "number")
    // ES spec: convert source operands in left-to-right order. The `leftFirst` flag indicates
    // whether `left` is the syntactic-left source operand. When false (e.g. `>` and `<=`),
    // the right parameter is the syntactic-left source operand and must be converted first.
    if (leftFirst) {
        if (left_type == LMD_TYPE_MAP || left_type == LMD_TYPE_ARRAY || left_type == LMD_TYPE_FUNC || left_type == LMD_TYPE_ELEMENT) {
            left = js_op_to_primitive(left, 1);
            if (js_exception_pending) return ItemNull;
            left_type = get_type_id(left);
        }
        if (right_type == LMD_TYPE_MAP || right_type == LMD_TYPE_ARRAY || right_type == LMD_TYPE_FUNC || right_type == LMD_TYPE_ELEMENT) {
            right = js_op_to_primitive(right, 1);
            if (js_exception_pending) return ItemNull;
            right_type = get_type_id(right);
        }
    } else {
        if (right_type == LMD_TYPE_MAP || right_type == LMD_TYPE_ARRAY || right_type == LMD_TYPE_FUNC || right_type == LMD_TYPE_ELEMENT) {
            right = js_op_to_primitive(right, 1);
            if (js_exception_pending) return ItemNull;
            right_type = get_type_id(right);
        }
        if (left_type == LMD_TYPE_MAP || left_type == LMD_TYPE_ARRAY || left_type == LMD_TYPE_FUNC || left_type == LMD_TYPE_ELEMENT) {
            left = js_op_to_primitive(left, 1);
            if (js_exception_pending) return ItemNull;
            left_type = get_type_id(left);
        }
    }
    // Propagate any pending exception before performing comparison
    if (js_exception_pending) return ItemNull;

    if (js_is_symbol(left) || js_is_symbol(right)) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return ItemNull;
    }

    // String comparison
    if (left_type == LMD_TYPE_STRING && right_type == LMD_TYPE_STRING) {
        String* l_str = it2s(left);
        String* r_str = it2s(right);
        int cmp = js_compare_strings_utf16(l_str, r_str);
        return (Item){.item = b2it(cmp < 0)};
    }

    // Numeric comparison
    // Convert boolean/null to numeric for comparison (ES spec §7.2.14 step 3)
    left_type = get_type_id(left);
    right_type = get_type_id(right);
    if (left_type == LMD_TYPE_BOOL) { left = (Item){.item = i2it(it2b(left) ? 1 : 0)}; left_type = LMD_TYPE_INT; }
    if (right_type == LMD_TYPE_BOOL) { right = (Item){.item = i2it(it2b(right) ? 1 : 0)}; right_type = LMD_TYPE_INT; }
    if (left_type == LMD_TYPE_NULL) { left = (Item){.item = i2it(0)}; left_type = LMD_TYPE_INT; }
    if (right_type == LMD_TYPE_NULL) { right = (Item){.item = i2it(0)}; right_type = LMD_TYPE_INT; }
    if (js_is_native_bigint_egress(left)) { left = js_native_bigint_to_bigint(left); left_type = get_type_id(left); }
    if (js_is_native_bigint_egress(right)) { right = js_native_bigint_to_bigint(right); right_type = get_type_id(right); }
    // BigInt comparisons
    bool left_bi = js_is_bigint(left);
    bool right_bi = js_is_bigint(right);
    if (left_bi && right_bi) {
        return (Item){.item = b2it(bigint_cmp(left, right) < 0)};
    }
    // BigInt vs Number
    if (left_bi && js_number_like_type(right_type)) {
        double r = js_get_number(right);
        if (isnan(r)) return (Item){.item = ITEM_JS_UNDEFINED};
        return (Item){.item = b2it(bigint_cmp_double(left, r) < 0)};
    }
    if (js_number_like_type(left_type) && right_bi) {
        double l = js_get_number(left);
        if (isnan(l)) return (Item){.item = ITEM_JS_UNDEFINED};
        return (Item){.item = b2it(bigint_cmp_double(right, l) > 0)};
    }
    // BigInt vs String — ES spec: StringToBigInt with hex/octal/binary support
    if (left_bi && right_type == LMD_TYPE_STRING) {
        String* s = it2s(right);
        if (!s) return (Item){.item = ITEM_JS_UNDEFINED};
        Item rbi = bigint_from_string(s->chars, s->len);
        if (rbi.item == ItemError.item) return (Item){.item = ITEM_JS_UNDEFINED};
        return (Item){.item = b2it(bigint_cmp(left, rbi) < 0)};
    }
    if (left_type == LMD_TYPE_STRING && right_bi) {
        String* s = it2s(left);
        if (!s) return (Item){.item = ITEM_JS_UNDEFINED};
        Item lbi = bigint_from_string(s->chars, s->len);
        if (lbi.item == ItemError.item) return (Item){.item = ITEM_JS_UNDEFINED};
        return (Item){.item = b2it(bigint_cmp(lbi, right) < 0)};
    }
    double l = js_get_number(left);
    double r = js_get_number(right);
    if (isnan(l) || isnan(r)) {
        return (Item){.item = ITEM_JS_UNDEFINED}; // per spec: Abstract Relational Comparison returns undefined for NaN
    }
    return (Item){.item = b2it(l < r)};
}

// ES spec §13.10.1: < operator — undefined from ARC becomes false
// Tune8 §2.1: box-returning comparison dispatcher.
// Replaces 4 separate runtime entries (js_less_than / js_less_equal /
// js_greater_than / js_greater_equal) with one. The JIT lowering passes a
// constant op operand at every call site.
//
//   op = 0: LT (a <  b)
//   op = 1: GT (a >  b)
//   op = 2: LE (a <= b)
//   op = 3: GE (a >= b)
extern "C" Item js_compare(int64_t op, Item left, Item right) {
    switch (op) {
    case 0: {  // LT — Abstract Relational Comparison (left, right, leftFirst=true)
        Item result = js_abstract_relational_lt(left, right);
        if (js_exception_pending) return make_js_undefined();
        if (result.item == ITEM_JS_UNDEFINED) return (Item){.item = b2it(false)};
        return result;
    }
    case 1: {  // GT — a > b => ARC(right, left, leftFirst=false)
        Item result = js_abstract_relational_lt(right, left, false);
        if (js_exception_pending) return make_js_undefined();
        if (result.item == ITEM_JS_UNDEFINED) return (Item){.item = b2it(false)};
        return result;
    }
    case 2: {  // LE — a <= b => !(b < a); NaN → false
        Item gt = js_abstract_relational_lt(right, left, false);
        if (js_exception_pending) return make_js_undefined();
        if (gt.item == ITEM_JS_UNDEFINED) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(!it2b(gt))};
    }
    case 3: {  // GE — a >= b => !(a < b); NaN → false
        Item lt = js_abstract_relational_lt(left, right);
        if (js_exception_pending) return make_js_undefined();
        if (lt.item == ITEM_JS_UNDEFINED) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(!it2b(lt))};
    }
    default: return (Item){.item = b2it(false)};
    }
}

// C wrappers retained for direct callers in js_runtime.cpp.
extern "C" Item js_less_than(Item left, Item right) {
    return js_compare(0, left, right);
}
extern "C" Item js_greater_than(Item left, Item right) {
    return js_compare(1, left, right);
}

// =============================================================================
// Logical Operators
// =============================================================================

extern "C" Item js_logical_and(Item left, Item right) {
    // Returns left if falsy, otherwise right
    if (!js_is_truthy(left)) {
        return left;
    }
    return right;
}

extern "C" Item js_logical_or(Item left, Item right) {
    // Returns left if truthy, otherwise right
    if (js_is_truthy(left)) {
        return left;
    }
    return right;
}

extern "C" Item js_logical_not(Item operand) {
    return (Item){.item = b2it(!js_is_truthy(operand))};
}

// =============================================================================
// Bitwise Operators
// =============================================================================

// JavaScript ToInt32: non-finite values → 0, large values wrap modulo 2^32
int32_t js_to_int32(double d) {
    if (!isfinite(d) || d == 0.0) return 0;
    // Modulo 2^32, then interpret as signed
    double d2 = fmod(trunc(d), 4294967296.0);
    if (d2 < 0) d2 += 4294967296.0;
    return (d2 >= 2147483648.0) ? (int32_t)(d2 - 4294967296.0) : (int32_t)d2;
}

// JIT-callable version of ToInt32: takes double, returns int64 for MIR compatibility
extern "C" int64_t js_double_to_int32(double d) {
    return (int64_t)js_to_int32(d);
}

extern "C" Item js_bitwise_and(Item left, Item right) {
    left = js_to_numeric(left); if (js_exception_pending) return ItemNull;
    right = js_to_numeric(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        return bigint_bitwise_and(left, right);
    }
    int32_t l = js_to_int32(js_get_number(left));
    int32_t r = js_to_int32(js_get_number(right));
    return js_make_number((double)(l & r));
}

extern "C" Item js_bitwise_or(Item left, Item right) {
    left = js_to_numeric(left); if (js_exception_pending) return ItemNull;
    right = js_to_numeric(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        return bigint_bitwise_or(left, right);
    }
    int32_t l = js_to_int32(js_get_number(left));
    int32_t r = js_to_int32(js_get_number(right));
    return js_make_number((double)(l | r));
}

extern "C" Item js_bitwise_xor(Item left, Item right) {
    left = js_to_numeric(left); if (js_exception_pending) return ItemNull;
    right = js_to_numeric(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        return bigint_bitwise_xor(left, right);
    }
    int32_t l = js_to_int32(js_get_number(left));
    int32_t r = js_to_int32(js_get_number(right));
    return js_make_number((double)(l ^ r));
}

extern "C" Item js_bitwise_not(Item operand) {
    operand = js_numeric_operand(operand);
    if (js_is_symbol(operand)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(operand)) {
        return bigint_bitwise_not(operand);
    }
    int32_t val = js_to_int32(js_get_number(operand));
    return js_make_number((double)(~val));
}

extern "C" Item js_left_shift(Item left, Item right) {
    left = js_numeric_operand(left); if (js_exception_pending) return ItemNull;
    right = js_numeric_operand(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        return bigint_left_shift(left, right);
    }
    int32_t l = js_to_int32(js_get_number(left));
    uint32_t r = (uint32_t)js_to_int32(js_get_number(right)) & 0x1F;
    return js_make_number((double)(l << r));
}

extern "C" Item js_right_shift(Item left, Item right) {
    left = js_numeric_operand(left); if (js_exception_pending) return ItemNull;
    right = js_numeric_operand(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    if (js_is_bigint(left) || js_is_bigint(right)) {
        if (js_check_bigint_arithmetic(left, right)) return ItemNull;
        return bigint_right_shift(left, right);
    }
    int32_t l = js_to_int32(js_get_number(left));
    uint32_t r = (uint32_t)js_to_int32(js_get_number(right)) & 0x1F;
    return js_make_number((double)(l >> r));
}

extern "C" Item js_unsigned_right_shift(Item left, Item right) {
    left = js_numeric_operand(left); if (js_exception_pending) return ItemNull;
    right = js_numeric_operand(right); if (js_exception_pending) return ItemNull;
    if (js_is_symbol(left) || js_is_symbol(right)) { js_throw_type_error("Cannot convert a Symbol value to a number"); return ItemNull; }
    // ES spec: BigInt does not support unsigned right shift (>>>)
    if (js_is_bigint(left) || js_is_bigint(right)) {
        js_throw_type_error("Cannot mix BigInt and other types, use explicit conversions");
        return ItemNull;
    }
    uint32_t l = (uint32_t)js_to_int32(js_get_number(left));
    uint32_t r = (uint32_t)js_to_int32(js_get_number(right)) & 0x1F;
    return js_make_number((double)(l >> r));
}

// =============================================================================
// Unary Operators
// =============================================================================

// v90: BigInt(value) — ES2020 BigInt constructor (called as function, not with new)
// Calls ToPrimitive(value, "number") to handle valueOf/toString on objects.
// Returns a BigInt (Decimal with unlimited == DECIMAL_BIGINT).
extern "C" Item js_bigint_constructor(Item value) {
    // If already a BigInt, return as-is
    if (js_is_bigint(value)) return value;
    if (js_is_native_bigint_egress(value)) return js_native_bigint_to_bigint(value);
    TypeId vt = get_type_id(value);
    // ToPrimitive for objects (hint: number) — ES spec §7.1.13
    if (vt == LMD_TYPE_MAP || vt == LMD_TYPE_ARRAY || vt == LMD_TYPE_FUNC) {
        Item prim = js_to_numeric(value);
        if (js_check_exception()) return ItemNull;
        // If ToPrimitive returned a BigInt, we're done
        if (js_is_bigint(prim)) return prim;
        // Otherwise recursively convert the primitive result
        return js_bigint_constructor(prim);
    }
    // Boolean: true → 1n, false → 0n
    if (vt == LMD_TYPE_BOOL) {
        return bigint_from_int64(js_is_truthy(value) ? 1 : 0);
    }
    // String: parse as integer
    if (vt == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if (s) {
            Item bi = bigint_from_string(s->chars, s->len);
            if (bi.item != ItemError.item) return bi;
        }
        js_throw_syntax_error((Item){.item = s2it(heap_create_name("Cannot convert string to a BigInt"))});
        return ItemNull;
    }
    // Number: must be an integer (no fraction)
    if (vt == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        if (d != d || d == INFINITY || d == -INFINITY || d != (double)(int64_t)d) {
            return js_throw_range_error("The number cannot be converted to a BigInt because it is not an integer");
        }
        return bigint_from_int64((int64_t)d);
    }
    if (vt == LMD_TYPE_INT) {
        int64_t iv = it2i(value);
        // Check for symbol encoded as negative int
        if (iv <= -(int64_t)JS_SYMBOL_BASE) {
            js_throw_type_error("Cannot convert a Symbol value to a BigInt");
            return ItemNull;
        }
        return bigint_from_int64(iv);
    }
    // undefined, null, object, etc. → TypeError
    js_throw_type_error("Cannot convert value to a BigInt");
    return ItemNull;
}

static Item js_to_bigint_for_bigint_op(Item value) {
    if (js_is_bigint(value)) return value;
    if (js_is_native_bigint_egress(value)) return js_native_bigint_to_bigint(value);
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_MAP || type == LMD_TYPE_ARRAY || type == LMD_TYPE_FUNC || type == LMD_TYPE_ELEMENT) {
        Item prim = js_to_primitive(value, JS_HINT_NUMBER);
        if (js_check_exception()) return ItemNull;
        return js_to_bigint_for_bigint_op(prim);
    }
    if (type == LMD_TYPE_BOOL) return bigint_from_int64(js_is_truthy(value) ? 1 : 0);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if (s) {
            Item bi = bigint_from_string(s->chars, s->len);
            if (bi.item != ItemError.item) return bi;
        }
        js_throw_syntax_error((Item){.item = s2it(heap_create_name("Cannot convert string to a BigInt"))});
        return ItemNull;
    }
    js_throw_type_error("Cannot convert value to a BigInt");
    return ItemNull;
}

static bool js_bigint_to_index(Item value, int64_t* out_bits) {
    if (js_is_symbol(value)) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return false;
    }
    Item num = js_to_number(value);
    if (js_check_exception()) return false;
    if (js_is_symbol(num)) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return false;
    }
    double d = js_get_number(num);
    double integer_index;
    if (d != d || d == 0.0) integer_index = 0.0;
    else if (d == INFINITY || d == -INFINITY) integer_index = d;
    else integer_index = d < 0.0 ? ceil(d) : floor(d);
    if (integer_index < 0.0 || integer_index > 9007199254740991.0 || integer_index == INFINITY) {
        js_throw_range_error("Invalid value: not a valid index");
        return false;
    }
    *out_bits = (int64_t)integer_index;
    return true;
}

extern "C" Item js_bigint_as_int_n(Item bits_item, Item bigint_item) {
    // BigInt.asIntN(bits, bigintValue) — clamp to signed N-bit range
    // ES spec: mod = bigintValue mod 2^bits; if mod >= 2^(bits-1) return mod - 2^bits, else mod
    int64_t bits = 0;
    if (!js_bigint_to_index(bits_item, &bits)) return ItemNull;
    Item bigint_val = js_to_bigint_for_bigint_op(bigint_item);
    if (js_check_exception()) return ItemNull;
    if (bits == 0) return bigint_from_int64(0);
    // 2^bits
    Item two = bigint_from_int64(2);
    Item exp = bigint_from_int64(bits);
    Item modulus = bigint_pow(two, exp);  // 2^bits
    Item mod = bigint_mod(bigint_val, modulus);
    // mathematical mod: ensure non-negative
    if (bigint_cmp(mod, bigint_from_int64(0)) < 0)
        mod = bigint_add(mod, modulus);
    // half = 2^(bits-1)
    Item exp_half = bigint_from_int64(bits - 1);
    Item half = bigint_pow(two, exp_half);
    // if mod >= half, result = mod - 2^bits
    if (bigint_cmp(mod, half) >= 0)
        return bigint_sub(mod, modulus);
    return mod;
}

extern "C" Item js_bigint_as_uint_n(Item bits_item, Item bigint_item) {
    // BigInt.asUintN(bits, bigintValue) — clamp to unsigned N-bit range
    // ES spec: return bigintValue mod 2^bits
    int64_t bits = 0;
    if (!js_bigint_to_index(bits_item, &bits)) return ItemNull;
    Item bigint_val = js_to_bigint_for_bigint_op(bigint_item);
    if (js_check_exception()) return ItemNull;
    if (bits == 0) return bigint_from_int64(0);
    Item two = bigint_from_int64(2);
    Item exp = bigint_from_int64(bits);
    Item modulus = bigint_pow(two, exp);
    Item mod = bigint_mod(bigint_val, modulus);
    // mathematical mod: ensure non-negative
    if (bigint_cmp(mod, bigint_from_int64(0)) < 0)
        mod = bigint_add(mod, modulus);
    return mod;
}

extern "C" Item js_bigint_not_constructor(void) {
    js_throw_type_error("BigInt is not a constructor");
    return ItemNull;
}

extern "C" Item js_unary_plus(Item operand) {
    // ES spec: ToNumber(Symbol) throws TypeError
    if (get_type_id(operand) == LMD_TYPE_INT && it2i(operand) <= -(int64_t)JS_SYMBOL_BASE) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return ItemNull;
    }
    return js_to_number(operand);
}

extern "C" Item js_unary_minus(Item operand) {
    // ToNumeric for objects (unwrap Object(BigInt) etc.)
    operand = js_numeric_operand(operand);
    // BigInt negation
    if (js_is_bigint(operand)) {
        return bigint_neg(operand);
    }
    // ES spec: ToNumber(Symbol) throws TypeError
    if (get_type_id(operand) == LMD_TYPE_INT && it2i(operand) <= -(int64_t)JS_SYMBOL_BASE) {
        js_throw_type_error("Cannot convert a Symbol value to a number");
        return ItemNull;
    }
    Item num = js_to_number(operand);
    // v18p: Integer 0 negated must produce float -0.0 per IEEE 754 / ECMAScript spec
    if (get_type_id(num) == LMD_TYPE_INT && it2i(num) == 0) {
        return js_make_number(-0.0);
    }
    Item result = fn_neg(num);
    // After negation, check if the result is an int in the symbol collision range.
    // If so, promote to float to avoid being misidentified as a symbol.
    if (get_type_id(result) == LMD_TYPE_INT && it2i(result) <= -(int64_t)JS_SYMBOL_BASE) {
        return js_make_number((double)it2i(result));
    }
    return result;
}

extern "C" Item js_typeof(Item value) {
    TypeId type = get_type_id(value);

    const char* result;
    switch (type) {
    case LMD_TYPE_UNDEFINED:
        result = "undefined";
        break;
    case LMD_TYPE_NULL:
        result = "object";  // typeof null === "object" (JS quirk)
        break;
    case LMD_TYPE_BOOL:
        result = "boolean";
        break;
    case LMD_TYPE_INT:
    case LMD_TYPE_FLOAT:
    case LMD_TYPE_FLOAT64:
    case LMD_TYPE_NUM_SIZED:
        result = js_key_is_symbol(value) ? "symbol" : "number";
        break;
    case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64:
        result = "bigint";
        break;
    case LMD_TYPE_DECIMAL:
        result = js_is_bigint(value) ? "bigint" : "number";
        break;
    case LMD_TYPE_STRING:
        result = "string";
        break;
    case LMD_TYPE_SYMBOL:
        result = "symbol";
        break;
    case LMD_TYPE_FUNC:
        result = "function";
        break;
    case LMD_TYPE_MAP: {
        // Proxy: typeof is "function" if target is callable
        if (js_is_proxy(value)) {
            result = js_proxy_has_callable_target(value) ? "function" : "object";
            goto done;
        }
        // v18h: class objects (MAPs with __instance_proto__) should return "function"
        // Use direct property lookup instead of shape walking for GC safety
        bool own_ip = false;
        js_map_get_fast_ext(value.map, "__instance_proto__", 18, &own_ip);
        if (own_ip) {
            result = "function";
            goto done;
        }
        // Function.prototype is callable per spec, typeof should return "function"
        if (js_class_id(value) == JS_CLASS_FUNCTION) {
            bool own_proto = false;
            js_map_get_fast_ext(value.map, "__is_proto__", 12, &own_proto);
            if (own_proto) {
                result = "function";
                goto done;
            }
        }
        result = "object";
        break;
    }
    default:
        result = "object";
        break;
    }
done:
    return (Item){.item = s2it(heap_create_name(result))};
}
