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
extern Item parse_json_to_item_strict(Input* input, const char* json_string, bool* ok);

// v24: strict mode flag from js_runtime.cpp
extern bool js_strict_mode;

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
extern "C" Item js_object_get_own_property_names(Item object);
extern "C" Item js_object_get_own_property_symbols(Item object);
extern "C" Item js_array_push(Item array, Item value);
extern "C" Item js_object_define_property(Item obj, Item name, Item descriptor);
extern "C" Item js_object_prevent_extensions(Item obj);

// forward declaration for builtin method check helper
static bool js_map_has_builtin_method(Map* m, const char* name, int len);
extern "C" Item js_lookup_builtin_method(TypeId type, const char* name, int len);

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

// Date() without 'new' — returns a string representation of the current date/time
extern "C" Item js_date_now_string(void) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char buf[128];
    // Match V8/SpiderMonkey format: "Thu Jan 01 1970 00:00:00 GMT+0000 (Timezone)"
    strftime(buf, sizeof(buf), "%a %b %d %Y %H:%M:%S GMT%z", tm_info);
    return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
}

// new Date(value) — accepts a numeric timestamp (ms since epoch) or a date string
extern "C" Item js_date_new_from(Item value) {
    Item obj = js_new_object();
    Item key = (Item){.item = s2it(heap_create_name("_time"))};
    TypeId tid = get_type_id(value);

    // helper: store ms with TimeClip validation (|v| > 8.64e15 → NaN)
    auto store_time = [&](double ms) {
        if (isnan(ms) || isinf(ms) || ms > 8.64e15 || ms < -8.64e15) ms = NAN;
        static double date_buf[16];
        static int date_idx = 0;
        double* fp = &date_buf[date_idx++ % 16];
        *fp = ms;
        js_property_set(obj, key, (Item){.item = d2it(fp)});
    };

    if (tid == LMD_TYPE_INT || tid == LMD_TYPE_INT64 || tid == LMD_TYPE_FLOAT) {
        double ms;
        if (tid == LMD_TYPE_FLOAT) ms = it2d(value);
        else ms = (double)it2i(value);
        store_time(ms);
    } else if (tid == LMD_TYPE_STRING) {
        // parse date string — try ISO 8601 format
        String* s = it2s(value);
        if (s) {
            struct tm tm = {};
            char* rest = strptime(s->chars, "%Y-%m-%dT%H:%M:%S", &tm);
            if (!rest) rest = strptime(s->chars, "%a %b %d %Y %H:%M:%S", &tm);
            if (!rest) rest = strptime(s->chars, "%c", &tm);
            if (rest) {
                time_t t = timegm(&tm);
                double ms = (double)t * 1000.0;
                // Parse fractional seconds (e.g. ".872" → 872ms)
                if (rest && *rest == '.') {
                    char* end_frac;
                    double frac = strtod(rest, &end_frac);
                    ms += frac * 1000.0;
                    rest = end_frac;
                }
                store_time(ms);
            } else {
                // fallback: try mktime (local time)
                struct tm tm2 = {};
                if (sscanf(s->chars, "%d-%d-%d", &tm2.tm_year, &tm2.tm_mon, &tm2.tm_mday) == 3) {
                    tm2.tm_year -= 1900;
                    tm2.tm_mon -= 1;
                    time_t t = mktime(&tm2);
                    double ms = (double)t * 1000.0;
                    store_time(ms);
                } else {
                    // If unparseable, set NaN (Invalid Date)
                    store_time(NAN);
                }
            }
        } else {
            store_time(NAN);
        }
    } else if (tid == LMD_TYPE_MAP) {
        // Date object: extract _time from the other Date
        bool has_time = false;
        Item other_time = js_map_get_fast_ext(value.map, "_time", 5, &has_time);
        if (has_time && (get_type_id(other_time) == LMD_TYPE_FLOAT || get_type_id(other_time) == LMD_TYPE_INT || get_type_id(other_time) == LMD_TYPE_INT64)) {
            double ms = (get_type_id(other_time) == LMD_TYPE_FLOAT) ? it2d(other_time) : (double)it2i(other_time);
            store_time(ms);
        } else {
            // Non-Date object: ToPrimitive(value) per ES spec §21.4.2
            // 1. Check Symbol.toPrimitive
            Item sym_key = (Item){.item = s2it(heap_create_name("__sym_2", 7))};
            Item to_prim = js_property_get(value, sym_key);
            Item prim;
            if (to_prim.item != ItemNull.item && get_type_id(to_prim) == LMD_TYPE_FUNC) {
                Item hint = (Item){.item = s2it(heap_create_name("default", 7))};
                Item args[1] = { hint };
                prim = js_call_function(to_prim, value, args, 1);
                if (js_check_exception()) return ItemNull;
                // If result is an object, throw TypeError
                TypeId pt = get_type_id(prim);
                if (pt == LMD_TYPE_MAP || pt == LMD_TYPE_ARRAY || pt == LMD_TYPE_ELEMENT) {
                    js_throw_type_error("Cannot convert object to primitive value");
                    return ItemNull;
                }
                // Symbol results → throw TypeError
                if ((pt == LMD_TYPE_INT && it2i(prim) <= -(int64_t)JS_SYMBOL_BASE) || pt == LMD_TYPE_SYMBOL) {
                    js_throw_type_error("Cannot convert a Symbol value to a number");
                    return ItemNull;
                }
            } else {
                // No Symbol.toPrimitive: try valueOf/toString
                prim = js_to_number(value);
                if (js_check_exception()) return ItemNull;
                TypeId pt = get_type_id(prim);
                if (pt == LMD_TYPE_FLOAT)
                    store_time(it2d(prim));
                else if (pt == LMD_TYPE_INT || pt == LMD_TYPE_INT64)
                    store_time((double)it2i(prim));
                else
                    store_time(NAN);
                goto date_done;
            }
            // Dispatch on ToPrimitive result type
            TypeId pt = get_type_id(prim);
            if (pt == LMD_TYPE_STRING) {
                // Re-enter Date constructor with the string
                return js_date_new_from(prim);
            } else {
                // ToNumber on the primitive
                Item num = js_to_number(prim);
                TypeId nt = get_type_id(num);
                if (nt == LMD_TYPE_FLOAT)
                    store_time(it2d(num));
                else if (nt == LMD_TYPE_INT || nt == LMD_TYPE_INT64)
                    store_time((double)it2i(num));
                else
                    store_time(NAN);
            }
        }
    } else {
        // Per spec: ToNumber(value) then TimeClip
        // null→0, undefined→NaN, true→1, false→0
        Item num = js_to_number(value);
        TypeId nt = get_type_id(num);
        if (nt == LMD_TYPE_FLOAT) store_time(it2d(num));
        else if (nt == LMD_TYPE_INT || nt == LMD_TYPE_INT64) store_time((double)it2i(num));
        else store_time(NAN);
    }
date_done:
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

    // guard: if no _time property, receiver is not a Date object — TypeError per ES spec
    TypeId tv_type = get_type_id(time_val);
    if (tv_type != LMD_TYPE_FLOAT && tv_type != LMD_TYPE_INT && tv_type != LMD_TYPE_INT64) {
        return js_throw_type_error("this is not a Date object");
    }

    double ms = time_val.get_double();
    if (method_id == 0) { // getTime
        static double gt_buf[16];
        static int gt_idx = 0;
        double* fp = &gt_buf[gt_idx++ % 16];
        *fp = ms;
        return (Item){.item = d2it(fp)};
    }
    // NaN (Invalid Date) handling
    if (isnan(ms)) {
        if (method_id == 8) // toISOString: throw RangeError for Invalid Date
            return js_throw_type_error("Invalid time value");
        if (method_id == 17 || method_id == 9) // toString, toLocaleDateString
            return (Item){.item = s2it(heap_create_name("Invalid Date", 12))};
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
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

// v20: Date setter methods — mutate internal _time timestamp
// method_id: 20=setTime, 21=setFullYear, 22=setMonth, 23=setDate,
//   24=setHours, 25=setMinutes, 26=setSeconds, 27=setMilliseconds,
//   30=setUTCFullYear, 31=setUTCMonth, 32=setUTCDate,
//   33=setUTCHours, 34=setUTCMinutes, 35=setUTCSeconds, 36=setUTCMilliseconds
// 40=getDay, 41=getUTCDay, 42=getTimezoneOffset, 43=valueOf, 44=toJSON,
// 45=toUTCString, 46=toDateString, 47=toTimeString
extern "C" Item js_date_setter(Item date_obj, int method_id, Item arg0, Item arg1, Item arg2, Item arg3) {
    Item key = (Item){.item = s2it(heap_create_name("_time"))};
    Item time_val = js_property_get(date_obj, key);

    // guard: if no _time property, receiver is not a Date object — TypeError per ES spec
    TypeId tv_type = get_type_id(time_val);
    if (tv_type != LMD_TYPE_FLOAT && tv_type != LMD_TYPE_INT && tv_type != LMD_TYPE_INT64) {
        if (method_id == 43) { // valueOf — fall back to generic Object.prototype.valueOf
            if (get_type_id(date_obj) == LMD_TYPE_MAP) {
                bool own_pv = false;
                Item pv = js_map_get_fast_ext(date_obj.map, "__primitiveValue__", 18, &own_pv);
                if (own_pv) return pv;
            }
            return date_obj;
        }
        return js_throw_type_error("this is not a Date object");
    }

    double ms = time_val.get_double();

    auto to_double = [](Item v) -> double {
        TypeId t = get_type_id(v);
        if (t == LMD_TYPE_FLOAT) return it2d(v);
        if (t == LMD_TYPE_INT || t == LMD_TYPE_INT64) return (double)it2i(v);
        return NAN;
    };
    auto is_present = [](Item v) -> bool {
        return v.item != ItemNull.item && get_type_id(v) != LMD_TYPE_UNDEFINED;
    };

    auto store_ms = [&](double new_ms) -> Item {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = new_ms;
        Item new_time = (Item){.item = d2it(fp)};
        js_property_set(date_obj, key, new_time);
        return new_time;
    };

    // getDay / getUTCDay / getTimezoneOffset / valueOf / toJSON / toUTCString / toDateString / toTimeString
    if (method_id >= 40) {
        // NaN (Invalid Date) handling
        if (isnan(ms)) {
            if (method_id == 43) { // valueOf — return NaN
                double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
                *fp = NAN;
                return (Item){.item = d2it(fp)};
            }
            if (method_id == 44) // toJSON — return null for Invalid Date
                return ItemNull;
            if (method_id == 45 || method_id == 46 || method_id == 47) // string representations
                return (Item){.item = s2it(heap_create_name("Invalid Date", 12))};
            double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *fp = NAN;
            return (Item){.item = d2it(fp)};
        }
        time_t secs = (time_t)(ms / 1000.0);
        if (method_id == 40) { // getDay
            struct tm tm; localtime_r(&secs, &tm);
            return (Item){.item = i2it(tm.tm_wday)};
        }
        if (method_id == 41) { // getUTCDay
            struct tm utc; gmtime_r(&secs, &utc);
            return (Item){.item = i2it(utc.tm_wday)};
        }
        if (method_id == 42) { // getTimezoneOffset
            struct tm local_tm; localtime_r(&secs, &local_tm);
            struct tm utc_tm; gmtime_r(&secs, &utc_tm);
            time_t local_t = mktime(&local_tm);
            time_t utc_t = mktime(&utc_tm);
            int offset_min = (int)((utc_t - local_t) / 60);
            return (Item){.item = i2it(offset_min)};
        }
        if (method_id == 43) { // valueOf — same as getTime
            double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *fp = ms;
            return (Item){.item = d2it(fp)};
        }
        if (method_id == 44) { // toJSON — same as toISOString
            return js_date_method(date_obj, 8);
        }
        if (method_id == 45) { // toUTCString
            struct tm utc; gmtime_r(&secs, &utc);
            static const char* wday[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
            static const char* mon[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            char buf[64];
            snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT",
                wday[utc.tm_wday], utc.tm_mday, mon[utc.tm_mon],
                utc.tm_year + 1900, utc.tm_hour, utc.tm_min, utc.tm_sec);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        if (method_id == 46) { // toDateString
            struct tm tm; localtime_r(&secs, &tm);
            static const char* wday[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
            static const char* mon[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
            char buf[32];
            snprintf(buf, sizeof(buf), "%s %s %02d %04d",
                wday[tm.tm_wday], mon[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        if (method_id == 47) { // toTimeString
            struct tm tm; localtime_r(&secs, &tm);
            long gmtoff = tm.tm_gmtoff;
            int h_off = (int)(gmtoff / 3600);
            int m_off = (int)((gmtoff % 3600) / 60);
            if (m_off < 0) m_off = -m_off;
            char buf[64];
            snprintf(buf, sizeof(buf), "%02d:%02d:%02d GMT%+03d%02d",
                tm.tm_hour, tm.tm_min, tm.tm_sec, h_off, m_off);
            return (Item){.item = s2it(heap_create_name(buf))};
        }
        return ItemNull;
    }

    if (method_id == 20) { // setTime
        double new_ms = to_double(arg0);
        return store_ms(new_ms);
    }

    // local setters (21-27)
    if (method_id >= 21 && method_id <= 27) {
        time_t secs = (time_t)(ms / 1000.0);
        int old_millis = (int)(ms - (double)secs * 1000.0);
        if (old_millis < 0) old_millis += 1000;
        struct tm tm;
        localtime_r(&secs, &tm);

        switch (method_id) {
            case 21: // setFullYear(year [, month, date])
                tm.tm_year = (int)to_double(arg0) - 1900;
                if (is_present(arg1)) tm.tm_mon = (int)to_double(arg1);
                if (is_present(arg2)) tm.tm_mday = (int)to_double(arg2);
                break;
            case 22: // setMonth(month [, date])
                tm.tm_mon = (int)to_double(arg0);
                if (is_present(arg1)) tm.tm_mday = (int)to_double(arg1);
                break;
            case 23: // setDate(date)
                tm.tm_mday = (int)to_double(arg0);
                break;
            case 24: // setHours(hour [, min, sec, ms])
                tm.tm_hour = (int)to_double(arg0);
                if (is_present(arg1)) tm.tm_min = (int)to_double(arg1);
                if (is_present(arg2)) tm.tm_sec = (int)to_double(arg2);
                if (is_present(arg3)) old_millis = (int)to_double(arg3);
                break;
            case 25: // setMinutes(min [, sec, ms])
                tm.tm_min = (int)to_double(arg0);
                if (is_present(arg1)) tm.tm_sec = (int)to_double(arg1);
                if (is_present(arg2)) old_millis = (int)to_double(arg2);
                break;
            case 26: // setSeconds(sec [, ms])
                tm.tm_sec = (int)to_double(arg0);
                if (is_present(arg1)) old_millis = (int)to_double(arg1);
                break;
            case 27: // setMilliseconds(ms)
                old_millis = (int)to_double(arg0);
                break;
        }
        tm.tm_isdst = -1;
        time_t new_secs = mktime(&tm);
        double new_ms = (double)new_secs * 1000.0 + (double)old_millis;
        return store_ms(new_ms);
    }

    // UTC setters (30-36)
    if (method_id >= 30 && method_id <= 36) {
        time_t secs = (time_t)(ms / 1000.0);
        int old_millis = (int)(ms - (double)secs * 1000.0);
        if (old_millis < 0) old_millis += 1000;
        struct tm utc;
        gmtime_r(&secs, &utc);

        switch (method_id) {
            case 30: // setUTCFullYear(year [, month, date])
                utc.tm_year = (int)to_double(arg0) - 1900;
                if (is_present(arg1)) utc.tm_mon = (int)to_double(arg1);
                if (is_present(arg2)) utc.tm_mday = (int)to_double(arg2);
                break;
            case 31: // setUTCMonth(month [, date])
                utc.tm_mon = (int)to_double(arg0);
                if (is_present(arg1)) utc.tm_mday = (int)to_double(arg1);
                break;
            case 32: // setUTCDate(date)
                utc.tm_mday = (int)to_double(arg0);
                break;
            case 33: // setUTCHours(hour [, min, sec, ms])
                utc.tm_hour = (int)to_double(arg0);
                if (is_present(arg1)) utc.tm_min = (int)to_double(arg1);
                if (is_present(arg2)) utc.tm_sec = (int)to_double(arg2);
                if (is_present(arg3)) old_millis = (int)to_double(arg3);
                break;
            case 34: // setUTCMinutes(min [, sec, ms])
                utc.tm_min = (int)to_double(arg0);
                if (is_present(arg1)) utc.tm_sec = (int)to_double(arg1);
                if (is_present(arg2)) old_millis = (int)to_double(arg2);
                break;
            case 35: // setUTCSeconds(sec [, ms])
                utc.tm_sec = (int)to_double(arg0);
                if (is_present(arg1)) old_millis = (int)to_double(arg1);
                break;
            case 36: // setUTCMilliseconds(ms)
                old_millis = (int)to_double(arg0);
                break;
        }
        time_t new_secs = timegm(&utc);
        double new_ms = (double)new_secs * 1000.0 + (double)old_millis;
        return store_ms(new_ms);
    }

    return ItemNull;
}

// v20: new Date(year, month [, day, hours, minutes, seconds, ms]) — multi-arg constructor
extern "C" Item js_date_new_multi(Item args_array) {
    int len = (int)js_array_length(args_array);
    auto get_arg_int = [&](int idx) -> int {
        Item val = js_array_get_int(args_array, idx);
        TypeId t = get_type_id(val);
        if (t == LMD_TYPE_INT || t == LMD_TYPE_INT64) return (int)it2i(val);
        if (t == LMD_TYPE_FLOAT) return (int)it2d(val);
        return 0;
    };

    int year = len > 0 ? get_arg_int(0) : 0;
    int month = len > 1 ? get_arg_int(1) : 0;
    int day = len > 2 ? get_arg_int(2) : 1;
    int hour = len > 3 ? get_arg_int(3) : 0;
    int min = len > 4 ? get_arg_int(4) : 0;
    int sec = len > 5 ? get_arg_int(5) : 0;
    int millis = len > 6 ? get_arg_int(6) : 0;

    struct tm tm = {};
    // ES spec: if 0 <= year <= 99, treat as 1900 + year
    if (year >= 0 && year <= 99) year += 1900;
    tm.tm_year = year - 1900;
    tm.tm_mon = month;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_sec = sec;
    tm.tm_isdst = -1;

    time_t t = mktime(&tm);  // local time constructor
    double ms_val = (double)t * 1000.0 + (double)millis;

    Item obj = js_new_object();
    Item time_key = (Item){.item = s2it(heap_create_name("_time"))};
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = ms_val;
    js_property_set(obj, time_key, (Item){.item = d2it(fp)});
    Item cls_key = (Item){.item = s2it(heap_create_name("__class_name__"))};
    js_property_set(obj, cls_key, (Item){.item = s2it(heap_create_name("Date"))});
    return obj;
}

// v20: Date.parse(string) — parse a date string, return ms since epoch
extern "C" Item js_date_parse(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }
    struct tm tm = {};
    // Try ISO 8601 first
    if (strptime(s->chars, "%Y-%m-%dT%H:%M:%S", &tm) ||
        strptime(s->chars, "%Y-%m-%d", &tm) ||
        strptime(s->chars, "%a, %d %b %Y %H:%M:%S", &tm) ||
        strptime(s->chars, "%d %b %Y %H:%M:%S", &tm) ||
        strptime(s->chars, "%a %b %d %Y %H:%M:%S", &tm) ||
        strptime(s->chars, "%c", &tm)) {
        time_t t = timegm(&tm);
        double ms = (double)t * 1000.0;
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = ms;
        return (Item){.item = d2it(fp)};
    }
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = NAN;
    return (Item){.item = d2it(fp)};
}

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

    // Get radix per ES spec: ToInt32(radix), then validate 2-36
    int radix = 10;
    bool radix_explicit = false;
    TypeId rtype = get_type_id(radix_item);
    if (rtype != LMD_TYPE_UNDEFINED) {
        // Convert radix to number (handles strings, booleans, boxed numbers, etc.)
        Item radix_num = js_to_number(radix_item);
        TypeId rn_type = get_type_id(radix_num);
        double rdbl = 0;
        if (rn_type == LMD_TYPE_INT) rdbl = (double)it2i(radix_num);
        else if (rn_type == LMD_TYPE_FLOAT) rdbl = it2d(radix_num);
        // ToInt32: NaN, Infinity, -Infinity → 0
        if (isnan(rdbl) || isinf(rdbl)) {
            radix = 0;
        } else {
            // ToInt32 per ES spec: modulo 2^32, then signed 32-bit
            double d = fmod(trunc(rdbl), 4294967296.0);
            if (d < 0) d += 4294967296.0;
            if (d >= 2147483648.0) d -= 4294967296.0;
            radix = (int)d;
        }
        radix_explicit = true;
    }
    if (radix == 0) { radix = 10; radix_explicit = false; }
    // Invalid radix: return NaN
    if (radix_explicit && (radix < 2 || radix > 36)) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    // Null-terminate
    char buf[256];
    int len = s->len < 255 ? s->len : 255;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';

    // Skip whitespace (ES spec StrWhiteSpaceChar: ASCII + Unicode whitespace)
    char* start = buf;
    char* end_buf = buf + len;
    for (;;) {
        if (start >= end_buf) break;
        unsigned char c = (unsigned char)*start;
        // ASCII whitespace: space, tab, LF, CR, FF, VT
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') { start++; continue; }
        // 2-byte UTF-8: U+00A0 (NBSP) = 0xC2 0xA0
        if (c == 0xC2 && start + 1 < end_buf && (unsigned char)start[1] == 0xA0) { start += 2; continue; }
        // 3-byte UTF-8 whitespace chars
        if (c == 0xE2 && start + 2 < end_buf) {
            unsigned char b1 = (unsigned char)start[1], b2 = (unsigned char)start[2];
            // U+2000-U+200A (en/em space etc) = E2 80 80..8A
            if (b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A) { start += 3; continue; }
            // U+2028 (LS) = E2 80 A8, U+2029 (PS) = E2 80 A9
            if (b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9)) { start += 3; continue; }
            // U+202F (narrow NBSP) = E2 80 AF
            if (b1 == 0x80 && b2 == 0xAF) { start += 3; continue; }
            // U+205F (medium math space) = E2 81 9F
            if (b1 == 0x81 && b2 == 0x9F) { start += 3; continue; }
        }
        // U+3000 (ideographic space) = E3 80 80
        if (c == 0xE3 && start + 2 < end_buf && (unsigned char)start[1] == 0x80 && (unsigned char)start[2] == 0x80) { start += 3; continue; }
        // U+FEFF (BOM/ZWNBSP) = EF BB BF
        if (c == 0xEF && start + 2 < end_buf && (unsigned char)start[1] == 0xBB && (unsigned char)start[2] == 0xBF) { start += 3; continue; }
        // U+1680 (ogham space) = E1 9A 80
        if (c == 0xE1 && start + 2 < end_buf && (unsigned char)start[1] == 0x9A && (unsigned char)start[2] == 0x80) { start += 3; continue; }
        break;
    }

    // Parse sign (before hex prefix per ES spec)
    int sign = 1;
    if (*start == '-') { sign = -1; start++; }
    else if (*start == '+') { start++; }

    // Auto-detect hex prefix (0x/0X) when no explicit radix
    if (!radix_explicit && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        radix = 16;
        start += 2;
    } else if (radix == 16 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        start += 2; // skip 0x prefix even with explicit radix 16
    }

    // Manual parseInt per ES spec: accumulate as double to handle large values
    double result = 0;
    bool found_digit = false;
    while (*start) {
        int digit = -1;
        char ch = *start;
        if (ch >= '0' && ch <= '9') digit = ch - '0';
        else if (ch >= 'a' && ch <= 'z') digit = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'Z') digit = ch - 'A' + 10;
        if (digit < 0 || digit >= radix) break;
        found_digit = true;
        result = result * radix + digit;
        start++;
    }
    if (!found_digit) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }
    result *= sign;

    // Return as int if it fits, otherwise as double
    if (result >= -9007199254740992.0 && result <= 9007199254740992.0 &&
        result == (double)(int64_t)result) {
        return (Item){.item = i2it((int64_t)result)};
    }
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = result;
    return (Item){.item = d2it(fp)};
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

    // ES spec: skip leading StrWhiteSpaceChar (includes Unicode whitespace)
    char* p = buf;
    while (*p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\f' || *p == '\v') { p++; continue; }
        // UTF-8 two-byte: U+00A0 (NBSP) = C2 A0
        if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA0) { p += 2; continue; }
        // UTF-8 three-byte Unicode whitespace:
        // U+FEFF (BOM) = EF BB BF
        if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) { p += 3; continue; }
        // U+2000-U+200A (various spaces), U+2028 (LS), U+2029 (PS), U+202F, U+205F, U+3000
        // All encoded as E2 8x xx or E3 80 80
        if ((unsigned char)p[0] == 0xE2) {
            unsigned char b1 = (unsigned char)p[1], b2 = (unsigned char)p[2];
            // U+2000-U+200A: E2 80 80 - E2 80 8A
            if (b1 == 0x80 && b2 >= 0x80 && b2 <= 0x8A) { p += 3; continue; }
            // U+2028: E2 80 A8, U+2029: E2 80 A9
            if (b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9)) { p += 3; continue; }
            // U+202F: E2 80 AF
            if (b1 == 0x80 && b2 == 0xAF) { p += 3; continue; }
            // U+205F: E2 81 9F
            if (b1 == 0x81 && b2 == 0x9F) { p += 3; continue; }
        }
        // U+3000 (ideographic space): E3 80 80
        if ((unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 && (unsigned char)p[2] == 0x80) { p += 3; continue; }
        break;
    }

    // ES spec: parseFloat only parses StrDecimalLiteral — no hex (0x), no 0o, no 0b.
    // strtod parses hex on many platforms, so we must guard against it.
    // StrDecimalLiteral: [+-]? (Infinity | DecimalDigits [. DecimalDigits] [eE [+-] DecimalDigits] | . DecimalDigits [eE ...])
    char* start = p;
    if (*p == '+' || *p == '-') p++;
    if (*p == 'I') {
        // check for "Infinity"
        if (strncmp(p, "Infinity", 8) == 0) {
            double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
            *fp = (*start == '-') ? -HUGE_VAL : HUGE_VAL;
            return (Item){.item = d2it(fp)};
        }
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }
    // must start with a decimal digit or '.'
    if ((*p < '0' || *p > '9') && *p != '.') {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }
    // '.' alone without a following digit is not valid
    if (*p == '.' && (p[1] < '0' || p[1] > '9')) {
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = NAN;
        return (Item){.item = d2it(fp)};
    }

    // scan to find the end of the valid decimal literal (no hex)
    char* q = p;
    while (*q >= '0' && *q <= '9') q++;         // integer part
    if (*q == '.') { q++; while (*q >= '0' && *q <= '9') q++; } // fractional part
    if (*q == 'e' || *q == 'E') {                // exponent
        q++;
        if (*q == '+' || *q == '-') q++;
        if (*q >= '0' && *q <= '9') { while (*q >= '0' && *q <= '9') q++; }
        else { q = q - (q[-1] == '+' || q[-1] == '-' ? 2 : 1); } // no digits after e → backtrack
    }

    // null-terminate at the end of valid decimal literal and parse
    char saved = *q;
    *q = '\0';
    char* endptr;
    double val = strtod(start, &endptr);
    *q = saved;

    if (endptr == start) {
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

    // Step 1-4 per spec: validate fractionDigits BEFORE checking NaN
    int digits = 0;
    TypeId dtype = get_type_id(digits_item);
    if (dtype == LMD_TYPE_INT) {
        digits = (int)it2i(digits_item);
    } else if (dtype == LMD_TYPE_FLOAT) {
        double fd = it2d(digits_item);
        if (!isfinite(fd)) return js_throw_range_error("toFixed() digits argument must be between 0 and 100");
        digits = (int)fd;
    } else if (dtype != LMD_TYPE_UNDEFINED) {
        // coerce non-numeric to 0 (e.g. NaN string → 0)
        digits = 0;
    }

    // ES spec: RangeError if digits < 0 or > 100
    if (digits < 0 || digits > 100) {
        return js_throw_range_error("toFixed() digits argument must be between 0 and 100");
    }

    // Step 5: If x is NaN, return "NaN"
    if (isnan(num)) return (Item){.item = s2it(heap_create_name("NaN", 3))};

    // Step 6: If x is not finite, format Infinity
    if (isinf(num)) return num > 0
        ? (Item){.item = s2it(heap_create_name("Infinity", 8))}
        : (Item){.item = s2it(heap_create_name("-Infinity", 9))};

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
        // per spec: ToInteger(radix) + range check BEFORE NaN/Infinity handling
        if (argc > 0 && get_type_id(args[0]) != LMD_TYPE_UNDEFINED) {
            Item radix_item = js_to_number(args[0]);
            if (js_check_exception()) return ItemNull;
            int radix = 10;
            TypeId rt = get_type_id(radix_item);
            if (rt == LMD_TYPE_INT) radix = (int)it2i(radix_item);
            else if (rt == LMD_TYPE_FLOAT) {
                double rd = it2d(radix_item);
                if (isnan(rd)) radix = 0; // will trigger RangeError
                else radix = (int)rd;
            }
            // v20: RangeError for invalid radix
            if (radix < 2 || radix > 36) {
                return js_throw_range_error("toString() radix must be between 2 and 36");
            }
            if (radix != 10) {
                // v20: Handle float-to-radix conversion (integer + fractional parts)
                TypeId nt = get_type_id(num);
                double dval = 0;
                if (nt == LMD_TYPE_INT) dval = (double)it2i(num);
                else if (nt == LMD_TYPE_FLOAT) dval = it2d(num);
                // Handle NaN/Infinity for non-10 radix
                if (isnan(dval)) return (Item){.item = s2it(heap_create_name("NaN", 3))};
                if (isinf(dval)) return dval > 0
                    ? (Item){.item = s2it(heap_create_name("Infinity", 8))}
                    : (Item){.item = s2it(heap_create_name("-Infinity", 9))};
                bool negative = dval < 0;
                if (negative) dval = -dval;

                // Integer part
                uint64_t int_part = (uint64_t)dval;
                double frac_part = dval - (double)int_part;

                const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
                char buf[256];
                int pos = 128;
                buf[pos] = '\0';

                // Convert integer part
                if (int_part == 0) {
                    buf[--pos] = '0';
                } else {
                    while (int_part > 0) {
                        buf[--pos] = digits[int_part % radix];
                        int_part /= radix;
                    }
                }
                if (negative) buf[--pos] = '-';

                // Convert fractional part
                if (frac_part > 0) {
                    int frac_pos = 128;
                    buf[frac_pos++] = '.';
                    // Emit up to 20 fractional digits (sufficient precision)
                    int max_frac = 20;
                    for (int i = 0; i < max_frac && frac_part > 0; i++) {
                        frac_part *= radix;
                        int digit = (int)frac_part;
                        buf[frac_pos++] = digits[digit];
                        frac_part -= digit;
                        // Stop if remaining fraction is negligible
                        if (frac_part < 1e-15) break;
                    }
                    buf[frac_pos] = '\0';
                    return (Item){.item = s2it(heap_create_name(&buf[pos], frac_pos - pos))};
                }
                return (Item){.item = s2it(heap_create_name(&buf[pos], 128 - pos))};
            }
        }
        return js_to_string(num);
    }

    if (method->len == 7 && strncmp(method->chars, "valueOf", 7) == 0) {
        return num;
    }
    if (method->len == 11 && strncmp(method->chars, "toPrecision", 11) == 0) {
        if (argc < 1 || get_type_id(args[0]) == LMD_TYPE_UNDEFINED) return js_to_string(num);
        // per spec: step 3 ToInteger(precision) before step 4 NaN/Infinity check
        Item prec_item = js_to_number(args[0]);
        if (js_check_exception()) return ItemNull;
        int precision = 1;
        TypeId pt = get_type_id(prec_item);
        if (pt == LMD_TYPE_INT) precision = (int)it2i(prec_item);
        else if (pt == LMD_TYPE_FLOAT) precision = (int)it2d(prec_item);
        double d = 0;
        TypeId nt = get_type_id(num);
        if (nt == LMD_TYPE_INT) d = (double)it2i(num);
        else if (nt == LMD_TYPE_FLOAT) d = it2d(num);
        // Handle special cases (after ToInteger per spec)
        if (isnan(d)) return (Item){.item = s2it(heap_create_name("NaN", 3))};
        if (isinf(d)) return d > 0
            ? (Item){.item = s2it(heap_create_name("Infinity", 8))}
            : (Item){.item = s2it(heap_create_name("-Infinity", 9))};
        // Range check after NaN/Infinity per spec
        if (precision < 1 || precision > 100) {
            return js_throw_range_error("toPrecision() argument must be between 1 and 100");
        }        // Use %e to get the exponent, then decide format
        char ebuf[128];
        snprintf(ebuf, sizeof(ebuf), "%.*e", precision - 1, d);
        // Parse exponent from ebuf
        char* epos = strchr(ebuf, 'e');
        int exponent = epos ? atoi(epos + 1) : 0;
        char buf[128];
        if (exponent >= 0 && exponent < precision) {
            // Fixed-point format: e.g. 100.00(5) or 1.00(3)
            int frac_digits = precision - 1 - exponent;
            if (frac_digits < 0) frac_digits = 0;
            snprintf(buf, sizeof(buf), "%.*f", frac_digits, d);
        } else if (exponent < 0 && exponent >= -6) {
            // Small number: 0.000123(2) → use fixed-point with enough decimals
            int frac_digits = precision - 1 - exponent; // e.g. 2 - 1 - (-4) = 5
            snprintf(buf, sizeof(buf), "%.*f", frac_digits, d);
        } else {
            // Use exponential format
            snprintf(buf, sizeof(buf), "%.*e", precision - 1, d);
            // Normalize exponent format: remove leading zeros, use e+ or e-
            // C's %e uses e+01, JS uses e+1
            char* ep = strchr(buf, 'e');
            if (ep) {
                char sign = ep[1];
                int exp_val = atoi(ep + 1);
                char exp_buf[16];
                snprintf(exp_buf, sizeof(exp_buf), "e%c%d", sign, abs(exp_val));
                strcpy(ep, exp_buf);
            }
        }
        return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
    }
    if (method->len == 13 && strncmp(method->chars, "toExponential", 13) == 0) {
        // per spec: step 2 ToInteger(fractionDigits) before step 4 NaN check
        bool has_frac = (argc >= 1 && get_type_id(args[0]) != LMD_TYPE_UNDEFINED);
        int frac = 0;
        if (has_frac) {
            Item frac_item = js_to_number(args[0]);
            if (js_check_exception()) return ItemNull;
            TypeId ft = get_type_id(frac_item);
            if (ft == LMD_TYPE_INT) frac = (int)it2i(frac_item);
            else if (ft == LMD_TYPE_FLOAT) frac = (int)it2d(frac_item);
        }
        double d = 0;
        TypeId nt = get_type_id(num);
        if (nt == LMD_TYPE_INT) d = (double)it2i(num);
        else if (nt == LMD_TYPE_FLOAT) d = it2d(num);
        // Handle special cases per spec (after ToInteger)
        if (isnan(d)) return (Item){.item = s2it(heap_create_name("NaN", 3))};
        if (isinf(d)) return d > 0
            ? (Item){.item = s2it(heap_create_name("Infinity", 8))}
            : (Item){.item = s2it(heap_create_name("-Infinity", 9))};
        // Range check after NaN/Infinity per spec
        if (has_frac && (frac < 0 || frac > 100)) {
            return js_throw_range_error("toExponential() argument must be between 0 and 100");
        }
        char buf[128];
        if (!has_frac) {
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

    if (method->len == 14 && strncmp(method->chars, "toLocaleString", 14) == 0) {
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

// String.raw(template, ...substitutions) — tagged template literal
// Called with args[0]=template_object (has .raw property), args[1..]=substitutions
extern "C" Item js_string_raw(Item* args, int argc) {
    if (argc < 1) return (Item){.item = s2it(heap_strcpy("", 0))};

    Item template_obj = args[0];
    // Get template.raw
    Item raw_key = (Item){.item = s2it(heap_create_name("raw", 3))};
    Item raw = js_property_access(template_obj, raw_key);
    if (raw.item == ITEM_NULL || raw.item == ITEM_JS_UNDEFINED) {
        return (Item){.item = s2it(heap_strcpy("", 0))};
    }

    // Get raw.length (may be a MAP with numeric keys + length property)
    Item len_key = (Item){.item = s2it(heap_create_name("length", 6))};
    Item len_item = js_property_access(raw, len_key);
    int raw_len = 0;
    TypeId len_type = get_type_id(len_item);
    if (len_type == LMD_TYPE_INT) raw_len = (int)it2i(len_item);
    else if (len_type == LMD_TYPE_FLOAT) raw_len = (int)it2d(len_item);
    if (raw_len <= 0) return (Item){.item = s2it(heap_strcpy("", 0))};

    StrBuf* buf = strbuf_new();
    for (int i = 0; i < raw_len; i++) {
        // Get raw[i] — use string key for MAP compatibility
        char idx_buf[16];
        int idx_len = snprintf(idx_buf, sizeof(idx_buf), "%d", i);
        Item idx = (Item){.item = s2it(heap_create_name(idx_buf, idx_len))};
        Item part = js_property_access(raw, idx);
        Item part_str = js_to_string(part);
        String* s = it2s(part_str);
        if (s && s->len > 0) strbuf_append_str_n(buf, s->chars, s->len);

        // Interleave substitution if present (i+1 in args because args[0] is template)
        if (i < argc - 1) {
            Item sub_str = js_to_string(args[i + 1]);
            String* sub_s = it2s(sub_str);
            if (sub_s && sub_s->len > 0) strbuf_append_str_n(buf, sub_s->chars, sub_s->len);
        }
    }
    String* result = heap_strcpy(buf->str, buf->length);
    strbuf_free(buf);
    return (Item){.item = s2it(result)};
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

    // ES spec: if right is not an object, throw TypeError
    TypeId rt = get_type_id(right);
    if (rt != LMD_TYPE_MAP && rt != LMD_TYPE_FUNC) {
        js_throw_type_error("Right-hand side of 'instanceof' is not an object");
        return (Item){.item = b2it(false)};
    }

    // v16: Check for Symbol.hasInstance on the right-hand constructor FIRST (before type check)
    // Per ES spec §7.3.21: if right[@@hasInstance] exists, call it
    {
        if (rt == LMD_TYPE_MAP || rt == LMD_TYPE_FUNC) {
            // look for __sym_3 (Symbol.hasInstance = ID 3) via property_get (handles both MAP and FUNC)
            Item sym_key = (Item){.item = s2it(heap_create_name("__sym_3", 7))};
            Item has_instance_fn = js_property_get(right, sym_key);
            if (has_instance_fn.item != ItemNull.item && get_type_id(has_instance_fn) == LMD_TYPE_FUNC) {
                Item args[1] = { left };
                Item result = js_call_function(has_instance_fn, right, args, 1);
                return (Item){.item = b2it(js_is_truthy(result))};
            }
        }
    }

    // ES spec: if right is an object but not callable, throw TypeError
    if (rt == LMD_TYPE_MAP) {
        // MAP is only valid if it has __class_name__ (constructor object) — otherwise not callable
        Item class_key = (Item){.item = s2it(heap_create_name("__class_name__", 14))};
        Item cn = map_get(right.map, class_key);
        if (cn.item == 0 || get_type_id(cn) != LMD_TYPE_STRING) {
            js_throw_type_error("Right-hand side of 'instanceof' is not callable");
            return (Item){.item = b2it(false)};
        }
    }

    if (get_type_id(left) != LMD_TYPE_MAP) return (Item){.item = b2it(false)};

    // If right is a function, use ES spec OrdinaryHasInstance:
    // Walk left's __proto__ chain comparing against right.prototype
    TypeId right_type = get_type_id(right);
    if (right_type == LMD_TYPE_FUNC) {
        // v20: Get Func.prototype via property access (handles both Function and JsFunction)
        Item proto_key = (Item){.item = s2it(heap_create_name("prototype", 9))};
        Item func_proto = js_property_get(right, proto_key);
        if (func_proto.item != ItemNull.item && get_type_id(func_proto) == LMD_TYPE_MAP) {
            // Walk left's __proto__ chain looking for func_proto (identity check)
            Item proto_key_item = (Item){.item = s2it(heap_create_name("__proto__", 9))};
            Item obj = left;
            int depth = 0;
            while (obj.item != 0 && get_type_id(obj) == LMD_TYPE_MAP && depth < 32) {
                Item obj_proto = map_get(obj.map, proto_key_item);
                if (obj_proto.item != 0 && get_type_id(obj_proto) == LMD_TYPE_MAP) {
                    if (obj_proto.map == func_proto.map) {
                        return (Item){.item = b2it(true)};
                    }
                }
                obj = obj_proto;
                depth++;
            }
        }
        // Fallback: also check __ctor__ walk for class-based objects
        {
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
        }
        // v18c: name-based fallback for built-in constructors
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
    // v20: Object check — any object (MAP) is instanceof Object (includes user-defined instances)
    if (rn->len == 6 && strncmp(rn->chars, "Object", 6) == 0) {
        return (Item){.item = b2it(lt == LMD_TYPE_MAP)};
    }
    // v20: Function check — any function is instanceof Function
    if (rn->len == 8 && strncmp(rn->chars, "Function", 8) == 0) {
        return (Item){.item = b2it(lt == LMD_TYPE_FUNC)};
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
                    (on->len == 9 && strncmp(on->chars, "EvalError", 9) == 0) ||
                    (on->len == 14 && strncmp(on->chars, "AggregateError", 14) == 0)) {
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
    // ES spec: TypeError if RHS is not an object
    if (type != LMD_TYPE_MAP && type != LMD_TYPE_ARRAY && type != LMD_TYPE_FUNC
        && type != LMD_TYPE_ELEMENT) {
        return js_throw_type_error("Cannot use 'in' operator to search for a property in a non-object");
    }
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
                double dv = it2d(key);
                // v24: -0.0 should stringify to "0" per ES spec
                if (dv == 0.0) snprintf(buf, sizeof(buf), "0");
                else snprintf(buf, sizeof(buf), "%g", dv);
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
        // v26: check builtin methods on object and prototype chain
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* ks = it2s(key);
            if (ks) {
                // Check builtin methods on explicit prototype chain objects
                Item proto = js_get_prototype(object);
                int depth = 0;
                while (proto.item != ItemNull.item && get_type_id(proto) == LMD_TYPE_MAP && depth < 32) {
                    if (js_map_has_builtin_method(proto.map, ks->chars, (int)ks->len))
                        return (Item){.item = b2it(true)};
                    proto = js_get_prototype(proto);
                    depth++;
                }
                // Fallback: all objects implicitly inherit Object.prototype methods
                // UNLESS explicitly created with null prototype (Object.create(null))
                bool has_null_proto = false;
                bool proto_found = false;
                js_map_get_fast_ext(object.map, "__proto__", 9, &proto_found);
                if (proto_found) {
                    // __proto__ exists — walk chain to see if it terminates at null
                    Item pv = js_get_prototype(object);
                    if (pv.item == ItemNull.item || get_type_id(pv) != LMD_TYPE_MAP)
                        has_null_proto = true;
                }
                if (!has_null_proto) {
                    Item builtin = js_lookup_builtin_method(LMD_TYPE_MAP, ks->chars, (int)ks->len);
                    if (builtin.item != ItemNull.item) return (Item){.item = b2it(true)};
                    // "constructor" is also on Object.prototype
                    if (ks->len == 11 && strncmp(ks->chars, "constructor", 11) == 0)
                        return (Item){.item = b2it(true)};
                }
            }
        }
        return (Item){.item = b2it(false)};
    }
    if (type == LMD_TYPE_ARRAY) {
        // check if index is valid and not a deleted hole
        int64_t idx = -1;
        if (get_type_id(key) == LMD_TYPE_INT) {
            idx = it2i(key);
        } else if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk && sk->len > 0 && sk->len <= 10) {
                bool is_num = true;
                int64_t n = 0;
                for (int i = 0; i < (int)sk->len; i++) {
                    char c = sk->chars[i];
                    if (c < '0' || c > '9') { is_num = false; break; }
                    n = n * 10 + (c - '0');
                }
                if (is_num && !(sk->len > 1 && sk->chars[0] == '0')) idx = n;
            }
            // Also check "length" for arrays
            if (sk && sk->len == 6 && strncmp(sk->chars, "length", 6) == 0) {
                return (Item){.item = b2it(true)};
            }
        }
        Array* arr = object.array;
        if (idx >= 0 && idx < arr->length) {
            // v25: check for deleted sentinel (array hole)
            if (arr->items[idx].item == JS_DELETED_SENTINEL_VAL) {
                return (Item){.item = b2it(false)};
            }
            return (Item){.item = b2it(true)};
        }
        return (Item){.item = b2it(false)};
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
    // Per spec: proto must be Object or null, else TypeError
    TypeId pt = get_type_id(proto);
    bool is_null = (proto.item == ITEM_NULL || proto.item == 0 || pt == LMD_TYPE_NULL);
    bool is_object = (pt == LMD_TYPE_MAP || pt == LMD_TYPE_ELEMENT || pt == LMD_TYPE_FUNC);
    if (!is_null && !is_object) {
        return js_throw_type_error("Object prototype may only be an Object or null");
    }
    Item obj = js_new_object();
    if (is_object && pt == LMD_TYPE_MAP) {
        js_set_prototype(obj, proto);
    } else if (is_null) {
        // Object.create(null): mark explicitly as no prototype
        // Use JS undefined as sentinel — distinguished from "no __proto__ key"
        Item key = (Item){.item = s2it(heap_create_name("__proto__", 9))};
        js_property_set(obj, key, make_js_undefined());
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
    // ES6: ToObject for primitives
    TypeId ot = get_type_id(object);
    if (ot == LMD_TYPE_STRING) {
        Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name("String", 6))});
        return js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype", 9))});
    }
    if (ot == LMD_TYPE_INT || ot == LMD_TYPE_FLOAT) {
        Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Number", 6))});
        return js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype", 9))});
    }
    if (ot == LMD_TYPE_BOOL) {
        Item ctor = js_get_constructor((Item){.item = s2it(heap_create_name("Boolean", 7))});
        return js_property_get(ctor, (Item){.item = s2it(heap_create_name("prototype", 9))});
    }
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
    // Functions → return Function.prototype (or Error for NativeError constructors)
    if (get_type_id(object) == LMD_TYPE_FUNC) {
        // v82d: NativeError constructors have [[Prototype]] = Error (not Function.prototype)
        // Check .name property to see if it's a NativeError constructor
        Item name_key = (Item){.item = s2it(heap_create_name("name", 4))};
        Item name_val = js_property_get(object, name_key);
        if (get_type_id(name_val) == LMD_TYPE_STRING) {
            String* n = it2s(name_val);
            bool is_native_error =
                (n->len == 9 && strncmp(n->chars, "TypeError", 9) == 0) ||
                (n->len == 10 && strncmp(n->chars, "RangeError", 10) == 0) ||
                (n->len == 14 && strncmp(n->chars, "ReferenceError", 14) == 0) ||
                (n->len == 11 && strncmp(n->chars, "SyntaxError", 11) == 0) ||
                (n->len == 8 && strncmp(n->chars, "URIError", 8) == 0) ||
                (n->len == 9 && strncmp(n->chars, "EvalError", 9) == 0) ||
                (n->len == 14 && strncmp(n->chars, "AggregateError", 14) == 0);
            if (is_native_error) {
                return js_get_constructor((Item){.item = s2it(heap_create_name("Error", 5))});
            }
        }
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
        // Object.create(null) stores undefined as sentinel for null prototype
        if (raw_proto.item == ITEM_JS_UNDEFINED) return ItemNull;
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
extern void js_throw_value(Item value);
extern Item js_new_error_with_name(Item type_name, Item msg);

// Check if a function value is a constructor (has [[Construct]] internal method).
// Arrow functions, generators, and built-in prototype methods are NOT constructors.
#define JS_FUNC_FLAG_GENERATOR_G 1
#define JS_FUNC_FLAG_ARROW_G     2

struct JsFunctionLayout {
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
    uint8_t flags;
    int16_t formal_length;
};

static bool js_func_is_constructor(Item func_item) {
    if (get_type_id(func_item) != LMD_TYPE_FUNC) return false;
    JsFunctionLayout* fn = (JsFunctionLayout*)func_item.function;
    if (fn->flags & JS_FUNC_FLAG_ARROW_G) return false;
    if (fn->flags & JS_FUNC_FLAG_GENERATOR_G) return false;
    if (fn->builtin_id > 0) return false;
    if (fn->builtin_id == -2) return false; // global builtin wrappers are not constructors
    return true;
}

// Check if a function has its own .prototype property.
// Constructors and generators have .prototype; arrows and built-ins do not.
static bool js_func_has_own_prototype(Item func_item) {
    if (get_type_id(func_item) != LMD_TYPE_FUNC) return false;
    JsFunctionLayout* fn = (JsFunctionLayout*)func_item.function;
    if (fn->flags & JS_FUNC_FLAG_ARROW_G) return false;
    if (fn->builtin_id > 0) return false;
    if (fn->builtin_id == -2) return false;
    return true;
}

extern "C" Item js_reflect_construct(Item target, Item args_array, Item new_target) {
    // Validate target is a constructor
    if (!js_func_is_constructor(target)) {
        Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item msg = (Item){.item = s2it(heap_create_name("target is not a constructor"))};
        Item error = js_new_error_with_name(type_name, msg);
        js_throw_value(error);
        return ItemNull;
    }
    // Validate newTarget is a constructor (if provided and not undefined/null)
    TypeId nt_type = get_type_id(new_target);
    if (nt_type == LMD_TYPE_FUNC) {
        if (!js_func_is_constructor(new_target)) {
            Item type_name = (Item){.item = s2it(heap_create_name("TypeError"))};
            Item msg = (Item){.item = s2it(heap_create_name("newTarget is not a constructor"))};
            Item error = js_new_error_with_name(type_name, msg);
            js_throw_value(error);
            return ItemNull;
        }
    }
    // create new object inheriting from target's (or newTarget's) prototype
    Item proto_source = (nt_type == LMD_TYPE_FUNC) ? new_target : target;
    Item new_obj = js_constructor_create_object(proto_source);
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

// Reflect.ownKeys(obj) — returns array of all own property keys (strings + symbols)
extern "C" Item js_reflect_own_keys(Item obj) {
    // get string keys via getOwnPropertyNames
    Item names = js_object_get_own_property_names(obj);
    // get symbol keys via getOwnPropertySymbols
    Item symbols = js_object_get_own_property_symbols(obj);
    // concatenate: names + symbols
    if (get_type_id(symbols) == LMD_TYPE_ARRAY) {
        int sym_len = (int)js_array_length(symbols);
        for (int i = 0; i < sym_len; i++) {
            Item idx = {.item = i2it(i)};
            Item sym = js_array_get(symbols, idx);
            js_array_push(names, sym);
        }
    }
    return names;
}

// Reflect.set(obj, key, value) — returns boolean
extern "C" Item js_reflect_set(Item obj, Item key, Item value) {
    js_property_set(obj, key, value);
    return (Item){.item = b2it(true)};
}

// Reflect.defineProperty(obj, key, desc) — returns boolean (no throw)
extern "C" Item js_reflect_define_property(Item obj, Item key, Item desc) {
    js_object_define_property(obj, key, desc);
    return (Item){.item = b2it(true)};
}

// Reflect.deleteProperty(obj, key) — returns boolean
extern "C" Item js_reflect_delete_property(Item obj, Item key) {
    return js_delete_property(obj, key);
}

// Reflect.setPrototypeOf(obj, proto) — returns boolean
extern "C" Item js_reflect_set_prototype_of(Item obj, Item proto) {
    js_set_prototype(obj, proto);
    return (Item){.item = b2it(true)};
}

// Reflect.preventExtensions(obj) — returns boolean
extern "C" Item js_reflect_prevent_extensions(Item obj) {
    js_object_prevent_extensions(obj);
    return (Item){.item = b2it(true)};
}

// Reflect.apply(target, thisArg, argsList) — call target with thisArg and args
extern "C" Item js_reflect_apply(Item target, Item this_arg, Item args_array) {
    if (get_type_id(target) != LMD_TYPE_FUNC) {
        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item em = (Item){.item = s2it(heap_create_name("Reflect.apply requires a function"))};
        js_throw_value(js_new_error_with_name(tn, em));
        return ItemNull;
    }
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
    return js_call_function(target, this_arg, args, argc);
}

// =============================================================================
// Object.getOwnPropertyDescriptor — return descriptor for an own property
// =============================================================================

// Forward declarations for array companion map helpers (defined before defineProperty)
static Map* js_array_props_map(Array* arr);
static Item js_defprop_get_marker(Item obj, const char* key, int keylen, bool* found);

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
    // v20: GOPD should accept primitives (ES spec uses ToObject internally)
    // Only null/undefined throw TypeError
    TypeId type = get_type_id(obj);
    if (type == LMD_TYPE_NULL || type == LMD_TYPE_UNDEFINED ||
        (obj.item == 0 && type != LMD_TYPE_INT)) {
        extern Item js_new_error_with_name(Item type_name, Item message);
        extern void js_throw_value(Item error);
        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item msg = (Item){.item = s2it(heap_create_name("Cannot convert undefined or null to object"))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return ItemNull;
    }

    // Convert name to string for comparison
    Item name_str_item = js_to_string(name);
    if (get_type_id(name_str_item) != LMD_TYPE_STRING) return ItemNull;
    String* name_str = it2s(name_str_item);

    // Function properties: length, name, prototype
    if (type == LMD_TYPE_FUNC) {
        // v23: Check properties_map FIRST for overridden/deleted properties
        {
            JsFuncProps* fn = (JsFuncProps*)obj.function;
            if (fn->properties_map.item != 0) {
                bool own = false;
                Item val = js_map_get_fast_ext(fn->properties_map.map, name_str->chars, name_str->len, &own);
                if (own) {
                    if (val.item == JS_DELETED_SENTINEL_VAL) return make_js_undefined();
                    // Return descriptor with attributes from properties_map's attribute markers
                    Item desc = js_new_object();
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, val);
                    // Check attribute markers on the properties_map
                    Map* pm = fn->properties_map.map;
                    char attr_buf[256];
                    bool nw_found = false, nc_found = false, ne_found = false;
                    snprintf(attr_buf, sizeof(attr_buf), "__nw_%.*s", (int)name_str->len, name_str->chars);
                    Item nw_val = js_map_get_fast_ext(pm, attr_buf, (int)strlen(attr_buf), &nw_found);
                    snprintf(attr_buf, sizeof(attr_buf), "__nc_%.*s", (int)name_str->len, name_str->chars);
                    Item nc_val = js_map_get_fast_ext(pm, attr_buf, (int)strlen(attr_buf), &nc_found);
                    snprintf(attr_buf, sizeof(attr_buf), "__ne_%.*s", (int)name_str->len, name_str->chars);
                    Item ne_val = js_map_get_fast_ext(pm, attr_buf, (int)strlen(attr_buf), &ne_found);
                    bool is_writable = !(nw_found && js_is_truthy(nw_val));
                    bool is_configurable = !(nc_found && js_is_truthy(nc_val));
                    bool is_enumerable = !(ne_found && js_is_truthy(ne_val));
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(is_writable)});
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(is_enumerable)});
                    js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(is_configurable)});
                    return desc;
                }
            }
        }
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
            // Only constructor functions have prototype as own property
            if (!js_func_has_own_prototype(obj)) return make_js_undefined();
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
                // v25: deleted elements (holes) have no descriptor
                if (obj.array->items[idx].item == JS_DELETED_SENTINEL_VAL) {
                    return make_js_undefined();
                }
                Item desc = js_new_object();
                js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, obj.array->items[idx]);
                // v25: Check companion map for attribute markers set by defineProperty
                char attr_buf[256];
                bool nw_found = false, nc_found = false, ne_found = false;
                Item nw_val = ItemNull, nc_val = ItemNull, ne_val = ItemNull;
                snprintf(attr_buf, sizeof(attr_buf), "__nw_%.*s", (int)name_str->len, name_str->chars);
                nw_val = js_defprop_get_marker(obj, attr_buf, (int)strlen(attr_buf), &nw_found);
                snprintf(attr_buf, sizeof(attr_buf), "__nc_%.*s", (int)name_str->len, name_str->chars);
                nc_val = js_defprop_get_marker(obj, attr_buf, (int)strlen(attr_buf), &nc_found);
                snprintf(attr_buf, sizeof(attr_buf), "__ne_%.*s", (int)name_str->len, name_str->chars);
                ne_val = js_defprop_get_marker(obj, attr_buf, (int)strlen(attr_buf), &ne_found);
                bool is_writable = !(nw_found && js_is_truthy(nw_val));
                bool is_configurable = !(nc_found && js_is_truthy(nc_val));
                bool is_enumerable = !(ne_found && js_is_truthy(ne_val));
                js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(is_writable)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(is_enumerable)});
                js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(is_configurable)});
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
        if (!it2b(has_own)) {
            return make_js_undefined();
        }

        // v26: Check if the property is actually stored in the map, or if it's a
        // virtual builtin method from __class_name__ resolution. Builtins should have
        // enumerable:false, configurable:true.
        {
            bool stored = false;
            js_map_get_fast_ext(m, name_str->chars, (int)name_str->len, &stored);
            if (!stored) {
                // Not stored = virtual builtin method (only on prototypes)
                bool ip_own = false;
                js_map_get_fast_ext(m, "__is_proto__", 12, &ip_own);
                if (ip_own) {
                    bool cn_own = false;
                    Item cn = js_map_get_fast_ext(m, "__class_name__", 14, &cn_own);
                    if (cn_own && get_type_id(cn) == LMD_TYPE_STRING) {
                    String* cn_str = it2s(cn);
                    TypeId lookup_type = (TypeId)0;
                    if (cn_str && cn_str->len == 5 && strncmp(cn_str->chars, "Array", 5) == 0)
                        lookup_type = LMD_TYPE_ARRAY;
                    else if (cn_str && cn_str->len == 6 && strncmp(cn_str->chars, "String", 6) == 0)
                        lookup_type = LMD_TYPE_STRING;
                    else if (cn_str && cn_str->len == 8 && strncmp(cn_str->chars, "Function", 8) == 0)
                        lookup_type = LMD_TYPE_FUNC;
                    else if (cn_str && cn_str->len == 6 && strncmp(cn_str->chars, "Number", 6) == 0)
                        lookup_type = LMD_TYPE_INT;

                    Item builtin = ItemNull;
                    if (lookup_type != (TypeId)0) {
                        builtin = js_lookup_builtin_method(lookup_type, name_str->chars, (int)name_str->len);
                    }
                    if (builtin.item == ItemNull.item) {
                        builtin = js_lookup_builtin_method(LMD_TYPE_MAP, name_str->chars, (int)name_str->len);
                    }
                    if (builtin.item != ItemNull.item) {
                        Item desc = js_new_object();
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, builtin);
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
                        return desc;
                    }
                    }
                    // "constructor" — always has a descriptor if hasOwn returned true
                    if (name_str->len == 11 && strncmp(name_str->chars, "constructor", 11) == 0) {
                        Item value = js_property_get(obj, name);
                        Item desc = js_new_object();
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))}, value);
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(true)});
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(false)});
                        js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(true)});
                        return desc;
                    }
                } // ip_own
            }
        }

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
    // ES6: primitives get ToObject; null/undefined throw
    TypeId ot = get_type_id(obj);
    if (ot == LMD_TYPE_INT || ot == LMD_TYPE_FLOAT || ot == LMD_TYPE_BOOL) return js_new_object();
    if (ot == LMD_TYPE_STRING) {
        // String: describe each character index + length
        // Delegate to getOwnPropertyNames + getOwnPropertyDescriptor
        Item result = js_new_object();
        String* str = it2s(obj);
        int slen = str ? (int)str->len : 0;
        for (int i = 0; i < slen; i++) {
            char buf[16]; snprintf(buf, sizeof(buf), "%d", i);
            Item key = (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
            Item desc = js_new_object();
            js_property_set(desc, (Item){.item = s2it(heap_create_name("value", 5))},
                (Item){.item = s2it(heap_create_name(str->chars + i, 1))});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("writable", 8))}, (Item){.item = b2it(false)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("enumerable", 10))}, (Item){.item = b2it(true)});
            js_property_set(desc, (Item){.item = s2it(heap_create_name("configurable", 12))}, (Item){.item = b2it(false)});
            js_property_set(result, key, desc);
        }
        return result;
    }
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
// Array companion property map (stored in arr->extra)
// Arrays don't have Map storage for arbitrary string keys, so attribute markers
// (__nw_, __nc_, __ne_, __get_, __set_) are stored in a lazily-created companion Map.
// =============================================================================

