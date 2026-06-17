#include "js_exec_profile.h"

#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/strbuf.h"
#include "../../lib/time_util.h"
#include "../../lib/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <process.h>
#define js_profile_getpid _getpid
#else
#include <unistd.h>
#define js_profile_getpid getpid
#endif

typedef struct JsExecProfileSlot {
    const char* name;
    uint64_t calls;
    uint64_t inclusive_ns;
    uint64_t self_ns;
    uint64_t mir_sites;
} JsExecProfileSlot;

typedef struct JsExecProfileFrame {
    JsExecProfileEvent event;
    uint64_t start_ns;
    uint64_t child_ns;
} JsExecProfileFrame;

typedef struct JsExecProfileMirCall {
    const char* name;
    uint64_t sites;
} JsExecProfileMirCall;

int g_js_exec_profile_mode = -1;

static JsExecProfileSlot g_js_exec_profile_slots[JS_EXEC_PROF_EVENT_COUNT] = {
    {"property_get", 0, 0, 0, 0},
    {"property_set", 0, 0, 0, 0},
    {"property_access", 0, 0, 0, 0},
    {"array_get_int", 0, 0, 0, 0},
    {"array_set_int", 0, 0, 0, 0},
    {"array_push", 0, 0, 0, 0},
    {"call_function", 0, 0, 0, 0},
    {"dispatch_builtin", 0, 0, 0, 0},
    {"new_object", 0, 0, 0, 0},
    {"new_object_shape", 0, 0, 0, 0},
    {"get_slot_f", 0, 0, 0, 0},
    {"get_slot_i", 0, 0, 0, 0},
    {"set_slot_f", 0, 0, 0, 0},
    {"set_slot_i", 0, 0, 0, 0},
    {"box_float", 0, 0, 0, 0},
    {"unbox_int", 0, 0, 0, 0},
    {"unbox_float", 0, 0, 0, 0},
    {"coerce", 0, 0, 0, 0},
    {"other_runtime_call", 0, 0, 0, 0},
};

static JsExecProfileFrame g_js_exec_profile_stack[4096];
static JsExecProfileMirCall g_js_exec_profile_mir_calls[1024];
static int g_js_exec_profile_stack_depth = 0;
static int g_js_exec_profile_mir_call_count = 0;
static int g_js_exec_profile_registered = 0;

static int js_exec_profile_truthy(const char* s) {
    return s && s[0] && strcmp(s, "0") != 0 && strcmp(s, "false") != 0 &&
        strcmp(s, "off") != 0 && strcmp(s, "no") != 0;
}

int js_exec_profile_mode(void) {
    if (g_js_exec_profile_mode >= 0) return g_js_exec_profile_mode;
    const char* env = getenv("JS_EXEC_PROFILE");
    if (!js_exec_profile_truthy(env)) {
        g_js_exec_profile_mode = 0;
        return 0;
    }
    g_js_exec_profile_mode =
        (strcmp(env, "time") == 0 || strcmp(env, "2") == 0) ? 2 : 1;
    if (!g_js_exec_profile_registered) {
        atexit(js_exec_profile_dump);
        g_js_exec_profile_registered = 1;
    }
    return g_js_exec_profile_mode;
}

void js_exec_profile_reset(void) {
    for (int i = 0; i < JS_EXEC_PROF_EVENT_COUNT; i++) {
        g_js_exec_profile_slots[i].calls = 0;
        g_js_exec_profile_slots[i].inclusive_ns = 0;
        g_js_exec_profile_slots[i].self_ns = 0;
        g_js_exec_profile_slots[i].mir_sites = 0;
    }
    g_js_exec_profile_stack_depth = 0;
    g_js_exec_profile_mir_call_count = 0;
}

static void js_exec_profile_note_mir_call_name(const char* fn_name) {
    if (!fn_name || !fn_name[0]) return;
    for (int i = 0; i < g_js_exec_profile_mir_call_count; i++) {
        if (strcmp(g_js_exec_profile_mir_calls[i].name, fn_name) == 0) {
            g_js_exec_profile_mir_calls[i].sites++;
            return;
        }
    }
    if (g_js_exec_profile_mir_call_count >= (int)(sizeof(g_js_exec_profile_mir_calls) / sizeof(g_js_exec_profile_mir_calls[0]))) {
        return;
    }
    int index = g_js_exec_profile_mir_call_count++;
    g_js_exec_profile_mir_calls[index].name = fn_name;
    g_js_exec_profile_mir_calls[index].sites = 1;
}

