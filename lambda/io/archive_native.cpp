#include "archive.hpp"
#include "../../lib/zip/archive.h"
#include "../../lib/zip/archive_entry.h"
#include "../../lib/archive_codec/codec.h"
#include "../../lib/endian.h"
#include "../../lib/utf.h"
#include "../../lib/log.h"
#include <limits.h>
#include <string.h>

struct NativeArchive {
    Archive* owner;
    ByteStorage* content;
    ArchiveCodecBudget codecs;
    StrBuf* scratch;
    bool captured_expansion;
};

struct NativeOperation {
    NativeArchive* backend;
    ArchiveEntry* entry;
    ArchiveError* error;
};

// recovery actions use only trivial stack locals; owned scratch survives a C fatal bailout.
static bool native_run(NativeArchive* backend, ArchiveCodecAction action, ArchiveEntry* entry, ArchiveError* error) {
    NativeOperation operation = {backend, entry, error};
    bool ok = archive_codec_run(&backend->codecs, action, &operation);
    if (backend->codecs.failure_message) {
        archive_codec_discard(&backend->codecs);
        if (backend->scratch) strbuf_free(backend->scratch);
        backend->scratch = nullptr;
        return archive_fail(error, "Archive reader failed: %s", backend->codecs.failure_message);
    }
    return ok;
}

static bool native_charge(void* context, uint64_t bytes) {
    Archive* owner = (Archive*)context;
    if (archive_charge_expansion(owner, bytes, nullptr)) return true;
    // output already exists: retain the failed block's actual charge across nested readers.
    uint64_t used = owner->budget->expanded_bytes;
    owner->budget->expanded_bytes = bytes > UINT64_MAX - used ? UINT64_MAX : used + bytes;
    return false;
}

static bool native_error(NativeArchive* backend, struct archive* reader, ArchiveError* error) {
    return archive_fail(error, "%s: %s", archive_format_name(backend->owner->format),
        backend->codecs.limited ? "archive index, decoder memory, or expansion limit exceeded" :
        reader && archive_error_string(reader) ? archive_error_string(reader) : "invalid or unsupported archive");
}

static struct archive* native_reader(NativeArchive* backend, ArchiveFormat format, bool raw, int filter,
        ArchiveError* error) {
    struct archive* reader = archive_read_new();
    if (!reader) { native_error(backend, nullptr, error); return nullptr; }
    int status = archive_read_set_charset(reader, "UTF-8", 1);
    if (status == ARCHIVE_OK && raw) status = archive_read_support_format_raw(reader);
    else if (status == ARCHIVE_OK) switch (format) {
        case ARCHIVE_7Z: status = archive_read_support_format_7zip(reader); break;
        case ARCHIVE_TAR: status = archive_read_support_format_tar(reader); break;
        case ARCHIVE_RAR: status = archive_read_support_format_rar(reader); break;
        case ARCHIVE_RAR5: status = archive_read_support_format_rar5(reader); break;
        default: status = ARCHIVE_FATAL; break;
    }
    if (status == ARCHIVE_OK) switch (filter) {
        case ARCHIVE_FILTER_NONE: status = archive_read_support_filter_none(reader); break;
        case ARCHIVE_FILTER_BZIP2: status = archive_read_support_filter_bzip2(reader); break;
        case ARCHIVE_FILTER_XZ: status = archive_read_support_filter_xz(reader); break;
        case ARCHIVE_FILTER_ZSTD: status = archive_read_support_filter_zstd(reader); break;
        default: status = ARCHIVE_FATAL; break;
    }
    if (status == ARCHIVE_OK && !raw && (format == ARCHIVE_TAR || format == ARCHIVE_RAR)) {
        status = archive_read_set_format_option(reader, format == ARCHIVE_TAR ? "tar" : "rar",
            "hdrcharset", format == ARCHIVE_TAR ? "UTF-8" : "CP437");
    }
    if (status == ARCHIVE_OK) status = archive_read_open_memory(reader, backend->content->data, backend->content->capacity);
    if (status != ARCHIVE_OK) {
        native_error(backend, reader, error);
        archive_read_free(reader);
        return nullptr;
    }
    return reader;
}

static bool native_append(StrBuf* out, const void* data, size_t bytes, uint64_t limit, ArchiveError* error) {
    if (!archive_range(out->length, bytes, limit) || bytes >= SIZE_MAX - out->length ||
            !strbuf_ensure_cap(out, out->length + bytes + 1))
        return archive_fail(error, "Archive decoded buffer limit exceeded");
    if (bytes) memcpy(out->str + out->length, data, bytes);
    out->length += bytes;
    out->str[out->length] = 0;
    return true;
}

