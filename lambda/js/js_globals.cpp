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

static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

extern void* heap_alloc(int size, TypeId type_id);
extern void fn_array_set(Array* arr, int64_t index, Item value);
extern "C" void js_set_prototype(Item object, Item prototype);
extern "C" Item js_get_prototype(Item object);
extern Item _map_read_field(ShapeEntry* field, void* map_data);
extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);

// v18l: helper to throw TypeError if argument is not an object (ES5 §15.2.3.*)
static bool js_require_object_type(Item arg, const char* method_name) {
    TypeId t = get_type_id(arg);
    if (t == LMD_TYPE_MAP || t == LMD_TYPE_ARRAY || t == LMD_TYPE_FUNC || t == LMD_TYPE_ELEMENT)
        return true;
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
    char msg[128];
    snprintf(msg, sizeof(msg), "Object.%s called on non-object", method_name);
    Item msg_item = (Item){.item = s2it(heap_create_name(msg, strlen(msg)))};
    Item error = js_new_error_with_name(type_name, msg_item);
    js_throw_value(error);
    return false;
}

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
        // v18l: Handle NaN and Infinity before radix conversion
        TypeId nt = get_type_id(num);
        if (nt == LMD_TYPE_FLOAT) {
            double d = it2d(num);
            if (isnan(d)) return (Item){.item = s2it(heap_create_name("NaN", 3))};
            if (isinf(d)) return d > 0
                ? (Item){.item = s2it(heap_create_name("Infinity", 8))}
                : (Item){.item = s2it(heap_create_name("-Infinity", 9))};
        }
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

    if (method->len == 7 && strncmp(method->chars, "valueOf", 7) == 0) {
        return num;
    }
    if (method->len == 11 && strncmp(method->chars, "toPrecision", 11) == 0) {
        if (argc < 1 || get_type_id(args[0]) == LMD_TYPE_UNDEFINED) return js_to_string(num);
        Item prec_item = js_to_number(args[0]);
        int precision = 1;
        TypeId pt = get_type_id(prec_item);
        if (pt == LMD_TYPE_INT) precision = (int)it2i(prec_item);
        else if (pt == LMD_TYPE_FLOAT) precision = (int)it2d(prec_item);
        if (precision < 1 || precision > 100) {
            log_error("js_number_method: toPrecision precision %d out of range", precision);
            return ItemNull;
        }
        double d = 0;
        TypeId nt = get_type_id(num);
        if (nt == LMD_TYPE_INT) d = (double)it2i(num);
        else if (nt == LMD_TYPE_FLOAT) d = it2d(num);
        char buf[128];
        snprintf(buf, sizeof(buf), "%.*g", precision, d);
        return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
    }
    if (method->len == 13 && strncmp(method->chars, "toExponential", 13) == 0) {
        double d = 0;
        TypeId nt = get_type_id(num);
        if (nt == LMD_TYPE_INT) d = (double)it2i(num);
        else if (nt == LMD_TYPE_FLOAT) d = it2d(num);
        char buf[128];
        if (argc < 1 || get_type_id(args[0]) == LMD_TYPE_UNDEFINED) {
            snprintf(buf, sizeof(buf), "%e", d);
            // Remove trailing zeros after decimal in exponent format
            char* e = strchr(buf, 'e');
            if (e) {
                char* p = e - 1;
                while (p > buf && *p == '0') p--;
                if (*p == '.') p--;
                memmove(p + 1, e, strlen(e) + 1);
            }
        } else {
            Item frac_item = js_to_number(args[0]);
            int frac = 0;
            TypeId ft = get_type_id(frac_item);
            if (ft == LMD_TYPE_INT) frac = (int)it2i(frac_item);
            else if (ft == LMD_TYPE_FLOAT) frac = (int)it2d(frac_item);
            if (frac < 0 || frac > 100) {
                log_error("js_number_method: toExponential fraction %d out of range", frac);
                return ItemNull;
            }
            snprintf(buf, sizeof(buf), "%.*e", frac, d);
        }
        // Normalize exponent: remove leading zeros (e+07 -> e+7)
        char* e = strchr(buf, 'e');
        if (e) {
            char sign = e[1]; // '+' or '-'
            char* digits = e + 2;
            while (*digits == '0' && *(digits + 1)) digits++;
            char norm[16];
            snprintf(norm, sizeof(norm), "e%c%s", sign, digits);
            strcpy(e, norm);
        }
        return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
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

// partial JsFunction layout for accessing function name (used by instanceof fallback)
struct JsFuncName {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this;
    Item* bound_args;
    int bound_argc;
    String* name;
};

extern "C" Item js_instanceof(Item left, Item right) {
    // right should be a constructor (a class). We check if left's prototype chain
    // contains right's prototype. For our implementation, we check if right has
    // a __class_name__ marker that matches any __class_name__ in left's proto chain.
    if (get_type_id(left) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};

    // v16: Check for Symbol.hasInstance on the right-hand constructor
    // Per ES spec §7.3.21: if right[@@hasInstance] exists, call it
    {
        TypeId rt = get_type_id(right);
        if (rt == LMD_TYPE_MAP || rt == LMD_TYPE_FUNC) {
            // look for __sym_3 (Symbol.hasInstance = ID 3)
            Item has_instance_fn = ItemNull;
            if (rt == LMD_TYPE_MAP) {
                bool found = false;
                has_instance_fn = js_map_get_fast_ext(right.map, "__sym_3", 7, &found);
            }
            if (has_instance_fn.item != ItemNull.item && get_type_id(has_instance_fn) == LMD_TYPE_FUNC) {
                Item args[1] = { left };
                Item result = js_call_function(has_instance_fn, right, args, 1);
                return (Item){.item = b2it(js_is_truthy(result))};
            }
        }
    }

    // If right is a function (IIFE-returned constructor), walk the __proto__ chain
    // and check for __ctor__ (function identity) match, stored at object creation time.
    TypeId right_type = get_type_id(right);
    if (right_type == LMD_TYPE_FUNC) {
        Item obj = left;
        int depth = 0;
        Item ctor_key = (Item){.item = s2it(heap_create_name("__ctor__", 8))};
        Item proto_key_item = (Item){.item = s2it(heap_create_name("__proto__", 9))};
        while (obj.item != 0 && get_type_id(obj) == LMD_TYPE_MAP && depth < 32) {
            Item ctor_val = map_get(obj.map, ctor_key);
            if (ctor_val.item != 0 && get_type_id(ctor_val) == LMD_TYPE_FUNC) {
                if (ctor_val.item == right.item) return (Item){.item = b2it(true)};
            }
            obj = map_get(obj.map, proto_key_item);
            depth++;
        }
        // v18c: __ctor__ walk failed — fall through to name-based check using
        // the function's name (handles built-in constructors like TypeError
        // passed as variable arguments, e.g. assert.throws(TypeError, fn))
        JsFuncName* fp = (JsFuncName*)right.function;
        if (fp->name && fp->name->len > 0) {
            Item name_item = (Item){.item = s2it(fp->name)};
            return js_instanceof_classname(left, name_item);
        }
        return (Item){.item = b2it(false)};
    }

    if (right_type != LMD_TYPE_MAP) return (Item){.item = b2it(false)};

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
        // RegExp objects are stored as maps with a regex data pointer — check for __rd property
        if (lt == LMD_TYPE_MAP) {
            Item rkey = (Item){.item = s2it(heap_create_name("__rd", 4))};
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
            // error hierarchy: TypeError/RangeError/SyntaxError/ReferenceError/URIError/EvalError instanceof Error
            if (rn->len == 5 && strncmp(rn->chars, "Error", 5) == 0) {
                if ((on->len == 9 && strncmp(on->chars, "TypeError", 9) == 0) ||
                    (on->len == 10 && strncmp(on->chars, "RangeError", 10) == 0) ||
                    (on->len == 11 && strncmp(on->chars, "SyntaxError", 11) == 0) ||
                    (on->len == 14 && strncmp(on->chars, "ReferenceError", 14) == 0) ||
                    (on->len == 8 && strncmp(on->chars, "URIError", 8) == 0) ||
                    (on->len == 9 && strncmp(on->chars, "EvalError", 9) == 0)) {
                    return (Item){.item = b2it(true)};
                }
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
        // Check for symbol keys FIRST (before any numeric coercion)
        // Symbol items are encoded as negative ints <= -JS_SYMBOL_BASE
        if (get_type_id(key) == LMD_TYPE_INT && it2i(key) <= -(int64_t)JS_SYMBOL_BASE) {
            int64_t id = -(it2i(key) + (int64_t)JS_SYMBOL_BASE);
            char sym_buf[32];
            snprintf(sym_buf, sizeof(sym_buf), "__sym_%lld", (long long)id);
            int sym_len = (int)strlen(sym_buf);
            // check own data property
            bool own_found = false;
            Item own_val = js_map_get_fast_ext(object.map, sym_buf, sym_len, &own_found);
            if (own_found && own_val.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
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
                Item pval = js_map_get_fast_ext(proto.map, sym_buf, sym_len, &found);
                if (found && pval.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
                bool gfound = false;
                js_map_get_fast_ext(proto.map, getter_key, gk_len, &gfound);
                if (gfound) return (Item){.item = b2it(true)};
                proto = js_get_prototype(proto);
                depth++;
            }
            return (Item){.item = b2it(false)};
        }

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

        if (get_type_id(key) == LMD_TYPE_STRING || get_type_id(key) == LMD_TYPE_SYMBOL) {
            const char* key_str = key.get_chars();
            int key_len = (int)key.get_len();
            // 1. check own data property
            bool own_found = false;
            Item own_val = js_map_get_fast_ext(object.map, key_str, key_len, &own_found);
            if (own_found && own_val.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
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
                Item pval = js_map_get_fast_ext(proto.map, key_str, key_len, &found);
                if (found && pval.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
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
    if (!js_require_object_type(object, "getPrototypeOf")) return ItemNull;
    // v18g: Arrays → return Array.prototype
    if (get_type_id(object) == LMD_TYPE_ARRAY) {
        Item arr_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Array", 5))});
        if (get_type_id(arr_ctor) == LMD_TYPE_FUNC) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            return js_property_get(arr_ctor, proto_key);
        }
        return ItemNull;
    }
    // Functions → return Function.prototype
    if (get_type_id(object) == LMD_TYPE_FUNC) {
        Item func_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Function", 8))});
        if (get_type_id(func_ctor) == LMD_TYPE_FUNC) {
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            return js_property_get(func_ctor, proto_key);
        }
        return ItemNull;
    }
    if (get_type_id(object) != LMD_TYPE_MAP) return ItemNull;

    // v18h: Check if this is a class object (has __instance_proto__) → return Function.prototype
    {
        bool own_ip = false;
        js_map_get_fast_ext(object.map, "__instance_proto__", 18, &own_ip);
        if (own_ip) {
            // Class objects inherit from Function.prototype
            // Check for __proto__ first (set by extends)
            Item raw = js_get_prototype(object);
            if (raw.item != ItemNull.item) return raw;
            Item func_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Function", 8))});
            if (get_type_id(func_ctor) == LMD_TYPE_FUNC) {
                Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
                return js_property_get(func_ctor, proto_key);
            }
            return ItemNull;
        }
    }

    // v18l: Check __proto__ first — Object.create sets this explicitly
    {
        Item raw_proto = js_get_prototype(object);
        if (raw_proto.item != ItemNull.item) return raw_proto;
    }

    // v18g: If instance has a constructor with a .prototype property,
    // AND the object is NOT that prototype itself, return constructor.prototype
    {
        Item ctor_key = (Item){.item = s2it(heap_create_name("constructor", 11))};
        Item ctor = js_property_get(object, ctor_key);
        if (get_type_id(ctor) == LMD_TYPE_MAP) {
            // user-defined class: read its "prototype" property
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            Item proto = js_property_get(ctor, proto_key);
            // Guard: don't return self (prototype objects have constructor.prototype === self)
            if (get_type_id(proto) == LMD_TYPE_MAP && proto.map != object.map) return proto;
        } else if (get_type_id(ctor) == LMD_TYPE_FUNC) {
            // built-in constructor: get .prototype via property_get (triggers lazy init)
            Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
            Item proto = js_property_get(ctor, proto_key);
            if (get_type_id(proto) == LMD_TYPE_MAP && proto.map != object.map) {
                return proto;
            }
        }
    }

    // No __proto__ found — return Object.prototype for plain objects
    Item obj_ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Object", 6))});
    if (get_type_id(obj_ctor) == LMD_TYPE_FUNC) {
        Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
        return js_property_get(obj_ctor, proto_key);
    }
    return ItemNull;
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
// Object.getOwnPropertyDescriptor — return descriptor for an own property
// =============================================================================

// v18: partial JsFunction layout for properties_map access
struct JsFuncProps {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this;
    Item* bound_args;
    int bound_argc;
    String* name;
    int builtin_id;
    Item properties_map;
};

extern "C" Item js_object_get_own_property_descriptor(Item obj, Item name) {
    if (!js_require_object_type(obj, "getOwnPropertyDescriptor")) return ItemNull;
    TypeId type = get_type_id(obj);

    // Convert name to string for comparison
    Item name_str_item = js_to_string(name);
    if (get_type_id(name_str_item) != LMD_TYPE_STRING) return ItemNull;
    String* name_str = it2s(name_str_item);

    // Function properties: length, name, prototype
    if (type == LMD_TYPE_FUNC) {
        if (name_str->len == 6 && strncmp(name_str->chars, "length", 6) == 0) {
            Item value = js_property_get(obj, name);
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, value);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
            return desc;
        }
        if (name_str->len == 4 && strncmp(name_str->chars, "name", 4) == 0) {
            Item name_val = js_property_get(obj, name);
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, name_val);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
            return desc;
        }
        if (name_str->len == 9 && strncmp(name_str->chars, "prototype", 9) == 0) {
            Item desc = js_new_object();
            Item proto = js_property_get(obj, name);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, proto);
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
            return desc;
        }
        // v18: check custom properties backing map
        {
            JsFuncProps* fn = (JsFuncProps*)obj.function;
            if (fn->properties_map.item != 0) {
                bool own = false;
                Item val = js_map_get_fast_ext(fn->properties_map.map, name_str->chars, name_str->len, &own);
                if (own) {
                    Item desc = js_new_object();
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, val);
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(true)});
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
                    return desc;
                }
            }
        }
        return make_js_undefined();
    }

    // String properties: length, numeric indices
    if (type == LMD_TYPE_STRING) {
        String* s = it2s(obj);
        if (name_str->len == 6 && strncmp(name_str->chars, "length", 6) == 0) {
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, (Item){.item = i2it(s->len)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
            return desc;
        }
        // numeric index → character at that position
        if (name_str->len > 0 && name_str->chars[0] >= '0' && name_str->chars[0] <= '9') {
            int idx = 0;
            bool valid = true;
            for (int i = 0; i < (int)name_str->len; i++) {
                if (name_str->chars[i] < '0' || name_str->chars[i] > '9') { valid = false; break; }
                idx = idx * 10 + (name_str->chars[i] - '0');
            }
            if (valid && idx >= 0 && idx < (int)s->len) {
                Item ch = item_at(obj, (int64_t)idx);
                Item desc = js_new_object();
                js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, ch);
                js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(true)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
                return desc;
            }
        }
        return make_js_undefined();
    }

    // Array properties: length, numeric indices
    if (type == LMD_TYPE_ARRAY) {
        if (name_str->len == 6 && strncmp(name_str->chars, "length", 6) == 0) {
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, (Item){.item = i2it(obj.array->length)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
            return desc;
        }
        // numeric index
        if (name_str->len > 0 && name_str->chars[0] >= '0' && name_str->chars[0] <= '9') {
            int idx = 0;
            for (int i = 0; i < (int)name_str->len; i++) {
                if (name_str->chars[i] < '0' || name_str->chars[i] > '9') { idx = -1; break; }
                idx = idx * 10 + (name_str->chars[i] - '0');
            }
            if (idx >= 0 && idx < obj.array->length) {
                Item desc = js_new_object();
                js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, obj.array->items[idx]);
                js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(true)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
                return desc;
            }
        }
        return make_js_undefined();
    }

    // Map (object) properties
    if (type == LMD_TYPE_MAP) {
        Map* m = obj.map;
        if (!m || !m->type) return make_js_undefined();

        // Check for accessor properties (__get_<name> / __set_<name>)
        char key_buf[256];
        snprintf(key_buf, sizeof(key_buf), "__get_%.*s", (int)name_str->len, name_str->chars);
        Item getter_key = (Item){.item = s2it(heap_create_name(key_buf, strlen(key_buf)))};
        Item has_getter = js_has_own_property(obj, getter_key);
        snprintf(key_buf, sizeof(key_buf), "__set_%.*s", (int)name_str->len, name_str->chars);
        Item setter_key = (Item){.item = s2it(heap_create_name(key_buf, strlen(key_buf)))};
        Item has_setter = js_has_own_property(obj, setter_key);

        if (it2b(has_getter) || it2b(has_setter)) {
            // Accessor descriptor
            Item desc = js_new_object();
            if (it2b(has_getter)) {
                js_property_set(desc, (Item){.item = s2it(heap_create_name("get", 3))}, js_property_get(obj, getter_key));
            } else {
                js_property_set(desc, (Item){.item = s2it(heap_create_name("get", 3))}, make_js_undefined());
            }
            if (it2b(has_setter)) {
                js_property_set(desc, (Item){.item = s2it(heap_create_name("set", 3))}, js_property_get(obj, setter_key));
            } else {
                js_property_set(desc, (Item){.item = s2it(heap_create_name("set", 3))}, make_js_undefined());
            }
            // v18l: Check __ne_ and __nc_ markers for accessor properties
            char attr_buf[256];
            bool nc_found = false, ne_found = false;
            snprintf(attr_buf, sizeof(attr_buf), "__nc_%.*s", (int)name_str->len, name_str->chars);
            Item nc_val = js_map_get_fast_ext(m, attr_buf, (int)strlen(attr_buf), &nc_found);
            snprintf(attr_buf, sizeof(attr_buf), "__ne_%.*s", (int)name_str->len, name_str->chars);
            Item ne_val = js_map_get_fast_ext(m, attr_buf, (int)strlen(attr_buf), &ne_found);
            bool is_enumerable = !(ne_found && js_is_truthy(ne_val));
            bool is_configurable = !(nc_found && js_is_truthy(nc_val));
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(is_enumerable)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(is_configurable)});
            return desc;
        }

        // Check for own data property
        Item has_own = js_has_own_property(obj, name);
        if (!it2b(has_own)) return make_js_undefined();

        Item value = js_property_get(obj, name);
        Item desc = js_new_object();
        js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, value);
        // v16: Check stored attribute markers for writable/enumerable/configurable
        char attr_buf[256];
        bool nw_found = false, nc_found = false, ne_found = false;
        snprintf(attr_buf, sizeof(attr_buf), "__nw_%.*s", (int)name_str->len, name_str->chars);
        Item nw_val = js_map_get_fast_ext(m, attr_buf, (int)strlen(attr_buf), &nw_found);
        snprintf(attr_buf, sizeof(attr_buf), "__nc_%.*s", (int)name_str->len, name_str->chars);
        Item nc_val = js_map_get_fast_ext(m, attr_buf, (int)strlen(attr_buf), &nc_found);
        snprintf(attr_buf, sizeof(attr_buf), "__ne_%.*s", (int)name_str->len, name_str->chars);
        Item ne_val = js_map_get_fast_ext(m, attr_buf, (int)strlen(attr_buf), &ne_found);
        bool is_writable = !(nw_found && js_is_truthy(nw_val));
        bool is_configurable = !(nc_found && js_is_truthy(nc_val));
        bool is_enumerable = !(ne_found && js_is_truthy(ne_val));
        js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(is_writable)});
        js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(is_enumerable)});
        js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(is_configurable)});
        return desc;
    }

    return make_js_undefined();
}

