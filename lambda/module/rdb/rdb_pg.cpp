// PostgreSQL driver (libpq) for the rdb-drivers module.
//
// libpq never touches the network: the host opens each upstream connection
// and negotiates TLS (RDB11), and libpq speaks plaintext to the host's private
// socket. Results are read in text format and typed by column OID.
#include "rdb_module.h"
#include <libpq-fe.h>

#include <inttypes.h>

/* ══════════════════════════════════════════════════════════════════════
 * URI → targets and tunnel options
 * ══════════════════════════════════════════════════════════════════════ */

// libpq options the host's bridge replaces or the driver handles itself
static const char* const pg_bridge_keys[] = {
    "host", "hostaddr", "port", "sslmode", "sslrootcert", "sslcert", "sslkey", "sslpassword",
    "sslcrl", "sslcrldir", "sslsni", "sslcompression", "ssl_min_protocol_version",
    "ssl_max_protocol_version", "sslnegotiation", "requiressl", "sslcertmode", "gssencmode",
    "gsslib", "krbsrvname", "gssdelegation", "requirepeer", "target_session_attrs",
    "load_balance_hosts", "connect_timeout",
};

typedef struct {
    PQconninfoOption* options;
    RdbTlsMode tls;
    const char* ca_file;        // NULL: the host CA bundle
    int64_t connect_timeout_ms;
} PgUri;

static const char* pg_option(const PgUri* u, const char* key) {
    for (PQconninfoOption* o = u->options; o && o->keyword; o++) {
        if (strcmp(o->keyword, key) == 0) return o->val && o->val[0] ? o->val : NULL;
    }
    return NULL;
}

static bool pg_parse_uri(const char* uri, PgUri* out) {
    memset(out, 0, sizeof(*out));
    char* err = NULL;
    out->options = PQconninfoParse(uri, &err);
    if (!out->options) {
        rdb_mod_log(RDB_LOG_ERROR, "rdb pg: invalid connection string: %s", err ? err : "?");
        PQfreemem(err);
        return false;
    }
    if (pg_option(out, "service")) {
        rdb_mod_log(RDB_LOG_ERROR, "rdb pg: service files are not supported; name host/port instead");
        return false;
    }
    const char* binding = pg_option(out, "channel_binding");
    if (binding && strcmp(binding, "require") == 0) {
        // channel binding needs the TLS session inside libpq; the host owns it
        rdb_mod_log(RDB_LOG_ERROR, "rdb pg: channel_binding=require is not supported by the host TLS bridge");
        return false;
    }
    const char* mode = pg_option(out, "sslmode");
    out->tls = RDB_TLS_PREFER;      // libpq's default
    if (mode && !rdb_mod_parse_tls_mode(mode, &out->tls)) {
        rdb_mod_log(RDB_LOG_ERROR, "rdb pg: unknown sslmode '%s'", mode);
        return false;
    }
    out->ca_file = pg_option(out, "sslrootcert");
    if (out->ca_file && strcmp(out->ca_file, "system") == 0) out->ca_file = NULL;
    // libpq verifies the chain under sslmode=require once a root cert is named
    if (out->tls == RDB_TLS_REQUIRE && out->ca_file) out->tls = RDB_TLS_VERIFY_CA;
    const char* timeout = pg_option(out, "connect_timeout");
    out->connect_timeout_ms = timeout ? (int64_t)atol(timeout) * 1000 : 0;
    return true;
}

/** the n-th comma-separated item of `list`, or `fallback` */
static void pg_list_item(const char* list, int n, char* out, size_t cap, const char* fallback) {
    snprintf(out, cap, "%s", fallback ? fallback : "");
    if (!list) return;
    const char* p = list;
    for (int i = 0; i < n && p; i++) {
        p = strchr(p, ',');
        if (p) p++;
    }
    if (!p) return;
    size_t len = strcspn(p, ",");
    if (len == 0) return;
    snprintf(out, cap, "%.*s", (int)len, p);
}

