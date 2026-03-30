/**
 * JavaScript Process I/O and Global Functions for Lambda v5
 *
 * Implements:
 * - process.stdout.write(str)
 * - process.hrtime.bigint() (as float64 nanoseconds)
 * - process.argv
 * - parseInt, parseFloat, isNaN, isFinite
 * - console.log with multiple arguments
 */
#include "js_runtime.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "../transpiler.hpp"
#include "../format/format.h"
#include "../../lib/log.h"
#include "../../lib/url.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <time.h>

// forward declaration for JSON parser
extern Item parse_json_to_item(Input* input, const char* json_string);

#ifdef __APPLE__
#include <mach/mach_time.h>
#endif

extern void* heap_alloc(int size, TypeId type_id);
extern void fn_array_set(Array* arr, int64_t index, Item value);
extern "C" void js_set_prototype(Item object, Item prototype);
extern "C" Item js_get_prototype(Item object);
extern Item _map_read_field(ShapeEntry* field, void* map_data);

// =============================================================================
// Process I/O
// =============================================================================

extern "C" Item js_process_stdout_write(Item str_item) {
    TypeId type = get_type_id(str_item);
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(str_item);
        if (s && s->len > 0) {
            fwrite(s->chars, 1, s->len, stdout);
            fflush(stdout);
        }
    } else {
        // Convert to string first
        Item str = js_to_string(str_item);
        String* s = it2s(str);
        if (s && s->len > 0) {
            fwrite(s->chars, 1, s->len, stdout);
            fflush(stdout);
        }
    }
    return (Item){.item = ITEM_TRUE};
}

extern "C" Item js_process_hrtime_bigint(void) {
    // Return nanosecond-precision monotonic time as a double
    // double has ~53 bits of mantissa, good for ~104 days of nanosecond precision
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t ticks = mach_absolute_time();
    double ns = (double)ticks * (double)timebase.numer / (double)timebase.denom;
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = ns;
    return (Item){.item = d2it(fp)};
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double ns = (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = ns;
    return (Item){.item = d2it(fp)};
#endif
}

// performance.now() — returns milliseconds (monotonic, high-resolution)
// Uses static buffer (not GC heap) because the returned float must survive GC cycles.
// MIR-generated code stores the Item value in registers/stack, where the conservative
// GC scanner may not find it (nursery floats get overwritten; heap floats get collected).
static double js_perf_now_buf[64];
static int js_perf_now_idx = 0;

extern "C" Item js_performance_now(void) {
#ifdef __APPLE__
    static mach_timebase_info_data_t timebase = {0, 0};
    if (timebase.denom == 0) {
        mach_timebase_info(&timebase);
    }
    uint64_t ticks = mach_absolute_time();
    double ns = (double)ticks * (double)timebase.numer / (double)timebase.denom;
    double ms = ns / 1e6;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
    double* fp = &js_perf_now_buf[js_perf_now_idx % 64];
    js_perf_now_idx++;
    *fp = ms;
    return (Item){.item = d2it(fp)};
}

// Date.now() — returns milliseconds since Unix epoch
static double js_date_now_buf[64];
static int js_date_now_idx = 0;

extern "C" Item js_date_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
    double* fp = &js_date_now_buf[js_date_now_idx % 64];
    js_date_now_idx++;
    *fp = ms;
    return (Item){.item = d2it(fp)};
}

// new Date() — returns a map that acts as a Date object.
// Stores the current timestamp so .getTime() can retrieve it at runtime.
// The transpiler handles new Date().getTime() as a special case (→ js_date_now()),
// but js_date_new() is needed if the Date object is stored in a variable first.
extern "C" Item js_date_new(void) {
    Item obj = js_new_object();
    Item time_val = js_date_now();
    Item key = (Item){.item = s2it(heap_create_name("_time"))};
    js_property_set(obj, key, time_val);
    // mark as Date for instanceof
    Item cls_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    js_property_set(obj, cls_key, (Item){.item = s2it(heap_create_name("Date"))});
    return obj;
}

// new Date(value) — accepts a numeric timestamp (ms since epoch) or a date string
extern "C" Item js_date_new_from(Item value) {
    Item obj = js_new_object();
    Item key = (Item){.item = s2it(heap_create_name("_time"))};
    TypeId tid = get_type_id(value);
    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64 || tid == LMD_TYPE_FLOAT) {
        double ms;
        if (tid == LMD_TYPE_FLOAT) ms = it2d(value);
        else ms = (double)it2i(value);
        static double date_buf[16];
        static int date_idx = 0;
        double* fp = &date_buf[date_idx++ % 16];
        *fp = ms;
        js_property_set(obj, key, (Item){.item = d2it(fp)});
    } else if (tid == LMD_TYPE_STRING) {
        // parse date string — try ISO 8601 format
        String* s = it2s(value);
        if (s) {
            struct tm tm = {};
            if (strptime(s->chars, "%Y-%m-%dT%H:%M:%S", &tm) ||
                strptime(s->chars, "%a %b %d %Y %H:%M:%S", &tm) ||
                strptime(s->chars, "%c", &tm)) {
                time_t t = timegm(&tm);
                double ms = (double)t * 1000.0;
                static double date_buf2[16];
                static int date_idx2 = 0;
                double* fp = &date_buf2[date_idx2++ % 16];
                *fp = ms;
                js_property_set(obj, key, (Item){.item = d2it(fp)});
            } else {
                // fallback: try mktime (local time)
                struct tm tm2 = {};
                if (sscanf(s->chars, "%d-%d-%d", &tm2.tm_year, &tm2.tm_mon, &tm2.tm_mday) == 3) {
                    tm2.tm_year -= 1900;
                    tm2.tm_mon -= 1;
                    time_t t = mktime(&tm2);
                    double ms = (double)t * 1000.0;
                    static double date_buf3[16];
                    static int date_idx3 = 0;
                    double* fp = &date_buf3[date_idx3++ % 16];
                    *fp = ms;
                    js_property_set(obj, key, (Item){.item = d2it(fp)});
                } else {
                    // If unparseable, set NaN
                    js_property_set(obj, key, js_date_now());
                }
            }
        } else {
            js_property_set(obj, key, js_date_now());
        }
    } else {
        js_property_set(obj, key, js_date_now());
    }
    Item cls_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    js_property_set(obj, cls_key, (Item){.item = s2it(heap_create_name("Date"))});
    return obj;
}

// Date.UTC(year, month[, day[, hour[, min[, sec[, ms]]]]]) — returns ms since epoch
extern "C" Item js_date_utc(Item args_array) {
    // args_array is a JS array
    int len = (int)js_array_length(args_array);
    int year = 0, month = 0, day = 1, hour = 0, min = 0, sec = 0, millis = 0;
    auto get_arg_int = [&](int idx) -> int {
        Item val = js_array_get_int(args_array, idx);
        TypeId t = get_type_id(val);
        if (t == LMD_TYPE_INT) return (int)it2i(val);
        if (t == LMD_TYPE_FLOAT) return (int)it2d(val);
        return 0;
    };
    if (len > 0) year = get_arg_int(0);
    if (len > 1) month = get_arg_int(1);
    if (len > 2) day = get_arg_int(2);
    if (len > 3) hour = get_arg_int(3);
    if (len > 4) min = get_arg_int(4);
    if (len > 5) sec = get_arg_int(5);
    if (len > 6) millis = get_arg_int(6);

    struct tm tm = {};
    tm.tm_year = year - 1900;
    tm.tm_mon = month;       // 0-based
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;

    time_t t = timegm(&tm);
    double ms = (double)t * 1000.0 + (double)millis;
    static double utc_buf[16];
    static int utc_idx = 0;
    double* fp = &utc_buf[utc_idx++ % 16];
    *fp = ms;
    return (Item){.item = d2it(fp)};
}