static ByteStorage* native_capture(StrBuf* data, ArchiveError* error) {
    ByteStorage* storage = byte_storage_alloc(data->length, MEM_CAT_INPUT_OTHER);
    if (!storage) { archive_fail(error, "Archive decoded snapshot allocation failed"); return nullptr; }
    if (data->length) memcpy(storage->data, data->str, data->length);
    storage->flags |= BYTE_STORAGE_FLAG_READ_ONLY;
    return storage;
}

static bool native_read_member(NativeArchive* backend, struct archive* reader, ArchiveEntry* entry,
        ArchiveError* error) {
    if (!archive_member_allocate(backend->owner, entry, error)) return false;
    uint64_t total = 0;
    for (;;) {
        const void* data = nullptr;
        size_t bytes = 0;
        la_int64_t offset = 0;
        int status = archive_read_data_block(reader, &data, &bytes, &offset);
        if (status == ARCHIVE_EOF) break;
        if (!archive_codec_delivered(&backend->codecs, bytes)) return native_error(backend, reader, error);
        if (status != ARCHIVE_OK || offset < 0 || (uint64_t)offset != total ||
                !archive_range(total, bytes, entry->size)) return native_error(backend, reader, error);
        if (bytes) memcpy(entry->payload->data + total, data, bytes);
        total += bytes;
    }
    if (total != entry->size) return archive_fail(error, "Archive member size mismatch: %s", entry->path);
    entry->payload->data[entry->size] = 0;
    entry->payload->flags |= BYTE_STORAGE_FLAG_READ_ONLY;
    return true;
}

static bool native_gzip(NativeArchive* backend, StrBuf* out, ArchiveError* error) {
    const ByteStorage* source = backend->content;
    z_stream stream = {};
    if (lambda_archive_inflate_init(&stream, MAX_WBITS + 16, ZLIB_VERSION, sizeof(stream)) != Z_OK)
        return native_error(backend, nullptr, error);
    size_t consumed = 0;
    bool ok = true;
    int result = Z_OK;
    uint8_t output[32768];
    for (;;) {
        size_t available = source->capacity - consumed;
        uInt input_size = available > UINT_MAX ? UINT_MAX : (uInt)available;
        stream.next_in = source->data + consumed;
        stream.avail_in = input_size;
        stream.next_out = output;
        stream.avail_out = sizeof(output);
        result = lambda_archive_inflate(&stream, Z_NO_FLUSH);
        size_t read = input_size - stream.avail_in;
        size_t written = sizeof(output) - stream.avail_out;
        consumed += read;
        ok = (result == Z_OK || result == Z_STREAM_END) &&
            native_append(out, output, written, backend->owner->budget->limits.expanded_bytes, error);
        if (!ok || (result == Z_OK && !read && !written)) { ok = false; break; }
        if (result == Z_STREAM_END) {
            if (consumed == source->capacity) break;
            // gzip is a sequence of members; zlib verifies each header, CRC and ISIZE.
            if (source->capacity - consumed < 2 || source->data[consumed] != 0x1f ||
                    source->data[consumed + 1] != 0x8b || inflateReset2(&stream, MAX_WBITS + 16) != Z_OK) {
                ok = false; break;
            }
        }
    }
    inflateEnd(&stream);
    return ok || native_error(backend, nullptr, error);
}

static bool native_unwrap_tar(NativeArchive* backend, ArchiveError* error) {
    const uint8_t* bytes = backend->content->data;
    size_t length = backend->content->capacity;
    bool gzip = length >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b;
    int filter = length >= 3 && !memcmp(bytes, "BZh", 3) ? ARCHIVE_FILTER_BZIP2 :
        length >= 6 && !memcmp(bytes, "\xfd" "7zXZ\0", 6) ? ARCHIVE_FILTER_XZ :
        length >= 4 && read_le32(bytes) == 0xfd2fb528 ? ARCHIVE_FILTER_ZSTD : ARCHIVE_FILTER_NONE;
    if (!gzip && filter == ARCHIVE_FILTER_NONE) return true;
    archive_codec_phase(&backend->codecs, false);
    StrBuf* decoded = backend->scratch = strbuf_new();
    if (!decoded) return archive_fail(error, "Archive decoded snapshot allocation failed");
    bool ok = true;
    if (gzip) ok = native_gzip(backend, decoded, error);
    else {
        struct archive* reader = native_reader(backend, ARCHIVE_TAR, true, filter, error);
        struct archive_entry* entry = nullptr;
        ok = reader && archive_read_next_header(reader, &entry) == ARCHIVE_OK;
        while (ok) {
            const void* data = nullptr;
            size_t size = 0;
            la_int64_t offset = 0;
            int status = archive_read_data_block(reader, &data, &size, &offset);
            if (status == ARCHIVE_EOF) break;
            ok = status == ARCHIVE_OK && offset >= 0 && (uint64_t)offset == decoded->length &&
                archive_codec_delivered(&backend->codecs, size) &&
                native_append(decoded, data, size, backend->owner->budget->limits.expanded_bytes, error);
        }
        if (!ok) native_error(backend, reader, error);
        if (reader) archive_read_free(reader);
    }
    ByteStorage* snapshot = ok ? native_capture(decoded, error) : nullptr;
    strbuf_free(decoded);
    backend->scratch = nullptr;
    if (!snapshot) return false;
    byte_storage_release(backend->content);
    backend->content = snapshot;
    backend->captured_expansion = true;
    archive_codec_phase(&backend->codecs, true);
    return true;
}