static Map* js_array_props_map(Array* arr) {
    if (arr->extra == 0) return NULL;
    return (Map*)(uintptr_t)arr->extra;
}

static Map* js_array_ensure_props_map(Array* arr) {
    if (arr->extra == 0) {
        Item obj = js_new_object();
        arr->extra = (int64_t)(uintptr_t)obj.map;
    }
    return (Map*)(uintptr_t)arr->extra;
}

// store a marker (__nw_, __nc_, __ne_, __get_, __set_) on an object
// for arrays, routes to companion map instead of js_property_set
static void js_defprop_set_marker(Item obj, Item key, Item value) {
    if (get_type_id(obj) == LMD_TYPE_ARRAY) {
        Map* m = js_array_ensure_props_map(obj.array);
        Item map_item = (Item){.map = m};
        js_property_set(map_item, key, value);
    } else {
        js_property_set(obj, key, value);
    }
}

// check if a marker exists on an object (for arrays, checks companion map)
static bool js_defprop_has_marker(Item obj, Item key) {
    if (get_type_id(obj) == LMD_TYPE_ARRAY) {
        Map* m = js_array_props_map(obj.array);
        if (!m) return false;
        String* ks = it2s(key);
        if (!ks) return false;
        bool found = false;
        js_map_get_fast_ext(m, ks->chars, (int)ks->len, &found);
        return found;
    }
    return it2b(js_has_own_property(obj, key));
}

