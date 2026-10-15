#pragma once

#include "../../lib/arraylist.h"
#include "../../lib/byte_storage.h"
#include "../../lib/hashmap.h"
#include "../../lib/strbuf.h"

enum ArchiveFormat { ARCHIVE_UNKNOWN, ARCHIVE_ZIP, ARCHIVE_7Z, ARCHIVE_TAR, ARCHIVE_RAR, ARCHIVE_RAR5 };
static constexpr uint32_t ARCHIVE_NO_ENTRY = UINT32_MAX;

// S12.4.1v3/S14.3.1v3: this backend owns bytes and an index, never an open file.
struct ArchiveLimits {
    uint64_t archive_bytes;
    uint64_t index_bytes;
    uint64_t member_bytes;
    uint64_t expanded_bytes;
    uint32_t entries;
    uint32_t name_bytes;
    uint32_t path_depth;
    uint32_t nesting_depth;
};

struct ArchiveError {
    char message[384];
};

struct ArchiveBudget {
    RefCount refs;
    ArchiveLimits limits;
    uint64_t expanded_bytes;
    uint64_t declared_bytes;
};

struct ArchiveEntry {
    int64_t modified_seconds;
    uint32_t modified_nanoseconds;
    uint32_t source_index;
    bool has_modified;
    char* path;
    uint64_t compressed_size;
    uint64_t size;
    uint64_t data_offset;
    uint64_t local_offset;
    uint64_t record_end;
    uint32_t crc;
    uint32_t mode;
    uint32_t index;
    uint32_t parent;
    uint32_t first_child;
    uint32_t last_child;
    uint32_t next_sibling;
    uint32_t child_count;
    uint32_t dos_time;
    uint16_t method;
    uint16_t flags;
    bool directory;
    bool explicit_directory;
    bool decoded;
    ByteStorage* payload;
    ArchiveError* error;
};

struct Archive {
    RefCount refs;
    ByteStorage* snapshot;
    ArrayList* entries;           // includes the synthetic root at index zero
    HashMap* names;
    ArchiveBudget* budget;
    uint64_t index_bytes;
    uint64_t central_offset;
    uint64_t decompressions;
    uint32_t depth;
    bool zip64;
    ArchiveFormat format;
    void* backend;
    bool (*load_member)(Archive*, ArchiveEntry*, ArchiveError*);
    void (*destroy_backend)(Archive*);
};

ArchiveLimits archive_default_limits();
bool archive_fail(ArchiveError* error, const char* format, ...);
inline bool archive_range(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}
const char* archive_format_name(ArchiveFormat format);
ArchiveFormat archive_format_parse(const char* format);
ArchiveFormat archive_source_format(const char* name, const void* bytes, size_t length);
Archive* archive_open(const void* bytes, size_t length, ArchiveFormat format,
    const ArchiveLimits* limits, ArchiveBudget* parent_budget, uint32_t depth,
    ArchiveError* error);
Archive* archive_create(const void* bytes, size_t length, ArchiveFormat format,
    const ArchiveLimits* limits, ArchiveBudget* parent_budget, uint32_t depth,
    ArchiveError* error);
Archive* archive_retain(Archive* archive);
void archive_release(Archive* archive);
ArchiveEntry* archive_entry(const Archive* archive, uint32_t index);
ArchiveEntry* archive_index_add(Archive* archive, const char* name,
    uint32_t parent, bool directory, ArchiveError* error);
ArchiveEntry* archive_index_path(Archive* archive, StrBuf* name,
    bool directory, ArchiveError* error);
bool archive_normalize_name(StrBuf* name, const ArchiveLimits& limits,
    ArchiveError* error);
bool archive_declare(Archive* archive, uint64_t bytes, ArchiveError* error);
bool archive_charge_expansion(Archive* archive, uint64_t bytes, ArchiveError* error);
bool archive_member_allocate(Archive* archive, ArchiveEntry* entry, ArchiveError* error);
bool archive_member_bytes(Archive* archive, uint32_t index, ByteSpan* bytes,
    ArchiveError* error);