static bool native_tar_tail(NativeArchive* backend, struct archive* reader, ArchiveError* error) {
    size_t length = backend->content->capacity;
    la_int64_t end = archive_read_header_position(reader);
    if (length % 512 || end < 0 || (uint64_t)end > length || length - (size_t)end < 1024)
        return archive_fail(error, "tar end records are missing or truncated");
    for (size_t i = (size_t)end; i < length; i++) {
        if (backend->content->data[i]) return archive_fail(error, "tar contains data after its end records");
    }
    return true;
}

static bool native_index(NativeArchive* backend, ArchiveError* error) {
    Archive* owner = backend->owner;
    struct archive* reader = native_reader(backend, owner->format, false, ARCHIVE_FILTER_NONE, error);
    if (!reader) return false;
    StrBuf* path = backend->scratch = strbuf_new();
    bool ok = path && archive_index_add(owner, "", ARCHIVE_NO_ENTRY, true, error);
    uint32_t source_index = 0;
    while (ok) {
        struct archive_entry* header = nullptr;
        archive_codec_phase(&backend->codecs, true);
        int status = archive_read_next_header(reader, &header);
        if (status == ARCHIVE_EOF) {
            if (owner->format == ARCHIVE_TAR) ok = native_tar_tail(backend, reader, error);
            break;
        }
        if (status != ARCHIVE_OK || archive_entry_is_encrypted(header) > 0 ||
                archive_read_has_encrypted_entries(reader) > 0) { ok = native_error(backend, reader, error); break; }
        const char* name = archive_entry_pathname_utf8(header);
        uint32_t kind = archive_entry_filetype(header);
        bool directory = kind == AE_IFDIR;
        la_int64_t size = archive_entry_size(header);
        if (!name || !utf8_valid(name, strlen(name)) ||
                (!directory && kind != AE_IFREG) || archive_entry_symlink(header) ||
                archive_entry_hardlink(header) || archive_entry_sparse_count(header) > 0 ||
                size < 0 || (directory && size) || (!directory && !archive_entry_size_is_set(header)) ||
                (uint64_t)size > owner->budget->limits.member_bytes) {
            log_debug("ARCHIVE_ENTRY_REJECTED: name=%s kind=%u size=%lld set=%d sparse=%d",
                name ? name : "(invalid)", kind, (long long)size,
                archive_entry_size_is_set(header), archive_entry_sparse_count(header));
            ok = archive_fail(error, "Archive name, member size, link or special entry is unsupported"); break;
        }
        strbuf_reset(path);
        strbuf_append_str(path, name);
        // tar's conventional root record supplies metadata without inventing a child.
        const char* root_name = name;
        while (root_name[0] == '.' && root_name[1] == '/') root_name += 2;
        bool root = owner->format == ARCHIVE_TAR && directory && (!*root_name || !strcmp(root_name, "."));
        ArchiveEntry* entry = root ? archive_entry(owner, 0) :
            archive_normalize_name(path, owner->budget->limits, error) ? archive_index_path(owner, path, directory, error) : nullptr;
        if (!entry) { ok = false; break; }
        entry->source_index = source_index++;
        entry->explicit_directory = directory;
        entry->size = (uint64_t)size;
        entry->mode = archive_entry_mode(header);
        entry->has_modified = archive_entry_mtime_is_set(header);
        entry->modified_seconds = archive_entry_mtime(header);
        entry->modified_nanoseconds = (uint32_t)archive_entry_mtime_nsec(header);
        if (!archive_declare(owner, entry->size, error)) { ok = false; break; }
        if (owner->format == ARCHIVE_TAR) {
            la_int64_t offset = archive_filter_bytes(reader, 0);
            if (offset < 0 || !archive_range((uint64_t)offset, entry->size, backend->content->capacity)) {
                ok = archive_fail(error, "tar member range is invalid"); break;
            }
            entry->data_offset = (uint64_t)offset;
            uint64_t padding = (512 - entry->size % 512) % 512;
            uint64_t end = entry->data_offset + entry->size;
            if (!archive_range(end, padding, backend->content->capacity)) {
                ok = archive_fail(error, "tar member padding is truncated"); break;
            }
            for (uint64_t i = 0; i < padding; i++) {
                if (backend->content->data[end + i]) {
                    ok = archive_fail(error, "tar member padding is invalid"); break;
                }
            }
            if (!ok) break;
        }
        if (!directory && owner->format == ARCHIVE_RAR5) {
            archive_codec_phase(&backend->codecs, false);
            owner->decompressions++;
            ok = native_read_member(backend, reader, entry, error);
            entry->decoded = true;
        }
    }
    if (path) strbuf_free(path);
    backend->scratch = nullptr;
    archive_read_free(reader);
    return ok;
}

