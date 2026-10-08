#pragma once

#include "../../lib/arraylist.h"
#include "../../lib/byte_storage.h"
#include "../../lib/hashmap.h"
#include "../../lib/strbuf.h"

// S12.4.1v2/S14.3.1v2: this backend owns bytes and an index, never an open file.
struct ZipLimits {
    uint64_t archive_bytes;
    uint64_t index_bytes;
    uint64_t member_bytes;
    uint64_t expanded_bytes;
    uint32_t entries;
    uint32_t name_bytes;
    uint32_t path_depth;
    uint32_t nesting_depth;
};

struct ZipError {
    char message[384];
};

struct ZipBudget {
    RefCount refs;
    ZipLimits limits;
    uint64_t expanded_bytes;
    uint64_t declared_bytes;
};

struct ZipEntry {
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
    ZipError* error;
};

struct ZipArchive {
    RefCount refs;
    ByteStorage* snapshot;
    ArrayList* entries;           // includes the synthetic root at index zero
    HashMap* names;
    ZipBudget* budget;
    uint64_t index_bytes;
    uint64_t central_offset;
    uint64_t decompressions;
    uint32_t depth;
    bool zip64;
};

ZipLimits zip_default_limits();
bool zip_source_expected(const char* name, const void* bytes, size_t length);
ZipArchive* zip_archive_open(const void* bytes, size_t length,
    const ZipLimits* limits, ZipBudget* parent_budget, uint32_t depth,
    ZipError* error);
ZipArchive* zip_archive_retain(ZipArchive* archive);
void zip_archive_release(ZipArchive* archive);
ZipEntry* zip_archive_entry(const ZipArchive* archive, uint32_t index);
bool zip_entry_bytes(ZipArchive* archive, uint32_t index, ByteSpan* bytes,
    ZipError* error);

struct ZipOutputEntry {
    const char* path;
    const uint8_t* bytes;
    size_t length;
    uint32_t mode;
    uint32_t dos_time;
    bool directory;
};

struct ZipWriteOptions {
    uint16_t method;              // 0 Stored, 8 raw DEFLATE
    int compression_level;
    bool force_zip64;
    bool deterministic;
    ZipLimits limits;
};

ZipWriteOptions zip_default_write_options();
bool zip_write_entries(const ArrayList* entries, const ZipWriteOptions* options,
    StrBuf* bytes, ZipError* error);

// Checked length-bearing append shared by the encoder and filesystem adapter.
bool zip_append_bytes(StrBuf* out, const void* bytes, size_t size,
    uint64_t limit, ZipError* error);
