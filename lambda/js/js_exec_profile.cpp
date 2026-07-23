#include "js_exec_profile.h"

#ifdef LAMBDA_JS_EXEC_PROFILE

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
    char name[192];
    uint64_t sites;
} JsExecProfileMirCall;

typedef struct JsExecProfileShapeGuardSite {
    char label[96];
    uintptr_t expected_shape;
    uint64_t hits;
    uint64_t misses;
    uintptr_t miss_shapes[4];
    uint64_t miss_counts[4];
} JsExecProfileShapeGuardSite;

typedef struct JsExecProfilePropertySetSite {
    char label[128];
    uint64_t count;
} JsExecProfilePropertySetSite;

typedef struct JsExecProfilePropertySetBranch {
    char label[96];
    uint64_t calls;
    uint64_t inclusive_ns;
} JsExecProfilePropertySetBranch;

typedef struct JsExecProfileLoadICSite {
    char label[128];
    uint64_t counts[JS_LOAD_IC_SITE_REASON_COUNT];
} JsExecProfileLoadICSite;

typedef struct JsExecProfileStoreICSite {
    char label[128];
    uint64_t counts[JS_STORE_IC_SITE_REASON_COUNT];
} JsExecProfileStoreICSite;

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
    {"shape_slot_guard", 0, 0, 0, 0},
    {"shape_guard_hit", 0, 0, 0, 0},
    {"shape_guard_miss", 0, 0, 0, 0},
    {"load_ic_probe", 0, 0, 0, 0},
    {"load_ic_hit_mono", 0, 0, 0, 0},
    {"load_ic_hit_poly", 0, 0, 0, 0},
    {"load_ic_miss", 0, 0, 0, 0},
    {"load_ic_install_mono", 0, 0, 0, 0},
    {"load_ic_install_poly", 0, 0, 0, 0},
    {"load_ic_megamorphic", 0, 0, 0, 0},
    {"store_ic_probe", 0, 0, 0, 0},
    {"store_ic_hit_mono", 0, 0, 0, 0},
    {"store_ic_hit_poly", 0, 0, 0, 0},
    {"store_ic_miss", 0, 0, 0, 0},
    {"store_ic_install_mono", 0, 0, 0, 0},
    {"store_ic_install_poly", 0, 0, 0, 0},
    {"store_ic_megamorphic", 0, 0, 0, 0},
    {"box_float", 0, 0, 0, 0},
    {"unbox_int", 0, 0, 0, 0},
    {"unbox_float", 0, 0, 0, 0},
    {"coerce", 0, 0, 0, 0},
    {"other_runtime_call", 0, 0, 0, 0},
};

static JsExecProfileFrame g_js_exec_profile_stack[4096];
static JsExecProfileMirCall g_js_exec_profile_mir_calls[1024];
static JsExecProfileShapeGuardSite g_js_exec_profile_shape_guard_sites[128];
static JsExecProfilePropertySetSite g_js_exec_profile_property_set_sites[256];
static JsExecProfilePropertySetBranch g_js_exec_profile_property_set_branches[64];
static JsExecProfileLoadICSite g_js_exec_profile_load_ic_sites[512];
static JsExecProfileStoreICSite g_js_exec_profile_store_ic_sites[512];
static int g_js_exec_profile_stack_depth = 0;
static int g_js_exec_profile_mir_call_count = 0;
static int g_js_exec_profile_shape_guard_site_count = 0;
static int g_js_exec_profile_property_set_site_count = 0;
static int g_js_exec_profile_property_set_branch_count = 0;
static int g_js_exec_profile_load_ic_site_count = 0;
static int g_js_exec_profile_store_ic_site_count = 0;
static int g_js_exec_profile_registered = 0;
static bool g_js_exec_profile_dumped = false;

static const char* g_js_load_ic_reason_names[JS_LOAD_IC_SITE_REASON_COUNT] = {
    "probe",
    "hit_mono",
    "hit_poly",
    "miss_key",
    "miss_not_map",
    "miss_not_plain",
    "miss_no_data",
    "miss_bad_typemap",
    "miss_not_found",
    "miss_name",
    "miss_flags",
    "miss_offset",
    "miss_deleted",
    "install_mono",
    "install_poly",
    "megamorphic",
};

