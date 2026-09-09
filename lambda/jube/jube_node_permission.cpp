#include "jube_node_permission.h"
#include "../js/js_runtime.h"
#include "../js/js_runtime_state.hpp"
#include "jube_registry.h"
#include "../module/node_core/node_runtime_state.hpp"
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

extern String* heap_create_name(const char* name, size_t len);

typedef enum JsPermissionFsKind {
    JS_PERMISSION_FS_READ,
    JS_PERMISSION_FS_WRITE,
} JsPermissionFsKind;

// Command-line policy is immutable bootstrap configuration. Per-context
// mutations (notably process.permission.drop()) use a private copy below.
static JsPermissionPolicy js_permission_bootstrap_policy = {};

static JsPermissionPolicy* js_permission_context_policy() {
    if (!js_active_runtime_state) return NULL;
    void* session = jube_node_runtime_current_session();
    JsPermissionPolicy* policy = session ? jube_node_permission_policy(session) : NULL;
    return policy && policy->initialized ? policy : NULL;
}

static JsPermissionPolicy* js_permission_policy_current() {
    JsPermissionPolicy* policy = js_permission_context_policy();
    return policy ? policy : &js_permission_bootstrap_policy;
}

static JsPermissionPolicy* js_permission_policy_mutable() {
    if (!js_active_runtime_state) return &js_permission_bootstrap_policy;
    JsPermissionPolicy* policy = js_permission_context_policy();
    if (policy) return policy;
    jube_modules_runtime_attach();
    void* session = jube_node_runtime_current_session();
    policy = session ? jube_node_permission_policy_ensure(session) : NULL;
    if (!policy) {
        // The bootstrap policy remains available if the optional Node session
        // cannot be attached; callers still observe the command-line policy.
        return &js_permission_bootstrap_policy;
    }
    if (!policy->initialized) {
        if (!js_permission_policy_copy(policy, &js_permission_bootstrap_policy)) {
            return &js_permission_bootstrap_policy;
        }
        policy->initialized = true;
    }
    return policy;
}

#define g_permission_enabled (js_permission_policy_current()->enabled)
#define g_permission_child_process (js_permission_policy_current()->child_process)
#define g_permission_net (js_permission_policy_current()->net)
#define g_permission_inspector (js_permission_policy_current()->inspector)
#define g_permission_addon (js_permission_policy_current()->addon)
#define g_permission_wasi (js_permission_policy_current()->wasi)

static Item js_perm_string_item(const char* str) {
    if (!str) str = "";
    return js_name_item(str, strlen(str));
}

#define js_perm_item_to_cstr(value, buf, buf_size) \
    (js_item_to_cstr((value), (buf), (buf_size)) != NULL)

static bool js_perm_scope_equals(Item value, const char* lit) {
    if (get_type_id(value) != LMD_TYPE_STRING || !lit) return false;
    String* s = it2s(value);
    size_t len = strlen(lit);
    return s->len == len && memcmp(s->chars, lit, len) == 0;
}

static void js_permission_clear_grants(JsPermissionPolicy* policy) {
    if (!policy || !policy->grants) return;
    for (int i = 0; i < policy->grants->length; i++) {
        JsPermissionGrant* grant =
            (JsPermissionGrant*)arraylist_get(policy->grants, i);
        if (!grant) continue;
        mem_free(grant->path);
        mem_free(grant);
    }
    arraylist_free(policy->grants);
    policy->grants = NULL;
}

void js_permission_policy_destroy(JsPermissionPolicy* policy) {
    if (!policy) return;
    js_permission_clear_grants(policy);
    memset(policy, 0, sizeof(*policy));
}

void js_permission_shutdown(void) {
    // Command-line policy is process-owned, unlike the lazy session copies.
    js_permission_policy_destroy(&js_permission_bootstrap_policy);
}

static bool js_permission_append_grant(JsPermissionPolicy* policy,
                                       JsPermissionGrant* grant) {
    if (!policy || !grant) return false;
    if (!policy->grants) {
        policy->grants = arraylist_new(4);
        if (!policy->grants) return false;
    }
    if (arraylist_append(policy->grants, grant)) return true;
    mem_free(grant->path);
    mem_free(grant);
    return false;
}

extern "C" void js_permission_reset(void) {
    JsPermissionPolicy* policy = js_active_runtime_state
        ? js_permission_context_policy() : &js_permission_bootstrap_policy;
    if (!policy) return;
    policy->enabled = false;
    policy->child_process = false;
    policy->net = false;
    policy->inspector = false;
    policy->addon = false;
    policy->wasi = false;
    js_permission_clear_grants(policy);
}

