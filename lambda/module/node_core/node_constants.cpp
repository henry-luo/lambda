// node_constants.cpp — public node:constants namespace through Jube values.
#include "node_constants.hpp"
#include "../../jube/jube_registry.h"

#include <cstring>

static const JubeHostAPI* node_constants_host = NULL;
struct NodeConstantsSessionState {
    void* session;
    bool rooted;
    Item cached_namespace;
};
static NodeConstantsSessionState* node_constants_state(void) {
    return (NodeConstantsSessionState*)jube_node_current_module_state(
        JUBE_NODE_MODULE_STATE_CONSTANTS);
}
#define node_constants_session (node_constants_state()->session)
#define node_constants_rooted (node_constants_state()->rooted)
#define node_constants_cached_namespace (node_constants_state()->cached_namespace)

static const char* const node_constants_fs_names[] = {
    "F_OK", "R_OK", "W_OK", "X_OK", "O_RDONLY", "O_WRONLY", "O_RDWR", "O_CREAT",
    "O_TRUNC", "O_APPEND", "O_EXCL", "S_IFMT", "S_IFREG", "S_IFDIR", "S_IFCHR",
    "S_IFBLK", "S_IFIFO", "S_IFLNK", "S_IFSOCK", "S_IRUSR", "S_IWUSR", "S_IXUSR",
    "S_IRGRP", "S_IWGRP", "S_IXGRP", "S_IROTH", "S_IWOTH", "S_IXOTH",
    "UV_DIRENT_UNKNOWN", "UV_DIRENT_FILE", "UV_DIRENT_DIR", "UV_DIRENT_LINK",
    "UV_DIRENT_FIFO", "UV_DIRENT_SOCKET", "UV_DIRENT_CHAR", "UV_DIRENT_BLOCK",
    "UV_FS_SYMLINK_DIR", "UV_FS_SYMLINK_JUNCTION", "COPYFILE_EXCL", "COPYFILE_FICLONE",
    "COPYFILE_FICLONE_FORCE", NULL,
};

static const char* const node_constants_errno_names[] = {
    "E2BIG", "EACCES", "EADDRINUSE", "EADDRNOTAVAIL", "EAGAIN", "EALREADY", "EBADF",
    "EBUSY", "ECANCELED", "ECHILD", "ECONNABORTED", "ECONNREFUSED", "ECONNRESET",
    "EDEADLK", "EDESTADDRREQ", "EDOM", "EEXIST", "EFAULT", "EFBIG", "EHOSTUNREACH",
    "EINPROGRESS", "EINTR", "EINVAL", "EIO", "EISCONN", "EISDIR", "ELOOP", "EMFILE",
    "EMLINK", "EMSGSIZE", "ENAMETOOLONG", "ENETDOWN", "ENETUNREACH", "ENFILE",
    "ENOBUFS", "ENODEV", "ENOENT", "ENOMEM", "ENOPROTOOPT", "ENOSPC", "ENOSYS",
    "ENOTCONN", "ENOTDIR", "ENOTEMPTY", "ENOTSOCK", "ENOTSUP", "EPERM", "EPIPE",
    "EPROTONOSUPPORT", "EPROTOTYPE", "ERANGE", "EROFS", "ESPIPE", "ESRCH", "ETIMEDOUT",
    "ETXTBSY", "EWOULDBLOCK", "EXDEV", NULL,
};

static const char* const node_constants_priority_names[] = {
    "PRIORITY_LOW", "PRIORITY_BELOW_NORMAL", "PRIORITY_NORMAL", "PRIORITY_ABOVE_NORMAL",
    "PRIORITY_HIGH", "PRIORITY_HIGHEST", NULL,
};

static const char* const node_constants_signal_names[] = {
    "SIGHUP", "SIGINT", "SIGQUIT", "SIGILL", "SIGTRAP", "SIGABRT", "SIGBUS", "SIGFPE",
    "SIGKILL", "SIGUSR1", "SIGSEGV", "SIGUSR2", "SIGPIPE", "SIGALRM", "SIGTERM", "SIGCHLD",
    "SIGCONT", "SIGSTOP", "SIGTSTP", "SIGTTIN", "SIGTTOU", "SIGURG", "SIGXCPU", "SIGXFSZ",
    "SIGVTALRM", "SIGPROF", "SIGWINCH", "SIGIO", "SIGSYS", NULL,
};

static Item node_constants_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static Item node_constants_key(const char* name) {
    return node_constants_host->value->string_from_utf8_n(name, strlen(name));
}