static int pg_targets(const PgUri* u, RdbTarget* out, int cap) {
    const char* hosts = pg_option(u, "hostaddr");
    if (!hosts) hosts = pg_option(u, "host");
    if (!hosts) hosts = getenv("PGHOST");
    const char* ports = pg_option(u, "port");
    if (!ports) ports = getenv("PGPORT");
    int count = 1;
    for (const char* p = hosts; p && (p = strchr(p, ',')); p++) count++;
    if (count > cap) count = cap;
    for (int i = 0; i < count; i++) {
        char host[256], port[16];
        pg_list_item(hosts, i, host, sizeof(host), "localhost");
        // a single port applies to every host, as in libpq
        pg_list_item(ports, ports && !strchr(ports, ',') ? 0 : i, port, sizeof(port), "5432");
        memset(&out[i], 0, sizeof(out[i]));
        out[i].port = atoi(port);
        if (host[0] == '/' || host[0] == '@') {
            out[i].kind = RDB_PEER_UNIX_SOCKET;
            snprintf(out[i].path, sizeof(out[i].path), "%s/.s.PGSQL.%d", host, out[i].port);
        } else {
            out[i].kind = RDB_PEER_TCP;
            snprintf(out[i].host, sizeof(out[i].host), "%s", host);
        }
    }
    return count;
}

static int pg_resolve_targets(const char* uri, RdbTarget* out, int cap, int* out_count) {
    PgUri u;
    if (!pg_parse_uri(uri, &u)) {
        PQconninfoFree(u.options);
        return RDB_ERROR;
    }
    *out_count = pg_targets(&u, out, cap);
    PQconninfoFree(u.options);
    return RDB_OK;
}

/** single-quoted libpq value with \ and ' escaped */
static void pg_append_value(RdbModBuf* b, const char* key, const char* value) {
    rdb_mod_buf_printf(b, "%s%s='", b->len ? " " : "", key);
    for (const char* p = value; *p; p++) {
        if (*p == '\\' || *p == '\'') rdb_mod_buf_append(b, "\\", 1);
        rdb_mod_buf_append(b, p, 1);
    }
    rdb_mod_buf_append(b, "'", 1);
}

/** conninfo pointing libpq at the host bridge, keeping every other option */
static char* pg_bridge_conninfo(const PgUri* u, const char* endpoint, int port) {
    RdbModBuf b = { NULL, 0, 0 };
    char port_text[16];
    snprintf(port_text, sizeof(port_text), "%d", port);
    pg_append_value(&b, "host", endpoint);
    pg_append_value(&b, "port", port_text);
    pg_append_value(&b, "sslmode", "disable");
    pg_append_value(&b, "gssencmode", "disable");
    for (PQconninfoOption* o = u->options; o && o->keyword; o++) {
        if (!o->val || !o->val[0]) continue;
        bool bridged = false;
        for (size_t i = 0; i < sizeof(pg_bridge_keys) / sizeof(pg_bridge_keys[0]) && !bridged; i++) {
            bridged = strcmp(o->keyword, pg_bridge_keys[i]) == 0;
        }
        if (!bridged) pg_append_value(&b, o->keyword, o->val);
    }
    return b.data;
}

/* ══════════════════════════════════════════════════════════════════════
 * Connection
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    PGconn* pg;
} PgConn;

static const RdbDialect pg_dialect = { RDB_PLACEHOLDER_DOLLAR, '"' };

/** run a statement with no result rows; false (and logged) on failure */
static bool pg_exec_command(PGconn* pg, const char* sql) {
    PGresult* res = PQexec(pg, sql);
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) rdb_mod_log(RDB_LOG_ERROR, "rdb pg: '%s' failed: %s", sql, PQerrorMessage(pg));
    PQclear(res);
    return ok;
}