JS_FORWARD_EXPRESSION(int, js_permission_enabled, (void), g_permission_enabled ? 1 : 0)
JS_FORWARD_EXPRESSION(int, js_permission_has_net, (void),
    (!g_permission_enabled || g_permission_net) ? 1 : 0)

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

static bool js_permission_grant_same(const JsPermissionGrant* grant,
                                     const char* path, uint8_t shape_flags) {
    if (!grant || (grant->flags & ~(JS_PERMISSION_GRANT_READ |
            JS_PERMISSION_GRANT_WRITE)) != shape_flags) return false;
    if (!path) return grant->path == NULL;
    return grant->path && strcmp(grant->path, path) == 0;
}

static const char* js_permission_flag_value(const char* arg, const char* name) {
    int len = (int)strlen(name);
    if (strncmp(arg, name, len) != 0) return NULL;
    if (arg[len] == '=') return arg + len + 1;
    if (arg[len] == '\0') return "";
    return NULL;
}

static void js_permission_add_grant(JsPermissionPolicy* policy, uint8_t access,
                                    const char* raw) {
    if (!policy || !raw || !raw[0]) return;
    uint8_t shape_flags = 0;
    char normalized[PATH_MAX] = {};
    const char* path = normalized;
    if (strcmp(raw, "*") == 0) {
        shape_flags = JS_PERMISSION_GRANT_WILDCARD_ALL;
        path = NULL;
    } else {
        char temp[PATH_MAX];
        snprintf(temp, sizeof(temp), "%s", raw);
        int len = (int)strlen(temp);
        if (len > 0 && temp[len - 1] == '*') {
            shape_flags |= JS_PERMISSION_GRANT_WILDCARD_PREFIX;
            temp[len - 1] = '\0';
        }
        js_perm_normalize_absolute(temp, normalized, (int)sizeof(normalized));
        if (js_perm_path_is_dir(normalized)) {
            shape_flags |= JS_PERMISSION_GRANT_DIRECTORY;
        }
    }

    if (policy->grants) {
        for (int i = 0; i < policy->grants->length; i++) {
            JsPermissionGrant* grant =
                (JsPermissionGrant*)arraylist_get(policy->grants, i);
            if (js_permission_grant_same(grant, path, shape_flags)) {
                grant->flags |= access;
                return;
            }
        }
    }

    JsPermissionGrant* grant =
        (JsPermissionGrant*)mem_calloc(1, sizeof(JsPermissionGrant), MEM_CAT_SYSTEM);
    if (!grant) return;
    grant->path = path ? mem_strdup(path, MEM_CAT_SYSTEM) : NULL;
    if (path && !grant->path) {
        mem_free(grant);
        return;
    }
    grant->flags = (uint8_t)(shape_flags | access);
    js_permission_append_grant(policy, grant);
}

static void js_permission_add_grant_values(JsPermissionPolicy* policy, uint8_t access,
                                           const char* values) {
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
            js_permission_add_grant(policy, access, one);
        }
        if (*p == ',') p++;
    }
}

extern "C" void js_permission_init_from_argv(int argc, const char** argv) {
    // Command-line parsing must not attach a Node session to every JS realm;
    // the bootstrap policy is copied into Node state only when Node is active.
    JsPermissionPolicy* policy = &js_permission_bootstrap_policy;
    policy->initialized = false;
    policy->enabled = false;
    policy->child_process = false;
    policy->net = false;
    policy->inspector = false;
    policy->addon = false;
    policy->wasi = false;
    js_permission_clear_grants(policy);
    for (int i = 0; i < argc; i++) {
        const char* arg = argv[i];
        if (!arg) continue;
        if (strcmp(arg, "--permission") == 0) {
            policy->enabled = true;
        } else if (strcmp(arg, "--allow-child-process") == 0) {
            policy->child_process = true;
        } else if (strcmp(arg, "--allow-net") == 0) {
            policy->net = true;
        } else if (strcmp(arg, "--allow-inspector") == 0) {
            policy->inspector = true;
        } else if (strcmp(arg, "--allow-addons") == 0 || strcmp(arg, "--allow-addon") == 0) {
            policy->addon = true;
        } else if (strcmp(arg, "--allow-wasi") == 0) {
            policy->wasi = true;
        } else {
            const char* read_val = js_permission_flag_value(arg, "--allow-fs-read");
            const char* write_val = js_permission_flag_value(arg, "--allow-fs-write");
            if (read_val) {
                if (!read_val[0] && i + 1 < argc) read_val = argv[++i];
                js_permission_add_grant_values(policy, JS_PERMISSION_GRANT_READ, read_val);
            } else if (write_val) {
                if (!write_val[0] && i + 1 < argc) write_val = argv[++i];
                js_permission_add_grant_values(policy, JS_PERMISSION_GRANT_WRITE, write_val);
            }
        }
    }
    void* session = jube_node_runtime_current_session();
    JsPermissionPolicy* session_policy =
        session ? jube_node_permission_policy_ensure(session) : NULL;
    if (session_policy && js_permission_policy_copy(session_policy,
                                                     &js_permission_bootstrap_policy)) {
        session_policy->initialized = true;
    }
}

