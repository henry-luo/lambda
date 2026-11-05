#pragma once

#include "file.h"
#include "rdb_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// The owner retains the unlinked temporary inode; never share it with workers.
typedef struct RdbSpool RdbSpool;
typedef enum {
    RDB_SPOOL_OK = 0,
    RDB_SPOOL_INVALID,
    RDB_SPOOL_LIMIT,
    RDB_SPOOL_IO,
    RDB_SPOOL_MEMORY,
    RDB_SPOOL_CALLBACK
} RdbSpoolStatus;

typedef struct {
    int count;
    const RdbValue* values;
} RdbSpoolRow;
typedef bool (*RdbSpoolVisitor)(void* context, int count, const RdbValue* values);

// The directory is already authorized and remains owned by the caller. The
// limits include the on-disk record headers; append admits an entire batch or
// leaves its previously committed prefix readable.
RdbSpool* rdb_spool_open(FileAnchor* directory, uint64_t max_bytes,
    uint64_t max_rows, int max_columns);
void rdb_spool_close(RdbSpool* spool);
RdbSpoolStatus rdb_spool_append(RdbSpool* spool, const RdbSpoolRow* rows, int count);
RdbSpoolStatus rdb_spool_read(RdbSpool* spool, uint64_t first, int count,
    RdbSpoolVisitor visitor, void* context);
uint64_t rdb_spool_count(const RdbSpool* spool);
uint64_t rdb_spool_bytes(const RdbSpool* spool);

#ifdef __cplusplus
}
#endif
