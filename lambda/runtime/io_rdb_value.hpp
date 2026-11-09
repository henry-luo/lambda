#pragma once

#include "../lambda-data.hpp"
#include "lib/rdb_abi.h"
#include "lib/rdb_spool.h"

// Copy one native cell into destination-owned Lambda array storage.
bool io_rdb_append_value(Array* destination, const RdbValue* value);

struct IoRdbParams {
    RdbValue* values;
    int count;
    size_t bytes;
};
bool io_rdb_bind_values(Item params, IoRdbParams* out, int max_count, size_t max_bytes);
void io_rdb_params_free(IoRdbParams* params);

// Worker-owned row copies never retain database buffers or Lambda Items.
struct IoRdbRows;
IoRdbRows* io_rdb_rows_new(int max_rows, int max_columns, size_t max_cells, size_t max_bytes);
bool io_rdb_rows_append(IoRdbRows* rows, const RdbValue* values, int count);
int io_rdb_rows_count(const IoRdbRows* rows);
RdbSpoolStatus io_rdb_rows_spool(const IoRdbRows* rows, RdbSpool* spool);
bool io_rdb_rows_materialize(const IoRdbRows* rows, Rooted<Array*>* destination);
void io_rdb_rows_free(IoRdbRows* rows);