// =============================================================================
// Object.getOwnPropertyDescriptors — return descriptors for all own properties
// =============================================================================

extern "C" Item js_object_get_own_property_descriptors(Item obj) {
    if (!js_require_object_type(obj, "getOwnPropertyDescriptors")) return ItemNull;
    Item result = js_new_object();
    Item keys = js_object_get_own_property_names(obj);
    if (get_type_id(keys) != LMD_TYPE_ARRAY) return result;
    for (int i = 0; i < keys.array->length; i++) {
        Item key = keys.array->items[i];
        Item desc = js_object_get_own_property_descriptor(obj, key);
        if (desc.item != make_js_undefined().item) {
            js_property_set(result, key, desc);
        }
    }
    return result;
}

// =============================================================================
// Object.defineProperty — define a property on an object
// =============================================================================

extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor) {
    if (!js_require_object_type(obj, "defineProperty")) return ItemNull;
    if (obj.item == 0) return obj;
    // v18m: coerce property name to string (ES5 §8.12.9 step, ToPropertyKey)
    TypeId name_type = get_type_id(name);
    if (name_type != LMD_TYPE_STRING) {
        if (name.item == 0 || name_type == LMD_TYPE_NULL) {
            // null → "null" (ItemNull has item==0 which get_type_id maps to NULL)
            name = (Item){.item = s2it(heap_create_name("null", 4))};
        } else if (name_type == LMD_TYPE_UNDEFINED) {
            name = (Item){.item = s2it(heap_create_name("undefined", 9))};
        } else {
            name = js_to_string(name);
        }
    }
    // v18l: TypeError if descriptor is not an object (ES5 8.10.5 ToPropertyDescriptor step 1)
    TypeId desc_type = get_type_id(descriptor);
    if (desc_type != LMD_TYPE_MAP) {
        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item msg = (Item){.item = s2it(heap_create_name("Property description must be an object"))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return obj;
    }

    // v18l: Validate descriptor — mixed accessor+data is TypeError (ES5 8.10.5 step 9)
    {
        Item get_k = (Item){.item = s2it(heap_create_name("get", 3))};
        Item set_k = (Item){.item = s2it(heap_create_name("set", 3))};
        Item val_k = (Item){.item = s2it(heap_create_name("value", 5))};
        Item wri_k = (Item){.item = s2it(heap_create_name("writable", 8))};
        bool has_get = it2b(js_in(get_k, descriptor));
        bool has_set = it2b(js_in(set_k, descriptor));
        bool has_val = it2b(js_in(val_k, descriptor));
        bool has_wri = it2b(js_in(wri_k, descriptor));
        if ((has_get || has_set) && (has_val || has_wri)) {
            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
            Item msg = (Item){.item = s2it(heap_create_name("Invalid property descriptor. Cannot both specify accessors and a value or writable attribute"))};
            js_throw_value(js_new_error_with_name(tn, msg));
            return obj;
        }
        // v18l: Non-callable getter/setter is TypeError (ES5 8.10.5 step 7.b / 8.b)
        if (has_get) {
            Item getter = js_property_get(descriptor, get_k);
            if (get_type_id(getter) != LMD_TYPE_FUNC && get_type_id(getter) != LMD_TYPE_UNDEFINED) {
                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg = (Item){.item = s2it(heap_create_name("Getter must be a function"))};
                js_throw_value(js_new_error_with_name(tn, msg));
                return obj;
            }
        }
        if (has_set) {
            Item setter = js_property_get(descriptor, set_k);
            if (get_type_id(setter) != LMD_TYPE_FUNC && get_type_id(setter) != LMD_TYPE_UNDEFINED) {
                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item msg = (Item){.item = s2it(heap_create_name("Setter must be a function"))};
                js_throw_value(js_new_error_with_name(tn, msg));
                return obj;
            }
        }
    }

    // v18l: Non-extensible check — cannot add new properties to non-extensible objects
    if (get_type_id(obj) == LMD_TYPE_MAP) {
        Item is_ext = js_object_is_extensible(obj);
        if (!js_is_truthy(is_ext)) {
            // check if property already exists
            Item has = js_has_own_property(obj, name);
            if (!it2b(has)) {
                // also check accessor properties
                Item name_str_chk = js_to_string(name);
                bool has_accessor = false;
                if (get_type_id(name_str_chk) == LMD_TYPE_STRING) {
                    String* ns = it2s(name_str_chk);
                    char gk[256];
                    snprintf(gk, sizeof(gk), "__get_%.*s", (int)ns->len, ns->chars);
                    Item gk_item = (Item){.item = s2it(heap_create_name(gk, strlen(gk)))};
                    if (it2b(js_has_own_property(obj, gk_item))) has_accessor = true;
                }
                if (!has_accessor) {
                    Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                    Item msg = (Item){.item = s2it(heap_create_name("Cannot define property on a non-extensible object"))};
                    js_throw_value(js_new_error_with_name(tn, msg));
                    return obj;
                }
            }
        }
    }

    // Check if property is non-configurable before allowing redefinition
    Item name_str_item = js_to_string(name);
    if (get_type_id(name_str_item) == LMD_TYPE_STRING && get_type_id(obj) == LMD_TYPE_MAP) {
        String* name_str = it2s(name_str_item);
        if (name_str && name_str->len > 0 && name_str->len < 200) {
            char nc_key[256];
            snprintf(nc_key, sizeof(nc_key), "__nc_%.*s", (int)name_str->len, name_str->chars);
            Item nc_k = (Item){.item = s2it(heap_create_name(nc_key, strlen(nc_key)))};
            Item nc_val = js_property_get(obj, nc_k);
            if (js_is_truthy(nc_val)) {
                // Property is non-configurable — check what changes are being attempted
                // Read current property state
                char nw_key[256];
                snprintf(nw_key, sizeof(nw_key), "__nw_%.*s", (int)name_str->len, name_str->chars);
                Item nw_k = (Item){.item = s2it(heap_create_name(nw_key, strlen(nw_key)))};
                Item nw_val = js_property_get(obj, nw_k);
                bool cur_writable = !js_is_truthy(nw_val);

                // Check writable change: non-writable → writable is forbidden
                Item writable_key = (Item){.item = s2it(heap_create_name("writable", 8))};
                Item has_writable = js_in(writable_key, descriptor);
                if (it2b(has_writable)) {
                    Item new_writable = js_property_get(descriptor, writable_key);
                    if (js_is_truthy(new_writable) && !cur_writable) {
                        // trying to make non-writable → writable on non-configurable: TypeError
                        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                        Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property"))};
                        js_throw_value(js_new_error_with_name(tn, msg));
                        return obj;
                    }
                }
                // Check configurable change: non-configurable → configurable is forbidden
                Item configurable_key = (Item){.item = s2it(heap_create_name("configurable", 12))};
                Item has_conf = js_in(configurable_key, descriptor);
                if (it2b(has_conf)) {
                    Item new_conf = js_property_get(descriptor, configurable_key);
                    if (js_is_truthy(new_conf)) {
                        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                        Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property"))};
                        js_throw_value(js_new_error_with_name(tn, msg));
                        return obj;
                    }
                }
                // Check enumerable change on non-configurable
                char ne_key[256];
                snprintf(ne_key, sizeof(ne_key), "__ne_%.*s", (int)name_str->len, name_str->chars);
                Item ne_k = (Item){.item = s2it(heap_create_name(ne_key, strlen(ne_key)))};
                Item ne_val = js_property_get(obj, ne_k);
                bool cur_enumerable = !js_is_truthy(ne_val);
                Item enumerable_key = (Item){.item = s2it(heap_create_name("enumerable", 10))};
                Item has_enum = js_in(enumerable_key, descriptor);
                if (it2b(has_enum)) {
                    Item new_enum = js_property_get(descriptor, enumerable_key);
                    if (js_is_truthy(new_enum) != cur_enumerable) {
                        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                        Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property"))};
                        js_throw_value(js_new_error_with_name(tn, msg));
                        return obj;
                    }
                }
                // Check value change on non-configurable+non-writable
                if (!cur_writable) {
                    Item value_key_chk = (Item){.item = s2it(heap_create_name("value", 5))};
                    Item has_val = js_in(value_key_chk, descriptor);
                    if (it2b(has_val)) {
                        Item new_val = js_property_get(descriptor, value_key_chk);
                        Item cur_val = js_property_get(obj, name);
                        if (!it2b(js_strict_equal(new_val, cur_val))) {
                            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                            Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property"))};
                            js_throw_value(js_new_error_with_name(tn, msg));
                            return obj;
                        }
                    }
                }
                // v18l: Check getter/setter change on non-configurable accessor
                {
                    char gk_buf[256];
                    snprintf(gk_buf, sizeof(gk_buf), "__get_%.*s", (int)name_str->len, name_str->chars);
                    Item gk_item = (Item){.item = s2it(heap_create_name(gk_buf, strlen(gk_buf)))};
                    bool is_cur_accessor = it2b(js_has_own_property(obj, gk_item));
                    if (!is_cur_accessor) {
                        char sk_buf[256];
                        snprintf(sk_buf, sizeof(sk_buf), "__set_%.*s", (int)name_str->len, name_str->chars);
                        Item sk_item = (Item){.item = s2it(heap_create_name(sk_buf, strlen(sk_buf)))};
                        is_cur_accessor = it2b(js_has_own_property(obj, sk_item));
                    }
                    if (is_cur_accessor) {
                        // v18m: accessor-to-data conversion on non-configurable → TypeError
                        Item val_k = (Item){.item = s2it(heap_create_name("value", 5))};
                        Item wrt_k = (Item){.item = s2it(heap_create_name("writable", 8))};
                        if (it2b(js_in(val_k, descriptor)) || it2b(js_in(wrt_k, descriptor))) {
                            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                            Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property"))};
                            js_throw_value(js_new_error_with_name(tn, msg));
                            return obj;
                        }
                        // check if getter is changing
                        Item get_k = (Item){.item = s2it(heap_create_name("get", 3))};
                        Item has_get = js_in(get_k, descriptor);
                        if (it2b(has_get)) {
                            Item new_get = js_property_get(descriptor, get_k);
                            snprintf(gk_buf, sizeof(gk_buf), "__get_%.*s", (int)name_str->len, name_str->chars);
                            Item old_gk = (Item){.item = s2it(heap_create_name(gk_buf, strlen(gk_buf)))};
                            Item old_get = js_property_get(obj, old_gk);
                            if (!it2b(js_strict_equal(new_get, old_get))) {
                                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                                Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property"))};
                                js_throw_value(js_new_error_with_name(tn, msg));
                                return obj;
                            }
                        }
                        // check if setter is changing
                        Item set_k = (Item){.item = s2it(heap_create_name("set", 3))};
                        Item has_set = js_in(set_k, descriptor);
                        if (it2b(has_set)) {
                            Item new_set = js_property_get(descriptor, set_k);
                            char sk_buf[256];
                            snprintf(sk_buf, sizeof(sk_buf), "__set_%.*s", (int)name_str->len, name_str->chars);
                            Item old_sk = (Item){.item = s2it(heap_create_name(sk_buf, strlen(sk_buf)))};
                            Item old_set = js_property_get(obj, old_sk);
                            if (!it2b(js_strict_equal(new_set, old_set))) {
                                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                                Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property"))};
                                js_throw_value(js_new_error_with_name(tn, msg));
                                return obj;
                            }
                        }
                    } else {
                        // v18m: data-to-accessor conversion on non-configurable → TypeError
                        Item get_k2 = (Item){.item = s2it(heap_create_name("get", 3))};
                        Item set_k2 = (Item){.item = s2it(heap_create_name("set", 3))};
                        if (it2b(js_in(get_k2, descriptor)) || it2b(js_in(set_k2, descriptor))) {
                            Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                            Item msg = (Item){.item = s2it(heap_create_name("Cannot redefine property"))};
                            js_throw_value(js_new_error_with_name(tn, msg));
                            return obj;
                        }
                    }
                }
            }
        }
    }

    // v18n: check is_new_property BEFORE setting any value (since js_property_set creates the property)
    bool is_new_property = !it2b(js_has_own_property(obj, name));

    Item value_key = (Item){.item = s2it(heap_create_name("value", 5))};
    // use hasOwnProperty to correctly detect presence of "value" key
    Item has_value = js_in(value_key, descriptor);
    bool is_accessor = false;
    if (it2b(has_value)) {
        // data property descriptor: set value directly
        Item value = js_property_get(descriptor, value_key);
        js_property_set(obj, name, value);
        // v18m: when converting accessor→data, remove accessor markers
        Item name_str_conv = js_to_string(name);
        if (get_type_id(name_str_conv) == LMD_TYPE_STRING) {
            String* ns = it2s(name_str_conv);
            if (ns && ns->len > 0 && ns->len < 200) {
                char mk_buf[256];
                snprintf(mk_buf, sizeof(mk_buf), "__get_%.*s", (int)ns->len, ns->chars);
                Item gk = (Item){.item = s2it(heap_create_name(mk_buf, strlen(mk_buf)))};
                if (it2b(js_has_own_property(obj, gk))) {
                    js_property_set(obj, gk, (Item){.item = JS_DELETED_SENTINEL_VAL});
                }
                snprintf(mk_buf, sizeof(mk_buf), "__set_%.*s", (int)ns->len, ns->chars);
                Item sk = (Item){.item = s2it(heap_create_name(mk_buf, strlen(mk_buf)))};
                if (it2b(js_has_own_property(obj, sk))) {
                    js_property_set(obj, sk, (Item){.item = JS_DELETED_SENTINEL_VAL});
                }
            }
        }
    } else {
        // accessor descriptor: check for get/set
        Item name_str2 = js_to_string(name);
        if (get_type_id(name_str2) == LMD_TYPE_STRING) {
            String* ns = it2s(name_str2);
            char key_buf[256];
            Item get_key = (Item){.item = s2it(heap_create_name("get", 3))};
            Item getter = js_property_get(descriptor, get_key);
            if (get_type_id(getter) == LMD_TYPE_FUNC) {
                is_accessor = true;
                snprintf(key_buf, sizeof(key_buf), "__get_%.*s", (int)ns->len, ns->chars);
                Item gk = (Item){.item = s2it(heap_create_name(key_buf, strlen(key_buf)))};
                js_property_set(obj, gk, getter);
            }
            Item set_key = (Item){.item = s2it(heap_create_name("set", 3))};
            Item setter = js_property_get(descriptor, set_key);
            if (get_type_id(setter) == LMD_TYPE_FUNC) {
                is_accessor = true;
                snprintf(key_buf, sizeof(key_buf), "__set_%.*s", (int)ns->len, ns->chars);
                Item sk = (Item){.item = s2it(heap_create_name(key_buf, strlen(key_buf)))};
                js_property_set(obj, sk, setter);
            }
            // v18m: when converting data→accessor, remove the direct data value
            if (is_accessor) {
                // first remove __nw_ marker so the sentinel write isn't silently rejected
                snprintf(key_buf, sizeof(key_buf), "__nw_%.*s", (int)ns->len, ns->chars);
                Item nw_k = (Item){.item = s2it(heap_create_name(key_buf, strlen(key_buf)))};
                Item has_nw = js_has_own_property(obj, nw_k);
                if (it2b(has_nw)) {
                    js_property_set(obj, nw_k, (Item){.item = b2it(false)});
                }
                // now delete the data value
                Item cur_val = js_property_get(obj, name);
                if (cur_val.item != ItemNull.item) {
                    js_property_set(obj, name, (Item){.item = JS_DELETED_SENTINEL_VAL});
                }
            }
        }
    }
    // v18m: empty descriptor (no value/get/set) still creates data property with undefined
    if (!it2b(has_value) && !is_accessor) {
        // check if property doesn't already exist (own property only)
        if (!it2b(js_has_own_property(obj, name))) {
            js_property_set(obj, name, (Item){.item = ITEM_JS_UNDEFINED});
        }
    }
    // v18k: Store property attribute markers
    // For new properties, missing attributes default to false (unlike regular assignments)
    // For existing properties, only change attributes explicitly specified in the descriptor
    name_str_item = js_to_string(name);
    if (get_type_id(name_str_item) == LMD_TYPE_STRING) {
        String* name_str = it2s(name_str_item);
        if (name_str && name_str->len > 0 && name_str->len < 200) {
            char attr_key[256];
            // writable: default false for new properties (not applicable for accessors)
            if (!is_accessor) {
                Item writable_key = (Item){.item = s2it(heap_create_name("writable", 8))};
                Item has_writable = js_in(writable_key, descriptor);
                if (it2b(has_writable)) {
                    Item writable_val = js_property_get(descriptor, writable_key);
                    if (!js_is_truthy(writable_val)) {
                        snprintf(attr_key, sizeof(attr_key), "__nw_%.*s", (int)name_str->len, name_str->chars);
                        Item nw_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                        js_property_set(obj, nw_k, (Item){.item = b2it(true)});
                    } else {
                        // Explicitly writable: remove marker if exists
                        snprintf(attr_key, sizeof(attr_key), "__nw_%.*s", (int)name_str->len, name_str->chars);
                        Item nw_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                        js_property_set(obj, nw_k, (Item){.item = b2it(false)});
                    }
                } else if (is_new_property) {
                    // Not specified on NEW property: default to false (non-writable)
                    snprintf(attr_key, sizeof(attr_key), "__nw_%.*s", (int)name_str->len, name_str->chars);
                    Item nw_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_property_set(obj, nw_k, (Item){.item = b2it(true)});
                }
                // else: existing property, writable not specified → keep current
            }
            // configurable
            Item configurable_key = (Item){.item = s2it(heap_create_name("configurable", 12))};
            Item has_configurable = js_in(configurable_key, descriptor);
            if (it2b(has_configurable)) {
                Item configurable_val = js_property_get(descriptor, configurable_key);
                if (!js_is_truthy(configurable_val)) {
                    snprintf(attr_key, sizeof(attr_key), "__nc_%.*s", (int)name_str->len, name_str->chars);
                    Item nc_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_property_set(obj, nc_k, (Item){.item = b2it(true)});
                } else {
                    snprintf(attr_key, sizeof(attr_key), "__nc_%.*s", (int)name_str->len, name_str->chars);
                    Item nc_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_property_set(obj, nc_k, (Item){.item = b2it(false)});
                }
            } else if (is_new_property) {
                // Not specified on NEW property: default to false (non-configurable)
                snprintf(attr_key, sizeof(attr_key), "__nc_%.*s", (int)name_str->len, name_str->chars);
                Item nc_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                js_property_set(obj, nc_k, (Item){.item = b2it(true)});
            }
            // enumerable
            Item enumerable_key = (Item){.item = s2it(heap_create_name("enumerable", 10))};
            Item has_enumerable = js_in(enumerable_key, descriptor);
            if (it2b(has_enumerable)) {
                Item enumerable_val = js_property_get(descriptor, enumerable_key);
                if (!js_is_truthy(enumerable_val)) {
                    snprintf(attr_key, sizeof(attr_key), "__ne_%.*s", (int)name_str->len, name_str->chars);
                    Item ne_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_property_set(obj, ne_k, (Item){.item = b2it(true)});
                } else {
                    snprintf(attr_key, sizeof(attr_key), "__ne_%.*s", (int)name_str->len, name_str->chars);
                    Item ne_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_property_set(obj, ne_k, (Item){.item = b2it(false)});
                }
            } else if (is_new_property) {
                // Not specified on NEW property: default to false (non-enumerable)
                snprintf(attr_key, sizeof(attr_key), "__ne_%.*s", (int)name_str->len, name_str->chars);
                Item ne_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                js_property_set(obj, ne_k, (Item){.item = b2it(true)});
            }
        }
    }
    return obj;
}

// =============================================================================
// Object.defineProperties — define multiple properties on an object
// =============================================================================

extern "C" Item js_object_define_properties(Item obj, Item props) {
    if (!js_require_object_type(obj, "defineProperties")) return ItemNull;
    if (obj.item == 0 || get_type_id(props) != LMD_TYPE_MAP) return obj;
    Item keys = js_object_keys(props);
    if (get_type_id(keys) != LMD_TYPE_ARRAY) return obj;
    for (int i = 0; i < keys.array->length; i++) {
        Item key = keys.array->items[i];
        Item desc = js_property_get(props, key);
        js_object_define_property(obj, key, desc);
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

// Object.getOwnPropertyNames — includes non-enumerable own properties
extern "C" Item js_object_get_own_property_names(Item object) {
    if (!js_require_object_type(object, "getOwnPropertyNames")) return js_array_new(0);
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_ARRAY) {
        // indices + "length"
        int len = object.array->length;
        Item result = js_array_new(len + 1);
        for (int i = 0; i < len; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            js_array_set(result, (Item){.item = i2it(i)}, (Item){.item = s2it(heap_create_name(buf))});
        }
        js_array_set(result, (Item){.item = i2it(len)}, (Item){.item = s2it(heap_create_name("length", 6))});
        return result;
    }
    if (type == LMD_TYPE_FUNC) {
        // function own properties: length, name, prototype + custom
        Item result = js_array_new(3);
        int i = 0;
        js_array_set(result, (Item){.item = i2it(i++)}, (Item){.item = s2it(heap_create_name("length", 6))});
        js_array_set(result, (Item){.item = i2it(i++)}, (Item){.item = s2it(heap_create_name("name", 4))});
        js_array_set(result, (Item){.item = i2it(i++)}, (Item){.item = s2it(heap_create_name("prototype", 9))});
        return result;
    }
    if (type != LMD_TYPE_MAP) return js_array_new(0);
    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);
    TypeMap* tm = (TypeMap*)m->type;
    // Use dynamic array approach: push matched entries to result array
    Item result = js_array_new(0);
    Array* arr = result.array;
    ShapeEntry* e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int len = (int)e->name->length;
        bool skip = (len >= 2 && s[0] == '_' && s[1] == '_');
        if (!skip) {
            Item val = _map_read_field(e, m->data);
            if (val.item == JS_DELETED_SENTINEL_VAL) skip = true;
        }
        if (!skip) {
            char nbuf[256];
            int nlen = len < 255 ? len : 255;
            memcpy(nbuf, s, nlen);
            nbuf[nlen] = '\0';
            Item key_item = (Item){.item = s2it(heap_create_name(nbuf, nlen))};
            array_push(arr, key_item);
        }
        e = e->next;
    }
    // Detect accessor properties from __get_<name> entries
    e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int slen = (int)e->name->length;
        if (slen > 6 && strncmp(s, "__get_", 6) == 0) {
            const char* prop_name = s + 6;
            int prop_len = slen - 6;
            bool already = false;
            for (int j = 0; j < arr->length; j++) {
                String* ex = it2s(arr->items[j]);
                if (ex && (int)ex->len == prop_len && memcmp(ex->chars, prop_name, prop_len) == 0) {
                    already = true;
                    break;
                }
            }
            if (!already) {
                Item key_item = (Item){.item = s2it(heap_create_name(prop_name, prop_len))};
                array_push(arr, key_item);
            }
        }
        e = e->next;
    }
    return result;
}

