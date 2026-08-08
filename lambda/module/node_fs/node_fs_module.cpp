// node_fs_module.cpp -- callback fs slice owned by the node-fs Jube module.
#include "../../jube/jube.h"
#include "../../jube/jube_registry.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

// Access-mode constants are Node API values, not platform operations; keep
// them local so the dynamic module does not need a platform I/O header.
#ifndef F_OK
#define F_OK 0
#endif
#ifndef X_OK
#define X_OK 1
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef R_OK
#define R_OK 4
#endif

static const JubeHostAPI* node_fs_host = NULL;
static void* node_fs_session = NULL;

typedef enum NodeFsMode {
    NODE_FS_MODE_READ,
    NODE_FS_MODE_WRITE,
    NODE_FS_MODE_APPEND,
} NodeFsMode;

typedef struct NodeFsRequest {
    Item callback;
    Item domain;
    Item initial_error;
    char* path;
    char* bytes;
    size_t byte_count;
    int error_number;
    NodeFsMode mode;
    bool detached;
    bool roots_registered;
    uint32_t work_request_id;
    JubeNodeFilesystemReadWrite operation;
    struct NodeFsRequest* next;
} NodeFsRequest;

typedef struct NodeFsFileHandle {
    int descriptor;
} NodeFsFileHandle;

typedef struct NodeFsStats {
    JubeNodeFilesystemMetadata value;
    bool bigint;
} NodeFsStats;

typedef struct NodeFsVector {
    uint8_t* data;
    size_t length;
} NodeFsVector;

static void node_fs_filehandle_destroy(void* payload);
static void node_fs_stats_destroy(void* payload);
static Item node_fs_read_sync_export(Item descriptor_item, Item buffer_item, Item offset_item,
                                     Item length_item, Item position_item);
static Item node_fs_open_sync_export(Item path_item, Item flags_item, Item mode_item);
static Item node_fs_close_sync_export(Item descriptor_item);
static Item node_fs_fchmod_sync(Item descriptor_item, Item mode_item);
static Item node_fs_readv_sync(Item descriptor_item, Item buffers_item, Item position_item);
static Item node_fs_writev_sync(Item descriptor_item, Item buffers_item, Item position_item);
static Item node_fs_write_sync_export(Item descriptor_item, Item data_item, Item offset_item,
                                      Item length_item, Item position_item);
static const JubeTypeDef node_fs_types[] = {
    {"file_handle", JUBE_TYPE_OWNING_NATIVE, NULL, NULL, node_fs_filehandle_destroy},
    {"stats", JUBE_TYPE_OWNING_NATIVE, NULL, NULL, node_fs_stats_destroy},
};

static NodeFsRequest* node_fs_pending = NULL;

static Item node_fs_undefined(void) {
    return (Item){.item = ITEM_JS_UNDEFINED};
}

static void node_fs_pending_add(NodeFsRequest* request) {
    if (!request) return;
    request->next = node_fs_pending;
    node_fs_pending = request;
}

static void node_fs_pending_remove(NodeFsRequest* request) {
    if (!request) return;
    NodeFsRequest** link = &node_fs_pending;
    while (*link) {
        if (*link == request) {
            *link = request->next;
            request->next = NULL;
            return;
        }
        link = &(*link)->next;
    }
}

static bool node_fs_register_roots(NodeFsRequest* request) {
    if (!request || !node_fs_host || !node_fs_session || !node_fs_host->node ||
            !node_fs_host->node->roots || !node_fs_host->node->roots->persistent_root_register) {
        return false;
    }
    const JubeHostRootAPI* roots = node_fs_host->node->roots;
    if (roots->persistent_root_register(node_fs_session, &request->callback.item) != 0) {
        return false;
    }
    if (roots->persistent_root_register(node_fs_session, &request->domain.item) != 0) {
        roots->persistent_root_unregister(node_fs_session, &request->callback.item);
        return false;
    }
    if (roots->persistent_root_register(node_fs_session, &request->initial_error.item) != 0) {
        roots->persistent_root_unregister(node_fs_session, &request->domain.item);
        roots->persistent_root_unregister(node_fs_session, &request->callback.item);
        return false;
    }
    request->roots_registered = true;
    return true;
}

static void node_fs_unregister_roots(NodeFsRequest* request) {
    if (!request || !request->roots_registered || !node_fs_host || !node_fs_host->node ||
            !node_fs_host->node->roots || !node_fs_host->node->roots->persistent_root_unregister ||
            !node_fs_session) return;
    const JubeHostRootAPI* roots = node_fs_host->node->roots;
    roots->persistent_root_unregister(node_fs_session, &request->callback.item);
    roots->persistent_root_unregister(node_fs_session, &request->domain.item);
    roots->persistent_root_unregister(node_fs_session, &request->initial_error.item);
    request->roots_registered = false;
}

static void node_fs_request_destroy(void* user) {
    NodeFsRequest* request = (NodeFsRequest*)user;
    if (!request) return;
    node_fs_pending_remove(request);
    node_fs_unregister_roots(request);
    free(request->path);
    free(request->bytes);
    if (node_fs_host && node_fs_host->node && node_fs_host->node->filesystem &&
            node_fs_host->node->filesystem->read_write_release) {
        node_fs_host->node->filesystem->read_write_release(&request->operation);
    }
    free(request);
}

static bool node_fs_roots_begin(JubeRootFrame* frame, size_t count) {
    return node_fs_host && node_fs_host->node && node_fs_host->node->roots &&
        node_fs_host->node->roots->root_frame_begin &&
        node_fs_host->node->roots->root_frame_take_slot &&
        node_fs_host->node->roots->root_frame_end &&
        node_fs_host->node->roots->root_frame_begin(frame, count);
}

static Item node_fs_root_value(const uint64_t* slot) {
    return (Item){.item = slot ? *slot : 0};
}

static void node_fs_call(NodeFsRequest* request, Item* args, int arg_count) {
    if (!request || request->detached || !node_fs_host || !node_fs_host->node ||
            !node_fs_host->node->events || !node_fs_host->node->events->domain_call) return;
    // The request keeps callback and domain persistently rooted until this
    // call because work completion may compact before entering user code.
    node_fs_host->node->events->domain_call(request->domain, request->callback,
                                            ItemNull, args, arg_count);
}

static Item node_fs_system_error(const char* syscall, int error_number) {
    if (!node_fs_host || !node_fs_host->node || !node_fs_host->node->error ||
            !node_fs_host->node->error->throw_system_error) return ItemNull;
    return node_fs_host->node->error->throw_system_error(node_fs_session,
                                                        syscall ? syscall : "open", error_number);
}

static void node_fs_work(void* user) {
    NodeFsRequest* request = (NodeFsRequest*)user;
    if (!request || request->initial_error.item != 0) return;
    if (!node_fs_host || !node_fs_host->node || !node_fs_host->node->filesystem ||
            !node_fs_host->node->filesystem->read_write) {
        request->error_number = EIO;
        return;
    }
    request->operation.mode = request->mode == NODE_FS_MODE_READ ? JUBE_NODE_FILESYSTEM_READ :
        request->mode == NODE_FS_MODE_WRITE ? JUBE_NODE_FILESYSTEM_WRITE : JUBE_NODE_FILESYSTEM_APPEND;
    request->operation.path = request->path;
    request->operation.input = (const uint8_t*)request->bytes;
    request->operation.input_length = request->byte_count;
    // The host owns descriptors and raw I/O so a module image cannot retain a
    // platform filesystem dependency after it is dynamically loaded.
    if (!node_fs_host->node->filesystem->read_write(&request->operation)) {
        request->error_number = request->operation.error_number;
    }
}

static void node_fs_complete(void* user, int status) {
    NodeFsRequest* request = (NodeFsRequest*)user;
    if (!request || request->detached) return;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 3)) return;
    uint64_t* error_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* data_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* callback_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!error_root || !data_root || !callback_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return;
    }
    *callback_root = request->callback.item;
    Item error = request->initial_error;
    if (error.item == 0 && (status < 0 || request->error_number != 0)) {
        error = node_fs_system_error(request->operation.error_syscall,
                                     request->error_number != 0 ? request->error_number : EIO);
    }
    *error_root = error.item;
    if (request->mode != NODE_FS_MODE_READ) {
        Item args[1] = {node_fs_root_value(error_root)};
        node_fs_call(request, args, 1);
    } else if (error.item != 0) {
        Item args[2] = {node_fs_root_value(error_root), ItemNull};
        node_fs_call(request, args, 2);
    } else {
        Item data = node_fs_host->value->string_from_utf8_n((const char*)request->operation.output,
                                                            request->operation.output_length);
        *data_root = data.item;
        Item args[2] = {ItemNull, node_fs_root_value(data_root)};
        node_fs_call(request, args, 2);
    }
    node_fs_host->node->roots->root_frame_end(&frame);
}

static bool node_fs_copy_string(Item value, char** out_bytes, size_t* out_length) {
    if (!out_bytes || !out_length || !node_fs_host || !node_fs_host->value ||
            !node_fs_host->value->string_bytes || !node_fs_host->value->string_length) return false;
    const uint8_t* bytes = node_fs_host->value->string_bytes(value);
    size_t length = node_fs_host->value->string_length(value);
    if ((length > 0 && !bytes) || length > INT_MAX) return false;
    char* copy = (char*)malloc(length + 1);
    if (!copy) return false;
    if (length > 0) memcpy(copy, bytes, length);
    copy[length] = '\0';
    *out_bytes = copy;
    *out_length = length;
    return true;
}

static bool node_fs_copy_path(Item path_item, char** out_path, bool write, bool check_permission = true) {
    if (!node_fs_host || !node_fs_host->script || !node_fs_host->script->to_string ||
            !out_path) return false;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 1)) return false;
    uint64_t* path_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!path_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return false;
    }
    *path_root = path_item.item;
    Item path_string = node_fs_host->script->to_string(node_fs_root_value(path_root));
    *path_root = path_string.item;
    size_t path_length = 0;
    if (item_is_error(path_string) ||
            !node_fs_copy_string(node_fs_root_value(path_root), out_path, &path_length)) {
        if (item_is_error(path_string)) {
            (void)node_fs_host->script->error_lane_payload(path_string);
        }
        node_fs_host->node->roots->root_frame_end(&frame);
        return false;
    }
    if (!check_permission) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return true;
    }
    bool permitted = write ? node_fs_host->node->permission->has_fs_write(*out_path) :
                             node_fs_host->node->permission->has_fs_read(*out_path);
    if (!permitted) {
        Item permission_error = write ? node_fs_host->node->permission->check_fs_write(*out_path) :
                                        node_fs_host->node->permission->check_fs_read(*out_path);
        if (item_is_error(permission_error)) {
            (void)node_fs_host->script->error_lane_payload(permission_error);
        }
        free(*out_path);
        *out_path = NULL;
        node_fs_host->node->roots->root_frame_end(&frame);
        return false;
    }
    node_fs_host->node->roots->root_frame_end(&frame);
    return true;
}

static Item node_fs_sync_error(const char* syscall, int error_number) {
    if (!node_fs_host || !node_fs_host->node || !node_fs_host->node->error ||
            !node_fs_host->node->error->throw_system_error) return ItemNull;
    return node_fs_host->node->error->throw_system_error(node_fs_session, syscall, error_number);
}

static Item node_fs_path_operation_error(JubeNodeFilesystemPathOperation* operation) {
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    if (filesystem && filesystem->path_operation && filesystem->path_operation(operation)) {
        return node_fs_undefined();
    }
    const char* syscall = operation && operation->error_syscall ? operation->error_syscall : "fs";
    int error_number = operation && operation->error_number ? operation->error_number : EIO;
    return node_fs_sync_error(syscall, error_number);
}

static bool node_fs_descriptor_operation(JubeNodeFilesystemDescriptorOperation* operation) {
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    return filesystem && filesystem->descriptor_operation && filesystem->descriptor_operation(operation);
}

static int node_fs_descriptor_io(int descriptor, uint8_t* bytes, size_t byte_length,
                                 int64_t position, bool write, int* out_error_number) {
    JubeNodeFilesystemDescriptorOperation operation = {};
    operation.mode = write ? JUBE_NODE_FILESYSTEM_DESCRIPTOR_WRITE :
                             JUBE_NODE_FILESYSTEM_DESCRIPTOR_READ;
    operation.descriptor = descriptor;
    operation.bytes = bytes;
    operation.byte_length = byte_length;
    operation.position = position;
    if (!node_fs_descriptor_operation(&operation)) {
        if (out_error_number) *out_error_number = operation.error_number ? operation.error_number : EIO;
        return -1;
    }
    return (int)operation.transferred;
}

static bool node_fs_metadata_operation(JubeNodeFilesystemMetadataOperation* operation) {
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    return filesystem && filesystem->metadata_operation && filesystem->metadata_operation(operation);
}

static bool node_fs_access_mode(Item value, int* out_mode) {
    if (!out_mode || !node_fs_host || !node_fs_host->value || !node_fs_host->script) return false;
    int kind = node_fs_host->value->kind(value);
    if (kind == JUBE_VALUE_UNDEFINED || kind == JUBE_VALUE_NULL) {
        *out_mode = 0;
        return true;
    }
    int64_t mode = 0;
    if (kind != JUBE_VALUE_NUMBER || !node_fs_host->value->number_to_int64_exact ||
            !node_fs_host->value->number_to_int64_exact(value, &mode) || mode < 0 || mode > 7) {
        node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_VALUE", "mode must be an integer from 0 to 7");
        return false;
    }
    *out_mode = (int)mode;
    return true;
}

static Item node_fs_access_sync(Item path_item, Item mode_item) {
    int mode = 0;
    if (!node_fs_access_mode(mode_item, &mode)) return ItemNull;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, (mode & 2) != 0)) return ItemNull;
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_ACCESS;
    operation.path = path;
    operation.numeric_value = mode;
    Item result = node_fs_path_operation_error(&operation);
    free(path);
    return result;
}

static bool node_fs_option_truthy(Item options, const char* name) {
    if (!node_fs_host || !node_fs_host->value || !node_fs_host->script || !name ||
            node_fs_host->value->kind(options) != JUBE_VALUE_OBJECT) return false;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 2)) return false;
    uint64_t* options_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!options_root || !key_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return false;
    }
    *options_root = options.item;
    Item key = node_fs_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    Item value = node_fs_host->value->property_get(node_fs_root_value(options_root),
                                                   node_fs_root_value(key_root));
    bool result = node_fs_host->script->is_truthy(value);
    node_fs_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_fs_rm_sync(Item path_item, Item options) {
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, true)) return ItemNull;
    bool recursive = node_fs_option_truthy(options, "recursive");
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_RM;
    operation.path = path;
    operation.recursive = recursive;
    Item result = node_fs_path_operation_error(&operation);
    free(path);
    return result;
}

static bool node_fs_file_mode(Item value, int* out_mode) {
    if (!out_mode || !node_fs_host || !node_fs_host->value || !node_fs_host->script ||
            !node_fs_host->value->number_to_int64_exact) return false;
    int64_t mode = 0;
    if (node_fs_host->value->kind(value) != JUBE_VALUE_NUMBER ||
            !node_fs_host->value->number_to_int64_exact(value, &mode) || mode < 0 || mode > 07777) {
        node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_VALUE", "mode must be an integer from 0 to 4095");
        return false;
    }
    *out_mode = (int)mode;
    return true;
}

static Item node_fs_chmod_sync(Item path_item, Item mode_item) {
    int mode = 0;
    if (!node_fs_file_mode(mode_item, &mode)) return ItemNull;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, true)) return ItemNull;
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_CHMOD;
    operation.path = path;
    operation.numeric_value = mode;
    Item result = node_fs_path_operation_error(&operation);
    free(path);
    return result;
}

static Item node_fs_copy_file_sync(Item source_item, Item destination_item) {
    char* source = NULL;
    char* destination = NULL;
    if (!node_fs_copy_path(source_item, &source, false)) return ItemNull;
    if (!node_fs_copy_path(destination_item, &destination, true)) {
        free(source);
        return ItemNull;
    }
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    JubeNodeFilesystemCopy operation = {};
    operation.source_path = source;
    operation.destination_path = destination;
    // The host owns both descriptors so dynamically loaded node-fs cannot
    // retain platform I/O implementation or dependency state in its image.
    bool success = filesystem && filesystem->copy_file && filesystem->copy_file(&operation);
    free(source);
    free(destination);
    if (!success) return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "copyfile",
                                            operation.error_number ? operation.error_number : EIO);
    return node_fs_undefined();
}