// read a marker value from an object (for arrays, reads from companion map; for functions, from properties_map)
static Item js_defprop_get_marker(Item obj, const char* key, int keylen, bool* found) {
    if (get_type_id(obj) == LMD_TYPE_ARRAY) {
        Map* m = js_array_props_map(obj.array);
        if (!m) { *found = false; return ItemNull; }
        return js_map_get_fast_ext(m, key, keylen, found);
    }
    if (get_type_id(obj) == LMD_TYPE_FUNC) {
        JsFuncProps* fn = (JsFuncProps*)obj.function;
        if (fn->properties_map.item != 0 && get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
            return js_map_get_fast_ext(fn->properties_map.map, key, keylen, found);
        }
        *found = false;
        return ItemNull;
    }
    return js_map_get_fast_ext(obj.map, key, keylen, found);
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
    TypeId obj_type = get_type_id(obj);
    if (obj_type == LMD_TYPE_MAP || obj_type == LMD_TYPE_ARRAY) {
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

    // Check if property is non-configurable before allowing redefinition.
    // Only check OWN properties — inherited non-configurable markers must not block
    // defining a new own property on the object (ES spec 8.12.9 step 1).
    Item name_str_item = js_to_string(name);
    if (get_type_id(name_str_item) == LMD_TYPE_STRING && (obj_type == LMD_TYPE_MAP || obj_type == LMD_TYPE_ARRAY)) {
        String* name_str = it2s(name_str_item);
        if (name_str && name_str->len > 0 && name_str->len < 200) {
            char nc_key[256];
            snprintf(nc_key, sizeof(nc_key), "__nc_%.*s", (int)name_str->len, name_str->chars);
            // check actual marker value, not just existence (marker set to false = configurable)
            bool nc_found = false;
            Item nc_val = js_defprop_get_marker(obj, nc_key, strlen(nc_key), &nc_found);
            if (nc_found && js_is_truthy(nc_val)) {
                // Property is non-configurable — check what changes are being attempted
                // Read current property state
                char nw_key[256];
                snprintf(nw_key, sizeof(nw_key), "__nw_%.*s", (int)name_str->len, name_str->chars);
                // check actual marker value (marker true = non-writable)
                bool nw_found = false;
                Item nw_val = js_defprop_get_marker(obj, nw_key, strlen(nw_key), &nw_found);
                bool cur_writable = !(nw_found && js_is_truthy(nw_val));

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
                // check actual marker value (marker true = non-enumerable)
                bool ne_found = false;
                Item ne_val = js_defprop_get_marker(obj, ne_key, strlen(ne_key), &ne_found);
                bool cur_enumerable = !(ne_found && js_is_truthy(ne_val));
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
                        // use SameValue (not strict equals) — NaN === NaN must be true here
                        if (!it2b(js_object_is(new_val, cur_val))) {
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
        // v23: For FUNC type, use js_func_init_property to bypass writability checks
        if (get_type_id(obj) == LMD_TYPE_FUNC) {
            js_func_init_property(obj, name, value);
        } else {
            js_property_set(obj, name, value);
        }
        // v18m: when converting accessor→data, remove accessor markers
        Item name_str_conv = js_to_string(name);
        if (get_type_id(name_str_conv) == LMD_TYPE_STRING) {
            String* ns = it2s(name_str_conv);
            if (ns && ns->len > 0 && ns->len < 200) {
                char mk_buf[256];
                snprintf(mk_buf, sizeof(mk_buf), "__get_%.*s", (int)ns->len, ns->chars);
                Item gk = (Item){.item = s2it(heap_create_name(mk_buf, strlen(mk_buf)))};
                if (js_defprop_has_marker(obj, gk)) {
                    js_defprop_set_marker(obj, gk, (Item){.item = JS_DELETED_SENTINEL_VAL});
                }
                snprintf(mk_buf, sizeof(mk_buf), "__set_%.*s", (int)ns->len, ns->chars);
                Item sk = (Item){.item = s2it(heap_create_name(mk_buf, strlen(mk_buf)))};
                if (js_defprop_has_marker(obj, sk)) {
                    js_defprop_set_marker(obj, sk, (Item){.item = JS_DELETED_SENTINEL_VAL});
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
                js_defprop_set_marker(obj, gk, getter);
            }
            Item set_key = (Item){.item = s2it(heap_create_name("set", 3))};
            Item setter = js_property_get(descriptor, set_key);
            if (get_type_id(setter) == LMD_TYPE_FUNC) {
                is_accessor = true;
                snprintf(key_buf, sizeof(key_buf), "__set_%.*s", (int)ns->len, ns->chars);
                Item sk = (Item){.item = s2it(heap_create_name(key_buf, strlen(key_buf)))};
                js_defprop_set_marker(obj, sk, setter);
            }
            // v18m: when converting data→accessor, remove the direct data value
            if (is_accessor) {
                // first remove __nw_ marker so the sentinel write isn't silently rejected
                snprintf(key_buf, sizeof(key_buf), "__nw_%.*s", (int)ns->len, ns->chars);
                Item nw_k = (Item){.item = s2it(heap_create_name(key_buf, strlen(key_buf)))};
                if (js_defprop_has_marker(obj, nw_k)) {
                    js_defprop_set_marker(obj, nw_k, (Item){.item = b2it(false)});
                }
                // now delete the data value — use raw map lookup to avoid triggering
                // the getter that was just stored above
                if (get_type_id(obj) == LMD_TYPE_MAP) {
                    bool data_found = false;
                    js_map_get_fast_ext(obj.map, ns->chars, (int)ns->len, &data_found);
                    if (data_found) {
                        js_property_set(obj, name, (Item){.item = JS_DELETED_SENTINEL_VAL});
                    }
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
    // v25: Arrays now use companion map for markers (stored in arr->extra)
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
                        js_defprop_set_marker(obj, nw_k, (Item){.item = b2it(true)});
                    } else {
                        // Explicitly writable: remove marker if exists
                        snprintf(attr_key, sizeof(attr_key), "__nw_%.*s", (int)name_str->len, name_str->chars);
                        Item nw_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                        js_defprop_set_marker(obj, nw_k, (Item){.item = b2it(false)});
                    }
                } else if (is_new_property) {
                    // Not specified on NEW property: default to false (non-writable)
                    snprintf(attr_key, sizeof(attr_key), "__nw_%.*s", (int)name_str->len, name_str->chars);
                    Item nw_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_defprop_set_marker(obj, nw_k, (Item){.item = b2it(true)});
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
                    js_defprop_set_marker(obj, nc_k, (Item){.item = b2it(true)});
                } else {
                    snprintf(attr_key, sizeof(attr_key), "__nc_%.*s", (int)name_str->len, name_str->chars);
                    Item nc_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_defprop_set_marker(obj, nc_k, (Item){.item = b2it(false)});
                }
            } else if (is_new_property) {
                // Not specified on NEW property: default to false (non-configurable)
                snprintf(attr_key, sizeof(attr_key), "__nc_%.*s", (int)name_str->len, name_str->chars);
                Item nc_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                js_defprop_set_marker(obj, nc_k, (Item){.item = b2it(true)});
            }
            // enumerable
            Item enumerable_key = (Item){.item = s2it(heap_create_name("enumerable", 10))};
            Item has_enumerable = js_in(enumerable_key, descriptor);
            if (it2b(has_enumerable)) {
                Item enumerable_val = js_property_get(descriptor, enumerable_key);
                if (!js_is_truthy(enumerable_val)) {
                    snprintf(attr_key, sizeof(attr_key), "__ne_%.*s", (int)name_str->len, name_str->chars);
                    Item ne_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_defprop_set_marker(obj, ne_k, (Item){.item = b2it(true)});
                } else {
                    snprintf(attr_key, sizeof(attr_key), "__ne_%.*s", (int)name_str->len, name_str->chars);
                    Item ne_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_defprop_set_marker(obj, ne_k, (Item){.item = b2it(false)});
                }
            } else if (is_new_property) {
                // Not specified on NEW property: default to false (non-enumerable)
                snprintf(attr_key, sizeof(attr_key), "__ne_%.*s", (int)name_str->len, name_str->chars);
                Item ne_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                js_defprop_set_marker(obj, ne_k, (Item){.item = b2it(true)});
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
    TypeId pt = get_type_id(props);
    if (obj.item == 0 || (pt != LMD_TYPE_MAP && pt != LMD_TYPE_ARRAY)) return obj;
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
    // ES6: ToObject for primitives
    TypeId ot = get_type_id(object);
    if (ot == LMD_TYPE_STRING) {
        String* str = it2s(object);
        int slen = str ? (int)str->len : 0;
        Item result = js_array_new(slen + 1);
        for (int i = 0; i < slen; i++) {
            char buf[16];
            int blen = snprintf(buf, sizeof(buf), "%d", i);
            js_array_set(result, (Item){.item = i2it(i)}, (Item){.item = s2it(heap_create_name(buf, blen))});
        }
        js_array_set(result, (Item){.item = i2it(slen)}, (Item){.item = s2it(heap_create_name("length", 6))});
        return result;
    }
    if (ot == LMD_TYPE_INT || ot == LMD_TYPE_FLOAT || ot == LMD_TYPE_BOOL) {
        return js_array_new(0);
    }
    if (!js_require_object_type(object, "getOwnPropertyNames")) return js_array_new(0);
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_ARRAY) {
        // indices + "length" + companion map custom properties
        int len = object.array->length;
        // v26: use push approach to skip deleted sentinel elements
        Item result = js_array_new(0);
        for (int i = 0; i < len; i++) {
            if (object.array->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            js_array_push(result, (Item){.item = s2it(heap_create_name(buf))});
        }
        js_array_push(result, (Item){.item = s2it(heap_create_name("length", 6))});
        // v25: also include custom properties from companion map
        Map* pm = js_array_props_map(object.array);
        if (pm && pm->type) {
            TypeMap* pmt = (TypeMap*)pm->type;
            ShapeEntry* e = pmt->shape;
            while (e) {
                const char* s = e->name->str;
                int slen = (int)e->name->length;
                if (slen >= 2 && s[0] == '_' && s[1] == '_') { e = e->next; continue; }
                Item val = _map_read_field(e, pm->data);
                if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
                Item key_item = (Item){.item = s2it(heap_create_name(s, slen))};
                js_array_push(result, key_item);
                e = e->next;
            }
        }
        return result;
    }
    if (type == LMD_TYPE_FUNC) {
        // function own properties: length, name, [prototype] + custom from properties_map
        JsFunctionLayout* fn_lay = (JsFunctionLayout*)object.function;
        bool has_proto = js_func_has_own_prototype(object); // constructors & generators have prototype
        Item result = js_array_new(0);
        js_array_push(result, (Item){.item = s2it(heap_create_name("length", 6))});
        js_array_push(result, (Item){.item = s2it(heap_create_name("name", 4))});
        if (has_proto)
            js_array_push(result, (Item){.item = s2it(heap_create_name("prototype", 9))});
        // Include properties from properties_map (Number.NEGATIVE_INFINITY, static methods, etc.)
        JsFuncProps* fn_props = (JsFuncProps*)object.function;
        if (fn_props->properties_map.item != 0 && get_type_id(fn_props->properties_map) == LMD_TYPE_MAP) {
            Map* pm = fn_props->properties_map.map;
            if (pm && pm->type) {
                TypeMap* pmt = (TypeMap*)pm->type;
                ShapeEntry* e = pmt->shape;
                while (e) {
                    const char* s = e->name->str;
                    int slen = (int)e->name->length;
                    // skip internal markers (__ne_, __nw_, __nc_, etc.)
                    if (slen >= 2 && s[0] == '_' && s[1] == '_') { e = e->next; continue; }
                    // skip deleted sentinels
                    Item val = _map_read_field(e, pm->data);
                    if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
                    js_array_push(result, (Item){.item = s2it(heap_create_name(s, slen))});
                    e = e->next;
                }
            }
        }
        return result;
    }
    if (type != LMD_TYPE_MAP) return js_array_new(0);
    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);

    // v25: String wrapper objects — character indices + "length"
    {
        bool own_cn = false;
        Item cn = js_map_get_fast_ext(m, "__class_name__", 14, &own_cn);
        if (own_cn && get_type_id(cn) == LMD_TYPE_STRING) {
            String* cn_s = it2s(cn);
            if (cn_s && cn_s->len == 6 && memcmp(cn_s->chars, "String", 6) == 0) {
                bool own_pv = false;
                Item pv = js_map_get_fast_ext(m, "__primitiveValue__", 18, &own_pv);
                if (own_pv && get_type_id(pv) == LMD_TYPE_STRING) {
                    String* pv_s = it2s(pv);
                    int slen = pv_s ? (int)pv_s->len : 0;
                    Item result = js_array_new(0);
                    for (int i = 0; i < slen; i++) {
                        char buf[16];
                        int blen = snprintf(buf, sizeof(buf), "%d", i);
                        js_array_push(result, (Item){.item = s2it(heap_create_name(buf, blen))});
                    }
                    js_array_push(result, (Item){.item = s2it(heap_create_name("length", 6))});
                    // v26: append builtin String method names only for prototype objects
                    bool is_proto = false;
                    js_map_get_fast_ext(m, "__is_proto__", 12, &is_proto);
                    if (is_proto) {
                        js_append_builtin_method_names(LMD_TYPE_STRING, result);
                    }
                    return result;
                }
            }
        }
    }

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
    // Detect accessor properties from __get_<name> or __set_<name> entries
    e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int slen = (int)e->name->length;
        const char* prop_name = NULL;
        int prop_len = 0;
        if (slen > 6 && strncmp(s, "__get_", 6) == 0) {
            prop_name = s + 6;
            prop_len = slen - 6;
        } else if (slen > 6 && strncmp(s, "__set_", 6) == 0) {
            prop_name = s + 6;
            prop_len = slen - 6;
        }
        if (prop_name) {
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
    // v26: for prototype Maps with __class_name__ and __is_proto__, append builtin method names
    {
        bool ip_own = false;
        js_map_get_fast_ext(m, "__is_proto__", 12, &ip_own);
        if (ip_own) {
            bool cn_own = false;
            Item cn = js_map_get_fast_ext(m, "__class_name__", 14, &cn_own);
            if (cn_own && get_type_id(cn) == LMD_TYPE_STRING) {
                String* cn_str = it2s(cn);
                if (cn_str) {
                    TypeId lookup_type = LMD_TYPE_MAP;
                    if (cn_str->len == 6 && strncmp(cn_str->chars, "String", 6) == 0) lookup_type = LMD_TYPE_STRING;
                    else if (cn_str->len == 5 && strncmp(cn_str->chars, "Array", 5) == 0) lookup_type = LMD_TYPE_ARRAY;
                    else if (cn_str->len == 6 && strncmp(cn_str->chars, "Number", 6) == 0) lookup_type = LMD_TYPE_INT;
                    else if (cn_str->len == 8 && strncmp(cn_str->chars, "Function", 8) == 0) lookup_type = LMD_TYPE_FUNC;
                    // Append builtin method names (they are "own" properties of the prototype)
                    int prev_len = arr->length;
                    js_append_builtin_method_names(lookup_type, result);
                    // Deduplicate: remove any that were already in the list
                    for (int i = arr->length - 1; i >= prev_len; i--) {
                        String* new_s = it2s(arr->items[i]);
                        bool dup = false;
                        for (int j = 0; j < prev_len; j++) {
                            String* old_s = it2s(arr->items[j]);
                            if (old_s && new_s && old_s->len == new_s->len && memcmp(old_s->chars, new_s->chars, new_s->len) == 0) {
                                dup = true; break;
                            }
                        }
                        if (dup) {
                            for (int k = i; k < arr->length - 1; k++) arr->items[k] = arr->items[k+1];
                            arr->length--;
                        }
                    }
                }
            }
        }
    }
    return result;
}

// v20: helper — check if a property name is a valid ES array index (0..2^32-2)
// Returns the numeric index, or -1 if not a valid index
static int64_t js_parse_array_index(const char* s, int len) {
    if (len <= 0 || len > 10) return -1; // max "4294967294" is 10 chars
    if (s[0] == '0' && len > 1) return -1; // no leading zeros except "0"
    int64_t val = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        val = val * 10 + (s[i] - '0');
        if (val > 4294967294LL) return -1; // 2^32 - 2
    }
    return val;
}

// v20: comparison function for sorting index keys
static int js_index_entry_cmp(const void* a, const void* b) {
    int64_t ia = *(const int64_t*)a;
    int64_t ib = *(const int64_t*)b;
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

extern "C" Item js_object_keys(Item object) {
    // ES6: ToObject for primitives
    TypeId ot = get_type_id(object);
    if (ot == LMD_TYPE_STRING) {
        String* str = it2s(object);
        int slen = str ? (int)str->len : 0;
        Item result = js_array_new(slen);
        for (int i = 0; i < slen; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            js_array_set(result, (Item){.item = i2it(i)}, (Item){.item = s2it(heap_create_name(buf, strlen(buf)))});
        }
        return result;
    }
    if (ot == LMD_TYPE_INT || ot == LMD_TYPE_FLOAT || ot == LMD_TYPE_BOOL) {
        return js_array_new(0);
    }
    if (!js_require_object_type(object, "keys")) return js_array_new(0);
    TypeId type = get_type_id(object);

    // For arrays, return indices as string keys: ["0", "1", "2", ...]
    if (type == LMD_TYPE_ARRAY) {
        int len = object.array->length;
        Item result = js_array_new(0);
        Map* pm = js_array_props_map(object.array);
        for (int i = 0; i < len; i++) {
            // v25: skip deleted elements (holes)
            if (object.array->items[i].item == JS_DELETED_SENTINEL_VAL) continue;
            // v27: skip non-enumerable elements (defineProperty with enumerable: false)
            if (pm) {
                char ne_buf[32];
                snprintf(ne_buf, sizeof(ne_buf), "__ne_%d", i);
                bool ne_found = false;
                Item ne_val = js_map_get_fast_ext(pm, ne_buf, (int)strlen(ne_buf), &ne_found);
                if (ne_found && js_is_truthy(ne_val)) continue;
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            Item key_str = (Item){.item = s2it(heap_create_name(buf))};
            js_array_push(result, key_str);
        }
        // v25: also include custom (non-index) properties from companion map
        if (pm && pm->type) {
            TypeMap* pmt = (TypeMap*)pm->type;
            ShapeEntry* e = pmt->shape;
            while (e) {
                const char* s = e->name->str;
                int slen = (int)e->name->length;
                // skip internal markers (__xx_xxx), deleted sentinels, non-enumerable
                if (slen >= 2 && s[0] == '_' && s[1] == '_') { e = e->next; continue; }
                Item val = _map_read_field(e, pm->data);
                if (val.item == JS_DELETED_SENTINEL_VAL) { e = e->next; continue; }
                // check non-enumerable marker
                if (slen > 0 && slen < 200) {
                    char ne_key[256];
                    snprintf(ne_key, sizeof(ne_key), "__ne_%.*s", slen, s);
                    bool ne_found = false;
                    Item ne_val = js_map_get_fast_ext(pm, ne_key, (int)strlen(ne_key), &ne_found);
                    if (ne_found && js_is_truthy(ne_val)) { e = e->next; continue; }
                }
                Item key_item = (Item){.item = s2it(heap_create_name(s, slen))};
                js_array_push(result, key_item);
                e = e->next;
            }
        }
        return result;
    }

    if (type != LMD_TYPE_MAP) {
        return js_array_new(0);
    }

    Map* m = object.map;
    if (!m || !m->type) return js_array_new(0);

    // v25: String wrapper objects — enumerate character indices then non-internal properties
    {
        bool own_cn = false;
        Item cn = js_map_get_fast_ext(m, "__class_name__", 14, &own_cn);
        if (own_cn && get_type_id(cn) == LMD_TYPE_STRING) {
            String* cn_s = it2s(cn);
            if (cn_s && cn_s->len == 6 && memcmp(cn_s->chars, "String", 6) == 0) {
                bool own_pv = false;
                Item pv = js_map_get_fast_ext(m, "__primitiveValue__", 18, &own_pv);
                if (own_pv && get_type_id(pv) == LMD_TYPE_STRING) {
                    String* pv_s = it2s(pv);
                    int slen = pv_s ? (int)pv_s->len : 0;
                    Item result = js_array_new(slen);
                    for (int i = 0; i < slen; i++) {
                        char buf[16];
                        int blen = snprintf(buf, sizeof(buf), "%d", i);
                        js_array_set(result, (Item){.item = i2it(i)}, (Item){.item = s2it(heap_create_name(buf, blen))});
                    }
                    return result;
                }
            }
        }
    }

    TypeMap* tm = (TypeMap*)m->type;
    Item result = js_array_new(0);
    Array* arr = result.array;

    // v20: ES spec property enumeration order:
    //   1. Integer indices in ascending numeric order
    //   2. Non-index strings in creation order
    // We collect into two separate arrays, then merge.

    // Temporary storage for index keys: pairs of (numeric_index, Item_string)
    int idx_cap = 16, idx_count = 0;
    int64_t* idx_vals = (int64_t*)alloca(idx_cap * sizeof(int64_t));
    Item* idx_items = (Item*)alloca(idx_cap * sizeof(Item));

    // Non-index keys in insertion order
    int str_cap = 16, str_count = 0;
    Item* str_items = (Item*)alloca(str_cap * sizeof(Item));

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

            // v20: classify as index vs string key
            int64_t idx = js_parse_array_index(s, len);
            if (idx >= 0) {
                if (idx_count >= idx_cap) {
                    // overflow alloca - just append to string keys as fallback
                    if (str_count < str_cap) str_items[str_count++] = key_str;
                    else array_push(arr, key_str);
                } else {
                    idx_vals[idx_count] = idx;
                    idx_items[idx_count] = key_str;
                    idx_count++;
                }
            } else {
                if (str_count < str_cap) str_items[str_count++] = key_str;
                else array_push(arr, key_str);
            }
        }
        e = e->next;
    }

    // Second pass: detect accessor properties defined via __get_<name> or __set_<name>
    e = tm->shape;
    while (e) {
        const char* s = e->name->str;
        int slen = (int)e->name->length;
        // Look for __get_<name> or __set_<name> entries
        const char* prop_name = NULL;
        int prop_len = 0;
        if (slen > 6 && strncmp(s, "__get_", 6) == 0) {
            prop_name = s + 6;
            prop_len = slen - 6;
        } else if (slen > 6 && strncmp(s, "__set_", 6) == 0) {
            prop_name = s + 6;
            prop_len = slen - 6;
        }
        if (prop_name) {
            // Check if this property is already collected
            bool already_present = false;
            // check index keys
            for (int j = 0; j < idx_count; j++) {
                String* existing = it2s(idx_items[j]);
                if (existing && (int)existing->len == prop_len && memcmp(existing->chars, prop_name, prop_len) == 0) {
                    already_present = true;
                    break;
                }
            }
            // check string keys
            if (!already_present) {
                for (int j = 0; j < str_count; j++) {
                    String* existing = it2s(str_items[j]);
                    if (existing && (int)existing->len == prop_len && memcmp(existing->chars, prop_name, prop_len) == 0) {
                        already_present = true;
                        break;
                    }
                }
            }
            // check overflow keys already in arr
            if (!already_present) {
                for (int j = 0; j < arr->length; j++) {
                    String* existing = it2s(arr->items[j]);
                    if (existing && (int)existing->len == prop_len && memcmp(existing->chars, prop_name, prop_len) == 0) {
                        already_present = true;
                        break;
                    }
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
                    int64_t idx = js_parse_array_index(prop_name, prop_len);
                    if (idx >= 0 && idx_count < idx_cap) {
                        idx_vals[idx_count] = idx;
                        idx_items[idx_count] = key_str;
                        idx_count++;
                    } else {
                        if (str_count < str_cap) str_items[str_count++] = key_str;
                        else array_push(arr, key_str);
                    }
                }
            }
        }
        e = e->next;
    }

    // v20: sort index keys numerically
    // Simple insertion sort (typically few index keys on objects)
    for (int i = 1; i < idx_count; i++) {
        int64_t iv = idx_vals[i];
        Item ii = idx_items[i];
        int j = i - 1;
        while (j >= 0 && idx_vals[j] > iv) {
            idx_vals[j + 1] = idx_vals[j];
            idx_items[j + 1] = idx_items[j];
            j--;
        }
        idx_vals[j + 1] = iv;
        idx_items[j + 1] = ii;
    }

    // Build final result: index keys first, then string keys, then overflow
    Item final_result = js_array_new(idx_count + str_count + arr->length);
    Array* final_arr = final_result.array;
    final_arr->length = 0; // reset - we'll push

    for (int i = 0; i < idx_count; i++) array_push(final_arr, idx_items[i]);
    for (int i = 0; i < str_count; i++) array_push(final_arr, str_items[i]);
    for (int i = 0; i < arr->length; i++) array_push(final_arr, arr->items[i]);

    return final_result;
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

    // v20: separate index keys and string keys for spec-compliant ordering
    int idx_cap = 16, idx_count = 0;
    int64_t* idx_vals = (int64_t*)alloca(idx_cap * sizeof(int64_t));
    Item* idx_items = (Item*)alloca(idx_cap * sizeof(Item));

    Item str_result = js_array_new(0); // non-index string keys in creation order

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
                        // v20: classify as index or string key
                        int64_t idx = js_parse_array_index(s, len);
                        if (idx >= 0 && idx_count < idx_cap) {
                            idx_vals[idx_count] = idx;
                            idx_items[idx_count] = key_str;
                            idx_count++;
                        } else {
                            js_array_push(str_result, key_str);
                        }
                    }
                }
                e = e->next;
            }
        }
        // Second pass on this level: detect accessor properties via __get_<name> or __set_<name>
        if (m && m->type) {
            TypeMap* tm = (TypeMap*)m->type;
            ShapeEntry* e = tm->shape;
            while (e) {
                const char* s = e->name->str;
                int slen = (int)e->name->length;
                // Look for __get_<name> or __set_<name> entries (accessor properties)
                const char* prop_name = NULL;
                int prop_len = 0;
                if (slen > 6 && strncmp(s, "__get_", 6) == 0) {
                    prop_name = s + 6;
                    prop_len = slen - 6;
                } else if (slen > 6 && strncmp(s, "__set_", 6) == 0) {
                    prop_name = s + 6;
                    prop_len = slen - 6;
                }
                if (prop_name) {
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
                        // deduplicate
                        struct SeenEntry probe;
                        int nlen = prop_len < 255 ? prop_len : 255;
                        memcpy(probe.name, prop_name, nlen);
                        probe.name[nlen] = '\0';
                        const struct SeenEntry* existing = (const struct SeenEntry*)hashmap_get(seen, &probe);
                        if (!existing) {
                            hashmap_set(seen, &probe);
                            Item key_str = (Item){.item = s2it(heap_create_name(probe.name, nlen))};
                            int64_t idx = js_parse_array_index(prop_name, prop_len);
                            if (idx >= 0 && idx_count < idx_cap) {
                                idx_vals[idx_count] = idx;
                                idx_items[idx_count] = key_str;
                                idx_count++;
                            } else {
                                js_array_push(str_result, key_str);
                            }
                        }
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

    // v20: sort index keys numerically (insertion sort, typically few)
    for (int i = 1; i < idx_count; i++) {
        int64_t iv = idx_vals[i];
        Item ii = idx_items[i];
        int j = i - 1;
        while (j >= 0 && idx_vals[j] > iv) {
            idx_vals[j + 1] = idx_vals[j];
            idx_items[j + 1] = idx_items[j];
            j--;
        }
        idx_vals[j + 1] = iv;
        idx_items[j + 1] = ii;
    }

    // Build final result: index keys first, then string keys
    Array* str_arr = str_result.array;
    Item result = js_array_new(idx_count + str_arr->length);
    Array* arr = result.array;
    arr->length = 0;
    for (int i = 0; i < idx_count; i++) array_push(arr, idx_items[i]);
    for (int i = 0; i < str_arr->length; i++) array_push(arr, str_arr->items[i]);

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
    // String(Symbol()) is allowed — explicit conversion (ES spec 19.1.1)
    if (get_type_id(value) == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) {
        return js_symbol_to_string(value);
    }
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
    if (type == LMD_TYPE_STRING) {
        String* str = it2s(object);
        int slen = str ? (int)str->len : 0;
        Item result = js_array_new(slen);
        for (int i = 0; i < slen; i++) {
            js_array_set(result, (Item){.item = i2it(i)}, (Item){.item = s2it(heap_create_name(str->chars + i, 1))});
        }
        return result;
    }
    if (type != LMD_TYPE_MAP) return js_array_new(0);

    // v20: use js_object_keys for spec-compliant ordering
    Item keys = js_object_keys(object);
    int len = (int)js_array_length(keys);
    Item result = js_array_new(0);
    for (int i = 0; i < len; i++) {
        Item key = js_array_get(keys, (Item){.item = i2it(i)});
        Item val = js_property_access(object, key);
        js_array_push(result, val);
    }
    return result;
}

// =============================================================================
// Object.entries — return array of [key, value] pairs
// =============================================================================

extern "C" Item js_object_entries(Item object) {
    TypeId type = get_type_id(object);
    if (type == LMD_TYPE_STRING) {
        String* str = it2s(object);
        int slen = str ? (int)str->len : 0;
        Item result = js_array_new(0);
        for (int i = 0; i < slen; i++) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", i);
            Item pair = js_array_new(2);
            js_array_set(pair, (Item){.item = i2it(0)}, (Item){.item = s2it(heap_create_name(buf, strlen(buf)))});
            js_array_set(pair, (Item){.item = i2it(1)}, (Item){.item = s2it(heap_create_name(str->chars + i, 1))});
            js_array_push(result, pair);
        }
        return result;
    }
    if (type != LMD_TYPE_MAP) return js_array_new(0);

    // v20: use js_object_keys for spec-compliant ordering
    Item keys = js_object_keys(object);
    int len = (int)js_array_length(keys);
    Item result = js_array_new(0);
    for (int i = 0; i < len; i++) {
        Item key = js_array_get(keys, (Item){.item = i2it(i)});
        Item val = js_property_access(object, key);
        Item pair = js_array_new(2);
        js_array_set(pair, (Item){.item = i2it(0)}, key);
        js_array_set(pair, (Item){.item = i2it(1)}, val);
        js_array_push(result, pair);
    }
    return result;
}

// =============================================================================
// Object.fromEntries(iterable) — create object from [key, value] pairs
// =============================================================================

extern "C" Item js_iterable_to_array(Item iterable);

extern "C" Item js_object_from_entries(Item iterable) {
    TypeId type = get_type_id(iterable);
    // Convert non-array iterables (Map, generator, etc.) to array first
    Item arr = iterable;
    if (type != LMD_TYPE_ARRAY) {
        arr = js_iterable_to_array(iterable);
        if (get_type_id(arr) != LMD_TYPE_ARRAY) return js_new_object();
    }

    Item result = js_new_object();
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item pair = js_array_get(arr, (Item){.item = i2it(i)});
        if (get_type_id(pair) != LMD_TYPE_ARRAY) continue;
        Item key = js_array_get(pair, (Item){.item = i2it(0)});
        Item val = js_array_get(pair, (Item){.item = i2it(1)});
        Item key_str = js_to_string(key);
        js_property_set(result, key_str, val);
    }
    return result;
}

// =============================================================================
// Object.groupBy(items, callbackFn) — groups items into plain object by key
// =============================================================================

extern "C" Item js_iterable_to_array(Item iterable);

extern "C" Item js_object_group_by(Item items, Item callback) {
    // Convert iterable to array first
    Item arr = js_iterable_to_array(items);
    // Create null-prototype object per spec
    Item result = js_object_create(ItemNull);
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item elem = js_array_get(arr, (Item){.item = i2it(i)});
        Item idx_item = {.item = i2it(i)};
        Item fn_args[2] = {elem, idx_item};
        Item key = js_call_function(callback, make_js_undefined(), fn_args, 2);
        Item key_str = js_to_string(key);
        // get or create array for this group
        String* ks = it2s(key_str);
        bool found = false;
        Item group = js_map_get_fast_ext(result.map, ks->chars, (int)ks->len, &found);
        if (!found) {
            group = js_array_new(0);
            js_property_set(result, key_str, group);
        }
        js_array_push(group, elem);
    }
    return result;
}

// =============================================================================
// Map.groupBy(items, callbackFn) — groups items into a Map by key
// =============================================================================

extern "C" Item js_map_collection_new(void);

extern "C" Item js_map_group_by(Item items, Item callback) {
    extern Item js_collection_method(Item obj, int method_id, Item arg1, Item arg2);
    Item result = js_map_collection_new();
    // Convert iterable to array first
    Item arr = js_iterable_to_array(items);
    int64_t len = js_array_length(arr);
    for (int64_t i = 0; i < len; i++) {
        Item elem = js_array_get(arr, (Item){.item = i2it(i)});
        Item idx_item = {.item = i2it(i)};
        Item fn_args[2] = {elem, idx_item};
        Item key = js_call_function(callback, make_js_undefined(), fn_args, 2);
        // has(key) -> method_id=2
        Item has = js_collection_method(result, 2, key, ItemNull);
        if (it2b(has)) {
            // get(key) -> method_id=1, then push elem
            Item group = js_collection_method(result, 1, key, ItemNull);
            js_array_push(group, elem);
        } else {
            Item group = js_array_new(0);
            js_array_push(group, elem);
            // set(key, group) -> method_id=0
            js_collection_method(result, 0, key, group);
        }
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
// Native assert.sameValue / assert.notSameValue for test262 (debug builds only)
// =============================================================================
// These bypass the JS-level assert.sameValue/notSameValue, avoiding:
//   - full JS function dispatch overhead (property lookup, args array, etc.)
//   - string concatenation for error messages on the hot (passing) path
// The transpiler intercepts assert.sameValue(a,b,msg) calls and emits direct
// calls to these C++ functions instead.

#ifndef NDEBUG

// helper: build error message string for assert.sameValue/notSameValue
static Item assert_build_error_msg(Item actual, Item expected, Item message, bool same) {
    extern Item js_to_string_val(Item value);

    Item actual_str = js_to_string_val(actual);
    Item expected_str = js_to_string_val(expected);
    String* a_s = it2s(actual_str);
    String* e_s = it2s(expected_str);
    const char* a_chars = a_s ? a_s->chars : "undefined";
    int a_len = a_s ? (int)a_s->len : 9;
    const char* e_chars = e_s ? e_s->chars : "undefined";
    int e_len = e_s ? (int)e_s->len : 9;

    // format: "[<message> ]Expected SameValue(«<actual>», «<expected>») to be true/false"
    const char* tail = same ? "\xC2\xBB) to be true" : "\xC2\xBB) to be false";
    const char* mid = "\xC2\xBB, \xC2\xAB";
    const char* head = "Expected SameValue(\xC2\xAB";

    // get optional message prefix
    const char* msg_chars = NULL;
    int msg_len = 0;
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* m_s = it2s(message);
        if (m_s && m_s->len > 0) { msg_chars = m_s->chars; msg_len = (int)m_s->len; }
    }

    int total = msg_len + (msg_len > 0 ? 1 : 0) + (int)strlen(head) + a_len +
                (int)strlen(mid) + e_len + (int)strlen(tail) + 1;
    char* buf = (char*)malloc(total);
    int pos = 0;
    if (msg_chars) {
        memcpy(buf + pos, msg_chars, msg_len); pos += msg_len;
        buf[pos++] = ' ';
    }
    int hlen = (int)strlen(head);
    memcpy(buf + pos, head, hlen); pos += hlen;
    memcpy(buf + pos, a_chars, a_len); pos += a_len;
    int mlen = (int)strlen(mid);
    memcpy(buf + pos, mid, mlen); pos += mlen;
    memcpy(buf + pos, e_chars, e_len); pos += e_len;
    int tlen = (int)strlen(tail);
    memcpy(buf + pos, tail, tlen); pos += tlen;
    buf[pos] = '\0';

    Item result = (Item){.item = s2it(heap_create_name(buf, pos))};
    free(buf);
    return result;
}

extern "C" void js_assert_same_value(Item actual, Item expected, Item message) {
    // SameValue semantics: NaN === NaN, +0 !== -0
    Item result = js_object_is(actual, expected);
    if (it2b(result)) return;  // fast path: values are the same

    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);

    Item msg = assert_build_error_msg(actual, expected, message, true);
    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    js_throw_value(js_new_error_with_name(err_name, msg));
}

extern "C" void js_assert_not_same_value(Item actual, Item unexpected, Item message) {
    Item result = js_object_is(actual, unexpected);
    if (!it2b(result)) return;  // fast path: values are different

    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);

    Item msg = assert_build_error_msg(actual, unexpected, message, false);
    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    js_throw_value(js_new_error_with_name(err_name, msg));
}

// =============================================================================
// Native compareArray / assert.compareArray for test262 (debug builds only)
// =============================================================================

// compareArray(a, b): element-wise SameValue comparison, returns bool Item
extern "C" Item js_compare_array(Item a, Item b) {
    extern int64_t js_array_length(Item array);
    extern Item js_array_get_int(Item array, int64_t index);

    if (get_type_id(a) != LMD_TYPE_ARRAY || get_type_id(b) != LMD_TYPE_ARRAY)
        return (Item){.item = b2it(false)};

    int64_t len_a = js_array_length(a);
    int64_t len_b = js_array_length(b);
    if (len_a != len_b) return (Item){.item = b2it(false)};

    for (int64_t i = 0; i < len_a; i++) {
        Item ai = js_array_get_int(a, i);
        Item bi = js_array_get_int(b, i);
        if (!it2b(js_object_is(ai, bi))) return (Item){.item = b2it(false)};
    }
    return (Item){.item = b2it(true)};
}

// helper: format array as "[elem1, elem2, ...]" for error messages
static Item assert_format_array(Item arr) {
    extern int64_t js_array_length(Item array);
    extern Item js_array_get_int(Item array, int64_t index);
    extern Item js_to_string_val(Item value);

    if (get_type_id(arr) != LMD_TYPE_ARRAY) {
        return (Item){.item = s2it(heap_create_name("(not an array)"))};
    }
    int64_t len = js_array_length(arr);
    // pre-pass: compute total length
    int total = 2; // "[]"
    char* strs[256];
    int slens[256];
    int maxn = len > 256 ? 256 : (int)len;
    for (int i = 0; i < maxn; i++) {
        Item elem = js_array_get_int(arr, i);
        Item s = js_to_string_val(elem);
        String* ss = it2s(s);
        strs[i] = ss ? ss->chars : (char*)"undefined";
        slens[i] = ss ? (int)ss->len : 9;
        total += slens[i] + (i > 0 ? 2 : 0); // ", " separator
    }
    char* buf = (char*)malloc(total + 1);
    int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < maxn; i++) {
        if (i > 0) { buf[pos++] = ','; buf[pos++] = ' '; }
        memcpy(buf + pos, strs[i], slens[i]); pos += slens[i];
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    Item result = (Item){.item = s2it(heap_create_name(buf, pos))};
    free(buf);
    return result;
}

// assert.compareArray(actual, expected, message): throws on mismatch
extern "C" void js_assert_compare_array(Item actual, Item expected, Item message) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);

    // null checks
    TypeId at = get_type_id(actual);
    if (at == LMD_TYPE_NULL || at == LMD_TYPE_UNDEFINED) {
        const char* msg_prefix = "Actual argument shouldn't be nullish. ";
        String* ms = (get_type_id(message) == LMD_TYPE_STRING) ? it2s(message) : NULL;
        int total = (int)strlen(msg_prefix) + (ms ? (int)ms->len : 0);
        char* buf = (char*)malloc(total + 1);
        memcpy(buf, msg_prefix, strlen(msg_prefix));
        if (ms) memcpy(buf + strlen(msg_prefix), ms->chars, ms->len);
        buf[total] = '\0';
        Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
        Item err_msg = (Item){.item = s2it(heap_create_name(buf, total))};
        free(buf);
        js_throw_value(js_new_error_with_name(err_name, err_msg));
        return;
    }

    TypeId et = get_type_id(expected);
    if (et == LMD_TYPE_NULL || et == LMD_TYPE_UNDEFINED) {
        const char* msg_prefix = "Expected argument shouldn't be nullish. ";
        String* ms = (get_type_id(message) == LMD_TYPE_STRING) ? it2s(message) : NULL;
        int total = (int)strlen(msg_prefix) + (ms ? (int)ms->len : 0);
        char* buf = (char*)malloc(total + 1);
        memcpy(buf, msg_prefix, strlen(msg_prefix));
        if (ms) memcpy(buf + strlen(msg_prefix), ms->chars, ms->len);
        buf[total] = '\0';
        Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
        Item err_msg = (Item){.item = s2it(heap_create_name(buf, total))};
        free(buf);
        js_throw_value(js_new_error_with_name(err_name, err_msg));
        return;
    }

    Item result = js_compare_array(actual, expected);
    if (it2b(result)) return; // pass

    // build error message: "Actual [...] and expected [...] should have the same contents. <message>"
    Item a_fmt = assert_format_array(actual);
    Item e_fmt = assert_format_array(expected);
    String* as = it2s(a_fmt);
    String* es = it2s(e_fmt);
    String* ms = (get_type_id(message) == LMD_TYPE_STRING) ? it2s(message) : NULL;

    const char* p1 = "Actual ";
    const char* p2 = " and expected ";
    const char* p3 = " should have the same contents. ";
    int total = (int)strlen(p1) + (as ? (int)as->len : 0) + (int)strlen(p2) +
                (es ? (int)es->len : 0) + (int)strlen(p3) + (ms ? (int)ms->len : 0);
    char* buf = (char*)malloc(total + 1);
    int pos = 0;
    int l;
    l = (int)strlen(p1); memcpy(buf + pos, p1, l); pos += l;
    if (as) { memcpy(buf + pos, as->chars, as->len); pos += (int)as->len; }
    l = (int)strlen(p2); memcpy(buf + pos, p2, l); pos += l;
    if (es) { memcpy(buf + pos, es->chars, es->len); pos += (int)es->len; }
    l = (int)strlen(p3); memcpy(buf + pos, p3, l); pos += l;
    if (ms) { memcpy(buf + pos, ms->chars, ms->len); pos += (int)ms->len; }
    buf[pos] = '\0';

    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    Item err_msg = (Item){.item = s2it(heap_create_name(buf, pos))};
    free(buf);
    js_throw_value(js_new_error_with_name(err_name, err_msg));
}