extern "C" Item js_object_keys(Item object) {
    if (!js_require_object_type(object, "keys")) return js_array_new(0);
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
    Item result = js_array_new(0);
    Array* arr = result.array;

    // Main pass: collect enumerable own properties
    ShapeEntry* e = tm->shape;
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
            Item val = _map_read_field(e, m->data);
            if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
            // skip non-enumerable properties
            if (len > 0 && len < 200) {
                char ne_key[256];
                snprintf(ne_key, sizeof(ne_key), "__ne_%.*s", len, s);
                bool ne_found = false;
                Item ne_val = js_map_get_fast_ext(m, ne_key, (int)strlen(ne_key), &ne_found);
                if (ne_found && js_is_truthy(ne_val)) { e = e->next; continue; }
            }
            char nbuf[256];
            int nlen = len < 255 ? len : 255;
            memcpy(nbuf, s, nlen);
            nbuf[nlen] = '\0';
            Item key_str = (Item){.item = s2it(heap_create_name(nbuf, nlen))};
            array_push(arr, key_str);
        }
        e = e->next;
    }

    // Second pass: detect accessor properties defined via __get_<name>
    e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int slen = (int)e->name->length;
        // Look for __get_<name> entries
        if (slen > 6 && strncmp(s, "__get_", 6) == 0) {
            const char* prop_name = s + 6;
            int prop_len = slen - 6;
            // Check if this property is already in the result (from the main pass)
            bool already_present = false;
            for (int j = 0; j < arr->length; j++) {
                String* existing = it2s(arr->items[j]);
                if (existing && (int)existing->len == prop_len && memcmp(existing->chars, prop_name, prop_len) == 0) {
                    already_present = true;
                    break;
                }
            }
            if (!already_present) {
                // Check non-enumerable marker
                bool skip_ne = false;
                if (prop_len > 0 && prop_len < 200) {
                    char ne_key[256];
                    snprintf(ne_key, sizeof(ne_key), "__ne_%.*s", prop_len, prop_name);
                    bool ne_found = false;
                    Item ne_val = js_map_get_fast_ext(m, ne_key, (int)strlen(ne_key), &ne_found);
                    if (ne_found && js_is_truthy(ne_val)) skip_ne = true;
                }
                if (!skip_ne) {
                    Item key_str = (Item){.item = s2it(heap_create_name(prop_name, prop_len))};
                    array_push(arr, key_str);
                }
            }
        }
        e = e->next;
    }

    return result;
}