static Item node_fs_truncate_sync(Item path_item, Item length_item) {
    if (!node_fs_host || !node_fs_host->value || !node_fs_host->value->number_to_int64_exact) return ItemNull;
    int64_t length = 0;
    if (node_fs_host->value->kind(length_item) != JUBE_VALUE_NUMBER ||
            !node_fs_host->value->number_to_int64_exact(length_item, &length) || length < 0) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_VALUE",
                                                            "length must be a non-negative integer");
    }
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, true)) return ItemNull;
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_TRUNCATE;
    operation.path = path;
    operation.numeric_value = length;
    Item result = node_fs_path_operation_error(&operation);
    free(path);
    return result;
}

static Item node_fs_read_file_sync(Item path_item, Item options) {
    (void)options;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, false)) return ItemNull;
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    if (!filesystem || !filesystem->read_write || !filesystem->read_write_release) {
        free(path);
        return node_fs_sync_error("read", EIO);
    }
    JubeNodeFilesystemReadWrite operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_READ;
    operation.path = path;
    // Sync reads share the host I/O service with queued reads, preventing the
    // module from growing a second platform-descriptor implementation.
    bool success = filesystem->read_write(&operation);
    int error_number = operation.error_number;
    free(path);
    if (!success) {
        filesystem->read_write_release(&operation);
        return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "read", error_number);
    }
    Item result = node_fs_host->value->string_from_utf8_n((const char*)operation.output,
                                                           operation.output_length);
    filesystem->read_write_release(&operation);
    return result;
}

static Item node_fs_write_file_sync(Item path_item, Item data_item, Item options, bool append) {
    (void)options;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, true)) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 1)) {
        free(path);
        return ItemNull;
    }
    uint64_t* data_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!data_root) {
        free(path);
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *data_root = data_item.item;
    Item data_string = node_fs_host->script->to_string(node_fs_root_value(data_root));
    *data_root = data_string.item;
    char* bytes = NULL;
    size_t count = 0;
    if (item_is_error(data_string) ||
            !node_fs_copy_string(node_fs_root_value(data_root), &bytes, &count)) {
        if (item_is_error(data_string)) {
            (void)node_fs_host->script->error_lane_payload(data_string);
        }
        free(path);
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    JubeNodeFilesystemReadWrite operation = {};
    operation.mode = append ? JUBE_NODE_FILESYSTEM_APPEND : JUBE_NODE_FILESYSTEM_WRITE;
    operation.path = path;
    operation.input = (const uint8_t*)bytes;
    operation.input_length = count;
    bool success = filesystem && filesystem->read_write && filesystem->read_write(&operation);
    int error_number = operation.error_number;
    free(bytes);
    free(path);
    node_fs_host->node->roots->root_frame_end(&frame);
    if (!success) {
        return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "write",
                                  error_number ? error_number : EIO);
    }
    return node_fs_undefined();
}

static Item node_fs_write_file_sync_export(Item path, Item data, Item options) {
    return node_fs_write_file_sync(path, data, options, false);
}

static Item node_fs_append_file_sync_export(Item path, Item data, Item options) {
    return node_fs_write_file_sync(path, data, options, true);
}

static Item node_fs_exists_sync(Item path_item) {
    char* path = NULL;
    size_t ignored_length = 0;
    if (!node_fs_host || !node_fs_host->script || !node_fs_host->script->to_string ||
            !node_fs_host->value) return (Item){.item = b2it(false)};
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 1)) return (Item){.item = b2it(false)};
    uint64_t* path_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!path_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return (Item){.item = b2it(false)};
    }
    *path_root = path_item.item;
    Item path_string = node_fs_host->script->to_string(node_fs_root_value(path_root));
    *path_root = path_string.item;
    if (item_is_error(path_string) ||
            !node_fs_copy_string(node_fs_root_value(path_root), &path, &ignored_length)) {
        if (item_is_error(path_string)) {
            (void)node_fs_host->script->error_lane_payload(path_string);
        }
        node_fs_host->node->roots->root_frame_end(&frame);
        return (Item){.item = b2it(false)};
    }
    JubeNodeFilesystemMetadataOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_METADATA_STAT;
    operation.path = path;
    bool exists = node_fs_metadata_operation(&operation);
    free(path);
    node_fs_host->node->roots->root_frame_end(&frame);
    return (Item){.item = b2it(exists)};
}

static Item node_fs_unlink_sync(Item path_item) {
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, true)) return ItemNull;
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_UNLINK;
    operation.path = path;
    Item result = node_fs_path_operation_error(&operation);
    free(path);
    return result;
}

static Item node_fs_rmdir_sync(Item path_item) {
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, true)) return ItemNull;
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_RMDIR;
    operation.path = path;
    Item result = node_fs_path_operation_error(&operation);
    free(path);
    return result;
}

static Item node_fs_rename_sync(Item old_path_item, Item new_path_item) {
    char* old_path = NULL;
    char* new_path = NULL;
    // Renaming mutates both directory entries, so the source requires write
    // permission too; treating it as a read bypassed the host's policy.
    if (!node_fs_copy_path(old_path_item, &old_path, true)) return ItemNull;
    if (!node_fs_copy_path(new_path_item, &new_path, true)) {
        free(old_path);
        return ItemNull;
    }
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_RENAME;
    operation.path = old_path;
    operation.secondary_path = new_path;
    Item result = node_fs_path_operation_error(&operation);
    free(old_path);
    free(new_path);
    return result;
}

static bool node_fs_options_mkdir(Item options, int* out_mode, bool* out_recursive) {
    if (!out_mode || !out_recursive || !node_fs_host || !node_fs_host->value ||
            !node_fs_host->script) return false;
    *out_mode = 0777;
    *out_recursive = false;
    int kind = node_fs_host->value->kind(options);
    if (kind == JUBE_VALUE_NUMBER) {
        *out_mode = (int)node_fs_host->script->get_number(options);
        return true;
    }
    if (kind != JUBE_VALUE_OBJECT) return true;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 3)) return false;
    uint64_t* options_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* mode_key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* recursive_key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!options_root || !mode_key_root || !recursive_key_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return false;
    }
    *options_root = options.item;
    Item mode_key = node_fs_host->value->string_from_utf8_n("mode", 4);
    *mode_key_root = mode_key.item;
    Item recursive_key = node_fs_host->value->string_from_utf8_n("recursive", 9);
    *recursive_key_root = recursive_key.item;
    Item mode = node_fs_host->value->property_get(node_fs_root_value(options_root),
                                                  node_fs_root_value(mode_key_root));
    if (node_fs_host->value->kind(mode) == JUBE_VALUE_NUMBER) {
        *out_mode = (int)node_fs_host->script->get_number(mode);
    }
    Item recursive = node_fs_host->value->property_get(node_fs_root_value(options_root),
                                                       node_fs_root_value(recursive_key_root));
    *out_recursive = node_fs_host->script->is_truthy(recursive);
    node_fs_host->node->roots->root_frame_end(&frame);
    return true;
}

static Item node_fs_mkdir_sync(Item path_item, Item options) {
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, true)) return ItemNull;
    int mode = 0777;
    bool recursive = false;
    if (!node_fs_options_mkdir(options, &mode, &recursive)) {
        free(path);
        return ItemNull;
    }
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_MKDIR;
    operation.path = path;
    operation.numeric_value = mode;
    operation.recursive = recursive;
    Item result = node_fs_path_operation_error(&operation);
    free(path);
    return result;
}

static Item node_fs_encoding_is_valid(Item encoding) {
    if (!node_fs_host || !node_fs_host->value) return ItemNull;
    int kind = node_fs_host->value->kind(encoding);
    if (kind == JUBE_VALUE_UNDEFINED || kind == JUBE_VALUE_NULL || kind != JUBE_VALUE_STRING) {
        return node_fs_undefined();
    }
    static const char* const valid[] = {
        "utf8", "utf-8", "buffer", "ascii", "base64", "base64url", "hex", "latin1",
        "binary", "ucs2", "ucs-2", "utf16le", "utf-16le",
    };
    size_t length = node_fs_host->value->string_length(encoding);
    const uint8_t* bytes = node_fs_host->value->string_bytes(encoding);
    for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        size_t candidate_length = strlen(valid[i]);
        if (length == candidate_length && bytes && memcmp(bytes, valid[i], length) == 0) {
            return node_fs_undefined();
        }
    }
    node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_VALUE",
                                                "The argument 'encoding' is invalid encoding");
    return node_fs_host->script->throw_type_error_code(
        "ERR_INVALID_ARG_VALUE", "The argument 'encoding' is invalid encoding");
}

static Item node_fs_readdir_options_valid(Item options) {
    if (!node_fs_host || !node_fs_host->value) return ItemNull;
    int kind = node_fs_host->value->kind(options);
    if (kind != JUBE_VALUE_OBJECT) return node_fs_encoding_is_valid(options);
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 2)) return ItemNull;
    uint64_t* options_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!options_root || !key_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *options_root = options.item;
    Item key = node_fs_host->value->string_from_utf8_n("encoding", 8);
    *key_root = key.item;
    Item encoding = node_fs_host->value->property_get(node_fs_root_value(options_root),
                                                      node_fs_root_value(key_root));
    Item valid = node_fs_encoding_is_valid(encoding);
    node_fs_host->node->roots->root_frame_end(&frame);
    return valid;
}

static bool node_fs_readdir_append(Item array, const char* name) {
    if (!node_fs_host || !node_fs_host->value || !name) return false;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 2)) return false;
    uint64_t* array_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* name_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!array_root || !name_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return false;
    }
    *array_root = array.item;
    Item value = node_fs_host->value->string_from_utf8_n(name, strlen(name));
    *name_root = value.item;
    node_fs_host->value->array_push(node_fs_root_value(array_root), node_fs_root_value(name_root));
    node_fs_host->node->roots->root_frame_end(&frame);
    return true;
}

static Item node_fs_readdir_sync(Item path_item, Item options) {
    Item validation = node_fs_readdir_options_valid(options);
    if (item_is_error(validation)) return validation;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, false)) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 1)) {
        free(path);
        return ItemNull;
    }
    uint64_t* array_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!array_root) {
        free(path);
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item array = node_fs_host->value->array_new(0);
    *array_root = array.item;
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    JubeNodeFilesystemDirectoryOperation operation = {};
    operation.path = path;
    bool success = filesystem && filesystem->directory_read && filesystem->directory_read_release &&
        filesystem->directory_read(&operation);
    free(path);
    if (!success) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "readdir",
                                  operation.error_number ? operation.error_number : EIO);
    }
    for (size_t index = 0; index < operation.entry_count; ++index) {
        if (!node_fs_readdir_append(node_fs_root_value(array_root), operation.entries[index])) break;
    }
    filesystem->directory_read_release(&operation);
    Item result = node_fs_root_value(array_root);
    node_fs_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_fs_realpath_sync(Item path_item, Item options) {
    Item validation = node_fs_readdir_options_valid(options);
    if (item_is_error(validation)) return validation;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, false)) return ItemNull;
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    JubeNodeFilesystemStringOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_STRING_REALPATH;
    operation.path = path;
    bool success = filesystem && filesystem->string_operation &&
        filesystem->string_operation_release && filesystem->string_operation(&operation);
    free(path);
    if (!success) return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "realpath",
                                            operation.error_number ? operation.error_number : EIO);
    Item result = node_fs_host->value->string_from_utf8_n(operation.output, operation.output_length);
    filesystem->string_operation_release(&operation);
    return result;
}

static Item node_fs_mkdtemp_sync(Item prefix_item, Item options) {
    Item validation = node_fs_readdir_options_valid(options);
    if (item_is_error(validation)) return validation;
    char* prefix = NULL;
    if (!node_fs_copy_path(prefix_item, &prefix, true)) return ItemNull;
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    JubeNodeFilesystemStringOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_STRING_MKDTEMP;
    operation.path = prefix;
    bool success = filesystem && filesystem->string_operation &&
        filesystem->string_operation_release && filesystem->string_operation(&operation);
    free(prefix);
    if (!success) return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "mkdtemp",
                                            operation.error_number ? operation.error_number : EIO);
    Item output = node_fs_host->value->string_from_utf8_n(operation.output, operation.output_length);
    filesystem->string_operation_release(&operation);
    return output;
}

static Item node_fs_link_sync(Item existing_path_item, Item new_path_item) {
    char* existing_path = NULL;
    char* new_path = NULL;
    if (!node_fs_copy_path(existing_path_item, &existing_path, false)) return ItemNull;
    if (!node_fs_copy_path(new_path_item, &new_path, true)) {
        free(existing_path);
        return ItemNull;
    }
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_LINK;
    operation.path = existing_path;
    operation.secondary_path = new_path;
    Item result = node_fs_path_operation_error(&operation);
    free(existing_path);
    free(new_path);
    return result;
}

static Item node_fs_symlink_sync(Item target_item, Item path_item) {
    char* target = NULL;
    char* path = NULL;
    if (!node_fs_copy_path(target_item, &target, false)) return ItemNull;
    if (!node_fs_copy_path(path_item, &path, true)) {
        free(target);
        return ItemNull;
    }
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_SYMLINK;
    operation.path = target;
    operation.secondary_path = path;
    Item result = node_fs_path_operation_error(&operation);
    free(target);
    free(path);
    return result;
}

static Item node_fs_schedule(Item path_item, Item data_item, Item callback, NodeFsMode mode) {
    if (!node_fs_host || !node_fs_session || !node_fs_host->value || !node_fs_host->script ||
            !node_fs_host->node || !node_fs_host->node->async_ops ||
            !node_fs_host->node->async_ops->work_submit || !node_fs_host->node->filesystem ||
            !node_fs_host->node->filesystem->read_write ||
            !node_fs_host->node->filesystem->read_write_release || !node_fs_host->node->events ||
            !node_fs_host->node->events->domain_current || !node_fs_host->node->permission ||
            !node_fs_host->node->permission->has_fs_read || !node_fs_host->node->permission->has_fs_write) {
        return ItemNull;
    }
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE",
                                                            "callback must be a function");
    }
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 3)) return ItemNull;
    uint64_t* path_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* data_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* callback_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!path_root || !data_root || !callback_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *path_root = path_item.item;
    *data_root = data_item.item;
    *callback_root = callback.item;
    Item path_string = node_fs_host->script->to_string(node_fs_root_value(path_root));
    *path_root = path_string.item;
    if (item_is_error(path_string)) {
        (void)node_fs_host->script->error_lane_payload(path_string);
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    NodeFsRequest* request = (NodeFsRequest*)calloc(1, sizeof(NodeFsRequest));
    if (!request || !node_fs_copy_string(node_fs_root_value(path_root), &request->path,
                                         &request->byte_count)) {
        free(request);
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    request->byte_count = 0;
    request->mode = mode;
    request->callback = node_fs_root_value(callback_root);
    request->domain = node_fs_host->node->events->domain_current();
    if (mode != NODE_FS_MODE_READ) {
        Item data_string = node_fs_host->script->to_string(node_fs_root_value(data_root));
        *data_root = data_string.item;
        if (item_is_error(data_string) ||
                !node_fs_copy_string(node_fs_root_value(data_root), &request->bytes,
                                     &request->byte_count)) {
            if (item_is_error(data_string)) {
                (void)node_fs_host->script->error_lane_payload(data_string);
            }
            node_fs_request_destroy(request);
            node_fs_host->node->roots->root_frame_end(&frame);
            return ItemNull;
        }
    }
    if (!node_fs_register_roots(request)) {
        node_fs_request_destroy(request);
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    bool permitted = mode != NODE_FS_MODE_READ ? node_fs_host->node->permission->has_fs_write(request->path) :
                             node_fs_host->node->permission->has_fs_read(request->path);
    if (!permitted) {
        request->initial_error = mode != NODE_FS_MODE_READ ?
            node_fs_host->node->permission->check_fs_write(request->path) :
            node_fs_host->node->permission->check_fs_read(request->path);
    }
    node_fs_pending_add(request);
    int submit = node_fs_host->node->async_ops->work_submit(node_fs_session, node_fs_work,
                                                             node_fs_complete,
                                                             node_fs_request_destroy, request,
                                                             &request->work_request_id);
    node_fs_host->node->roots->root_frame_end(&frame);
    if (submit != 0) {
        node_fs_request_destroy(request);
        return ItemNull;
    }
    return node_fs_undefined();
}

static Item node_fs_read_file(Item path, Item options_or_callback, Item callback) {
    Item actual_callback = node_fs_host && node_fs_host->value &&
            node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION ?
        options_or_callback : callback;
    return node_fs_schedule(path, ItemNull, actual_callback, NODE_FS_MODE_READ);
}

static Item node_fs_write_file(Item path, Item data, Item options_or_callback, Item callback) {
    Item actual_callback = node_fs_host && node_fs_host->value &&
            node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION ?
        options_or_callback : callback;
    return node_fs_schedule(path, data, actual_callback, NODE_FS_MODE_WRITE);
}

static Item node_fs_append_file(Item path, Item data, Item options_or_callback, Item callback) {
    Item actual_callback = node_fs_host && node_fs_host->value &&
            node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION ?
        options_or_callback : callback;
    return node_fs_schedule(path, data, actual_callback, NODE_FS_MODE_APPEND);
}

static bool node_fs_nullish(Item value) {
    int kind = node_fs_host->value->kind(value);
    return kind == JUBE_VALUE_NULL || kind == JUBE_VALUE_UNDEFINED;
}

static Item node_fs_promise_read_callback(Item env_item, Item error, Item data) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || !node_fs_host || !node_fs_host->script ||
            !node_fs_host->script->call_function) return node_fs_undefined();
    Item args[1] = {node_fs_nullish(error) ? data : error};
    Item target = node_fs_nullish(error) ? env[0] : env[1];
    node_fs_host->script->call_function(target, node_fs_undefined(), args, 1);
    return node_fs_undefined();
}