// v11: Date instance method dispatch
// method_id: 0=getTime, 1=getFullYear, 2=getMonth, 3=getDate,
//   4=getHours, 5=getMinutes, 6=getSeconds, 7=getMilliseconds,
//   8=toISOString, 9=toLocaleDateString
extern "C" Item js_date_method(Item date_obj, int method_id) {
    // extract epoch-ms from the _time property
    Item key = (Item){.item = s2it(heap_create_name("_time"))};
    Item time_val = js_property_get(date_obj, key);
    double ms = time_val.get_double();
    if (method_id == 0) { // getTime
        static double gt_buf[16];
        static int gt_idx = 0;
        double* fp = &gt_buf[gt_idx++ % 16];
        *fp = ms;
        return (Item){.item = d2it(fp)};
    }
    time_t secs = (time_t)(ms / 1000.0);
    struct tm tm;
    localtime_r(&secs, &tm);
    switch (method_id) {
        case 1: return (Item){.item = i2it(tm.tm_year + 1900)}; // getFullYear
        case 2: return (Item){.item = i2it(tm.tm_mon)};         // getMonth (0-based)
        case 3: return (Item){.item = i2it(tm.tm_mday)};        // getDate
        case 4: return (Item){.item = i2it(tm.tm_hour)};        // getHours
        case 5: return (Item){.item = i2it(tm.tm_min)};         // getMinutes
        case 6: return (Item){.item = i2it(tm.tm_sec)};         // getSeconds
        case 7: {                                                // getMilliseconds
            int millis = (int)(ms - (double)secs * 1000.0);
            return (Item){.item = i2it(millis)};
        }
        case 8: { // toISOString
            char buf[32];
            struct tm utc;
            gmtime_r(&secs, &utc);
            int millis = (int)(ms - (double)secs * 1000.0);
            snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                utc.tm_hour, utc.tm_min, utc.tm_sec, millis);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        case 9: { // toLocaleDateString
            char buf[32];
            snprintf(buf, sizeof(buf), "%d/%d/%04d",
                tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        default: break;
    }
    // UTC variants: use gmtime_r
    if (method_id >= 10 && method_id <= 16) {
        struct tm utc;
        gmtime_r(&secs, &utc);
        switch (method_id) {
            case 10: return (Item){.item = i2it(utc.tm_year + 1900)}; // getUTCFullYear
            case 11: return (Item){.item = i2it(utc.tm_mon)};         // getUTCMonth (0-based)
            case 12: return (Item){.item = i2it(utc.tm_mday)};        // getUTCDate
            case 13: return (Item){.item = i2it(utc.tm_hour)};        // getUTCHours
            case 14: return (Item){.item = i2it(utc.tm_min)};         // getUTCMinutes
            case 15: return (Item){.item = i2it(utc.tm_sec)};         // getUTCSeconds
            case 16: {                                                 // getUTCMilliseconds
                int millis = (int)(ms - (double)secs * 1000.0);
                return (Item){.item = i2it(millis)};
            }
            default: break;
        }
    }
    // toString: produce a human-readable date string parseable by new Date(str)
    if (method_id == 17) {
        struct tm utc;
        gmtime_r(&secs, &utc);
        // JS-style: "Thu Jun 09 3141 02:06:53 GMT+0000"
        static const char* wday[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* mon[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %s %02d %04d %02d:%02d:%02d GMT+0000",
            wday[utc.tm_wday], mon[utc.tm_mon], utc.tm_mday,
            utc.tm_year + 1900, utc.tm_hour, utc.tm_min, utc.tm_sec);
        return (Item){.item = s2it(heap_create_name(buf))};
    }
    return ItemNull;
}

// Process argv storage
static Item js_process_argv_items = {.item = ITEM_NULL};

extern "C" void js_set_process_argv(int argc, const char** argv) {
    // Build a Lambda array from the argv
    Array* arr = array();
    for (int i = 0; i < argc; i++) {
        array_push(arr, (Item){.item = s2it(heap_create_name(argv[i]))});
    }
    js_process_argv_items = array_end(arr);
}

extern "C" Item js_get_process_argv(void) {
    return js_process_argv_items;
}

// =============================================================================
// Global Functions
// =============================================================================

extern "C" Item js_parseInt(Item str_item, Item radix_item) {
    // Convert first arg to string
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    // Get radix (default 10)
    int radix = 10;
    TypeId rtype = get_type_id(radix_item);
    if (rtype == LMD_TYPE_INT) {
        radix = (int)it2i(radix_item);
    } else if (rtype == LMD_TYPE_FLOAT) {
        radix = (int)it2d(radix_item);
    }
    if (radix == 0) radix = 10;

    // Null-terminate
    char buf[256];
    int len = s->len < 255 ? s->len : 255;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';

    // Skip whitespace
    char* start = buf;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') start++;

    char* endptr;
    long long val = strtoll(start, &endptr, radix);
    if (endptr == start) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    return (Item){.item = i2it((int64_t)val)};
}

extern "C" Item js_parseFloat(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    char buf[256];
    int len = s->len < 255 ? s->len : 255;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';

    char* endptr;
    double val = strtod(buf, &endptr);
    if (endptr == buf) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = val;
    return (Item){.item = d2it(fp)};
}

extern "C" Item js_isNaN(Item value) {
    Item num = js_to_number(value);
    TypeId type = get_type_id(num);
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(num);
        return (Item){.item = isnan(d) ? ITEM_TRUE : ITEM_FALSE};
    }
    return (Item){.item = ITEM_FALSE};
}

extern "C" Item js_isFinite(Item value) {
    Item num = js_to_number(value);
    TypeId type = get_type_id(num);
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(num);
        return (Item){.item = isfinite(d) ? ITEM_TRUE : ITEM_FALSE};
    }
    if (type == LMD_TYPE_INT) {
        return (Item){.item = ITEM_TRUE};
    }
    return (Item){.item = ITEM_FALSE};
}

// =============================================================================
// Number Methods
// =============================================================================

extern "C" Item js_toFixed(Item num_item, Item digits_item) {
    double num;
    TypeId type = get_type_id(num_item);
    if (type == LMD_TYPE_FLOAT) {
        num = it2d(num_item);
    } else if (type == LMD_TYPE_INT) {
        num = (double)it2i(num_item);
    } else {
        return js_to_string(num_item);
    }

    int digits = 0;
    TypeId dtype = get_type_id(digits_item);
    if (dtype == LMD_TYPE_INT) {
        digits = (int)it2i(digits_item);
    } else if (dtype == LMD_TYPE_FLOAT) {
        digits = (int)it2d(digits_item);
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "%.*f", digits, num);
    return (Item){.item = s2it(heap_create_name(buf))};
}

extern "C" Item js_number_method(Item num, Item method_name, Item* args, int argc) {
    if (get_type_id(method_name) != LMD_TYPE_STRING) return ItemNull;
    String* method = it2s(method_name);
    if (!method) return ItemNull;

    if (method->len == 7 && strncmp(method->chars, "toFixed", 7) == 0) {
        Item digits = (argc > 0) ? args[0] : (Item){.item = i2it(0)};
        return js_toFixed(num, digits);
    }
    if (method->len == 8 && strncmp(method->chars, "toString", 8) == 0) {
        if (argc > 0) {
            Item radix_item = js_to_number(args[0]);
            int radix = 10;
            TypeId rt = get_type_id(radix_item);
            if (rt == LMD_TYPE_INT) radix = (int)it2i(radix_item);
            else if (rt == LMD_TYPE_FLOAT) radix = (int)it2d(radix_item);
            if (radix >= 2 && radix <= 36 && radix != 10) {
                // convert number to string with given radix
                int64_t n = 0;
                TypeId nt = get_type_id(num);
                if (nt == LMD_TYPE_INT) n = it2i(num);
                else if (nt == LMD_TYPE_FLOAT) n = (int64_t)it2d(num);
                bool negative = n < 0;
                uint64_t val = negative ? (uint64_t)(-(int64_t)n) : (uint64_t)n;
                char buf[68]; // max 64 binary digits + sign + null
                int pos = 66;
                buf[66] = '\0';
                if (val == 0) {
                    buf[--pos] = '0';
                } else {
                    const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
                    while (val > 0) {
                        buf[--pos] = digits[val % radix];
                        val /= radix;
                    }
                }
                if (negative) buf[--pos] = '-';
                return (Item){.item = s2it(heap_create_name(&buf[pos], 66 - pos))};
            }
        }
        return js_to_string(num);
    }

    log_debug("js_number_method: unknown method '%.*s'", (int)method->len, method->chars);
    return ItemNull;
}

// =============================================================================
// String Methods (v5 additions)
// =============================================================================

extern "C" Item js_string_charCodeAt(Item str_item, Item index_item) {
    String* s = it2s(str_item);
    if (!s) return (Item){.item = i2it(0)};

    int idx = 0;
    TypeId itype = get_type_id(index_item);
    if (itype == LMD_TYPE_INT) {
        idx = (int)it2i(index_item);
    } else if (itype == LMD_TYPE_FLOAT) {
        idx = (int)it2d(index_item);
    }

    if (idx < 0 || idx >= (int)s->len) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    // Return the UTF-16 code unit (for ASCII, same as byte value)
    return (Item){.item = i2it((int64_t)(unsigned char)s->chars[idx])};
}

extern "C" Item js_string_fromCharCode(Item code_item) {
    int code = 0;
    TypeId type = get_type_id(code_item);
    if (type == LMD_TYPE_INT) {
        code = (int)it2i(code_item);
    } else if (type == LMD_TYPE_FLOAT) {
        code = (int)it2d(code_item);
    }
    // JS fromCharCode uses UTF-16 code units (truncate to 16-bit)
    code &= 0xFFFF;

    char buf[5]; // max 4 bytes for UTF-8 + null
    int len = 0;
    if (code < 128) {
        buf[0] = (char)code;
        len = 1;
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
        len = 2;
    } else {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
        len = 3;
    }
    buf[len] = '\0';

    return (Item){.item = s2it(heap_strcpy(buf, len))};
}

// Helper: encode a UTF-16 code unit to UTF-8 into buf, return bytes written
static int encode_charcode_utf8(char* buf, int code) {
    code &= 0xFFFF; // truncate to 16-bit (JS fromCharCode uses UTF-16 code units)
    if (code < 128) {
        buf[0] = (char)code;
        return 1;
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
        return 2;
    } else {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
        return 3;
    }
}

// Multi-argument String.fromCharCode: js_string_fromCharCode_array(Item arr)
// Takes a Lambda Array or TypedArray of code points and returns a concatenated string
extern "C" Item js_string_fromCharCode_array(Item arr_item) {
    TypeId type = get_type_id(arr_item);

    // Handle TypedArray (Uint8Array, Int32Array, etc.)
    if (type == LMD_TYPE_MAP && js_is_typed_array(arr_item)) {
        JsTypedArray* ta = (JsTypedArray*)arr_item.map->data;
        int len = ta->length;
        if (len == 0) return (Item){.item = s2it(heap_strcpy("", 0))};
        char* buf = (char*)malloc(len * 3 + 1);
        int pos = 0;
        for (int i = 0; i < len; i++) {
            int code = 0;
            switch (ta->element_type) {
            case JS_TYPED_UINT8:   code = ((uint8_t*)ta->data)[i]; break;
            case JS_TYPED_INT8:    code = ((int8_t*)ta->data)[i]; break;
            case JS_TYPED_UINT16:  code = ((uint16_t*)ta->data)[i]; break;
            case JS_TYPED_INT16:   code = ((int16_t*)ta->data)[i]; break;
            case JS_TYPED_UINT32:  code = (int)((uint32_t*)ta->data)[i]; break;
            case JS_TYPED_INT32:   code = ((int32_t*)ta->data)[i]; break;
            case JS_TYPED_FLOAT32: code = (int)((float*)ta->data)[i]; break;
            case JS_TYPED_FLOAT64: code = (int)((double*)ta->data)[i]; break;
            }
            pos += encode_charcode_utf8(buf + pos, code);
        }
        buf[pos] = '\0';
        Item result = (Item){.item = s2it(heap_strcpy(buf, pos))};
        free(buf);
        return result;
    }

    if (type != LMD_TYPE_ARRAY) {
        return js_string_fromCharCode(arr_item); // fallback: single arg
    }
    Array* arr = arr_item.array;
    int len = arr->length;
    if (len == 0) return (Item){.item = s2it(heap_strcpy("", 0))};
    char* buf = (char*)malloc(len * 3 + 1);
    int pos = 0;
    for (int i = 0; i < len; i++) {
        int code = 0;
        TypeId itype = get_type_id(arr->items[i]);
        if (itype == LMD_TYPE_INT) {
            code = (int)it2i(arr->items[i]);
        } else if (itype == LMD_TYPE_FLOAT) {
            code = (int)it2d(arr->items[i]);
        }
        pos += encode_charcode_utf8(buf + pos, code);
    }
    buf[pos] = '\0';
    Item result = (Item){.item = s2it(heap_strcpy(buf, pos))};
    free(buf);
    return result;
}

// Helper: encode a full Unicode code point to UTF-8 (up to 4 bytes)
static int encode_codepoint_utf8(char* buf, int code) {
    if (code < 0 || code > 0x10FFFF) return 0;
    if (code < 0x80) {
        buf[0] = (char)code;
        return 1;
    } else if (code < 0x800) {
        buf[0] = (char)(0xC0 | (code >> 6));
        buf[1] = (char)(0x80 | (code & 0x3F));
        return 2;
    } else if (code < 0x10000) {
        buf[0] = (char)(0xE0 | (code >> 12));
        buf[1] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (code & 0x3F));
        return 3;
    } else {
        buf[0] = (char)(0xF0 | (code >> 18));
        buf[1] = (char)(0x80 | ((code >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((code >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (code & 0x3F));
        return 4;
    }
}

// String.fromCodePoint(cp) — single code point
extern "C" Item js_string_fromCodePoint(Item code_item) {
    int code = 0;
    TypeId type = get_type_id(code_item);
    if (type == LMD_TYPE_INT) {
        code = (int)it2i(code_item);
    } else if (type == LMD_TYPE_FLOAT) {
        code = (int)it2d(code_item);
    }
    char buf[5];
    int len = encode_codepoint_utf8(buf, code);
    buf[len] = '\0';
    return (Item){.item = s2it(heap_strcpy(buf, len))};
}

// String.fromCodePoint(cp1, cp2, ...) — multiple code points via array
extern "C" Item js_string_fromCodePoint_array(Item arr_item) {
    TypeId type = get_type_id(arr_item);
    if (type != LMD_TYPE_ARRAY) {
        return js_string_fromCodePoint(arr_item);
    }
    Array* arr = arr_item.array;
    int len = arr->length;
    if (len == 0) return (Item){.item = s2it(heap_strcpy("", 0))};
    char* buf = (char*)malloc(len * 4 + 1);
    int pos = 0;
    for (int i = 0; i < len; i++) {
        int code = 0;
        TypeId itype = get_type_id(arr->items[i]);
        if (itype == LMD_TYPE_INT) {
            code = (int)it2i(arr->items[i]);
        } else if (itype == LMD_TYPE_FLOAT) {
            code = (int)it2d(arr->items[i]);
        }
        pos += encode_codepoint_utf8(buf + pos, code);
    }
    buf[pos] = '\0';
    Item result = (Item){.item = s2it(heap_strcpy(buf, pos))};
    free(buf);
    return result;
}

// =============================================================================
// Console multi-argument log
// =============================================================================

extern "C" void js_console_log_multi(Item* args, int argc) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) fputc(' ', stdout);
        Item str = js_to_string(args[i]);
        String* s = it2s(str);
        if (s) fwrite(s->chars, 1, s->len, stdout);
    }
    fputc('\n', stdout);
    fflush(stdout);
}

// =============================================================================
// Array fill (regular arrays)
// =============================================================================

extern "C" Item js_array_fill(Item arr_item, Item value) {
    TypeId type = get_type_id(arr_item);

    // Check if typed array first
    if (type == LMD_TYPE_MAP && js_is_typed_array(arr_item)) {
        return js_typed_array_fill(arr_item, value, 0, INT_MAX);
    }

    if (type != LMD_TYPE_ARRAY) return arr_item;

    int len = fn_len(arr_item);
    Array* arr = it2arr(arr_item);
    for (int i = 0; i < len; i++) {
        fn_array_set(arr, i, value);
    }

    return arr_item;
}

// =============================================================================
// instanceof operator — walks prototype chain
// =============================================================================

extern "C" Item js_instanceof(Item left, Item right) {
    // right should be a constructor (a class). We check if left's prototype chain
    // contains right's prototype. For our implementation, we check if right has
    // a __class_name__ marker that matches any __class_name__ in left's proto chain.
    if (get_type_id(left) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    if (get_type_id(right) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};

    // Get the class name from right (constructor's __class_name__)
    Item class_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
    Item right_name = map_get(right.map, class_key);
    if (right_name.item == 0 || get_type_id(right_name) != LMD_TYPE_STRING)
        return (Item){.item = b2it(false)};

    // Delegate to name-based check
    return js_instanceof_classname(left, right_name);
}

// instanceof check by class name string — walks prototype chain checking __class_name__
extern "C" Item js_instanceof_classname(Item left, Item classname) {
    if (get_type_id(classname) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};

    String* rn = it2s(classname);
    if (!rn) return (Item){.item = b2it(false)};

    // Check built-in types that don't use __class_name__ prototype chain
    TypeId lt = get_type_id(left);

    // Array check
    if (rn->len == 5 && strncmp(rn->chars, "Array", 5) == 0) {
        return (Item){.item = b2it(lt == LMD_TYPE_ARRAY)};
    }
    // RegExp check
    if (rn->len == 6 && strncmp(rn->chars, "RegExp", 6) == 0) {
        // RegExp objects are stored as maps with a regex handle — check for __regex__ property
        if (lt == LMD_TYPE_MAP) {
            Item rkey = (Item){.item = s2it(heap_create_name("__regex__", 9))};
            Item rval = map_get(left.map, rkey);
            return (Item){.item = b2it(rval.item != 0 && get_type_id(rval) != LMD_TYPE_NULL)};
        }
        return (Item){.item = b2it(false)};
    }

    if (lt == LMD_TYPE_MAP) {
        // Collection types: WeakSet, WeakMap, Map, Set
        if ((rn->len == 3 && strncmp(rn->chars, "Map", 3) == 0) ||
            (rn->len == 7 && strncmp(rn->chars, "WeakMap", 7) == 0)) {
            return (Item){.item = b2it(js_is_map_instance(left))};
        }
        if ((rn->len == 3 && strncmp(rn->chars, "Set", 3) == 0) ||
            (rn->len == 7 && strncmp(rn->chars, "WeakSet", 7) == 0)) {
            return (Item){.item = b2it(js_is_set_instance(left))};
        }

        // TypedArray types
        if (js_is_typed_array(left)) {
            JsTypedArray* ta = (JsTypedArray*)left.map->data;
            if (rn->len == 10 && strncmp(rn->chars, "Uint8Array", 10) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_UINT8)};
            if (rn->len == 17 && strncmp(rn->chars, "Uint8ClampedArray", 17) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_UINT8_CLAMPED)};
            if (rn->len == 11 && strncmp(rn->chars, "Uint16Array", 11) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_UINT16)};
            if (rn->len == 11 && strncmp(rn->chars, "Uint32Array", 11) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_UINT32)};
            if (rn->len == 9 && strncmp(rn->chars, "Int8Array", 9) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_INT8)};
            if (rn->len == 10 && strncmp(rn->chars, "Int16Array", 10) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_INT16)};
            if (rn->len == 10 && strncmp(rn->chars, "Int32Array", 10) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_INT32)};
            if (rn->len == 12 && strncmp(rn->chars, "Float32Array", 12) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_FLOAT32)};
            if (rn->len == 12 && strncmp(rn->chars, "Float64Array", 12) == 0)
                return (Item){.item = b2it(ta->element_type == JS_TYPED_FLOAT64)};
            // BigInt64Array/BigUint64Array — not supported, always false
            if (rn->len >= 12 && strncmp(rn->chars, "Big", 3) == 0)
                return (Item){.item = b2it(false)};
        }

        // ArrayBuffer check
        if (rn->len == 11 && strncmp(rn->chars, "ArrayBuffer", 11) == 0) {
            return (Item){.item = b2it(js_is_arraybuffer(left))};
        }
    }

    if (lt != LMD_TYPE_MAP) return (Item){.item = b2it(false)};

    Item class_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};

    // Check if left has this class name in its prototype chain
    Item obj = left;
    int depth = 0;
    while (obj.item != 0 && get_type_id(obj) == LMD_TYPE_MAP && depth < 32) {
        Item obj_name = map_get(obj.map, class_key);
        if (obj_name.item != 0 && get_type_id(obj_name) == LMD_TYPE_STRING) {
            String* on = it2s(obj_name);
            if (on->len == rn->len && strncmp(on->chars, rn->chars, on->len) == 0) {
                return (Item){.item = b2it(true)};
            }
        }
        // walk up __proto__
        Item proto_key = (Item){.item = s2it(heap_create_name("__proto__", 9))};
        obj = map_get(obj.map, proto_key);
        depth++;
    }
    return (Item){.item = b2it(false)};
}