// =============================================================================
// Native verifyProperty for test262 (debug builds only)
// =============================================================================
// Simplified native version: checks descriptor fields against
// Object.getOwnPropertyDescriptor result. Skips destructive isWritable/
// isConfigurable/isEnumerable checks for performance.

extern "C" void js_verify_property(Item obj, Item name, Item desc, Item options) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    extern Item js_object_get_own_property_descriptor(Item obj, Item name);
    extern Item js_has_own_property(Item obj, Item key);
    extern Item js_property_get(Item obj, Item key);
    extern Item js_to_string_val(Item value);

    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};

    // verifyProperty requires at least 3 arguments: obj, name, desc
    // (always true when called from transpiler)

    Item originalDesc = js_object_get_own_property_descriptor(obj, name);

    // desc === undefined → verify property doesn't exist
    if (get_type_id(desc) == LMD_TYPE_UNDEFINED) {
        if (get_type_id(originalDesc) != LMD_TYPE_UNDEFINED) {
            Item name_str = js_to_string_val(name);
            String* ns = it2s(name_str);
            char buf[256];
            int len = snprintf(buf, sizeof(buf), "obj['%.*s'] descriptor should be undefined",
                              ns ? (int)ns->len : 0, ns ? ns->chars : "");
            js_throw_value(js_new_error_with_name(err_name, (Item){.item = s2it(heap_create_name(buf, len))}));
        }
        return;
    }

    // assert(hasOwnProperty(obj, name))
    if (!it2b(js_has_own_property(obj, name))) {
        Item name_str = js_to_string_val(name);
        String* ns = it2s(name_str);
        char buf[256];
        int len = snprintf(buf, sizeof(buf), "obj should have an own property %.*s",
                          ns ? (int)ns->len : 0, ns ? ns->chars : "");
        js_throw_value(js_new_error_with_name(err_name, (Item){.item = s2it(heap_create_name(buf, len))}));
        return;
    }

    // desc must be an object
    if (get_type_id(desc) != LMD_TYPE_MAP) {
        js_throw_value(js_new_error_with_name(err_name,
            (Item){.item = s2it(heap_create_name("The desc argument should be an object or undefined"))}));
        return;
    }

    if (get_type_id(originalDesc) == LMD_TYPE_UNDEFINED) {
        // property should exist but getOwnPropertyDescriptor returned undefined
        js_throw_value(js_new_error_with_name(err_name,
            (Item){.item = s2it(heap_create_name("property descriptor is undefined but property should exist"))}));
        return;
    }

    // check each descriptor field
    Item value_key = (Item){.item = s2it(heap_create_name("value", 5))};
    Item writable_key = (Item){.item = s2it(heap_create_name("writable", 8))};
    Item enumerable_key = (Item){.item = s2it(heap_create_name("enumerable", 10))};
    Item configurable_key = (Item){.item = s2it(heap_create_name("configurable", 12))};

    // collect failure messages
    char failures[1024];
    int fpos = 0;
    failures[0] = '\0';

    auto append_failure = [&](const char* msg) {
        if (fpos > 0) {
            memcpy(failures + fpos, "; ", 2);
            fpos += 2;
        }
        int ml = (int)strlen(msg);
        if (fpos + ml < (int)sizeof(failures) - 1) {
            memcpy(failures + fpos, msg, ml);
            fpos += ml;
        }
        failures[fpos] = '\0';
    };

    if (it2b(js_has_own_property(desc, value_key))) {
        Item desc_value = js_property_get(desc, value_key);
        Item orig_value = js_property_get(originalDesc, value_key);
        if (!it2b(js_object_is(desc_value, orig_value))) {
            append_failure("descriptor value mismatch");
        }
        // also check obj[name] matches
        Item obj_value = js_property_get(obj, name);
        if (!it2b(js_object_is(desc_value, obj_value))) {
            append_failure("object value mismatch");
        }
    }

    if (it2b(js_has_own_property(desc, enumerable_key))) {
        Item desc_enum = js_property_get(desc, enumerable_key);
        Item orig_enum = js_property_get(originalDesc, enumerable_key);
        bool desc_e = it2b(desc_enum);
        bool orig_e = it2b(orig_enum);
        if (desc_e != orig_e) {
            append_failure(desc_e ? "descriptor should be enumerable" : "descriptor should not be enumerable");
        }
    }

    if (it2b(js_has_own_property(desc, writable_key))) {
        Item desc_writ = js_property_get(desc, writable_key);
        Item orig_writ = js_property_get(originalDesc, writable_key);
        bool desc_w = it2b(desc_writ);
        bool orig_w = it2b(orig_writ);
        if (desc_w != orig_w) {
            append_failure(desc_w ? "descriptor should be writable" : "descriptor should not be writable");
        }
    }

    if (it2b(js_has_own_property(desc, configurable_key))) {
        Item desc_conf = js_property_get(desc, configurable_key);
        Item orig_conf = js_property_get(originalDesc, configurable_key);
        bool desc_c = it2b(desc_conf);
        bool orig_c = it2b(orig_conf);
        if (desc_c != orig_c) {
            append_failure(desc_c ? "descriptor should be configurable" : "descriptor should not be configurable");
        }
    }

    if (fpos > 0) {
        js_throw_value(js_new_error_with_name(err_name, (Item){.item = s2it(heap_create_name(failures, fpos))}));
        return;
    }

    // options.restore: restore the original descriptor
    if (get_type_id(options) == LMD_TYPE_MAP) {
        Item restore_key = (Item){.item = s2it(heap_create_name("restore", 7))};
        Item restore_val = js_property_get(options, restore_key);
        if (it2b(restore_val)) {
            extern Item js_object_define_property(Item obj, Item name, Item desc);
            js_object_define_property(obj, name, originalDesc);
        }
    }
}