static Item node_fs_promise_write_callback(Item env_item, Item error) {
    Item* env = (Item*)(uintptr_t)env_item.item;
    if (!env || !node_fs_host || !node_fs_host->script ||
            !node_fs_host->script->call_function) return node_fs_undefined();
    Item args[1] = {node_fs_nullish(error) ? node_fs_undefined() : error};
    Item target = node_fs_nullish(error) ? env[0] : env[1];
    node_fs_host->script->call_function(target, node_fs_undefined(), args, 1);
    return node_fs_undefined();
}

static Item node_fs_promise_capability(NodeFsMode mode, Item path, Item data, Item options) {
    if (!node_fs_host || !node_fs_host->script || !node_fs_host->script->promise_with_resolvers ||
            !node_fs_host->script->new_closure || !node_fs_host->script->closure_env_new ||
            !node_fs_host->value || !node_fs_host->value->property_get) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 5)) return ItemNull;
    uint64_t* capability_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* promise_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* resolve_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* reject_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* callback_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!capability_root || !promise_root || !resolve_root || !reject_root || !callback_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item capability = node_fs_host->script->promise_with_resolvers();
    *capability_root = capability.item;
    Item promise_key = node_fs_host->value->string_from_utf8_n("promise", 7);
    Item resolve_key = node_fs_host->value->string_from_utf8_n("resolve", 7);
    Item reject_key = node_fs_host->value->string_from_utf8_n("reject", 6);
    Item promise = node_fs_host->value->property_get(node_fs_root_value(capability_root), promise_key);
    *promise_root = promise.item;
    Item resolve = node_fs_host->value->property_get(node_fs_root_value(capability_root), resolve_key);
    *resolve_root = resolve.item;
    Item reject = node_fs_host->value->property_get(node_fs_root_value(capability_root), reject_key);
    *reject_root = reject.item;
    Item* environment = node_fs_host->script->closure_env_new(2);
    if (!environment) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    environment[0] = node_fs_root_value(resolve_root);
    environment[1] = node_fs_root_value(reject_root);
    Item callback = node_fs_host->script->new_closure(
        mode != NODE_FS_MODE_READ ? (void*)node_fs_promise_write_callback : (void*)node_fs_promise_read_callback,
        mode != NODE_FS_MODE_READ ? 1 : 2, environment, 2);
    *callback_root = callback.item;
    Item scheduled = node_fs_schedule(path, data, node_fs_root_value(callback_root), mode);
    if (scheduled.item == 0 || item_is_error(scheduled)) {
        Item error = item_is_error(scheduled) ?
            node_fs_host->script->error_lane_payload(scheduled) : ItemNull;
        Item reject_args[1] = {error};
        node_fs_host->script->call_function(node_fs_root_value(reject_root), node_fs_undefined(),
                                            reject_args, 1);
    }
    Item result = node_fs_root_value(promise_root);
    node_fs_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_fs_promises_read_file(Item path, Item options) {
    return node_fs_promise_capability(NODE_FS_MODE_READ, path, ItemNull, options);
}

static Item node_fs_promises_write_file(Item path, Item data, Item options) {
    return node_fs_promise_capability(NODE_FS_MODE_WRITE, path, data, options);
}

static Item node_fs_promises_append_file(Item path, Item data, Item options) {
    return node_fs_promise_capability(NODE_FS_MODE_APPEND, path, data, options);
}

static Item node_fs_promise_settled(Item value, bool rejected) {
    if (!node_fs_host || !node_fs_host->script || !node_fs_host->value ||
            !node_fs_host->script->promise_with_resolvers || !node_fs_host->script->call_function ||
            !node_fs_host->value->property_get) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 4)) return ItemNull;
    uint64_t* capability_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* promise_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* settle_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!capability_root || !promise_root || !settle_root || !value_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *value_root = value.item;
    Item capability = node_fs_host->script->promise_with_resolvers();
    *capability_root = capability.item;
    Item promise_key = node_fs_host->value->string_from_utf8_n("promise", 7);
    const char* settle_name = rejected ? "reject" : "resolve";
    Item settle_key = node_fs_host->value->string_from_utf8_n(settle_name, strlen(settle_name));
    Item promise = node_fs_host->value->property_get(node_fs_root_value(capability_root), promise_key);
    *promise_root = promise.item;
    Item settle = node_fs_host->value->property_get(node_fs_root_value(capability_root), settle_key);
    *settle_root = settle.item;
    Item args[1] = {node_fs_root_value(value_root)};
    node_fs_host->script->call_function(node_fs_root_value(settle_root), node_fs_undefined(), args, 1);
    Item result = node_fs_root_value(promise_root);
    node_fs_host->node->roots->root_frame_end(&frame);
    return result;
}

static NodeFsFileHandle* node_fs_filehandle_data(Item receiver) {
    if (!node_fs_host || !node_fs_host->value || !node_fs_host->value->native_object_data) return NULL;
    return (NodeFsFileHandle*)node_fs_host->value->native_object_data(receiver, &node_fs_types[0]);
}

static void node_fs_filehandle_destroy(void* payload) {
    NodeFsFileHandle* handle = (NodeFsFileHandle*)payload;
    if (!handle) return;
    // A forgotten FileHandle must still release its OS descriptor when the
    // owning Jube object dies; otherwise dynamic module teardown leaks fds.
    if (handle->descriptor >= 0) {
        JubeNodeFilesystemDescriptorOperation operation = {};
        operation.mode = JUBE_NODE_FILESYSTEM_DESCRIPTOR_CLOSE;
        operation.descriptor = handle->descriptor;
        // FileHandle finalization can run during module teardown; the host is
        // still responsible for closing the descriptor even when no JS error
        // can be surfaced to the caller.
        node_fs_descriptor_operation(&operation);
    }
    free(handle);
}

static int node_fs_filehandle_fd_get(Item receiver, Item* out) {
    NodeFsFileHandle* handle = node_fs_filehandle_data(receiver);
    if (!handle || !out) return 0;
    *out = (Item){.item = i2it((int64_t)handle->descriptor)};
    return 1;
}

static int node_fs_filehandle_close_call(Item receiver, Item* args, int argc, Item* out) {
    (void)args;
    (void)argc;
    NodeFsFileHandle* handle = node_fs_filehandle_data(receiver);
    if (!handle || !out) return 0;
    JubeNodeFilesystemDescriptorOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_DESCRIPTOR_CLOSE;
    operation.descriptor = handle->descriptor;
    if (handle->descriptor >= 0 && !node_fs_descriptor_operation(&operation)) {
        int error_number = operation.error_number ? operation.error_number : EIO;
        Item error = node_fs_host->node->error->throw_system_error(node_fs_session, "close",
                                                                    error_number);
        *out = node_fs_promise_settled(error, true);
        return 1;
    }
    handle->descriptor = -1;
    *out = node_fs_promise_settled(node_fs_undefined(), false);
    return 1;
}

static int node_fs_filehandle_read_call(Item receiver, Item* args, int argc, Item* out) {
    NodeFsFileHandle* handle = node_fs_filehandle_data(receiver);
    if (!handle || !out) return 0;
    if (handle->descriptor < 0) {
        Item error = node_fs_host->node->error->throw_error_code(
            node_fs_session, "EBADF", "The FileHandle has been transferred");
        *out = node_fs_promise_settled(error, true);
        return 1;
    }
    Item buffer = argc > 0 ? args[0] : node_fs_undefined();
    Item offset = argc > 1 ? args[1] : node_fs_undefined();
    Item length = argc > 2 ? args[2] : node_fs_undefined();
    Item position = argc > 3 ? args[3] : node_fs_undefined();
    Item bytes_read = node_fs_read_sync_export((Item){.item = i2it((int64_t)handle->descriptor)},
                                               buffer, offset, length, position);
    if (item_is_error(bytes_read)) {
        *out = node_fs_promise_settled(
            node_fs_host->script->error_lane_payload(bytes_read), true);
        return 1;
    }
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 5)) return 0;
    uint64_t* buffer_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* count_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* result_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* count_key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* buffer_key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!buffer_root || !count_root || !result_root || !count_key_root || !buffer_key_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return 0;
    }
    *buffer_root = buffer.item;
    *count_root = bytes_read.item;
    Item result = node_fs_host->script->object_create(ItemNull);
    *result_root = result.item;
    Item count_key = node_fs_host->value->string_from_utf8_n("bytesRead", 9);
    *count_key_root = count_key.item;
    Item buffer_key = node_fs_host->value->string_from_utf8_n("buffer", 6);
    *buffer_key_root = buffer_key.item;
    node_fs_host->value->property_set(node_fs_root_value(result_root),
                                      node_fs_root_value(count_key_root),
                                      node_fs_root_value(count_root));
    node_fs_host->value->property_set(node_fs_root_value(result_root),
                                      node_fs_root_value(buffer_key_root),
                                      node_fs_root_value(buffer_root));
    *out = node_fs_promise_settled(node_fs_root_value(result_root), false);
    node_fs_host->node->roots->root_frame_end(&frame);
    return 1;
}

static int node_fs_filehandle_read_file_call(Item receiver, Item* args, int argc, Item* out) {
    NodeFsFileHandle* handle = node_fs_filehandle_data(receiver);
    if (!handle || !out) return 0;
    Item options = argc > 0 ? args[0] : node_fs_undefined();
    Item validation = node_fs_readdir_options_valid(options);
    if (item_is_error(validation)) {
        Item error = node_fs_host->script->error_lane_payload(validation);
        *out = node_fs_promise_settled(error, true);
        return 1;
    }
    if (handle->descriptor < 0) {
        Item error = node_fs_host->node->error->throw_error_code(
            node_fs_session, "EBADF", "The FileHandle has been transferred");
        *out = node_fs_promise_settled(error, true);
        return 1;
    }
    JubeNodeFilesystemMetadataOperation metadata = {};
    metadata.mode = JUBE_NODE_FILESYSTEM_METADATA_FSTAT;
    metadata.descriptor = handle->descriptor;
    if (!node_fs_metadata_operation(&metadata) || metadata.value.size > (uint64_t)INT_MAX) {
        int error_number = metadata.error_number ? metadata.error_number : EFBIG;
        Item error = node_fs_host->node->error->throw_system_error(node_fs_session, "fstat",
                                                                    error_number);
        *out = node_fs_promise_settled(error, true);
        return 1;
    }
    size_t byte_count = (size_t)metadata.value.size;
    char* bytes = (char*)malloc(byte_count > 0 ? byte_count : 1);
    if (!bytes) {
        Item error = node_fs_host->node->error->throw_system_error(node_fs_session, "read", ENOMEM);
        *out = node_fs_promise_settled(error, true);
        return 1;
    }
    size_t offset = 0;
    int error_number = 0;
    while (offset < byte_count) {
        int count = node_fs_descriptor_io(handle->descriptor, (uint8_t*)bytes + offset,
                                          byte_count - offset, -1, false, &error_number);
        if (count < 0) {
            break;
        }
        if (count == 0) break;
        offset += (size_t)count;
    }
    if (error_number != 0) {
        free(bytes);
        Item error = node_fs_host->node->error->throw_system_error(node_fs_session, "read",
                                                                    error_number);
        *out = node_fs_promise_settled(error, true);
        return 1;
    }
    Item result = node_fs_host->value->string_from_utf8_n(bytes, offset);
    free(bytes);
    *out = node_fs_promise_settled(result, false);
    return 1;
}

static void node_fs_stats_destroy(void* payload) {
    free(payload);
}

static NodeFsStats* node_fs_stats_data(Item receiver) {
    if (!node_fs_host || !node_fs_host->value || !node_fs_host->value->native_object_data) return NULL;
    return (NodeFsStats*)node_fs_host->value->native_object_data(receiver, &node_fs_types[1]);
}

static Item node_fs_stats_unsigned(uint64_t value, bool bigint) {
    if (!bigint) return node_fs_host->script->make_number((double)value);
    char text[32];
    int length = snprintf(text, sizeof(text), "%llu", (unsigned long long)value);
    return node_fs_host->script->bigint_from_decimal(text, (size_t)length);
}

enum NodeFsStatTime {
    NODE_FS_STAT_ATIME,
    NODE_FS_STAT_MTIME,
    NODE_FS_STAT_CTIME,
    NODE_FS_STAT_BIRTHTIME,
};

static int64_t node_fs_stats_time_millis(const JubeNodeFilesystemMetadata* value, NodeFsStatTime which) {
    if (!value) return 0;
    if (which == NODE_FS_STAT_ATIME) return value->atime_millis;
    if (which == NODE_FS_STAT_MTIME) return value->mtime_millis;
    if (which == NODE_FS_STAT_CTIME) return value->ctime_millis;
    return value->birthtime_millis;
}

static Item node_fs_stats_time_value(const NodeFsStats* stats, NodeFsStatTime which) {
    int64_t millis = node_fs_stats_time_millis(&stats->value, which);
    if (!stats->bigint) return node_fs_host->script->make_number((double)millis);
    char text[32];
    int length = snprintf(text, sizeof(text), "%lld", (long long)millis);
    return node_fs_host->script->bigint_from_decimal(text, (size_t)length);
}

static int node_fs_stats_size_get(Item receiver, Item* out) {
    NodeFsStats* stats = node_fs_stats_data(receiver);
    if (!stats || !out) return 0;
    *out = node_fs_stats_unsigned(stats->value.size, stats->bigint);
    return 1;
}

static int node_fs_stats_ino_get(Item receiver, Item* out) {
    NodeFsStats* stats = node_fs_stats_data(receiver);
    if (!stats || !out) return 0;
    *out = node_fs_stats_unsigned(stats->value.ino, stats->bigint);
    return 1;
}

static int node_fs_stats_mtime_ms_get(Item receiver, Item* out) {
    NodeFsStats* stats = node_fs_stats_data(receiver);
    if (!stats || !out) return 0;
    *out = node_fs_stats_time_value(stats, NODE_FS_STAT_MTIME);
    return 1;
}

#define NODE_FS_STATS_TIME_GET(function_name, which) \
static int function_name(Item receiver, Item* out) { \
    NodeFsStats* stats = node_fs_stats_data(receiver); \
    if (!stats || !out) return 0; \
    *out = node_fs_stats_time_value(stats, which); \
    return 1; \
}

NODE_FS_STATS_TIME_GET(node_fs_stats_atime_ms_get, NODE_FS_STAT_ATIME)
NODE_FS_STATS_TIME_GET(node_fs_stats_ctime_ms_get, NODE_FS_STAT_CTIME)
NODE_FS_STATS_TIME_GET(node_fs_stats_birthtime_ms_get, NODE_FS_STAT_BIRTHTIME)

#define NODE_FS_STATS_DATE_GET(function_name, which) \
static int function_name(Item receiver, Item* out) { \
    NodeFsStats* stats = node_fs_stats_data(receiver); \
    if (!stats || !out) return 0; \
    Item millis = node_fs_host->script->make_number((double)node_fs_stats_time_millis(&stats->value, which)); \
    *out = node_fs_host->script->date_new_from(millis); \
    return 1; \
}

NODE_FS_STATS_DATE_GET(node_fs_stats_atime_get, NODE_FS_STAT_ATIME)
NODE_FS_STATS_DATE_GET(node_fs_stats_mtime_get, NODE_FS_STAT_MTIME)
NODE_FS_STATS_DATE_GET(node_fs_stats_ctime_get, NODE_FS_STAT_CTIME)
NODE_FS_STATS_DATE_GET(node_fs_stats_birthtime_get, NODE_FS_STAT_BIRTHTIME)