static const char* g_js_store_ic_reason_names[JS_STORE_IC_SITE_REASON_COUNT] = {
    "probe",
    "hit_mono",
    "hit_poly",
    "miss_key",
    "miss_not_map",
    "miss_not_plain",
    "miss_no_data",
    "miss_bad_typemap",
    "miss_not_found",
    "miss_name",
    "miss_flags",
    "miss_offset",
    "miss_deleted",
    "miss_type",
    "install_mono",
    "install_poly",
    "megamorphic",
};

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
    g_js_exec_profile_dumped = false;
    for (int i = 0; i < JS_EXEC_PROF_EVENT_COUNT; i++) {
        g_js_exec_profile_slots[i].calls = 0;
        g_js_exec_profile_slots[i].inclusive_ns = 0;
        g_js_exec_profile_slots[i].self_ns = 0;
        g_js_exec_profile_slots[i].mir_sites = 0;
    }
    g_js_exec_profile_stack_depth = 0;
    g_js_exec_profile_mir_call_count = 0;
    g_js_exec_profile_shape_guard_site_count = 0;
    g_js_exec_profile_property_set_site_count = 0;
    g_js_exec_profile_property_set_branch_count = 0;
    g_js_exec_profile_load_ic_site_count = 0;
    g_js_exec_profile_store_ic_site_count = 0;
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
    // Exact transpiler collections are compile-lifetime pool allocations; the
    // profiler outlives them until runtime cleanup and therefore owns the label.
    snprintf(g_js_exec_profile_mir_calls[index].name,
        sizeof(g_js_exec_profile_mir_calls[index].name), "%s", fn_name);
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

extern "C" void js_profile_shape_guard_hit(void) {
    js_exec_profile_count(JS_EXEC_PROF_SHAPE_GUARD_HIT);
}

extern "C" void js_profile_shape_guard_miss(void) {
    js_exec_profile_count(JS_EXEC_PROF_SHAPE_GUARD_MISS);
}

static JsExecProfileShapeGuardSite* js_exec_profile_shape_guard_site(const char* label,
        void* expected_shape) {
    const char* safe_label = (label && label[0]) ? label : "unknown";
    uintptr_t expected = (uintptr_t)expected_shape;
    for (int i = 0; i < g_js_exec_profile_shape_guard_site_count; i++) {
        JsExecProfileShapeGuardSite* site = &g_js_exec_profile_shape_guard_sites[i];
        if (site->expected_shape == expected && strcmp(site->label, safe_label) == 0) return site;
    }
    if (g_js_exec_profile_shape_guard_site_count >=
            (int)(sizeof(g_js_exec_profile_shape_guard_sites) / sizeof(g_js_exec_profile_shape_guard_sites[0]))) {
        return NULL;
    }
    JsExecProfileShapeGuardSite* site =
        &g_js_exec_profile_shape_guard_sites[g_js_exec_profile_shape_guard_site_count++];
    memset(site, 0, sizeof(*site));
    strncpy(site->label, safe_label, sizeof(site->label) - 1);
    site->expected_shape = expected;
    return site;
}

static void js_exec_profile_shape_guard_note(const char* label, void* expected_shape,
        void* actual_shape, bool hit) {
    int mode = g_js_exec_profile_mode >= 0 ? g_js_exec_profile_mode : js_exec_profile_mode();
    if (mode <= 0) return;
    JsExecProfileShapeGuardSite* site =
        js_exec_profile_shape_guard_site(label, expected_shape);
    if (!site) return;
    if (hit) {
        site->hits++;
        return;
    }
    site->misses++;
    uintptr_t actual = (uintptr_t)actual_shape;
    for (int i = 0; i < (int)(sizeof(site->miss_shapes) / sizeof(site->miss_shapes[0])); i++) {
        if (site->miss_counts[i] == 0 || site->miss_shapes[i] == actual) {
            site->miss_shapes[i] = actual;
            site->miss_counts[i]++;
            return;
        }
    }
}

extern "C" void js_profile_shape_guard_hit_site(const char* label,
        void* expected_shape, void* actual_shape) {
    js_exec_profile_count(JS_EXEC_PROF_SHAPE_GUARD_HIT);
    js_exec_profile_shape_guard_note(label, expected_shape, actual_shape, true);
}