static bool native_load_member_inner(void* context) {
    NativeOperation* operation = (NativeOperation*)context;
    NativeArchive* backend = operation->backend;
    Archive* owner = backend->owner;
    ArchiveEntry* entry = operation->entry;
    ArchiveError* error = operation->error;
    if (owner->format == ARCHIVE_TAR) {
        if ((!backend->captured_expansion && !archive_charge_expansion(owner, entry->size, error)) ||
                !archive_member_allocate(owner, entry, error)) return false;
        if (entry->size) memcpy(entry->payload->data, backend->content->data + entry->data_offset, (size_t)entry->size);
        return true;
    }
    archive_codec_phase(&backend->codecs, true);
    struct archive* reader = native_reader(backend, owner->format, false, ARCHIVE_FILTER_NONE, error);
    if (!reader) return false;
    bool ok = true;
    for (uint32_t index = 0; ok && index <= entry->source_index; index++) {
        struct archive_entry* header = nullptr;
        ok = archive_read_next_header(reader, &header) == ARCHIVE_OK;
        if (ok && index == entry->source_index) {
            archive_codec_phase(&backend->codecs, false);
            owner->decompressions++;
            ok = native_read_member(backend, reader, entry, error);
        }
    }
    if (!ok && (!error || !error->message[0])) native_error(backend, reader, error);
    archive_read_free(reader);
    return ok;
}

static bool native_load_member(Archive* owner, ArchiveEntry* entry, ArchiveError* error) {
    NativeArchive* backend = (NativeArchive*)owner->backend;
    if (backend->codecs.limited) return native_error(backend, nullptr, error);
    return native_run(backend, native_load_member_inner, entry, error);
}

static void native_destroy(Archive* owner) {
    NativeArchive* backend = (NativeArchive*)owner->backend;
    archive_codec_discard(&backend->codecs);
    if (backend->scratch) strbuf_free(backend->scratch);
    byte_storage_release(backend->content);
    mem_free(backend);
}

static bool native_open_inner(void* context) {
    NativeOperation* operation = (NativeOperation*)context;
    NativeArchive* backend = operation->backend;
    ArchiveError* error = operation->error;
    return (backend->owner->format != ARCHIVE_TAR || native_unwrap_tar(backend, error)) &&
        native_index(backend, error);
}

Archive* archive_native_open(const void* bytes, size_t length, ArchiveFormat format,
        const ArchiveLimits* limits, ArchiveBudget* parent_budget, uint32_t depth, ArchiveError* error) {
    ArchiveFormat detected = archive_source_format(nullptr, bytes, length);
    if (format == ARCHIVE_RAR && detected == ARCHIVE_RAR5) format = detected;
    if (format != ARCHIVE_TAR && format != detected) {
        archive_fail(error, "Archive signature does not match the selected format"); return nullptr;
    }
    Archive* owner = archive_create(bytes, length, format, limits, parent_budget, depth, error);
    if (!owner) return nullptr;
    NativeArchive* backend = (NativeArchive*)mem_calloc(1, sizeof(NativeArchive), MEM_CAT_INPUT_OTHER);
    if (!backend) { archive_release(owner); archive_fail(error, "Archive backend allocation failed"); return nullptr; }
    backend->owner = owner;
    backend->content = byte_storage_retain(owner->snapshot);
    backend->codecs.context = owner;
    backend->codecs.charge_expansion = native_charge;
    backend->codecs.index_bytes = &owner->index_bytes;
    backend->codecs.index_limit = owner->budget->limits.index_bytes;
    backend->codecs.decoder_limit = owner->budget->limits.member_bytes > owner->budget->limits.index_bytes
        ? owner->budget->limits.member_bytes : owner->budget->limits.index_bytes;
    backend->codecs.format_output = format == ARCHIVE_7Z || format == ARCHIVE_RAR5;
    backend->codecs.metadata = true;
    owner->backend = backend;
    owner->load_member = native_load_member;
    owner->destroy_backend = native_destroy;
    bool ok = native_run(backend, native_open_inner, nullptr, error);
    if (!ok) { archive_release(owner); return nullptr; }
    log_debug("ARCHIVE_INDEX_READY: format=%s entries=%d bytes=%zu", archive_format_name(format), owner->entries->length - 1, length);
    return owner;
}