#define NODE_FS_STATS_UINT_GET(function_name, field_name) \
static int function_name(Item receiver, Item* out) { \
    NodeFsStats* stats = node_fs_stats_data(receiver); \
    if (!stats || !out) return 0; \
    *out = node_fs_stats_unsigned((uint64_t)stats->value.field_name, stats->bigint); \
    return 1; \
}

NODE_FS_STATS_UINT_GET(node_fs_stats_mode_get, mode)
NODE_FS_STATS_UINT_GET(node_fs_stats_nlink_get, nlink)
NODE_FS_STATS_UINT_GET(node_fs_stats_dev_get, dev)

static int node_fs_stats_uid_get(Item receiver, Item* out) {
    NodeFsStats* stats = node_fs_stats_data(receiver);
    if (!stats || !out) return 0;
    *out = node_fs_stats_unsigned(stats->value.uid, stats->bigint);
    return 1;
}

static int node_fs_stats_gid_get(Item receiver, Item* out) {
    NodeFsStats* stats = node_fs_stats_data(receiver);
    if (!stats || !out) return 0;
    *out = node_fs_stats_unsigned(stats->value.gid, stats->bigint);
    return 1;
}

static int node_fs_stats_rdev_get(Item receiver, Item* out) {
    NodeFsStats* stats = node_fs_stats_data(receiver);
    if (!stats || !out) return 0;
    *out = node_fs_stats_unsigned(stats->value.rdev, stats->bigint);
    return 1;
}

static int node_fs_stats_blksize_get(Item receiver, Item* out) {
    NodeFsStats* stats = node_fs_stats_data(receiver);
    if (!stats || !out) return 0;
    *out = node_fs_stats_unsigned(stats->value.blksize, stats->bigint);
    return 1;
}

static int node_fs_stats_blocks_get(Item receiver, Item* out) {
    NodeFsStats* stats = node_fs_stats_data(receiver);
    if (!stats || !out) return 0;
    *out = node_fs_stats_unsigned(stats->value.blocks, stats->bigint);
    return 1;
}

static int node_fs_stats_kind(Item receiver, int mode, Item* out) {
    NodeFsStats* stats = node_fs_stats_data(receiver);
    if (!stats || !out) return 0;
    *out = (Item){.item = b2it((stats->value.mode & S_IFMT) == mode)};
    return 1;
}

static int node_fs_stats_is_file(Item receiver, Item* args, int argc, Item* out) {
    (void)args; (void)argc;
    return node_fs_stats_kind(receiver, S_IFREG, out);
}

static int node_fs_stats_is_directory(Item receiver, Item* args, int argc, Item* out) {
    (void)args; (void)argc;
    return node_fs_stats_kind(receiver, S_IFDIR, out);
}

static int node_fs_stats_is_symbolic_link(Item receiver, Item* args, int argc, Item* out) {
    (void)args; (void)argc;
#ifdef S_IFLNK
    return node_fs_stats_kind(receiver, S_IFLNK, out);
#else
    if (out) *out = (Item){.item = b2it(false)};
    return out != NULL;
#endif
}

static int node_fs_stats_is_block_device(Item receiver, Item* args, int argc, Item* out) {
    (void)args; (void)argc;
#ifdef S_IFBLK
    return node_fs_stats_kind(receiver, S_IFBLK, out);
#else
    if (out) *out = (Item){.item = b2it(false)};
    return out != NULL;
#endif
}

static int node_fs_stats_is_character_device(Item receiver, Item* args, int argc, Item* out) {
    (void)args; (void)argc;
#ifdef S_IFCHR
    return node_fs_stats_kind(receiver, S_IFCHR, out);
#else
    if (out) *out = (Item){.item = b2it(false)};
    return out != NULL;
#endif
}

static int node_fs_stats_is_fifo(Item receiver, Item* args, int argc, Item* out) {
    (void)args; (void)argc;
#ifdef S_IFIFO
    return node_fs_stats_kind(receiver, S_IFIFO, out);
#else
    if (out) *out = (Item){.item = b2it(false)};
    return out != NULL;
#endif
}

static int node_fs_stats_is_socket(Item receiver, Item* args, int argc, Item* out) {
    (void)args; (void)argc;
#ifdef S_IFSOCK
    return node_fs_stats_kind(receiver, S_IFSOCK, out);
#else
    if (out) *out = (Item){.item = b2it(false)};
    return out != NULL;
#endif
}

static bool node_fs_options_bigint(Item options) {
    if (!node_fs_host || !node_fs_host->value || !node_fs_host->script ||
            node_fs_host->value->kind(options) != JUBE_VALUE_OBJECT) return false;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 2)) return false;
    uint64_t* options_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!options_root || !key_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return false;
    }
    *options_root = options.item;
    Item key = node_fs_host->value->string_from_utf8_n("bigint", 6);
    *key_root = key.item;
    Item value = node_fs_host->value->property_get(node_fs_root_value(options_root),
                                                   node_fs_root_value(key_root));
    bool result = node_fs_host->script->is_truthy(value);
    node_fs_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_fs_stats_wrap(NodeFsStats* stats, Item options) {
    if (!stats) return ItemNull;
    stats->bigint = node_fs_options_bigint(options);
    Item object = node_fs_host->value->native_object_new(&node_fs_types[1], stats);
    if (object.item == 0) {
        node_fs_stats_destroy(stats);
        return ItemNull;
    }
    return object;
}

static Item node_fs_stat_sync(Item path_item, Item options, bool link) {
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, false)) return ItemNull;
    NodeFsStats* stats = (NodeFsStats*)calloc(1, sizeof(NodeFsStats));
    if (!stats) {
        free(path);
        return node_fs_sync_error(link ? "lstat" : "stat", ENOMEM);
    }
    JubeNodeFilesystemMetadataOperation operation = {};
    operation.mode = link ? JUBE_NODE_FILESYSTEM_METADATA_LSTAT : JUBE_NODE_FILESYSTEM_METADATA_STAT;
    operation.path = path;
    bool loaded = node_fs_metadata_operation(&operation);
    free(path);
    if (!loaded) {
        free(stats);
        return node_fs_sync_error(operation.error_syscall ? operation.error_syscall :
                                  (link ? "lstat" : "stat"),
                                  operation.error_number ? operation.error_number : EIO);
    }
    stats->value = operation.value;
    return node_fs_stats_wrap(stats, options);
}

static Item node_fs_stat_sync_export(Item path, Item options) {
    return node_fs_stat_sync(path, options, false);
}

static Item node_fs_lstat_sync_export(Item path, Item options) {
    return node_fs_stat_sync(path, options, true);
}

static Item node_fs_fstat_sync_export(Item descriptor_item, Item options) {
    if (!node_fs_host || !node_fs_host->value ||
            node_fs_host->value->kind(descriptor_item) != JUBE_VALUE_NUMBER) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "fd must be a number");
    }
    int descriptor = (int)node_fs_host->script->get_number(descriptor_item);
    NodeFsStats* stats = (NodeFsStats*)calloc(1, sizeof(NodeFsStats));
    if (!stats) return node_fs_sync_error("fstat", ENOMEM);
    JubeNodeFilesystemMetadataOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_METADATA_FSTAT;
    operation.descriptor = descriptor;
    if (!node_fs_metadata_operation(&operation)) {
        free(stats);
        return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "fstat",
                                  operation.error_number ? operation.error_number : EIO);
    }
    stats->value = operation.value;
    return node_fs_stats_wrap(stats, options);
}

static Item node_fs_stat_callback_complete(Item callback, Item value, bool failed) {
    if (!node_fs_host || !node_fs_host->value || !node_fs_host->script ||
            !node_fs_host->script->call_function ||
            node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 2)) return ItemNull;
    uint64_t* callback_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!callback_root || !value_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *callback_root = callback.item;
    *value_root = value.item;
    Item args[2] = {failed ? node_fs_root_value(value_root) : ItemNull,
                    failed ? ItemNull : node_fs_root_value(value_root)};
    // The result may be a branded Stats object; retain it until the callback
    // consumes it because entering JS can compact the heap before arguments
    // are observed.
    node_fs_host->script->call_function(node_fs_root_value(callback_root), node_fs_undefined(),
                                        args, failed ? 1 : 2);
    node_fs_host->node->roots->root_frame_end(&frame);
    return node_fs_undefined();
}

static Item node_fs_stat_callback(Item path, Item options_or_callback, Item callback, bool link) {
    Item options = options_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION) {
        options = node_fs_undefined();
        actual_callback = options_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item result = node_fs_stat_sync(path, options, link);
    if (item_is_error(result)) {
        Item error = node_fs_host->script->error_lane_payload(result);
        return node_fs_stat_callback_complete(actual_callback, error, true);
    }
    return node_fs_stat_callback_complete(actual_callback, result, false);
}

static Item node_fs_stat_export(Item path, Item options_or_callback, Item callback) {
    return node_fs_stat_callback(path, options_or_callback, callback, false);
}

static Item node_fs_lstat_export(Item path, Item options_or_callback, Item callback) {
    return node_fs_stat_callback(path, options_or_callback, callback, true);
}

static Item node_fs_fstat_export(Item descriptor, Item options_or_callback, Item callback) {
    Item options = options_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION) {
        options = node_fs_undefined();
        actual_callback = options_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item result = node_fs_fstat_sync_export(descriptor, options);
    if (item_is_error(result)) {
        Item error = node_fs_host->script->error_lane_payload(result);
        return node_fs_stat_callback_complete(actual_callback, error, true);
    }
    return node_fs_stat_callback_complete(actual_callback, result, false);
}

static Item node_fs_promises_stat(Item path, Item options) {
    Item result = node_fs_stat_sync(path, options, false);
    if (item_is_error(result)) {
        return node_fs_promise_settled(node_fs_host->script->error_lane_payload(result), true);
    }
    return node_fs_promise_settled(result, false);
}

static Item node_fs_promises_lstat(Item path, Item options) {
    Item result = node_fs_stat_sync(path, options, true);
    if (item_is_error(result)) {
        return node_fs_promise_settled(node_fs_host->script->error_lane_payload(result), true);
    }
    return node_fs_promise_settled(result, false);
}

static Item node_fs_promise_from_sync_result(Item result) {
    if (item_is_error(result)) {
        return node_fs_promise_settled(node_fs_host->script->error_lane_payload(result), true);
    }
    return node_fs_promise_settled(result, false);
}

static Item node_fs_promises_mkdir(Item path, Item options) {
    return node_fs_promise_from_sync_result(node_fs_mkdir_sync(path, options));
}

static Item node_fs_promises_unlink(Item path) {
    return node_fs_promise_from_sync_result(node_fs_unlink_sync(path));
}

static Item node_fs_promises_rename(Item old_path, Item new_path) {
    return node_fs_promise_from_sync_result(node_fs_rename_sync(old_path, new_path));
}

static Item node_fs_promises_readdir(Item path, Item options) {
    return node_fs_promise_from_sync_result(node_fs_readdir_sync(path, options));
}

static Item node_fs_promises_access(Item path, Item mode) {
    return node_fs_promise_from_sync_result(node_fs_access_sync(path, mode));
}

static Item node_fs_promises_chmod(Item path, Item mode) {
    return node_fs_promise_from_sync_result(node_fs_chmod_sync(path, mode));
}

static Item node_fs_promises_copy_file(Item source, Item destination) {
    return node_fs_promise_from_sync_result(node_fs_copy_file_sync(source, destination));
}

static Item node_fs_promises_truncate(Item path, Item length) {
    return node_fs_promise_from_sync_result(node_fs_truncate_sync(path, length));
}

static Item node_fs_promises_rm(Item path, Item options) {
    return node_fs_promise_from_sync_result(node_fs_rm_sync(path, options));
}

static Item node_fs_promises_realpath(Item path, Item options) {
    return node_fs_promise_from_sync_result(node_fs_realpath_sync(path, options));
}

static Item node_fs_promises_mkdtemp(Item prefix, Item options) {
    return node_fs_promise_from_sync_result(node_fs_mkdtemp_sync(prefix, options));
}

static Item node_fs_promises_symlink(Item target, Item path) {
    return node_fs_promise_from_sync_result(node_fs_symlink_sync(target, path));
}

static Item node_fs_callback_error_only(Item callback, Item error) {
    if (!node_fs_host || !node_fs_host->value || !node_fs_host->script ||
            !node_fs_host->script->call_function ||
            node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 2)) return ItemNull;
    uint64_t* callback_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* error_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!callback_root || !error_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *callback_root = callback.item;
    *error_root = error.item;
    Item args[1] = {node_fs_root_value(error_root)};
    node_fs_host->script->call_function(node_fs_root_value(callback_root), node_fs_undefined(), args, 1);
    node_fs_host->node->roots->root_frame_end(&frame);
    return node_fs_undefined();
}

static Item node_fs_open_export(Item path, Item flags, Item mode_or_callback, Item callback) {
    Item mode = mode_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(mode_or_callback) == JUBE_VALUE_FUNCTION) {
        mode = node_fs_undefined();
        actual_callback = mode_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item descriptor = node_fs_open_sync_export(path, flags, mode);
    if (item_is_error(descriptor)) {
        return node_fs_stat_callback_complete(actual_callback,
                                              node_fs_host->script->error_lane_payload(descriptor), true);
    }
    return node_fs_stat_callback_complete(actual_callback, descriptor, false);
}

static Item node_fs_close_export(Item descriptor, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item close_result = node_fs_close_sync_export(descriptor);
    if (item_is_error(close_result)) {
        return node_fs_callback_error_only(callback,
                                           node_fs_host->script->error_lane_payload(close_result));
    }
    return node_fs_callback_error_only(callback, ItemNull);
}

static Item node_fs_callback_bytes_complete(Item callback, Item error, Item byte_count, Item buffer,
                                            bool failed) {
    if (!node_fs_host || !node_fs_host->value || !node_fs_host->script ||
            !node_fs_host->script->call_function ||
            node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 4)) return ItemNull;
    uint64_t* callback_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* error_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* count_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* buffer_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!callback_root || !error_root || !count_root || !buffer_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *callback_root = callback.item;
    *error_root = error.item;
    *count_root = byte_count.item;
    *buffer_root = buffer.item;
    Item args[3] = {node_fs_root_value(error_root), node_fs_root_value(count_root),
                    node_fs_root_value(buffer_root)};
    node_fs_host->script->call_function(node_fs_root_value(callback_root), node_fs_undefined(), args,
                                        failed ? 1 : 3);
    node_fs_host->node->roots->root_frame_end(&frame);
    return node_fs_undefined();
}

static bool node_fs_root_arguments(JubeRootFrame* frame, uint64_t** roots, const Item* values,
                                   int count) {
    if (!frame || !roots || !values || count <= 0 || !node_fs_roots_begin(frame, count)) return false;
    for (int index = 0; index < count; ++index) {
        roots[index] = node_fs_host->node->roots->root_frame_take_slot(frame);
        if (!roots[index]) {
            node_fs_host->node->roots->root_frame_end(frame);
            return false;
        }
        *roots[index] = values[index].item;
    }
    return true;
}