extern "C" void js_profile_shape_guard_miss_site(const char* label,
        void* expected_shape, void* actual_shape) {
    js_exec_profile_count(JS_EXEC_PROF_SHAPE_GUARD_MISS);
    js_exec_profile_shape_guard_note(label, expected_shape, actual_shape, false);
}

extern "C" void js_profile_property_set_site(const char* label) {
    int mode = g_js_exec_profile_mode >= 0 ? g_js_exec_profile_mode : js_exec_profile_mode();
    if (mode <= 0) return;
    const char* safe_label = (label && label[0]) ? label : "unknown";
    for (int i = 0; i < g_js_exec_profile_property_set_site_count; i++) {
        JsExecProfilePropertySetSite* site = &g_js_exec_profile_property_set_sites[i];
        if (strcmp(site->label, safe_label) == 0) {
            site->count++;
            return;
        }
    }
    if (g_js_exec_profile_property_set_site_count >=
            (int)(sizeof(g_js_exec_profile_property_set_sites) / sizeof(g_js_exec_profile_property_set_sites[0]))) {
        return;
    }
    JsExecProfilePropertySetSite* site =
        &g_js_exec_profile_property_set_sites[g_js_exec_profile_property_set_site_count++];
    memset(site, 0, sizeof(*site));
    strncpy(site->label, safe_label, sizeof(site->label) - 1);
    site->count = 1;
}

static JsExecProfilePropertySetBranch* js_exec_profile_property_set_branch(const char* label) {
    const char* safe_label = (label && label[0]) ? label : "unknown";
    for (int i = 0; i < g_js_exec_profile_property_set_branch_count; i++) {
        JsExecProfilePropertySetBranch* branch = &g_js_exec_profile_property_set_branches[i];
        if (strcmp(branch->label, safe_label) == 0) return branch;
    }
    if (g_js_exec_profile_property_set_branch_count >=
            (int)(sizeof(g_js_exec_profile_property_set_branches) / sizeof(g_js_exec_profile_property_set_branches[0]))) {
        return NULL;
    }
    JsExecProfilePropertySetBranch* branch =
        &g_js_exec_profile_property_set_branches[g_js_exec_profile_property_set_branch_count++];
    memset(branch, 0, sizeof(*branch));
    strncpy(branch->label, safe_label, sizeof(branch->label) - 1);
    return branch;
}

extern "C" uint64_t js_profile_property_set_branch_enter(const char* label) {
    int mode = g_js_exec_profile_mode >= 0 ? g_js_exec_profile_mode : js_exec_profile_mode();
    if (mode <= 0) return 0;
    JsExecProfilePropertySetBranch* branch = js_exec_profile_property_set_branch(label);
    if (!branch) return 0;
    branch->calls++;
    if (mode < 2) return 1;
    return time_now_ns();
}

extern "C" void js_profile_property_set_branch_leave(const char* label, uint64_t token) {
    int mode = g_js_exec_profile_mode >= 0 ? g_js_exec_profile_mode : js_exec_profile_mode();
    if (mode < 2 || token == 0) return;
    JsExecProfilePropertySetBranch* branch = js_exec_profile_property_set_branch(label);
    if (!branch) return;
    uint64_t now = time_now_ns();
    if (now >= token) branch->inclusive_ns += now - token;
}

extern "C" void js_profile_property_set_branch_add_count(const char* label, uint64_t count) {
    int mode = g_js_exec_profile_mode >= 0 ? g_js_exec_profile_mode : js_exec_profile_mode();
    if (mode <= 0 || count == 0) return;
    JsExecProfilePropertySetBranch* branch = js_exec_profile_property_set_branch(label);
    if (!branch) return;
    branch->calls += count;
}

extern "C" void js_profile_load_ic_site(const char* label, JsLoadICProfileReason reason) {
    int mode = g_js_exec_profile_mode >= 0 ? g_js_exec_profile_mode : js_exec_profile_mode();
    if (mode <= 0 || reason < 0 || reason >= JS_LOAD_IC_SITE_REASON_COUNT) return;
    const char* safe_label = (label && label[0]) ? label : "unknown";
    for (int i = 0; i < g_js_exec_profile_load_ic_site_count; i++) {
        JsExecProfileLoadICSite* site = &g_js_exec_profile_load_ic_sites[i];
        if (strcmp(site->label, safe_label) == 0) {
            site->counts[reason]++;
            return;
        }
    }
    if (g_js_exec_profile_load_ic_site_count >=
            (int)(sizeof(g_js_exec_profile_load_ic_sites) / sizeof(g_js_exec_profile_load_ic_sites[0]))) {
        return;
    }
    JsExecProfileLoadICSite* site =
        &g_js_exec_profile_load_ic_sites[g_js_exec_profile_load_ic_site_count++];
    memset(site, 0, sizeof(*site));
    strncpy(site->label, safe_label, sizeof(site->label) - 1);
    site->counts[reason] = 1;
}

