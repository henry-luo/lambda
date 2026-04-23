/**
 * js_fs.cpp — Node.js-style 'fs' module for LambdaJS v15
 *
 * Provides synchronous and asynchronous file I/O backed by libuv.
 * Registered as built-in module 'fs' via js_module_get().
 */
#include "js_runtime.h"
#include "js_event_loop.h"
#include "js_typed_array.h"
#include "js_error_codes.h"
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
// Stats prototype — provides isFile(), isDirectory(), etc. methods
// =============================================================================
static Item stats_proto = {0};

// Stats method: checks mode bits via js_get_this().__mode
extern "C" Item js_get_this(void);
extern "C" Item js_date_new_from(Item value);

extern "C" Item js_stats_isFile() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFREG)};
}
extern "C" Item js_stats_isDirectory() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFDIR)};
}
extern "C" Item js_stats_isSymbolicLink() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFLNK)};
}
extern "C" Item js_stats_isBlockDevice() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFBLK)};
}
extern "C" Item js_stats_isCharacterDevice() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFCHR)};
}
extern "C" Item js_stats_isFIFO() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFIFO)};
}
extern "C" Item js_stats_isSocket() {
    Item mode_val = js_property_get(js_get_this(), make_string_item("__mode"));
    int mode = (int)it2i(mode_val);
    return (Item){.item = b2it((mode & S_IFMT) == S_IFSOCK)};
}

static Item get_stats_proto() {
    if (stats_proto.item != 0) return stats_proto;
    stats_proto = js_new_object();
    js_property_set(stats_proto, make_string_item("isFile"), js_new_function((void*)js_stats_isFile, 0));
    js_property_set(stats_proto, make_string_item("isDirectory"), js_new_function((void*)js_stats_isDirectory, 0));
    js_property_set(stats_proto, make_string_item("isSymbolicLink"), js_new_function((void*)js_stats_isSymbolicLink, 0));
    js_property_set(stats_proto, make_string_item("isBlockDevice"), js_new_function((void*)js_stats_isBlockDevice, 0));
    js_property_set(stats_proto, make_string_item("isCharacterDevice"), js_new_function((void*)js_stats_isCharacterDevice, 0));
    js_property_set(stats_proto, make_string_item("isFIFO"), js_new_function((void*)js_stats_isFIFO, 0));
    js_property_set(stats_proto, make_string_item("isSocket"), js_new_function((void*)js_stats_isSocket, 0));
    return stats_proto;
}