static Item node_fs_read_export(Item descriptor, Item buffer, Item offset, Item length, Item position,
                                Item callback) {
    Item values[6] = {descriptor, buffer, offset, length, position, callback};
    uint64_t* roots[6] = {};
    JubeRootFrame frame = {};
    if (!node_fs_root_arguments(&frame, roots, values, 6)) return ItemNull;
    Item actual_position = node_fs_root_value(roots[4]);
    Item actual_callback = node_fs_root_value(roots[5]);
    if (node_fs_host->value->kind(node_fs_root_value(roots[4])) == JUBE_VALUE_FUNCTION) {
        actual_position = node_fs_undefined();
        actual_callback = node_fs_root_value(roots[4]);
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    // Promisify-created callbacks are only stack arguments at this boundary;
    // retain every operand until the synchronous read and guest callback finish.
    Item result = node_fs_read_sync_export(node_fs_root_value(roots[0]), node_fs_root_value(roots[1]),
                                           node_fs_root_value(roots[2]), node_fs_root_value(roots[3]), actual_position);
    Item completed = ItemNull;
    if (item_is_error(result)) {
        completed = node_fs_callback_bytes_complete(actual_callback,
                                                    node_fs_host->script->error_lane_payload(result),
                                                    ItemNull, ItemNull, true);
    } else {
        completed = node_fs_callback_bytes_complete(actual_callback, ItemNull, result,
                                                    node_fs_root_value(roots[1]), false);
    }
    node_fs_host->node->roots->root_frame_end(&frame);
    return completed;
}

static Item node_fs_write_export(Item descriptor, Item data, Item offset, Item length, Item position,
                                 Item callback) {
    Item values[6] = {descriptor, data, offset, length, position, callback};
    uint64_t* roots[6] = {};
    JubeRootFrame frame = {};
    if (!node_fs_root_arguments(&frame, roots, values, 6)) return ItemNull;
    Item actual_position = node_fs_root_value(roots[4]);
    Item actual_callback = node_fs_root_value(roots[5]);
    if (node_fs_host->value->kind(node_fs_root_value(roots[4])) == JUBE_VALUE_FUNCTION) {
        actual_position = node_fs_undefined();
        actual_callback = node_fs_root_value(roots[4]);
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item result = node_fs_write_sync_export(node_fs_root_value(roots[0]), node_fs_root_value(roots[1]),
                                            node_fs_root_value(roots[2]), node_fs_root_value(roots[3]), actual_position);
    Item completed = ItemNull;
    if (item_is_error(result)) {
        completed = node_fs_callback_bytes_complete(actual_callback,
                                                    node_fs_host->script->error_lane_payload(result),
                                                    ItemNull, ItemNull, true);
    } else {
        completed = node_fs_callback_bytes_complete(actual_callback, ItemNull, result,
                                                    node_fs_root_value(roots[1]), false);
    }
    node_fs_host->node->roots->root_frame_end(&frame);
    return completed;
}

static Item node_fs_readv_export(Item descriptor, Item buffers, Item position, Item callback) {
    Item values[4] = {descriptor, buffers, position, callback};
    uint64_t* roots[4] = {};
    JubeRootFrame frame = {};
    if (!node_fs_root_arguments(&frame, roots, values, 4)) return ItemNull;
    Item actual_position = node_fs_root_value(roots[2]);
    Item actual_callback = node_fs_root_value(roots[3]);
    if (node_fs_host->value->kind(node_fs_root_value(roots[2])) == JUBE_VALUE_FUNCTION) {
        actual_position = node_fs_undefined();
        actual_callback = node_fs_root_value(roots[2]);
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item result = node_fs_readv_sync(node_fs_root_value(roots[0]), node_fs_root_value(roots[1]), actual_position);
    Item completed = ItemNull;
    if (item_is_error(result)) {
        completed = node_fs_callback_bytes_complete(actual_callback,
                                                    node_fs_host->script->error_lane_payload(result),
                                                    ItemNull, ItemNull, true);
    } else {
        completed = node_fs_callback_bytes_complete(actual_callback, ItemNull, result,
                                                    node_fs_root_value(roots[1]), false);
    }
    node_fs_host->node->roots->root_frame_end(&frame);
    return completed;
}

static Item node_fs_writev_export(Item descriptor, Item buffers, Item position, Item callback) {
    Item values[4] = {descriptor, buffers, position, callback};
    uint64_t* roots[4] = {};
    JubeRootFrame frame = {};
    if (!node_fs_root_arguments(&frame, roots, values, 4)) return ItemNull;
    Item actual_position = node_fs_root_value(roots[2]);
    Item actual_callback = node_fs_root_value(roots[3]);
    if (node_fs_host->value->kind(node_fs_root_value(roots[2])) == JUBE_VALUE_FUNCTION) {
        actual_position = node_fs_undefined();
        actual_callback = node_fs_root_value(roots[2]);
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item result = node_fs_writev_sync(node_fs_root_value(roots[0]), node_fs_root_value(roots[1]), actual_position);
    Item completed = ItemNull;
    if (item_is_error(result)) {
        completed = node_fs_callback_bytes_complete(actual_callback,
                                                    node_fs_host->script->error_lane_payload(result),
                                                    ItemNull, ItemNull, true);
    } else {
        completed = node_fs_callback_bytes_complete(actual_callback, ItemNull, result,
                                                    node_fs_root_value(roots[1]), false);
    }
    node_fs_host->node->roots->root_frame_end(&frame);
    return completed;
}

static Item node_fs_void_callback_result(Item callback, Item result) {
    if (item_is_error(result)) {
        return node_fs_callback_error_only(callback,
                                           node_fs_host->script->error_lane_payload(result));
    }
    return node_fs_callback_error_only(callback, ItemNull);
}

static Item node_fs_access_export(Item path, Item mode_or_callback, Item callback) {
    Item mode = mode_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(mode_or_callback) == JUBE_VALUE_FUNCTION) {
        mode = node_fs_undefined();
        actual_callback = mode_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(actual_callback, node_fs_access_sync(path, mode));
}

static Item node_fs_chmod_export(Item path, Item mode, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(callback, node_fs_chmod_sync(path, mode));
}

static Item node_fs_fchmod_export(Item descriptor, Item mode, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(callback, node_fs_fchmod_sync(descriptor, mode));
}

static Item node_fs_copy_file_export(Item source, Item destination, Item flags_or_callback, Item callback) {
    Item actual_callback = callback;
    if (node_fs_host->value->kind(flags_or_callback) == JUBE_VALUE_FUNCTION) {
        actual_callback = flags_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(actual_callback,
                                        node_fs_copy_file_sync(source, destination));
}

static Item node_fs_truncate_export(Item path, Item length_or_callback, Item callback) {
    Item length = length_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(length_or_callback) == JUBE_VALUE_FUNCTION) {
        length = node_fs_host->script->make_number(0.0);
        actual_callback = length_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(actual_callback, node_fs_truncate_sync(path, length));
}

static Item node_fs_rm_export(Item path, Item options_or_callback, Item callback) {
    Item options = options_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION) {
        options = node_fs_undefined();
        actual_callback = options_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(actual_callback, node_fs_rm_sync(path, options));
}

static Item node_fs_realpath_export(Item path, Item options_or_callback, Item callback) {
    Item options = options_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION) {
        options = node_fs_undefined();
        actual_callback = options_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item result = node_fs_realpath_sync(path, options);
    if (item_is_error(result)) {
        return node_fs_stat_callback_complete(actual_callback,
                                              node_fs_host->script->error_lane_payload(result), true);
    }
    return node_fs_stat_callback_complete(actual_callback, result, false);
}

static Item node_fs_mkdtemp_export(Item prefix, Item options_or_callback, Item callback) {
    Item options = options_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION) {
        options = node_fs_undefined();
        actual_callback = options_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item result = node_fs_mkdtemp_sync(prefix, options);
    if (item_is_error(result)) {
        return node_fs_stat_callback_complete(actual_callback,
                                              node_fs_host->script->error_lane_payload(result), true);
    }
    return node_fs_stat_callback_complete(actual_callback, result, false);
}

static Item node_fs_link_export(Item existing_path, Item new_path, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(callback, node_fs_link_sync(existing_path, new_path));
}

static Item node_fs_symlink_export(Item target, Item path, Item type_or_callback, Item callback) {
    Item actual_callback = callback;
    if (node_fs_host->value->kind(type_or_callback) == JUBE_VALUE_FUNCTION) {
        actual_callback = type_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(actual_callback, node_fs_symlink_sync(target, path));
}

static Item node_fs_mkdir_export(Item path, Item options_or_callback, Item callback) {
    Item options = options_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION) {
        options = node_fs_undefined();
        actual_callback = options_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(actual_callback, node_fs_mkdir_sync(path, options));
}

static Item node_fs_unlink_export(Item path, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(callback, node_fs_unlink_sync(path));
}

static Item node_fs_rmdir_export(Item path, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(callback, node_fs_rmdir_sync(path));
}

static Item node_fs_rename_export(Item old_path, Item new_path, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    return node_fs_void_callback_result(callback, node_fs_rename_sync(old_path, new_path));
}

static Item node_fs_readdir_export(Item path, Item options_or_callback, Item callback) {
    Item options = options_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION) {
        options = node_fs_undefined();
        actual_callback = options_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item entries = node_fs_readdir_sync(path, options);
    if (item_is_error(entries)) {
        return node_fs_stat_callback_complete(actual_callback,
                                              node_fs_host->script->error_lane_payload(entries), true);
    }
    return node_fs_stat_callback_complete(actual_callback, entries, false);
}

static Item node_fs_close_sync_export(Item descriptor_item) {
    if (!node_fs_host || !node_fs_host->value ||
            node_fs_host->value->kind(descriptor_item) != JUBE_VALUE_NUMBER) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "fd must be a number");
    }
    int descriptor = (int)node_fs_host->script->get_number(descriptor_item);
    JubeNodeFilesystemDescriptorOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_DESCRIPTOR_CLOSE;
    operation.descriptor = descriptor;
    if (!node_fs_descriptor_operation(&operation)) {
        return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "close",
                                  operation.error_number ? operation.error_number : EIO);
    }
    return node_fs_undefined();
}

static const JubeMemberBind node_fs_filehandle_members[] = {
    {"fd", NULL, NULL, NULL, node_fs_filehandle_fd_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"close", NULL, NULL, NULL, NULL, NULL, node_fs_filehandle_close_call, NULL, JUBE_MEMBER_NONE},
    {"read", NULL, NULL, NULL, NULL, NULL, node_fs_filehandle_read_call, NULL, JUBE_MEMBER_NONE},
    {"read_file", "readFile", NULL, NULL, NULL, NULL, node_fs_filehandle_read_file_call, NULL,
     JUBE_MEMBER_NONE},
};

static const JubeMemberBind node_fs_stats_members[] = {
    {"mode", NULL, NULL, NULL, node_fs_stats_mode_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"size", NULL, NULL, NULL, node_fs_stats_size_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"uid", NULL, NULL, NULL, node_fs_stats_uid_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"gid", NULL, NULL, NULL, node_fs_stats_gid_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"nlink", NULL, NULL, NULL, node_fs_stats_nlink_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"ino", NULL, NULL, NULL, node_fs_stats_ino_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"dev", NULL, NULL, NULL, node_fs_stats_dev_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"rdev", NULL, NULL, NULL, node_fs_stats_rdev_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"blksize", NULL, NULL, NULL, node_fs_stats_blksize_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"blocks", NULL, NULL, NULL, node_fs_stats_blocks_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"mtime_ms", "mtimeMs", NULL, NULL, node_fs_stats_mtime_ms_get, NULL, NULL, NULL,
     JUBE_MEMBER_NONE},
    {"atime_ms", "atimeMs", NULL, NULL, node_fs_stats_atime_ms_get, NULL, NULL, NULL,
     JUBE_MEMBER_NONE},
    {"ctime_ms", "ctimeMs", NULL, NULL, node_fs_stats_ctime_ms_get, NULL, NULL, NULL,
     JUBE_MEMBER_NONE},
    {"birthtime_ms", "birthtimeMs", NULL, NULL, node_fs_stats_birthtime_ms_get, NULL, NULL, NULL,
     JUBE_MEMBER_NONE},
    {"atime", NULL, NULL, NULL, node_fs_stats_atime_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"mtime", NULL, NULL, NULL, node_fs_stats_mtime_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"ctime", NULL, NULL, NULL, node_fs_stats_ctime_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"birthtime", NULL, NULL, NULL, node_fs_stats_birthtime_get, NULL, NULL, NULL, JUBE_MEMBER_NONE},
    {"is_file", "isFile", NULL, NULL, NULL, NULL, node_fs_stats_is_file, NULL, JUBE_MEMBER_NONE},
    {"is_directory", "isDirectory", NULL, NULL, NULL, NULL, node_fs_stats_is_directory, NULL,
     JUBE_MEMBER_NONE},
    {"is_symbolic_link", "isSymbolicLink", NULL, NULL, NULL, NULL, node_fs_stats_is_symbolic_link, NULL,
     JUBE_MEMBER_NONE},
    {"is_block_device", "isBlockDevice", NULL, NULL, NULL, NULL, node_fs_stats_is_block_device, NULL,
     JUBE_MEMBER_NONE},
    {"is_character_device", "isCharacterDevice", NULL, NULL, NULL, NULL, node_fs_stats_is_character_device, NULL,
     JUBE_MEMBER_NONE},
    {"is_fifo", "isFIFO", NULL, NULL, NULL, NULL, node_fs_stats_is_fifo, NULL, JUBE_MEMBER_NONE},
    {"is_socket", "isSocket", NULL, NULL, NULL, NULL, node_fs_stats_is_socket, NULL, JUBE_MEMBER_NONE},
};

static const char node_fs_interface[] =
    "type file_handle {\n"
    "    fd: int,\n"
    "    close: fn() any,\n"
    "    read: fn(buffer: any, offset: any, length: any, position: any) any,\n"
    "    read_file: fn(options: any) any\n"
    "}\n"
    "type stats {\n"
    "    mode: any,\n"
    "    size: any,\n"
    "    uid: any,\n"
    "    gid: any,\n"
    "    nlink: any,\n"
    "    ino: any,\n"
    "    dev: any,\n"
    "    rdev: any,\n"
    "    blksize: any,\n"
    "    blocks: any,\n"
    "    mtime_ms: any,\n"
    "    atime_ms: any,\n"
    "    ctime_ms: any,\n"
    "    birthtime_ms: any,\n"
    "    atime: any,\n"
    "    mtime: any,\n"
    "    ctime: any,\n"
    "    birthtime: any,\n"
    "    is_file: fn() bool,\n"
    "    is_directory: fn() bool,\n"
    "    is_symbolic_link: fn() bool,\n"
    "    is_block_device: fn() bool,\n"
    "    is_character_device: fn() bool,\n"
    "    is_fifo: fn() bool,\n"
    "    is_socket: fn() bool\n"
    "}\n";

static const JubeTypeBinding node_fs_type_bindings[] = {
    {"file_handle", &node_fs_types[0], node_fs_filehandle_members,
     (int32_t)(sizeof(node_fs_filehandle_members) / sizeof(node_fs_filehandle_members[0])),
     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    {"stats", &node_fs_types[1], node_fs_stats_members,
     (int32_t)(sizeof(node_fs_stats_members) / sizeof(node_fs_stats_members[0])),
     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
};

static int node_fs_open_flags(Item flags_item, int* out_flags, bool* out_read, bool* out_write) {
    if (!out_flags || !out_read || !out_write || !node_fs_host || !node_fs_host->value) return -1;
    int flags = O_RDONLY | O_BINARY;
    int kind = node_fs_host->value->kind(flags_item);
    if (kind == JUBE_VALUE_STRING) {
        const uint8_t* text = node_fs_host->value->string_bytes(flags_item);
        size_t length = node_fs_host->value->string_length(flags_item);
        if (!text) return -1;
        if (length == 2 && memcmp(text, "r+", 2) == 0) flags = O_RDWR | O_BINARY;
        else if (length == 1 && text[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC | O_BINARY;
        else if (length == 2 && memcmp(text, "w+", 2) == 0) flags = O_RDWR | O_CREAT | O_TRUNC | O_BINARY;
        else if (length == 1 && text[0] == 'a') flags = O_WRONLY | O_CREAT | O_APPEND | O_BINARY;
        else if (length == 2 && memcmp(text, "a+", 2) == 0) flags = O_RDWR | O_CREAT | O_APPEND | O_BINARY;
        else if (length == 2 && memcmp(text, "ax", 2) == 0) flags = O_WRONLY | O_CREAT | O_APPEND | O_EXCL | O_BINARY;
        else if (length == 2 && memcmp(text, "wx", 2) == 0) flags = O_WRONLY | O_CREAT | O_TRUNC | O_EXCL | O_BINARY;
        else if (!(length == 1 && text[0] == 'r')) return -1;
    } else if (kind == JUBE_VALUE_NUMBER) {
        flags = (int)node_fs_host->script->get_number(flags_item);
    }
    *out_flags = flags;
    *out_write = (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0;
    *out_read = !*out_write || (flags & O_RDWR) != 0;
    return 0;
}

static Item node_fs_promises_open(Item path_item, Item flags_item, Item mode_item) {
    int flags = 0;
    bool needs_read = false;
    bool needs_write = false;
    if (node_fs_open_flags(flags_item, &flags, &needs_read, &needs_write) != 0) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_VALUE", "invalid open flags");
    }
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, needs_write)) return ItemNull;
    if (needs_read && !node_fs_host->node->permission->has_fs_read(path)) {
        Item permission_error = node_fs_host->node->permission->check_fs_read(path);
        if (item_is_error(permission_error)) {
            (void)node_fs_host->script->error_lane_payload(permission_error);
        }
        free(path);
        return ItemNull;
    }
    int mode = node_fs_host->value->kind(mode_item) == JUBE_VALUE_NUMBER ?
        (int)node_fs_host->script->get_number(mode_item) : 0666;
    JubeNodeFilesystemDescriptorOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_DESCRIPTOR_OPEN;
    operation.path = path;
    operation.flags = flags;
    operation.mode_value = mode;
    bool opened = node_fs_descriptor_operation(&operation);
    free(path);
    if (!opened) {
        Item error = node_fs_host->node->error->throw_system_error(
            node_fs_session, operation.error_syscall ? operation.error_syscall : "open",
            operation.error_number ? operation.error_number : EIO);
        return node_fs_promise_settled(error, true);
    }
    int descriptor = operation.descriptor;
    NodeFsFileHandle* native = (NodeFsFileHandle*)calloc(1, sizeof(NodeFsFileHandle));
    if (!native) {
        JubeNodeFilesystemDescriptorOperation close_operation = {};
        close_operation.mode = JUBE_NODE_FILESYSTEM_DESCRIPTOR_CLOSE;
        close_operation.descriptor = descriptor;
        node_fs_descriptor_operation(&close_operation);
        Item error = node_fs_host->node->error->throw_system_error(node_fs_session, "open", ENOMEM);
        return node_fs_promise_settled(error, true);
    }
    native->descriptor = descriptor;
    Item handle = node_fs_host->value->native_object_new(&node_fs_types[0], native);
    if (handle.item == 0) {
        node_fs_filehandle_destroy(native);
        return ItemNull;
    }
    return node_fs_promise_settled(handle, false);
}

static Item node_fs_open_sync_export(Item path_item, Item flags_item, Item mode_item) {
    int flags = 0;
    bool needs_read = false;
    bool needs_write = false;
    if (node_fs_open_flags(flags_item, &flags, &needs_read, &needs_write) != 0) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_VALUE", "invalid open flags");
    }
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, needs_write)) return ItemNull;
    if (needs_read && !node_fs_host->node->permission->has_fs_read(path)) {
        Item permission_error = node_fs_host->node->permission->check_fs_read(path);
        if (item_is_error(permission_error)) {
            (void)node_fs_host->script->error_lane_payload(permission_error);
        }
        free(path);
        return ItemNull;
    }
    int mode = node_fs_host->value->kind(mode_item) == JUBE_VALUE_NUMBER ?
        (int)node_fs_host->script->get_number(mode_item) : 0666;
    JubeNodeFilesystemDescriptorOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_DESCRIPTOR_OPEN;
    operation.path = path;
    operation.flags = flags;
    operation.mode_value = mode;
    bool opened = node_fs_descriptor_operation(&operation);
    free(path);
    if (!opened) return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "open",
                                           operation.error_number ? operation.error_number : EIO);
    return (Item){.item = i2it((int64_t)operation.descriptor)};
}

static bool node_fs_number_arg(Item value, int64_t fallback, int64_t* out_value) {
    if (!out_value || !node_fs_host || !node_fs_host->value || !node_fs_host->script) return false;
    if (node_fs_host->value->kind(value) == JUBE_VALUE_UNDEFINED ||
            node_fs_host->value->kind(value) == JUBE_VALUE_NULL) {
        *out_value = fallback;
        return true;
    }
    if (node_fs_host->value->kind(value) != JUBE_VALUE_NUMBER) return false;
    *out_value = (int64_t)node_fs_host->script->get_number(value);
    return true;
}

static Item node_fs_fchmod_sync(Item descriptor_item, Item mode_item) {
    int64_t descriptor = 0;
    int mode = 0;
    if (!node_fs_number_arg(descriptor_item, 0, &descriptor) || descriptor < 0) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "fd must be a number");
    }
    if (!node_fs_file_mode(mode_item, &mode)) return ItemNull;
    JubeNodeFilesystemDescriptorOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_DESCRIPTOR_FCHMOD;
    operation.descriptor = (int)descriptor;
    operation.mode_value = mode;
    if (!node_fs_descriptor_operation(&operation)) {
        return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "fchmod",
                                  operation.error_number ? operation.error_number : EIO);
    }
    return node_fs_undefined();
}

