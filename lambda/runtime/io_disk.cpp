// anchored native mechanisms only; scan/reconciliation/repository policy lives in script.
#include "io_file_search.hpp"
#include "io_resource.hpp"
#include "concurrency.h"
#include "lambda-number-runtime.hpp"
#include "re2_wrapper.hpp"
#include "lib/arraylist.h"
#include "lib/file.h"
#include "lib/utf.h"
#include <errno.h>
#include <string.h>

namespace {
struct DiskAnchor {
    FileAnchor* anchor = nullptr;
    char* relative = nullptr;
    ~DiskAnchor() { file_anchor_close(anchor); mem_free(relative); }
};
bool disk_open(Item root, Item relative, DiskAnchor* out, Item* error) {
    if (!io_fs_allow_unbrokered("io.disk", error)) return false;
    if (!is_text_type_id(get_type_id(relative)) || memchr(relative.get_chars(), 0, relative.get_len())) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.disk: relative name must be text without NUL"); return false;
    }
    out->relative = mem_dup_n(relative.get_chars(), relative.get_len(), MEM_CAT_EVAL);
    StrBuf* path = lambda_get_local_path_from_item(root);
    FileAnchorOptions options = {FILE_ANCHOR_FULL_SYNC, io_resource_file_guard, lambda_task_running(), nullptr, nullptr};
    if (path) out->anchor = file_anchor_open(path->str, &options);
    strbuf_free(path);
    if (!out->anchor || !out->relative) {
        *error = io_fs_error(ERR_IO_ERROR, "io.disk: cannot acquire root (%d)", errno); return false;
    }
    return true;
}
void word(uint8_t* target, uint64_t value) {
    for (int i = 7; i >= 0; --i) { target[i] = (uint8_t)value; value >>= 8; }
}
void revision_bytes(const FileRevision& revision, uint8_t* bytes) {
    // explicit fields avoid exposing native struct padding; tokens are local observations.
    memset(bytes, 0, 64);
    word(bytes, revision.device); word(bytes + 8, revision.inode);
    word(bytes + 16, revision.stat.size); word(bytes + 24, revision.stat.modified);
    word(bytes + 32, revision.stat.created); word(bytes + 40, revision.modified_nsec);
    word(bytes + 48, revision.stat.mode);
    bytes[56] = revision.stat.exists; bytes[57] = revision.stat.is_file;
    bytes[58] = revision.stat.is_dir; bytes[59] = revision.stat.is_symlink;
}
bool matches(Item token, const FileRevision& revision) {
    if (get_type_id(token) != LMD_TYPE_BINARY) return false;
    Binary* data = token.get_safe_binary();
    uint8_t bytes[64]; revision_bytes(revision, bytes);
    return data && binary_length(data) == sizeof(bytes) && !memcmp(binary_data(data), bytes, sizeof(bytes));
}
Item disk_row(const char* name, const FileRevision& revision, Item payload = ItemNull) {
    static const char* names[] = {"name", "kind", "size", "identity", "token", "bytes"};
    static const TypeId types[] = {LMD_TYPE_STRING, LMD_TYPE_SYMBOL, LMD_TYPE_INT, LMD_TYPE_BINARY, LMD_TYPE_BINARY, LMD_TYPE_ANY};
    RootFrame roots(6);
    Rooted<Item> data(roots, payload), label(roots, io_fs_make_string(name, strlen(name)));
    Rooted<Item> kind(roots, (Item){.item = y2it(heap_create_symbol(revision.stat.is_dir ? "dir" : "file", revision.stat.is_dir ? 3 : 4))});
    uint8_t token[64]; revision_bytes(revision, token);
    Rooted<Binary*> identity(roots, heap_binary_from_bytes((const char*)token, 16));
    Rooted<Binary*> observed(roots, heap_binary_from_bytes((const char*)token, sizeof(token)));
    Rooted<Map*> result(roots, map_alloc_for_type(runtime_result_shape(names, types, 6), 0));
    if (!result.get() || !identity.get() || !observed.get()) return ItemError;
    Item values[] = {label.get(), kind.get(), (Item){.item = i2it(revision.stat.size)},
        (Item){.item = x2it(identity.get())}, (Item){.item = x2it(observed.get())}, data.get()};
    map_fill_items(result.get(), values, 6);
    return (Item){.map = result.get()};
}
struct DiskEntry { char* name; FileRevision revision; };
bool visit(void* context, const char* name, const FileRevision* revision) {
    if (!utf8_valid(name, strlen(name))) { errno = EILSEQ; return false; }
    DiskEntry* entry = (DiskEntry*)mem_alloc(sizeof(DiskEntry), MEM_CAT_EVAL);
    if (!entry) return false;
    entry->name = mem_strdup(name, MEM_CAT_EVAL); entry->revision = *revision;
    if (entry->name && arraylist_append((ArrayList*)context, entry)) return true;
    mem_free(entry->name); mem_free(entry); return false;
}
bool bound(Item limit, int64_t maximum, int64_t* out) {
    if (get_type_id(limit) != LMD_TYPE_INT) return false;
    // Lambda int is immediate; get_int64() dereferences the distinct wide scalar home.
    *out = it2i(limit); return *out >= 0 && *out <= maximum;
}
}