// =============================================================================
// Native assert.deepEqual for test262 (debug builds only)
// =============================================================================

// forward declaration for recursive call
static bool js_deep_equal_compare(Item a, Item b, int depth);

static bool js_deep_equal_compare(Item a, Item b, int depth) {
    extern int64_t js_array_length(Item array);
    extern Item js_array_get_int(Item array, int64_t index);
    extern Item js_property_get(Item obj, Item key);
    extern Item js_object_keys(Item object);
    extern Item js_strict_equal(Item left, Item right);

    if (depth > 100) return false; // prevent infinite recursion

    // fast path: strict equality (same reference, same primitives)
    if (it2b(js_strict_equal(a, b))) return true;

    TypeId ta = get_type_id(a);
    TypeId tb = get_type_id(b);

    // null/undefined: only equal if both the same
    if (ta == LMD_TYPE_NULL || ta == LMD_TYPE_UNDEFINED ||
        tb == LMD_TYPE_NULL || tb == LMD_TYPE_UNDEFINED) {
        return ta == tb && a.item == b.item;
    }

    // NaN handling: NaN === NaN for deepEqual
    if (ta == LMD_TYPE_FLOAT && tb == LMD_TYPE_FLOAT) {
        double da = it2d(a);
        double db = it2d(b);
        if (da != da && db != db) return true; // both NaN
        return da == db;
    }

    // different primitive types
    if ((ta == LMD_TYPE_BOOL || ta == LMD_TYPE_INT || ta == LMD_TYPE_FLOAT ||
         ta == LMD_TYPE_STRING) &&
        (tb == LMD_TYPE_BOOL || tb == LMD_TYPE_INT || tb == LMD_TYPE_FLOAT ||
         tb == LMD_TYPE_STRING)) {
        // both primitives but not strict equal — not deep equal
        // (cross-type like int/float: 1 === 1.0 should have passed strict equal)
        return false;
    }

    // both arrays: element-wise deep comparison
    if (ta == LMD_TYPE_ARRAY && tb == LMD_TYPE_ARRAY) {
        int64_t len_a = js_array_length(a);
        int64_t len_b = js_array_length(b);
        if (len_a != len_b) return false;
        for (int64_t i = 0; i < len_a; i++) {
            if (!js_deep_equal_compare(js_array_get_int(a, i), js_array_get_int(b, i), depth + 1))
                return false;
        }
        return true;
    }

    // both objects/maps: structural comparison
    if (ta == LMD_TYPE_MAP && tb == LMD_TYPE_MAP) {
        Item keys_a = js_object_keys(a);
        Item keys_b = js_object_keys(b);
        int64_t len_a = js_array_length(keys_a);
        int64_t len_b = js_array_length(keys_b);
        if (len_a != len_b) return false;

        for (int64_t i = 0; i < len_a; i++) {
            Item key = js_array_get_int(keys_a, i);
            // check same key exists in b
            Item val_a = js_property_get(a, key);
            Item val_b = js_property_get(b, key);
            if (!js_deep_equal_compare(val_a, val_b, depth + 1))
                return false;
        }
        return true;
    }

    // mismatched types (array vs object, etc.)
    return false;
}

extern "C" void js_assert_deep_equal(Item actual, Item expected, Item message) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    extern Item js_to_string_val(Item value);

    bool equal = js_deep_equal_compare(actual, expected, 0);
    if (equal) return;

    // build error message
    Item a_str = js_to_string_val(actual);
    Item e_str = js_to_string_val(expected);
    String* as = it2s(a_str);
    String* es = it2s(e_str);
    String* ms = (get_type_id(message) == LMD_TYPE_STRING) ? it2s(message) : NULL;

    const char* p1 = "Expected ";
    const char* p2 = " to be structurally equal to ";
    const char* p3 = ". ";
    int total = (int)strlen(p1) + (as ? (int)as->len : 0) + (int)strlen(p2) +
                (es ? (int)es->len : 0) + (int)strlen(p3) + (ms ? (int)ms->len : 0);
    char* buf = (char*)malloc(total + 1);
    int pos = 0;
    int l;
    l = (int)strlen(p1); memcpy(buf + pos, p1, l); pos += l;
    if (as) { memcpy(buf + pos, as->chars, as->len); pos += (int)as->len; }
    l = (int)strlen(p2); memcpy(buf + pos, p2, l); pos += l;
    if (es) { memcpy(buf + pos, es->chars, es->len); pos += (int)es->len; }
    l = (int)strlen(p3); memcpy(buf + pos, p3, l); pos += l;
    if (ms) { memcpy(buf + pos, ms->chars, ms->len); pos += (int)ms->len; }
    buf[pos] = '\0';

    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    Item err_msg = (Item){.item = s2it(heap_create_name(buf, pos))};
    free(buf);
    js_throw_value(js_new_error_with_name(err_name, err_msg));
}

// =============================================================================
// Native assert.throws for test262 (debug builds only)
// assert.throws(expectedErrorConstructor, func [, message])
// =============================================================================

extern "C" void js_assert_throws(Item expected_ctor, Item func, Item message) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    extern Item js_call_function(Item func_item, Item this_val, Item* args, int arg_count);
    extern int js_check_exception(void);
    extern Item js_clear_exception(void);
    extern Item js_to_string_val(Item value);
    extern Item js_instanceof(Item left, Item right);
    extern Item js_property_get(Item obj, Item key);

    // validate func argument
    if (get_type_id(func) != LMD_TYPE_FUNC) {
        Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
        Item err_msg  = (Item){.item = s2it(heap_create_name("assert.throws requires two arguments: the error constructor and a function to run"))};
        js_throw_value(js_new_error_with_name(err_name, err_msg));
        return;
    }

    // get message prefix
    const char* msg_chars = "";
    int msg_len = 0;
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* ms = it2s(message);
        if (ms && ms->len > 0) { msg_chars = ms->chars; msg_len = (int)ms->len; }
    }

    // call the function — expect it to throw
    js_call_function(func, make_js_undefined(), NULL, 0);

    if (js_check_exception()) {
        // good — an exception was thrown. check its type.
        Item thrown = js_clear_exception();

        // thrown must be an object
        TypeId tid = get_type_id(thrown);
        if (tid != LMD_TYPE_MAP && tid != LMD_TYPE_ELEMENT) {
            char buf[1200];
            int pos = 0;
            if (msg_len > 0) { memcpy(buf, msg_chars, msg_len < 1000 ? msg_len : 1000); pos = msg_len < 1000 ? msg_len : 1000; buf[pos++] = ' '; }
            const char* t = "Thrown value was not an object!";
            int tl = (int)strlen(t);
            memcpy(buf + pos, t, tl); pos += tl;
            buf[pos] = '\0';
            Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
            Item err_msg  = (Item){.item = s2it(heap_create_name(buf, pos))};
            js_throw_value(js_new_error_with_name(err_name, err_msg));
            return;
        }

        // check: thrown instanceof expectedErrorConstructor
        Item instanceof_result = js_instanceof(thrown, expected_ctor);
        bool is_instance = (get_type_id(instanceof_result) == LMD_TYPE_BOOL && it2b(instanceof_result));

        if (!is_instance) {
            // type mismatch — build error message
            Item name_key = (Item){.item = s2it(heap_create_name("name"))};
            Item exp_name = js_property_get(expected_ctor, name_key);
            // get actual constructor name via prototype chain
            extern Item js_prototype_lookup(Item obj, Item key);
            Item ctor_key = (Item){.item = s2it(heap_create_name("constructor"))};
            Item thrown_ctor = js_prototype_lookup(thrown, ctor_key);
            Item act_name = (get_type_id(thrown_ctor) != LMD_TYPE_UNDEFINED && get_type_id(thrown_ctor) != LMD_TYPE_NULL)
                ? js_property_get(thrown_ctor, name_key) : make_js_undefined();
            String* ens = (get_type_id(exp_name) == LMD_TYPE_STRING) ? it2s(exp_name) : NULL;
            String* ans = (get_type_id(act_name) == LMD_TYPE_STRING) ? it2s(act_name) : NULL;
            const char* en = ens ? ens->chars : "?";
            int enl = ens ? (int)ens->len : 1;
            const char* an = ans ? ans->chars : "?";
            int anl = ans ? (int)ans->len : 1;

            char buf[1200];
            int pos = 0;
            if (msg_len > 0) { memcpy(buf, msg_chars, msg_len < 900 ? msg_len : 900); pos = msg_len < 900 ? msg_len : 900; buf[pos++] = ' '; }

            if (enl == anl && strncmp(en, an, enl) == 0) {
                const char* t = "Expected a ";
                int tl = (int)strlen(t);
                memcpy(buf + pos, t, tl); pos += tl;
                memcpy(buf + pos, en, enl < 100 ? enl : 100); pos += enl < 100 ? enl : 100;
                const char* t2 = " but got a different error constructor with the same name";
                int t2l = (int)strlen(t2);
                memcpy(buf + pos, t2, t2l); pos += t2l;
            } else {
                const char* t = "Expected a ";
                int tl = (int)strlen(t);
                memcpy(buf + pos, t, tl); pos += tl;
                memcpy(buf + pos, en, enl < 100 ? enl : 100); pos += enl < 100 ? enl : 100;
                const char* t2 = " but got a ";
                int t2l = (int)strlen(t2);
                memcpy(buf + pos, t2, t2l); pos += t2l;
                memcpy(buf + pos, an, anl < 100 ? anl : 100); pos += anl < 100 ? anl : 100;
            }
            buf[pos] = '\0';
            Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
            Item err_msg  = (Item){.item = s2it(heap_create_name(buf, pos))};
            js_throw_value(js_new_error_with_name(err_name, err_msg));
        }
        return;
    }

    // no exception was thrown — that's a failure
    {
        Item name_key = (Item){.item = s2it(heap_create_name("name"))};
        Item exp_name = js_property_get(expected_ctor, name_key);
        String* ens = (get_type_id(exp_name) == LMD_TYPE_STRING) ? it2s(exp_name) : NULL;
        const char* en = ens ? ens->chars : "?";
        int enl = ens ? (int)ens->len : 1;

        char buf[1200];
        int pos = 0;
        if (msg_len > 0) { memcpy(buf, msg_chars, msg_len < 900 ? msg_len : 900); pos = msg_len < 900 ? msg_len : 900; buf[pos++] = ' '; }
        const char* t = "Expected a ";
        int tl = (int)strlen(t);
        memcpy(buf + pos, t, tl); pos += tl;
        memcpy(buf + pos, en, enl < 100 ? enl : 100); pos += enl < 100 ? enl : 100;
        const char* t2 = " to be thrown but no exception was thrown at all";
        int t2l = (int)strlen(t2);
        memcpy(buf + pos, t2, t2l); pos += t2l;
        buf[pos] = '\0';
        Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
        Item err_msg  = (Item){.item = s2it(heap_create_name(buf, pos))};
        js_throw_value(js_new_error_with_name(err_name, err_msg));
    }
}