// =============================================================================
// in operator — check if key exists in object/array
// =============================================================================

extern "C" Item js_in(Item key, Item object) {
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_MAP) {
        // JS semantics: numeric keys are coerced to strings (17 in obj === "17" in obj)
        if (get_type_id(key) == LMD_TYPE_INT || get_type_id(key) == LMD_TYPE_FLOAT) {
            char buf[64];
            if (get_type_id(key) == LMD_TYPE_INT) {
                snprintf(buf, sizeof(buf), "%lld", (long long)it2i(key));
            } else {
                snprintf(buf, sizeof(buf), "%g", it2d(key));
            }
            key = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
        }
        // check if property exists on map (including prototype chain)
        // MUST NOT trigger getters — use js_map_get_fast for data-only lookup,
        // then check for __get_<key> getter presence without invoking it.

        // Convert symbol keys (negative ints <= -1000000) to __sym_NNN string form
        if (get_type_id(key) == LMD_TYPE_INT && it2i(key) <= -1000000) {
            int64_t id = -(it2i(key) + 1000000);
            char sym_buf[32];
            snprintf(sym_buf, sizeof(sym_buf), "__sym_%lld", (long long)id);
            int sym_len = (int)strlen(sym_buf);
            // check own data property
            bool own_found = false;
            js_map_get_fast_ext(object.map, sym_buf, sym_len, &own_found);
            if (own_found) return (Item){.item = b2it(true)};
            // check getter property (__get___sym_NNN)
            char getter_key[64];
            snprintf(getter_key, sizeof(getter_key), "__get_%s", sym_buf);
            int gk_len = (int)strlen(getter_key);
            bool getter_found = false;
            js_map_get_fast_ext(object.map, getter_key, gk_len, &getter_found);
            if (getter_found) return (Item){.item = b2it(true)};
            // walk prototype chain
            Item proto = js_get_prototype(object);
            int depth = 0;
            while (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP && depth < 32) {
                bool found = false;
                js_map_get_fast_ext(proto.map, sym_buf, sym_len, &found);
                if (found) return (Item){.item = b2it(true)};
                bool gfound = false;
                js_map_get_fast_ext(proto.map, getter_key, gk_len, &gfound);
                if (gfound) return (Item){.item = b2it(true)};
                proto = js_get_prototype(proto);
                depth++;
            }
            return (Item){.item = b2it(false)};
        }

        if (get_type_id(key) == LMD_TYPE_STRING || get_type_id(key) == LMD_TYPE_SYMBOL) {
            const char* key_str = key.get_chars();
            int key_len = (int)key.get_len();
            // 1. check own data property
            bool own_found = false;
            js_map_get_fast_ext(object.map, key_str, key_len, &own_found);
            if (own_found) return (Item){.item = b2it(true)};
            // 2. check getter property (__get_<key>)
            if (key_len < 200 && key_len > 0) {
                char getter_key[256];
                snprintf(getter_key, sizeof(getter_key), "__get_%.*s", key_len, key_str);
                int gk_len = key_len + 6;
                bool getter_found = false;
                js_map_get_fast_ext(object.map, getter_key, gk_len, &getter_found);
                if (getter_found) return (Item){.item = b2it(true)};
            }
            // 3. walk prototype chain (data properties + getters)
            Item proto = js_get_prototype(object);
            int depth = 0;
            while (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP && depth < 32) {
                bool found = false;
                js_map_get_fast_ext(proto.map, key_str, key_len, &found);
                if (found) return (Item){.item = b2it(true)};
                if (key_len < 200 && key_len > 0) {
                    char getter_key[256];
                    snprintf(getter_key, sizeof(getter_key), "__get_%.*s", key_len, key_str);
                    int gk_len = key_len + 6;
                    bool getter_found = false;
                    js_map_get_fast_ext(proto.map, getter_key, gk_len, &getter_found);
                    if (getter_found) return (Item){.item = b2it(true)};
                }
                proto = js_get_prototype(proto);
                depth++;
            }
        } else {
            // non-string key: fall back to map_get
            Item result = map_get(object.map, key);
            if (result.item != ItemNull.item) return (Item){.item = b2it(true)};
        }
        return (Item){.item = b2it(false)};
    }
    if (type == LMD_TYPE_ARRAY) {
        // check if index is valid
        int idx = (int)it2i(key);
        int len = fn_len(object);
        return (Item){.item = b2it(idx >= 0 && idx < len)};
    }
    return (Item){.item = b2it(false)};
}