// Helper: build a Stats object from uv_stat_t
static Item make_stats_object(const uv_stat_t* st) {
    Item obj = js_new_object();
    
    // Store mode for isFile/isDirectory/etc methods
    js_property_set(obj, make_string_item("__mode"), (Item){.item = i2it((int64_t)st->st_mode)});
    js_property_set(obj, make_string_item("mode"), (Item){.item = i2it((int64_t)st->st_mode)});
    js_property_set(obj, make_string_item("size"), (Item){.item = i2it((int64_t)st->st_size)});
    js_property_set(obj, make_string_item("uid"), (Item){.item = i2it((int64_t)st->st_uid)});
    js_property_set(obj, make_string_item("gid"), (Item){.item = i2it((int64_t)st->st_gid)});
    js_property_set(obj, make_string_item("nlink"), (Item){.item = i2it((int64_t)st->st_nlink)});
    js_property_set(obj, make_string_item("ino"), (Item){.item = i2it((int64_t)st->st_ino)});
    js_property_set(obj, make_string_item("dev"), (Item){.item = i2it((int64_t)st->st_dev)});
    js_property_set(obj, make_string_item("rdev"), (Item){.item = i2it((int64_t)st->st_rdev)});
    js_property_set(obj, make_string_item("blksize"), (Item){.item = i2it((int64_t)st->st_blksize)});
    js_property_set(obj, make_string_item("blocks"), (Item){.item = i2it((int64_t)st->st_blocks)});

    // Time properties in milliseconds
    auto set_time = [&](const char* name, const uv_timespec_t& ts) {
        double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = ms;
        Item val;
        val.item = d2it(fp);
        js_property_set(obj, make_string_item(name), val);
    };
    set_time("atimeMs", st->st_atim);
    set_time("mtimeMs", st->st_mtim);
    set_time("ctimeMs", st->st_ctim);
    set_time("birthtimeMs", st->st_birthtim);

    // Time properties as Date objects (for stats.mtime instanceof Date, etc.)
    auto set_date = [&](const char* name, const uv_timespec_t& ts) {
        double ms = (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
        double* fp = (double*)heap_alloc(sizeof(double), LMD_TYPE_FLOAT);
        *fp = ms;
        Item ms_item;
        ms_item.item = d2it(fp);
        js_property_set(obj, make_string_item(name), js_date_new_from(ms_item));
    };
    set_date("atime", st->st_atim);
    set_date("mtime", st->st_mtim);
    set_date("ctime", st->st_ctim);
    set_date("birthtime", st->st_birthtim);

    // Set __proto__ so methods resolve via prototype chain lookup
    js_property_set(obj, make_string_item("__proto__"), get_stats_proto());
    
    return obj;
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
        return js_throw_invalid_arg_type("path", "string", path_item);
    }

    uv_fs_t req;
    int fd = uv_fs_open(NULL, &req, path, UV_FS_O_RDONLY, 0, NULL);
    uv_fs_req_cleanup(&req);
    if (fd < 0) {
        return js_throw_system_error(fd, "open", path);
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
        return js_throw_invalid_arg_type("path", "string", path_item);
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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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
    if (!old_path) return js_throw_invalid_arg_type("oldPath", "string", old_path_item);
    if (!new_path) return js_throw_invalid_arg_type("newPath", "string", new_path_item);

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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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

// fs.statSync(path) → Stats object with isFile(), isDirectory(), etc.
extern "C" Item js_fs_statSync(Item path_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

    uv_fs_t req;
    int r = uv_fs_stat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return js_throw_system_error(r, "stat", path);
    }

    Item obj = make_stats_object(&req.statbuf);
    uv_fs_req_cleanup(&req);
    return obj;
}

// fs.appendFileSync(path, data)
extern "C" Item js_fs_appendFileSync(Item path_item, Item data_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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
    if (!src) return js_throw_invalid_arg_type("src", "string", src_item);
    if (!dest) return js_throw_invalid_arg_type("dest", "string", dest_item);

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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

    int mode = 0; // F_OK by default
    if (get_type_id(mode_item) == LMD_TYPE_INT) {
        mode = (int)it2i(mode_item);
    }

    uv_fs_t req;
    int r = uv_fs_access(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);
    if (r < 0) {
        return js_throw_system_error(r, "access", path);
    }
    return make_js_undefined();
}

// fs.rmSync(path[, options]) — remove file or directory (optionally recursive)
extern "C" Item js_fs_rmSync(Item path_item, Item options_item) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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

    // Node.js appends XXXXXX to the user-provided prefix for mkdtemp template
    char tpl[1030];
    snprintf(tpl, sizeof(tpl), "%sXXXXXX", prefix);

    uv_fs_t req;
    int r = uv_fs_mkdtemp(NULL, &req, tpl, NULL);
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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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
    if (!target) return js_throw_invalid_arg_type("target", "string", target_item);
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

    uv_fs_t req;
    int r = uv_fs_lstat(NULL, &req, path, NULL);
    if (r < 0) {
        uv_fs_req_cleanup(&req);
        return js_throw_system_error(r, "lstat", path);
    }

    Item obj = make_stats_object(&req.statbuf);
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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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
    if (!path) return js_throw_invalid_arg_type("path", "string", path_item);

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
    if (get_type_id(path_item) != LMD_TYPE_STRING) return js_throw_invalid_arg_type("path", "string", path_item);
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
    if (!p) return js_throw_invalid_arg_type("path", "string", path_item);

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

    Item result = make_stats_object(&req.statbuf);
    uv_fs_req_cleanup(&req);
    return result;
}

// =============================================================================
// Async (callback) wrappers — perform sync then invoke callback
// =============================================================================

// helper: create an error object with code property
static Item make_fs_error(int uv_err, const char* path) {
    const char* msg = uv_strerror(uv_err);
    Item err = js_new_error(make_string_item(msg));
    const char* code = "ERR_FS";
    if (uv_err == UV_ENOENT) code = "ENOENT";
    else if (uv_err == UV_EACCES) code = "EACCES";
    else if (uv_err == UV_EEXIST) code = "EEXIST";
    else if (uv_err == UV_EISDIR) code = "EISDIR";
    else if (uv_err == UV_ENOTDIR) code = "ENOTDIR";
    else if (uv_err == UV_EPERM) code = "EPERM";
    else if (uv_err == UV_EBADF) code = "EBADF";
    js_property_set(err, make_string_item("code"), make_string_item(code));
    if (path) {
        js_property_set(err, make_string_item("path"), make_string_item(path));
    }
    js_property_set(err, make_string_item("errno"), (Item){.item = i2it(uv_err)});
    js_property_set(err, make_string_item("syscall"), make_string_item("access"));
    return err;
}

