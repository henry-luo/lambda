/** Internal SQLite transaction primitives for the host-owned RDB connection. */
#ifndef LIB_RDB_SQLITE_TX_H
#define LIB_RDB_SQLITE_TX_H

#include "rdb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RdbSqliteTxn RdbSqliteTxn;
typedef struct RdbSqliteWork RdbSqliteWork;

/* Names and byte values are borrowed until the callback returns. */
typedef bool (*RdbSqliteRowFn)(void* user, int count, const char* const* names,
                               const RdbValue* values);

/* The pool owns the transaction object; finish closes its registered connection. */
RdbSqliteTxn* rdb_sqlite_tx_begin(Pool* pool, const char* path);
RdbSqliteTxn* rdb_sqlite_tx_begin_existing(Pool* pool, const char* path);

/* Execute exactly one statement. Parameters use RdbValue, including byte lengths. */
int rdb_sqlite_tx_execute(RdbSqliteTxn* tx, const char* sql, size_t sql_len,
                          const RdbValue* params, int param_count,
                          RdbSqliteRowFn row_fn, void* user);

/* The owner pins before dispatch, then joins the worker before ending the work.
 * tx and its pool must outlive that join; cancelled is an atomic int owned by
 * the scheduler. The worker must not call the runtime registry or Lambda GC. */
RdbSqliteWork* rdb_sqlite_tx_work_begin(RdbSqliteTxn* tx);
int rdb_sqlite_tx_work_execute(RdbSqliteWork* work, const char* sql, size_t sql_len,
                                const RdbValue* params, int param_count,
                                RdbSqliteRowFn row_fn, void* user, const int* cancelled);
int rdb_sqlite_tx_work_end(RdbSqliteWork* work);

/* A language-level validation or delivery failure also poisons its scope. */
void rdb_sqlite_tx_mark_failed(RdbSqliteTxn* tx);

/* An error or cancellation rolls back; a failed statement also poisons the scope. */
int rdb_sqlite_tx_finish(RdbSqliteTxn* tx, bool unwinding);

/* Outside an open scope, one statement owns its commit or rollback. */
int rdb_sqlite_execute_once(const char* path, const char* sql, size_t sql_len,
                            const RdbValue* params, int param_count,
                            RdbSqliteRowFn row_fn, void* user);

#ifdef __cplusplus
}
#endif
#endif