// =============================================================================
// nullish coalesce: a ?? b — returns b if a is null or undefined
// =============================================================================

extern "C" Item js_nullish_coalesce(Item left, Item right) {
    TypeId type = get_type_id(left);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED) return right;
    return left;
}

// =============================================================================
// Object.create — create new object with specified prototype
// =============================================================================

extern "C" Item js_object_create(Item proto) {
    Item obj = js_new_object();
    if (proto.item != 0 && get_type_id(proto) == LMD_TYPE_MAP) {
        js_set_prototype(obj, proto);
    }
    return obj;
}

// =============================================================================
// Object.getPrototypeOf — returns enriched prototype with methods/getters
// =============================================================================
// In standard JS, class methods and getters live on the prototype object.
// In our engine, they live on each instance. To support $clone() patterns like
// Object.create(Object.getPrototypeOf(this)), we create a rich prototype that
// includes __class_name__, __get_*, __set_*, and function-valued entries from
// the source instance, chained to the original __proto__ for instanceof support.

extern "C" Item js_get_prototype_of(Item object) {
    if (get_type_id(object) != LMD_TYPE_MAP) return ItemNull;
    Map* m = object.map;
    if (!m || !m->type) return js_get_prototype(object);
    TypeMap* tm = (TypeMap*)m->type;
    if (!tm->shape) return js_get_prototype(object);

    // Get the raw __proto__
    Item raw_proto = js_get_prototype(object);

    // Create a new "rich" prototype object with methods and getters
    Item rich_proto = js_new_object();

    // Walk shape entries and copy __class_name__, __get_*, __set_*, and
    // function-valued entries (methods) to the rich prototype
    ShapeEntry* e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int len = (int)e->name->length;

        bool copy = false;
        if (len == 14 && memcmp(s, "__class_name__", 14) == 0) {
            copy = true;
        } else if (len > 6 && strncmp(s, "__get_", 6) == 0) {
            copy = true;
        } else if (len > 6 && strncmp(s, "__set_", 6) == 0) {
            copy = true;
        } else if (len > 6 && strncmp(s, "__sym_", 6) == 0) {
            // Check if the symbol-keyed entry is a function (getter/method)
            Item val = _map_read_field(e, m->data);
            if (get_type_id(val) == LMD_TYPE_FUNC) {
                copy = true;
            }
        } else if (len >= 2 && s[0] != '_') {
            // Non-underscore-prefixed entry: check if it's a function (regular method)
            Item val = _map_read_field(e, m->data);
            if (get_type_id(val) == LMD_TYPE_FUNC) {
                copy = true;
            }
        }

        if (copy) {
            Item val = _map_read_field(e, m->data);
            Item key = (Item){.item = s2it(heap_create_name(s, len))};
            js_property_set(rich_proto, key, val);
        }

        e = e->next;
    }

    // Chain the rich prototype to the original __proto__ for instanceof support
    if (raw_proto.item != ItemNull.item && get_type_id(raw_proto) == LMD_TYPE_MAP) {
        js_set_prototype(rich_proto, raw_proto);
    }

    return rich_proto;
}