// fs.access(path[, mode], callback)
static Item js_fs_access_async(Item path_item, Item mode_or_cb, Item callback_item) {
    Item callback = callback_item;
    int mode = 0; // F_OK
    if (get_type_id(mode_or_cb) == LMD_TYPE_FUNC) {
        callback = mode_or_cb;
    } else {
        if (get_type_id(mode_or_cb) == LMD_TYPE_INT) mode = (int)it2i(mode_or_cb);
    }
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) {
        if (get_type_id(callback) == LMD_TYPE_FUNC) {
            Item err = make_fs_error(UV_EINVAL, NULL);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        }
        return make_js_undefined();
    }
    uv_fs_t req;
    int r = uv_fs_access(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (r < 0) {
            Item err = make_fs_error(r, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[1] = {ItemNull};
            js_call_function(callback, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

// fs.stat(path[, options], callback)
static Item js_fs_stat_async(Item path_item, Item opts_or_cb, Item callback_item) {
    Item callback = callback_item;
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
    }
    // Reuse statSync to build the stat result
    Item stat_result = js_fs_statSync(path_item);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (stat_result.item == ITEM_NULL || get_type_id(stat_result) == LMD_TYPE_NULL) {
            char path_buf[1024];
            const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
            Item err = make_fs_error(UV_ENOENT, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[2] = {ItemNull, stat_result};
            js_call_function(callback, ItemNull, args, 2);
        }
    }
    return make_js_undefined();
}

// fs.lstat(path[, options], callback)
static Item js_fs_lstat_async(Item path_item, Item opts_or_cb, Item callback_item) {
    Item callback = callback_item;
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
    }
    Item stat_result = js_fs_lstatSync(path_item);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (stat_result.item == ITEM_NULL || get_type_id(stat_result) == LMD_TYPE_NULL) {
            char path_buf[1024];
            const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
            Item err = make_fs_error(UV_ENOENT, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[2] = {ItemNull, stat_result};
            js_call_function(callback, ItemNull, args, 2);
        }
    }
    return make_js_undefined();
}

// fs.open(path, flags[, mode], callback)
static Item js_fs_open_async(Item path_item, Item flags_item, Item mode_or_cb, Item callback_item) {
    Item callback = callback_item;
    Item mode_item = mode_or_cb;
    if (get_type_id(mode_or_cb) == LMD_TYPE_FUNC) {
        callback = mode_or_cb;
        mode_item = make_js_undefined();
    }
    Item fd = js_fs_openSync(path_item, flags_item, mode_item);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (fd.item == ITEM_NULL || get_type_id(fd) == LMD_TYPE_NULL) {
            char path_buf[1024];
            const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
            Item err = make_fs_error(UV_ENOENT, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[2] = {ItemNull, fd};
            js_call_function(callback, ItemNull, args, 2);
        }
    }
    return make_js_undefined();
}

// fs.close(fd, callback)
static Item js_fs_close_async(Item fd_item, Item callback) {
    js_fs_closeSync(fd_item);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {ItemNull};
        js_call_function(callback, ItemNull, args, 1);
    }
    return make_js_undefined();
}

// fs.chmod(path, mode, callback)
static Item js_fs_chmod_async(Item path_item, Item mode_item, Item callback) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) {
        if (get_type_id(callback) == LMD_TYPE_FUNC) {
            Item err = make_fs_error(UV_EINVAL, NULL);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        }
        return make_js_undefined();
    }
    int mode = 0;
    if (get_type_id(mode_item) == LMD_TYPE_INT) mode = (int)it2i(mode_item);
    uv_fs_t req;
    int r = uv_fs_chmod(NULL, &req, path, mode, NULL);
    uv_fs_req_cleanup(&req);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (r < 0) {
            Item err = make_fs_error(r, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[1] = {ItemNull};
            js_call_function(callback, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

// fs.unlink(path, callback)
static Item js_fs_unlink_async(Item path_item, Item callback) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) {
        if (get_type_id(callback) == LMD_TYPE_FUNC) {
            Item err = make_fs_error(UV_EINVAL, NULL);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        }
        return make_js_undefined();
    }
    uv_fs_t req;
    int r = uv_fs_unlink(NULL, &req, path, NULL);
    uv_fs_req_cleanup(&req);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (r < 0) {
            Item err = make_fs_error(r, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[1] = {ItemNull};
            js_call_function(callback, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

// fs.exists(path, callback) — deprecated but still used in tests
// NOTE: callback signature is (exists) not (err, exists)
static Item js_fs_exists_async(Item path_item, Item callback) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    bool exists = false;
    if (path) {
        uv_fs_t req;
        int r = uv_fs_stat(NULL, &req, path, NULL);
        uv_fs_req_cleanup(&req);
        exists = (r == 0);
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {(Item){.item = b2it(exists)}};
        js_call_function(callback, ItemNull, args, 1);
    }
    return make_js_undefined();
}

// fs.rename(oldPath, newPath, callback)
static Item js_fs_rename_async(Item old_item, Item new_item, Item callback) {
    js_fs_renameSync(old_item, new_item);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = {ItemNull};
        js_call_function(callback, ItemNull, args, 1);
    }
    return make_js_undefined();
}

// fs.readdir(path[, options], callback)
static Item js_fs_readdir_async(Item path_item, Item opts_or_cb, Item callback_item) {
    Item callback = callback_item;
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
    }
    Item result = js_fs_readdirSync(path_item);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (result.item == ITEM_NULL || get_type_id(result) == LMD_TYPE_NULL) {
            char path_buf[1024];
            const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
            Item err = make_fs_error(UV_ENOENT, path);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[2] = {ItemNull, result};
            js_call_function(callback, ItemNull, args, 2);
        }
    }
    return make_js_undefined();
}

// fs.fstat(fd[, options], callback)
static Item js_fs_fstat_async(Item fd_item, Item opts_or_cb, Item callback_item) {
    Item callback = callback_item;
    if (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) {
        callback = opts_or_cb;
    }
    Item result = js_fs_fstatSync(fd_item);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        if (result.item == ITEM_NULL || get_type_id(result) == LMD_TYPE_NULL) {
            Item err = make_fs_error(UV_EBADF, NULL);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        } else {
            Item args[2] = {ItemNull, result};
            js_call_function(callback, ItemNull, args, 2);
        }
    }
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

// ─── fs.promises wrapper functions ─────────────────────────────────────────
// Each wraps the sync version, returning a resolved/rejected Promise
extern Item js_promise_resolve(Item value);
extern Item js_promise_reject(Item reason);
extern int js_check_exception(void);
extern Item js_clear_exception(void);

static Item fs_promise_wrap_result(Item result) {
    if (js_check_exception()) {
        Item err = js_clear_exception();
        return js_promise_reject(err);
    }
    return js_promise_resolve(result);
}

extern "C" Item js_fs_readFile_promise(Item path, Item opts) {
    Item result = js_fs_readFileSync(path, opts);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_writeFile_promise(Item path, Item data) {
    Item result = js_fs_writeFileSync(path, data);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_stat_promise(Item path) {
    Item result = js_fs_statSync(path);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_lstat_promise(Item path) {
    Item result = js_fs_lstatSync(path);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_mkdir_promise(Item path, Item opts) {
    Item result = js_fs_mkdirSync(path, opts);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_access_promise(Item path, Item mode) {
    Item result = js_fs_accessSync(path, mode);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_unlink_promise(Item path) {
    Item result = js_fs_unlinkSync(path);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_rename_promise(Item oldpath, Item newpath) {
    Item result = js_fs_renameSync(oldpath, newpath);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_readdir_promise(Item path) {
    Item result = js_fs_readdirSync(path);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_open_promise(Item path, Item flags, Item mode) {
    Item result = js_fs_openSync(path, flags, mode);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_chmod_promise(Item path, Item mode) {
    Item result = js_fs_chmodSync(path, mode);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_realpath_promise(Item path) {
    Item result = js_fs_realpathSync(path);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_copyFile_promise(Item src, Item dest) {
    Item result = js_fs_copyFileSync(src, dest);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_mkdtemp_promise(Item prefix) {
    Item result = js_fs_mkdtempSync(prefix);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_truncate_promise(Item path, Item len) {
    Item result = js_fs_truncateSync(path, len);
    return fs_promise_wrap_result(result);
}

extern "C" Item js_fs_symlink_promise(Item target, Item path) {
    Item result = js_fs_symlinkSync(target, path);
    return fs_promise_wrap_result(result);
}

// =============================================================================
// Additional async (callback) wrappers — synchronous I/O + callback invocation
// =============================================================================

static Item js_fs_rmdir_async(Item path_item, Item callback) {
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) {
        if (get_type_id(callback) == LMD_TYPE_FUNC) {
            Item err = make_fs_error(UV_EINVAL, NULL);
            Item args[1] = {err};
            js_call_function(callback, ItemNull, args, 1);
        }
        return make_js_undefined();
    }
    uv_fs_t req;
    int r = uv_fs_rmdir(NULL, &req, path, NULL);
    uv_fs_req_cleanup(&req);
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        Item args[1] = { r < 0 ? make_fs_error(r, path) : ItemNull };
        js_call_function(callback, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_copyFile_async(Item src_item, Item dest_item, Item flags_or_cb, Item callback) {
    // copyFile(src, dest, [flags], callback) — flags is optional
    Item cb = callback;
    if (get_type_id(flags_or_cb) == LMD_TYPE_FUNC) {
        cb = flags_or_cb;
    }
    Item result = js_fs_copyFileSync(src_item, dest_item);
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = { get_type_id(result) == LMD_TYPE_NULL ? ItemNull : ItemNull };
        js_call_function(cb, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_realpath_async(Item path_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    Item result = js_fs_realpathSync(path_item);
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        if (get_type_id(result) == LMD_TYPE_STRING) {
            Item args[2] = {ItemNull, result};
            js_call_function(cb, ItemNull, args, 2);
        } else {
            Item err = make_fs_error(UV_ENOENT, NULL);
            Item args[1] = {err};
            js_call_function(cb, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

static Item js_fs_mkdtemp_async(Item prefix_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    Item result = js_fs_mkdtempSync(prefix_item);
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        if (get_type_id(result) == LMD_TYPE_STRING) {
            Item args[2] = {ItemNull, result};
            js_call_function(cb, ItemNull, args, 2);
        } else {
            Item err = make_fs_error(UV_EINVAL, NULL);
            Item args[1] = {err};
            js_call_function(cb, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

static Item js_fs_readlink_async(Item path_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    Item result = js_fs_readlinkSync(path_item);
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        if (get_type_id(result) == LMD_TYPE_STRING) {
            Item args[2] = {ItemNull, result};
            js_call_function(cb, ItemNull, args, 2);
        } else {
            Item err = make_fs_error(UV_EINVAL, NULL);
            Item args[1] = {err};
            js_call_function(cb, ItemNull, args, 1);
        }
    }
    return make_js_undefined();
}

static Item js_fs_symlink_async(Item target_item, Item path_item, Item type_or_cb, Item callback) {
    Item cb = (get_type_id(type_or_cb) == LMD_TYPE_FUNC) ? type_or_cb : callback;
    Item result = js_fs_symlinkSync(target_item, path_item);
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = { ItemNull };
        js_call_function(cb, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_truncate_async(Item path_item, Item len_or_cb, Item callback) {
    Item cb = (get_type_id(len_or_cb) == LMD_TYPE_FUNC) ? len_or_cb : callback;
    Item len = (get_type_id(len_or_cb) == LMD_TYPE_FUNC) ? (Item){.item = i2it(0)} : len_or_cb;
    Item result = js_fs_truncateSync(path_item, len);
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = { ItemNull };
        js_call_function(cb, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_appendFile_async(Item path_item, Item data_item, Item opts_or_cb, Item callback) {
    Item cb = (get_type_id(opts_or_cb) == LMD_TYPE_FUNC) ? opts_or_cb : callback;
    Item result = js_fs_appendFileSync(path_item, data_item);
    if (get_type_id(cb) == LMD_TYPE_FUNC) {
        Item args[1] = { ItemNull };
        js_call_function(cb, ItemNull, args, 1);
    }
    return make_js_undefined();
}

static Item js_fs_fchmod_async(Item fd_item, Item mode_item, Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    int fd = (int)it2i(js_to_number(fd_item));
    int mode = (int)it2i(js_to_number(mode_item));
    uv_fs_t req;
    int r = uv_fs_fchmod(NULL, &req, fd, mode, NULL);
    uv_fs_req_cleanup(&req);
    Item args[1] = { r < 0 ? make_fs_error(r, NULL) : ItemNull };
    js_call_function(callback, ItemNull, args, 1);
    return make_js_undefined();
}

static Item js_fs_fchown_async(Item fd_item, Item uid_item, Item gid_item, Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    int fd = (int)it2i(js_to_number(fd_item));
    int uid = (int)it2i(js_to_number(uid_item));
    int gid = (int)it2i(js_to_number(gid_item));
    uv_fs_t req;
    int r = uv_fs_fchown(NULL, &req, fd, uid, gid, NULL);
    uv_fs_req_cleanup(&req);
    Item args[1] = { r < 0 ? make_fs_error(r, NULL) : ItemNull };
    js_call_function(callback, ItemNull, args, 1);
    return make_js_undefined();
}

static Item js_fs_lchown_async(Item path_item, Item uid_item, Item gid_item, Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) {
        Item err = make_fs_error(UV_EINVAL, NULL);
        Item args[1] = {err};
        js_call_function(callback, ItemNull, args, 1);
        return make_js_undefined();
    }
    int uid = (int)it2i(js_to_number(uid_item));
    int gid = (int)it2i(js_to_number(gid_item));
    uv_fs_t req;
    int r = uv_fs_lchown(NULL, &req, path, uid, gid, NULL);
    uv_fs_req_cleanup(&req);
    Item args[1] = { r < 0 ? make_fs_error(r, path) : ItemNull };
    js_call_function(callback, ItemNull, args, 1);
    return make_js_undefined();
}

static Item js_fs_chown_async(Item path_item, Item uid_item, Item gid_item, Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    char path_buf[1024];
    const char* path = item_to_cstr(path_item, path_buf, sizeof(path_buf));
    if (!path) {
        Item err = make_fs_error(UV_EINVAL, NULL);
        Item args[1] = {err};
        js_call_function(callback, ItemNull, args, 1);
        return make_js_undefined();
    }
    int uid = (int)it2i(js_to_number(uid_item));
    int gid = (int)it2i(js_to_number(gid_item));
    uv_fs_t req;
    int r = uv_fs_chown(NULL, &req, path, uid, gid, NULL);
    uv_fs_req_cleanup(&req);
    Item args[1] = { r < 0 ? make_fs_error(r, path) : ItemNull };
    js_call_function(callback, ItemNull, args, 1);
    return make_js_undefined();
}

static Item js_fs_link_async(Item existing_item, Item new_item, Item callback) {
    if (get_type_id(callback) != LMD_TYPE_FUNC) return make_js_undefined();
    char existing_buf[1024], new_buf[1024];
    const char* existing = item_to_cstr(existing_item, existing_buf, sizeof(existing_buf));
    const char* new_path = item_to_cstr(new_item, new_buf, sizeof(new_buf));
    if (!existing || !new_path) {
        Item err = make_fs_error(UV_EINVAL, NULL);
        Item args[1] = {err};
        js_call_function(callback, ItemNull, args, 1);
        return make_js_undefined();
    }
    uv_fs_t req;
    int r = uv_fs_link(NULL, &req, existing, new_path, NULL);
    uv_fs_req_cleanup(&req);
    Item args[1] = { r < 0 ? make_fs_error(r, existing) : ItemNull };
    js_call_function(callback, ItemNull, args, 1);
    return make_js_undefined();
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
    js_fs_set_method(fs_namespace, "access",          (void*)js_fs_access_async, 3);
    js_fs_set_method(fs_namespace, "stat",            (void*)js_fs_stat_async, 3);
    js_fs_set_method(fs_namespace, "lstat",           (void*)js_fs_lstat_async, 3);
    js_fs_set_method(fs_namespace, "open",            (void*)js_fs_open_async, 4);
    js_fs_set_method(fs_namespace, "close",           (void*)js_fs_close_async, 2);
    js_fs_set_method(fs_namespace, "chmod",           (void*)js_fs_chmod_async, 3);
    js_fs_set_method(fs_namespace, "unlink",          (void*)js_fs_unlink_async, 2);
    js_fs_set_method(fs_namespace, "exists",          (void*)js_fs_exists_async, 2);
    js_fs_set_method(fs_namespace, "rename",          (void*)js_fs_rename_async, 3);
    js_fs_set_method(fs_namespace, "readdir",         (void*)js_fs_readdir_async, 3);
    js_fs_set_method(fs_namespace, "fstat",           (void*)js_fs_fstat_async, 3);
    js_fs_set_method(fs_namespace, "rmdir",           (void*)js_fs_rmdir_async, 2);
    js_fs_set_method(fs_namespace, "copyFile",        (void*)js_fs_copyFile_async, 4);
    js_fs_set_method(fs_namespace, "realpath",        (void*)js_fs_realpath_async, 3);
    js_fs_set_method(fs_namespace, "mkdtemp",         (void*)js_fs_mkdtemp_async, 3);
    js_fs_set_method(fs_namespace, "readlink",        (void*)js_fs_readlink_async, 3);
    js_fs_set_method(fs_namespace, "symlink",         (void*)js_fs_symlink_async, 4);
    js_fs_set_method(fs_namespace, "truncate",        (void*)js_fs_truncate_async, 3);
    js_fs_set_method(fs_namespace, "appendFile",      (void*)js_fs_appendFile_async, 4);
    js_fs_set_method(fs_namespace, "fchmod",          (void*)js_fs_fchmod_async, 3);
    js_fs_set_method(fs_namespace, "fchown",          (void*)js_fs_fchown_async, 4);
    js_fs_set_method(fs_namespace, "lchown",          (void*)js_fs_lchown_async, 4);
    js_fs_set_method(fs_namespace, "chown",           (void*)js_fs_chown_async, 4);
    js_fs_set_method(fs_namespace, "link",            (void*)js_fs_link_async, 3);

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

    // fs.constants — null prototype per Node.js spec
    extern Item js_object_create(Item proto);
    Item constants = js_object_create(ItemNull);
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
    // POSIX file mode constants
    js_property_set(constants, make_string_item("S_IFMT"),  (Item){.item = i2it(S_IFMT)});
    js_property_set(constants, make_string_item("S_IFREG"), (Item){.item = i2it(S_IFREG)});
    js_property_set(constants, make_string_item("S_IFDIR"), (Item){.item = i2it(S_IFDIR)});
    js_property_set(constants, make_string_item("S_IFCHR"), (Item){.item = i2it(S_IFCHR)});
    js_property_set(constants, make_string_item("S_IFBLK"), (Item){.item = i2it(S_IFBLK)});
    js_property_set(constants, make_string_item("S_IFIFO"), (Item){.item = i2it(S_IFIFO)});
    js_property_set(constants, make_string_item("S_IFLNK"), (Item){.item = i2it(S_IFLNK)});
    js_property_set(constants, make_string_item("S_IFSOCK"), (Item){.item = i2it(S_IFSOCK)});
    js_property_set(constants, make_string_item("S_IRUSR"), (Item){.item = i2it(S_IRUSR)});
    js_property_set(constants, make_string_item("S_IWUSR"), (Item){.item = i2it(S_IWUSR)});
    js_property_set(constants, make_string_item("S_IXUSR"), (Item){.item = i2it(S_IXUSR)});
    js_property_set(constants, make_string_item("S_IRGRP"), (Item){.item = i2it(S_IRGRP)});
    js_property_set(constants, make_string_item("S_IWGRP"), (Item){.item = i2it(S_IWGRP)});
    js_property_set(constants, make_string_item("S_IXGRP"), (Item){.item = i2it(S_IXGRP)});
    js_property_set(constants, make_string_item("S_IROTH"), (Item){.item = i2it(S_IROTH)});
    js_property_set(constants, make_string_item("S_IWOTH"), (Item){.item = i2it(S_IWOTH)});
    js_property_set(constants, make_string_item("S_IXOTH"), (Item){.item = i2it(S_IXOTH)});
    // UV_DIRENT_ constants
    js_property_set(constants, make_string_item("UV_DIRENT_UNKNOWN"), (Item){.item = i2it(UV_DIRENT_UNKNOWN)});
    js_property_set(constants, make_string_item("UV_DIRENT_FILE"),    (Item){.item = i2it(UV_DIRENT_FILE)});
    js_property_set(constants, make_string_item("UV_DIRENT_DIR"),     (Item){.item = i2it(UV_DIRENT_DIR)});
    js_property_set(constants, make_string_item("UV_DIRENT_LINK"),    (Item){.item = i2it(UV_DIRENT_LINK)});
    js_property_set(constants, make_string_item("UV_DIRENT_FIFO"),    (Item){.item = i2it(UV_DIRENT_FIFO)});
    js_property_set(constants, make_string_item("UV_DIRENT_SOCKET"),  (Item){.item = i2it(UV_DIRENT_SOCKET)});
    js_property_set(constants, make_string_item("UV_DIRENT_CHAR"),    (Item){.item = i2it(UV_DIRENT_CHAR)});
    js_property_set(constants, make_string_item("UV_DIRENT_BLOCK"),   (Item){.item = i2it(UV_DIRENT_BLOCK)});
    // UV_FS_SYMLINK constants
    js_property_set(constants, make_string_item("UV_FS_SYMLINK_DIR"),      (Item){.item = i2it(UV_FS_SYMLINK_DIR)});
    js_property_set(constants, make_string_item("UV_FS_SYMLINK_JUNCTION"), (Item){.item = i2it(UV_FS_SYMLINK_JUNCTION)});
    // COPYFILE constants
    js_property_set(constants, make_string_item("COPYFILE_EXCL"),         (Item){.item = i2it(UV_FS_COPYFILE_EXCL)});
    js_property_set(constants, make_string_item("COPYFILE_FICLONE"),      (Item){.item = i2it(UV_FS_COPYFILE_FICLONE)});
    js_property_set(constants, make_string_item("COPYFILE_FICLONE_FORCE"),(Item){.item = i2it(UV_FS_COPYFILE_FICLONE_FORCE)});
    js_property_set(fs_namespace, make_string_item("constants"), constants);

    // fs.promises — promise-based API
    {
        extern Item js_promise_resolve(Item value);
        extern Item js_promise_reject(Item reason);

        Item promises = js_new_object();

        // fs.promises.readFile(path, options?) — returns Promise<Buffer|string>
        // We wrap the sync version for simplicity
        auto make_promise_wrapper_1 = [](Item (*sync_fn)(Item)) -> void* {
            // Can't create closures in C, so we'll register each individually
            return nullptr;
        };
        (void)make_promise_wrapper_1;

        // Create a set of promise-returning wrapper functions
        // Each calls the sync version and wraps result in a resolved promise
        // (or rejected promise on error)
        js_fs_set_method(promises, "readFile",    (void*)js_fs_readFile_promise, 2);
        js_fs_set_method(promises, "writeFile",   (void*)js_fs_writeFile_promise, 2);
        js_fs_set_method(promises, "stat",        (void*)js_fs_stat_promise, 1);
        js_fs_set_method(promises, "lstat",       (void*)js_fs_lstat_promise, 1);
        js_fs_set_method(promises, "mkdir",       (void*)js_fs_mkdir_promise, 2);
        js_fs_set_method(promises, "access",      (void*)js_fs_access_promise, 2);
        js_fs_set_method(promises, "unlink",      (void*)js_fs_unlink_promise, 1);
        js_fs_set_method(promises, "rm",          (void*)js_fs_unlink_promise, 1);
        js_fs_set_method(promises, "rename",      (void*)js_fs_rename_promise, 2);
        js_fs_set_method(promises, "readdir",     (void*)js_fs_readdir_promise, 1);
        js_fs_set_method(promises, "open",        (void*)js_fs_open_promise, 3);
        js_fs_set_method(promises, "chmod",       (void*)js_fs_chmod_promise, 2);
        js_fs_set_method(promises, "realpath",    (void*)js_fs_realpath_promise, 1);
        js_fs_set_method(promises, "copyFile",    (void*)js_fs_copyFile_promise, 2);
        js_fs_set_method(promises, "mkdtemp",     (void*)js_fs_mkdtemp_promise, 1);
        js_fs_set_method(promises, "truncate",    (void*)js_fs_truncate_promise, 2);
        js_fs_set_method(promises, "symlink",     (void*)js_fs_symlink_promise, 2);

        // fs.promises.constants === fs.constants
        js_property_set(promises, make_string_item("constants"), constants);

        js_property_set(fs_namespace, make_string_item("promises"), promises);
    }

    // set "default" export to the namespace itself (for `import fs from 'fs'`)
    Item default_key = make_string_item("default");
    js_property_set(fs_namespace, default_key, fs_namespace);

    return fs_namespace;
}

// Reset fs namespace (for re-initialization between runs)
extern "C" void js_fs_reset(void) {
    fs_namespace = (Item){0};
}
