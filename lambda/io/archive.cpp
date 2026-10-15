#include "archive.hpp"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

ArchiveLimits archive_default_limits() {
    return {256ULL << 20, 64ULL << 20, 256ULL << 20, 1ULL << 30, 1000000, 4096, 128, 8};
}

bool archive_fail(ArchiveError* error, const char* format, ...) {
    if (error) {
        va_list args;
        va_start(args, format);
        vsnprintf(error->message, sizeof(error->message), format, args);
        va_end(args);
        log_debug("ARCHIVE_FAILURE: %s", error->message);
    }
    return false;
}

static uint64_t archive_name_hash(const void* value, uint64_t a, uint64_t b) {
    const ArchiveEntry* entry = *(ArchiveEntry* const*)value;
    return hashmap_sip(entry->path, strlen(entry->path), a, b);
}


static int archive_name_compare(const void* a, const void* b, void*) {
    return strcmp((*(ArchiveEntry* const*)a)->path, (*(ArchiveEntry* const*)b)->path);
}


ArchiveEntry* archive_entry(const Archive* archive, uint32_t index) {
    return archive && index < (uint32_t)archive->entries->length
        ? (ArchiveEntry*)archive->entries->data[index] : nullptr;
}


Archive* archive_retain(Archive* archive) {
    return archive && ref_count_retain(&archive->refs) ? archive : nullptr;
}


void archive_release(Archive* archive) {
    if (!archive || ref_count_release(&archive->refs) != REF_COUNT_LAST) return;
    if (archive->destroy_backend) archive->destroy_backend(archive);
    if (archive->entries) {
        for (int i = 0; i < archive->entries->length; i++) {
            ArchiveEntry* entry = (ArchiveEntry*)archive->entries->data[i];
            byte_storage_release(entry->payload);
            mem_free(entry->error);
            mem_free(entry->path);
            mem_free(entry);
        }
        arraylist_free(archive->entries);
    }
    if (archive->names) hashmap_free(archive->names);
    byte_storage_release(archive->snapshot);
    if (archive->budget && ref_count_release(&archive->budget->refs) == REF_COUNT_LAST) {
        mem_free(archive->budget);
    }
    mem_free(archive);
}


ArchiveEntry* archive_index_add(Archive* archive, const char* name,
        uint32_t parent, bool directory, ArchiveError* error) {
    size_t bytes = strlen(name) + 1 + sizeof(ArchiveEntry) + sizeof(ArchiveEntry*) * 3;
    const ArchiveLimits& limits = archive->budget->limits;
    if ((uint32_t)archive->entries->length > limits.entries ||
            !archive_range(archive->index_bytes, bytes, limits.index_bytes)) {
        archive_fail(error, "Archive index/entry limit exceeded");
        return nullptr;
    }
    ArchiveEntry* entry = (ArchiveEntry*)mem_calloc(1, sizeof(ArchiveEntry), MEM_CAT_INPUT_OTHER);
    if (!entry) { archive_fail(error, "Archive index allocation failed"); return nullptr; }
    entry->path = mem_strdup(name, MEM_CAT_INPUT_OTHER);
    entry->directory = directory;
    entry->index = (uint32_t)archive->entries->length;
    entry->parent = parent;
    entry->first_child = entry->last_child = entry->next_sibling = ARCHIVE_NO_ENTRY;
    if (!entry->path || !arraylist_append(archive->entries, entry)) {
        mem_free(entry->path);
        mem_free(entry);
        archive_fail(error, "Archive index allocation failed");
        return nullptr;
    }
    hashmap_set(archive->names, &entry);
    if (hashmap_oom(archive->names)) {
        archive_fail(error, "Archive name index allocation failed");
        return nullptr;
    }
    archive->index_bytes += bytes;
    if (parent != ARCHIVE_NO_ENTRY) {
        ArchiveEntry* owner = archive_entry(archive, parent);
        uint32_t index = (uint32_t)archive->entries->length - 1;
        if (owner->last_child != ARCHIVE_NO_ENTRY) {
            archive_entry(archive, owner->last_child)->next_sibling = index;
        } else owner->first_child = index;
        owner->last_child = index;
        owner->child_count++;
    }
    return entry;
}


static ArchiveEntry* archive_find_name(Archive* archive, const char* name) {
    ArchiveEntry key = {};
    key.path = (char*)name;
    ArchiveEntry* key_pointer = &key;
    ArchiveEntry* const* found = (ArchiveEntry* const*)hashmap_get(archive->names, &key_pointer);
    return found ? *found : nullptr;
}


