// rdb-drivers Jube module: shared helpers for the PostgreSQL and MySQL drivers.
// The module reaches the host only through RdbHostAPI (RDB3, D7.3.3), so it
// carries small local helpers instead of linking host lib code.
#pragma once

#include "../../../lib/rdb_abi.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// every open passes the same host table; plugins without an open context use it
extern const RdbHostAPI* rdb_mod_host;

void rdb_mod_log(int level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// growable text buffer for SQL and conninfo assembly
typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} RdbModBuf;

void rdb_mod_buf_append(RdbModBuf* b, const char* s, size_t n);
void rdb_mod_buf_puts(RdbModBuf* b, const char* s);
void rdb_mod_buf_printf(RdbModBuf* b, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void rdb_mod_buf_free(RdbModBuf* b);

char* rdb_mod_strdup(const char* s);

// render a bound parameter as text (both wire protocols accept text values);
// returns NULL for SQL NULL, else a malloc'd string
char* rdb_mod_param_text(const RdbParam* param);

// schema arena helpers over RdbHostAPI.meta_*
typedef struct {
    const RdbHostAPI* host;
    void*             meta;
} RdbModMeta;

void*       rdb_mod_meta_calloc(const RdbModMeta* m, size_t count, size_t size);
const char* rdb_mod_meta_dup(const RdbModMeta* m, const char* s);

// "BEFORE"/"AFTER"/"INSTEAD OF" and "INSERT"/"UPDATE"/"DELETE" as arena strings
const char* rdb_mod_meta_word(const RdbModMeta* m, const char* word);

// parse a TLS mode name shared by libpq sslmode and MySQL ssl-mode spellings
bool rdb_mod_parse_tls_mode(const char* name, RdbTlsMode* out);

extern const RdbDriver rdb_pg_driver;
extern const RdbDriver rdb_mysql_driver;

int  rdb_mysql_module_init(void);
void rdb_mysql_module_shutdown(void);