uint64_t js_exec_profile_enter(JsExecProfileEvent event) {
    if (event < 0 || event >= JS_EXEC_PROF_EVENT_COUNT) return 0;
    g_js_exec_profile_slots[event].calls++;
    if (g_js_exec_profile_mode < 2) return 0;
    if (g_js_exec_profile_stack_depth >= (int)(sizeof(g_js_exec_profile_stack) / sizeof(g_js_exec_profile_stack[0]))) {
        return 0;
    }
    int frame_index = g_js_exec_profile_stack_depth++;
    g_js_exec_profile_stack[frame_index].event = event;
    g_js_exec_profile_stack[frame_index].start_ns = time_now_ns();
    g_js_exec_profile_stack[frame_index].child_ns = 0;
    return (uint64_t)frame_index + 1;
}

void js_exec_profile_leave(JsExecProfileEvent event, uint64_t token) {
    if (event < 0 || event >= JS_EXEC_PROF_EVENT_COUNT) return;
    if (g_js_exec_profile_mode < 2 || token == 0) return;
    int frame_index = (int)(token - 1);
    if (frame_index < 0 || frame_index >= g_js_exec_profile_stack_depth) return;
    if (frame_index != g_js_exec_profile_stack_depth - 1) return;
    JsExecProfileFrame* frame = &g_js_exec_profile_stack[frame_index];
    if (frame->event != event) return;
    uint64_t now = time_now_ns();
    if (now >= frame->start_ns) {
        uint64_t elapsed_ns = now - frame->start_ns;
        uint64_t self_ns = elapsed_ns >= frame->child_ns ? elapsed_ns - frame->child_ns : 0;
        g_js_exec_profile_slots[event].inclusive_ns += elapsed_ns;
        g_js_exec_profile_slots[event].self_ns += self_ns;
        g_js_exec_profile_stack_depth--;
        if (g_js_exec_profile_stack_depth > 0) {
            g_js_exec_profile_stack[g_js_exec_profile_stack_depth - 1].child_ns += elapsed_ns;
        }
    }
}

void js_exec_profile_count(JsExecProfileEvent event) {
    int mode = g_js_exec_profile_mode >= 0 ? g_js_exec_profile_mode : js_exec_profile_mode();
    if (mode <= 0 || event < 0 || event >= JS_EXEC_PROF_EVENT_COUNT) return;
    g_js_exec_profile_slots[event].calls++;
}

static JsExecProfileEvent js_exec_profile_event_for_runtime_call(const char* fn_name) {
    if (!fn_name) return JS_EXEC_PROF_OTHER_RUNTIME_CALL;
    if (strcmp(fn_name, "js_property_get") == 0 ||
        strcmp(fn_name, "js_super_property_get") == 0 ||
        strcmp(fn_name, "js_get_global_property") == 0 ||
        strcmp(fn_name, "js_get_module_var") == 0) {
        return JS_EXEC_PROF_PROPERTY_GET;
    }
    if (strcmp(fn_name, "js_property_set") == 0 ||
        strcmp(fn_name, "js_property_set_v") == 0 ||
        strcmp(fn_name, "js_super_property_set") == 0 ||
        strcmp(fn_name, "js_set_module_var") == 0) {
        return JS_EXEC_PROF_PROPERTY_SET;
    }
    if (strcmp(fn_name, "js_property_access") == 0) return JS_EXEC_PROF_PROPERTY_ACCESS;
    if (strcmp(fn_name, "js_array_get_int") == 0) return JS_EXEC_PROF_ARRAY_GET_INT;
    if (strcmp(fn_name, "js_array_set_int") == 0) return JS_EXEC_PROF_ARRAY_SET_INT;
    if (strcmp(fn_name, "js_array_push") == 0) return JS_EXEC_PROF_ARRAY_PUSH;
    if (strcmp(fn_name, "js_call_function") == 0) return JS_EXEC_PROF_CALL_FUNCTION;
    if (strcmp(fn_name, "js_new_object") == 0) return JS_EXEC_PROF_NEW_OBJECT;
    if (strcmp(fn_name, "js_new_object_with_shape") == 0) return JS_EXEC_PROF_NEW_OBJECT_SHAPE;
    if (strcmp(fn_name, "js_get_slot_f") == 0) return JS_EXEC_PROF_GET_SLOT_F;
    if (strcmp(fn_name, "js_get_slot_i") == 0) return JS_EXEC_PROF_GET_SLOT_I;
    if (strcmp(fn_name, "js_set_slot_f") == 0) return JS_EXEC_PROF_SET_SLOT_F;
    if (strcmp(fn_name, "js_set_slot_i") == 0) return JS_EXEC_PROF_SET_SLOT_I;
    if (strcmp(fn_name, "push_d") == 0 ||
        strcmp(fn_name, "js_profiled_push_d") == 0) {
        return JS_EXEC_PROF_BOX_FLOAT;
    }
    if (strcmp(fn_name, "it2i") == 0 ||
        strcmp(fn_name, "js_profiled_it2i") == 0) {
        return JS_EXEC_PROF_UNBOX_INT;
    }
    if (strcmp(fn_name, "it2d") == 0 ||
        strcmp(fn_name, "js_profiled_it2d") == 0) {
        return JS_EXEC_PROF_UNBOX_FLOAT;
    }
    if (strncmp(fn_name, "js_to_", 6) == 0 || strncmp(fn_name, "js_coerce", 9) == 0) {
        return JS_EXEC_PROF_COERCE;
    }
    return JS_EXEC_PROF_OTHER_RUNTIME_CALL;
}

