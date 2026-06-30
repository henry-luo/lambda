#include "js_permission.h"
#include "js_runtime.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

extern String* heap_create_name(const char* str, size_t len);

#define JS_PERMISSION_MAX_GRANTS 128

typedef enum JsPermissionFsKind {
    JS_PERMISSION_FS_READ,
    JS_PERMISSION_FS_WRITE,
} JsPermissionFsKind;

typedef struct JsPermissionGrant {
    char path[PATH_MAX];
    bool wildcard_all;
    bool wildcard_prefix;
    bool directory;
    bool active;
} JsPermissionGrant;

static bool g_permission_enabled = false;
static bool g_permission_child_process = false;
static bool g_permission_net = false;
static bool g_permission_inspector = false;
static bool g_permission_addon = false;
static bool g_permission_wasi = false;
static JsPermissionGrant g_fs_read_grants[JS_PERMISSION_MAX_GRANTS];
static JsPermissionGrant g_fs_write_grants[JS_PERMISSION_MAX_GRANTS];

static Item js_perm_string_item(const char* str) {
    if (!str) str = "";
    return (Item){.item = s2it(heap_create_name(str, strlen(str)))};
}

static bool js_perm_item_to_cstr(Item value, char* buf, int buf_size) {
    if (buf_size <= 0 || get_type_id(value) != LMD_TYPE_STRING) return false;
    String* s = it2s(value);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return true;
}

static bool js_perm_scope_equals(Item value, const char* lit) {
    if (get_type_id(value) != LMD_TYPE_STRING || !lit) return false;
    String* s = it2s(value);
    size_t len = strlen(lit);
    return s->len == len && memcmp(s->chars, lit, len) == 0;
}

static void js_permission_clear_grants(JsPermissionGrant* grants) {
    for (int i = 0; i < JS_PERMISSION_MAX_GRANTS; i++) {
        grants[i].path[0] = '\0';
        grants[i].wildcard_all = false;
        grants[i].wildcard_prefix = false;
        grants[i].directory = false;
        grants[i].active = false;
    }
}

extern "C" void js_permission_reset(void) {
    g_permission_enabled = false;
    g_permission_child_process = false;
    g_permission_net = false;
    g_permission_inspector = false;
    g_permission_addon = false;
    g_permission_wasi = false;
    js_permission_clear_grants(g_fs_read_grants);
    js_permission_clear_grants(g_fs_write_grants);
}

extern "C" int js_permission_enabled(void) {
    return g_permission_enabled ? 1 : 0;
}

static void js_perm_normalize_absolute(const char* in, char* out, int out_size) {
    if (!out || out_size <= 0) return;
    out[0] = '\0';
    if (!in || !in[0]) return;

    char abs_buf[PATH_MAX];
    if (in[0] == '/') {
        snprintf(abs_buf, sizeof(abs_buf), "%s", in);
    } else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
        snprintf(abs_buf, sizeof(abs_buf), "%s/%s", cwd, in);
    }

    char result[PATH_MAX];
    int result_len = 0;
    result[result_len++] = '/';
    result[result_len] = '\0';

    const char* p = abs_buf;
    while (*p == '/') p++;
    while (*p) {
        const char* start = p;
        while (*p && *p != '/') p++;
        int part_len = (int)(p - start);
        while (*p == '/') p++;
        if (part_len == 0 || (part_len == 1 && start[0] == '.')) continue;
        if (part_len == 2 && start[0] == '.' && start[1] == '.') {
            if (result_len > 1) {
                result_len--;
                while (result_len > 0 && result[result_len - 1] != '/') result_len--;
                if (result_len == 0) result[result_len++] = '/';
                result[result_len] = '\0';
            }
            continue;
        }
        if (result_len > 1 && result_len < (int)sizeof(result) - 1) {
            result[result_len++] = '/';
        }
        int copy_len = part_len;
        if (result_len + copy_len >= (int)sizeof(result)) {
            copy_len = (int)sizeof(result) - result_len - 1;
        }
        if (copy_len > 0) {
            memcpy(result + result_len, start, copy_len);
            result_len += copy_len;
            result[result_len] = '\0';
        }
    }

    snprintf(out, out_size, "%s", result);
}