extern "C" void js_profile_store_ic_site(const char* label, JsStoreICProfileReason reason) {
    int mode = g_js_exec_profile_mode >= 0 ? g_js_exec_profile_mode : js_exec_profile_mode();
    if (mode <= 0 || reason < 0 || reason >= JS_STORE_IC_SITE_REASON_COUNT) return;
    const char* safe_label = (label && label[0]) ? label : "unknown";
    for (int i = 0; i < g_js_exec_profile_store_ic_site_count; i++) {
        JsExecProfileStoreICSite* site = &g_js_exec_profile_store_ic_sites[i];
        if (strcmp(site->label, safe_label) == 0) {
            site->counts[reason]++;
            return;
        }
    }
    if (g_js_exec_profile_store_ic_site_count >=
            (int)(sizeof(g_js_exec_profile_store_ic_sites) / sizeof(g_js_exec_profile_store_ic_sites[0]))) {
        return;
    }
    JsExecProfileStoreICSite* site =
        &g_js_exec_profile_store_ic_sites[g_js_exec_profile_store_ic_site_count++];
    memset(site, 0, sizeof(*site));
    strncpy(site->label, safe_label, sizeof(site->label) - 1);
    site->counts[reason] = 1;
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
        strcmp(fn_name, "js_property_set_named_ic") == 0 ||
        strcmp(fn_name, "js_super_property_set") == 0 ||
        strcmp(fn_name, "js_set_module_var") == 0) {
        return JS_EXEC_PROF_PROPERTY_SET;
    }
    if (strcmp(fn_name, "js_property_access") == 0 ||
        strcmp(fn_name, "js_property_access_named_ic") == 0) return JS_EXEC_PROF_PROPERTY_ACCESS;
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
    if (strcmp(fn_name, "js_shape_slot_guard") == 0) return JS_EXEC_PROF_SHAPE_SLOT_GUARD;
    if (strcmp(fn_name, "js_profile_shape_guard_hit") == 0) return JS_EXEC_PROF_SHAPE_GUARD_HIT;
    if (strcmp(fn_name, "js_profile_shape_guard_miss") == 0) return JS_EXEC_PROF_SHAPE_GUARD_MISS;
    if (strcmp(fn_name, "js_profile_shape_guard_hit_site") == 0) return JS_EXEC_PROF_SHAPE_GUARD_HIT;
    if (strcmp(fn_name, "js_profile_shape_guard_miss_site") == 0) return JS_EXEC_PROF_SHAPE_GUARD_MISS;
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
    if (g_js_exec_profile_mode <= 0 || g_js_exec_profile_dumped) return;
    // MIR-call labels can be pool-owned by the transpiler; runtime cleanup must
    // flush them before pool destruction, while atexit remains a safe fallback.
    g_js_exec_profile_dumped = true;
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
    if (g_js_exec_profile_property_set_site_count > 0) {
        strbuf_append_str(buf, "\n# Property set sites\n");
        strbuf_append_str(buf, "property_set_site\tcount\n");
        for (int i = 0; i < g_js_exec_profile_property_set_site_count; i++) {
            JsExecProfilePropertySetSite* site = &g_js_exec_profile_property_set_sites[i];
            strbuf_append_str(buf, site->label);
            strbuf_append_char(buf, '\t');
            strbuf_append_uint64(buf, site->count);
            strbuf_append_char(buf, '\n');
        }
    }
    if (g_js_exec_profile_property_set_branch_count > 0) {
        strbuf_append_str(buf, "\n# Property set branches\n");
        strbuf_append_str(buf, "property_set_branch\tcalls\tinclusive_ms\tavg_ns\n");
        for (int i = 0; i < g_js_exec_profile_property_set_branch_count; i++) {
            JsExecProfilePropertySetBranch* branch = &g_js_exec_profile_property_set_branches[i];
            uint64_t avg_ns = branch->calls ? branch->inclusive_ns / branch->calls : 0;
            strbuf_append_str(buf, branch->label);
            strbuf_append_char(buf, '\t');
            strbuf_append_uint64(buf, branch->calls);
            strbuf_append_char(buf, '\t');
            strbuf_append_format(buf, "%.3f", (double)branch->inclusive_ns / 1000000.0);
            strbuf_append_char(buf, '\t');
            strbuf_append_uint64(buf, avg_ns);
            strbuf_append_char(buf, '\n');
        }
    }
    if (g_js_exec_profile_load_ic_site_count > 0) {
        strbuf_append_str(buf, "\n# Load IC sites\n");
        strbuf_append_str(buf, "load_ic_site");
        for (int i = 0; i < JS_LOAD_IC_SITE_REASON_COUNT; i++) {
            strbuf_append_char(buf, '\t');
            strbuf_append_str(buf, g_js_load_ic_reason_names[i]);
        }
        strbuf_append_char(buf, '\n');
        for (int i = 0; i < g_js_exec_profile_load_ic_site_count; i++) {
            JsExecProfileLoadICSite* site = &g_js_exec_profile_load_ic_sites[i];
            strbuf_append_str(buf, site->label);
            for (int j = 0; j < JS_LOAD_IC_SITE_REASON_COUNT; j++) {
                strbuf_append_char(buf, '\t');
                strbuf_append_uint64(buf, site->counts[j]);
            }
            strbuf_append_char(buf, '\n');
        }
    }
    if (g_js_exec_profile_store_ic_site_count > 0) {
        strbuf_append_str(buf, "\n# Store IC sites\n");
        strbuf_append_str(buf, "store_ic_site");
        for (int i = 0; i < JS_STORE_IC_SITE_REASON_COUNT; i++) {
            strbuf_append_char(buf, '\t');
            strbuf_append_str(buf, g_js_store_ic_reason_names[i]);
        }
        strbuf_append_char(buf, '\n');
        for (int i = 0; i < g_js_exec_profile_store_ic_site_count; i++) {
            JsExecProfileStoreICSite* site = &g_js_exec_profile_store_ic_sites[i];
            strbuf_append_str(buf, site->label);
            for (int j = 0; j < JS_STORE_IC_SITE_REASON_COUNT; j++) {
                strbuf_append_char(buf, '\t');
                strbuf_append_uint64(buf, site->counts[j]);
            }
            strbuf_append_char(buf, '\n');
        }
    }
    if (g_js_exec_profile_shape_guard_site_count > 0) {
        strbuf_append_str(buf, "\n# Shape guard sites\n");
        strbuf_append_str(buf,
            "shape_guard_site\thits\tmisses\texpected_shape\tmiss_shape_1\tmiss_count_1\tmiss_shape_2\tmiss_count_2\tmiss_shape_3\tmiss_count_3\tmiss_shape_4\tmiss_count_4\n");
        for (int i = 0; i < g_js_exec_profile_shape_guard_site_count; i++) {
            JsExecProfileShapeGuardSite* site = &g_js_exec_profile_shape_guard_sites[i];
            strbuf_append_str(buf, site->label);
            strbuf_append_char(buf, '\t');
            strbuf_append_uint64(buf, site->hits);
            strbuf_append_char(buf, '\t');
            strbuf_append_uint64(buf, site->misses);
            strbuf_append_char(buf, '\t');
            strbuf_append_format(buf, "0x%llx", (unsigned long long)site->expected_shape);
            for (int j = 0; j < (int)(sizeof(site->miss_shapes) / sizeof(site->miss_shapes[0])); j++) {
                strbuf_append_char(buf, '\t');
                if (site->miss_counts[j] == 0) {
                    strbuf_append_str(buf, "0x0");
                } else {
                    strbuf_append_format(buf, "0x%llx", (unsigned long long)site->miss_shapes[j]);
                }
                strbuf_append_char(buf, '\t');
                strbuf_append_uint64(buf, site->miss_counts[j]);
            }
            strbuf_append_char(buf, '\n');
        }
    }

    if (write_text_file_atomic(out_path, buf->str ? buf->str : "") != 0) {
        log_error("js-exec-profile: failed to write '%s'", out_path);
    }
    strbuf_free(buf);
}

#endif // LAMBDA_JS_EXEC_PROFILE