bool archive_normalize_name(StrBuf* name, const ArchiveLimits& limits,
        ArchiveError* error) {
    if (!name->length || name->length > limits.name_bytes || name->str[0] == '/' ||
            (name->length >= 2 && name->str[1] == ':') ||
            memchr(name->str, '\\', name->length) || memchr(name->str, 0, name->length)) {
        return archive_fail(error, "Archive entry name is absolute, invalid, or exceeds its limit");
    }
    size_t source = 0, destination = 0;
    uint32_t depth = 0;
    while (source < name->length) {
        size_t start = source;
        while (source < name->length && name->str[source] != '/') source++;
        size_t length = source - start;
        if (source < name->length) source++;
        if (length == 2 && name->str[start] == '.' && name->str[start + 1] == '.') {
            return archive_fail(error, "Archive entry name contains '..' traversal");
        }
        if (!length || (length == 1 && name->str[start] == '.')) continue;
        if (++depth > limits.path_depth) return archive_fail(error, "Archive path depth limit exceeded");
        if (destination) name->str[destination++] = '/';
        memmove(name->str + destination, name->str + start, length);
        destination += length;
    }
    name->length = destination;
    name->str[destination] = 0;
    return destination != 0 || archive_fail(error, "Archive entry name normalizes to the archive root");
}


ArchiveEntry* archive_index_path(Archive* archive, StrBuf* name,
        bool directory, ArchiveError* error) {
    uint32_t parent = 0;
    for (size_t position = 0; position <= name->length; position++) {
        if (position != name->length && name->str[position] != '/') continue;
        bool is_parent = position != name->length;
        char saved = name->str[position];
        name->str[position] = 0;
        ArchiveEntry* entry = archive_find_name(archive, name->str);
        if (!entry) entry = archive_index_add(archive, name->str, parent, is_parent || directory, error);
        else if (!entry->directory || (!is_parent && (!directory || entry->explicit_directory))) {
            archive_fail(error, "Archive duplicate name or file/directory conflict: %s", name->str);
            entry = nullptr;
        }
        name->str[position] = saved;
        if (!entry) return nullptr;
        if (!is_parent) return entry;
        parent = entry->index;
    }
    return nullptr;
}


Archive* archive_create(const void* bytes, size_t length, ArchiveFormat format,
        const ArchiveLimits* requested_limits, ArchiveBudget* parent_budget,
        uint32_t depth, ArchiveError* error) {
    if (error) error->message[0] = 0;
    ArchiveLimits limits = parent_budget ? parent_budget->limits
        : requested_limits ? *requested_limits : archive_default_limits();
    if ((!bytes && length) || length > limits.archive_bytes || depth > limits.nesting_depth) {
        archive_fail(error, "Archive size/nesting limit exceeded"); return nullptr;
    }
    Archive* archive = (Archive*)mem_calloc(1, sizeof(Archive), MEM_CAT_INPUT_OTHER);
    if (!archive) { archive_fail(error, "Archive allocation failed"); return nullptr; }
    ref_count_init(&archive->refs);
    archive->depth = depth;
    if (parent_budget && ref_count_retain(&parent_budget->refs)) archive->budget = parent_budget;
    else if (!parent_budget) {
        archive->budget = (ArchiveBudget*)mem_calloc(1, sizeof(ArchiveBudget), MEM_CAT_INPUT_OTHER);
        if (archive->budget) {
            ref_count_init(&archive->budget->refs);
            archive->budget->limits = limits;
        }
    }
    archive->snapshot = byte_storage_alloc(length, MEM_CAT_INPUT_OTHER);
    archive->entries = arraylist_new(16);
    archive->names = hashmap_new(sizeof(ArchiveEntry*), 16, 0, 0, archive_name_hash, archive_name_compare, nullptr, nullptr);
    if (!archive->budget || !archive->snapshot || !archive->entries || !archive->names) {
        archive_fail(error, "Archive allocation failed");
        archive_release(archive);
        return nullptr;
    }
    if (length) memcpy(archive->snapshot->data, bytes, length);
    archive->snapshot->flags |= BYTE_STORAGE_FLAG_READ_ONLY;
    archive->format = format;
    return archive;
}

bool archive_declare(Archive* archive, uint64_t bytes, ArchiveError* error) {
    if (!archive_range(archive->budget->declared_bytes, bytes, archive->budget->limits.expanded_bytes))
        return archive_fail(error, "Archive declared expansion limit exceeded");
    archive->budget->declared_bytes += bytes;
    return true;
}

bool archive_charge_expansion(Archive* archive, uint64_t bytes, ArchiveError* error) {
    if (!archive_range(archive->budget->expanded_bytes, bytes, archive->budget->limits.expanded_bytes))
        return archive_fail(error, "Archive aggregate expansion limit exceeded");
    archive->budget->expanded_bytes += bytes;
    return true;
}

bool archive_member_allocate(Archive* archive, ArchiveEntry* entry, ArchiveError* error) {
    if (entry->size >= SIZE_MAX || entry->size > archive->budget->limits.member_bytes)
        return archive_fail(error, "Archive member allocation limit exceeded");
    entry->payload = byte_storage_alloc((size_t)entry->size + 1, MEM_CAT_INPUT_OTHER);
    return entry->payload || archive_fail(error, "Archive member allocation failed");
}