// =============================================================================
// Reflect.construct(target, argumentsList[, newTarget])
// Equivalent to: new target(...argumentsList)
// =============================================================================

extern Item js_constructor_create_object(Item callee);
extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
extern Item js_array_get(Item array, Item index);
extern int64_t js_array_length(Item array);

extern "C" Item js_reflect_construct(Item target, Item args_array) {
    if (get_type_id(target) != LMD_TYPE_FUNC) return ItemNull;
    // create new object inheriting from target's prototype
    Item new_obj = js_constructor_create_object(target);
    // extract args from array
    int argc = 0;
    Item* args = NULL;
    if (get_type_id(args_array) == LMD_TYPE_ARRAY) {
        argc = (int)js_array_length(args_array);
        if (argc > 0) {
            args = (Item*)alloca(argc * sizeof(Item));
            for (int i = 0; i < argc; i++) {
                Item idx = {.item = i2it(i)};
                args[i] = js_array_get(args_array, idx);
            }
        }
    }
    Item result = js_call_function(target, new_obj, args, argc);
    // if constructor returned an object, use it; otherwise return new_obj
    TypeId rt = get_type_id(result);
    if (rt == LMD_TYPE_MAP || rt == LMD_TYPE_ELEMENT || rt == LMD_TYPE_OBJECT) {
        return result;
    }
    return new_obj;
}

// =============================================================================
// Object.defineProperty — define a property on an object
// =============================================================================

extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor) {
    if (obj.item == 0) return obj;
    // extract descriptor.value
    if (get_type_id(descriptor) == LMD_TYPE_MAP) {
        Item value_key = (Item){.item = s2it(heap_create_name("value", 5))};
        Item value = js_property_get(descriptor, value_key);
        if (value.item != 0) {
            js_property_set(obj, name, value);
        } else {
            // check for getter/setter (accessor descriptor)
            Item get_key = (Item){.item = s2it(heap_create_name("get", 3))};
            Item getter = js_property_get(descriptor, get_key);
            if (getter.item != 0) {
                // store getter as the property value (simplified — no true accessor)
                js_property_set(obj, name, getter);
            }
        }
    }
    return obj;
}

// =============================================================================
// Array.isArray — check if value is an array
// =============================================================================

extern "C" Item js_array_is_array(Item value) {
    TypeId type = get_type_id(value);
    return (Item){.item = (type == LMD_TYPE_ARRAY) ? ITEM_TRUE : ITEM_FALSE};
}

// =============================================================================
// alert() — shim for benchmarks (outputs to console)
// =============================================================================

extern "C" Item js_alert(Item msg) {
    js_console_log(msg);
    return ItemNull;
}

// =============================================================================
// Object.keys — return array of property names
// =============================================================================

extern "C" Item js_object_keys(Item object) {
    TypeId type = get_type_id(object);

    // For arrays, return indices as string keys: ["0", "1", "2", ...]
    if (type == LMD_TYPE_ARRAY) {
        int len = object.array->length;
        Item result = js_array_new(len);
        for (int i = 0; i < len; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            Item key_str = (Item){.item = s2it(heap_create_name(buf))};
            js_array_set(result, (Item){.item = i2it(i)}, key_str);
        }
        return result;
    }

    if (type != LMD_TYPE_MAP) {
        return js_array_new(0);
    }

    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);

    TypeMap* tm = (TypeMap*)m->type;

    // first pass: count visible entries (skip engine-internal properties)
    int count = 0;
    ShapeEntry* e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int len = (int)e->name->length;
        // skip engine-internal properties:
        // __class_name__, __proto__, __get_*, __set_*, __sym_*, constructor
        bool skip = false;
        if (len >= 2 && s[0] == '_' && s[1] == '_') {
            // skip __class_name__, __proto__, __get_*, __set_*, __sym_*
            skip = true;
        } else if (len == 11 && memcmp(s, "constructor", 11) == 0) {
            skip = true;
        }
        if (!skip) count++;
        e = e->next;
    }

    Item result = js_array_new(count);
    e = tm->shape;
    int i = 0;
    while (e) {
        const char* s = e->name->str;
        int len = (int)e->name->length;
        bool skip = false;
        if (len >= 2 && s[0] == '_' && s[1] == '_') {
            skip = true;
        } else if (len == 11 && memcmp(s, "constructor", 11) == 0) {
            skip = true;
        }
        if (!skip) {
            char nbuf[256];
            int nlen = len < 255 ? len : 255;
            memcpy(nbuf, s, nlen);
            nbuf[nlen] = '\0';
            Item key_str = (Item){.item = s2it(heap_create_name(nbuf))};
            js_array_set(result, (Item){.item = i2it(i)}, key_str);
            i++;
        }
        e = e->next;
    }
    return result;
}

// =============================================================================
// js_to_string_val — convert any value to string (returns Item)
// =============================================================================

extern "C" Item js_object_get_own_property_symbols(Item object) {
    // Returns an array of all own Symbol keys of an object.
    // In our engine, symbols are stored as string keys "__sym_<N>" in the shape.
    // Walk shape entries, find __sym_* keys, convert back to symbol items.
    if (get_type_id(object) != LMD_TYPE_MAP) return js_array_new(0);
    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);
    TypeMap* tm = (TypeMap*)m->type;

    // first pass: count __sym_* entries
    int count = 0;
    ShapeEntry* e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int len = (int)e->name->length;
        if (len > 6 && strncmp(s, "__sym_", 6) == 0) {
            count++;
        }
        e = e->next;
    }

    Item result_arr = js_array_new(count);
    e = tm->shape;
    int i = 0;
    while (e) {
        const char* s = e->name->str;
        int len = (int)e->name->length;
        if (len > 6 && strncmp(s, "__sym_", 6) == 0) {
            // parse the numeric id from "__sym_<N>"
            long long id = atoll(s + 6);
            // convert back to symbol: symbol value = -(id + 1000000)
            int64_t sym_val = -(id + 1000000);
            js_array_set(result_arr, (Item){.item = i2it(i)}, (Item){.item = i2it(sym_val)});
            i++;
        }
        e = e->next;
    }
    return result_arr;
}

extern "C" Item js_to_string_val(Item value) {
    return js_to_string(value);
}

// =============================================================================
// Number property access — MAX_SAFE_INTEGER, MIN_SAFE_INTEGER, etc.
// =============================================================================

static Item make_double(double val) {
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = val;
    return (Item){.item = d2it(fp)};
}

