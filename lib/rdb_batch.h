/**
 * @file rdb_batch.h
 * @brief Internal: host shredding adapter that serves Arrow column batches
 *        for drivers without RDB_CAP_COLUMNAR (RDB7). Callers use
 *        rdb_result_schema() / rdb_fetch_batch() from rdb.h instead.
 */

#ifndef LIB_RDB_BATCH_H
#define LIB_RDB_BATCH_H

#include "rdb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RdbBatchShred RdbBatchShred;

int  rdb_batch_shred_schema(RdbStmt* stmt, RdbBatchShred** state, struct ArrowSchema* out);
int  rdb_batch_shred_fetch(RdbStmt* stmt, RdbBatchShred** state, int64_t max_rows,
                           struct ArrowArray* out);
void rdb_batch_shred_free(RdbBatchShred* state);

#ifdef __cplusplus
}
#endif

#endif /* LIB_RDB_BATCH_H */