static bool node_fs_position_arg(Item value, int64_t minimum,
                                 int64_t* out_position) {
    if (!out_position || !node_fs_host || !node_fs_host->value ||
            !node_fs_host->value->number_to_int64_exact ||
            !node_fs_host->script || !node_fs_host->node ||
            !node_fs_host->node->error) return false;
    int kind = node_fs_host->value->kind(value);
    if (kind == JUBE_VALUE_UNDEFINED || kind == JUBE_VALUE_NULL) {
        *out_position = -1;
        return true;
    }

    bool exact = false;
    if (kind == JUBE_VALUE_NUMBER) {
        exact = node_fs_host->value->number_to_int64_exact(value, out_position);
    } else if (kind == JUBE_VALUE_BIGINT &&
            node_fs_host->script->bigint_to_int64_exact) {
        exact = node_fs_host->script->bigint_to_int64_exact(value, out_position);
    } else {
        node_fs_host->script->throw_type_error_code(
            "ERR_INVALID_ARG_TYPE", "position must be an integer or bigint");
        return false;
    }
    // BigInt overflow was previously reported as a type error because the
    // module accepted only the host's Number category. Preserve Node's -1
    // current-position sentinel while range-checking before native narrowing.
    if (!exact || *out_position < minimum) {
        node_fs_host->node->error->throw_range_error_code(node_fs_session,
            "ERR_OUT_OF_RANGE", "position is outside the supported range");
        return false;
    }
    return true;
}

static NodeFsVector* node_fs_vectors_from_array(Item buffers_item, int64_t* out_count,
                                                Item* out_error) {
    if (out_error) *out_error = ItemNull;
    if (!out_count || !node_fs_host || !node_fs_host->value || !node_fs_host->script || !node_fs_host->node ||
            !node_fs_host->node->binary || !node_fs_host->value->is_array ||
            !node_fs_host->value->array_length || !node_fs_host->value->array_get ||
            !node_fs_host->node->binary->is_typed_array || !node_fs_host->node->binary->typed_array_data ||
            !node_fs_host->node->binary->typed_array_length) {
        return NULL;
    }
    if (!node_fs_host->value->is_array(buffers_item)) {
        if (out_error) *out_error = node_fs_host->script->throw_type_error_code(
            "ERR_INVALID_ARG_TYPE", "buffers must be an array of typed arrays");
        return NULL;
    }
    int64_t count = node_fs_host->value->array_length(buffers_item);
    if (count < 0 || count > 1024) {
        if (out_error) *out_error = node_fs_host->script->throw_type_error_code(
            "ERR_OUT_OF_RANGE", "buffers length must not exceed 1024");
        return NULL;
    }
    NodeFsVector* vectors = count > 0 ? (NodeFsVector*)calloc((size_t)count, sizeof(NodeFsVector)) : NULL;
    if (count > 0 && !vectors) {
        if (out_error) *out_error = node_fs_host->node->error->throw_system_error(
            node_fs_session, "readv", ENOMEM);
        return NULL;
    }
    for (int64_t index = 0; index < count; ++index) {
        Item buffer = node_fs_host->value->array_get(buffers_item, index);
        if (!node_fs_host->node->binary->is_typed_array(buffer)) {
            free(vectors);
            if (out_error) *out_error = node_fs_host->script->throw_type_error_code(
                "ERR_INVALID_ARG_TYPE", "buffers must be an array of typed arrays");
            return NULL;
        }
        int length = node_fs_host->node->binary->typed_array_length(buffer);
        uint8_t* data = node_fs_host->node->binary->typed_array_data(buffer);
        if (length < 0 || (!data && length > 0)) {
            free(vectors);
            if (out_error) *out_error = node_fs_host->script->throw_type_error_code(
                "ERR_INVALID_ARG_VALUE", "buffer is detached");
            return NULL;
        }
        vectors[index].data = data;
        vectors[index].length = (size_t)length;
    }
    *out_count = count;
    return vectors;
}

static Item node_fs_vector_io(Item descriptor_item, Item buffers_item, Item position_item, bool is_write) {
    int64_t descriptor = 0;
    int64_t position = -1;
    if (!node_fs_number_arg(descriptor_item, 0, &descriptor) || descriptor < 0) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "fd must be a number");
    }
    if (!node_fs_position_arg(position_item, 0, &position)) return ItemNull;
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 1)) return ItemNull;
    uint64_t* buffers_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!buffers_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *buffers_root = buffers_item.item;
    int64_t vector_count = 0;
    Item vector_error = ItemNull;
    NodeFsVector* vectors = node_fs_vectors_from_array(node_fs_root_value(buffers_root), &vector_count,
                                                       &vector_error);
    if (!vectors && vector_count == 0 && item_is_error(vector_error)) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    int64_t total = 0;
    int error_number = 0;
    for (int64_t index = 0; index < vector_count && error_number == 0; ++index) {
        size_t offset = 0;
        while (offset < vectors[index].length) {
            size_t remaining = vectors[index].length - offset;
            unsigned int chunk = remaining > (size_t)UINT_MAX ? UINT_MAX : (unsigned int)remaining;
            int count = node_fs_descriptor_io((int)descriptor, vectors[index].data + offset, chunk,
                                              position >= 0 ? position + total : -1, is_write,
                                              &error_number);
            if (count < 0) {
                break;
            }
            if (count == 0) break;
            offset += (size_t)count;
            total += count;
            if (!is_write && (unsigned int)count < chunk) break;
        }
        if (!is_write && offset < vectors[index].length) break;
    }
    free(vectors);
    node_fs_host->node->roots->root_frame_end(&frame);
    if (error_number != 0) return node_fs_sync_error(is_write ? "writev" : "readv", error_number);
    return node_fs_host->script->make_number((double)total);
}

static Item node_fs_readv_sync(Item descriptor_item, Item buffers_item, Item position_item) {
    return node_fs_vector_io(descriptor_item, buffers_item, position_item, false);
}

static Item node_fs_writev_sync(Item descriptor_item, Item buffers_item, Item position_item) {
    return node_fs_vector_io(descriptor_item, buffers_item, position_item, true);
}

static Item node_fs_read_sync_export(Item descriptor_item, Item buffer_item, Item offset_item,
                                     Item length_item, Item position_item) {
    int64_t descriptor = 0, offset = 0, length = 0, position = -1;
    if (!node_fs_number_arg(descriptor_item, 0, &descriptor) ||
            !node_fs_number_arg(offset_item, 0, &offset) ||
            !node_fs_number_arg(length_item, -1, &length) ||
            descriptor < 0 || offset < 0) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "invalid readSync arguments");
    }
    if (!node_fs_position_arg(position_item, -1, &position)) return ItemNull;
    if (!node_fs_host->node || !node_fs_host->node->binary ||
            !node_fs_host->node->binary->is_typed_array(buffer_item) ||
            !node_fs_host->node->binary->typed_array_data) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "buffer must be a typed array");
    }
    int byte_length = node_fs_host->node->binary->typed_array_length(buffer_item);
    if (length < 0) length = byte_length - offset;
    if (offset > byte_length || length < 0 || length > byte_length - offset) {
        return node_fs_host->node->error->throw_range_error_code(node_fs_session,
                                                                  "ERR_OUT_OF_RANGE", "offset and length exceed buffer");
    }
    uint8_t* data = node_fs_host->node->binary->typed_array_data(buffer_item);
    if (!data && length > 0) return node_fs_sync_error("read", EINVAL);
    int error_number = 0;
    int count = node_fs_descriptor_io((int)descriptor, data + offset, (size_t)length, position,
                                      false, &error_number);
    if (count < 0) return node_fs_sync_error("read", error_number);
    return node_fs_host->script->make_number((double)count);
}

static Item node_fs_write_sync_export(Item descriptor_item, Item data_item, Item offset_item,
                                      Item length_item, Item position_item) {
    int64_t descriptor = 0, offset = 0, length = -1, position = -1;
    if (!node_fs_number_arg(descriptor_item, 0, &descriptor) ||
            !node_fs_number_arg(offset_item, 0, &offset) ||
            !node_fs_number_arg(length_item, -1, &length) ||
            descriptor < 0 || offset < 0) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "invalid writeSync arguments");
    }
    if (!node_fs_position_arg(position_item, -1, &position)) return ItemNull;
    const uint8_t* bytes = NULL;
    size_t byte_count = 0;
    char* copied = NULL;
    if (node_fs_host->value->kind(data_item) == JUBE_VALUE_STRING) {
        if (!node_fs_copy_string(data_item, &copied, &byte_count)) return ItemNull;
        bytes = (const uint8_t*)copied;
    } else if (node_fs_host->node && node_fs_host->node->binary &&
               node_fs_host->node->binary->is_typed_array(data_item)) {
        bytes = node_fs_host->node->binary->typed_array_data(data_item);
        byte_count = (size_t)node_fs_host->node->binary->typed_array_length(data_item);
    } else {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "data must be a string or typed array");
    }
    if (length < 0) length = (int64_t)byte_count - offset;
    if ((size_t)offset > byte_count || length < 0 || (size_t)length > byte_count - (size_t)offset) {
        free(copied);
        return node_fs_host->node->error->throw_range_error_code(node_fs_session,
                                                                  "ERR_OUT_OF_RANGE", "offset and length exceed data");
    }
    int error_number = 0;
    int count = node_fs_descriptor_io((int)descriptor, (uint8_t*)bytes + offset, (size_t)length,
                                      position, true, &error_number);
    free(copied);
    if (count < 0) return node_fs_sync_error("write", error_number);
    return node_fs_host->script->make_number((double)count);
}

static void node_fs_set_method(Item namespace_item, const char* name, void* function,
                               int parameter_count) {
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 3)) return;
    uint64_t* namespace_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* function_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !key_root || !function_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return;
    }
    *namespace_root = namespace_item.item;
    Item key = node_fs_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    Item method = node_fs_host->script->new_function(function, parameter_count);
    *function_root = method.item;
    // Key and method construction may compact before the host namespace owns
    // them, so publish only from the temporary root frame.
    node_fs_host->value->property_set(node_fs_root_value(namespace_root),
                                      node_fs_root_value(key_root),
                                      node_fs_root_value(function_root));
    node_fs_host->node->roots->root_frame_end(&frame);
}

static Item node_fs_watch_options_valid(Item options) {
    Item encoding_result = node_fs_readdir_options_valid(options);
    if (item_is_error(encoding_result)) return encoding_result;
    if (node_fs_host->value->kind(options) != JUBE_VALUE_OBJECT) return node_fs_undefined();
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 3)) return ItemNull;
    uint64_t* options_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* ignore_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!options_root || !key_root || !ignore_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *options_root = options.item;
    Item key = node_fs_host->value->string_from_utf8_n("ignore", 6);
    *key_root = key.item;
    Item ignore = node_fs_host->value->property_get(node_fs_root_value(options_root),
                                                    node_fs_root_value(key_root));
    *ignore_root = ignore.item;
    int kind = node_fs_host->value->kind(node_fs_root_value(ignore_root));
    Item valid = node_fs_undefined();
    if (kind == JUBE_VALUE_STRING) {
        if (node_fs_host->value->string_length(node_fs_root_value(ignore_root)) == 0) {
            valid = node_fs_host->script->throw_type_error_code(
                "ERR_INVALID_ARG_VALUE", "The property 'options.ignore' is invalid. Received ''");
        }
    } else if (kind != JUBE_VALUE_UNDEFINED && kind != JUBE_VALUE_NULL) {
        if (!node_fs_host->value->is_array(node_fs_root_value(ignore_root))) {
            valid = node_fs_host->script->throw_type_error_code(
                "ERR_INVALID_ARG_TYPE", "options.ignore must be a string or an array of strings");
        } else {
            int64_t length = node_fs_host->value->array_length(node_fs_root_value(ignore_root));
            for (int64_t index = 0; !item_is_error(valid) && index < length; ++index) {
                Item entry = node_fs_host->value->array_get(node_fs_root_value(ignore_root), index);
                if (node_fs_host->value->kind(entry) != JUBE_VALUE_STRING) {
                    valid = node_fs_host->script->throw_type_error_code(
                        "ERR_INVALID_ARG_TYPE", "options.ignore must be a string or an array of strings");
                } else if (node_fs_host->value->string_length(entry) == 0) {
                    valid = node_fs_host->script->throw_type_error_code(
                        "ERR_INVALID_ARG_VALUE", "The property 'options.ignore' is invalid. Received ''");
                }
            }
        }
    }
    node_fs_host->node->roots->root_frame_end(&frame);
    return valid;
}

static Item node_fs_watcher_close(void) {
    return node_fs_undefined();
}

static Item node_fs_watcher_ref(void) {
    return node_fs_host->node->runtime->current_this(node_fs_session);
}

static Item node_fs_watcher_unref(void) {
    return node_fs_host->node->runtime->current_this(node_fs_session);
}