static bool js_perm_path_is_dir(const char* path) {
    struct stat st;
    if (!path || stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

static void js_permission_add_grant(JsPermissionGrant* grants, const char* raw) {
    if (!raw || !raw[0]) return;
    for (int i = 0; i < JS_PERMISSION_MAX_GRANTS; i++) {
        if (!grants[i].active) {
            JsPermissionGrant* grant = &grants[i];
            grant->active = true;
            grant->wildcard_all = strcmp(raw, "*") == 0;
            grant->wildcard_prefix = false;
            grant->directory = false;
            grant->path[0] = '\0';
            if (grant->wildcard_all) return;

            char temp[PATH_MAX];
            snprintf(temp, sizeof(temp), "%s", raw);
            int len = (int)strlen(temp);
            if (len > 0 && temp[len - 1] == '*') {
                grant->wildcard_prefix = true;
                temp[len - 1] = '\0';
            }
            js_perm_normalize_absolute(temp, grant->path, sizeof(grant->path));
            grant->directory = js_perm_path_is_dir(grant->path);
            return;
        }
    }
}

static const char* js_permission_flag_value(const char* arg, const char* name) {
    int len = (int)strlen(name);
    if (strncmp(arg, name, len) != 0) return NULL;
    if (arg[len] == '=') return arg + len + 1;
    if (arg[len] == '\0') return "";
    return NULL;
}

static void js_permission_add_grant_values(JsPermissionGrant* grants, const char* values) {
    if (!values || !values[0]) return;
    const char* p = values;
    while (*p) {
        const char* start = p;
        while (*p && *p != ',') p++;
        int len = (int)(p - start);
        if (len > 0) {
            char one[PATH_MAX];
            if (len >= (int)sizeof(one)) len = (int)sizeof(one) - 1;
            memcpy(one, start, len);
            one[len] = '\0';
            js_permission_add_grant(grants, one);
        }
        if (*p == ',') p++;
    }
}

extern "C" void js_permission_init_from_argv(int argc, const char** argv) {
    js_permission_reset();
    for (int i = 0; i < argc; i++) {
        const char* arg = argv[i];
        if (!arg) continue;
        if (strcmp(arg, "--permission") == 0) {
            g_permission_enabled = true;
        } else if (strcmp(arg, "--allow-child-process") == 0) {
            g_permission_child_process = true;
        } else if (strcmp(arg, "--allow-net") == 0) {
            g_permission_net = true;
        } else if (strcmp(arg, "--allow-inspector") == 0) {
            g_permission_inspector = true;
        } else if (strcmp(arg, "--allow-addons") == 0 || strcmp(arg, "--allow-addon") == 0) {
            g_permission_addon = true;
        } else if (strcmp(arg, "--allow-wasi") == 0) {
            g_permission_wasi = true;
        } else {
            const char* read_val = js_permission_flag_value(arg, "--allow-fs-read");
            const char* write_val = js_permission_flag_value(arg, "--allow-fs-write");
            if (read_val) {
                if (!read_val[0] && i + 1 < argc) read_val = argv[++i];
                js_permission_add_grant_values(g_fs_read_grants, read_val);
            } else if (write_val) {
                if (!write_val[0] && i + 1 < argc) write_val = argv[++i];
                js_permission_add_grant_values(g_fs_write_grants, write_val);
            }
        }
    }
}

static bool js_permission_grant_matches(JsPermissionGrant* grant, const char* normalized) {
    if (!grant->active) return false;
    if (grant->wildcard_all) return true;
    if (!normalized || !normalized[0]) return false;
    int grant_len = (int)strlen(grant->path);
    if (grant->wildcard_prefix) {
        return strncmp(normalized, grant->path, grant_len) == 0;
    }
    if (strcmp(normalized, grant->path) == 0) return true;
    if (grant->directory && grant_len > 1 &&
        strncmp(normalized, grant->path, grant_len) == 0 &&
        normalized[grant_len] == '/') {
        return true;
    }
    return false;
}

static bool js_permission_grants_have_all(JsPermissionGrant* grants) {
    for (int i = 0; i < JS_PERMISSION_MAX_GRANTS; i++) {
        if (grants[i].active && grants[i].wildcard_all) return true;
    }
    return false;
}

static bool js_permission_has_grant(JsPermissionGrant* grants, const char* path) {
    if (!g_permission_enabled) return true;
    if (!path) return js_permission_grants_have_all(grants);
    char normalized[PATH_MAX];
    js_perm_normalize_absolute(path, normalized, sizeof(normalized));
    for (int i = 0; i < JS_PERMISSION_MAX_GRANTS; i++) {
        if (js_permission_grant_matches(&grants[i], normalized)) return true;
    }
    return false;
}

extern "C" int js_permission_has_fs_read(const char* path) {
    return js_permission_has_grant(g_fs_read_grants, path) ? 1 : 0;
}

extern "C" int js_permission_has_fs_write(const char* path) {
    return js_permission_has_grant(g_fs_write_grants, path) ? 1 : 0;
}

extern "C" int js_permission_has_full_fs_read(void) {
    return (!g_permission_enabled || js_permission_grants_have_all(g_fs_read_grants)) ? 1 : 0;
}

extern "C" int js_permission_has_full_fs_write(void) {
    return (!g_permission_enabled || js_permission_grants_have_all(g_fs_write_grants)) ? 1 : 0;
}

static void js_permission_drop_grants(JsPermissionGrant* grants, const char* path) {
    if (!path) {
        js_permission_clear_grants(grants);
        return;
    }
    char normalized[PATH_MAX];
    js_perm_normalize_absolute(path, normalized, sizeof(normalized));
    for (int i = 0; i < JS_PERMISSION_MAX_GRANTS; i++) {
        if (!grants[i].active || grants[i].wildcard_all) continue;
        if (strcmp(grants[i].path, normalized) == 0) grants[i].active = false;
    }
}

static bool js_permission_scope_kind(Item scope_item, JsPermissionFsKind* kind) {
    if (js_perm_scope_equals(scope_item, "fs.read") ||
        js_perm_scope_equals(scope_item, "FileSystemRead")) {
        *kind = JS_PERMISSION_FS_READ;
        return true;
    }
    if (js_perm_scope_equals(scope_item, "fs.write") ||
        js_perm_scope_equals(scope_item, "FileSystemWrite")) {
        *kind = JS_PERMISSION_FS_WRITE;
        return true;
    }
    return false;
}

extern "C" Item js_process_permission_has(Item scope_item, Item resource_item) {
    JsPermissionFsKind kind;
    if (js_permission_scope_kind(scope_item, &kind)) {
        char resource[PATH_MAX];
        const char* path = NULL;
        if (get_type_id(resource_item) == LMD_TYPE_STRING) {
            if (!js_perm_item_to_cstr(resource_item, resource, sizeof(resource))) return (Item){.item = ITEM_FALSE};
            path = resource;
        }
        bool ok = kind == JS_PERMISSION_FS_READ
            ? js_permission_has_grant(g_fs_read_grants, path)
            : js_permission_has_grant(g_fs_write_grants, path);
        return (Item){.item = b2it(ok)};
    }
    if (js_perm_scope_equals(scope_item, "child")) {
        return (Item){.item = b2it(!g_permission_enabled || g_permission_child_process)};
    }
    if (js_perm_scope_equals(scope_item, "net")) {
        return (Item){.item = b2it(!g_permission_enabled || g_permission_net)};
    }
    if (js_perm_scope_equals(scope_item, "inspector")) {
        return (Item){.item = b2it(!g_permission_enabled || g_permission_inspector)};
    }
    if (js_perm_scope_equals(scope_item, "addon")) {
        return (Item){.item = b2it(!g_permission_enabled || g_permission_addon)};
    }
    if (js_perm_scope_equals(scope_item, "wasi")) {
        return (Item){.item = b2it(!g_permission_enabled || g_permission_wasi)};
    }
    return (Item){.item = ITEM_FALSE};
}

extern "C" Item js_process_permission_drop(Item scope_item, Item resource_item) {
    if (!g_permission_enabled) return (Item){.item = ITEM_FALSE};
    JsPermissionFsKind kind;
    if (!js_permission_scope_kind(scope_item, &kind)) return (Item){.item = ITEM_FALSE};

    char resource[PATH_MAX];
    const char* path = NULL;
    if (get_type_id(resource_item) == LMD_TYPE_STRING) {
        if (!js_perm_item_to_cstr(resource_item, resource, sizeof(resource))) return (Item){.item = ITEM_FALSE};
        path = resource;
    }
    js_permission_drop_grants(kind == JS_PERMISSION_FS_READ ? g_fs_read_grants : g_fs_write_grants, path);
    return (Item){.item = ITEM_TRUE};
}

extern "C" Item js_permission_make_fs_error(const char* permission, const char* resource, const char* message) {
    if (!message) {
        if (permission && strcmp(permission, "FileSystemWrite") == 0) {
            message = "Access to this API has been restricted. Use --allow-fs-write to manage permissions.";
        } else {
            message = "Access to this API has been restricted. Use --allow-fs-read to manage permissions.";
        }
    }
    Item err = js_new_error(js_perm_string_item(message));
    js_property_set(err, js_perm_string_item("code"), js_perm_string_item("ERR_ACCESS_DENIED"));
    if (permission) js_property_set(err, js_perm_string_item("permission"), js_perm_string_item(permission));
    if (resource) js_property_set(err, js_perm_string_item("resource"), js_perm_string_item(resource));
    return err;
}

extern "C" Item js_permission_throw_fs_error(const char* permission, const char* resource, const char* message) {
    Item err = js_permission_make_fs_error(permission, resource, message);
    js_throw_value(err);
    return ItemNull;
}

extern "C" Item js_permission_check_fs_read(const char* path) {
    if (!g_permission_enabled || js_permission_has_fs_read(path)) return (Item){.item = ITEM_TRUE};
    return js_permission_throw_fs_error("FileSystemRead", path, NULL);
}

extern "C" Item js_permission_check_fs_write(const char* path) {
    if (!g_permission_enabled || js_permission_has_fs_write(path)) return (Item){.item = ITEM_TRUE};
    return js_permission_throw_fs_error("FileSystemWrite", path, NULL);
}