extern "C" Item pn_io_disk_inspect(Item root, Item relative) {
    DiskAnchor disk; Item error = ItemNull;
    if (!disk_open(root, relative, &disk, &error)) return error;
    FileRevision revision;
    if (!file_anchor_stat(disk.anchor, disk.relative, &revision))
        return errno == ENOENT ? ItemNull : io_fs_error(ERR_IO_ERROR, "io.disk.inspect: metadata failed (%d)", errno);
    return disk_row(disk.relative, revision);
}
extern "C" Item pn_io_disk_resolve(Item root) {
    RootFrame roots(1);
    Rooted<Item> relative(roots, io_fs_make_string("", 0));
    DiskAnchor disk; Item error = ItemNull;
    if (!disk_open(root, relative.get(), &disk, &error)) return error;
    char* path = file_anchor_path(disk.anchor);
    if (!path) return io_fs_error(ERR_IO_ERROR, "io.disk.resolve: cannot resolve root");
    Item result = io_fs_make_string(path, strlen(path)); mem_free(path); return result;
}
extern "C" Item pn_io_disk_entries(Item root, Item relative, Item limit) {
    DiskAnchor disk; Item error = ItemNull; int64_t maximum;
    if (!disk_open(root, relative, &disk, &error)) return error;
    if (!bound(limit, 100000, &maximum)) return io_fs_error(ERR_INVALID_OPERATION, "io.disk.entries: invalid limit");
    FileAnchor* directory = *disk.relative
        ? file_anchor_directory(disk.anchor, disk.relative, false, FILE_ANCHOR_FULL_SYNC) : disk.anchor;
    ArrayList* entries = arraylist_new(16);
    bool valid = directory && entries && file_anchor_visit_strict(directory, (uint32_t)maximum, visit, entries);
    int status = errno;
    if (directory != disk.anchor) file_anchor_close(directory);
    RootFrame roots(2);
    Rooted<Array*> result(roots, array_plain()); Rooted<Item> row(roots, ItemNull);
    if (entries) {
        for (int i = 0; i < entries->length; ++i) {
            DiskEntry* entry = (DiskEntry*)entries->data[i];
            if (valid) {
                row.set(disk_row(entry->name, entry->revision));
                if (get_type_id(row.get()) == LMD_TYPE_ERROR) valid = false;
                else array_push_verbatim(result.get(), row.get());
            }
            mem_free(entry->name); mem_free(entry);
        }
        arraylist_free(entries);
    }
    return valid ? (Item){.array = result.get()} : io_fs_error(ERR_IO_ERROR, "io.disk.entries: incomplete enumeration (%d)", status);
}
extern "C" Item pn_io_disk_capture(Item root, Item relative, Item limit) {
    DiskAnchor disk; Item error = ItemNull; int64_t maximum;
    if (!disk_open(root, relative, &disk, &error)) return error;
    if (!bound(limit, 8 * 1024 * 1024, &maximum)) return io_fs_error(ERR_INVALID_OPERATION, "io.disk.capture: invalid limit");
    char* bytes = nullptr; size_t size = 0; FileRevision revision;
    if (!file_anchor_read_all(disk.anchor, disk.relative, maximum, MEM_CAT_EVAL, &bytes, &size, &revision))
        return io_fs_error(ERR_IO_ERROR, "io.disk.capture: incomplete capture (%d)", errno);
    Binary* binary = size ? heap_binary_from_bytes(bytes, size) : nullptr; mem_free(bytes);
    return size && !binary ? ItemError : disk_row(disk.relative, revision, binary ? (Item){.item = x2it(binary)} : ItemNull);
}
extern "C" Item pn_io_disk_install(Item root, Item relative, Item bytes, Item expected) {
    DiskAnchor disk; Item error = ItemNull;
    if (!disk_open(root, relative, &disk, &error)) return error;
    if (get_type_id(bytes) != LMD_TYPE_BINARY && get_type_id(bytes) != LMD_TYPE_NULL)
        return io_fs_error(ERR_TYPE_MISMATCH, "io.disk.install: expected binary or empty content");
    FileRevision revision; FileRevision* old = nullptr;
    if (get_type_id(expected) != LMD_TYPE_NULL) {
        if (!file_anchor_stat(disk.anchor, disk.relative, &revision) || !matches(expected, revision))
            return io_fs_error(ERR_INVALID_STATE, "io.disk.install: stale observation");
        old = &revision;
    }
    Binary* payload = get_type_id(bytes) == LMD_TYPE_BINARY ? bytes.get_safe_binary() : nullptr;
    int status = file_anchor_write_atomic(disk.anchor, disk.relative, payload ? binary_data(payload) : (const uint8_t*)"",
        payload ? binary_length(payload) : 0, old);
    return status == 0 ? ItemNull : io_fs_error(ERR_IO_ERROR,
        status > 0 ? "io.disk.install: published; durability uncertain" : "io.disk.install: publication failed (%d)", errno);
}
extern "C" Item pn_io_disk_erase(Item root, Item relative, Item expected) {
    DiskAnchor disk; Item error = ItemNull; FileRevision revision;
    if (!disk_open(root, relative, &disk, &error)) return error;
    if (!file_anchor_stat(disk.anchor, disk.relative, &revision) || !matches(expected, revision))
        return io_fs_error(ERR_INVALID_STATE, "io.disk.erase: stale observation");
    return file_anchor_remove(disk.anchor, disk.relative, &revision) ? ItemNull :
        io_fs_error(ERR_IO_ERROR, "io.disk.erase: removal or directory sync failed (%d)", errno);
}
extern "C" Item pn_io_disk_directory(Item root, Item relative) {
    DiskAnchor disk; Item error = ItemNull;
    if (!disk_open(root, relative, &disk, &error)) return error;
    FileAnchor* directory = file_anchor_directory(disk.anchor, disk.relative, true, FILE_ANCHOR_FULL_SYNC);
    if (!directory) return io_fs_error(ERR_IO_ERROR, "io.disk.directory: creation or sync failed (%d)", errno);
    file_anchor_close(directory); return ItemNull;
}