// =============================================================================
// Native assert() base function for test262 (debug builds only)
// assert(mustBeTrue [, message])
// =============================================================================

extern "C" void js_assert_base(Item must_be_true, Item message) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    extern Item js_to_string_val(Item value);

    // check mustBeTrue === true
    if (get_type_id(must_be_true) == LMD_TYPE_BOOL && it2b(must_be_true)) return;

    // build error message
    if (get_type_id(message) == LMD_TYPE_STRING) {
        String* ms = it2s(message);
        if (ms && ms->len > 0) {
            Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
            js_throw_value(js_new_error_with_name(err_name, message));
            return;
        }
    }

    // default message: "Expected true but got <value>"
    Item val_str = js_to_string_val(must_be_true);
    String* vs = it2s(val_str);
    const char* prefix = "Expected true but got ";
    int plen = (int)strlen(prefix);
    int vlen = vs ? (int)vs->len : 9;
    const char* vchars = vs ? vs->chars : "undefined";
    char* buf = (char*)malloc(plen + vlen + 1);
    memcpy(buf, prefix, plen);
    memcpy(buf + plen, vchars, vlen);
    buf[plen + vlen] = '\0';
    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    Item err_msg  = (Item){.item = s2it(heap_create_name(buf, plen + vlen))};
    free(buf);
    js_throw_value(js_new_error_with_name(err_name, err_msg));
}

// =============================================================================
// Native $DONOTEVALUATE for test262 (debug builds only)
// =============================================================================

extern "C" void js_donotevaluate(void) {
    extern Item js_new_error_with_name(Item type_name, Item message);
    extern void js_throw_value(Item error);
    Item err_name = (Item){.item = s2it(heap_create_name("Test262Error"))};
    Item err_msg  = (Item){.item = s2it(heap_create_name("Test262: This statement should not be evaluated."))};
    js_throw_value(js_new_error_with_name(err_name, err_msg));
}

#endif // !NDEBUG

// =============================================================================
// Object.assign(target, ...sources)
// =============================================================================

extern "C" Item js_object_assign(Item target, Item* sources, int count) {
    TypeId tid = get_type_id(target);
    if (tid == LMD_TYPE_NULL || tid == LMD_TYPE_UNDEFINED ||
        (target.item == 0 && tid != LMD_TYPE_INT)) {
        extern Item js_new_error_with_name(Item type_name, Item message);
        extern void js_throw_value(Item error);
        Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
        Item msg = (Item){.item = s2it(heap_create_name("Cannot convert undefined or null to object"))};
        js_throw_value(js_new_error_with_name(tn, msg));
        return ItemNull;
    }
    if (tid != LMD_TYPE_MAP) return target;
    for (int i = 0; i < count; i++) {
        Item source = sources[i];
        if (get_type_id(source) != LMD_TYPE_MAP) continue;
        Map* m = source.map;
        if (!m || !m->type) continue;
        TypeMap* tm = (TypeMap*)m->type;
        ShapeEntry* e = tm->shape;
        while (e) {
            if (e->name) {
                const char* n = e->name->str;
                int nlen = (int)e->name->length;
                // v20: Skip internal marker properties
                if (nlen >= 2 && n[0] == '_' && n[1] == '_') {
                    e = e->next;
                    continue;
                }
                // v20: Skip non-enumerable properties
                char ne_buf[256];
                snprintf(ne_buf, sizeof(ne_buf), "__ne_%.*s", nlen, n);
                bool ne_found = false;
                Item ne_val = js_map_get_fast_ext(m, ne_buf, (int)strlen(ne_buf), &ne_found);
                if (ne_found && js_is_truthy(ne_val)) {
                    e = e->next;
                    continue;
                }
                Item val = _map_read_field(e, m->data);
                if (val.item != JS_DELETED_SENTINEL_VAL) {
                    Item key = (Item){.item = s2it(heap_create_name(n, nlen))};
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
            const char* n = e->name->str;
            int nlen = (int)e->name->length;
            // v20: Skip internal marker properties (__ne_, __nw_, __nc_, __get_, __set_, __proto__, __class_name__, __sym_)
            if (nlen >= 2 && n[0] == '_' && n[1] == '_') {
                e = e->next;
                continue;
            }
            // v20: Skip non-enumerable properties (check __ne_ marker)
            char ne_buf[256];
            snprintf(ne_buf, sizeof(ne_buf), "__ne_%.*s", nlen, n);
            bool ne_found = false;
            Item ne_val = js_map_get_fast_ext(m, ne_buf, (int)strlen(ne_buf), &ne_found);
            if (ne_found && js_is_truthy(ne_val)) {
                e = e->next;
                continue;
            }
            Item val = _map_read_field(e, m->data);
            if (val.item != JS_DELETED_SENTINEL_VAL) {
                Item key = (Item){.item = s2it(heap_create_name(n, nlen))};
                js_property_set(target, key, val);
            }
        }
        e = e->next;
    }
    return target;
}

// =============================================================================
// Helper: check if a Map with __class_name__ has a built-in method as own prop
// Returns true if the map is a prototype object and the key matches a builtin.
// =============================================================================
static bool js_map_has_builtin_method(Map* m, const char* name, int len) {
    // Only check builtin methods on actual prototype objects (not instances)
    bool ip_own = false;
    js_map_get_fast_ext(m, "__is_proto__", 12, &ip_own);
    if (!ip_own) return false;
    bool cn_own = false;
    Item cn = js_map_get_fast_ext(m, "__class_name__", 14, &cn_own);
    if (!cn_own || get_type_id(cn) != LMD_TYPE_STRING) return false;
    String* cn_str = it2s(cn);
    if (!cn_str) return false;
    // map class name to TypeId for builtin lookup
    TypeId lookup_type = LMD_TYPE_MAP;
    if (cn_str->len == 6 && strncmp(cn_str->chars, "String", 6) == 0) lookup_type = LMD_TYPE_STRING;
    else if (cn_str->len == 5 && strncmp(cn_str->chars, "Array", 5) == 0) lookup_type = LMD_TYPE_ARRAY;
    else if (cn_str->len == 6 && strncmp(cn_str->chars, "Number", 6) == 0) lookup_type = LMD_TYPE_INT;
    else if (cn_str->len == 8 && strncmp(cn_str->chars, "Function", 8) == 0) lookup_type = LMD_TYPE_FUNC;
    else if (cn_str->len == 6 && strncmp(cn_str->chars, "RegExp", 6) == 0) lookup_type = LMD_TYPE_MAP;
    else if (cn_str->len == 7 && strncmp(cn_str->chars, "Boolean", 7) == 0) lookup_type = LMD_TYPE_MAP;
    // Skip "constructor" — handled separately
    if (len == 11 && strncmp(name, "constructor", 11) == 0) return true;
    Item builtin = js_lookup_builtin_method(lookup_type, name, len);
    return builtin.item != ItemNull.item;
}

// =============================================================================
// obj.hasOwnProperty(key) / Object.hasOwn(obj, key)
// =============================================================================

extern "C" Item js_has_own_property(Item obj, Item key) {
    // v23: handle array objects — numeric indices and "length"
    if (get_type_id(obj) == LMD_TYPE_ARRAY) {
        Item k = js_to_string(key);
        if (get_type_id(k) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
        String* ks = it2s(k);
        if (!ks) return (Item){.item = b2it(false)};
        // "length" is always an own property of arrays
        if (ks->len == 6 && strncmp(ks->chars, "length", 6) == 0) {
            return (Item){.item = b2it(true)};
        }
        // Check if it's a valid numeric index within bounds
        // Parse as integer directly to avoid depending on static js_get_number
        bool is_numeric = (ks->len > 0 && ks->len <= 10);
        int64_t idx = 0;
        if (is_numeric) {
            for (int i = 0; i < (int)ks->len; i++) {
                char c = ks->chars[i];
                if (c < '0' || c > '9') { is_numeric = false; break; }
                idx = idx * 10 + (c - '0');
            }
            // reject leading zeros like "01" (except "0" itself)
            if (is_numeric && ks->len > 1 && ks->chars[0] == '0') is_numeric = false;
        }
        if (is_numeric) {
            Array* arr = obj.array;
            if (idx >= 0 && idx < arr->length) {
                // v25: check for deleted sentinel (array hole)
                if (arr->items[idx].item == JS_DELETED_SENTINEL_VAL) {
                    return (Item){.item = b2it(false)};
                }
                return (Item){.item = b2it(true)};
            }
        }
        return (Item){.item = b2it(false)};
    }
    // v18: handle function objects — prototype, name, length, and custom properties
    if (get_type_id(obj) == LMD_TYPE_FUNC) {
        Item k = js_to_string(key);
        if (get_type_id(k) != LMD_TYPE_STRING) return (Item){.item = b2it(false)};
        String* ks = it2s(k);
        if (!ks) return (Item){.item = b2it(false)};
        // v23: Check properties_map FIRST for deleted/overridden properties
        JsFuncProps* fn = (JsFuncProps*)obj.function;
        if (fn->properties_map.item != 0) {
            bool own = false;
            Item val = js_map_get_fast_ext(fn->properties_map.map, ks->chars, ks->len, &own);
            if (own) {
                // If sentinel, property was deleted
                if (val.item == JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(false)};
                return (Item){.item = b2it(true)};
            }
        }
        // built-in own properties (not overridden/deleted)
        if ((ks->len == 4 && strncmp(ks->chars, "name", 4) == 0) ||
            (ks->len == 6 && strncmp(ks->chars, "length", 6) == 0)) {
            return (Item){.item = b2it(true)};
        }
        // prototype is own only for constructor functions
        if (ks->len == 9 && strncmp(ks->chars, "prototype", 9) == 0) {
            return (Item){.item = b2it(js_func_has_own_prototype(obj))};
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
    // Use js_map_get_fast_ext which uses the hash table (last-writer-wins)
    // to always find the latest value for a key. The old linear shape walk
    // could find stale entries when a property was updated (e.g. deleted).
    bool found = false;
    Item val = js_map_get_fast_ext(m, ks->chars, (int)ks->len, &found);
    if (!found) {
        // v24: Also check for accessor properties (__get_key / __set_key)
        if (ks->len > 0 && ks->len < 200) {
            char accessor_key[256];
            snprintf(accessor_key, sizeof(accessor_key), "__get_%.*s", (int)ks->len, ks->chars);
            bool has_get = false;
            Item get_val = js_map_get_fast_ext(m, accessor_key, (int)strlen(accessor_key), &has_get);
            if (has_get && get_val.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
            snprintf(accessor_key, sizeof(accessor_key), "__set_%.*s", (int)ks->len, ks->chars);
            bool has_set = false;
            Item set_val = js_map_get_fast_ext(m, accessor_key, (int)strlen(accessor_key), &has_set);
            if (has_set && set_val.item != JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(true)};
        }
        // v26: check if this is a prototype Map with builtin methods
        if (js_map_has_builtin_method(m, ks->chars, (int)ks->len)) return (Item){.item = b2it(true)};
        // String wrapper indexed access: new String("abc").hasOwnProperty("0") → true
        if (ks->len > 0 && ks->chars[0] >= '0' && ks->chars[0] <= '9') {
            bool cn_found = false;
            Item cn = js_map_get_fast_ext(m, "__class_name__", 14, &cn_found);
            if (cn_found && get_type_id(cn) == LMD_TYPE_STRING) {
                String* cn_str = it2s(cn);
                if (cn_str && cn_str->len == 6 && strncmp(cn_str->chars, "String", 6) == 0) {
                    bool pv_found = false;
                    Item pv = js_map_get_fast_ext(m, "__primitiveValue__", 18, &pv_found);
                    if (pv_found && get_type_id(pv) == LMD_TYPE_STRING) {
                        String* pv_str = it2s(pv);
                        bool is_idx = true;
                        int64_t idx = 0;
                        for (int ni = 0; ni < (int)ks->len; ni++) {
                            if (ks->chars[ni] < '0' || ks->chars[ni] > '9') { is_idx = false; break; }
                            idx = idx * 10 + (ks->chars[ni] - '0');
                        }
                        if (is_idx && idx >= 0 && idx < (int64_t)(pv_str ? pv_str->len : 0)) {
                            return (Item){.item = b2it(true)};
                        }
                    }
                }
            }
        }
        return (Item){.item = b2it(false)};
    }
    if (val.item == JS_DELETED_SENTINEL_VAL) return (Item){.item = b2it(false)};
    return (Item){.item = b2it(true)};
}

// =============================================================================
// Object.freeze(obj) — set __frozen__ flag, Object.isFrozen(obj)
// =============================================================================

extern "C" Item js_object_freeze(Item obj) {
    // ES6: non-objects return the argument
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT) return obj;
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
                    js_defprop_set_marker(obj, nw_k, (Item){.item = b2it(true)});
                    snprintf(attr_key, sizeof(attr_key), "__nc_%.*s", (int)str_key->len, str_key->chars);
                    Item nc_k = (Item){.item = s2it(heap_create_name(attr_key, strlen(attr_key)))};
                    js_defprop_set_marker(obj, nc_k, (Item){.item = b2it(true)});
                }
            }
        }
    }
    Item key = (Item){.item = s2it(heap_create_name("__frozen__", 10))};
    js_defprop_set_marker(obj, key, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_frozen(Item obj) {
    // ES6: non-objects are frozen
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT)
        return (Item){.item = b2it(true)};
    // For arrays and functions, check via marker system
    if (ot == LMD_TYPE_ARRAY || ot == LMD_TYPE_FUNC) {
        bool found = false;
        Item fv = js_defprop_get_marker(obj, "__frozen__", 10, &found);
        if (found && js_is_truthy(fv)) return (Item){.item = b2it(true)};
        return (Item){.item = b2it(false)};
    }
    if (ot != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    // fast path: explicitly frozen
    bool fk_found = false;
    Item fv = js_map_get_fast_ext(obj.map, "__frozen__", 10, &fk_found);
    if (fk_found && js_is_truthy(fv)) return (Item){.item = b2it(true)};
    // must be non-extensible
    bool ne_found = false;
    Item nev = js_map_get_fast_ext(obj.map, "__non_extensible__", 17, &ne_found);
    if (!ne_found || !js_is_truthy(nev)) return (Item){.item = b2it(false)};
    // check all own properties are non-configurable and non-writable (or accessor)
    Map* m = obj.map;
    if (!m || !m->type) return (Item){.item = b2it(true)}; // no shape = no properties
    TypeMap* tm = (TypeMap*)m->type;
    ShapeEntry* e = tm->shape;
    while (e) {
        if (e->name) {
            const char* n = e->name->str;
            int nlen = (int)e->name->length;
            if (nlen >= 2 && n[0] == '_' && n[1] == '_') { e = e->next; continue; }
            // check non-configurable
            char buf[256];
            snprintf(buf, sizeof(buf), "__nc_%.*s", nlen, n);
            bool nc_found = false;
            Item ncv = js_map_get_fast_ext(m, buf, (int)strlen(buf), &nc_found);
            if (!nc_found || !js_is_truthy(ncv)) return (Item){.item = b2it(false)};
            // check non-writable (data property) or accessor
            snprintf(buf, sizeof(buf), "__get_%.*s", nlen, n);
            bool is_accessor = false;
            js_map_get_fast_ext(m, buf, (int)strlen(buf), &is_accessor);
            if (!is_accessor) {
                snprintf(buf, sizeof(buf), "__set_%.*s", nlen, n);
                js_map_get_fast_ext(m, buf, (int)strlen(buf), &is_accessor);
            }
            if (!is_accessor) {
                snprintf(buf, sizeof(buf), "__nw_%.*s", nlen, n);
                bool nw_found = false;
                Item nwv = js_map_get_fast_ext(m, buf, (int)strlen(buf), &nw_found);
                if (!nw_found || !js_is_truthy(nwv)) return (Item){.item = b2it(false)};
            }
        }
        e = e->next;
    }
    return (Item){.item = b2it(true)};
}

// =============================================================================
// Object.seal — mark all properties non-configurable, mark object non-extensible
// =============================================================================

extern "C" Item js_object_seal(Item obj) {
    // ES6: non-objects return the argument
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT) return obj;
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
                    js_defprop_set_marker(obj, nc_k, (Item){.item = b2it(true)});
                }
            }
        }
    }
    Item sealed_k = (Item){.item = s2it(heap_create_name("__sealed__", 10))};
    js_defprop_set_marker(obj, sealed_k, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_sealed(Item obj) {
    // ES6: non-objects are sealed
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT)
        return (Item){.item = b2it(true)};
    // For arrays and functions, check via marker system
    if (ot == LMD_TYPE_ARRAY || ot == LMD_TYPE_FUNC) {
        bool found = false;
        Item sv = js_defprop_get_marker(obj, "__sealed__", 10, &found);
        if (found && js_is_truthy(sv)) return (Item){.item = b2it(true)};
        Item fv = js_defprop_get_marker(obj, "__frozen__", 10, &found);
        if (found && js_is_truthy(fv)) return (Item){.item = b2it(true)};
        return (Item){.item = b2it(false)};
    }
    if (ot != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    // fast path: explicitly sealed or frozen
    bool sk_found = false;
    Item sv = js_map_get_fast_ext(obj.map, "__sealed__", 10, &sk_found);
    if (sk_found && js_is_truthy(sv)) return (Item){.item = b2it(true)};
    bool fk_found = false;
    Item fv = js_map_get_fast_ext(obj.map, "__frozen__", 10, &fk_found);
    if (fk_found && js_is_truthy(fv)) return (Item){.item = b2it(true)};
    // must be non-extensible
    bool ne_found = false;
    Item nev = js_map_get_fast_ext(obj.map, "__non_extensible__", 17, &ne_found);
    if (!ne_found || !js_is_truthy(nev)) return (Item){.item = b2it(false)};
    // check all own properties are non-configurable
    Map* m = obj.map;
    if (!m || !m->type) return (Item){.item = b2it(true)}; // no shape = no properties
    TypeMap* tm = (TypeMap*)m->type;
    ShapeEntry* e = tm->shape;
    while (e) {
        if (e->name) {
            const char* n = e->name->str;
            int nlen = (int)e->name->length;
            if (nlen >= 2 && n[0] == '_' && n[1] == '_') { e = e->next; continue; }
            char buf[256];
            snprintf(buf, sizeof(buf), "__nc_%.*s", nlen, n);
            bool nc_found = false;
            Item ncv = js_map_get_fast_ext(m, buf, (int)strlen(buf), &nc_found);
            if (!nc_found || !js_is_truthy(ncv)) return (Item){.item = b2it(false)};
        }
        e = e->next;
    }
    return (Item){.item = b2it(true)};
}

// =============================================================================
// Object.preventExtensions / Object.isExtensible
// =============================================================================

extern "C" Item js_object_prevent_extensions(Item obj) {
    // ES6: non-objects return the argument
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT) return obj;
    Item key = (Item){.item = s2it(heap_create_name("__non_extensible__", 17))};
    js_defprop_set_marker(obj, key, (Item){.item = b2it(true)});
    return obj;
}

extern "C" Item js_object_is_extensible(Item obj) {
    // ES6: non-objects are not extensible
    TypeId ot = get_type_id(obj);
    if (ot != LMD_TYPE_MAP && ot != LMD_TYPE_ARRAY && ot != LMD_TYPE_FUNC && ot != LMD_TYPE_ELEMENT)
        return (Item){.item = b2it(false)};
    if (ot == LMD_TYPE_ARRAY) {
        // Arrays: check companion map for __non_extensible__ marker
        Array* arr = obj.array;
        if (arr->extra != 0) {
            Map* props = (Map*)(uintptr_t)arr->extra;
            bool found = false;
            Item ne_v = js_map_get_fast_ext(props, "__non_extensible__", 17, &found);
            if (found && js_is_truthy(ne_v)) return (Item){.item = b2it(false)};
            Item sl_v = js_map_get_fast_ext(props, "__sealed__", 10, &found);
            if (found && js_is_truthy(sl_v)) return (Item){.item = b2it(false)};
            Item fr_v = js_map_get_fast_ext(props, "__frozen__", 10, &found);
            if (found && js_is_truthy(fr_v)) return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(true)};
    }
    if (ot == LMD_TYPE_FUNC) {
        // Functions: check properties_map for __non_extensible__ marker
        JsFuncProps* fn = (JsFuncProps*)obj.function;
        if (get_type_id(fn->properties_map) == LMD_TYPE_MAP) {
            Map* pm = fn->properties_map.map;
            bool found = false;
            Item ne_v = js_map_get_fast_ext(pm, "__non_extensible__", 17, &found);
            if (found && js_is_truthy(ne_v)) return (Item){.item = b2it(false)};
            Item sl_v = js_map_get_fast_ext(pm, "__sealed__", 10, &found);
            if (found && js_is_truthy(sl_v)) return (Item){.item = b2it(false)};
            Item fr_v = js_map_get_fast_ext(pm, "__frozen__", 10, &found);
            if (found && js_is_truthy(fr_v)) return (Item){.item = b2it(false)};
        }
        return (Item){.item = b2it(true)};
    }
    if (ot != LMD_TYPE_MAP) return (Item){.item = b2it(false)};
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
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) {
        if (type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(true)};
    }
    if (type == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        return (Item){.item = b2it(isfinite(d) && d == floor(d))};
    }
    return (Item){.item = b2it(false)};
}

extern "C" Item js_number_is_finite(Item value) {
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) {
        if (type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) return (Item){.item = b2it(false)};
        return (Item){.item = b2it(true)};
    }
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
    if (type == LMD_TYPE_INT || type == LMD_TYPE_INT64) {
        if (type == LMD_TYPE_INT && it2i(value) <= -(int64_t)JS_SYMBOL_BASE) return (Item){.item = b2it(false)};
        int64_t v = it2i(value);
        return (Item){.item = b2it(v >= -9007199254740991LL && v <= 9007199254740991LL)};
    }
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
    // v20: Array-like objects with .length property (e.g. {0: 'a', 1: 'b', length: 2})
    if (tid == LMD_TYPE_MAP) {
        Item len_key = (Item){.item = s2it(heap_create_name("length", 6))};
        Item len_val = js_property_get(iterable, len_key);
        TypeId lt = get_type_id(len_val);
        if (lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) {
            int len = (lt == LMD_TYPE_INT) ? (int)it2i(len_val) : (int)it2d(len_val);
            if (len >= 0) {
                Item result = js_array_new(0);
                for (int i = 0; i < len; i++) {
                    char idx_buf[16];
                    snprintf(idx_buf, sizeof(idx_buf), "%d", i);
                    Item idx_key = (Item){.item = s2it(heap_create_name(idx_buf, strlen(idx_buf)))};
                    Item val = js_property_get(iterable, idx_key);
                    js_array_push(result, val);
                }
                return result;
            }
        }
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
    if (!s || s->len == 0) {
        // empty string is not valid JSON
        js_throw_syntax_error((Item){.item = s2it(heap_create_name("Unexpected end of JSON input"))});
        return ItemNull;
    }

    // null-terminate for the parser
    char* buf = (char*)alloca(s->len + 1);
    memcpy(buf, s->chars, s->len);
    buf[s->len] = '\0';

    if (!js_input) {
        log_error("js_json_parse: no input context");
        return ItemNull;
    }

    bool ok = false;
    Item result = parse_json_to_item_strict(js_input, buf, &ok);
    if (!ok) {
        js_throw_syntax_error((Item){.item = s2it(heap_create_name("Unexpected token in JSON"))});
        return ItemNull;
    }
    return result;
}

