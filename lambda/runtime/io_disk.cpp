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
                bytes.get().get_len() > 8388608 || bytes.get().get_len() > size - written)
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
    if (!bound(limit, 64 * 1024 * 1024, &maximum)) return io_fs_error(ERR_INVALID_OPERATION, "io.disk.capture: invalid limit");
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
    int64_t block_bytes = 65536, maximum = 268435456, offset = 0, length = -1;
    if (!io_fs_options_open(held_options.get(), "io.disk.read_stream", SYSPROC_IO_DISK_READ_STREAM, &opts, &error) ||
            !io_fs_option_count(&opts, "block_bytes", &block_bytes, &error) ||
            !io_fs_option_count(&opts, "max_bytes", &maximum, &error) ||
            !io_fs_option_count(&opts, "offset", &offset, &error) ||
            !io_fs_option_count(&opts, "length", &length, &error)) return error;
    if (block_bytes < 1 || block_bytes > 8388608 || maximum < 0 || maximum > INT64_C(68719476736) ||
            offset < 0 || length < -1 ||
            get_type_id(callback.get()) != LMD_TYPE_FUNC)
        return io_fs_error(ERR_INVALID_OPERATION, "io.disk.read_stream: invalid bounds or consumer");
    state.set(io_fs_option_get(&opts, "state"));
    Item token = io_fs_option_get(&opts, "token");
    stream.source = file_anchor_open_read(disk.anchor, disk.relative);
    if (!stream.source) return io_fs_error(ERR_IO_ERROR, "io.disk.read_stream: cannot open source (%d)", errno);
    FileRevision before = file_revision_stream(stream.source);
    if (!before.stat.is_file || before.stat.size < 0 || offset > before.stat.size ||
            (length >= 0 && length > before.stat.size - offset) ||
            (get_type_id(token) != LMD_TYPE_NULL && !matches(token, before)) ||
            !stream_matches(&disk, stream.source, before))
        return io_fs_error(ERR_INVALID_STATE, "io.disk.read_stream: source changed or exceeds limit");
    // a pack range retains the same held-file and revision guards as a full capture.
    int64_t remaining = length < 0 ? before.stat.size - offset : length;
#ifdef _WIN32
    bool positioned = _fseeki64(stream.source, offset, SEEK_SET) == 0;
#else
    bool positioned = fseeko(stream.source, (off_t)offset, SEEK_SET) == 0;
#endif
    if (remaining > maximum || !positioned)
        return io_fs_error(ERR_INVALID_STATE, "io.disk.read_stream: invalid or oversized range");
    stream.digest = digest_ctx_new(DIGEST_SHA256);
    stream.buffer = (char*)mem_alloc((size_t)block_bytes, MEM_CAT_EVAL);
    if (!stream.digest || !stream.buffer) return ItemError;
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