bool js_permission_policy_copy(JsPermissionPolicy* dst, const JsPermissionPolicy* src) {
    if (!dst || !src) return false;
    if (dst == src) return true;
    js_permission_policy_destroy(dst);
    dst->initialized = src->initialized;
    dst->enabled = src->enabled;
    dst->child_process = src->child_process;
    dst->net = src->net;
    dst->inspector = src->inspector;
    dst->addon = src->addon;
    dst->wasi = src->wasi;
    if (!src->grants) return true;
    for (int i = 0; i < src->grants->length; i++) {
        const JsPermissionGrant* grant =
            (const JsPermissionGrant*)arraylist_get(src->grants, i);
        if (!grant) continue;
        JsPermissionGrant* copy =
            (JsPermissionGrant*)mem_calloc(1, sizeof(JsPermissionGrant), MEM_CAT_SYSTEM);
        if (!copy) {
            js_permission_policy_destroy(dst);
            return false;
        }
        copy->path = grant->path ? mem_strdup(grant->path, MEM_CAT_SYSTEM) : NULL;
        if (grant->path && !copy->path) {
            mem_free(copy);
            js_permission_policy_destroy(dst);
            return false;
        }
        copy->flags = grant->flags;
        if (!js_permission_append_grant(dst, copy)) {
            js_permission_policy_destroy(dst);
            return false;
        }
    }
    return true;
}

static bool js_permission_grant_matches(const JsPermissionGrant* grant,
                                        uint8_t access, const char* normalized) {
    if (!grant || !(grant->flags & access)) return false;
    if (grant->flags & JS_PERMISSION_GRANT_WILDCARD_ALL) return true;
    if (!normalized || !normalized[0]) return false;
    if (!grant->path) return false;
    int grant_len = (int)strlen(grant->path);
    if (grant->flags & JS_PERMISSION_GRANT_WILDCARD_PREFIX) {
        return strncmp(normalized, grant->path, grant_len) == 0;
    }
    if (strcmp(normalized, grant->path) == 0) return true;
    if ((grant->flags & JS_PERMISSION_GRANT_DIRECTORY) && grant_len > 1 &&
        strncmp(normalized, grant->path, grant_len) == 0 &&
        normalized[grant_len] == '/') {
        return true;
    }
    return false;
}

static bool js_permission_grants_have_all(const JsPermissionPolicy* policy,
                                          uint8_t access) {
    if (!policy || !policy->grants) return false;
    for (int i = 0; i < policy->grants->length; i++) {
        const JsPermissionGrant* grant =
            (const JsPermissionGrant*)arraylist_get(policy->grants, i);
        if (grant && (grant->flags & access) &&
                (grant->flags & JS_PERMISSION_GRANT_WILDCARD_ALL)) return true;
    }
    return false;
}

static bool js_permission_has_grant(const JsPermissionPolicy* policy, uint8_t access,
                                    const char* path) {
    if (!g_permission_enabled) return true;
    if (!path) return js_permission_grants_have_all(policy, access);
    char normalized[PATH_MAX];
    js_perm_normalize_absolute(path, normalized, sizeof(normalized));
    if (!policy || !policy->grants) return false;
    for (int i = 0; i < policy->grants->length; i++) {
        const JsPermissionGrant* grant =
            (const JsPermissionGrant*)arraylist_get(policy->grants, i);
        if (js_permission_grant_matches(grant, access, normalized)) return true;
    }
    return false;
}

JS_FORWARD_STATIC_EXPRESSION(int, js_permission_has_fs_grant,
    (const char* path, uint8_t access),
    js_permission_has_grant(js_permission_policy_current(), access, path) ? 1 : 0)

JS_FORWARD_STATIC_EXPRESSION(int, js_permission_has_full_fs_grant,
    (uint8_t access),
    (!g_permission_enabled || js_permission_grants_have_all(
        js_permission_policy_current(), access)) ? 1 : 0)