extern "C" Item js_number_property(Item prop_name) {
    TypeId type = get_type_id(prop_name);
    if (type != LMD_TYPE_STRING) return ItemNull;

    String* s = it2s(prop_name);
    if (!s) return ItemNull;

    if (s->len == 16 && strncmp(s->chars, "MAX_SAFE_INTEGER", 16) == 0) return make_double(9007199254740991.0);
    if (s->len == 16 && strncmp(s->chars, "MIN_SAFE_INTEGER", 16) == 0) return make_double(-9007199254740991.0);
    if (s->len == 9 && strncmp(s->chars, "MAX_VALUE", 9) == 0) return make_double(1.7976931348623157e+308);
    if (s->len == 9 && strncmp(s->chars, "MIN_VALUE", 9) == 0) return make_double(5e-324);
    if (s->len == 17 && strncmp(s->chars, "POSITIVE_INFINITY", 17) == 0) return make_double(1.0/0.0);
    if (s->len == 17 && strncmp(s->chars, "NEGATIVE_INFINITY", 17) == 0) return make_double(-1.0/0.0);
    if (s->len == 3 && strncmp(s->chars, "NaN", 3) == 0) return make_double(0.0/0.0);
    if (s->len == 7 && strncmp(s->chars, "EPSILON", 7) == 0) return make_double(2.220446049250313e-16);

    return ItemNull;
}

// =============================================================================
// Object.values — return array of property values
// =============================================================================

extern "C" Item js_object_values(Item object) {
    TypeId type = get_type_id(object);
    if (type != LMD_TYPE_MAP) return js_array_new(0);

    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);

    TypeMap* tm = (TypeMap*)m->type;
    Item result = js_array_new(0);
    ShapeEntry* e = tm->shape;
    while (e) {
        // skip internal properties
        if (e->name && e->name->length >= 2 &&
            e->name->str[0] == '_' && e->name->str[1] == '_') {
            e = e->next;
            continue;
        }
        Item key_str = (Item){.item = s2it(heap_create_name(e->name->str, (int)e->name->length))};
        Item val = map_get(m, key_str);
        js_array_push(result, val);
        e = e->next;
    }
    return result;
}

// =============================================================================
// Object.entries — return array of [key, value] pairs
// =============================================================================

extern "C" Item js_object_entries(Item object) {
    TypeId type = get_type_id(object);
    if (type != LMD_TYPE_MAP) return js_array_new(0);

    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);

    TypeMap* tm = (TypeMap*)m->type;
    Item result = js_array_new(0);
    ShapeEntry* e = tm->shape;
    while (e) {
        if (e->name && e->name->length >= 2 &&
            e->name->str[0] == '_' && e->name->str[1] == '_') {
            e = e->next;
            continue;
        }
        Item pair = js_array_new(2);
        char nbuf[256];
        int nlen = (int)e->name->length < 255 ? (int)e->name->length : 255;
        memcpy(nbuf, e->name->str, nlen);
        nbuf[nlen] = '\0';
        Item key_str = (Item){.item = s2it(heap_create_name(nbuf))};
        js_array_set(pair, (Item){.item = i2it(0)}, key_str);
        js_array_set(pair, (Item){.item = i2it(1)}, map_get(m, key_str));
        js_array_push(result, pair);
        e = e->next;
    }
    return result;
}

// =============================================================================
// Object.fromEntries(iterable) — create object from [key, value] pairs
// =============================================================================

extern "C" Item js_object_from_entries(Item iterable) {
    TypeId type = get_type_id(iterable);
    if (type != LMD_TYPE_ARRAY) return js_new_object();

    Item result = js_new_object();
    int64_t len = js_array_length(iterable);
    for (int64_t i = 0; i < len; i++) {
        Item pair = js_array_get(iterable, (Item){.item = i2it(i)});
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item key = js_array_get(pair, (Item){.item = i2it(0)});
        Item val = js_array_get(pair, (Item){.item = i2it(1)});
        Item key_str = js_to_string(key);
        js_property_set(result, key_str, val);
    }
    return result;
}

// =============================================================================
// Object.is(value1, value2) — SameValue comparison (handles NaN, +0/-0)
// =============================================================================

extern "C" Item js_object_is(Item left, Item right) {
    TypeId left_type = get_type_id(left);
    TypeId right_type = get_type_id(right);

    bool left_is_num = (left_type == LMD_TYPE_INT || left_type == LMD_TYPE_INT64 || left_type == LMD_TYPE_FLOAT);
    bool right_is_num = (right_type == LMD_TYPE_INT || right_type == LMD_TYPE_INT64 || right_type == LMD_TYPE_FLOAT);
    if (left_is_num && right_is_num) {
        double l = (left_type == LMD_TYPE_FLOAT) ? it2d(left) : (double)it2i(left);
        double r = (right_type == LMD_TYPE_FLOAT) ? it2d(right) : (double)it2i(right);
        // Object.is(NaN, NaN) → true (unlike ===)
        if (isnan(l) && isnan(r)) return (Item){.item = b2it(true)};
        if (isnan(l) || isnan(r)) return (Item){.item = b2it(false)};
        // Object.is(+0, -0) → false (unlike ===)
        if (l == 0.0 && r == 0.0) {
            return (Item){.item = b2it(signbit(l) == signbit(r))};
        }
        return (Item){.item = b2it(l == r)};
    }

    // Fall back to strict equality for non-numeric types
    return js_strict_equal(left, right);
}

// =============================================================================
// Object.assign(target, ...sources)
// =============================================================================

extern "C" Item js_object_assign(Item target, Item* sources, int count) {
    if (get_type_id(target) != LMD_TYPE_MAP) return target;
    for (int i = 0; i < count; i++) {
        Item source = sources[i];
        if (get_type_id(source) != LMD_TYPE_MAP) continue;
        Map* m = source.map;
        if (!m || !m->type) continue;
        TypeMap* tm = (TypeMap*)m->type;
        ShapeEntry* e = tm->shape;
        while (e) {
            if (e->name) {
                Item key = (Item){.item = s2it(heap_create_name(e->name->str, (int)e->name->length))};
                Item val = map_get(m, key);
                js_property_set(target, key, val);
            }
            e = e->next;
        }
    }
    return target;
}

// Object spread: copy all own enumerable properties from source into target
// Used for { ...source } in object literals
extern "C" Item js_object_spread_into(Item target, Item source) {
    if (get_type_id(target) != LMD_TYPE_MAP) return target;
    if (get_type_id(source) != LMD_TYPE_MAP) return target;
    Map* m = source.map;
    if (!m || !m->type) return target;
    TypeMap* tm = (TypeMap*)m->type;
    ShapeEntry* e = tm->shape;
    while (e) {
        if (e->name) {
            Item key = (Item){.item = s2it(heap_create_name(e->name->str, (int)e->name->length))};
            Item val = map_get(m, key);
            js_property_set(target, key, val);
        }
        e = e->next;
    }
    return target;
}

// =============================================================================
// obj.hasOwnProperty(key) / Object.hasOwn(obj, key)
// =============================================================================

extern "C" Item js_has_own_property(Item obj, Item key) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    // Convert symbol keys to their internal string representation
    Item k;
    if (get_type_id(key) == LMD_TYPE_INT && it2i(key) <= -1000000) {
        int64_t id = -(it2i(key) + 1000000);
        char buf[32];
        snprintf(buf, sizeof(buf), "__sym_%lld", (long long)id);
        k = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
    } else {
        k = js_to_string(key);
    }
    if (get_type_id(k) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* ks = it2s(k);
    if (!ks) return (Item){.item = b2it(false)};
    Map* m = obj.map;
    if (!m || !m->type) return (Item){.item = b2it(false)};
    TypeMap* tm = (TypeMap*)m->type;
    // Check shape for key existence — unlike map_get which returns ItemNull for both
    // "not found" and "found with null value", causing hasOwnProperty to incorrectly
    // return false for properties set to null.
    ShapeEntry* e = tm->shape;
    while (e) {
        if (e->name && e->name->length == (size_t)ks->len &&
            strncmp(e->name->str, ks->chars, ks->len) == 0) {
            return (Item){.item = b2it(true)};
        }
        e = e->next;
    }
    return (Item){.item = b2it(false)};
}

// =============================================================================
// Object.freeze(obj) — set __frozen__ flag, Object.isFrozen(obj)
// =============================================================================