void js_exec_profile_note_mir_call(const char* fn_name) {
    int mode = g_js_exec_profile_mode >= 0 ? g_js_exec_profile_mode : js_exec_profile_mode();
    if (mode <= 0) return;
    js_exec_profile_note_mir_call_name(fn_name);
    JsExecProfileEvent event = js_exec_profile_event_for_runtime_call(fn_name);
    if (event < 0 || event >= JS_EXEC_PROF_EVENT_COUNT) return;
    g_js_exec_profile_slots[event].mir_sites++;
}

void js_exec_profile_dump(void) {
    if (g_js_exec_profile_mode <= 0) return;
    create_dir_recursive("temp");

    char default_path[128];
    snprintf(default_path, sizeof(default_path), "temp/js_exec_profile_%ld.tsv", (long)js_profile_getpid());
    const char* out_path = getenv("JS_EXEC_PROFILE_OUT");
    if (!out_path || !out_path[0]) out_path = default_path;

    StrBuf* buf = strbuf_new();
    if (!buf) return;
    strbuf_append_str(buf, "# JS_EXEC_PROFILE=");
    strbuf_append_int(buf, g_js_exec_profile_mode);
    strbuf_append_str(buf, "\n");
    strbuf_append_str(buf, "event\tcalls\tinclusive_ms\tself_ms\tavg_self_ns\tmir_sites\n");
    for (int i = 0; i < JS_EXEC_PROF_EVENT_COUNT; i++) {
        JsExecProfileSlot* slot = &g_js_exec_profile_slots[i];
        if (slot->calls == 0 && slot->mir_sites == 0) continue;
        uint64_t avg_self_ns = slot->calls ? slot->self_ns / slot->calls : 0;
        strbuf_append_str(buf, slot->name);
        strbuf_append_char(buf, '\t');
        strbuf_append_uint64(buf, slot->calls);
        strbuf_append_char(buf, '\t');
        strbuf_append_format(buf, "%.3f", (double)slot->inclusive_ns / 1000000.0);
        strbuf_append_char(buf, '\t');
        strbuf_append_format(buf, "%.3f", (double)slot->self_ns / 1000000.0);
        strbuf_append_char(buf, '\t');
        strbuf_append_uint64(buf, avg_self_ns);
        strbuf_append_char(buf, '\t');
        strbuf_append_uint64(buf, slot->mir_sites);
        strbuf_append_char(buf, '\n');
    }
    if (g_js_exec_profile_mir_call_count > 0) {
        strbuf_append_str(buf, "\n# MIR runtime call sites by helper\n");
        strbuf_append_str(buf, "runtime_call\tmir_sites\n");
        for (int i = 0; i < g_js_exec_profile_mir_call_count; i++) {
            strbuf_append_str(buf, g_js_exec_profile_mir_calls[i].name);
            strbuf_append_char(buf, '\t');
            strbuf_append_uint64(buf, g_js_exec_profile_mir_calls[i].sites);
            strbuf_append_char(buf, '\n');
        }
    }

    if (write_text_file_atomic(out_path, buf->str ? buf->str : "") != 0) {
        log_error("js-exec-profile: failed to write '%s'", out_path);
    }
    strbuf_free(buf);
}