JS_FORWARD_RETURN(int, js_permission_has_fs_read, (const char* path),
    js_permission_has_fs_grant, (path, JS_PERMISSION_GRANT_READ))
JS_FORWARD_RETURN(int, js_permission_has_fs_write, (const char* path),
    js_permission_has_fs_grant, (path, JS_PERMISSION_GRANT_WRITE))
JS_FORWARD_RETURN(int, js_permission_has_full_fs_read, (void),
    js_permission_has_full_fs_grant, (JS_PERMISSION_GRANT_READ))
JS_FORWARD_RETURN(int, js_permission_has_full_fs_write, (void),
    js_permission_has_full_fs_grant, (JS_PERMISSION_GRANT_WRITE))

static void js_permission_drop_grants(JsPermissionPolicy* policy, uint8_t access,
                                      const char* path) {
    if (!policy || !policy->grants) return;
    char normalized[PATH_MAX];
    if (path) js_perm_normalize_absolute(path, normalized, sizeof(normalized));
    for (int i = policy->grants->length - 1; i >= 0; i--) {
        JsPermissionGrant* grant =
            (JsPermissionGrant*)arraylist_get(policy->grants, i);
        if (!grant || !(grant->flags & access)) continue;
        if (path && (!grant->path || strcmp(grant->path, normalized) != 0)) continue;
        grant->flags &= (uint8_t)~access;
        if (grant->flags & (JS_PERMISSION_GRANT_READ | JS_PERMISSION_GRANT_WRITE)) continue;
        mem_free(grant->path);
        mem_free(grant);
        arraylist_remove(policy->grants, i);
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
        bool ok = js_permission_has_grant(js_permission_policy_current(),
            kind == JS_PERMISSION_FS_READ ? JS_PERMISSION_GRANT_READ :
                                            JS_PERMISSION_GRANT_WRITE, path);
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
    JsPermissionPolicy* policy = js_permission_policy_mutable();
    if (!policy || !policy->enabled) return (Item){.item = ITEM_FALSE};
    JsPermissionFsKind kind;
    if (!js_permission_scope_kind(scope_item, &kind)) return (Item){.item = ITEM_FALSE};

    char resource[PATH_MAX];
    const char* path = NULL;
    if (get_type_id(resource_item) == LMD_TYPE_STRING) {
        if (!js_perm_item_to_cstr(resource_item, resource, sizeof(resource))) return (Item){.item = ITEM_FALSE};
        path = resource;
    }
    js_permission_drop_grants(policy, kind == JS_PERMISSION_FS_READ
            ? JS_PERMISSION_GRANT_READ : JS_PERMISSION_GRANT_WRITE, path);
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
    js_set_key_default(err, js_perm_string_item("code"), js_perm_string_item("ERR_ACCESS_DENIED"));
    if (permission) js_set_key_default(err, js_perm_string_item("permission"), js_perm_string_item(permission));
    if (resource) js_set_key_default(err, js_perm_string_item("resource"), js_perm_string_item(resource));
    return err;
}

extern "C" Item js_permission_make_net_error(const char* syscall, const char* resource) {
    const char* message =
        "Access to this API has been restricted. Use --allow-net to manage permissions.";
    Item err = js_new_error(js_perm_string_item(message));
    js_set_key_default(err, js_perm_string_item("code"), js_perm_string_item("ERR_ACCESS_DENIED"));
    js_set_key_default(err, js_perm_string_item("permission"), js_perm_string_item("Net"));
    if (syscall) js_set_key_default(err, js_perm_string_item("syscall"), js_perm_string_item(syscall));
    if (resource) js_set_key_default(err, js_perm_string_item("resource"), js_perm_string_item(resource));
    return err;
}

extern "C" Item js_permission_throw_fs_error(const char* permission, const char* resource, const char* message) {
    Item err = js_permission_make_fs_error(permission, resource, message);
    return js_throw_value(err);
}

extern "C" Item js_permission_check_fs_read(const char* path) {
    if (!g_permission_enabled || js_permission_has_fs_read(path)) return (Item){.item = ITEM_TRUE};
    return js_permission_throw_fs_error("FileSystemRead", path, NULL);
}

extern "C" Item js_permission_check_fs_write(const char* path) {
    if (!g_permission_enabled || js_permission_has_fs_write(path)) return (Item){.item = ITEM_TRUE};
    return js_permission_throw_fs_error("FileSystemWrite", path, NULL);
}

#undef g_permission_enabled
#undef g_permission_child_process
#undef g_permission_net
#undef g_permission_inspector
#undef g_permission_addon
#undef g_permission_wasi