extern "C" Item js_object_freeze(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return obj;
    Item key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    js_property_set(obj, key, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_frozen(Item obj) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    Item key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    Item val = map_get(obj.map, key);
    return (Item){.item = b2it(js_is_truthy(val))};
}

// =============================================================================
// Number static methods — Number.isInteger, Number.isFinite, Number.isNaN, Number.isSafeInteger
// =============================================================================

extern "C" Item js_number_is_integer(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) return (Item){.item = b2it(true)};
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        return (Item){.item = b2it(isfinite(d) && d == floor(d))};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_number_is_finite(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) return (Item){.item = b2it(true)};
    if (type == LMD_TYPE_FLOAT) {
        return (Item){.item = b2it(isfinite(it2d(value)))};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_number_is_nan(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_FLOAT) {
        return (Item){.item = b2it(isnan(it2d(value)))};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_number_is_safe_integer(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) return (Item){.item = b2it(true)};
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        return (Item){.item = b2it(isfinite(d) && d == floor(d) && fabs(d) <= 9007199254740991.0)};
    }
    return (Item){.item = b2it(false)};
}

// =============================================================================
// Array.from(iterable) — convert array-like to array
// =============================================================================

extern "C" Item js_array_from(Item iterable) {
    TypeId tid = get_type_id(iterable);
    if (tid == LMD_TYPE_ARRAY) {
        // shallow copy
        Array* src = iterable.array;
        Item result = js_array_new(src->length);
        Array* dst = result.array;
        memcpy(dst->items, src->items, src->length * sizeof(Item));
        dst->length = src->length;
        return result;
    }
    // TypedArray: convert each element to a JS number in a regular array
    if (js_is_typed_array(iterable)) {
        JsTypedArray* ta = (JsTypedArray*)iterable.map->data;
        int len = ta->length;
        Item result = js_array_new(0);
        for (int i = 0; i < len; i++) {
            Item idx = (Item){.item = i2it(i)};
            Item val = js_typed_array_get(iterable, idx);
            array_push(result.array, val);
        }
        return result;
    }
    if (tid == LMD_TYPE_STRING) {
        // split string into array of single characters
        String* s = it2s(iterable);
        if (!s) return js_array_new(0);
        Item result = js_array_new(0);
        for (int i = 0; i < (int)s->len; i++) {
            String* ch = heap_strcpy(&s->chars[i], 1);
            js_array_push(result, (Item){.item = s2it(ch)});
        }
        return result;
    }
    // Use js_iterable_to_array for Map, Set, generators, and other iterables
    Item converted = js_iterable_to_array(iterable);
    if (get_type_id(converted) == LMD_TYPE_ARRAY) {
        // shallow copy to return a new array
        Array* src = converted.array;
        Item result = js_array_new(src->length);
        Array* dst = result.array;
        memcpy(dst->items, src->items, src->length * sizeof(Item));
        dst->length = src->length;
        return result;
    }
    return js_array_new(0);
}

// Array.from(iterable, mapFn) — with optional mapper function
extern "C" Item js_array_from_with_mapper(Item iterable, Item mapFn) {
    Item arr = js_array_from(iterable);
    // Apply mapper if provided and is a function
    if (get_type_id(mapFn) == LMD_TYPE_FUNC) {
        int64_t len = js_array_length(arr);
        for (int64_t i = 0; i < len; i++) {
            Item elem = js_array_get(arr, (Item){.item = i2it(i)});
            Item idx_item = (Item){.item = i2it(i)};
            Item args[2] = {elem, idx_item};
            Item mapped = js_call_function(mapFn, ItemNull, args, 2);
            js_array_set(arr, (Item){.item = i2it(i)}, mapped);
        }
    }
    return arr;
}

// =============================================================================
// JSON.parse(str) — parse JSON string to Lambda object
// =============================================================================

extern Input* js_input;

extern "C" Item js_json_parse(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return ItemNull;

    // null-terminate for the parser
    char* buf = (char*)alloca(s->len + 1);
    memcpy(buf, s->chars, s->len);
    buf[s->len] = '\0';

    if (!js_input) {
        log_error("js_json_parse: no input context");
        return ItemNull;
    }

    Item result = parse_json_to_item(js_input, buf);
    return result;
}

// =============================================================================
// JSON.stringify(value, replacer?, space?) — convert Lambda object to JSON string
// =============================================================================

extern "C" Item js_json_stringify(Item value) {
    if (get_type_id(value) == LMD_TYPE_NULL) {
        return (Item){.item = s2it(heap_create_name("null", 4))};
    }

    Pool* pool = pool_create();
    String* json = format_json(pool, value);
    if (!json) {
        pool_destroy(pool);
        return ItemNull;
    }

    // copy string to GC heap before destroying pool
    String* result = heap_strcpy(json->chars, json->len);
    pool_destroy(pool);
    return (Item){.item = s2it(result)};
}

// =============================================================================
// delete operator — remove property from object
// =============================================================================

extern "C" Item js_delete_property(Item obj, Item key) {
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    // set property to null (real deletion would require map_remove)
    js_property_set(obj, key, ItemNull);
    return (Item){.item = b2it(true)};
}

// =============================================================================
// v12: encodeURIComponent / decodeURIComponent
// =============================================================================

extern "C" Item js_encodeURIComponent(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};
    char* encoded = url_encode_component(s->chars, s->len);
    if (!encoded) return (Item){.item = s2it(heap_create_name("", 0))};
    String* result = heap_create_name(encoded, strlen(encoded));
    free(encoded);
    return (Item){.item = s2it(result)};
}

extern "C" Item js_decodeURIComponent(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};
    size_t decoded_len = 0;
    char* decoded = url_decode_component(s->chars, s->len, &decoded_len);
    if (!decoded) return (Item){.item = s2it(heap_create_name("", 0))};
    String* result = heap_create_name(decoded, decoded_len);
    free(decoded);
    return (Item){.item = s2it(result)};
}

// =============================================================================
// unescape(string) — legacy percent-decoding (%XX and %uXXXX)
// =============================================================================

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

extern "C" Item js_unescape(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};

    const char* src = s->chars;
    int src_len = s->len;

    // allocate output buffer (worst case: all %XX with values >= 0x80 → 2 bytes each,
    // but that's still ≤ src_len since 3 input bytes → 2 output bytes)
    char* buf = (char*)malloc(src_len * 2 + 1);
    if (!buf) return (Item){.item = s2it(heap_create_name("", 0))};

    int out = 0;
    int i = 0;
    while (i < src_len) {
        if (src[i] == '%' && i + 2 < src_len) {
            if (src[i + 1] == 'u' && i + 5 < src_len) {
                // %uXXXX
                int d0 = hex_digit_value(src[i + 2]);
                int d1 = hex_digit_value(src[i + 3]);
                int d2 = hex_digit_value(src[i + 4]);
                int d3 = hex_digit_value(src[i + 5]);
                if (d0 >= 0 && d1 >= 0 && d2 >= 0 && d3 >= 0) {
                    int cp = (d0 << 12) | (d1 << 8) | (d2 << 4) | d3;
                    if (cp <= 0x7F) {
                        buf[out++] = (char)cp;
                    } else if (cp <= 0x7FF) {
                        buf[out++] = (char)(0xC0 | (cp >> 6));
                        buf[out++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        buf[out++] = (char)(0xE0 | (cp >> 12));
                        buf[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        buf[out++] = (char)(0x80 | (cp & 0x3F));
                    }
                    i += 6;
                    continue;
                }
            }
            // %XX
            int d0 = hex_digit_value(src[i + 1]);
            int d1 = hex_digit_value(src[i + 2]);
            if (d0 >= 0 && d1 >= 0) {
                int cp = (d0 << 4) | d1;
                if (cp <= 0x7F) {
                    buf[out++] = (char)cp;
                } else {
                    // UTF-8 encode values 0x80-0xFF as 2-byte sequences
                    buf[out++] = (char)(0xC0 | (cp >> 6));
                    buf[out++] = (char)(0x80 | (cp & 0x3F));
                }
                i += 3;
                continue;
            }
        }
        buf[out++] = src[i++];
    }

    String* result = heap_create_name(buf, out);
    free(buf);
    return (Item){.item = s2it(result)};
}

// =============================================================================
// v12: globalThis
// =============================================================================

static Item js_global_this_obj = {0};

extern "C" Item js_get_global_this() {
    if (js_global_this_obj.item == 0) {
        js_global_this_obj = js_new_object();
        // populate standard globals
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("undefined", 9))}, ItemNull);
        double* nan_p = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *nan_p = NAN;
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("NaN", 3))}, (Item){.item = d2it(nan_p)});
        double* inf_p = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *inf_p = INFINITY;
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("Infinity", 8))}, (Item){.item = d2it(inf_p)});
    }
    return js_global_this_obj;
}

// =============================================================================
// v12: Symbol API
// =============================================================================

#include "../../lib/hashmap.h"

// symbol registry entry for Symbol.for() / Symbol.keyFor()
struct JsSymbolEntry {
    char key[128];
    uint64_t symbol_id;
};

static uint64_t js_symbol_next_id = 100;  // IDs 1-99 reserved for well-known symbols
static HashMap* js_symbol_registry = nullptr;  // string key -> JsSymbolEntry

// well-known symbol IDs (pre-allocated)
#define JS_SYMBOL_ID_ITERATOR       1
#define JS_SYMBOL_ID_TO_PRIMITIVE   2
#define JS_SYMBOL_ID_HAS_INSTANCE   3
#define JS_SYMBOL_ID_TO_STRING_TAG  4