bool archive_member_bytes(Archive* archive, uint32_t index, ByteSpan* bytes, ArchiveError* error) {
    ArchiveEntry* entry = archive_entry(archive, index);
    if (!entry || entry->directory || index == 0)
        return archive_fail(error, "Archive content source is not a member file");
    if (!entry->decoded) {
        // cache errors as well as bytes; retries cannot reset the shared expansion budget.
        entry->decoded = true;
        ArchiveError failure = {};
        bool ok = archive->load_member && archive->load_member(archive, entry, &failure);
        if (ok && entry->payload) {
            entry->payload->data[entry->size] = 0;
            entry->payload->flags |= BYTE_STORAGE_FLAG_READ_ONLY;
        } else {
            entry->error = (ArchiveError*)mem_alloc(sizeof(ArchiveError), MEM_CAT_INPUT_OTHER);
            if (entry->error) *entry->error = failure;
            byte_storage_release(entry->payload);
            entry->payload = nullptr;
        }
    }
    if (!entry->payload) return archive_fail(error, "%s: %s", entry->path,
        entry->error ? entry->error->message : "Archive member decoding failed");
    return byte_span_init(bytes, entry->payload, 0, (size_t)entry->size);
}

#include "zip_archive.hpp"
#include "../../lib/endian.h"
#include "../../lib/str.h"

const char* archive_format_name(ArchiveFormat format) {
    static const char* names[] = {"", "zip", "7z", "tar", "rar", "rar5"};
    return format >= ARCHIVE_UNKNOWN && format <= ARCHIVE_RAR5 ? names[format] : "";
}

ArchiveFormat archive_format_parse(const char* name) {
    if (name) for (int kind = ARCHIVE_ZIP; kind <= ARCHIVE_RAR5; kind++) {
        if (!strcmp(name, archive_format_name((ArchiveFormat)kind))) return (ArchiveFormat)kind;
    }
    return ARCHIVE_UNKNOWN;
}

ArchiveFormat archive_source_format(const char* name, const void* bytes, size_t length) {
    const uint8_t* data = (const uint8_t*)bytes;
    if (data && length >= 6 && !memcmp(data, "7z\xbc\xaf\x27\x1c", 6)) return ARCHIVE_7Z;
    if (data && length >= 8 && !memcmp(data, "Rar!\x1a\x07\x01\x00", 8)) return ARCHIVE_RAR5;
    if (data && length >= 7 && !memcmp(data, "Rar!\x1a\x07\x00", 7)) return ARCHIVE_RAR;
    if (data && ((length >= 2 && data[0] == 0x1f && data[1] == 0x8b) ||
            (length >= 3 && !memcmp(data, "BZh", 3)) ||
            (length >= 6 && !memcmp(data, "\xfd" "7zXZ\x00", 6)) ||
            (length >= 4 && read_le32(data) == 0xfd2fb528))) return ARCHIVE_TAR;
    if (data && length >= 512) {
        // V7 tar has no magic; a complete non-empty first header needs its checksum.
        char checksum[9] = {};
        memcpy(checksum, data + 148, 8);
        char* end = nullptr;
        unsigned long expected = strtoul(checksum, &end, 8);
        unsigned long sum = 0;
        for (size_t i = 0; i < 512; i++) sum += i >= 148 && i < 156 ? ' ' : data[i];
        if (data[0] && end != checksum && expected == sum) return ARCHIVE_TAR;
    }
    if (zip_source_expected(name, bytes, length)) return ARCHIVE_ZIP;
    const char* extension = name ? strrchr(name, '.') : nullptr;
    struct Extension { const char* suffix; ArchiveFormat format; };
    static const Extension extensions[] = {
        {"7z", ARCHIVE_7Z}, {"rar", ARCHIVE_RAR}, {"tar", ARCHIVE_TAR},
        {"tgz", ARCHIVE_TAR}, {"tbz2", ARCHIVE_TAR}, {"txz", ARCHIVE_TAR},
        {"tzst", ARCHIVE_TAR}
    };
    for (const Extension& entry : extensions) {
        if (extension && !str_icmp_cstr(extension + 1, entry.suffix)) return entry.format;
    }
    if (name) {
        size_t size = strlen(name);
        static const char* suffixes[] = {".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst"};
        for (const char* suffix : suffixes) {
            size_t tail = strlen(suffix);
            if (size >= tail && !str_icmp_cstr(name + size - tail, suffix)) return ARCHIVE_TAR;
        }
    }
    return ARCHIVE_UNKNOWN;
}

#ifndef __EMSCRIPTEN__
Archive* archive_native_open(const void* bytes, size_t length, ArchiveFormat format,
    const ArchiveLimits* limits, ArchiveBudget* parent_budget, uint32_t depth, ArchiveError* error);
#endif

Archive* archive_open(const void* bytes, size_t length, ArchiveFormat format,
        const ArchiveLimits* limits, ArchiveBudget* parent_budget, uint32_t depth, ArchiveError* error) {
    if (format == ARCHIVE_UNKNOWN) format = archive_source_format(nullptr, bytes, length);
    if (format == ARCHIVE_ZIP) return zip_archive_open(bytes, length, limits, parent_budget, depth, error);
#ifndef __EMSCRIPTEN__
    if (format != ARCHIVE_UNKNOWN) return archive_native_open(bytes, length, format, limits, parent_budget, depth, error);
#endif
    archive_fail(error, "Archive format is unrecognized or unavailable in this build");
    return nullptr;
}