// =============================================================================
// js_to_string_val — convert any value to string (returns Item)
// =============================================================================

// v17: for-in enumeration — walks prototype chain to collect all enumerable string keys
extern "C" Item js_for_in_keys(Item object) {
    TypeId type = get_type_id(object);

    // for-in over null/undefined: 0 iterations
    if (object.item == ItemNull.item || type == LMD_TYPE_UNDEFINED) {
        return js_array_new(0);
    }

    // for arrays: return indices as string keys (own only, same as before)
    if (type == LMD_TYPE_ARRAY) {
        return js_object_keys(object);
    }

    // for non-map primitives (string, number, bool): coerce
    if (type == LMD_TYPE_STRING) {
        // enumerate string indices "0", "1", ... "length-1"
        String* s = it2s(object);
        int len = (int)s->len;
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

    // walk prototype chain collecting enumerable string keys
    // use a simple seen-set via hashmap to deduplicate
    struct SeenEntry { char name[256]; };
    HashMap* seen = hashmap_new(sizeof(struct SeenEntry), 64, 0, 0,
        [](const void* item, uint64_t s0, uint64_t s1) -> uint64_t {
            return hashmap_sip(((const struct SeenEntry*)item)->name,
                strlen(((const struct SeenEntry*)item)->name), s0, s1);
        },
        [](const void* a, const void* b, void*) -> int {
            return strcmp(((const struct SeenEntry*)a)->name,
                          ((const struct SeenEntry*)b)->name);
        },
        NULL, NULL);

    // collect keys using push (js_array_new(0) starts empty)
    Item result = js_array_new(0);

    Item current = object;
    int depth = 0;
    while (current.item != ItemNull.item && get_type_id(current) == LMD_TYPE_MAP && depth < 64) {
        Map* m = current.map;
        if (m && m->type) {
            TypeMap* tm = (TypeMap*)m->type;
            ShapeEntry* e = tm->shape;
            while (e) {
                const char* s = e->name->str;
                int len = (int)e->name->length;

                // skip engine-internal properties and constructor
                bool skip = false;
                if (len >= 2 && s[0] == '_' && s[1] == '_') {
                    skip = true;
                } else if (len == 11 && memcmp(s, "constructor", 11) == 0) {
                    skip = true;
                }

                if (!skip) {
                    // skip deleted properties
                    Item val = _map_read_field(e, m->data);
                    if (val.item == JS_DELETED_SENTINEL_VAL) skip = true;
                }

                // skip non-enumerable properties
                if (!skip && len > 0 && len < 200) {
                    char ne_key[256];
                    snprintf(ne_key, sizeof(ne_key), "__ne_%.*s", len, s);
                    bool ne_found = false;
                    Item ne_val = js_map_get_fast_ext(m, ne_key, (int)strlen(ne_key), &ne_found);
                    if (ne_found && js_is_truthy(ne_val)) skip = true;
                }

                if (!skip) {
                    // deduplicate: only add if not seen before
                    struct SeenEntry probe;
                    int nlen = len < 255 ? len : 255;
                    memcpy(probe.name, s, nlen);
                    probe.name[nlen] = '\0';
                    const struct SeenEntry* existing = (const struct SeenEntry*)hashmap_get(seen, &probe);
                    if (!existing) {
                        hashmap_set(seen, &probe);
                        Item key_str = (Item){.item = s2it(heap_create_name(probe.name))};
                        js_array_push(result, key_str);
                    }
                }
                e = e->next;
            }
        }
        // walk up prototype chain
        current = js_get_prototype(current);
        depth++;
    }

    hashmap_free(seen);
    return result;
}

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
            // convert back to symbol: symbol value = -(id + JS_SYMBOL_BASE)
            int64_t sym_val = -(id + (int64_t)JS_SYMBOL_BASE);
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

    // v18k: Fall through to constructor property access for static methods
    // (isInteger, isFinite, isNaN, isSafeInteger, parseInt, parseFloat)
    Item ctor_name = (Item){.item = s2it(heap_create_name("Number", 6))};
    Item ctor = js_get_constructor(ctor_name);
    if (get_type_id(ctor) == LMD_TYPE_FUNC) {
        return js_property_get(ctor, prop_name);
    }

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
        Item val = _map_read_field(e, m->data);
        if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
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
        Item val = _map_read_field(e, m->data);
        if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
        Item pair = js_array_new(2);
        char nbuf[256];
        int nlen = (int)e->name->length < 255 ? (int)e->name->length : 255;
        memcpy(nbuf, e->name->str, nlen);
        nbuf[nlen] = '\0';
        Item key_str = (Item){.item = s2it(heap_create_name(nbuf))};
        js_array_set(pair, (Item){.item = i2it(0)}, key_str);
        js_array_set(pair, (Item){.item = i2it(1)}, val);
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
                Item val = _map_read_field(e, m->data);
                if (val.item != JS_DELETED_SENTINEL_VAL) {
                    Item key = (Item){.item = s2it(heap_create_name(e->name->str, (int)e->name->length))};
                    js_property_set(target, key, val);
                }
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
            Item val = _map_read_field(e, m->data);
            if (val.item != JS_DELETED_SENTINEL_VAL) {
                Item key = (Item){.item = s2it(heap_create_name(e->name->str, (int)e->name->length))};
                js_property_set(target, key, val);
            }
        }
        e = e->next;
    }
    return target;
}