static int pg_open(const RdbHostAPI* host, void* open_ctx, const char* uri,
                   const RdbOpenOptions* opts, void** out_conn) {
    rdb_mod_host = host;
    PgUri u;
    if (!pg_parse_uri(uri, &u)) {
        PQconninfoFree(u.options);
        return RDB_ERROR;
    }
    RdbTarget targets[8];
    int count = pg_targets(&u, targets, 8);
    PGconn* pg = NULL;
    int connected = -1;
    for (int i = 0; i < count && connected < 0; i++) {
        RdbTunnelSpec spec;
        memset(&spec, 0, sizeof(spec));
        spec.struct_size = sizeof(spec);
        spec.protocol = RDB_WIRE_POSTGRES;
        spec.tls = targets[i].kind == RDB_PEER_UNIX_SOCKET ? RDB_TLS_DISABLE : u.tls;
        spec.ca_file = u.ca_file;
        // with hostaddr the TLS name is the matching `host` item; else the target host
        char server_name[256] = "";
        if (pg_option(&u, "hostaddr") && pg_option(&u, "host")) {
            pg_list_item(pg_option(&u, "host"), i, server_name, sizeof(server_name), "");
        }
        spec.server_name = server_name[0] ? server_name : NULL;
        spec.connect_timeout_ms = u.connect_timeout_ms;
        char endpoint[1024];
        if (host->tunnel_open(open_ctx, &targets[i], &spec, endpoint, sizeof(endpoint)) != RDB_OK) {
            continue;
        }
        char* conninfo = pg_bridge_conninfo(&u, endpoint, targets[i].port > 0 ? targets[i].port : 5432);
        pg = PQconnectdb(conninfo);
        free(conninfo);
        if (PQstatus(pg) == CONNECTION_OK) {
            connected = i;
        } else {
            rdb_mod_log(RDB_LOG_ERROR, "rdb pg: connection failed: %s", PQerrorMessage(pg));
            PQfinish(pg);
            pg = NULL;
        }
    }
    PQconninfoFree(u.options);
    if (!pg) return RDB_ERROR;
    if (opts->readonly && !pg_exec_command(pg, "SET default_transaction_read_only = on")) {
        PQfinish(pg);
        return RDB_ERROR;
    }

    PgConn* conn = (PgConn*)calloc(1, sizeof(PgConn));
    conn->pg = pg;
    RdbConnInfo info;
    memset(&info, 0, sizeof(info));
    info.peer = targets[connected];
    info.socket_fd = -1;            // the network socket belongs to the host bridge
    info.backend_id = PQbackendPID(pg);
    const char* version = PQparameterStatus(pg, "server_version");
    snprintf(info.server_version, sizeof(info.server_version), "%s", version ? version : "");
    if (host->conn_register(open_ctx, conn, &info) != RDB_OK) {
        PQfinish(pg);
        free(conn);
        return RDB_ERROR;
    }
    *out_conn = conn;
    return RDB_OK;
}

static void pg_close(void* conn) {
    PgConn* c = (PgConn*)conn;
    if (!c) return;
    PQfinish(c->pg);
    free(c);
}

static int pg_ping(void* conn) {
    return PQstatus(((PgConn*)conn)->pg) == CONNECTION_OK ? RDB_OK : RDB_CONN_LOST;
}

static int pg_reset(void* conn) {
    return pg_exec_command(((PgConn*)conn)->pg, "DISCARD ALL") ? RDB_OK : RDB_ERROR;
}

static int pg_cancel(void* conn) {
    // the cancel request is a second local connection the bridge forwards
    PGcancelConn* cancel = PQcancelCreate(((PgConn*)conn)->pg);
    int rc = cancel && PQcancelBlocking(cancel) ? RDB_OK : RDB_ERROR;
    PQcancelFinish(cancel);
    return rc;
}

static int pg_set_timeout(void* conn, int64_t statement_ms) {
    char sql[64];
    snprintf(sql, sizeof(sql), "SET statement_timeout = %" PRId64, statement_ms);
    return pg_exec_command(((PgConn*)conn)->pg, sql) ? RDB_OK : RDB_ERROR;
}

static const char* pg_error_msg(void* conn) {
    return PQerrorMessage(((PgConn*)conn)->pg);
}

/* ══════════════════════════════════════════════════════════════════════
 * Types
 * ══════════════════════════════════════════════════════════════════════ */