static Item node_fs_make_watcher(void) {
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 1)) return ItemNull;
    uint64_t* watcher_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!watcher_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item watcher = node_fs_host->script->object_create(ItemNull);
    *watcher_root = watcher.item;
    // Method construction can compact before the watcher receives a property,
    // so retain this otherwise-unpublished watcher in the module root frame.
    node_fs_set_method(node_fs_root_value(watcher_root), "close", (void*)node_fs_watcher_close, 0);
    node_fs_set_method(node_fs_root_value(watcher_root), "ref", (void*)node_fs_watcher_ref, 0);
    node_fs_set_method(node_fs_root_value(watcher_root), "unref", (void*)node_fs_watcher_unref, 0);
    Item result = node_fs_root_value(watcher_root);
    node_fs_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_fs_watch(Item path_item, Item options_or_listener, Item listener_item) {
    (void)listener_item;
    if (node_fs_host->value->kind(options_or_listener) != JUBE_VALUE_FUNCTION) {
        Item validation = node_fs_watch_options_valid(options_or_listener);
        if (item_is_error(validation)) return validation;
    }
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, false)) return ItemNull;
    free(path);
    return node_fs_make_watcher();
}

static Item node_fs_watch_file(Item path_item, Item options_or_listener, Item listener_item) {
    Item listener = node_fs_host->value->kind(options_or_listener) == JUBE_VALUE_FUNCTION ?
                    options_or_listener : listener_item;
    if (node_fs_host->value->kind(listener) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE",
                                                            "listener must be a function");
    }
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, false)) return ItemNull;
    free(path);
    return node_fs_make_watcher();
}

static Item node_fs_unwatch_file(Item path_item, Item listener_item) {
    (void)listener_item;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, false, false)) return ItemNull;
    free(path);
    return node_fs_undefined();
}

static Item node_fs_utimes_sync(Item path_item, Item atime_item, Item mtime_item) {
    (void)atime_item;
    (void)mtime_item;
    char* path = NULL;
    // The legacy compatibility implementation validates this path but has no
    // timestamp mutation yet, so retain that observable stub contract.
    if (!node_fs_copy_path(path_item, &path, false, false)) return ItemNull;
    free(path);
    return node_fs_undefined();
}

static Item node_fs_utimes_export(Item path_item, Item atime_item, Item mtime_item, Item callback) {
    Item result = node_fs_utimes_sync(path_item, atime_item, mtime_item);
    if (item_is_error(result)) return result;
    if (node_fs_host->value->kind(callback) == JUBE_VALUE_FUNCTION) {
        Item args[1] = {ItemNull};
        node_fs_host->script->call_function(callback, ItemNull, args, 1);
    }
    (void)result;
    return node_fs_undefined();
}

static Item node_fs_opendir_sync(Item path_item, Item options) {
    Item validation = node_fs_readdir_options_valid(options);
    if (item_is_error(validation)) return validation;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, false, false)) return ItemNull;
    free(path);
    return node_fs_host->script->object_create(ItemNull);
}

static Item node_fs_readlink_sync(Item path_item, Item options) {
    Item validation = node_fs_readdir_options_valid(options);
    if (item_is_error(validation)) return validation;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, false)) return ItemNull;
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    JubeNodeFilesystemStringOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_STRING_READLINK;
    operation.path = path;
    bool success = filesystem && filesystem->string_operation &&
        filesystem->string_operation_release && filesystem->string_operation(&operation);
    free(path);
    if (!success) return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "readlink",
                                            operation.error_number ? operation.error_number : EIO);
    Item result = node_fs_host->value->string_from_utf8_n(operation.output, operation.output_length);
    filesystem->string_operation_release(&operation);
    return result;
}

static Item node_fs_readlink_export(Item path, Item options_or_callback, Item callback) {
    Item options = options_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION) {
        options = node_fs_undefined();
        actual_callback = options_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item result = node_fs_readlink_sync(path, options);
    if (item_is_error(result)) {
        return node_fs_stat_callback_complete(actual_callback,
                                              node_fs_host->script->error_lane_payload(result), true);
    }
    return node_fs_stat_callback_complete(actual_callback, result, false);
}

static bool node_fs_uid_gid(Item value, const char* name, int* out_value) {
    int64_t converted = 0;
    if (!out_value || node_fs_host->value->kind(value) != JUBE_VALUE_NUMBER ||
            !node_fs_host->value->number_to_int64_exact(value, &converted) ||
            converted < 0 || converted > INT_MAX) {
        node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_VALUE",
                                                    "uid and gid must be non-negative integers");
        return false;
    }
    (void)name;
    *out_value = (int)converted;
    return true;
}

static Item node_fs_chown_sync(Item path_item, Item uid_item, Item gid_item, bool link) {
    int uid = 0;
    int gid = 0;
    if (!node_fs_uid_gid(uid_item, "uid", &uid) || !node_fs_uid_gid(gid_item, "gid", &gid)) return ItemNull;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, true)) return ItemNull;
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = link ? JUBE_NODE_FILESYSTEM_PATH_LCHOWN : JUBE_NODE_FILESYSTEM_PATH_CHOWN;
    operation.path = path;
    operation.numeric_value = uid;
    operation.secondary_numeric_value = gid;
    Item result = node_fs_path_operation_error(&operation);
    free(path);
    return result;
}

static Item node_fs_chown_sync_export(Item path, Item uid, Item gid) {
    return node_fs_chown_sync(path, uid, gid, false);
}

static Item node_fs_lchown_sync(Item path, Item uid, Item gid) {
    return node_fs_chown_sync(path, uid, gid, true);
}

static Item node_fs_fchown_sync(Item descriptor_item, Item uid_item, Item gid_item) {
    int64_t descriptor = 0;
    int uid = 0;
    int gid = 0;
    if (node_fs_host->value->kind(descriptor_item) != JUBE_VALUE_NUMBER ||
            !node_fs_host->value->number_to_int64_exact(descriptor_item, &descriptor) ||
            descriptor < 0 || descriptor > INT_MAX || !node_fs_uid_gid(uid_item, "uid", &uid) ||
            !node_fs_uid_gid(gid_item, "gid", &gid)) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_VALUE",
                                                            "fd, uid, and gid must be non-negative integers");
    }
    JubeNodeFilesystemDescriptorOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_DESCRIPTOR_FCHOWN;
    operation.descriptor = (int)descriptor;
    operation.mode_value = uid;
    operation.secondary_mode_value = gid;
    if (!node_fs_descriptor_operation(&operation)) {
        return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "fchown",
                                  operation.error_number ? operation.error_number : EIO);
    }
    return node_fs_undefined();
}

static Item node_fs_lchmod_sync(Item path_item, Item mode_item) {
    int mode = 0;
    if (!node_fs_file_mode(mode_item, &mode)) return ItemNull;
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, true)) return ItemNull;
    JubeNodeFilesystemPathOperation operation = {};
    operation.mode = JUBE_NODE_FILESYSTEM_PATH_LCHMOD;
    operation.path = path;
    operation.numeric_value = mode;
    Item result = node_fs_path_operation_error(&operation);
    free(path);
    return result;
}

static Item node_fs_chown_export(Item path, Item uid, Item gid, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    node_fs_chown_sync_export(path, uid, gid);
    return node_fs_void_callback_result(callback, node_fs_chown_sync_export(path, uid, gid));
}

static Item node_fs_lchown_export(Item path, Item uid, Item gid, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    node_fs_lchown_sync(path, uid, gid);
    return node_fs_void_callback_result(callback, node_fs_lchown_sync(path, uid, gid));
}

static Item node_fs_fchown_export(Item descriptor, Item uid, Item gid, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    node_fs_fchown_sync(descriptor, uid, gid);
    return node_fs_void_callback_result(callback, node_fs_fchown_sync(descriptor, uid, gid));
}

static Item node_fs_lchmod_export(Item path, Item mode, Item callback) {
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    node_fs_lchmod_sync(path, mode);
    return node_fs_void_callback_result(callback, node_fs_lchmod_sync(path, mode));
}

static Item node_fs_promises_chown(Item path, Item uid, Item gid) {
    return node_fs_promise_from_sync_result(node_fs_chown_sync_export(path, uid, gid));
}

static Item node_fs_promises_lchown(Item path, Item uid, Item gid) {
    return node_fs_promise_from_sync_result(node_fs_lchown_sync(path, uid, gid));
}

static Item node_fs_exists(Item path_item, Item callback) {
    Item result = node_fs_exists_sync(path_item);
    if (node_fs_host->value->kind(callback) != JUBE_VALUE_FUNCTION) return node_fs_undefined();
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 2)) return ItemNull;
    uint64_t* callback_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* result_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!callback_root || !result_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *callback_root = callback.item;
    *result_root = result.item;
    Item args[1] = {node_fs_root_value(result_root)};
    node_fs_host->script->call_function(node_fs_root_value(callback_root), node_fs_undefined(), args, 1);
    node_fs_host->node->roots->root_frame_end(&frame);
    return node_fs_undefined();
}

static Item node_fs_exists_promisified(Item path_item) {
    return node_fs_promise_from_sync_result(node_fs_exists_sync(path_item));
}

static Item node_fs_to_unix_timestamp(Item value) {
    int kind = node_fs_host->value->kind(value);
    double number = 0.0;
    if (kind == JUBE_VALUE_NUMBER) {
        number = node_fs_host->script->get_number(value);
    } else if (kind == JUBE_VALUE_STRING) {
        size_t length = node_fs_host->value->string_length(value);
        const uint8_t* bytes = node_fs_host->value->string_bytes(value);
        char* text = (char*)malloc(length + 1);
        if (!text || (length > 0 && !bytes)) {
            free(text);
            return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "time must be a number");
        }
        if (length > 0) memcpy(text, bytes, length);
        text[length] = '\0';
        char* end = NULL;
        number = strtod(text, &end);
        bool valid = end && end != text && *end == '\0';
        free(text);
        if (!valid) return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "time must be a number");
    } else {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "time must be a number");
    }
    if (!isfinite(number)) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "time must be a number");
    }
    return node_fs_host->script->make_number(number);
}

static Item node_fs_create_stream(Item path, Item options, bool readable) {
    JubeRootFrame frame = {};
    Item values[2] = {path, options};
    uint64_t* roots[2] = {};
    if (!node_fs_root_arguments(&frame, roots, values, 2)) return ItemNull;
    Item result = readable ?
        node_fs_host->node->streams->file_read_stream_new(node_fs_root_value(roots[0]),
                                                           node_fs_root_value(roots[1])) :
        node_fs_host->node->streams->file_write_stream_new(node_fs_root_value(roots[0]),
                                                            node_fs_root_value(roots[1]));
    node_fs_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_fs_create_read_stream(Item path, Item options) {
    return node_fs_create_stream(path, options, true);
}

static Item node_fs_create_write_stream(Item path, Item options) {
    return node_fs_create_stream(path, options, false);
}

static void node_fs_install_custom_promisify(Item namespace_item, const char* name,
                                             void* function, int parameter_count) {
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 2)) return;
    uint64_t* namespace_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !key_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return;
    }
    *namespace_root = namespace_item.item;
    Item key = node_fs_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    Item exported = node_fs_host->value->property_get(node_fs_root_value(namespace_root),
                                                       node_fs_root_value(key_root));
    node_fs_host->node->async_ops->function_install_promisify_custom(exported, function, parameter_count);
    node_fs_host->node->roots->root_frame_end(&frame);
}

static void node_fs_install_custom_promisify_args(Item namespace_item, const char* name,
                                                  const char* first, const char* second) {
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 2)) return;
    uint64_t* namespace_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root || !key_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return;
    }
    *namespace_root = namespace_item.item;
    Item key = node_fs_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    Item exported = node_fs_host->value->property_get(node_fs_root_value(namespace_root),
                                                       node_fs_root_value(key_root));
    node_fs_host->node->async_ops->function_install_promisify_args(exported, first, second);
    node_fs_host->node->roots->root_frame_end(&frame);
}

static bool node_fs_set_property(Item object, const char* name, Item property_value) {
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 3)) return false;
    uint64_t* object_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* key_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    uint64_t* value_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!object_root || !key_root || !value_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return false;
    }
    *object_root = object.item;
    Item key = node_fs_host->value->string_from_utf8_n(name, strlen(name));
    *key_root = key.item;
    *value_root = property_value.item;
    node_fs_host->value->property_set(node_fs_root_value(object_root), node_fs_root_value(key_root),
                                      node_fs_root_value(value_root));
    node_fs_host->node->roots->root_frame_end(&frame);
    return true;
}

static bool node_fs_set_unsigned_property(Item object, const char* name, uint64_t value,
                                          bool bigint) {
    return node_fs_set_property(object, name, node_fs_stats_unsigned(value, bigint));
}

static void node_fs_install_constants(Item namespace_item) {
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 1)) return;
    uint64_t* constants_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!constants_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return;
    }
    Item constants = node_fs_host->script->object_create(ItemNull);
    *constants_root = constants.item;
#define NODE_FS_CONSTANT(name) \
    node_fs_set_unsigned_property(node_fs_root_value(constants_root), #name, (uint64_t)(name), false)
    bool complete = NODE_FS_CONSTANT(F_OK) && NODE_FS_CONSTANT(R_OK) && NODE_FS_CONSTANT(W_OK) &&
                    NODE_FS_CONSTANT(X_OK) && NODE_FS_CONSTANT(O_RDONLY) && NODE_FS_CONSTANT(O_WRONLY) &&
                    NODE_FS_CONSTANT(O_RDWR) && NODE_FS_CONSTANT(O_CREAT) && NODE_FS_CONSTANT(O_TRUNC) &&
                    NODE_FS_CONSTANT(O_APPEND) && NODE_FS_CONSTANT(O_EXCL) &&
                    NODE_FS_CONSTANT(S_IFMT) && NODE_FS_CONSTANT(S_IFREG) && NODE_FS_CONSTANT(S_IFDIR) &&
                    NODE_FS_CONSTANT(S_IFCHR) && NODE_FS_CONSTANT(S_IFBLK) && NODE_FS_CONSTANT(S_IFIFO) &&
                    NODE_FS_CONSTANT(S_IFLNK) && NODE_FS_CONSTANT(S_IFSOCK) &&
                    NODE_FS_CONSTANT(S_IRUSR) && NODE_FS_CONSTANT(S_IWUSR) && NODE_FS_CONSTANT(S_IXUSR) &&
                    NODE_FS_CONSTANT(S_IRGRP) && NODE_FS_CONSTANT(S_IWGRP) && NODE_FS_CONSTANT(S_IXGRP) &&
                    NODE_FS_CONSTANT(S_IROTH) && NODE_FS_CONSTANT(S_IWOTH) && NODE_FS_CONSTANT(S_IXOTH);
#undef NODE_FS_CONSTANT
    // libuv's public dirent and copy flags are stable Node constants.  Keep
    // their values module-local so the compatibility boundary exposes no uv API.
    complete = complete &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "UV_DIRENT_UNKNOWN", 0, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "UV_DIRENT_FILE", 1, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "UV_DIRENT_DIR", 2, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "UV_DIRENT_LINK", 3, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "UV_DIRENT_FIFO", 4, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "UV_DIRENT_SOCKET", 5, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "UV_DIRENT_CHAR", 6, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "UV_DIRENT_BLOCK", 7, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "UV_FS_SYMLINK_DIR", 1, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "UV_FS_SYMLINK_JUNCTION", 2, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "COPYFILE_EXCL", 1, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "COPYFILE_FICLONE", 2, false) &&
               node_fs_set_unsigned_property(node_fs_root_value(constants_root), "COPYFILE_FICLONE_FORCE", 4, false);
    if (complete) node_fs_set_property(namespace_item, "constants", node_fs_root_value(constants_root));
    node_fs_host->node->roots->root_frame_end(&frame);
}

