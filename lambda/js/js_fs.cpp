/**
 * js_fs.cpp — Node.js-style 'fs' module for LambdaJS v15
 *
 * Provides synchronous and asynchronous file I/O backed by libuv.
 * Registered as built-in module 'fs' via js_module_get().
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"

#include <cstring>
#include "../../lib/mem.h"

extern Input* js_input;

// Helper: make JS undefined value
static inline Item make_js_undefined() {
    return (Item){.item = ((uint64_t)LMD_TYPE_UNDEFINED << 56)};
}

// Helper: extract a null-terminated C string from an Item string
// Returns a stack-allocated buffer (caller should not free)
static const char* item_to_cstr(Item value, char* buf, int buf_size) {
    if (get_type_id(value) != LMD_TYPE_STRING) return NULL;
    String* s = it2s(value);
    int len = (int)s->len;
    if (len >= buf_size) len = buf_size - 1;
    memcpy(buf, s->chars, len);
    buf[len] = '\0';
    return buf;
}

// Helper: create a string Item from a C string
static Item make_string_item(const char* str, int len) {
    if (!str) return ItemNull;
    String* s = heap_create_name(str, (size_t)len);
    return (Item){.item = s2it(s)};
}

static Item make_string_item(const char* str) {
    if (!str) return ItemNull;
    return make_string_item(str, (int)strlen(str));
}

// =============================================================================
// Synchronous File Operations
// =============================================================================

// fs.readFileSync(path[, encoding])
// Returns file contents as a string (assumes UTF-8)
extern "C" Item js_fs_readFileSync(Item path_item, Item encoding_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) {
        log_error("fs: readFileSync: invalid path argument");
        return ItemNull;
    }

    uv_fs_t req;
    int fd = uv_fs_open(NULL, &req, path, UV_FS_O_RDONLY, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) {
        log_error("fs: readFileSync: cannot open '%s': %s", path, uv_strerror(fd));
        return ItemNull;
    }

    // stat to get file size
    uv_fs_t stat_req;
    int r = uv_fs_fstat(NULL, &stat_req, fd, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&stat_req);
        uv_fs_t close_req;
        uv_fs_close(NULL, &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
        log_error("fs: readFileSync: cannot stat '%s': %s", path, uv_strerror(r));
        return ItemNull;
    }
    size_t file_size = (size_t)stat_req.statbuf.st_size;
    uv_fs_req_cleanup(&stat_req);

    // read file contents
    char* data = (char*)mem_alloc(file_size + 1, MEM_CAT_JS_RUNTIME);
    if (!data) {
        uv_fs_t close_req;
        uv_fs_close(NULL, &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
        return ItemNull;
    }

    uv_buf_t buf = uv_buf_init(data, (unsigned int)file_size);
    uv_fs_t read_req;
    int bytes_read = (int)uv_fs_read(NULL, &read_req, fd, &buf, 1, 0, NULL);
    uv_fs_req_cleanup(&read_req);

    uv_fs_t close_req;
    uv_fs_close(NULL, &close_req, fd, NULL);
    uv_fs_req_cleanup(&close_req);

    if (bytes_read < 0) {
        mem_free(data);
        log_error("fs: readFileSync: read error for '%s': %s", path, uv_strerror(bytes_read));
        return ItemNull;
    }

    data[bytes_read] = '\0';
    Item result = make_string_item(data, bytes_read);
    mem_free(data);
    return result;
}

// fs.writeFileSync(path, data)
extern "C" Item js_fs_writeFileSync(Item path_item, Item data_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) {
        log_error("fs: writeFileSync: invalid path argument");
        return ItemNull;
    }

    // convert data to string
    Item str_item = js_to_string(data_item);
    if (get_type_id(str_item) != LMD_TYPE_STRING) return ItemNull;
    String* str = it2s(str_item);

    uv_fs_t req;
    int fd = uv_fs_open(NULL, &req, path,
        UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC, 0644, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) {
        log_error("fs: writeFileSync: cannot open '%s': %s", path, uv_strerror(fd));
        return ItemNull;
    }

    uv_buf_t buf = uv_buf_init((char*)str->chars, (unsigned int)str->len);
    uv_fs_t write_req;
    int written = (int)uv_fs_write(NULL, &write_req, fd, &buf, 1, 0, NULL);
    uv_fs_req_cleanup(&write_req);

    uv_fs_t close_req;
    uv_fs_close(NULL, &close_req, fd, NULL);
    uv_fs_req_cleanup(&close_req);

    if (written < 0) {
        log_error("fs: writeFileSync: write error for '%s': %s", path, uv_strerror(written));
        return ItemNull;
    }

    return make_js_undefined();
}

// fs.existsSync(path) → bool
extern "C" Item js_fs_existsSync(Item path_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return (Item){.item = b2it(false)};

    uv_fs_t req;
    int r = uv_fs_stat(NULL, &req, path, NULL);
    uv_fs_req_cleanup(&req);
    return (Item){.item = b2it(r == 0)};
}

// fs.unlinkSync(path) — delete a file
extern "C" Item js_fs_unlinkSync(Item path_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_unlink(NULL, &req, path, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: unlinkSync: '%s': %s", path, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.mkdirSync(path[, options])
extern "C" Item js_fs_mkdirSync(Item path_item, Item options) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_mkdir(NULL, &req, path, 0755, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0 && r != UV_EEXIST) {
        log_error("fs: mkdirSync: '%s': %s", path, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.rmdirSync(path)
extern "C" Item js_fs_rmdirSync(Item path_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_rmdir(NULL, &req, path, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: rmdirSync: '%s': %s", path, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.renameSync(oldPath, newPath)
extern "C" Item js_fs_renameSync(Item old_path_item, Item new_path_item) {
    char old_buf[1024], new_buf[1024];
    const char* old_path = item_to_cstr(old_path_item, old_buf, sizeof(old_buf));
    const char* new_path = item_to_cstr(new_path_item, new_buf, sizeof(new_buf));
    if (!old_path || !new_path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_rename(NULL, &req, old_path, new_path, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: renameSync: '%s' -> '%s': %s", old_path, new_path, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.readdirSync(path) → Array<string>
extern "C" Item js_fs_readdirSync(Item path_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int n = uv_fs_scandir(NULL, &req, path, 0, NULL);
    if (n < 0) {
        uv_fs_req_cleanup(&req);
        log_error("fs: readdirSync: '%s': %s", path, uv_strerror(n));
        return ItemNull;
    }

    Item arr = js_array_new(0);
    uv_dirent_t dirent;
    while (uv_fs_scandir_next(&req, &dirent) != UV_EOF) {
        js_array_push(arr, make_string_item(dirent.name));
    }

    uv_fs_req_cleanup(&req);
    return arr;
}

// fs.statSync(path) → { size, isFile, isDirectory, ... }
extern "C" Item js_fs_statSync(Item path_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_stat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        log_error("fs: statSync: '%s': %s", path, uv_strerror(r));
        return ItemNull;
    }

    Item obj = js_new_object();
    Item size_key = make_string_item("size");
    js_property_set(obj, size_key, (Item){.item = i2it(req.statbuf.st_size)});

    bool is_file = (req.statbuf.st_mode & S_IFMT) == S_IFREG;
    bool is_dir = (req.statbuf.st_mode & S_IFMT) == S_IFDIR;
    Item isFile_key = make_string_item("isFile");
    Item isDir_key = make_string_item("isDirectory");
    // store as method-like properties (bool values)
    js_property_set(obj, isFile_key, (Item){.item = b2it(is_file)});
    js_property_set(obj, isDir_key, (Item){.item = b2it(is_dir)});

    Item mtime_key = make_string_item("mtimeMs");
    double mtime_ms = (double)req.statbuf.st_mtim.tv_sec * 1000.0 +
                      (double)req.statbuf.st_mtim.tv_nsec / 1000000.0;
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = mtime_ms;
    Item mtime_val;
    mtime_val.item = d2it(fp);
    js_property_set(obj, mtime_key, mtime_val);

    uv_fs_req_cleanup(&req);
    return obj;
}

// fs.appendFileSync(path, data)
extern "C" Item js_fs_appendFileSync(Item path_item, Item data_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    Item str_item = js_to_string(data_item);
    if (get_type_id(str_item) != LMD_TYPE_STRING) return ItemNull;
    String* str = it2s(str_item);

    uv_fs_t req;
    int fd = uv_fs_open(NULL, &req, path,
        UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND, 0644, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) {
        log_error("fs: appendFileSync: cannot open '%s': %s", path, uv_strerror(fd));
        return ItemNull;
    }

    uv_buf_t buf = uv_buf_init((char*)str->chars, (unsigned int)str->len);
    uv_fs_t write_req;
    uv_fs_write(NULL, &write_req, fd, &buf, 1, -1, NULL);
    uv_fs_req_cleanup(&write_req);

    uv_fs_t close_req;
    uv_fs_close(NULL, &close_req, fd, NULL);
    uv_fs_req_cleanup(&close_req);

    return make_js_undefined();
}

// =============================================================================
// Asynchronous File Operations (callback-style)
// =============================================================================

typedef struct JsFsReq {
    uv_fs_t req;
    Item callback;        // JS callback: (err, data) => ...
    char* buffer;         // read buffer (for readFile)
    size_t buffer_size;
    int fd;               // file descriptor
    char path[1024];      // file path (for multi-step operations)
} JsFsReq;

static void on_fs_read_complete(uv_fs_t* req) {
    JsFsReq* fsreq = (JsFsReq*)req->data;
    int result = (int)req->result;

    // close the file
    uv_fs_t close_req;
    uv_fs_close(lambda_uv_loop(), &close_req, fsreq->fd, NULL);
    uv_fs_req_cleanup(&close_req);

    if (result < 0) {
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item err = make_string_item(uv_strerror(result));
            Item args[2] = {err, ItemNull};
            js_call_function(fsreq->callback, ItemNull, args, 2);
        }
    } else {
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item data = make_string_item(fsreq->buffer, result);
            Item args[2] = {ItemNull, data};
            js_call_function(fsreq->callback, ItemNull, args, 2);
        }
    }

    if (fsreq->buffer) mem_free(fsreq->buffer);
    uv_fs_req_cleanup(req);
    mem_free(fsreq);
}

static void on_fs_open_for_read(uv_fs_t* req) {
    JsFsReq* fsreq = (JsFsReq*)req->data;
    int fd = (int)req->result;
    uv_fs_req_cleanup(req);

    if (fd < 0) {
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item err = make_string_item(uv_strerror(fd));
            Item args[2] = {err, ItemNull};
            js_call_function(fsreq->callback, ItemNull, args, 2);
        }
        mem_free(fsreq);
        return;
    }

    fsreq->fd = fd;

    // stat to get file size
    uv_fs_t stat_req;
    int r = uv_fs_fstat(NULL, &stat_req, fd, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&stat_req);
        uv_fs_t close_req;
        uv_fs_close(lambda_uv_loop(), &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item err = make_string_item(uv_strerror(r));
            Item args[2] = {err, ItemNull};
            js_call_function(fsreq->callback, ItemNull, args, 2);
        }
        mem_free(fsreq);
        return;
    }

    size_t file_size = (size_t)stat_req.statbuf.st_size;
    uv_fs_req_cleanup(&stat_req);

    fsreq->buffer = (char*)mem_alloc(file_size + 1, MEM_CAT_JS_RUNTIME);
    fsreq->buffer_size = file_size;
    if (!fsreq->buffer) {
        uv_fs_t close_req;
        uv_fs_close(lambda_uv_loop(), &close_req, fd, NULL);
        uv_fs_req_cleanup(&close_req);
        mem_free(fsreq);
        return;
    }

    uv_buf_t buf = uv_buf_init(fsreq->buffer, (unsigned int)file_size);
    fsreq->req.data = fsreq;
    uv_fs_read(lambda_uv_loop(), &fsreq->req, fd, &buf, 1, 0, on_fs_read_complete);
}

// fs.readFile(path, callback)
extern "C" Item js_fs_readFile(Item path_item, Item callback) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    JsFsReq* fsreq = (JsFsReq*)mem_calloc(1, sizeof(JsFsReq), MEM_CAT_JS_RUNTIME);
    if (!fsreq) return ItemNull;

    fsreq->callback = callback;
    snprintf(fsreq->path, sizeof(fsreq->path), "%s", path);
    fsreq->req.data = fsreq;

    uv_fs_open(lambda_uv_loop(), &fsreq->req, path, UV_FS_O_RDONLY, 0, on_fs_open_for_read);
    return make_js_undefined();
}

static void on_fs_write_complete(uv_fs_t* req) {
    JsFsReq* fsreq = (JsFsReq*)req->data;

    // close the file
    uv_fs_t close_req;
    uv_fs_close(lambda_uv_loop(), &close_req, fsreq->fd, NULL);
    uv_fs_req_cleanup(&close_req);

    if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
        if (req->result < 0) {
            Item err = make_string_item(uv_strerror((int)req->result));
            Item args[1] = {err};
            js_call_function(fsreq->callback, ItemNull, args, 1);
        } else {
            Item args[1] = {ItemNull};
            js_call_function(fsreq->callback, ItemNull, args, 1);
        }
    }

    if (fsreq->buffer) mem_free(fsreq->buffer);
    uv_fs_req_cleanup(req);
    mem_free(fsreq);
}

static void on_fs_open_for_write(uv_fs_t* req) {
    JsFsReq* fsreq = (JsFsReq*)req->data;
    int fd = (int)req->result;
    uv_fs_req_cleanup(req);

    if (fd < 0) {
        if (get_type_id(fsreq->callback) == LMD_TYPE_FUNC) {
            Item err = make_string_item(uv_strerror(fd));
            Item args[1] = {err};
            js_call_function(fsreq->callback, ItemNull, args, 1);
        }
        if (fsreq->buffer) mem_free(fsreq->buffer);
        mem_free(fsreq);
        return;
    }

    fsreq->fd = fd;
    uv_buf_t buf = uv_buf_init(fsreq->buffer, (unsigned int)fsreq->buffer_size);
    fsreq->req.data = fsreq;
    uv_fs_write(lambda_uv_loop(), &fsreq->req, fd, &buf, 1, 0, on_fs_write_complete);
}

// fs.writeFile(path, data, callback)
extern "C" Item js_fs_writeFile(Item path_item, Item data_item, Item callback) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    Item str_item = js_to_string(data_item);
    if (get_type_id(str_item) != LMD_TYPE_STRING) return ItemNull;
    String* str = it2s(str_item);

    JsFsReq* fsreq = (JsFsReq*)mem_calloc(1, sizeof(JsFsReq), MEM_CAT_JS_RUNTIME);
    if (!fsreq) return ItemNull;

    // copy write data since it may be GC'd during async operation
    fsreq->buffer = (char*)mem_alloc(str->len, MEM_CAT_JS_RUNTIME);
    if (!fsreq->buffer) { mem_free(fsreq); return ItemNull; }
    memcpy(fsreq->buffer, str->chars, str->len);
    fsreq->buffer_size = str->len;
    fsreq->callback = callback;
    fsreq->req.data = fsreq;

    uv_fs_open(lambda_uv_loop(), &fsreq->req, path,
        UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC, 0644, on_fs_open_for_write);
    return make_js_undefined();
}

// =============================================================================
// fs Module Namespace Object
// =============================================================================

static Item fs_namespace = {0};

static void js_fs_set_method(Item ns, const char* name, void* func_ptr, int param_count) {
    Item key = make_string_item(name);
    Item fn = js_new_function(func_ptr, param_count);
    js_property_set(ns, key, fn);
}

extern "C" Item js_get_fs_namespace(void) {
    if (fs_namespace.item != 0) return fs_namespace;

    fs_namespace = js_new_object();

    // synchronous methods
    js_fs_set_method(fs_namespace, "readFileSync",    (void*)js_fs_readFileSync, 2);
    js_fs_set_method(fs_namespace, "writeFileSync",   (void*)js_fs_writeFileSync, 2);
    js_fs_set_method(fs_namespace, "existsSync",      (void*)js_fs_existsSync, 1);
    js_fs_set_method(fs_namespace, "unlinkSync",      (void*)js_fs_unlinkSync, 1);
    js_fs_set_method(fs_namespace, "mkdirSync",       (void*)js_fs_mkdirSync, 2);
    js_fs_set_method(fs_namespace, "rmdirSync",       (void*)js_fs_rmdirSync, 1);
    js_fs_set_method(fs_namespace, "renameSync",      (void*)js_fs_renameSync, 2);
    js_fs_set_method(fs_namespace, "readdirSync",     (void*)js_fs_readdirSync, 1);
    js_fs_set_method(fs_namespace, "statSync",        (void*)js_fs_statSync, 1);
    js_fs_set_method(fs_namespace, "appendFileSync",  (void*)js_fs_appendFileSync, 2);

    // asynchronous (callback) methods
    js_fs_set_method(fs_namespace, "readFile",        (void*)js_fs_readFile, 2);
    js_fs_set_method(fs_namespace, "writeFile",       (void*)js_fs_writeFile, 3);

    // set "default" export to the namespace itself (for `import fs from 'fs'`)
    Item default_key = make_string_item("default");
    js_property_set(fs_namespace, default_key, fs_namespace);

    return fs_namespace;
}

// Reset fs namespace (for re-initialization between runs)
extern "C" void js_fs_reset(void) {
    fs_namespace = (Item){0};
}
