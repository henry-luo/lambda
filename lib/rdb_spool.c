// Bounded anonymous result storage; the owning task controls its lifetime.
#include "rdb_spool.h"
#include "digest.h"
#include "endian.h"
#include "hex.h"
#include "memtrack.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <unistd.h>

struct RdbSpool {
    FILE* stream;
    uint64_t* offsets;
    uint64_t count, capacity, bytes, max_rows, max_bytes;
    int max_columns;
    bool failed;
};

static bool spool_io(int fd, void* bytes, size_t size, uint64_t offset, bool writing) {
    uint8_t* cursor = (uint8_t*)bytes;
    while (size) {
        size_t chunk = size > SSIZE_MAX ? SSIZE_MAX : size;
        ssize_t n = writing ? pwrite(fd, cursor, chunk, (off_t)offset)
                            : pread(fd, cursor, chunk, (off_t)offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        cursor += n;
        size -= (size_t)n;
        offset += (uint64_t)n;
    }
    return true;
}

RdbSpool* rdb_spool_open(FileAnchor* directory, uint64_t max_bytes,
        uint64_t max_rows, int max_columns) {
    if (!directory || !max_bytes || max_bytes > INT64_MAX || !max_rows ||
            max_rows > SIZE_MAX / sizeof(uint64_t) || max_columns <= 0) {
        errno = EINVAL; return NULL;
    }
    char name[sizeof(FILE_ATOMIC_STAGE_PREFIX) + 32];
    FILE* stream = NULL;
    for (int attempt = 0; attempt < 8; attempt++) {
        uint8_t random[16];
        if (!crypto_random_bytes(random, sizeof(random))) { errno = EIO; return NULL; }
        memcpy(name, FILE_ATOMIC_STAGE_PREFIX, sizeof(FILE_ATOMIC_STAGE_PREFIX) - 1);
        hex_encode(random, sizeof(random), name + sizeof(FILE_ATOMIC_STAGE_PREFIX) - 1);
        stream = file_anchor_create_regular(directory, name);
        if (stream || errno != EEXIST) break;
    }
    if (!stream) return NULL;
    FileRevision revision = file_revision_stream(stream);
    // Unlink before retaining the descriptor, so exit also removes the spool.
    if (!revision.stat.exists || !file_anchor_remove(directory, name, &revision)) {
        int error = errno; fclose(stream); errno = error; return NULL;
    }
    RdbSpool* spool = (RdbSpool*)mem_calloc(1, sizeof(RdbSpool), MEM_CAT_INPUT_OTHER);
    if (!spool) { fclose(stream); errno = ENOMEM; return NULL; }
    spool->stream = stream;
    spool->max_bytes = max_bytes;
    spool->max_rows = max_rows;
    spool->max_columns = max_columns;
    return spool;
}

void rdb_spool_close(RdbSpool* spool) {
    if (!spool) return;
    if (spool->stream) fclose(spool->stream);
    mem_free(spool->offsets);
    mem_free(spool);
}

static bool spool_row_size(const RdbSpool* spool, const RdbSpoolRow* row, uint64_t* out) {
    if (!row || row->count < 0 || row->count > spool->max_columns || (row->count && !row->values)) return false;
    uint64_t size = 8; // record length and column count
    for (int i = 0; i < row->count; i++) {
        const RdbValue* value = &row->values[i];
        uint64_t length = 1;
        if (!value->is_null && value->type != RDB_TYPE_NULL) {
            switch (value->type) {
                case RDB_TYPE_BOOL: length += 1; break;
                case RDB_TYPE_INT: case RDB_TYPE_FLOAT: length += 8; break;
                case RDB_TYPE_STRING: case RDB_TYPE_BLOB:
                    if (value->str_len < 0 || (!value->str_val && value->str_len)) return false;
                    length += 4 + (uint64_t)value->str_len;
                    break;
                default: return false;
            }
        }
        if (length > UINT32_MAX - size) return false;
        size += length;
    }
    *out = size;
    return true;
}

static bool spool_reserve(RdbSpool* spool, uint64_t needed) {
    if (needed <= spool->capacity) return true;
    uint64_t capacity = spool->capacity ? spool->capacity : 8;
    while (capacity < needed) {
        if (capacity > spool->max_rows / 2) { capacity = spool->max_rows; break; }
        capacity *= 2;
    }
    if (capacity > spool->max_rows) capacity = spool->max_rows;
    uint64_t* offsets = (uint64_t*)mem_realloc(spool->offsets, (size_t)capacity * sizeof(uint64_t), MEM_CAT_INPUT_OTHER);
    if (!offsets) return false;
    spool->offsets = offsets;
    spool->capacity = capacity;
    return true;
}

RdbSpoolStatus rdb_spool_append(RdbSpool* spool, const RdbSpoolRow* rows, int count) {
    if (!spool || count < 0 || (count && !rows)) return RDB_SPOOL_INVALID;
    if (spool->failed) return RDB_SPOOL_IO;
    if ((uint64_t)count > spool->max_rows - spool->count) return RDB_SPOOL_LIMIT;
    uint64_t bytes = 0;
    for (int i = 0; i < count; i++) {
        uint64_t size;
        if (!spool_row_size(spool, &rows[i], &size)) return RDB_SPOOL_INVALID;
        if (size > spool->max_bytes - spool->bytes - bytes) return RDB_SPOOL_LIMIT;
        bytes += size;
    }
    if (!bytes) return RDB_SPOOL_OK;
    if (bytes > SIZE_MAX || !spool_reserve(spool, spool->count + (uint64_t)count)) return RDB_SPOOL_MEMORY;
    uint8_t* buffer = (uint8_t*)mem_alloc((size_t)bytes, MEM_CAT_INPUT_OTHER);
    if (!buffer) return RDB_SPOOL_MEMORY;
    size_t at = 0;
    for (int r = 0; r < count; r++) {
        uint64_t row_size;
        (void)spool_row_size(spool, &rows[r], &row_size);
        spool->offsets[spool->count + (uint64_t)r] = spool->bytes + at;
        write_le32(buffer + at, (uint32_t)row_size); at += 4;
        write_le32(buffer + at, (uint32_t)rows[r].count); at += 4;
        for (int c = 0; c < rows[r].count; c++) {
            const RdbValue* value = &rows[r].values[c];
            RdbType type = value->is_null ? RDB_TYPE_NULL : value->type;
            buffer[at++] = (uint8_t)type;
            if (type == RDB_TYPE_BOOL) buffer[at++] = value->bool_val ? 1 : 0;
            else if (type == RDB_TYPE_INT || type == RDB_TYPE_FLOAT) {
                uint64_t bits;
                if (type == RDB_TYPE_INT) memcpy(&bits, &value->int_val, sizeof(bits));
                else memcpy(&bits, &value->float_val, sizeof(bits));
                write_le64(buffer + at, bits); at += 8;
            } else if (type == RDB_TYPE_STRING || type == RDB_TYPE_BLOB) {
                write_le32(buffer + at, (uint32_t)value->str_len); at += 4;
                if (value->str_len) memcpy(buffer + at, value->str_val, (size_t)value->str_len);
                at += (size_t)value->str_len;
            }
        }
    }
    int fd = fileno(spool->stream);
    bool written = spool_io(fd, buffer, (size_t)bytes, spool->bytes, true);
    mem_free(buffer);
    if (!written) {
        // A partially written batch is never indexed or followed by another one.
        (void)ftruncate(fd, (off_t)spool->bytes);
        spool->failed = true;
        return RDB_SPOOL_IO;
    }
    spool->count += (uint64_t)count;
    spool->bytes += bytes;
    return RDB_SPOOL_OK;
}

RdbSpoolStatus rdb_spool_read(RdbSpool* spool, uint64_t first, int count,
        RdbSpoolVisitor visitor, void* context) {
    if (!spool || count < 0 || !visitor || first > spool->count ||
            (uint64_t)count > spool->count - first) return RDB_SPOOL_INVALID;
    int fd = fileno(spool->stream);
    for (int r = 0; r < count; r++) {
        uint64_t index = first + (uint64_t)r;
        uint64_t offset = spool->offsets[index];
        uint64_t end = index + 1 < spool->count ? spool->offsets[index + 1] : spool->bytes;
        if (end < offset || end - offset < 8 || end - offset > UINT32_MAX) return RDB_SPOOL_IO;
        size_t size = (size_t)(end - offset);
        uint8_t* buffer = (uint8_t*)mem_alloc(size, MEM_CAT_INPUT_OTHER);
        if (!buffer) return RDB_SPOOL_MEMORY;
        if (!spool_io(fd, buffer, size, offset, false) || read_le32(buffer) != size ||
                read_le32(buffer + 4) > (uint32_t)spool->max_columns) {
            mem_free(buffer); return RDB_SPOOL_IO;
        }
        int columns = (int)read_le32(buffer + 4);
        if ((size_t)columns > SIZE_MAX / sizeof(RdbValue)) { mem_free(buffer); return RDB_SPOOL_IO; }
        RdbValue* values = columns ? (RdbValue*)mem_calloc((size_t)columns, sizeof(RdbValue), MEM_CAT_INPUT_OTHER) : NULL;
        if (columns && !values) { mem_free(buffer); return RDB_SPOOL_MEMORY; }
        size_t at = 8;
        bool valid = true;
        for (int c = 0; valid && c < columns; c++) {
            if (at >= size) { valid = false; break; }
            RdbValue* value = &values[c];
            value->type = (RdbType)buffer[at++];
            if (value->type == RDB_TYPE_NULL) value->is_null = true;
            else if (value->type == RDB_TYPE_BOOL) {
                if (at >= size || buffer[at] > 1) { valid = false; break; }
                value->bool_val = buffer[at++] != 0;
            } else if (value->type == RDB_TYPE_INT || value->type == RDB_TYPE_FLOAT) {
                if (size - at < 8) { valid = false; break; }
                uint64_t bits = read_le64(buffer + at); at += 8;
                if (value->type == RDB_TYPE_INT) memcpy(&value->int_val, &bits, sizeof(bits));
                else memcpy(&value->float_val, &bits, sizeof(bits));
            } else if (value->type == RDB_TYPE_STRING || value->type == RDB_TYPE_BLOB) {
                if (size - at < 4) { valid = false; break; }
                uint32_t length = read_le32(buffer + at); at += 4;
                if (length > INT_MAX || length > size - at) { valid = false; break; }
                value->str_len = (int)length;
                value->str_val = (const char*)(buffer + at);
                at += length;
            } else valid = false;
        }
        if (at != size) valid = false;
        bool accepted = valid && visitor(context, columns, values);
        mem_free(values);
        mem_free(buffer);
        if (!valid) return RDB_SPOOL_IO;
        if (!accepted) return RDB_SPOOL_CALLBACK;
    }
    return RDB_SPOOL_OK;
}

uint64_t rdb_spool_count(const RdbSpool* spool) { return spool ? spool->count : 0; }
uint64_t rdb_spool_bytes(const RdbSpool* spool) { return spool ? spool->bytes : 0; }

#else

RdbSpool* rdb_spool_open(FileAnchor* directory, uint64_t max_bytes, uint64_t max_rows, int max_columns) {
    (void)directory; (void)max_bytes; (void)max_rows; (void)max_columns; errno = ENOTSUP; return NULL;
}
void rdb_spool_close(RdbSpool* spool) { (void)spool; }
RdbSpoolStatus rdb_spool_append(RdbSpool* spool, const RdbSpoolRow* rows, int count) {
    (void)spool; (void)rows; (void)count; return RDB_SPOOL_IO;
}
RdbSpoolStatus rdb_spool_read(RdbSpool* spool, uint64_t first, int count, RdbSpoolVisitor visitor, void* context) {
    (void)spool; (void)first; (void)count; (void)visitor; (void)context; return RDB_SPOOL_IO;
}
uint64_t rdb_spool_count(const RdbSpool* spool) { (void)spool; return 0; }
uint64_t rdb_spool_bytes(const RdbSpool* spool) { (void)spool; return 0; }
#endif