static Item node_fs_statfs_sync(Item path_item, Item options) {
    char* path = NULL;
    if (!node_fs_copy_path(path_item, &path, false)) return ItemNull;
    const JubeHostFilesystemAPI* filesystem = node_fs_host && node_fs_host->node ?
        node_fs_host->node->filesystem : NULL;
    JubeNodeFilesystemStatfsOperation operation = {};
    operation.path = path;
    bool loaded = filesystem && filesystem->statfs_operation && filesystem->statfs_operation(&operation);
    free(path);
    if (!loaded) return node_fs_sync_error(operation.error_syscall ? operation.error_syscall : "statfs",
                                           operation.error_number ? operation.error_number : EIO);
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 1)) return ItemNull;
    uint64_t* result_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!result_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    Item result = node_fs_host->script->object_create(ItemNull);
    *result_root = result.item;
    bool bigint = node_fs_options_bigint(options);
    bool populated = node_fs_set_unsigned_property(node_fs_root_value(result_root), "type", operation.type, bigint) &&
                     node_fs_set_unsigned_property(node_fs_root_value(result_root), "bsize", operation.bsize, bigint) &&
                     node_fs_set_unsigned_property(node_fs_root_value(result_root), "frsize", operation.frsize, bigint) &&
                     node_fs_set_unsigned_property(node_fs_root_value(result_root), "blocks", operation.blocks, bigint) &&
                     node_fs_set_unsigned_property(node_fs_root_value(result_root), "bfree", operation.bfree, bigint) &&
                     node_fs_set_unsigned_property(node_fs_root_value(result_root), "bavail", operation.bavail, bigint) &&
                     node_fs_set_unsigned_property(node_fs_root_value(result_root), "files", operation.files, bigint) &&
                     node_fs_set_unsigned_property(node_fs_root_value(result_root), "ffree", operation.ffree, bigint);
    Item completed = populated ? node_fs_root_value(result_root) : ItemNull;
    node_fs_host->node->roots->root_frame_end(&frame);
    return completed;
}

static Item node_fs_statfs_export(Item path, Item options_or_callback, Item callback) {
    Item options = options_or_callback;
    Item actual_callback = callback;
    if (node_fs_host->value->kind(options_or_callback) == JUBE_VALUE_FUNCTION) {
        options = node_fs_undefined();
        actual_callback = options_or_callback;
    }
    if (node_fs_host->value->kind(actual_callback) != JUBE_VALUE_FUNCTION) {
        return node_fs_host->script->throw_type_error_code("ERR_INVALID_ARG_TYPE", "callback must be a function");
    }
    Item result = node_fs_statfs_sync(path, options);
    if (item_is_error(result)) {
        return node_fs_stat_callback_complete(actual_callback,
                                              node_fs_host->script->error_lane_payload(result), true);
    }
    return node_fs_stat_callback_complete(actual_callback, result, false);
}

static Item node_fs_namespace(void) {
    if (!node_fs_host || !node_fs_session || !node_fs_host->node || !node_fs_host->node->runtime ||
            !node_fs_host->node->runtime->resolve_host_namespace) return ItemNull;
    Item namespace_item = ItemNull;
    if (node_fs_host->node->runtime->resolve_host_namespace(node_fs_session, "fs",
                                                            &namespace_item) != 0) {
        return ItemNull;
    }
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 1)) return ItemNull;
    uint64_t* namespace_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *namespace_root = namespace_item.item;
    // Each method allocation can compact the heap; retain the borrowed host
    // namespace until every module-owned replacement property is published.
    node_fs_set_method(node_fs_root_value(namespace_root), "readFile", (void*)node_fs_read_file, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "writeFile", (void*)node_fs_write_file, 4);
    node_fs_set_method(node_fs_root_value(namespace_root), "appendFile", (void*)node_fs_append_file, 4);
    node_fs_set_method(node_fs_root_value(namespace_root), "readFileSync", (void*)node_fs_read_file_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "accessSync", (void*)node_fs_access_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "chmodSync", (void*)node_fs_chmod_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "fchmodSync", (void*)node_fs_fchmod_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "copyFileSync", (void*)node_fs_copy_file_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "truncateSync", (void*)node_fs_truncate_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "rmSync", (void*)node_fs_rm_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "realpathSync", (void*)node_fs_realpath_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "mkdtempSync", (void*)node_fs_mkdtemp_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "linkSync", (void*)node_fs_link_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "symlinkSync", (void*)node_fs_symlink_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "writeFileSync", (void*)node_fs_write_file_sync_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "appendFileSync", (void*)node_fs_append_file_sync_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "existsSync", (void*)node_fs_exists_sync, 1);
    node_fs_set_method(node_fs_root_value(namespace_root), "unlinkSync", (void*)node_fs_unlink_sync, 1);
    node_fs_set_method(node_fs_root_value(namespace_root), "mkdirSync", (void*)node_fs_mkdir_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "rmdirSync", (void*)node_fs_rmdir_sync, 1);
    node_fs_set_method(node_fs_root_value(namespace_root), "renameSync", (void*)node_fs_rename_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "readdirSync", (void*)node_fs_readdir_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "stat", (void*)node_fs_stat_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "statfs", (void*)node_fs_statfs_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "lstat", (void*)node_fs_lstat_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "fstat", (void*)node_fs_fstat_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "open", (void*)node_fs_open_export, 4);
    node_fs_set_method(node_fs_root_value(namespace_root), "close", (void*)node_fs_close_export, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "read", (void*)node_fs_read_export, 6);
    node_fs_set_method(node_fs_root_value(namespace_root), "write", (void*)node_fs_write_export, 6);
    node_fs_set_method(node_fs_root_value(namespace_root), "readv", (void*)node_fs_readv_export, 4);
    node_fs_set_method(node_fs_root_value(namespace_root), "writev", (void*)node_fs_writev_export, 4);
    node_fs_install_custom_promisify_args(node_fs_root_value(namespace_root), "read", "bytesRead", "buffer");
    node_fs_install_custom_promisify_args(node_fs_root_value(namespace_root), "write", "bytesWritten", "buffer");
    node_fs_install_custom_promisify_args(node_fs_root_value(namespace_root), "readv", "bytesRead", "buffers");
    node_fs_install_custom_promisify_args(node_fs_root_value(namespace_root), "writev", "bytesWritten", "buffer");
    node_fs_set_method(node_fs_root_value(namespace_root), "exists", (void*)node_fs_exists, 2);
    node_fs_install_custom_promisify(node_fs_root_value(namespace_root), "exists",
                                    (void*)node_fs_exists_promisified, 1);
    node_fs_set_method(node_fs_root_value(namespace_root), "mkdir", (void*)node_fs_mkdir_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "access", (void*)node_fs_access_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "chmod", (void*)node_fs_chmod_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "fchmod", (void*)node_fs_fchmod_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "copyFile", (void*)node_fs_copy_file_export, 4);
    node_fs_set_method(node_fs_root_value(namespace_root), "truncate", (void*)node_fs_truncate_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "rm", (void*)node_fs_rm_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "realpath", (void*)node_fs_realpath_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "mkdtemp", (void*)node_fs_mkdtemp_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "link", (void*)node_fs_link_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "symlink", (void*)node_fs_symlink_export, 4);
    node_fs_set_method(node_fs_root_value(namespace_root), "unlink", (void*)node_fs_unlink_export, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "rmdir", (void*)node_fs_rmdir_export, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "rename", (void*)node_fs_rename_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "readdir", (void*)node_fs_readdir_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "readlink", (void*)node_fs_readlink_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "chown", (void*)node_fs_chown_export, 4);
    node_fs_set_method(node_fs_root_value(namespace_root), "lchown", (void*)node_fs_lchown_export, 4);
    node_fs_set_method(node_fs_root_value(namespace_root), "fchown", (void*)node_fs_fchown_export, 4);
    node_fs_set_method(node_fs_root_value(namespace_root), "lchmod", (void*)node_fs_lchmod_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "watch", (void*)node_fs_watch, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "watchFile", (void*)node_fs_watch_file, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "unwatchFile", (void*)node_fs_unwatch_file, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "utimes", (void*)node_fs_utimes_export, 4);
    node_fs_set_method(node_fs_root_value(namespace_root), "statSync", (void*)node_fs_stat_sync_export, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "statfsSync", (void*)node_fs_statfs_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "lstatSync", (void*)node_fs_lstat_sync_export, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "fstatSync", (void*)node_fs_fstat_sync_export, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "closeSync", (void*)node_fs_close_sync_export, 1);
    node_fs_set_method(node_fs_root_value(namespace_root), "openSync", (void*)node_fs_open_sync_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "readSync", (void*)node_fs_read_sync_export, 5);
    node_fs_set_method(node_fs_root_value(namespace_root), "writeSync", (void*)node_fs_write_sync_export, 5);
    node_fs_set_method(node_fs_root_value(namespace_root), "readvSync", (void*)node_fs_readv_sync, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "writevSync", (void*)node_fs_writev_sync, 3);
    node_fs_install_constants(node_fs_root_value(namespace_root));
    node_fs_set_method(node_fs_root_value(namespace_root), "utimesSync", (void*)node_fs_utimes_sync, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "opendirSync", (void*)node_fs_opendir_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "readlinkSync", (void*)node_fs_readlink_sync, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "_toUnixTimestamp", (void*)node_fs_to_unix_timestamp, 1);
    node_fs_set_method(node_fs_root_value(namespace_root), "createReadStream", (void*)node_fs_create_read_stream, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "ReadStream", (void*)node_fs_create_read_stream, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "createWriteStream", (void*)node_fs_create_write_stream, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "WriteStream", (void*)node_fs_create_write_stream, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "chownSync", (void*)node_fs_chown_sync_export, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "lchownSync", (void*)node_fs_lchown_sync, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "fchownSync", (void*)node_fs_fchown_sync, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "lchmodSync", (void*)node_fs_lchmod_sync, 2);
    Item result = node_fs_root_value(namespace_root);
    node_fs_host->node->roots->root_frame_end(&frame);
    return result;
}

static Item node_fs_promises_namespace(void) {
    if (!node_fs_host || !node_fs_session || !node_fs_host->node || !node_fs_host->node->runtime ||
            !node_fs_host->node->runtime->resolve_host_namespace) return ItemNull;
    Item namespace_item = ItemNull;
    if (node_fs_host->node->runtime->resolve_host_namespace(node_fs_session, "fs/promises",
                                                            &namespace_item) != 0) {
        return ItemNull;
    }
    JubeRootFrame frame = {};
    if (!node_fs_roots_begin(&frame, 1)) return ItemNull;
    uint64_t* namespace_root = node_fs_host->node->roots->root_frame_take_slot(&frame);
    if (!namespace_root) {
        node_fs_host->node->roots->root_frame_end(&frame);
        return ItemNull;
    }
    *namespace_root = namespace_item.item;
    // Promise wrapper allocation has the same moving-heap requirement as the
    // callback namespace: never reuse an unrooted borrowed namespace Item.
    node_fs_set_method(node_fs_root_value(namespace_root), "readFile", (void*)node_fs_promises_read_file, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "writeFile", (void*)node_fs_promises_write_file, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "appendFile", (void*)node_fs_promises_append_file, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "stat", (void*)node_fs_promises_stat, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "lstat", (void*)node_fs_promises_lstat, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "mkdir", (void*)node_fs_promises_mkdir, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "access", (void*)node_fs_promises_access, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "chmod", (void*)node_fs_promises_chmod, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "chown", (void*)node_fs_promises_chown, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "lchown", (void*)node_fs_promises_lchown, 3);
    node_fs_set_method(node_fs_root_value(namespace_root), "copyFile", (void*)node_fs_promises_copy_file, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "truncate", (void*)node_fs_promises_truncate, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "unlink", (void*)node_fs_promises_unlink, 1);
    node_fs_set_method(node_fs_root_value(namespace_root), "rm", (void*)node_fs_promises_rm, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "realpath", (void*)node_fs_promises_realpath, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "mkdtemp", (void*)node_fs_promises_mkdtemp, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "symlink", (void*)node_fs_promises_symlink, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "rename", (void*)node_fs_promises_rename, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "readdir", (void*)node_fs_promises_readdir, 2);
    node_fs_set_method(node_fs_root_value(namespace_root), "open", (void*)node_fs_promises_open, 3);
    Item result = node_fs_root_value(namespace_root);
    node_fs_host->node->roots->root_frame_end(&frame);
    return result;
}

static const char* const node_fs_specifiers[] = {"fs"};
static const char* const node_fs_promises_specifiers[] = {"fs/promises"};
static const JubeNamespaceDef node_fs_namespaces[] = {
    {node_fs_specifiers, 1, node_fs_namespace, NULL, 0},
    {node_fs_promises_specifiers, 1, node_fs_promises_namespace, NULL, 0},
};

static const JubeModuleRequirements node_fs_requirements = {
    sizeof(JubeModuleRequirements),
    JUBE_HOST_API_VERSION,
    (uint32_t)(offsetof(JubeHostAPI, node) + sizeof(((JubeHostAPI*)NULL)->node)),
    0,
    JUBE_HOST_CAP_NODE_RUNTIME,
    JUBE_HOST_SERVICE_API_VERSION,
    sizeof(JubeHostNodeAPI),
    sizeof(JubeHostValueAPI),
    sizeof(JubeHostScriptAPI),
    sizeof(JubeHostRootAPI),
};

static int node_fs_init(const JubeHostAPI* host) {
    if (!host || !host->node || !host->node->runtime || !host->node->roots || !host->node->async_ops ||
            !host->node->events || !host->node->permission || !host->node->filesystem ||
            !host->value || !host->script ||
            !host->node->streams || !host->node->streams->file_read_stream_new ||
            !host->node->streams->file_write_stream_new ||
            !host->node->runtime->resolve_host_namespace || !host->node->roots->persistent_root_register ||
            !host->node->events->domain_current || !host->node->events->domain_call ||
            !host->node->permission->has_fs_read || !host->node->permission->has_fs_write ||
            !host->node->permission->check_fs_read || !host->node->permission->check_fs_write ||
            host->node->filesystem->struct_size < sizeof(JubeHostFilesystemAPI) ||
            !host->node->filesystem->read_write || !host->node->filesystem->read_write_release ||
            !host->node->async_ops->work_submit || !host->value->kind ||
            !host->node->async_ops->function_install_promisify_custom ||
            !host->node->async_ops->function_install_promisify_args ||
            !host->value->array_new || !host->value->array_push || !host->value->array_length ||
            !host->value->array_get || !host->value->is_array || !host->value->string_from_utf8_n ||
            !host->value->string_length || !host->value->string_bytes || !host->value->native_object_new ||
            !host->value->native_object_data || !host->value->property_get || !host->value->property_set ||
            !host->script->new_function ||
            !host->script->to_string || !host->script->error_lane_payload ||
            !host->script->get_number || !host->script->is_truthy || !host->script->object_create ||
            !host->script->bigint_to_int64_exact ||
            !host->node->runtime->current_this ||
            !host->value->number_to_int64_exact ||
            !host->script->promise_with_resolvers || !host->script->closure_env_new ||
            !host->script->new_closure || !host->script->call_function || !host->node->binary ||
            !host->node->binary->is_typed_array || !host->node->binary->typed_array_data ||
            !host->node->binary->typed_array_length) return -1;
    node_fs_host = host;
    return 0;
}

static void node_fs_runtime_attach(void* session) {
    if (!node_fs_host || !node_fs_host->node || !node_fs_host->node->runtime ||
            !node_fs_host->node->runtime->session_is_live ||
            !node_fs_host->node->runtime->session_is_live(session)) return;
    node_fs_session = session;
}

static void node_fs_runtime_reset(void* session) {
    (void)session;
}

static void node_fs_runtime_detach(void* session) {
    if (session != node_fs_session) return;
    for (NodeFsRequest* request = node_fs_pending; request; request = request->next) {
        request->detached = true;
        node_fs_unregister_roots(request);
        if (node_fs_host && node_fs_host->node && node_fs_host->node->async_ops &&
                node_fs_host->node->async_ops->work_cancel) {
            node_fs_host->node->async_ops->work_cancel(session, request->work_request_id);
        }
    }
    node_fs_session = NULL;
}

static void node_fs_shutdown(void) {
    node_fs_host = NULL;
    node_fs_session = NULL;
}

static const char* const node_fs_dependencies[] = { "node-core" };

static const JubeModuleDef node_fs_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "node-fs",
    "0.1.0",
    "Node fs and promises compatibility module",
    node_fs_types,
    2,
    NULL,
    0,
    node_fs_namespaces,
    2,
    node_fs_init,
    node_fs_shutdown,
    node_fs_interface,
    node_fs_type_bindings,
    (int32_t)(sizeof(node_fs_type_bindings) / sizeof(node_fs_type_bindings[0])),
    NULL,
    NULL,
    NULL,
    &node_fs_requirements,
    NULL,
    0,
    node_fs_runtime_attach,
    node_fs_runtime_reset,
    node_fs_runtime_detach,
    node_fs_dependencies,
    1,
};

#if !defined(LAMBDA_NODE_FS_DYNAMIC_MODULE)
extern "C" void node_fs_jube_register_static(void) {
    jube_register_static_module(&node_fs_module);
}
#endif

extern "C" const JubeModuleDef* node_fs_jube_module(void) { return &node_fs_module; }

#if defined(LAMBDA_NODE_FS_DYNAMIC_MODULE)
extern "C" const JubeModuleDef* jube_module(void) { return node_fs_jube_module(); }
#endif