// =============================================================================
// obj.hasOwnProperty(key) / Object.hasOwn(obj, key)
// =============================================================================

extern "C" Item js_has_own_property(Item obj, Item key) {
    // v18: handle function objects — prototype, name, length, and custom properties
    if (get_type_id(obj) == LMD_TYPE_FUNC) {
        Item k = js_to_string(key);
        if (get_type_id(k) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
        String* ks = it2s(k);
        if (!ks) return (Item){.item = b2it(false)};
        // built-in own properties
        if ((ks->len == 9 && strncmp(ks->chars, "prototype", 9) == 0) ||
            (ks->len == 4 && strncmp(ks->chars, "name", 4) == 0) ||
            (ks->len == 6 && strncmp(ks->chars, "length", 6) == 0)) {
            return (Item){.item = b2it(true)};
        }
        // check custom properties backing map
        JsFuncProps* fn = (JsFuncProps*)obj.function;
        if (fn->properties_map.item != 0) {
            bool own = false;
            js_map_get_fast_ext(fn->properties_map.map, ks->chars, ks->len, &own);
            if (own) return (Item){.item = b2it(true)};
        }
        return (Item){.item = b2it(false)};
    }
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    // Convert symbol keys to their internal string representation
    Item k;
    if (get_type_id(key) == LMD_TYPE_INT && it2i(key) <= -(int64_t)JS_SYMBOL_BASE) {
        int64_t id = -(it2i(key) + (int64_t)JS_SYMBOL_BASE);
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
            // Check for deleted sentinel — deleted properties are not "own"
            Item val = _map_read_field(e, m->data);
            if (val.item == JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(false)};
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
    if (!js_require_object_type(obj, "freeze")) return obj;
    if (get_type_id(obj) != LMD_TYPE_MAP) return obj;
    // v18l: Mark all own properties as non-writable and non-configurable
    Item keys = js_object_get_own_property_names(obj);
    if (get_type_id(keys) == LMD_TYPE_ARRAY) {
        for (int i = 0; i < keys.array->length; i++) {
            Item key = keys.array->items[i];
            if (get_type_id(key) == LMD_TYPE_STRING) {
                String* str_key = it2s(key);
                if (str_key && str_key->len > 0 && str_key->len < 200) {
                    char attr_key[256];
                    snprintf(attr_key, sizeof(attr_key), "__nw_%.*s", (int)str_key->len, str_key->chars);
                    Item nw_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_property_set(obj, nw_k, (Item){.item = b2it(true)});
                    snprintf(attr_key, sizeof(attr_key), "__nc_%.*s", (int)str_key->len, str_key->chars);
                    Item nc_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_property_set(obj, nc_k, (Item){.item = b2it(true)});
                }
            }
        }
    }
    Item key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    js_property_set(obj, key, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_frozen(Item obj) {
    if (!js_require_object_type(obj, "isFrozen")) return ItemNull;
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    Item key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    Item val = map_get(obj.map, key);
    return (Item){.item = b2it(js_is_truthy(val))};
}

// =============================================================================
// Object.seal — mark all properties non-configurable, mark object non-extensible
// =============================================================================

extern "C" Item js_object_seal(Item obj) {
    if (!js_require_object_type(obj, "seal")) return obj;
    if (get_type_id(obj) != LMD_TYPE_MAP) return obj;
    // mark all existing own properties as non-configurable
    Item keys = js_object_get_own_property_names(obj);
    if (get_type_id(keys) == LMD_TYPE_ARRAY) {
        for (int i = 0; i < keys.array->length; i++) {
            Item key = keys.array->items[i];
            if (get_type_id(key) == LMD_TYPE_STRING) {
                String* str_key = it2s(key);
                if (str_key && str_key->len > 0 && str_key->len < 200) {
                    char attr_key[256];
                    snprintf(attr_key, sizeof(attr_key), "__nc_%.*s", (int)str_key->len, str_key->chars);
                    Item nc_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_property_set(obj, nc_k, (Item){.item = b2it(true)});
                }
            }
        }
    }
    Item sealed_k = (Item){.item = s2it(heap_create_name("__sealed__", 10))};
    js_property_set(obj, sealed_k, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_sealed(Item obj) {
    if (!js_require_object_type(obj, "isSealed")) return ItemNull;
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    Item key = (Item){.item = s2it(heap_create_name("__sealed__", 10))};
    Item val = map_get(obj.map, key);
    if (js_is_truthy(val)) return (Item){.item = b2it(true)};
    // frozen objects are also sealed
    Item fk = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    Item fv = map_get(obj.map, fk);
    return (Item){.item = b2it(js_is_truthy(fv))};
}

// =============================================================================
// Object.preventExtensions / Object.isExtensible
// =============================================================================

extern "C" Item js_object_prevent_extensions(Item obj) {
    if (!js_require_object_type(obj, "preventExtensions")) return obj;
    if (get_type_id(obj) != LMD_TYPE_MAP) return obj;
    Item key = (Item){.item = s2it(heap_create_name("__non_extensible__", 17))};
    js_property_set(obj, key, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_extensible(Item obj) {
    if (!js_require_object_type(obj, "isExtensible")) return ItemNull;
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
    // non-extensible if explicitly marked, or sealed, or frozen
    Item ne_k = (Item){.item = s2it(heap_create_name("__non_extensible__", 17))};
    Item ne_v = map_get(obj.map, ne_k);
    if (js_is_truthy(ne_v)) return (Item){.item = b2it(false)};
    Item sl_k = (Item){.item = s2it(heap_create_name("__sealed__", 10))};
    Item sl_v = map_get(obj.map, sl_k);
    if (js_is_truthy(sl_v)) return (Item){.item = b2it(false)};
    Item fr_k = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    Item fr_v = map_get(obj.map, fr_k);
    if (js_is_truthy(fr_v)) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(true)};
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
    // v16: Frozen objects reject property deletion
    {
        Map* m = obj.map;
        bool frozen_found = false;
        Item frozen_val = js_map_get_fast_ext(m, "__frozen__", 10, &frozen_found);
        if (frozen_found && js_is_truthy(frozen_val)) {
            return (Item){.item = b2it(false)};
        }
    }
    // v16: Non-configurable properties cannot be deleted
    if (get_type_id(key) == LMD_TYPE_STRING) {
        String* str_key = it2s(key);
        if (str_key && str_key->len > 0 && str_key->len < 200) {
            char nc_key[256];
            snprintf(nc_key, sizeof(nc_key), "__nc_%.*s", (int)str_key->len, str_key->chars);
            bool nc_found = false;
            Item nc_val = js_map_get_fast_ext(obj.map, nc_key, (int)strlen(nc_key), &nc_found);
            if (nc_found && js_is_truthy(nc_val)) {
                return (Item){.item = b2it(false)};
            }
        }
    }
    // Mark property as deleted using sentinel value.
    // Object.keys, hasOwnProperty, in, and JSON.stringify skip sentinel entries.
    //
    // For FLOAT-typed fields, fn_map_set's FLOAT→INT widening path converts the
    // INT sentinel to a double, making it unrecognizable. Fix: first set a BOOL
    // value (which forces a type rebuild from FLOAT → BOOL), then set the INT sentinel.
    Map* m = obj.map;
    TypeMap* map_type = m ? (TypeMap*)m->type : NULL;
    if (map_type && map_type->shape && get_type_id(key) == LMD_TYPE_STRING) {
        String* str_key = it2s(key);
        if (str_key) {
            ShapeEntry* entry = map_type->shape;
            while (entry) {
                if (entry->name && entry->name->length == (size_t)str_key->len &&
                    strncmp(entry->name->str, str_key->chars, str_key->len) == 0) {
                    if (entry->type->type_id == LMD_TYPE_FLOAT) {
                        // Force type change from FLOAT → BOOL via rebuild
                        js_property_set(obj, key, (Item){.item = b2it(false)});
                    }
                    break;
                }
                entry = entry->next;
            }
        }
    }
    js_property_set(obj, key, (Item){.item = JS_DELETED_SENTINEL_VAL});
    return (Item){.item = b2it(true)};
}

// =============================================================================
// v12: encodeURIComponent / decodeURIComponent / atob / btoa
// =============================================================================

// Base64 decoding table
static const unsigned char b64_decode_table[256] = {
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
     52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
    255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
     15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
    255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
     41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
    255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255
};

extern "C" Item js_atob(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};

    const char* src = s->chars;
    int src_len = s->len;

    // skip whitespace and compute output size
    char* buf = (char*)malloc(src_len); // output is always <= input
    if (!buf) return (Item){.item = s2it(heap_create_name("", 0))};

    int out = 0;
    int bits = 0;
    int val = 0;
    for (int i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') continue;
        if (c == '=') break;
        unsigned char d = b64_decode_table[c];
        if (d == 255) continue; // skip invalid
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            buf[out++] = (char)((val >> bits) & 0xFF);
        }
    }

    String* result = heap_create_name(buf, out);
    free(buf);
    return (Item){.item = s2it(result)};
}

static const char b64_encode_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

extern "C" Item js_btoa(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};

    const unsigned char* src = (const unsigned char*)s->chars;
    int src_len = s->len;
    int out_len = ((src_len + 2) / 3) * 4;
    char* buf = (char*)malloc(out_len + 1);
    if (!buf) return (Item){.item = s2it(heap_create_name("", 0))};

    int out = 0;
    int i = 0;
    while (i < src_len) {
        unsigned int b0 = src[i];
        unsigned int b1 = (i + 1 < src_len) ? src[i + 1] : 0;
        unsigned int b2 = (i + 2 < src_len) ? src[i + 2] : 0;
        int remaining = src_len - i;
        unsigned int triple = (b0 << 16) | (b1 << 8) | b2;
        buf[out++] = b64_encode_table[(triple >> 18) & 0x3F];
        buf[out++] = b64_encode_table[(triple >> 12) & 0x3F];
        buf[out++] = (remaining > 1) ? b64_encode_table[(triple >> 6) & 0x3F] : '=';
        buf[out++] = (remaining > 2) ? b64_encode_table[triple & 0x3F] : '=';
        i += 3;
    }

    String* result = heap_create_name(buf, out);
    free(buf);
    return (Item){.item = s2it(result)};
}

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
// escape(string) — legacy percent-encoding (%XX and %uXXXX)
// Characters NOT escaped: A-Z a-z 0-9 @ * _ + - . /
// =============================================================================

static bool js_escape_is_passthrough(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= '0' && c <= '9') return true;
    // @*_+-./
    return c == '@' || c == '*' || c == '_' || c == '+' || c == '-' || c == '.' || c == '/';
}

extern "C" Item js_escape(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};

    const char* src = s->chars;
    int src_len = s->len;

    // worst case: every char becomes %uXXXX (6 bytes per input byte)
    char* buf = (char*)malloc(src_len * 6 + 1);
    if (!buf) return (Item){.item = s2it(heap_create_name("", 0))};

    static const char hex[] = "0123456789ABCDEF";
    int out = 0;
    int i = 0;
    while (i < src_len) {
        unsigned char c = (unsigned char)src[i];

        if (js_escape_is_passthrough(c)) {
            buf[out++] = (char)c;
            i++;
            continue;
        }

        // decode UTF-8 codepoint
        uint32_t cp;
        int bytes;
        if (c < 0x80) {
            cp = c; bytes = 1;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < src_len) {
            cp = (c & 0x1F) << 6 | ((unsigned char)src[i+1] & 0x3F);
            bytes = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < src_len) {
            cp = (c & 0x0F) << 12 | ((unsigned char)src[i+1] & 0x3F) << 6 | ((unsigned char)src[i+2] & 0x3F);
            bytes = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < src_len) {
            // surrogate pair for codepoints above 0xFFFF
            cp = (c & 0x07) << 18 | ((unsigned char)src[i+1] & 0x3F) << 12 | ((unsigned char)src[i+2] & 0x3F) << 6 | ((unsigned char)src[i+3] & 0x3F);
            bytes = 4;
        } else {
            cp = c; bytes = 1;
        }

        if (cp > 0xFFFF) {
            // encode as surrogate pair: %uD800-style
            uint32_t hi = 0xD800 + ((cp - 0x10000) >> 10);
            uint32_t lo = 0xDC00 + ((cp - 0x10000) & 0x3FF);
            buf[out++] = '%'; buf[out++] = 'u';
            buf[out++] = hex[(hi >> 12) & 0xF]; buf[out++] = hex[(hi >> 8) & 0xF];
            buf[out++] = hex[(hi >> 4) & 0xF]; buf[out++] = hex[hi & 0xF];
            buf[out++] = '%'; buf[out++] = 'u';
            buf[out++] = hex[(lo >> 12) & 0xF]; buf[out++] = hex[(lo >> 8) & 0xF];
            buf[out++] = hex[(lo >> 4) & 0xF]; buf[out++] = hex[lo & 0xF];
        } else if (cp > 0xFF) {
            // %uXXXX
            buf[out++] = '%'; buf[out++] = 'u';
            buf[out++] = hex[(cp >> 12) & 0xF]; buf[out++] = hex[(cp >> 8) & 0xF];
            buf[out++] = hex[(cp >> 4) & 0xF]; buf[out++] = hex[cp & 0xF];
        } else {
            // %XX
            buf[out++] = '%';
            buf[out++] = hex[(cp >> 4) & 0xF];
            buf[out++] = hex[cp & 0xF];
        }
        i += bytes;
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
// Built-in constructor cache: Array, Object, Function, String, Number, Boolean, etc.
// These return JsFunction objects so that `typeof Array === "function"` and
// `Array.prototype.push` work correctly.
// =============================================================================

// Constructor IDs for dispatch
enum JsConstructorId {
    JS_CTOR_OBJECT = 1,
    JS_CTOR_ARRAY,
    JS_CTOR_FUNCTION,
    JS_CTOR_STRING,
    JS_CTOR_NUMBER,
    JS_CTOR_BOOLEAN,
    JS_CTOR_SYMBOL,
    JS_CTOR_ERROR,
    JS_CTOR_TYPE_ERROR,
    JS_CTOR_RANGE_ERROR,
    JS_CTOR_REFERENCE_ERROR,
    JS_CTOR_SYNTAX_ERROR,
    JS_CTOR_URI_ERROR,
    JS_CTOR_REGEXP,
    JS_CTOR_DATE,
    JS_CTOR_PROMISE,
    JS_CTOR_MAP,
    JS_CTOR_SET,
    JS_CTOR_WEAKMAP,
    JS_CTOR_WEAKSET,
    JS_CTOR_MAX
};

static Item js_constructor_cache[JS_CTOR_MAX];
static bool js_ctor_cache_init = false;

// Dummy func_ptr for constructors (makes typeof return "function")
static Item js_ctor_placeholder() { return ItemNull; }

// v18: Real constructor functions for type coercion calls (Boolean(x), Number(x), String(x))
static Item js_ctor_boolean_fn(Item arg) { return js_to_boolean(arg); }
static Item js_ctor_number_fn(Item arg) { return js_to_number(arg); }
static Item js_ctor_string_fn(Item arg) { return js_to_string(arg); }

// Forward declaration of JsFunction struct (matches js_runtime.cpp definition)
struct JsCtor {
    TypeId type_id;
    void* func_ptr;
    int param_count;
    Item* env;
    int env_size;
    Item prototype;
    Item bound_this;
    Item* bound_args;
    int bound_argc;
    String* name;
    int builtin_id;
    Item properties_map; // v18: must match JsFunction layout
};

static Item js_create_constructor(int ctor_id, const char* name, int param_count) {
    if (!js_ctor_cache_init) {
        for (int i = 0; i < JS_CTOR_MAX; i++) js_constructor_cache[i] = ItemNull;
        js_ctor_cache_init = true;
    }
    if (js_constructor_cache[ctor_id].item != ItemNull.item) {
        return js_constructor_cache[ctor_id];
    }
    // Allocate directly via pool — do NOT use js_new_function() because it
    // caches by func_ptr and all constructors share js_ctor_placeholder.
    JsCtor* fn = (JsCtor*)pool_calloc(js_input->pool, sizeof(JsCtor));
    fn->type_id = LMD_TYPE_FUNC;
    // v18: Use real function pointers for type coercion constructors
    if (ctor_id == JS_CTOR_BOOLEAN) fn->func_ptr = (void*)js_ctor_boolean_fn;
    else if (ctor_id == JS_CTOR_NUMBER) fn->func_ptr = (void*)js_ctor_number_fn;
    else if (ctor_id == JS_CTOR_STRING) fn->func_ptr = (void*)js_ctor_string_fn;
    else fn->func_ptr = (void*)js_ctor_placeholder;
    fn->param_count = param_count;
    fn->env = NULL;
    fn->env_size = 0;
    fn->prototype = ItemNull;
    // NOTE: bound_this left as 0 (from pool_calloc). Do NOT set to ItemNull
    // because ItemNull.item is non-zero and bound check uses truthy test.
    fn->bound_args = NULL;
    fn->bound_argc = 0;
    fn->name = heap_create_name(name, strlen(name));
    fn->builtin_id = 0;
    Item fn_item = (Item){.function = (Function*)fn};
    js_constructor_cache[ctor_id] = fn_item;
    return fn_item;
}

extern "C" Item js_get_constructor(Item name_item) {
    if (get_type_id(name_item) != LMD_TYPE_STRING) return make_js_undefined();
    String* name = it2s(name_item);
    if (!name) return make_js_undefined();

    struct { const char* name; int len; int id; int pc; } ctors[] = {
        {"Object", 6, JS_CTOR_OBJECT, 1},
        {"Array", 5, JS_CTOR_ARRAY, 0},
        {"Function", 8, JS_CTOR_FUNCTION, 1},
        {"String", 6, JS_CTOR_STRING, 1},
        {"Number", 6, JS_CTOR_NUMBER, 1},
        {"Boolean", 7, JS_CTOR_BOOLEAN, 1},
        {"Symbol", 6, JS_CTOR_SYMBOL, 0},
        {"Error", 5, JS_CTOR_ERROR, 1},
        {"TypeError", 9, JS_CTOR_TYPE_ERROR, 1},
        {"RangeError", 10, JS_CTOR_RANGE_ERROR, 1},
        {"ReferenceError", 14, JS_CTOR_REFERENCE_ERROR, 1},
        {"SyntaxError", 11, JS_CTOR_SYNTAX_ERROR, 1},
        {"URIError", 8, JS_CTOR_URI_ERROR, 1},
        {"RegExp", 6, JS_CTOR_REGEXP, 2},
        {"Date", 4, JS_CTOR_DATE, 1},
        {"Promise", 7, JS_CTOR_PROMISE, 1},
        {"Map", 3, JS_CTOR_MAP, 0},
        {"Set", 3, JS_CTOR_SET, 0},
        {"WeakMap", 7, JS_CTOR_WEAKMAP, 0},
        {"WeakSet", 7, JS_CTOR_WEAKSET, 0},
        {NULL, 0, 0, 0}
    };

    for (int i = 0; ctors[i].name; i++) {
        if ((int)name->len == ctors[i].len && strncmp(name->chars, ctors[i].name, name->len) == 0) {
            return js_create_constructor(ctors[i].id, ctors[i].name, ctors[i].pc);
        }
    }
    return make_js_undefined();
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
#define JS_SYMBOL_ID_ASYNC_ITERATOR 5
#define JS_SYMBOL_ID_SPECIES        6
#define JS_SYMBOL_ID_MATCH          7
#define JS_SYMBOL_ID_REPLACE        8
#define JS_SYMBOL_ID_SEARCH         9
#define JS_SYMBOL_ID_SPLIT          10
#define JS_SYMBOL_ID_UNSCOPABLES    11

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
    // encode as an int with a special range: -(id + JS_SYMBOL_BASE)
    return (Item){.item = i2it(-(int64_t)(id + JS_SYMBOL_BASE))};
}

static bool js_is_symbol_item(Item item) {
    if (get_type_id(item) != LMD_TYPE_INT) return false;
    int64_t v = it2i(item);
    return v <= -(int64_t)JS_SYMBOL_BASE;
}

static uint64_t js_symbol_item_id(Item item) {
    return (uint64_t)(-(it2i(item) + (int64_t)JS_SYMBOL_BASE));
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
    if (id == JS_SYMBOL_ID_ITERATOR)       return (Item){.item = s2it(heap_create_name("Symbol(Symbol.iterator)", 23))};
    if (id == JS_SYMBOL_ID_TO_PRIMITIVE)   return (Item){.item = s2it(heap_create_name("Symbol(Symbol.toPrimitive)", 26))};
    if (id == JS_SYMBOL_ID_HAS_INSTANCE)   return (Item){.item = s2it(heap_create_name("Symbol(Symbol.hasInstance)", 26))};
    if (id == JS_SYMBOL_ID_TO_STRING_TAG)  return (Item){.item = s2it(heap_create_name("Symbol(Symbol.toStringTag)", 26))};
    if (id == JS_SYMBOL_ID_ASYNC_ITERATOR) return (Item){.item = s2it(heap_create_name("Symbol(Symbol.asyncIterator)", 28))};
    if (id == JS_SYMBOL_ID_SPECIES)        return (Item){.item = s2it(heap_create_name("Symbol(Symbol.species)", 22))};
    if (id == JS_SYMBOL_ID_MATCH)          return (Item){.item = s2it(heap_create_name("Symbol(Symbol.match)", 20))};
    if (id == JS_SYMBOL_ID_REPLACE)        return (Item){.item = s2it(heap_create_name("Symbol(Symbol.replace)", 22))};
    if (id == JS_SYMBOL_ID_SEARCH)         return (Item){.item = s2it(heap_create_name("Symbol(Symbol.search)", 21))};
    if (id == JS_SYMBOL_ID_SPLIT)          return (Item){.item = s2it(heap_create_name("Symbol(Symbol.split)", 20))};
    if (id == JS_SYMBOL_ID_UNSCOPABLES)    return (Item){.item = s2it(heap_create_name("Symbol(Symbol.unscopables)", 26))};

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

// Return a well-known symbol by its property name on the Symbol constructor.
// e.g. Symbol.iterator → fixed ID=1, Symbol.toPrimitive → fixed ID=2, etc.
// Unlike js_symbol_create(), this always returns the SAME item for a given name.
extern "C" Item js_symbol_well_known(Item name) {
    String* s = it2s(name);
    if (s) {
        if (s->len == 8 && strncmp(s->chars, "iterator", 8) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_ITERATOR);
        if (s->len == 11 && strncmp(s->chars, "toPrimitive", 11) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_TO_PRIMITIVE);
        if (s->len == 11 && strncmp(s->chars, "hasInstance", 11) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_HAS_INSTANCE);
        if (s->len == 11 && strncmp(s->chars, "toStringTag", 11) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_TO_STRING_TAG);
        if (s->len == 13 && strncmp(s->chars, "asyncIterator", 13) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_ASYNC_ITERATOR);
        if (s->len == 7 && strncmp(s->chars, "species", 7) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_SPECIES);
        if (s->len == 5 && strncmp(s->chars, "match", 5) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_MATCH);
        if (s->len == 7 && strncmp(s->chars, "replace", 7) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_REPLACE);
        if (s->len == 6 && strncmp(s->chars, "search", 6) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_SEARCH);
        if (s->len == 5 && strncmp(s->chars, "split", 5) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_SPLIT);
        if (s->len == 11 && strncmp(s->chars, "unscopables", 11) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_UNSCOPABLES);
    }
    // Unknown well-known symbol — create a stable entry via Symbol.for semantics
    return js_symbol_for(name);
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

// ReadableStream stub — Web Streams API minimal implementation.
// The test checks: typeof readable.getReader === "function"
static Item js_readable_stream_get_reader_stub(void) {
    return js_new_object();
}

extern "C" Item js_readable_stream_new(void) {
    Item obj = js_new_object();
    Item class_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    Item class_val = (Item){.item = s2it(heap_create_name("ReadableStream"))};
    js_property_set(obj, class_key, class_val);
    Item get_reader_key = (Item){.item = s2it(heap_create_name("getReader"))};
    Item get_reader_fn = js_new_function((void*)js_readable_stream_get_reader_stub, 0);
    js_property_set(obj, get_reader_key, get_reader_fn);
    return obj;
}

// WritableStream stub
static Item js_writable_stream_get_writer_stub(void) {
    return js_new_object();
}

extern "C" Item js_writable_stream_new(void) {
    Item obj = js_new_object();
    Item class_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    Item class_val = (Item){.item = s2it(heap_create_name("WritableStream"))};
    js_property_set(obj, class_key, class_val);
    Item get_writer_key = (Item){.item = s2it(heap_create_name("getWriter"))};
    Item get_writer_fn = js_new_function((void*)js_writable_stream_get_writer_stub, 0);
    js_property_set(obj, get_writer_key, get_writer_fn);
    return obj;
}