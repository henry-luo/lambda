// anchored native mechanisms only; scan/reconciliation/repository policy lives in script.
#include "io_file_search.hpp"
#include "io_resource.hpp"
#include "concurrency.h"
#include "lambda-number-runtime.hpp"
#include "re2_wrapper.hpp"
#include "lib/arraylist.h"
#include "lib/file.h"
#include "lib/digest.h"
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
struct DiskStream {
    FILE* source = nullptr;
    FileAtomicWriter* writer = nullptr;
    DigestCtx* digest = nullptr;
    char* buffer = nullptr;
    ~DiskStream() {
        if (source) fclose(source);
        file_anchor_write_abort(writer);
        digest_ctx_free(digest);
        mem_free(buffer);
    }
};
bool stream_matches(DiskAnchor* disk, FILE* source, const FileRevision& before) {
    FileRevision held = file_revision_stream(source), named;
    return file_revision_equal(&held, &before) && file_anchor_validate(disk->anchor) &&
        file_anchor_stat(disk->anchor, disk->relative, &named) && file_revision_equal(&named, &before);
}
Item stream_result(Item state, Item source, const uint8_t* hash) {
    RootFrame roots(4);
    Rooted<Item> held_state(roots, state), held_source(roots, source);
    Rooted<Binary*> digest(roots, heap_binary_from_bytes((const char*)hash, 32));
    static const char* names[] = {"source", "state", "hash"};
    static const TypeId types[] = {LMD_TYPE_ANY, LMD_TYPE_ANY, LMD_TYPE_BINARY};
    Rooted<Map*> result(roots, map_alloc_for_type(runtime_result_shape(names, types, 3), 0));
    if (!digest.get() || !result.get()) return ItemError;
    Item values[] = {held_source.get(), held_state.get(), (Item){.item = x2it(digest.get())}};
    map_fill_items(result.get(), values, 3);
    return (Item){.map = result.get()};
}
Item hash_producer(DiskStream* stream, Item producer, Item initial, int64_t size) {
    RootFrame roots(5);
    Rooted<Item> callback(roots, producer), state(roots, initial), part(roots, ItemNull), bytes(roots, ItemNull);
    int64_t written = 0;
    while (true) {
        if (lambda_task_cancel_requested(lambda_task_running()))
            return io_fs_error(ERR_CANCELLED, "io.stream: cancelled");
        part.set(fn_call1((Function*)(uintptr_t)callback.get().item, state.get()));
        if (get_type_id(part.get()) == LMD_TYPE_ERROR) return part.get();
        if (get_type_id(part.get()) != LMD_TYPE_MAP)
            return io_fs_error(ERR_TYPE_MISMATCH, "io.stream: producer must return {bytes, state, done}");
        IoFsOptionMap fields = {"io.stream producer", SYSPROC_IO_DISK_WRITE_STREAM,
            lambda_attr_shape(LMD_TYPE_MAP, part.get().map), lambda_attr_data(LMD_TYPE_MAP, part.get().map)};
        Item done = io_fs_option_get(&fields, "done");
        bytes.set(io_fs_option_get(&fields, "bytes"));
        if (get_type_id(done) != LMD_TYPE_BOOL)
            return io_fs_error(ERR_TYPE_MISMATCH, "io.stream: producer done must be Boolean");
        if (it2b(done)) {
            if (written != size || get_type_id(bytes.get()) != LMD_TYPE_NULL)
                return io_fs_error(ERR_INVALID_STATE, "io.stream: incomplete or nonempty final block");
            state.set(io_fs_option_get(&fields, "state"));
            break;
        }
        if (get_type_id(bytes.get()) != LMD_TYPE_BINARY || bytes.get().get_len() < 1 ||
                bytes.get().get_len() > 1048576 || bytes.get().get_len() > size - written)
            return io_fs_error(ERR_INVALID_STATE, "io.stream: invalid block or length overrun");
        Binary* payload = bytes.get().get_safe_binary();
        if (!digest_update(stream->digest, binary_data(payload), binary_length(payload)) ||
                (stream->writer && !file_anchor_write_chunk(stream->writer, binary_data(payload), binary_length(payload))))
            return io_fs_error(ERR_IO_ERROR, "io.stream: staged write failed (%d)", errno);
        written += (int64_t)binary_length(payload);
        state.set(io_fs_option_get(&fields, "state"));
    }
    uint8_t digest[32];
    if (!digest_finalize(stream->digest, digest, sizeof(digest)))
        return io_fs_error(ERR_IO_ERROR, "io.stream: digest failed");
    return stream_result(state.get(), ItemNull, digest);
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

extern "C" Item pn_io_disk_read_stream(Item root, Item relative, Item options, Item consumer) {
    RootFrame roots(9);
    Rooted<Item> held_root(roots, root), held_relative(roots, relative), held_options(roots, options);
    Rooted<Item> callback(roots, consumer), state(roots, ItemNull), block(roots, ItemNull), outcome(roots, ItemNull);
    DiskAnchor disk; DiskStream stream; Item error = ItemNull;
    if (!disk_open(held_root.get(), held_relative.get(), &disk, &error)) return error;
    IoFsOptionMap opts;
    int64_t block_bytes = 65536, maximum = 268435456;
    if (!io_fs_options_open(held_options.get(), "io.disk.read_stream", SYSPROC_IO_DISK_READ_STREAM, &opts, &error) ||
            !io_fs_option_count(&opts, "block_bytes", &block_bytes, &error) ||
            !io_fs_option_count(&opts, "max_bytes", &maximum, &error)) return error;
    if (block_bytes < 1 || block_bytes > 1048576 || maximum < 0 || maximum > INT64_C(68719476736) ||
            get_type_id(callback.get()) != LMD_TYPE_FUNC)
        return io_fs_error(ERR_INVALID_OPERATION, "io.disk.read_stream: invalid bounds or consumer");
    state.set(io_fs_option_get(&opts, "state"));
    Item token = io_fs_option_get(&opts, "token");
    stream.source = file_anchor_open_read(disk.anchor, disk.relative);
    if (!stream.source) return io_fs_error(ERR_IO_ERROR, "io.disk.read_stream: cannot open source (%d)", errno);
    FileRevision before = file_revision_stream(stream.source);
    if (!before.stat.is_file || before.stat.size < 0 || before.stat.size > maximum ||
            (get_type_id(token) != LMD_TYPE_NULL && !matches(token, before)) ||
            !stream_matches(&disk, stream.source, before))
        return io_fs_error(ERR_INVALID_STATE, "io.disk.read_stream: source changed or exceeds limit");
    stream.digest = digest_ctx_new(DIGEST_SHA256);
    stream.buffer = (char*)mem_alloc((size_t)block_bytes, MEM_CAT_EVAL);
    if (!stream.digest || !stream.buffer) return ItemError;
    int64_t remaining = before.stat.size;
    while (remaining > 0) {
        if (lambda_task_cancel_requested(lambda_task_running()))
            return io_fs_error(ERR_CANCELLED, "io.disk.read_stream: cancelled");
        size_t count = (size_t)(remaining < block_bytes ? remaining : block_bytes);
        if (fread(stream.buffer, 1, count, stream.source) != count || ferror(stream.source) ||
                !digest_update(stream.digest, stream.buffer, count))
            return io_fs_error(ERR_IO_ERROR, "io.disk.read_stream: incomplete read (%d)", errno);
        Binary* bytes = heap_binary_from_bytes(stream.buffer, count);
        if (!bytes) return ItemError;
        block.set((Item){.item = x2it(bytes)});
        outcome.set(fn_call2((Function*)(uintptr_t)callback.get().item, state.get(), block.get()));
        if (get_type_id(outcome.get()) == LMD_TYPE_ERROR) return outcome.get();
        state.set(outcome.get());
        // callbacks can park or alter the filesystem; accept no substituted revision.
        if (!stream_matches(&disk, stream.source, before))
            return io_fs_error(ERR_INVALID_STATE, "io.disk.read_stream: source changed during capture");
        remaining -= (int64_t)count;
    }
    uint8_t digest[32];
    if (!stream_matches(&disk, stream.source, before) || !digest_finalize(stream.digest, digest, sizeof(digest)))
        return io_fs_error(ERR_IO_ERROR, "io.disk.read_stream: final validation failed");
    outcome.set(disk_row(disk.relative, before));
    return stream_result(state.get(), outcome.get(), digest);
}

extern "C" Item pn_io_disk_write_stream(Item root, Item relative, Item options, Item producer) {
    RootFrame roots(9);
    Rooted<Item> held_root(roots, root), held_relative(roots, relative), held_options(roots, options);
    Rooted<Item> callback(roots, producer), state(roots, ItemNull), part(roots, ItemNull), bytes(roots, ItemNull);
    Rooted<Item> expected_hash(roots, ItemNull);
    DiskAnchor disk; DiskStream stream; Item error = ItemNull;
    if (!disk_open(held_root.get(), held_relative.get(), &disk, &error)) return error;
    IoFsOptionMap opts;
    int64_t size = -1;
    if (!io_fs_options_open(held_options.get(), "io.disk.write_stream", SYSPROC_IO_DISK_WRITE_STREAM, &opts, &error) ||
            !io_fs_option_count(&opts, "size", &size, &error)) return error;
    expected_hash.set(io_fs_option_get(&opts, "hash"));
    if (size < 0 || size > INT64_C(68719476736) || get_type_id(callback.get()) != LMD_TYPE_FUNC ||
            get_type_id(expected_hash.get()) != LMD_TYPE_BINARY || expected_hash.get().get_len() != 32)
        return io_fs_error(ERR_INVALID_OPERATION, "io.disk.write_stream: expected size, SHA256 and producer");
    state.set(io_fs_option_get(&opts, "state"));
    Item token = io_fs_option_get(&opts, "expected");
    FileRevision revision; FileRevision* old = nullptr;
    if (get_type_id(token) != LMD_TYPE_NULL) {
        if (!file_anchor_stat(disk.anchor, disk.relative, &revision) || !matches(token, revision))
            return io_fs_error(ERR_INVALID_STATE, "io.disk.write_stream: stale destination");
        old = &revision;
    }
    stream.writer = file_anchor_write_begin(disk.anchor, disk.relative, old);
    stream.digest = digest_ctx_new(DIGEST_SHA256);
    if (!stream.writer || !stream.digest)
        return io_fs_error(ERR_IO_ERROR, "io.disk.write_stream: cannot stage destination (%d)", errno);
    part.set(hash_producer(&stream, callback.get(), state.get(), size));
    if (get_type_id(part.get()) == LMD_TYPE_ERROR) return part.get();
    IoFsOptionMap result = {"io.disk.write_stream", SYSPROC_IO_DISK_WRITE_STREAM,
        lambda_attr_shape(LMD_TYPE_MAP, part.get().map), lambda_attr_data(LMD_TYPE_MAP, part.get().map)};
    bytes.set(io_fs_option_get(&result, "hash"));
    if (memcmp(binary_data(bytes.get().get_safe_binary()), binary_data(expected_hash.get().get_safe_binary()), 32))
        return io_fs_error(ERR_INVALID_STATE, "io.disk.write_stream: content digest mismatch");
    // commit consumes the stage on every result; the scope aborts only unpublished stages.
    FileAtomicWriter* writer = stream.writer; stream.writer = nullptr;
    int status = file_anchor_write_commit(writer);
    if (status != 0) return io_fs_error(ERR_IO_ERROR, status > 0
        ? "io.disk.write_stream: published; durability uncertain" : "io.disk.write_stream: publication failed (%d)", errno);
    return part.get();
}

extern "C" Item pn_io_crypto_hash_stream(Item options, Item producer) {
    RootFrame roots(3);
    Rooted<Item> held_options(roots, options), callback(roots, producer), state(roots, ItemNull);
    IoFsOptionMap opts; Item error = ItemNull;
    int64_t size = -1;
    if (!io_fs_options_open(held_options.get(), "io.crypto.hash_stream", SYSPROC_IO_CRYPTO_HASH_STREAM, &opts, &error) ||
            !io_fs_option_count(&opts, "size", &size, &error)) return error;
    if (size < 0 || size > INT64_C(68719476736) || get_type_id(callback.get()) != LMD_TYPE_FUNC)
        return io_fs_error(ERR_INVALID_OPERATION, "io.crypto.hash_stream: expected size and producer");
    state.set(io_fs_option_get(&opts, "state"));
    DiskStream stream;
    stream.digest = digest_ctx_new(DIGEST_SHA256);
    if (!stream.digest) return ItemError;
    return hash_producer(&stream, callback.get(), state.get(), size);
}