static void node_constants_copy(Item destination, Item source, const char* const* names) {
    JubeRootFrame frame = {};
    if (!node_constants_host->node->roots->root_frame_begin(&frame, 4)) return;
    uint64_t* destination_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* source_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    if (!destination_root || !source_root || !key_root || !value_root) {
        node_constants_host->node->roots->root_frame_end(&frame);
        return;
    }
    *destination_root = destination.item;
    *source_root = source.item;
    for (int index = 0; names[index]; index++) {
        Item key = node_constants_key(names[index]);
        *key_root = key.item;
        Item value = node_constants_host->value->property_get(node_constants_root_value(source_root),
                                                                node_constants_root_value(key_root));
        *value_root = value.item;
        if (node_constants_host->value->kind(value) != JUBE_VALUE_UNDEFINED &&
                node_constants_host->value->kind(value) != JUBE_VALUE_NULL) {
            node_constants_host->value->property_set(node_constants_root_value(destination_root),
                node_constants_root_value(key_root), node_constants_root_value(value_root));
        }
    }
    node_constants_host->node->roots->root_frame_end(&frame);
}

Item node_constants_namespace(void) {
    if (node_constants_cached_namespace.item != 0) return node_constants_cached_namespace;
    if (!node_constants_host || !node_constants_session) return ItemNull;
    Item fs_namespace = ItemNull;
    Item os_namespace = ItemNull;
    if (node_constants_host->node->runtime->resolve_host_namespace(node_constants_session, "fs",
            &fs_namespace) != 0 || node_constants_host->node->runtime->resolve_namespace(
            node_constants_session, "os", &os_namespace) != 0) return ItemNull;

    JubeRootFrame frame = {};
    if (!node_constants_host->node->roots->root_frame_begin(&frame, 7)) return ItemNull;
    uint64_t* fs_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* os_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* fs_constants_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* os_constants_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* errno_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* priority_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* signals_root = node_constants_host->node->roots->root_frame_take_slot(&frame);
    if (!fs_root || !os_root || !fs_constants_root || !os_constants_root || !errno_root ||
            !priority_root || !signals_root) {
        node_constants_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *fs_root = fs_namespace.item;
    *os_root = os_namespace.item;
    Item fs_key = node_constants_key("constants");
    *fs_constants_root = node_constants_host->value->property_get(node_constants_root_value(fs_root), fs_key).item;
    Item os_constants = node_constants_host->value->property_get(node_constants_root_value(os_root), fs_key);
    *os_constants_root = os_constants.item;
    Item errno_key = node_constants_key("errno");
    *errno_root = node_constants_host->value->property_get(node_constants_root_value(os_constants_root), errno_key).item;
    Item priority_key = node_constants_key("priority");
    *priority_root = node_constants_host->value->property_get(node_constants_root_value(os_constants_root), priority_key).item;
    Item signals_key = node_constants_key("signals");
    *signals_root = node_constants_host->value->property_get(node_constants_root_value(os_constants_root), signals_key).item;

    node_constants_cached_namespace = node_constants_host->script->object_create(ItemNull);
    node_constants_copy(node_constants_cached_namespace, node_constants_root_value(fs_constants_root), node_constants_fs_names);
    node_constants_copy(node_constants_cached_namespace, node_constants_root_value(errno_root), node_constants_errno_names);
    node_constants_copy(node_constants_cached_namespace, node_constants_root_value(priority_root), node_constants_priority_names);
    node_constants_copy(node_constants_cached_namespace, node_constants_root_value(signals_root), node_constants_signal_names);
    node_constants_host->script->object_freeze(node_constants_cached_namespace);
    node_constants_host->node->roots->root_frame_end(&frame);
    return node_constants_cached_namespace;
}

int node_constants_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots || !host->value ||
            !host->value->property_get || !host->value->property_set || !host->value->kind ||
            !host->value->string_from_utf8_n || !host->script || !host->script->object_create ||
            !host->script->object_freeze) return -1;
    node_constants_host = host;
    return 0;
}

void node_constants_shutdown(void) {
    node_constants_host = NULL;
}

void node_constants_runtime_attach(void* session) {
    if (!node_constants_host || !node_constants_host->node->runtime->session_is_live(session)) return;
    if (!jube_node_session_module_state_get(session, JUBE_NODE_MODULE_STATE_CONSTANTS,
            sizeof(NodeConstantsSessionState))) return;
    node_constants_session = session;
    if (node_constants_host->node->roots->persistent_root_register(session,
            &node_constants_cached_namespace.item) == 0) node_constants_rooted = true;
}

void node_constants_runtime_reset(void* session) {
    if (session == node_constants_session) node_constants_cached_namespace = (Item){0};
}

void node_constants_runtime_detach(void* session) {
    if (!node_constants_host || session != node_constants_session) return;
    if (node_constants_rooted) {
        node_constants_host->node->roots->persistent_root_unregister(session,
            &node_constants_cached_namespace.item);
        node_constants_rooted = false;
    }
    node_constants_cached_namespace = (Item){0};
    node_constants_session = NULL;
}
