#pragma once
#include "archive.hpp"

using ZipLimits = ArchiveLimits;
using ZipError = ArchiveError;
using ZipBudget = ArchiveBudget;
using ZipEntry = ArchiveEntry;
using ZipArchive = Archive;

inline ZipLimits zip_default_limits() { return archive_default_limits(); }
bool zip_source_expected(const char* name, const void* bytes, size_t length);
ZipArchive* zip_archive_open(const void* bytes, size_t length,
    const ZipLimits* limits, ZipBudget* parent_budget, uint32_t depth,
    ZipError* error);
inline ZipArchive* zip_archive_retain(ZipArchive* archive) { return archive_retain(archive); }
inline void zip_archive_release(ZipArchive* archive) { archive_release(archive); }
inline ZipEntry* zip_archive_entry(const ZipArchive* archive, uint32_t index) {
    return archive_entry(archive, index);
}
inline bool zip_entry_bytes(ZipArchive* archive, uint32_t index, ByteSpan* bytes, ZipError* error) {
    return archive_member_bytes(archive, index, bytes, error);
}

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