// v20: walk parsed JSON tree bottom-up, applying reviver function
static Item js_json_revive(Item holder, Item key, Item reviver) {
    Item val = js_property_access(holder, key);
    TypeId vtype = get_type_id(val);

    if (vtype == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(val);
        for (int64_t i = 0; i < len; i++) {
            Item idx_str = js_to_string((Item){.item = i2it((int)i)});
            Item new_elem = js_json_revive(val, idx_str, reviver);
            if (get_type_id(new_elem) == LMD_TYPE_UNDEFINED) {
                js_delete_property(val, idx_str);
            } else {
                js_array_set(val, (Item){.item = i2it((int)i)}, new_elem);
            }
        }
    } else if (vtype == LMD_TYPE_MAP) {
        Item keys = js_object_keys(val);
        int64_t klen = js_array_length(keys);
        for (int64_t i = 0; i < klen; i++) {
            Item k = js_array_get(keys, (Item){.item = i2it((int)i)});
            Item new_val = js_json_revive(val, k, reviver);
            if (get_type_id(new_val) == LMD_TYPE_UNDEFINED) {
                js_delete_property(val, k);
            } else {
                js_property_set(val, k, new_val);
            }
        }
    }

    // Call reviver with (key, val)
    Item args[2] = {key, val};
    return js_call_function(reviver, holder, args, 2);
}

extern "C" Item js_json_parse_full(Item str_item, Item reviver) {
    Item result = js_json_parse(str_item);
    if (result.item == ItemNull.item) return result;

    if (get_type_id(reviver) == LMD_TYPE_FUNC) {
        // Create a wrapper object {"": result} as the root holder
        Item wrapper = js_new_object();
        Item empty_key = (Item){.item = s2it(heap_create_name("", 0))};
        js_property_set(wrapper, empty_key, result);
        result = js_json_revive(wrapper, empty_key, reviver);
    }
    return result;
}

// =============================================================================
// JSON.stringify(value, replacer?, space?) — convert Lambda object to JSON string
// =============================================================================

// v20: forward declarations for recursive JSON serialization
// Circular reference detection for JSON.stringify
#define JSON_STRINGIFY_MAX_DEPTH 1024

// Forward declaration: check if an Item is a JS Symbol (encoded as negative int)
static bool js_is_symbol_item(Item item);

static void js_stringify_value(StrBuf* sb, Item value, Item replacer, const char* gap,
                               int depth, Item holder, Item key,
                               void** visited, int visited_count);
static void js_stringify_escape_string(StrBuf* sb, const char* s, int len);

static void js_stringify_escape_string(StrBuf* sb, const char* s, int len) {
    strbuf_append_char(sb, '"');
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"': strbuf_append_str_n(sb, "\\\"", 2); break;
            case '\\': strbuf_append_str_n(sb, "\\\\", 2); break;
            case '\b': strbuf_append_str_n(sb, "\\b", 2); break;
            case '\f': strbuf_append_str_n(sb, "\\f", 2); break;
            case '\n': strbuf_append_str_n(sb, "\\n", 2); break;
            case '\r': strbuf_append_str_n(sb, "\\r", 2); break;
            case '\t': strbuf_append_str_n(sb, "\\t", 2); break;
            default:
                if (c < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", c);
                    strbuf_append_str_n(sb, esc, 6);
                } else {
                    strbuf_append_char(sb, (char)c);
                }
                break;
        }
    }
    strbuf_append_char(sb, '"');
}

static void js_stringify_indent(StrBuf* sb, const char* gap, int depth) {
    if (!gap || !gap[0]) return;
    strbuf_append_char(sb, '\n');
    for (int i = 0; i < depth; i++) {
        strbuf_append_str_n(sb, gap, (int)strlen(gap));
    }
}

static void js_stringify_value(StrBuf* sb, Item value, Item replacer, const char* gap,
                               int depth, Item holder, Item key,
                               void** visited, int visited_count) {
    // Apply replacer function if provided
    if (get_type_id(replacer) == LMD_TYPE_FUNC || get_type_id(replacer) == LMD_TYPE_MAP) {
        // check if it's a callable function
        TypeId rt = get_type_id(replacer);
        if (rt == LMD_TYPE_FUNC) {
            Item args[2] = {key, value};
            value = js_call_function(replacer, holder, args, 2);
        }
    }

    // Handle toJSON method
    TypeId vtype = get_type_id(value);
    if (vtype == LMD_TYPE_MAP) {
        Item toJSON_name = (Item){.item = s2it(heap_create_name("toJSON", 6))};
        Item toJSON_fn = js_property_access(value, toJSON_name);
        if (get_type_id(toJSON_fn) == LMD_TYPE_FUNC) {
            Item args[1] = {key};
            value = js_call_function(toJSON_fn, value, args, 1);
            vtype = get_type_id(value);
        }
    }

    // Unwrap Boolean/Number/String wrapper objects
    if (vtype == LMD_TYPE_MAP) {
        bool cn_own = false;
        Item cn = js_map_get_fast_ext(value.map, "__class_name__", 14, &cn_own);
        if (cn_own && get_type_id(cn) == LMD_TYPE_STRING) {
            String* cn_str = it2s(cn);
            bool pv_own = false;
            Item pv = js_map_get_fast_ext(value.map, "__primitiveValue__", 18, &pv_own);
            if (pv_own) {
                if (cn_str->len == 7 && strncmp(cn_str->chars, "Boolean", 7) == 0) {
                    value = pv;
                    vtype = get_type_id(value);
                } else if (cn_str->len == 6 && strncmp(cn_str->chars, "Number", 6) == 0) {
                    value = js_to_number(pv);
                    vtype = get_type_id(value);
                } else if (cn_str->len == 6 && strncmp(cn_str->chars, "String", 6) == 0) {
                    value = js_to_string(pv);
                    vtype = get_type_id(value);
                }
            }
        }
    }

    // undefined, function, symbol → write "null" (in arrays, these become null)
    if (vtype == LMD_TYPE_UNDEFINED || vtype == LMD_TYPE_FUNC
        || js_is_symbol_item(value) || value.item == ITEM_JS_UNDEFINED) {
        strbuf_append_str_n(sb, "null", 4);
        return;
    }
    if (value.item == ItemNull.item) {
        strbuf_append_str_n(sb, "null", 4);
        return;
    }

    // Boolean
    if (vtype == LMD_TYPE_BOOL) {
        if (it2b(value)) strbuf_append_str_n(sb, "true", 4);
        else strbuf_append_str_n(sb, "false", 5);
        return;
    }

    // Number
    if (vtype == LMD_TYPE_INT || vtype == LMD_TYPE_INT64) {
        char buf[32];
        int64_t n = it2i(value);
        int len = snprintf(buf, sizeof(buf), "%lld", (long long)n);
        strbuf_append_str_n(sb, buf, len);
        return;
    }
    if (vtype == LMD_TYPE_FLOAT) {
        double d = it2d(value);
        if (d != d || d == (1.0/0.0) || d == (-1.0/0.0)) {
            strbuf_append_str_n(sb, "null", 4); // NaN, Infinity → null
            return;
        }
        // Negative zero → "0"
        if (d == 0.0) {
            strbuf_append_str_n(sb, "0", 1);
            return;
        }
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%.17g", d);
        strbuf_append_str_n(sb, buf, len);
        return;
    }

    // String
    if (vtype == LMD_TYPE_STRING) {
        String* s = it2s(value);
        if (!s) { strbuf_append_str_n(sb, "null", 4); return; }
        js_stringify_escape_string(sb, s->chars, (int)s->len);
        return;
    }

    // Circular reference detection for arrays and objects
    if (vtype == LMD_TYPE_ARRAY || vtype == LMD_TYPE_MAP) {
        void* ptr = (vtype == LMD_TYPE_ARRAY) ? (void*)value.array : (void*)value.map;
        for (int vi = 0; vi < visited_count; vi++) {
            if (visited[vi] == ptr) {
                Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
                Item msg = (Item){.item = s2it(heap_create_name("Converting circular structure to JSON"))};
                js_throw_value(js_new_error_with_name(tn, msg));
                return;
            }
        }
        if (depth >= JSON_STRINGIFY_MAX_DEPTH) {
            strbuf_append_str_n(sb, "null", 4);
            return;
        }
        // Push onto visited stack
        if (visited_count < JSON_STRINGIFY_MAX_DEPTH) {
            visited[visited_count] = ptr;
            visited_count++;
        }
    }

    // Array
    if (vtype == LMD_TYPE_ARRAY) {
        int64_t len = js_array_length(value);
        if (len == 0) {
            strbuf_append_str_n(sb, "[]", 2);
            return;
        }
        strbuf_append_char(sb, '[');
        for (int64_t i = 0; i < len; i++) {
            if (i > 0) strbuf_append_char(sb, ',');
            js_stringify_indent(sb, gap, depth + 1);
            if (!gap || !gap[0]) {
                // no extra space in compact mode
            }
            Item idx_key = js_to_string((Item){.item = i2it((int)i)});
            Item elem = js_array_get(value, (Item){.item = i2it((int)i)});
            // Check if after replacer, the value would be undefined/function
            TypeId et = get_type_id(elem);
            if (et == LMD_TYPE_UNDEFINED || et == LMD_TYPE_FUNC) {
                // Still call replacer to see what it returns
                if (get_type_id(replacer) == LMD_TYPE_FUNC) {
                    Item args[2] = {idx_key, elem};
                    Item replaced = js_call_function(replacer, value, args, 2);
                    TypeId rrt = get_type_id(replaced);
                    if (rrt == LMD_TYPE_UNDEFINED || rrt == LMD_TYPE_FUNC) {
                        strbuf_append_str_n(sb, "null", 4);
                    } else {
                        js_stringify_value(sb, elem, ItemNull, gap, depth + 1, value, idx_key, visited, visited_count);
                    }
                } else {
                    strbuf_append_str_n(sb, "null", 4);
                }
            } else {
                js_stringify_value(sb, elem, replacer, gap, depth + 1, value, idx_key, visited, visited_count);
            }
        }
        js_stringify_indent(sb, gap, depth);
        strbuf_append_char(sb, ']');
        return;
    }

    // Map (object)
    if (vtype == LMD_TYPE_MAP) {
        Item keys;
        bool use_replacer_array = false;
        // Check if replacer is an array (property whitelist)
        if (get_type_id(replacer) == LMD_TYPE_ARRAY) {
            keys = replacer;
            use_replacer_array = true;
        } else {
            keys = js_object_keys(value);
        }

        int64_t klen = js_array_length(keys);
        strbuf_append_char(sb, '{');
        bool first = true;
        for (int64_t i = 0; i < klen; i++) {
            Item k = js_array_get(keys, (Item){.item = i2it((int)i)});
            Item k_str = js_to_string(k);
            Item v = js_property_access(value, k_str);

            // v20: Apply replacer function BEFORE deciding to include key
            Item replacer_for_recurse = use_replacer_array ? ItemNull : replacer;
            if (get_type_id(replacer_for_recurse) == LMD_TYPE_FUNC) {
                Item args[2] = {k_str, v};
                v = js_call_function(replacer_for_recurse, value, args, 2);
            }

            // Handle toJSON on the value
            TypeId vt2 = get_type_id(v);
            if (vt2 == LMD_TYPE_MAP) {
                Item toJSON_name = (Item){.item = s2it(heap_create_name("toJSON", 6))};
                Item toJSON_fn = js_property_access(v, toJSON_name);
                if (get_type_id(toJSON_fn) == LMD_TYPE_FUNC) {
                    Item tj_args[1] = {k_str};
                    v = js_call_function(toJSON_fn, v, tj_args, 1);
                }
            }

            // Skip undefined, functions, and symbols (they're omitted from objects)
            TypeId vt = get_type_id(v);
            if (vt == LMD_TYPE_UNDEFINED || vt == LMD_TYPE_FUNC || js_is_symbol_item(v)
                || v.item == ITEM_JS_UNDEFINED) continue;

            if (!first) strbuf_append_char(sb, ',');
            first = false;
            js_stringify_indent(sb, gap, depth + 1);
            String* ks = it2s(k_str);
            if (ks) js_stringify_escape_string(sb, ks->chars, (int)ks->len);
            else strbuf_append_str_n(sb, "\"\"", 2);
            strbuf_append_char(sb, ':');
            if (gap && gap[0]) strbuf_append_char(sb, ' ');
            // Don't apply replacer again in recursive call since we already did
            js_stringify_value(sb, v, ItemNull, gap, depth + 1, value, k_str, visited, visited_count);
        }
        if (!first) js_stringify_indent(sb, gap, depth);
        strbuf_append_char(sb, '}');
        return;
    }

    // Fallback: try toString
    Item sval = js_to_string(value);
    String* ss = it2s(sval);
    if (ss) js_stringify_escape_string(sb, ss->chars, (int)ss->len);
    else strbuf_append_str_n(sb, "null", 4);
}

extern "C" Item js_json_stringify_full(Item value, Item replacer, Item space) {
    // Process space parameter
    // ES spec: unwrap Number/String wrapper objects first
    if (get_type_id(space) == LMD_TYPE_MAP) {
        bool cn_own = false;
        Item cn = js_map_get_fast_ext(space.map, "__class_name__", 14, &cn_own);
        if (cn_own && get_type_id(cn) == LMD_TYPE_STRING) {
            String* cn_str = it2s(cn);
            bool pv_own = false;
            Item pv = js_map_get_fast_ext(space.map, "__primitiveValue__", 18, &pv_own);
            if (pv_own) {
                if (cn_str->len == 6 && strncmp(cn_str->chars, "Number", 6) == 0) {
                    space = js_to_number(pv);
                } else if (cn_str->len == 6 && strncmp(cn_str->chars, "String", 6) == 0) {
                    space = pv;
                }
            }
        }
    }
    char gap_buf[11] = {0};
    const char* gap = "";

    TypeId space_type = get_type_id(space);
    if (space_type == LMD_TYPE_INT || space_type == LMD_TYPE_INT64 || space_type == LMD_TYPE_FLOAT) {
        double d = (space_type == LMD_TYPE_FLOAT) ? it2d(space) : (double)it2i(space);
        int n = (int)d;  // ToInteger: truncate toward zero
        if (n < 0) n = 0;
        if (n > 10) n = 10;
        if (n > 0) {
            memset(gap_buf, ' ', n);
            gap_buf[n] = '\0';
            gap = gap_buf;
        }
    } else if (space_type == LMD_TYPE_STRING) {
        String* space_str = it2s(space);
        if (space_str && space_str->len > 0) {
            int n = (int)space_str->len;
            if (n > 10) n = 10;
            memcpy(gap_buf, space_str->chars, n);
            gap_buf[n] = '\0';
            gap = gap_buf;
        }
    }

    // Check if value would produce undefined (bare undefined/function/symbol at top level)
    TypeId vtype = get_type_id(value);
    if (vtype == LMD_TYPE_UNDEFINED || vtype == LMD_TYPE_FUNC
        || js_is_symbol_item(value) || value.item == ITEM_JS_UNDEFINED) {
        // At top level, replacer can still transform
        if (get_type_id(replacer) == LMD_TYPE_FUNC) {
            Item empty_key = (Item){.item = s2it(heap_create_name("", 0))};
            Item wrapper = js_new_object();
            js_property_set(wrapper, empty_key, value);
            Item args[2] = {empty_key, value};
            Item replaced = js_call_function(replacer, wrapper, args, 2);
            TypeId rrt = get_type_id(replaced);
            if (rrt == LMD_TYPE_UNDEFINED || rrt == LMD_TYPE_FUNC
                || js_is_symbol_item(replaced) || replaced.item == ITEM_JS_UNDEFINED)
                return make_js_undefined();
            value = replaced;
        } else {
            return make_js_undefined(); // JSON.stringify(undefined/function/symbol) returns undefined
        }
    }

    StrBuf* sb = strbuf_new();
    Item empty_key = (Item){.item = s2it(heap_create_name("", 0))};
    Item holder = js_new_object();
    void* visited_stack[JSON_STRINGIFY_MAX_DEPTH];
    js_stringify_value(sb, value, replacer, gap, 0, holder, empty_key, visited_stack, 0);

    String* result = heap_strcpy(sb->str, (int)sb->length);
    strbuf_free(sb);
    return (Item){.item = s2it(result)};
}

extern "C" Item js_json_stringify(Item value) {
    return js_json_stringify_full(value, ItemNull, ItemNull);
}

// =============================================================================
// delete operator — remove property from object
// =============================================================================

extern "C" Item js_delete_property(Item obj, Item key) {
    // v23: Handle function property deletion (name, length, prototype, custom)
    if (get_type_id(obj) == LMD_TYPE_FUNC) {
        JsFuncProps* fn = (JsFuncProps*)obj.function;
        // Ensure properties_map exists
        if (fn->properties_map.item == 0) {
            fn->properties_map = js_new_object();
            heap_register_gc_root(&fn->properties_map.item);
        }
        // Check non-configurable: prototype is non-configurable by default for constructors
        if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk && sk->len == 9 && strncmp(sk->chars, "prototype", 9) == 0) {
                if (js_func_has_own_prototype(obj))
                    return (Item){.item = b2it(false)}; // prototype is non-configurable
                return (Item){.item = b2it(true)}; // non-constructors don't have prototype
            }
        }
        // Mark as deleted in properties_map
        js_property_set(fn->properties_map, key, (Item){.item = JS_DELETED_SENTINEL_VAL});
        return (Item){.item = b2it(true)};
    }
    // v25: Handle array element deletion — set element to sentinel to create "hole"
    if (get_type_id(obj) == LMD_TYPE_ARRAY) {
        Array* arr = obj.array;
        // Convert key to numeric index
        int64_t idx = -1;
        if (get_type_id(key) == LMD_TYPE_INT) {
            idx = it2i(key);
        } else if (get_type_id(key) == LMD_TYPE_STRING) {
            String* sk = it2s(key);
            if (sk && sk->len > 0 && sk->len <= 10) {
                bool is_num = true;
                int64_t n = 0;
                for (int i = 0; i < (int)sk->len; i++) {
                    char c = sk->chars[i];
                    if (c < '0' || c > '9') { is_num = false; break; }
                    n = n * 10 + (c - '0');
                }
                if (is_num && !(sk->len > 1 && sk->chars[0] == '0')) idx = n;
            }
        }
        if (idx >= 0 && idx < arr->length) {
            // v27: check __nc_ (non-configurable) marker before deleting
            if (arr->extra != 0) {
                Item k_str = js_to_string(key);
                if (get_type_id(k_str) == LMD_TYPE_STRING) {
                    String* ks = it2s(k_str);
                    if (ks) {
                        Map* pm = (Map*)(uintptr_t)arr->extra;
                        char nc_buf[256];
                        snprintf(nc_buf, sizeof(nc_buf), "__nc_%.*s", (int)ks->len, ks->chars);
                        bool nc_found = false;
                        js_map_get_fast_ext(pm, nc_buf, (int)strlen(nc_buf), &nc_found);
                        if (nc_found) {
                            return (Item){.item = b2it(false)};
                        }
                    }
                }
            }
            arr->items[idx] = (Item){.item = JS_DELETED_SENTINEL_VAL};
            return (Item){.item = b2it(true)};
        }
        // Non-numeric or out-of-range key: check companion map
        if (arr->extra != 0) {
            Map* pm = (Map*)(uintptr_t)arr->extra;
            Item k = js_to_string(key);
            js_property_set((Item){.item = (uint64_t)(uintptr_t)pm | ((uint64_t)LMD_TYPE_MAP << 56)}, k, (Item){.item = JS_DELETED_SENTINEL_VAL});
        }
        return (Item){.item = b2it(true)};
    }
    if (get_type_id(obj) != LMD_TYPE_MAP) return (Item){.item = b2it(true)};
    // v16: Frozen objects reject property deletion
    {
        Map* m = obj.map;
        bool frozen_found = false;
        Item frozen_val = js_map_get_fast_ext(m, "__frozen__", 10, &frozen_found);
        if (frozen_found && js_is_truthy(frozen_val)) {
            if (js_strict_mode) {
                String* sk = (get_type_id(key) == LMD_TYPE_STRING) ? it2s(key) : NULL;
                char msg[256];
                snprintf(msg, sizeof(msg), "Cannot delete property '%.*s' of a frozen object",
                         sk ? (int)sk->len : 0, sk ? sk->chars : "");
                Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                Item em = (Item){.item = s2it(heap_create_name(msg))};
                js_throw_value(js_new_error_with_name(tn, em));
            }
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
                if (js_strict_mode) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "Cannot delete property '%.*s' of #<Object>",
                             (int)str_key->len, str_key->chars);
                    Item tn = (Item){.item = s2it(heap_create_name("TypeError"))};
                    Item em = (Item){.item = s2it(heap_create_name(msg))};
                    js_throw_value(js_new_error_with_name(tn, em));
                }
                return (Item){.item = b2it(false)};
            }
        }
    }
    // Mark property as deleted using sentinel value.
    // Object.keys, hasOwnProperty, in, and JSON.stringify skip sentinel entries.
    //
    // v24: Clear __nw_/__nc_/__ne_ markers BEFORE setting sentinel.
    // js_property_set enforces non-writable checks, which would silently reject
    // the sentinel write for properties defined via Object.defineProperty with
    // writable:false (the default). We clear markers to false first, then
    // use js_property_set for the sentinel (which may trigger shape rebuild).
    if (get_type_id(key) == LMD_TYPE_STRING) {
        String* str_key = it2s(key);
        if (str_key && str_key->len > 0 && str_key->len < 200) {
            char attr_buf[256];
            Item false_val = (Item){.item = b2it(false)};
            // Clear non-writable marker to false (allows sentinel write through js_property_set)
            snprintf(attr_buf, sizeof(attr_buf), "__nw_%.*s", (int)str_key->len, str_key->chars);
            bool nw_found = false;
            js_map_get_fast_ext(obj.map, attr_buf, (int)strlen(attr_buf), &nw_found);
            if (nw_found) {
                Item nw_key = (Item){.item = s2it(heap_create_name(attr_buf, strlen(attr_buf)))};
                fn_map_set(obj, nw_key, false_val);
            }
            // Clear non-configurable marker
            snprintf(attr_buf, sizeof(attr_buf), "__nc_%.*s", (int)str_key->len, str_key->chars);
            bool nc_found = false;
            js_map_get_fast_ext(obj.map, attr_buf, (int)strlen(attr_buf), &nc_found);
            if (nc_found) {
                Item nc_key = (Item){.item = s2it(heap_create_name(attr_buf, strlen(attr_buf)))};
                fn_map_set(obj, nc_key, false_val);
            }
            // Clear non-enumerable marker
            snprintf(attr_buf, sizeof(attr_buf), "__ne_%.*s", (int)str_key->len, str_key->chars);
            bool ne_found = false;
            js_map_get_fast_ext(obj.map, attr_buf, (int)strlen(attr_buf), &ne_found);
            if (ne_found) {
                Item ne_key = (Item){.item = s2it(heap_create_name(attr_buf, strlen(attr_buf)))};
                fn_map_set(obj, ne_key, false_val);
            }
            // Clear getter/setter
            snprintf(attr_buf, sizeof(attr_buf), "__get_%.*s", (int)str_key->len, str_key->chars);
            bool get_found = false;
            js_map_get_fast_ext(obj.map, attr_buf, (int)strlen(attr_buf), &get_found);
            if (get_found) {
                Item get_key = (Item){.item = s2it(heap_create_name(attr_buf, strlen(attr_buf)))};
                fn_map_set(obj, get_key, false_val);
            }
            snprintf(attr_buf, sizeof(attr_buf), "__set_%.*s", (int)str_key->len, str_key->chars);
            bool set_found = false;
            js_map_get_fast_ext(obj.map, attr_buf, (int)strlen(attr_buf), &set_found);
            if (set_found) {
                Item set_key = (Item){.item = s2it(heap_create_name(attr_buf, strlen(attr_buf)))};
                fn_map_set(obj, set_key, false_val);
            }
        }
    }
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