static int js_symbol_entry_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    return strcmp(((const JsSymbolEntry*)a)->key, ((const JsSymbolEntry*)b)->key);
}

static uint64_t js_symbol_entry_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JsSymbolEntry* e = (const JsSymbolEntry*)item;
    return hashmap_sip(e->key, strlen(e->key), seed0, seed1);
}

static void js_symbol_init_registry() {
    if (!js_symbol_registry) {
        js_symbol_registry = hashmap_new(sizeof(JsSymbolEntry), 16, 0, 0,
            js_symbol_entry_hash, js_symbol_entry_compare, NULL, NULL);
    }
}

// create Item encoding for a symbol: use LMD_TYPE_INT with a high-bit marker
// symbol items are encoded as negative ints that won't collide with normal ints
static Item js_make_symbol_item(uint64_t id) {
    // encode as an int with a special range: -(id + 1000000)
    return (Item){.item = i2it(-(int64_t)(id + 1000000))};
}

static bool js_is_symbol_item(Item item) {
    if (get_type_id(item) != LMD_TYPE_INT) return false;
    int64_t v = it2i(item);
    return v <= -1000000;
}

static uint64_t js_symbol_item_id(Item item) {
    return (uint64_t)(-(it2i(item) + 1000000));
}

extern "C" Item js_symbol_create(Item description) {
    uint64_t id = js_symbol_next_id++;
    // store description on a map wrapper so toString can access it
    Item sym = js_make_symbol_item(id);
    (void)description;  // description is only for debugging
    return sym;
}

extern "C" Item js_symbol_for(Item key) {
    js_symbol_init_registry();
    String* s = it2s(js_to_string(key));
    if (!s) return js_symbol_create(key);

    JsSymbolEntry lookup;
    int klen = s->len < 127 ? (int)s->len : 127;
    memcpy(lookup.key, s->chars, klen);
    lookup.key[klen] = '\0';

    JsSymbolEntry* found = (JsSymbolEntry*)hashmap_get(js_symbol_registry, &lookup);
    if (found) return js_make_symbol_item(found->symbol_id);

    // create new entry
    lookup.symbol_id = js_symbol_next_id++;
    hashmap_set(js_symbol_registry, &lookup);
    return js_make_symbol_item(lookup.symbol_id);
}

extern "C" Item js_symbol_key_for(Item sym) {
    if (!js_is_symbol_item(sym)) return ItemNull;
    js_symbol_init_registry();
    uint64_t id = js_symbol_item_id(sym);

    // linear scan — symbol registry is small
    size_t iter = 0;
    void* entry;
    while (hashmap_iter(js_symbol_registry, &iter, &entry)) {
        JsSymbolEntry* e = (JsSymbolEntry*)entry;
        if (e->symbol_id == id) {
            return (Item){.item = s2it(heap_create_name(e->key, strlen(e->key)))};
        }
    }
    return ItemNull;  // not in global registry
}

extern "C" Item js_symbol_to_string(Item sym) {
    if (!js_is_symbol_item(sym)) {
        return (Item){.item = s2it(heap_create_name("Symbol()", 8))};
    }
    // check global registry for description
    uint64_t id = js_symbol_item_id(sym);

    // well-known symbols
    if (id == JS_SYMBOL_ID_ITERATOR)      return (Item){.item = s2it(heap_create_name("Symbol(Symbol.iterator)", 23))};
    if (id == JS_SYMBOL_ID_TO_PRIMITIVE)   return (Item){.item = s2it(heap_create_name("Symbol(Symbol.toPrimitive)", 26))};
    if (id == JS_SYMBOL_ID_HAS_INSTANCE)   return (Item){.item = s2it(heap_create_name("Symbol(Symbol.hasInstance)", 26))};
    if (id == JS_SYMBOL_ID_TO_STRING_TAG)  return (Item){.item = s2it(heap_create_name("Symbol(Symbol.toStringTag)", 26))};

    // check registry
    if (js_symbol_registry) {
        size_t iter = 0;
        void* entry;
        while (hashmap_iter(js_symbol_registry, &iter, &entry)) {
            JsSymbolEntry* e = (JsSymbolEntry*)entry;
            if (e->symbol_id == id) {
                char buf[160];
                snprintf(buf, sizeof(buf), "Symbol(%s)", e->key);
                return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
            }
        }
    }

    return (Item){.item = s2it(heap_create_name("Symbol()", 8))};
}

// =============================================================================
// URL Constructor — wraps lib/url.c
// =============================================================================

static Item js_url_to_object(Url* url) {
    if (!url || !url->is_valid) {
        if (url) url_destroy(url);
        return ItemNull;
    }
    Item obj = js_new_object();

    // Helper macro to set a string property
    #define URL_SET_PROP(propname, getter) do { \
        const char* _v = getter(url); \
        Item _key = (Item){.item = s2it(heap_create_name(propname))}; \
        Item _val = _v ? (Item){.item = s2it(heap_create_name(_v, strlen(_v)))} : (Item){.item = s2it(heap_create_name("", 0))}; \
        js_property_set(obj, _key, _val); \
    } while(0)

    URL_SET_PROP("href", url_get_href);
    // Compute origin: protocol + "//" + host (hostname + optional port)
    {
        const char* proto = url_get_protocol(url);
        const char* host = url_get_host(url);
        const char* hostname = url_get_hostname(url);
        // For schemes like mailto:, tel: — origin is "null"
        bool has_authority = proto && (strncmp(proto, "http", 4) == 0 ||
                                       strncmp(proto, "ftp", 3) == 0 ||
                                       strncmp(proto, "ws", 2) == 0);
        char origin_buf[512];
        if (has_authority && hostname && hostname[0]) {
            const char* h = (host && host[0]) ? host : hostname;
            snprintf(origin_buf, sizeof(origin_buf), "%s//%s", proto ? proto : "", h);
        } else {
            snprintf(origin_buf, sizeof(origin_buf), "null");
        }
        Item o_key = (Item){.item = s2it(heap_create_name("origin"))};
        Item o_val = (Item){.item = s2it(heap_create_name(origin_buf, strlen(origin_buf)))};
        js_property_set(obj, o_key, o_val);
    }
    URL_SET_PROP("protocol", url_get_protocol);
    URL_SET_PROP("username", url_get_username);
    URL_SET_PROP("password", url_get_password);
    URL_SET_PROP("host", url_get_host);
    URL_SET_PROP("hostname", url_get_hostname);
    URL_SET_PROP("port", url_get_port);
    URL_SET_PROP("pathname", url_get_pathname);
    URL_SET_PROP("search", url_get_search);
    URL_SET_PROP("hash", url_get_hash);

    #undef URL_SET_PROP

    url_destroy(url);
    return obj;
}

extern "C" Item js_url_construct(Item input) {
    TypeId tid = get_type_id(input);
    if (tid != LMD_TYPE_STRING) return ItemNull;
    String* s = it2s(input);
    if (!s || s->len == 0) return ItemNull;

    Url* url = url_parse(s->chars);
    return js_url_to_object(url);
}

extern "C" Item js_url_construct_with_base(Item input, Item base) {
    TypeId tid_base = get_type_id(base);
    if (tid_base != LMD_TYPE_STRING) {
        return js_url_construct(input);
    }
    String* base_str = it2s(base);
    if (!base_str || base_str->len == 0) {
        return js_url_construct(input);
    }

    TypeId tid = get_type_id(input);
    if (tid != LMD_TYPE_STRING) return ItemNull;
    String* s = it2s(input);
    if (!s) return ItemNull;

    Url* base_url = url_parse(base_str->chars);
    if (!base_url || !base_url->is_valid) {
        if (base_url) url_destroy(base_url);
        return ItemNull;
    }
    Url* url = url_parse_with_base(s->chars, base_url);
    url_destroy(base_url);
    return js_url_to_object(url);
}

extern "C" Item js_url_parse(Item input, Item base) {
    TypeId tid = get_type_id(input);
    if (tid != LMD_TYPE_STRING) return ItemNull;

    TypeId tid_base = get_type_id(base);
    if (tid_base == LMD_TYPE_STRING) {
        return js_url_construct_with_base(input, base);
    }
    return js_url_construct(input);
}

extern "C" Item js_url_can_parse(Item input) {
    TypeId tid = get_type_id(input);
    if (tid != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
    String* s = it2s(input);
    if (!s || s->len == 0) return (Item){.item = b2it(false)};

    Url* url = url_parse(s->chars);
    if (!url) return (Item){.item = b2it(false)};
    bool valid = url->is_valid;
    url_destroy(url);
    return valid ? (Item){.item = b2it(true)} : (Item){.item = b2it(false)};
}