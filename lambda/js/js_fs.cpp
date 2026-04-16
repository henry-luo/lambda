/**
 * js_fs.cpp — Node.js-style 'fs' module for LambdaJS v15
 *
 * Provides synchronous and asynchronous file I/O backed by libuv.
 * Registered as built-in module 'fs' via js_module_get().
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "js_typed_array.h"
#include "../lambda-data.hpp"
#include "../transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/uv_loop.h"

#include <cstring>
#include "../../lib/mem.h"
#include "../../lib/file.h"

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

// Helper: get raw data pointer and length from a Buffer (typed array) Item
static uint8_t* buffer_data(Item buf, int* out_len) {
    if (!js_is_typed_array(buf)) { *out_len = 0; return NULL; }
    Map* m = buf.map;
    JsTypedArray* ta = (JsTypedArray*)m->data;
    if (!ta || !ta->data) { *out_len = 0; return NULL; }
    *out_len = ta->byte_length;
    return (uint8_t*)ta->data;
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

    int mode = 0777;
    bool recursive = false;

    // options can be an integer (mode) or an object { recursive, mode }
    if (get_type_id(options) == LMD_TYPE_INT) {
        mode = (int)it2i(options);
    } else if (get_type_id(options) == LMD_TYPE_FLOAT) {
        mode = (int)it2d(options);
    } else if (get_type_id(options) == LMD_TYPE_MAP) {
        Item mode_val = js_property_get(options, make_string_item("mode"));
        if (get_type_id(mode_val) == LMD_TYPE_INT) mode = (int)it2i(mode_val);
        else if (get_type_id(mode_val) == LMD_TYPE_FLOAT) mode = (int)it2d(mode_val);
        Item rec_val = js_property_get(options, make_string_item("recursive"));
        recursive = js_is_truthy(rec_val);
    }

    if (recursive) {
        // create parent directories as needed
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", path);
        for (char* p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                uv_fs_t req;
                uv_fs_mkdir(NULL, &req, tmp, mode, NULL);
                uv_fs_req_cleanup(&req);
                *p = '/';
            }
        }
    }

    uv_fs_t req;
    int r = uv_fs_mkdir(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);

    if (recursive) {
        // for recursive, return the first directory created (or undefined)
        return make_js_undefined();
    }
    if (r < 0 && r != UV_EEXIST) {
        log_error("fs: mkdirSync: '%s': %s", path, uv_strerror(r));
        return js_new_error(make_string_item(uv_strerror(r)));
    }
    return make_js_undefined();
}

// fs.mkdir(path[, options], callback) — async version
extern "C" Item js_fs_mkdir_async(Item path_item, Item options_or_cb, Item callback_item) {
    Item callback = callback_item;
    Item options = options_or_cb;

    // if second arg is a function, it's the callback (no options)
    if (get_type_id(options_or_cb) == LMD_TYPE_FUNC) {
        callback = options_or_cb;
        options = ItemNull;
    }

    // perform the operation synchronously
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) {
        if (get_type_id(callback) == LMD_TYPE_FUNC) {
            Item err = make_string_item("EINVAL: invalid path");
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        }
        return make_js_undefined();
    }

    int mode = 0777;
    bool recursive = false;

    if (get_type_id(options) == LMD_TYPE_INT) {
        mode = (int)it2i(options);
    } else if (get_type_id(options) == LMD_TYPE_FLOAT) {
        mode = (int)it2d(options);
    } else if (get_type_id(options) == LMD_TYPE_MAP) {
        Item mode_val = js_property_get(options, make_string_item("mode"));
        if (get_type_id(mode_val) == LMD_TYPE_INT) mode = (int)it2i(mode_val);
        else if (get_type_id(mode_val) == LMD_TYPE_FLOAT) mode = (int)it2d(mode_val);
        Item rec_val = js_property_get(options, make_string_item("recursive"));
        recursive = js_is_truthy(rec_val);
    }

    if (recursive) {
        char tmp[1024];
        snprintf(tmp, sizeof(tmp), "%s", path);
        for (char* p = tmp + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                uv_fs_t req;
                uv_fs_mkdir(NULL, &req, tmp, mode, NULL);
                uv_fs_req_cleanup(&req);
                *p = '/';
            }
        }
    }

    uv_fs_t req;
    int r = uv_fs_mkdir(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);

    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (r < 0 && !(recursive && r == UV_EEXIST)) {
            Item err = js_new_error(make_string_item(uv_strerror(r)));
            js_property_set(err, make_string_item("code"),
                make_string_item(r == UV_EEXIST ? "EEXIST" :
                                 r == UV_ENOENT ? "ENOENT" :
                                 r == UV_EACCES ? "EACCES" : "ERR_FS"));
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[1] = {ItemNull};
            js_call_function(callback, ItemNull, args, 1);
        }
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

// fs.copyFileSync(src, dest)
extern "C" Item js_fs_copyFileSync(Item src_item, Item dest_item) {
    char src_buf[1024], dest_buf[1024];
    const char* src = item_to_cstr(src_item, src_buf, sizeof(src_buf));
    const char* dest = item_to_cstr(dest_item, dest_buf, sizeof(dest_buf));
    if (!src || !dest) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_copyfile(NULL, &req, src, dest, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: copyFileSync: '%s' -> '%s': %s", src, dest, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.realpathSync(path) → string
extern "C" Item js_fs_realpathSync(Item path_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_realpath(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        log_error("fs: realpathSync: '%s': %s", path, uv_strerror(r));
        return ItemNull;
    }
    Item result = make_string_item((const char*)req.ptr);
    uv_fs_req_cleanup(&req);
    return result;
}

// fs.accessSync(path[, mode]) — throws if access check fails
// mode: fs.constants.F_OK (0), R_OK (4), W_OK (2), X_OK (1)
extern "C" Item js_fs_accessSync(Item path_item, Item mode_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    int mode = 0; // F_OK by default
    if (get_type_id(mode_item) == LMD_TYPE_INT) {
        mode = (int)it2i(mode_item);
    }

    uv_fs_t req;
    int r = uv_fs_access(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: accessSync: '%s': %s", path, uv_strerror(r));
        return js_throw_type_error("ENOENT: no such file or directory");
    }
    return make_js_undefined();
}

// fs.rmSync(path[, options]) — remove file or directory (optionally recursive)
extern "C" Item js_fs_rmSync(Item path_item, Item options_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    bool recursive = false;
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        Item rec_key = make_string_item("recursive");
        Item rec_val = js_property_get(options_item, rec_key);
        recursive = js_is_truthy(rec_val);
    }

    if (recursive) {
        // use lib/file.c recursive delete
        int r = file_delete_recursive(path);
        if (r != 0) {
            log_error("fs: rmSync: recursive delete failed for '%s'", path);
        }
    } else {
        // try unlink first, then rmdir
        uv_fs_t req;
        int r = uv_fs_unlink(NULL, &req, path, NULL);
        uv_fs_req_cleanup(&req);
        if (r < 0) {
            r = uv_fs_rmdir(NULL, &req, path, NULL);
            uv_fs_req_cleanup(&req);
            if (r < 0) {
                log_error("fs: rmSync: '%s': %s", path, uv_strerror(r));
            }
        }
    }
    return make_js_undefined();
}

// fs.mkdtempSync(prefix) → string
extern "C" Item js_fs_mkdtempSync(Item prefix_item) {
    char prefix_buf[1024];
    const char* prefix = item_to_cstr(prefix_item, prefix_buf, sizeof(prefix_buf));
    if (!prefix) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_mkdtemp(NULL, &req, prefix, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        log_error("fs: mkdtempSync: %s", uv_strerror(r));
        return ItemNull;
    }
    Item result = make_string_item(req.path);
    uv_fs_req_cleanup(&req);
    return result;
}

// fs.chmodSync(path, mode)
extern "C" Item js_fs_chmodSync(Item path_item, Item mode_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    int mode = 0;
    if (get_type_id(mode_item) == LMD_TYPE_INT) {
        mode = (int)it2i(mode_item);
    }

    uv_fs_t req;
    int r = uv_fs_chmod(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: chmodSync: '%s': %s", path, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.symlinkSync(target, path)
extern "C" Item js_fs_symlinkSync(Item target_item, Item path_item) {
    char target_buf[1024], path_buf[1024];
    const char* target = item_to_cstr(target_item, target_buf, sizeof(target_buf));
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!target || !path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_symlink(NULL, &req, target, path, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        log_error("fs: symlinkSync: '%s' -> '%s': %s", target, path, uv_strerror(r));
    }
    return make_js_undefined();
}

// fs.readlinkSync(path) → string
extern "C" Item js_fs_readlinkSync(Item path_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_readlink(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        log_error("fs: readlinkSync: '%s': %s", path, uv_strerror(r));
        return ItemNull;
    }
    Item result = make_string_item((const char*)req.ptr);
    uv_fs_req_cleanup(&req);
    return result;
}

// fs.lstatSync(path) — like statSync but doesn't follow symlinks
extern "C" Item js_fs_lstatSync(Item path_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return ItemNull;

    uv_fs_t req;
    int r = uv_fs_lstat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        log_error("fs: lstatSync: '%s': %s", path, uv_strerror(r));
        return ItemNull;
    }

    Item obj = js_new_object();
    js_property_set(obj, make_string_item("size"), (Item){.item = i2it(req.statbuf.st_size)});
    bool is_file = (req.statbuf.st_mode & S_IFMT) == S_IFREG;
    bool is_dir = (req.statbuf.st_mode & S_IFMT) == S_IFDIR;
    bool is_symlink_val = (req.statbuf.st_mode & S_IFMT) == S_IFLNK;
    js_property_set(obj, make_string_item("isFile"), (Item){.item = b2it(is_file)});
    js_property_set(obj, make_string_item("isDirectory"), (Item){.item = b2it(is_dir)});
    js_property_set(obj, make_string_item("isSymbolicLink"), (Item){.item = b2it(is_symlink_val)});

    double mtime_ms = (double)req.statbuf.st_mtim.tv_sec * 1000.0 +
                      (double)req.statbuf.st_mtim.tv_nsec / 1000000.0;
    double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *fp = mtime_ms;
    Item mtime_val;
    mtime_val.item = d2it(fp);
    js_property_set(obj, make_string_item("mtimeMs"), mtime_val);

    uv_fs_req_cleanup(&req);
    return obj;
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
// truncateSync
// =============================================================================

extern "C" Item js_fs_truncateSync(Item path_item, Item len_item) {
    if (get_type_id(path_item) != LMD_TYPE_STRING) return ItemNull;
    String* ps = it2s(path_item);
    char path[PATH_MAX];
    int plen = (int)ps->len;
    if (plen >= (int)sizeof(path)) plen = (int)sizeof(path) - 1;
    memcpy(path, ps->chars, plen);
    path[plen] = '\0';

    int64_t length = 0;
    if (get_type_id(len_item) == LMD_TYPE_INT) length = it2i(len_item);

    uv_fs_t req;
    int fd = uv_fs_open(lambda_uv_loop(), &req, path, UV_FS_O_WRONLY, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) return ItemNull;

    int r = uv_fs_ftruncate(lambda_uv_loop(), &req, fd, length, NULL);
    uv_fs_req_cleanup(&req);

    uv_fs_close(lambda_uv_loop(), &req, fd, NULL);
    uv_fs_req_cleanup(&req);

    if (r < 0) return ItemNull;
    return make_js_undefined();
}

// =============================================================================
// File Descriptor Operations: openSync, closeSync, readSync, writeSync, fstatSync
// =============================================================================

extern "C" Item js_fs_openSync(Item path_item, Item flags_item, Item mode_item) {
    char path[PATH_MAX];
    const char* p = item_to_cstr(path_item, path, sizeof(path));
    if (!p) return ItemNull;

    int flags = UV_FS_O_RDONLY;
    if (get_type_id(flags_item) == LMD_TYPE_STRING) {
        String* fs = it2s(flags_item);
        if (fs->len == 1 && fs->chars[0] == 'r') flags = UV_FS_O_RDONLY;
        else if (fs->len == 2 && fs->chars[0] == 'r' && fs->chars[1] == '+') flags = UV_FS_O_RDWR;
        else if (fs->len == 1 && fs->chars[0] == 'w') flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC;
        else if (fs->len == 2 && fs->chars[0] == 'w' && fs->chars[1] == '+') flags = UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_TRUNC;
        else if (fs->len == 1 && fs->chars[0] == 'a') flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND;
        else if (fs->len == 2 && fs->chars[0] == 'a' && fs->chars[1] == '+') flags = UV_FS_O_RDWR | UV_FS_O_CREAT | UV_FS_O_APPEND;
        else if (fs->len == 2 && fs->chars[0] == 'a' && fs->chars[1] == 'x') flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_APPEND | UV_FS_O_EXCL;
        else if (fs->len == 2 && fs->chars[0] == 'w' && fs->chars[1] == 'x') flags = UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC | UV_FS_O_EXCL;
    } else if (get_type_id(flags_item) == LMD_TYPE_INT) {
        flags = (int)it2i(flags_item);
    }

    int mode = 0666;
    if (get_type_id(mode_item) == LMD_TYPE_INT) mode = (int)it2i(mode_item);

    uv_fs_t req;
    int fd = uv_fs_open(lambda_uv_loop(), &req, p, flags, mode, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) return ItemNull;
    return (Item){.item = i2it((int64_t)fd)};
}

extern "C" Item js_fs_closeSync(Item fd_item) {
    if (get_type_id(fd_item) != LMD_TYPE_INT) return ItemNull;
    int fd = (int)it2i(fd_item);
    uv_fs_t req;
    uv_fs_close(lambda_uv_loop(), &req, fd, NULL);
    uv_fs_req_cleanup(&req);
    return make_js_undefined();
}

extern "C" Item js_fs_readSync(Item fd_item, Item buffer_item, Item offset_item, Item length_item, Item position_item) {
    if (get_type_id(fd_item) != LMD_TYPE_INT) return (Item){.item = i2it(0)};
    int fd = (int)it2i(fd_item);

    int blen = 0;
    uint8_t* data = buffer_data(buffer_item, &blen);
    if (!data) return (Item){.item = i2it(0)};

    int offset = 0, length = blen;
    if (get_type_id(offset_item) == LMD_TYPE_INT) offset = (int)it2i(offset_item);
    if (get_type_id(length_item) == LMD_TYPE_INT) length = (int)it2i(length_item);
    if (offset + length > blen) length = blen - offset;
    if (length <= 0) return (Item){.item = i2it(0)};

    int64_t position = -1;
    if (get_type_id(position_item) == LMD_TYPE_INT) position = it2i(position_item);

    uv_buf_t buf = uv_buf_init((char*)(data + offset), length);
    uv_fs_t req;
    int nread = uv_fs_read(lambda_uv_loop(), &req, fd, &buf, 1, position, NULL);
    uv_fs_req_cleanup(&req);
    if (nread < 0) return (Item){.item = i2it(0)};
    return (Item){.item = i2it((int64_t)nread)};
}

extern "C" Item js_fs_writeSync(Item fd_item, Item data_item, Item offset_item, Item length_item, Item position_item) {
    if (get_type_id(fd_item) != LMD_TYPE_INT) return (Item){.item = i2it(0)};
    int fd = (int)it2i(fd_item);

    // data can be a string or a Buffer
    const char* write_buf = NULL;
    int write_len = 0;

    if (get_type_id(data_item) == LMD_TYPE_STRING) {
        String* s = it2s(data_item);
        write_buf = s->chars;
        write_len = (int)s->len;
    } else {
        int blen = 0;
        uint8_t* bdata = buffer_data(data_item, &blen);
        if (bdata) {
            write_buf = (const char*)bdata;
            write_len = blen;
        }
    }
    if (!write_buf) return (Item){.item = i2it(0)};

    int offset = 0, length = write_len;
    if (get_type_id(offset_item) == LMD_TYPE_INT) offset = (int)it2i(offset_item);
    if (get_type_id(length_item) == LMD_TYPE_INT) length = (int)it2i(length_item);
    if (offset + length > write_len) length = write_len - offset;
    if (length <= 0) return (Item){.item = i2it(0)};

    int64_t position = -1;
    if (get_type_id(position_item) == LMD_TYPE_INT) position = it2i(position_item);

    uv_buf_t buf = uv_buf_init((char*)(write_buf + offset), length);
    uv_fs_t req;
    int nwritten = uv_fs_write(lambda_uv_loop(), &req, fd, &buf, 1, position, NULL);
    uv_fs_req_cleanup(&req);
    if (nwritten < 0) return (Item){.item = i2it(0)};
    return (Item){.item = i2it((int64_t)nwritten)};
}

extern "C" Item js_fs_fstatSync(Item fd_item) {
    if (get_type_id(fd_item) != LMD_TYPE_INT) return ItemNull;
    int fd = (int)it2i(fd_item);
    uv_fs_t req;
    int r = uv_fs_fstat(lambda_uv_loop(), &req, fd, NULL);
    if (r < 0) { uv_fs_req_cleanup(&req); return ItemNull; }

    uv_stat_t* st = &req.statbuf;
    Item result = js_new_object();
    js_property_set(result, make_string_item("size"), (Item){.item = i2it((int64_t)st->st_size)});
    js_property_set(result, make_string_item("mode"), (Item){.item = i2it((int64_t)st->st_mode)});
    js_property_set(result, make_string_item("uid"),  (Item){.item = i2it((int64_t)st->st_uid)});
    js_property_set(result, make_string_item("gid"),  (Item){.item = i2it((int64_t)st->st_gid)});
    js_property_set(result, make_string_item("nlink"),(Item){.item = i2it((int64_t)st->st_nlink)});
    js_property_set(result, make_string_item("dev"),  (Item){.item = i2it((int64_t)st->st_dev)});
    js_property_set(result, make_string_item("ino"),  (Item){.item = i2it((int64_t)st->st_ino)});

    // time fields in milliseconds
    double mt = (double)st->st_mtim.tv_sec * 1000.0 + (double)st->st_mtim.tv_nsec / 1e6;
    double at = (double)st->st_atim.tv_sec * 1000.0 + (double)st->st_atim.tv_nsec / 1e6;
    double ct = (double)st->st_ctim.tv_sec * 1000.0 + (double)st->st_ctim.tv_nsec / 1e6;
    double bt = (double)st->st_birthtim.tv_sec * 1000.0 + (double)st->st_birthtim.tv_nsec / 1e6;

    double* mtp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *mtp = mt;
    js_property_set(result, make_string_item("mtimeMs"), (Item){.item = d2it(mtp)});
    double* atp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *atp = at;
    js_property_set(result, make_string_item("atimeMs"), (Item){.item = d2it(atp)});
    double* ctp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *ctp = ct;
    js_property_set(result, make_string_item("ctimeMs"), (Item){.item = d2it(ctp)});
    double* btp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
    *btp = bt;
    js_property_set(result, make_string_item("birthtimeMs"), (Item){.item = d2it(btp)});

    // isFile() / isDirectory() method checks
    bool is_file = S_ISREG(st->st_mode);
    bool is_dir = S_ISDIR(st->st_mode);
    js_property_set(result, make_string_item("isFile"), (Item){.item = b2it(is_file)});
    js_property_set(result, make_string_item("isDirectory"), (Item){.item = b2it(is_dir)});

    uv_fs_req_cleanup(&req);
    return result;
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
    js_fs_set_method(fs_namespace, "mkdir",           (void*)js_fs_mkdir_async, 3);

    // additional sync methods
    js_fs_set_method(fs_namespace, "copyFileSync",    (void*)js_fs_copyFileSync, 2);
    js_fs_set_method(fs_namespace, "realpathSync",    (void*)js_fs_realpathSync, 1);
    js_fs_set_method(fs_namespace, "accessSync",      (void*)js_fs_accessSync, 2);
    js_fs_set_method(fs_namespace, "rmSync",          (void*)js_fs_rmSync, 2);
    js_fs_set_method(fs_namespace, "mkdtempSync",     (void*)js_fs_mkdtempSync, 1);
    js_fs_set_method(fs_namespace, "chmodSync",       (void*)js_fs_chmodSync, 2);
    js_fs_set_method(fs_namespace, "symlinkSync",     (void*)js_fs_symlinkSync, 2);
    js_fs_set_method(fs_namespace, "readlinkSync",    (void*)js_fs_readlinkSync, 1);
    js_fs_set_method(fs_namespace, "lstatSync",       (void*)js_fs_lstatSync, 1);
    js_fs_set_method(fs_namespace, "truncateSync",    (void*)js_fs_truncateSync, 2);

    // file descriptor operations
    js_fs_set_method(fs_namespace, "openSync",        (void*)js_fs_openSync, 3);
    js_fs_set_method(fs_namespace, "closeSync",       (void*)js_fs_closeSync, 1);
    js_fs_set_method(fs_namespace, "readSync",        (void*)js_fs_readSync, 5);
    js_fs_set_method(fs_namespace, "writeSync",       (void*)js_fs_writeSync, 5);
    js_fs_set_method(fs_namespace, "fstatSync",       (void*)js_fs_fstatSync, 1);

    // fs.constants
    Item constants = js_new_object();
    js_property_set(constants, make_string_item("F_OK"), (Item){.item = i2it(0)});
    js_property_set(constants, make_string_item("R_OK"), (Item){.item = i2it(4)});
    js_property_set(constants, make_string_item("W_OK"), (Item){.item = i2it(2)});
    js_property_set(constants, make_string_item("X_OK"), (Item){.item = i2it(1)});
    js_property_set(constants, make_string_item("O_RDONLY"),   (Item){.item = i2it(UV_FS_O_RDONLY)});
    js_property_set(constants, make_string_item("O_WRONLY"),   (Item){.item = i2it(UV_FS_O_WRONLY)});
    js_property_set(constants, make_string_item("O_RDWR"),     (Item){.item = i2it(UV_FS_O_RDWR)});
    js_property_set(constants, make_string_item("O_CREAT"),    (Item){.item = i2it(UV_FS_O_CREAT)});
    js_property_set(constants, make_string_item("O_TRUNC"),    (Item){.item = i2it(UV_FS_O_TRUNC)});
    js_property_set(constants, make_string_item("O_APPEND"),   (Item){.item = i2it(UV_FS_O_APPEND)});
    js_property_set(constants, make_string_item("O_EXCL"),     (Item){.item = i2it(UV_FS_O_EXCL)});
    js_property_set(fs_namespace, make_string_item("constants"), constants);

    // set "default" export to the namespace itself (for `import fs from 'fs'`)
    Item default_key = make_string_item("default");
    js_property_set(fs_namespace, default_key, fs_namespace);

    return fs_namespace;
}

// Reset fs namespace (for re-initialization between runs)
extern "C" void js_fs_reset(void) {
    fs_namespace = (Item){0};
}