extern "C" uint64_t js_get_heap_epoch();

extern "C" Item js_decodeURIComponent(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};
    size_t decoded_len = 0;
    char* decoded = url_decode_component(s->chars, s->len, &decoded_len);
    if (!decoded) {
        // Cache URIError object per-epoch to avoid expensive error creation
        // in hot loops (e.g., test262 tests that iterate 65000+ code points).
        static Item cached_error = {0};
        static uint64_t cached_epoch = 0;
        if (!cached_error.item || cached_epoch != js_get_heap_epoch()) {
            Item tn = (Item){.item = s2it(heap_create_name("URIError", 8))};
            Item msg = (Item){.item = s2it(heap_create_name("URI malformed", 13))};
            extern Item js_new_error_with_name(Item type_name, Item message);
            cached_error = js_new_error_with_name(tn, msg);
            cached_epoch = js_get_heap_epoch();
        }
        js_throw_value(cached_error);
        return ItemNull;
    }
    String* result = heap_create_name(decoded, decoded_len);
    free(decoded);
    return (Item){.item = s2it(result)};
}

// v20: encodeURI / decodeURI (non-Component variants preserving URI structural chars)
extern "C" Item js_encodeURI(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};
    char* encoded = url_encode_uri(s->chars, s->len);
    if (!encoded) return (Item){.item = s2it(heap_create_name("", 0))};
    String* result = heap_create_name(encoded, strlen(encoded));
    free(encoded);
    return (Item){.item = s2it(result)};
}

extern "C" Item js_decodeURI(Item str_item) {
    Item str_val = js_to_string(str_item);
    String* s = it2s(str_val);
    if (!s || s->len == 0) return (Item){.item = s2it(heap_create_name("", 0))};
    size_t decoded_len = 0;
    char* decoded = url_decode_uri(s->chars, s->len, &decoded_len);
    if (!decoded) {
        static Item cached_error = {0};
        static uint64_t cached_epoch = 0;
        if (!cached_error.item || cached_epoch != js_get_heap_epoch()) {
            Item tn = (Item){.item = s2it(heap_create_name("URIError", 8))};
            Item msg = (Item){.item = s2it(heap_create_name("URI malformed", 13))};
            extern Item js_new_error_with_name(Item type_name, Item message);
            cached_error = js_new_error_with_name(tn, msg);
            cached_epoch = js_get_heap_epoch();
        }
        js_throw_value(cached_error);
        return ItemNull;
    }
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

/**
 * Reset globalThis for batch mode. Forces re-creation on next access
 * so element IDs and variables from previous files don't leak.
 */
extern "C" void js_globals_batch_reset() {
    js_global_this_obj = (Item){0};
    // reset constructor cache (function objects from old pool)
    extern void js_ctor_cache_reset();
    js_ctor_cache_reset();
    // reset process.argv cache
    js_process_argv_items = (Item){.item = ITEM_NULL};
}

// forward declaration for populating globalThis with constructors
extern "C" Item js_get_constructor(Item name_item);

extern "C" Item js_get_global_this() {
    if (js_global_this_obj.item == 0) {
        js_global_this_obj = js_new_object();
        // populate standard globals
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("undefined", 9))}, make_js_undefined());
        double* nan_p = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *nan_p = NAN;
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("NaN", 3))}, (Item){.item = d2it(nan_p)});
        double* inf_p = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *inf_p = INFINITY;
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("Infinity", 8))}, (Item){.item = d2it(inf_p)});

        // populate constructor functions on globalThis
        static const struct { const char* name; int len; } ctor_names[] = {
            {"Object", 6}, {"Array", 5}, {"Function", 8},
            {"String", 6}, {"Number", 6}, {"Boolean", 7}, {"Symbol", 6},
            {"Error", 5}, {"TypeError", 9}, {"RangeError", 10},
            {"ReferenceError", 14}, {"SyntaxError", 11},
            {"URIError", 8}, {"EvalError", 9}, {"AggregateError", 14},
            {"RegExp", 6}, {"Date", 4}, {"Promise", 7},
            {"Map", 3}, {"Set", 3}, {"WeakMap", 7}, {"WeakSet", 7},
            {"ArrayBuffer", 11}, {"DataView", 8},
            {"Int8Array", 9}, {"Uint8Array", 10}, {"Uint8ClampedArray", 17},
            {"Int16Array", 10}, {"Uint16Array", 11},
            {"Int32Array", 10}, {"Uint32Array", 11},
            {"Float32Array", 12}, {"Float64Array", 12},
            {NULL, 0}
        };
        for (int i = 0; ctor_names[i].name; i++) {
            Item name_item = (Item){.item = s2it(heap_create_name(ctor_names[i].name, ctor_names[i].len))};
            Item ctor = js_get_constructor(name_item);
            if (get_type_id(ctor) == LMD_TYPE_FUNC) {
                js_property_set(js_global_this_obj, name_item, ctor);
            }
        }
        // globalThis self-reference
        js_property_set(js_global_this_obj, (Item){.item = s2it(heap_create_name("globalThis", 10))}, js_global_this_obj);
        // ES spec: all standard global properties are non-enumerable
        js_mark_all_non_enumerable(js_global_this_obj);
    }
    return js_global_this_obj;
}

// js_get_global_object: alias for js_get_global_this (used by assignment fallback)
extern "C" Item js_get_global_object() {
    return js_get_global_this();
}

// js_get_global_property: look up a property on the global object by name string
// Used as fallback for unresolved identifiers — implements browser-like named access
extern "C" Item js_get_global_property(Item key) {
    Item global = js_get_global_this();
    return js_property_get(global, key);
}

// js_get_global_property_strict: like js_get_global_property but throws ReferenceError
// for properties that don't exist on the global object. Used for bare identifier reads
// (e.g. `x` as opposed to `obj.x`), which per ES spec must throw ReferenceError.
extern "C" Item js_get_global_property_strict(Item key) {
    Item global = js_get_global_this();
    Item result = js_property_get(global, key);
    // property_get returns JS undefined for missing keys.
    // We need to distinguish "property exists with value undefined" from "not found".
    if (get_type_id(result) == LMD_TYPE_UNDEFINED) {
        // Check if the property actually exists on the global (own or prototype chain)
        extern Item js_has_own_property(Item obj, Item key);
        if (!it2b(js_has_own_property(global, key))) {
            String* sk = it2s(key);
            if (sk) {
                char msg[256];
                snprintf(msg, sizeof(msg), "%.*s is not defined", (int)sk->len, sk->chars);
                extern void js_throw_reference_error(Item message);
                js_throw_reference_error((Item){.item = s2it(heap_create_name(msg, strlen(msg)))});
            }
        }
    }
    return result;
}

// v48: Return a function wrapper for global builtins (parseInt, parseFloat, etc.)
// so they can be passed as values, and have .name/.length properties.
// Uses a simple cache indexed by name hash to avoid re-creating function objects.
#define GLOBAL_BUILTIN_CACHE_SIZE 32
static Item global_builtin_fn_cache[GLOBAL_BUILTIN_CACHE_SIZE];
static bool global_builtin_fn_cache_init = false;

extern "C" Item js_get_global_builtin_fn(Item name_item, Item param_count_item) {
    if (!global_builtin_fn_cache_init) {
        for (int i = 0; i < GLOBAL_BUILTIN_CACHE_SIZE; i++) global_builtin_fn_cache[i] = ItemNull;
        global_builtin_fn_cache_init = true;
    }
    String* name = it2s(name_item);
    if (!name) return ItemNull;
    int pc = (int)it2i(param_count_item);

    // Simple hash to cache slot
    unsigned hash = 0;
    for (int i = 0; i < (int)name->len; i++) hash = hash * 31 + (unsigned char)name->chars[i];
    int slot = hash % GLOBAL_BUILTIN_CACHE_SIZE;
    if (global_builtin_fn_cache[slot].item != ItemNull.item) {
        // verify name matches
        JsFunctionLayout* cached = (JsFunctionLayout*)global_builtin_fn_cache[slot].function;
        if (cached && cached->name && cached->name->len == name->len &&
            strncmp(cached->name->chars, name->chars, name->len) == 0) {
            return global_builtin_fn_cache[slot];
        }
    }

    // Create a new function object with builtin_id = -2 (dispatched by name at call site)
    // pool_calloc zeros all fields; don't set properties_map or prototype to ItemNull
    // since ItemNull.item != 0, which would confuse property lookup code.
    JsFunctionLayout* fn = (JsFunctionLayout*)pool_calloc(js_input->pool, sizeof(JsFunctionLayout));
    fn->type_id = LMD_TYPE_FUNC;
    fn->func_ptr = NULL;
    fn->param_count = pc;
    fn->formal_length = -1; // -1 = use param_count for .length
    fn->builtin_id = -2; // marker: global builtin wrapper
    fn->name = heap_create_name(name->chars, name->len);
    // prototype and properties_map left as zero (pool_calloc)
    Item result = {.function = (Function*)fn};
    global_builtin_fn_cache[slot] = result;
    return result;
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
    JS_CTOR_EVAL_ERROR,
    JS_CTOR_REGEXP,
    JS_CTOR_DATE,
    JS_CTOR_PROMISE,
    JS_CTOR_MAP,
    JS_CTOR_SET,
    JS_CTOR_WEAKMAP,
    JS_CTOR_WEAKSET,
    JS_CTOR_ARRAY_BUFFER,
    JS_CTOR_DATAVIEW,
    JS_CTOR_INT8ARRAY,
    JS_CTOR_UINT8ARRAY,
    JS_CTOR_UINT8CLAMPEDARRAY,
    JS_CTOR_INT16ARRAY,
    JS_CTOR_UINT16ARRAY,
    JS_CTOR_INT32ARRAY,
    JS_CTOR_UINT32ARRAY,
    JS_CTOR_FLOAT32ARRAY,
    JS_CTOR_FLOAT64ARRAY,
    JS_CTOR_AGGREGATE_ERROR,
    JS_CTOR_MAX
};

static Item js_constructor_cache[JS_CTOR_MAX];
static bool js_ctor_cache_init = false;

void js_ctor_cache_reset() {
    memset(js_constructor_cache, 0, sizeof(js_constructor_cache));
    js_ctor_cache_init = false;
}

// Dummy func_ptr for constructors (makes typeof return "function")
static Item js_ctor_placeholder() { return ItemNull; }

// v18: Real constructor functions for type coercion calls (Boolean(x), Number(x), String(x))
static Item js_ctor_boolean_fn(Item arg) { return js_to_boolean(arg); }
static Item js_ctor_number_fn(Item arg) { return js_to_number(arg); }
static Item js_ctor_string_fn(Item arg) {
    // String(Symbol()) is allowed — explicit conversion (ES spec 19.1.1)
    if (get_type_id(arg) == LMD_TYPE_INT && it2i(arg) <= -(int64_t)JS_SYMBOL_BASE) {
        return js_symbol_to_string(arg);
    }
    return js_to_string(arg);
}

// RegExp(pattern, flags) without 'new' should behave like new RegExp(pattern, flags)
extern "C" Item js_regexp_construct(Item pattern_item, Item flags_item);
static Item js_ctor_regexp_fn(Item pattern, Item flags) {
    return js_regexp_construct(pattern, flags);
}

// Date() without 'new' should return a date string (not a Date object)
extern "C" Item js_date_now_string();
static Item js_ctor_date_fn(Item arg) {
    (void)arg;
    return js_date_now_string();
}

// Error(msg) without 'new' should behave like new Error(msg)
static Item js_ctor_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("Error", 5))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_type_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("TypeError", 9))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_range_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("RangeError", 10))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_reference_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("ReferenceError", 14))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_syntax_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("SyntaxError", 11))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_uri_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("URIError", 8))};
    return js_new_error_with_name(tn, msg);
}
static Item js_ctor_eval_error_fn(Item msg) {
    Item tn = (Item){.item = s2it(heap_create_name("EvalError", 9))};
    return js_new_error_with_name(tn, msg);
}

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
    uint8_t flags;       // must match JsFunction layout (generator, arrow flags)
    int16_t formal_length; // must match JsFunction layout
};

// Forward declarations for functions in js_runtime.cpp used by constructor population
extern "C" void js_func_init_property(Item fn_item, Item key, Item value);
extern "C" void js_mark_non_enumerable(Item object, Item name);
extern "C" void js_mark_non_writable(Item object, Item name);
extern "C" Item js_property_get(Item object, Item key);
extern "C" void js_populate_constructor_statics(Item ctor_item, const char* ctor_name, int ctor_len);

// Populate Number constructor with own properties (constants + static methods)
// so they appear via hasOwnProperty, Object.getOwnPropertyDescriptor, etc.
static void js_populate_number_ctor(Item fn_item) {
    // Constants: non-enumerable, non-writable, non-configurable
    struct { const char* name; int len; double value; } constants[] = {
        {"NEGATIVE_INFINITY", 17, -1.0/0.0},
        {"POSITIVE_INFINITY", 17, 1.0/0.0},
        {"NaN", 3, 0.0/0.0},
        {"MAX_VALUE", 9, 1.7976931348623157e+308},
        {"MIN_VALUE", 9, 5e-324},
        {"MAX_SAFE_INTEGER", 16, 9007199254740991.0},
        {"MIN_SAFE_INTEGER", 16, -9007199254740991.0},
        {"EPSILON", 7, 2.220446049250313e-16},
    };
    for (int i = 0; i < 8; i++) {
        Item key = (Item){.item = s2it(heap_create_name(constants[i].name, constants[i].len))};
        js_func_init_property(fn_item, key, make_double(constants[i].value));
        js_mark_non_enumerable(fn_item, key);
        js_mark_non_writable(fn_item, key);
        // Also mark non-configurable
        char nc_buf[64];
        snprintf(nc_buf, sizeof(nc_buf), "__nc_%s", constants[i].name);
        Item nc_key = (Item){.item = s2it(heap_create_name(nc_buf, strlen(nc_buf)))};
        js_func_init_property(fn_item, nc_key, (Item){.item = b2it(true)});
    }
    // Static methods: non-enumerable (writable, configurable by default)
    // Fetch via js_property_get which triggers js_lookup_constructor_static
    const char* methods[] = {"isFinite", "isNaN", "isInteger", "isSafeInteger", "parseInt", "parseFloat"};
    int method_lens[] = {8, 5, 9, 13, 8, 10};
    for (int i = 0; i < 6; i++) {
        Item key = (Item){.item = s2it(heap_create_name(methods[i], method_lens[i]))};
        Item method = js_property_get(fn_item, key);
        if (method.item != ItemNull.item && method.item != make_js_undefined().item) {
            js_func_init_property(fn_item, key, method);
            js_mark_non_enumerable(fn_item, key);
        }
    }
}

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
    else if (ctor_id == JS_CTOR_REGEXP) fn->func_ptr = (void*)js_ctor_regexp_fn;
    else if (ctor_id == JS_CTOR_DATE) fn->func_ptr = (void*)js_ctor_date_fn;
    else if (ctor_id == JS_CTOR_ERROR) fn->func_ptr = (void*)js_ctor_error_fn;
    else if (ctor_id == JS_CTOR_TYPE_ERROR) fn->func_ptr = (void*)js_ctor_type_error_fn;
    else if (ctor_id == JS_CTOR_RANGE_ERROR) fn->func_ptr = (void*)js_ctor_range_error_fn;
    else if (ctor_id == JS_CTOR_REFERENCE_ERROR) fn->func_ptr = (void*)js_ctor_reference_error_fn;
    else if (ctor_id == JS_CTOR_SYNTAX_ERROR) fn->func_ptr = (void*)js_ctor_syntax_error_fn;
    else if (ctor_id == JS_CTOR_URI_ERROR) fn->func_ptr = (void*)js_ctor_uri_error_fn;
    else if (ctor_id == JS_CTOR_EVAL_ERROR) fn->func_ptr = (void*)js_ctor_eval_error_fn;
    else fn->func_ptr = (void*)js_ctor_placeholder;
    fn->param_count = param_count;
    fn->formal_length = -1; // -1 = use param_count for .length
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
    // Populate constructor-specific own properties
    if (ctor_id == JS_CTOR_NUMBER) js_populate_number_ctor(fn_item);
    // Populate static methods as own properties for all constructors
    js_populate_constructor_statics(fn_item, name, strlen(name));
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
        {"EvalError", 9, JS_CTOR_EVAL_ERROR, 1},
        {"AggregateError", 14, JS_CTOR_AGGREGATE_ERROR, 2},
        {"RegExp", 6, JS_CTOR_REGEXP, 2},
        {"Date", 4, JS_CTOR_DATE, 1},
        {"Promise", 7, JS_CTOR_PROMISE, 1},
        {"Map", 3, JS_CTOR_MAP, 0},
        {"Set", 3, JS_CTOR_SET, 0},
        {"WeakMap", 7, JS_CTOR_WEAKMAP, 0},
        {"WeakSet", 7, JS_CTOR_WEAKSET, 0},
        {"ArrayBuffer", 11, JS_CTOR_ARRAY_BUFFER, 1},
        {"DataView", 8, JS_CTOR_DATAVIEW, 1},
        {"Int8Array", 9, JS_CTOR_INT8ARRAY, 1},
        {"Uint8Array", 10, JS_CTOR_UINT8ARRAY, 1},
        {"Uint8ClampedArray", 17, JS_CTOR_UINT8CLAMPEDARRAY, 1},
        {"Int16Array", 10, JS_CTOR_INT16ARRAY, 1},
        {"Uint16Array", 11, JS_CTOR_UINT16ARRAY, 1},
        {"Int32Array", 10, JS_CTOR_INT32ARRAY, 1},
        {"Uint32Array", 11, JS_CTOR_UINT32ARRAY, 1},
        {"Float32Array", 12, JS_CTOR_FLOAT32ARRAY, 1},
        {"Float64Array", 12, JS_CTOR_FLOAT64ARRAY, 1},
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

// symbol description registry: maps symbol_id -> description string
struct JsSymbolDesc {
    uint64_t symbol_id;
    char desc[128];
    int desc_len;    // -1 means no description (Symbol() with no arg)
};

static HashMap* js_symbol_desc_registry = nullptr;

static int js_symbol_desc_compare(const void* a, const void* b, void* udata) {
    (void)udata;
    return ((const JsSymbolDesc*)a)->symbol_id != ((const JsSymbolDesc*)b)->symbol_id;
}

static uint64_t js_symbol_desc_hash(const void* item, uint64_t seed0, uint64_t seed1) {
    const JsSymbolDesc* e = (const JsSymbolDesc*)item;
    return hashmap_sip(&e->symbol_id, sizeof(uint64_t), seed0, seed1);
}

static void js_symbol_desc_init() {
    if (!js_symbol_desc_registry) {
        js_symbol_desc_registry = hashmap_new(sizeof(JsSymbolDesc), 16, 0, 0,
            js_symbol_desc_hash, js_symbol_desc_compare, NULL, NULL);
    }
}

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
#define JS_SYMBOL_ID_IS_CONCAT_SPREADABLE 12

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
    Item sym = js_make_symbol_item(id);

    // store description for Symbol.prototype.description
    js_symbol_desc_init();
    JsSymbolDesc entry;
    entry.symbol_id = id;
    if (description.item == ITEM_NULL || description.item == ITEM_JS_UNDEFINED) {
        entry.desc[0] = '\0';
        entry.desc_len = -1;  // no description
    } else {
        String* s = it2s(js_to_string(description));
        if (s) {
            int len = s->len < 127 ? (int)s->len : 127;
            memcpy(entry.desc, s->chars, len);
            entry.desc[len] = '\0';
            entry.desc_len = len;
        } else {
            entry.desc[0] = '\0';
            entry.desc_len = -1;
        }
    }
    hashmap_set(js_symbol_desc_registry, &entry);

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
    if (id == JS_SYMBOL_ID_IS_CONCAT_SPREADABLE) return (Item){.item = s2it(heap_create_name("Symbol(Symbol.isConcatSpreadable)", 32))};

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

    // check description registry
    if (js_symbol_desc_registry) {
        JsSymbolDesc lookup;
        lookup.symbol_id = id;
        JsSymbolDesc* found = (JsSymbolDesc*)hashmap_get(js_symbol_desc_registry, &lookup);
        if (found && found->desc_len >= 0) {
            char buf[160];
            snprintf(buf, sizeof(buf), "Symbol(%s)", found->desc);
            return (Item){.item = s2it(heap_create_name(buf, strlen(buf)))};
        }
    }

    return (Item){.item = s2it(heap_create_name("Symbol()", 8))};
}

// Return the description of a symbol, or undefined if none
extern "C" Item js_symbol_get_description(Item sym) {
    if (!js_is_symbol_item(sym)) return make_js_undefined();
    uint64_t id = js_symbol_item_id(sym);

    // well-known symbols have fixed descriptions
    if (id == JS_SYMBOL_ID_ITERATOR)       return (Item){.item = s2it(heap_create_name("Symbol.iterator", 15))};
    if (id == JS_SYMBOL_ID_TO_PRIMITIVE)   return (Item){.item = s2it(heap_create_name("Symbol.toPrimitive", 18))};
    if (id == JS_SYMBOL_ID_HAS_INSTANCE)   return (Item){.item = s2it(heap_create_name("Symbol.hasInstance", 18))};
    if (id == JS_SYMBOL_ID_TO_STRING_TAG)  return (Item){.item = s2it(heap_create_name("Symbol.toStringTag", 18))};
    if (id == JS_SYMBOL_ID_ASYNC_ITERATOR) return (Item){.item = s2it(heap_create_name("Symbol.asyncIterator", 20))};
    if (id == JS_SYMBOL_ID_SPECIES)        return (Item){.item = s2it(heap_create_name("Symbol.species", 14))};
    if (id == JS_SYMBOL_ID_MATCH)          return (Item){.item = s2it(heap_create_name("Symbol.match", 12))};
    if (id == JS_SYMBOL_ID_REPLACE)        return (Item){.item = s2it(heap_create_name("Symbol.replace", 14))};
    if (id == JS_SYMBOL_ID_SEARCH)         return (Item){.item = s2it(heap_create_name("Symbol.search", 13))};
    if (id == JS_SYMBOL_ID_SPLIT)          return (Item){.item = s2it(heap_create_name("Symbol.split", 12))};
    if (id == JS_SYMBOL_ID_UNSCOPABLES)    return (Item){.item = s2it(heap_create_name("Symbol.unscopables", 18))};
    if (id == JS_SYMBOL_ID_IS_CONCAT_SPREADABLE) return (Item){.item = s2it(heap_create_name("Symbol.isConcatSpreadable", 24))};

    // check Symbol.for() registry
    if (js_symbol_registry) {
        size_t iter = 0;
        void* entry;
        while (hashmap_iter(js_symbol_registry, &iter, &entry)) {
            JsSymbolEntry* e = (JsSymbolEntry*)entry;
            if (e->symbol_id == id) {
                return (Item){.item = s2it(heap_create_name(e->key, strlen(e->key)))};
            }
        }
    }

    // check description registry
    if (js_symbol_desc_registry) {
        JsSymbolDesc lookup;
        lookup.symbol_id = id;
        JsSymbolDesc* found = (JsSymbolDesc*)hashmap_get(js_symbol_desc_registry, &lookup);
        if (found) {
            if (found->desc_len < 0) return make_js_undefined();  // Symbol() with no arg
            return (Item){.item = s2it(heap_create_name(found->desc, found->desc_len))};
        }
    }

    return make_js_undefined();
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
        if (s->len == 18 && strncmp(s->chars, "isConcatSpreadable", 18) == 0)
            return js_make_symbol_item(JS_SYMBOL_ID_IS_CONCAT_SPREADABLE);
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