static RdbType pg_type_for_oid(Oid oid) {
    switch (oid) {
        case 16:   return RDB_TYPE_BOOL;                        // bool
        case 20: case 21: case 23: case 26: return RDB_TYPE_INT; // int8 int2 int4 oid
        case 700: case 701: return RDB_TYPE_FLOAT;              // float4 float8
        case 1700: return RDB_TYPE_DECIMAL;                     // numeric
        case 114: case 3802: return RDB_TYPE_JSON;              // json jsonb
        case 1082: case 1114: case 1184: return RDB_TYPE_DATETIME; // date timestamp timestamptz
        case 17:   return RDB_TYPE_BLOB;                        // bytea
        default:   return RDB_TYPE_STRING;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * Statements: executed on first use, results buffered
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    PgConn*   conn;
    char*     sql;
    char**    params;
    int       param_count;
    PGresult* res;
    int       row;
} PgStmt;

static int pg_prepare(void* conn, const char* sql, void** out_stmt) {
    PgStmt* s = (PgStmt*)calloc(1, sizeof(PgStmt));
    s->conn = (PgConn*)conn;
    s->sql = rdb_mod_strdup(sql);
    s->row = -1;
    *out_stmt = s;
    return RDB_OK;
}

static int pg_bind_param(void* stmt, int index, const RdbParam* param) {
    PgStmt* s = (PgStmt*)stmt;
    if (index < 1 || s->res) return RDB_ERROR;
    if (index > s->param_count) {
        s->params = (char**)realloc(s->params, (size_t)index * sizeof(char*));
        for (int i = s->param_count; i < index; i++) s->params[i] = NULL;
        s->param_count = index;
    }
    free(s->params[index - 1]);
    s->params[index - 1] = rdb_mod_param_text(param);
    return RDB_OK;
}

static int pg_execute(PgStmt* s) {
    if (s->res) return RDB_OK;
    PGconn* pg = s->conn->pg;
    s->res = PQexecParams(pg, s->sql, s->param_count, NULL, (const char* const*)s->params,
                          NULL, NULL, 0);
    ExecStatusType status = PQresultStatus(s->res);
    if (status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK) return RDB_OK;
    rdb_mod_log(RDB_LOG_ERROR, "rdb pg: query failed: %s", PQerrorMessage(pg));
    return PQstatus(pg) == CONNECTION_OK ? RDB_ERROR : RDB_CONN_LOST;
}

static int pg_step(void* stmt) {
    PgStmt* s = (PgStmt*)stmt;
    int rc = pg_execute(s);
    if (rc != RDB_OK) return rc;
    s->row++;
    return s->row < PQntuples(s->res) ? RDB_ROW : RDB_DONE;
}

static int pg_column_count(void* stmt) {
    PgStmt* s = (PgStmt*)stmt;
    return pg_execute(s) == RDB_OK ? PQnfields(s->res) : 0;
}

static int pg_column_desc(void* stmt, int col, RdbColumnDesc* out) {
    PgStmt* s = (PgStmt*)stmt;
    if (pg_execute(s) != RDB_OK || col < 0 || col >= PQnfields(s->res)) return RDB_ERROR;
    out->name = PQfname(s->res, col);
    out->type = pg_type_for_oid(PQftype(s->res, col));
    return RDB_OK;
}

static RdbValue pg_column_value(void* stmt, int col) {
    PgStmt* s = (PgStmt*)stmt;
    RdbValue v;
    memset(&v, 0, sizeof(v));
    v.type = RDB_TYPE_NULL;
    v.is_null = true;
    if (!s->res || s->row < 0 || s->row >= PQntuples(s->res) || col < 0 ||
            col >= PQnfields(s->res) || PQgetisnull(s->res, s->row, col)) {
        return v;
    }
    const char* text = PQgetvalue(s->res, s->row, col);
    v.is_null = false;
    v.type = pg_type_for_oid(PQftype(s->res, col));
    switch (v.type) {
        case RDB_TYPE_INT:   v.int_val = strtoll(text, NULL, 10); break;
        case RDB_TYPE_FLOAT: v.float_val = strtod(text, NULL); break;
        case RDB_TYPE_BOOL:  v.bool_val = text[0] == 't'; break;
        case RDB_TYPE_BLOB:  v.is_null = true; break;   // binary columns arrive in Phase 2, as for SQLite
        default:
            v.type = RDB_TYPE_STRING;   // decimal/datetime/JSON travel as text (RDB6)
            v.str_val = text;
            v.str_len = PQgetlength(s->res, s->row, col);
            break;
    }
    return v;
}

static void pg_finalize(void* stmt) {
    PgStmt* s = (PgStmt*)stmt;
    if (!s) return;
    PQclear(s->res);
    for (int i = 0; i < s->param_count; i++) free(s->params[i]);
    free(s->params);
    free(s->sql);
    free(s);
}

/* ══════════════════════════════════════════════════════════════════════
 * Schema (pg_catalog, current schema only)
 * ══════════════════════════════════════════════════════════════════════ */

#define PG_SCHEMA_RELS "n.nspname = current_schema() AND c.relkind IN ('r','v','m','p','f')"

static PGresult* pg_catalog_query(PGconn* pg, const char* sql) {
    PGresult* res = PQexec(pg, sql);
    if (PQresultStatus(res) == PGRES_TUPLES_OK) return res;
    rdb_mod_log(RDB_LOG_ERROR, "rdb pg: catalog query failed: %s", PQerrorMessage(pg));
    PQclear(res);
    return NULL;
}

/** rows of `res` whose column 0 equals `table` */
static int pg_rows_for(PGresult* res, const char* table) {
    int n = 0;
    for (int r = 0; res && r < PQntuples(res); r++) {
        if (strcmp(PQgetvalue(res, r, 0), table) == 0) n++;
    }
    return n;
}

static void pg_load_columns(PGresult* res, const RdbModMeta* m, RdbTable* t) {
    t->column_count = pg_rows_for(res, t->name);
    t->columns = (RdbColumn*)rdb_mod_meta_calloc(m, (size_t)t->column_count, sizeof(RdbColumn));
    int i = 0;
    for (int r = 0; res && r < PQntuples(res); r++) {
        if (strcmp(PQgetvalue(res, r, 0), t->name) != 0) continue;
        RdbColumn* c = &t->columns[i++];
        c->name = rdb_mod_meta_dup(m, PQgetvalue(res, r, 1));
        c->type_decl = rdb_mod_meta_dup(m, PQgetvalue(res, r, 2));
        c->type = pg_type_for_oid((Oid)strtoul(PQgetvalue(res, r, 3), NULL, 10));
        c->nullable = PQgetvalue(res, r, 4)[0] != 't';
        c->pk_index = atoi(PQgetvalue(res, r, 5));
        c->primary_key = c->pk_index > 0;
    }
}

static void pg_load_indexes(PGresult* res, const RdbModMeta* m, RdbTable* t) {
    t->index_count = pg_rows_for(res, t->name);
    t->indexes = (RdbIndex*)rdb_mod_meta_calloc(m, (size_t)t->index_count, sizeof(RdbIndex));
    int i = 0;
    for (int r = 0; res && r < PQntuples(res); r++) {
        if (strcmp(PQgetvalue(res, r, 0), t->name) != 0) continue;
        RdbIndex* x = &t->indexes[i++];
        x->name = rdb_mod_meta_dup(m, PQgetvalue(res, r, 1));
        x->unique = PQgetvalue(res, r, 2)[0] == 't';
        // column names arrive joined by the 0x1f unit separator
        const char* cols = PQgetvalue(res, r, 3);
        x->column_count = cols[0] ? 1 : 0;
        for (const char* p = cols; *p; p++) x->column_count += *p == '\x1f';
        x->columns = (const char**)rdb_mod_meta_calloc(m, (size_t)x->column_count, sizeof(char*));
        for (int k = 0; k < x->column_count; k++) {
            size_t len = strcspn(cols, "\x1f");
            char name[256];
            snprintf(name, sizeof(name), "%.*s", (int)len, cols);
            x->columns[k] = rdb_mod_meta_dup(m, name);
            cols += len + (cols[len] ? 1 : 0);
        }
    }
}

static void pg_load_foreign_keys(PGresult* res, const RdbModMeta* m, RdbTable* t) {
    t->fk_count = pg_rows_for(res, t->name);
    t->foreign_keys = (RdbForeignKey*)rdb_mod_meta_calloc(m, (size_t)t->fk_count, sizeof(RdbForeignKey));
    int i = 0;
    for (int r = 0; res && r < PQntuples(res); r++) {
        if (strcmp(PQgetvalue(res, r, 0), t->name) != 0) continue;
        RdbForeignKey* fk = &t->foreign_keys[i++];
        fk->column = rdb_mod_meta_dup(m, PQgetvalue(res, r, 1));
        fk->ref_table = rdb_mod_meta_dup(m, PQgetvalue(res, r, 2));
        fk->ref_column = rdb_mod_meta_dup(m, PQgetvalue(res, r, 3));
    }
}

static void pg_load_triggers(PGresult* res, const RdbModMeta* m, RdbTable* t) {
    t->trigger_count = pg_rows_for(res, t->name);
    t->triggers = (RdbTrigger*)rdb_mod_meta_calloc(m, (size_t)t->trigger_count, sizeof(RdbTrigger));
    int i = 0;
    for (int r = 0; res && r < PQntuples(res); r++) {
        if (strcmp(PQgetvalue(res, r, 0), t->name) != 0) continue;
        RdbTrigger* tg = &t->triggers[i++];
        int type = atoi(PQgetvalue(res, r, 2));
        // pg_trigger.tgtype bits: 2 BEFORE, 4 INSERT, 8 DELETE, 16 UPDATE, 32 TRUNCATE, 64 INSTEAD
        tg->name = rdb_mod_meta_dup(m, PQgetvalue(res, r, 1));
        tg->timing = rdb_mod_meta_word(m, (type & 64) ? "INSTEAD OF" : (type & 2) ? "BEFORE" : "AFTER");
        tg->event = rdb_mod_meta_word(m, (type & 4) ? "INSERT" : (type & 16) ? "UPDATE" :
                                         (type & 8) ? "DELETE" : "TRUNCATE");
    }
}

static void pg_load_functions(PGresult* res, const RdbModMeta* m, RdbSchema* out) {
    out->function_count = res ? PQntuples(res) : 0;
    out->functions = (RdbFunction*)rdb_mod_meta_calloc(m, (size_t)out->function_count, sizeof(RdbFunction));
    for (int r = 0; r < out->function_count; r++) {
        RdbFunction* f = &out->functions[r];
        char kind = PQgetvalue(res, r, 1)[0];
        f->name = rdb_mod_meta_dup(m, PQgetvalue(res, r, 0));
        f->type = rdb_mod_meta_word(m, kind == 'a' ? "aggregate" : kind == 'w' ? "window" : "scalar");
        f->narg = PQgetvalue(res, r, 3)[0] == 't' ? -1 : atoi(PQgetvalue(res, r, 2));
        f->builtin = false;     // only the current schema's own functions are listed
    }
}

static int pg_load_schema(void* conn, const RdbHostAPI* host, void* meta, RdbSchema* out) {
    PGconn* pg = ((PgConn*)conn)->pg;
    RdbModMeta m = { host, meta };
    PGresult* tables = pg_catalog_query(pg,
        "SELECT c.relname, c.relkind FROM pg_class c "
        "JOIN pg_namespace n ON n.oid = c.relnamespace WHERE " PG_SCHEMA_RELS
        " ORDER BY c.relname");
    if (!tables) return RDB_ERROR;
    PGresult* columns = pg_catalog_query(pg,
        "SELECT c.relname, a.attname, format_type(a.atttypid, a.atttypmod), a.atttypid, "
        // int2vector casts to a 0-based array, so array_position would report the
        // first key column as 0 ("not a key"); ORDINALITY is 1-based
        "a.attnotnull, coalesce((SELECT k.ord FROM pg_index i, "
        "unnest(i.indkey::int2[]) WITH ORDINALITY k(attnum, ord) "
        "WHERE i.indrelid = c.oid AND i.indisprimary AND k.attnum = a.attnum), 0) "
        "FROM pg_class c JOIN pg_namespace n ON n.oid = c.relnamespace "
        "JOIN pg_attribute a ON a.attrelid = c.oid WHERE " PG_SCHEMA_RELS
        " AND a.attnum > 0 AND NOT a.attisdropped ORDER BY c.relname, a.attnum");
    PGresult* indexes = pg_catalog_query(pg,
        "SELECT c.relname, ic.relname, i.indisunique, "
        "(SELECT string_agg(a.attname, chr(31) ORDER BY k.ord) "
        " FROM unnest(i.indkey::int2[]) WITH ORDINALITY k(attnum, ord) "
        " JOIN pg_attribute a ON a.attrelid = i.indrelid AND a.attnum = k.attnum) "
        "FROM pg_index i JOIN pg_class c ON c.oid = i.indrelid "
        "JOIN pg_class ic ON ic.oid = i.indexrelid "
        "JOIN pg_namespace n ON n.oid = c.relnamespace WHERE " PG_SCHEMA_RELS
        " ORDER BY c.relname, ic.relname");
    PGresult* fks = pg_catalog_query(pg,
        "SELECT c.relname, a.attname, rc.relname, ra.attname FROM pg_constraint con "
        "JOIN pg_class c ON c.oid = con.conrelid JOIN pg_namespace n ON n.oid = c.relnamespace "
        "JOIN pg_class rc ON rc.oid = con.confrelid "
        "CROSS JOIN LATERAL unnest(con.conkey, con.confkey) AS k(col, refcol) "
        "JOIN pg_attribute a ON a.attrelid = con.conrelid AND a.attnum = k.col "
        "JOIN pg_attribute ra ON ra.attrelid = con.confrelid AND ra.attnum = k.refcol "
        "WHERE con.contype = 'f' AND " PG_SCHEMA_RELS " ORDER BY c.relname, con.conname, a.attnum");
    PGresult* triggers = pg_catalog_query(pg,
        "SELECT c.relname, tg.tgname, tg.tgtype FROM pg_trigger tg "
        "JOIN pg_class c ON c.oid = tg.tgrelid JOIN pg_namespace n ON n.oid = c.relnamespace "
        "WHERE NOT tg.tgisinternal AND " PG_SCHEMA_RELS " ORDER BY c.relname, tg.tgname");
    PGresult* functions = pg_catalog_query(pg,
        "SELECT p.proname, p.prokind, p.pronargs, p.provariadic <> 0 FROM pg_proc p "
        "JOIN pg_namespace n ON n.oid = p.pronamespace "
        "WHERE n.nspname = current_schema() AND p.prokind <> 'p' ORDER BY p.proname, p.pronargs");

    out->table_count = PQntuples(tables);
    out->tables = (RdbTable*)rdb_mod_meta_calloc(&m, (size_t)out->table_count, sizeof(RdbTable));
    for (int t = 0; t < out->table_count; t++) {
        RdbTable* tbl = &out->tables[t];
        tbl->name = rdb_mod_meta_dup(&m, PQgetvalue(tables, t, 0));
        tbl->is_view = PQgetvalue(tables, t, 1)[0] == 'v' || PQgetvalue(tables, t, 1)[0] == 'm';
        pg_load_columns(columns, &m, tbl);
        pg_load_indexes(indexes, &m, tbl);
        pg_load_foreign_keys(fks, &m, tbl);
        pg_load_triggers(triggers, &m, tbl);
    }
    pg_load_functions(functions, &m, out);
    PQclear(tables);
    PQclear(columns);
    PQclear(indexes);
    PQclear(fks);
    PQclear(triggers);
    PQclear(functions);
    return RDB_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * Driver table
 * ══════════════════════════════════════════════════════════════════════ */

const RdbDriver rdb_pg_driver = {
    sizeof(RdbDriver),
    RDB_DRIVER_API_VERSION,
    "postgresql",
    &pg_dialect,
    RDB_CAP_CANCEL | RDB_CAP_STATEMENT_TIMEOUT,
    pg_resolve_targets,
    pg_open,
    pg_close,
    pg_ping,
    pg_reset,
    pg_cancel,
    pg_set_timeout,
    pg_load_schema,
    pg_prepare,
    pg_bind_param,
    pg_step,
    pg_column_count,
    pg_column_desc,
    pg_column_value,
    pg_finalize,
    pg_error_msg,
    NULL,       // result_schema: row driver, served by the host adapter
    NULL,       // fetch_batch
};