// scoped scratch files carry payload bytes; SQL callers retain descriptors only.
namespace {
char spool_brand;
VMapVtable spool_vtable = {io_resource_get, io_resource_set, io_resource_count,
    io_resource_keys, io_resource_at, io_resource_at, io_resource_destroy, io_resource_trace};
struct DiskSpool {
    DiskAnchor disk;
    LambdaTask* owner = nullptr;
    FILE* file = nullptr;
    DigestCtx* digest = nullptr;
    int64_t size = 0, limit = 0;
    bool failed = false, anonymous = false;
    char leaf[64] = {};
    void clear() {
        if (file) {
            fflush(file);
            FileRevision revision = file_revision_stream(file);
            fclose(file); file = nullptr;
            if (!anonymous) file_anchor_remove(disk.anchor, leaf, &revision);
        }
        digest_ctx_free(digest); digest = nullptr; size = 0; leaf[0] = 0; anonymous = false;
    }
    ~DiskSpool() { clear(); }
};
DiskSpool* spool_of(Item handle) {
    DiskSpool* s = get_type_id(handle) == LMD_TYPE_VMAP && handle.vmap &&
        handle.vmap->host_type == &spool_brand ? (DiskSpool*)handle.vmap->host_data : nullptr;
    Item denied = ItemNull;
    return s && s->owner == lambda_task_running() && !s->failed &&
        !lambda_task_cancel_requested(s->owner) && io_fs_allow_unbrokered("io.disk.spool", &denied)
        ? s : nullptr;
}
bool spool_seek(FILE* file, int64_t offset) {
#ifdef _WIN32
    return _fseeki64(file, offset, SEEK_SET) == 0;
#else
    return fseeko(file, (off_t)offset, SEEK_SET) == 0;
#endif
}
bool spool_open(DiskSpool* s) {
    if (s->file) return true;
    uint8_t random[16];
    if (!crypto_random_bytes(random, sizeof(random))) return false;
    memcpy(s->leaf, ".lambda-spool-", 14);
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) { s->leaf[14+i*2] = hex[random[i] >> 4]; s->leaf[15+i*2] = hex[random[i] & 15]; }
    s->leaf[46] = 0;
    s->file = file_anchor_create_scratch(s->disk.anchor, s->leaf);
    s->digest = digest_ctx_new(DIGEST_SHA256);
    if (!s->file || !s->digest) return false;
    // Scratch has no published name; final pack publication performs the durable sync.
    s->anonymous = true;
    return true;
}
}
extern "C" Item pn_io_disk_spool(Item root, Item limit, Item action) {
    RootFrame roots(5);
    Rooted<Item> held_root(roots, root), callback(roots, action), handle(roots, ItemNull), result(roots, ItemNull);
    DiskSpool spool; Item error = ItemNull;
    spool.owner = lambda_task_running();
    if (!bound(limit, INT64_C(68719476736), &spool.limit) || !spool.limit || get_type_id(action) != LMD_TYPE_FUNC)
        return io_fs_error(ERR_INVALID_OPERATION, "io.disk.spool: expected positive limit and action");
    Rooted<Item> relative(roots, ItemNull);
    relative.set(io_fs_make_string("", 0));
    if (!disk_open(held_root.get(), relative.get(), &spool.disk, &error)) return error;
    VMap* carrier = io_resource_handle_new(&spool_vtable, &spool_brand, &spool);
    if (!carrier) return ItemError;
    handle.set((Item){.vmap = carrier});
    result.set(fn_call1((Function*)(uintptr_t)callback.get().item, handle.get()));
    // an escaped carrier must fail after this native scope, including error exits.
    handle.get().vmap->host_data = nullptr; handle.get().vmap->data = nullptr;
    return result.get();
}
extern "C" Item pn_io_disk_spool_write(Item handle, Item bytes) {
    DiskSpool* s = spool_of(handle);
    if (!s || get_type_id(bytes) != LMD_TYPE_BINARY || bytes.get_len() < 1 || bytes.get_len() > 16777216)
        return io_fs_error(ERR_INVALID_OPERATION, "io.disk.spool_write: invalid handle or block");
    size_t n = bytes.get_len();
    if ((int64_t)n > s->limit - s->size || !spool_open(s) ||
        !spool_seek(s->file, s->size) || fwrite(binary_data(bytes.get_safe_binary()), 1, n, s->file) != n ||
        !digest_update(s->digest, binary_data(bytes.get_safe_binary()), n)) {
        s->failed = true;
        return io_fs_error(ERR_IO_ERROR, "io.disk.spool_write: bounded write failed");
    }
    int64_t offset = s->size; s->size += (int64_t)n;
    return (Item){.item = i2it(offset)};
}
extern "C" Item pn_io_disk_spool_read(Item handle, Item offset, Item length) {
    DiskSpool* s = spool_of(handle); int64_t at = 0, n = 0;
    if (!s || !bound(offset, INT64_C(68719476736), &at) || !bound(length, 16777216, &n) ||
        !s->file || at > s->size || n > s->size-at)
        return io_fs_error(ERR_INVALID_OPERATION, "io.disk.spool_read: invalid handle or range");
    if (!n) return ItemNull;
    char* buffer = (char*)mem_alloc((size_t)n, MEM_CAT_EVAL);
    if (!buffer) return ItemError;
    if (fflush(s->file) || !spool_seek(s->file, at) || fread(buffer, 1, (size_t)n, s->file) != (size_t)n) {
        s->failed = true; mem_free(buffer);
        return io_fs_error(ERR_IO_ERROR, "io.disk.spool_read: incomplete read");
    }
    Binary* bytes = heap_binary_from_bytes(buffer, n); mem_free(buffer);
    return bytes ? (Item){.item = x2it(bytes)} : ItemError;
}
extern "C" Item pn_io_disk_spool_publish(Item handle, Item suffix) {
    DiskSpool* s = spool_of(handle);
    if (!s || !s->file || get_type_id(suffix) != LMD_TYPE_STRING || suffix.get_len() > 16)
        return io_fs_error(ERR_INVALID_OPERATION, "io.disk.spool_publish: invalid handle or suffix");
    for (uint32_t i = 0; i < suffix.get_len(); ++i) {
        char c = suffix.get_chars()[i];
        if (!(c == '.' || (c >= 'a' && c <= 'z')))
            return io_fs_error(ERR_INVALID_OPERATION, "io.disk.spool_publish: invalid suffix");
    }
    uint8_t hash[32];
    if (fflush(s->file) || !digest_finalize(s->digest, hash, sizeof(hash)) || !spool_seek(s->file, 0)) {
        s->failed = true; return io_fs_error(ERR_IO_ERROR, "io.disk.spool_publish: cannot finalize");
    }
    char name[81]; static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) { name[i*2] = hex[hash[i] >> 4]; name[i*2+1] = hex[hash[i] & 15]; }
    memcpy(name+64, suffix.get_chars(), suffix.get_len()); name[64+suffix.get_len()] = 0;
    DiskStream published;
    published.writer = file_anchor_write_begin(s->disk.anchor, name, nullptr);
    published.digest = digest_ctx_new(DIGEST_SHA256);
    published.buffer = (char*)mem_alloc(65536, MEM_CAT_EVAL);
    if (!published.writer || !published.digest || !published.buffer) { s->failed = true; return ItemError; }
    int64_t left = s->size;
    while (left) {
        size_t n = left < 65536 ? (size_t)left : 65536;
        if (lambda_task_cancel_requested(s->owner) || fread(published.buffer, 1, n, s->file) != n ||
            !digest_update(published.digest, published.buffer, n) ||
            !file_anchor_write_chunk(published.writer, published.buffer, n)) {
            s->failed = true; return io_fs_error(ERR_IO_ERROR, "io.disk.spool_publish: copy failed");
        }
        left -= (int64_t)n;
    }
    uint8_t copied[32];
    if (!digest_finalize(published.digest, copied, sizeof(copied)) || memcmp(hash, copied, 32)) {
        s->failed = true; return io_fs_error(ERR_IO_ERROR, "io.disk.spool_publish: content changed");
    }
    FileAtomicWriter* writer = published.writer; published.writer = nullptr;
    int status = file_anchor_write_commit(writer);
    s->failed = status != 0;
    if (status) return io_fs_error(ERR_IO_ERROR, status > 0 ?
        "io.disk.spool_publish: published; durability uncertain" : "io.disk.spool_publish: failed");
    s->clear();
    return io_fs_make_string(name, 64);
}
