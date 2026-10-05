// MySQL / MariaDB driver (Connector/C 3.3, built without TLS) for the
// rdb-drivers module.
//
// Connector/C connects only to the host bridge's private Unix socket; the host
// owns the network connection and upgrades it to TLS inside the MySQL
// handshake (RDB11). The SHA-2 login plugins need crypto that a TLS-free
// Connector/C lacks, so Lambda-side implementations are registered through
// the public plugin API, hashing through the host (RDB12).
#include "rdb_module.h"
// client_plugin.h pulls Connector/C's internal headers, which expect ma_global.h
// first and still use C's `register` storage class (an error in C++17)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wregister"
#include <ma_global.h>
#include <mysql.h>
#include <mysql/client_plugin.h>
#pragma GCC diagnostic pop

#include <ctype.h>
#include <inttypes.h>

/* ══════════════════════════════════════════════════════════════════════
 * Lambda-side SHA-2 login plugins (RDB12)
 * ══════════════════════════════════════════════════════════════════════ */

#define MYSQL_NONCE_LENGTH 20

// whether the upstream of the connection being opened is guaranteed TLS. The
// local socket always looks "secure" to Connector/C, so the plugins consult
// the host bridge's guarantee instead before sending a cleartext password.
static thread_local bool mysql_upstream_tls = false;

/** nonce from the greeting or the auth-switch request */
static bool mysql_read_nonce(MYSQL_PLUGIN_VIO* vio, unsigned char nonce[MYSQL_NONCE_LENGTH]) {
    unsigned char* packet = NULL;
    int length = vio->read_packet(vio, &packet);
    if (length < MYSQL_NONCE_LENGTH) return false;
    memcpy(nonce, packet, MYSQL_NONCE_LENGTH);
    return true;
}

static int mysql_send_cleartext(MYSQL_PLUGIN_VIO* vio, const char* password, const char* plugin) {
    if (!mysql_upstream_tls) {
        rdb_mod_log(RDB_LOG_ERROR, "rdb mysql: %s full authentication needs TLS upstream; "
                    "use ssl-mode=REQUIRED (or a cached login)", plugin);
        return CR_ERROR;
    }
    return vio->write_packet(vio, (const unsigned char*)password, (int)strlen(password) + 1)
        ? CR_ERROR : CR_OK;
}

/** XOR(SHA256(pw), SHA256(SHA256(SHA256(pw)) || nonce)), MySQL's fast-auth proof */
static bool mysql_sha2_scramble(const char* password, const unsigned char* nonce, unsigned char out[32]) {
    unsigned char stage1[32], stage2[32], salted[32 + MYSQL_NONCE_LENGTH], stage3[32];
    if (!rdb_mod_host->sha256(password, strlen(password), stage1) ||
            !rdb_mod_host->sha256(stage1, sizeof(stage1), stage2)) {
        return false;
    }
    memcpy(salted, stage2, 32);
    memcpy(salted + 32, nonce, MYSQL_NONCE_LENGTH);
    if (!rdb_mod_host->sha256(salted, sizeof(salted), stage3)) return false;
    for (int i = 0; i < 32; i++) out[i] = stage1[i] ^ stage3[i];
    return true;
}

static int caching_sha2_authenticate(MYSQL_PLUGIN_VIO* vio, MYSQL* mysql) {
    unsigned char nonce[MYSQL_NONCE_LENGTH];
    if (!mysql_read_nonce(vio, nonce)) return CR_ERROR;
    const char* password = mysql->passwd ? mysql->passwd : "";
    if (!password[0]) return vio->write_packet(vio, (const unsigned char*)"", 0) ? CR_ERROR : CR_OK;
    unsigned char proof[32];
    if (!mysql_sha2_scramble(password, nonce, proof) || vio->write_packet(vio, proof, 32)) {
        return CR_ERROR;
    }
    unsigned char* reply = NULL;
    int length = vio->read_packet(vio, &reply);
    // 3: the server's cache accepted the proof; 4: it wants the full password
    if (length == 1 && reply[0] == 3) return CR_OK;
    if (length == 1 && reply[0] == 4) return mysql_send_cleartext(vio, password, "caching_sha2_password");
    return CR_ERROR;
}

static int sha256_authenticate(MYSQL_PLUGIN_VIO* vio, MYSQL* mysql) {
    unsigned char nonce[MYSQL_NONCE_LENGTH];
    if (!mysql_read_nonce(vio, nonce)) return CR_ERROR;
    const char* password = mysql->passwd ? mysql->passwd : "";
    if (!password[0]) return vio->write_packet(vio, (const unsigned char*)"", 1) ? CR_ERROR : CR_OK;
    return mysql_send_cleartext(vio, password, "sha256_password");
}

static struct st_mysql_client_plugin_AUTHENTICATION caching_sha2_plugin = {
    MYSQL_CLIENT_AUTHENTICATION_PLUGIN, MYSQL_CLIENT_AUTHENTICATION_PLUGIN_INTERFACE_VERSION,
    "caching_sha2_password", "Lambda", "caching_sha2_password over the host TLS bridge",
    {1, 0, 0}, "MIT", NULL, NULL, NULL, NULL, caching_sha2_authenticate,
};

static struct st_mysql_client_plugin_AUTHENTICATION sha256_plugin = {
    MYSQL_CLIENT_AUTHENTICATION_PLUGIN, MYSQL_CLIENT_AUTHENTICATION_PLUGIN_INTERFACE_VERSION,
    "sha256_password", "Lambda", "sha256_password over the host TLS bridge",
    {1, 0, 0}, "MIT", NULL, NULL, NULL, NULL, sha256_authenticate,
};

int rdb_mysql_module_init(void) {
    if (mysql_library_init(0, NULL, NULL) != 0) return -1;
    MYSQL* scratch = mysql_init(NULL);
    bool ok = scratch &&
        mysql_client_register_plugin(scratch, (struct st_mysql_client_plugin*)&caching_sha2_plugin) &&
        mysql_client_register_plugin(scratch, (struct st_mysql_client_plugin*)&sha256_plugin);
    if (scratch) mysql_close(scratch);
    return ok ? 0 : -1;
}

void rdb_mysql_module_shutdown(void) {
    mysql_library_end();
}

/* ══════════════════════════════════════════════════════════════════════
 * URI → target and options
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    char       user[256];
    char*      password;        // malloc'd; wiped and freed after connecting
    char       host[256];
    int        port;
    char       database[256];
    char       socket[1024];
    char       ca_file[1024];
    RdbTlsMode tls;
    int64_t    connect_timeout_ms;
} MysqlUri;

static int mysql_hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/** percent-decode [s, s+n) into out */
static void mysql_decode(const char* s, size_t n, char* out, size_t cap) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        if (s[i] == '%' && i + 2 < n && mysql_hex(s[i + 1]) >= 0 && mysql_hex(s[i + 2]) >= 0) {
            out[o++] = (char)(mysql_hex(s[i + 1]) * 16 + mysql_hex(s[i + 2]));
            i += 2;
        } else {
            out[o++] = s[i];
        }
    }
    out[o] = '\0';
}

static bool mysql_parse_uri(const char* uri, MysqlUri* u) {
    memset(u, 0, sizeof(*u));
    u->port = 3306;
    u->tls = RDB_TLS_PREFER;    // the MySQL client default (ssl-mode=PREFERRED)
    const char* p = strstr(uri, "://");
    if (!p) {
        rdb_mod_log(RDB_LOG_ERROR, "rdb mysql: expected mysql://user:password@host:port/database");
        return false;
    }
    p += 3;
    const char* query = strchr(p, '?');
    const char* end = query ? query : p + strlen(p);
    const char* at = NULL;
    for (const char* q = p; q < end; q++) {
        if (*q == '@') at = q;
    }
    if (at) {
        const char* colon = (const char*)memchr(p, ':', (size_t)(at - p));
        mysql_decode(p, (size_t)((colon ? colon : at) - p), u->user, sizeof(u->user));
        if (colon) {
            size_t n = (size_t)(at - colon - 1);
            u->password = (char*)calloc(1, n + 1);
            mysql_decode(colon + 1, n, u->password, n + 1);
        }
        p = at + 1;
    }
    const char* slash = (const char*)memchr(p, '/', (size_t)(end - p));
    const char* host_end = slash ? slash : end;
    const char* port = (const char*)memchr(p, ':', (size_t)(host_end - p));
    mysql_decode(p, (size_t)((port ? port : host_end) - p), u->host, sizeof(u->host));
    if (!u->host[0]) snprintf(u->host, sizeof(u->host), "localhost");
    if (port) u->port = atoi(port + 1);
    if (slash) mysql_decode(slash + 1, (size_t)(end - slash - 1), u->database, sizeof(u->database));
    while (query && *query) {
        query++;
        const char* amp = strchr(query, '&');
        size_t len = amp ? (size_t)(amp - query) : strlen(query);
        const char* eq = (const char*)memchr(query, '=', len);
        if (eq) {
            char key[64], value[1024];
            snprintf(key, sizeof(key), "%.*s", (int)(eq - query), query);
            mysql_decode(eq + 1, len - (size_t)(eq - query) - 1, value, sizeof(value));
            for (char* k = key; *k; k++) if (*k == '_') *k = '-';
            if (strcasecmp(key, "ssl-mode") == 0 || strcasecmp(key, "sslmode") == 0) {
                if (!rdb_mod_parse_tls_mode(value, &u->tls)) {
                    rdb_mod_log(RDB_LOG_ERROR, "rdb mysql: unknown ssl-mode '%s'", value);
                    return false;
                }
            } else if (strcasecmp(key, "ssl-ca") == 0) {
                snprintf(u->ca_file, sizeof(u->ca_file), "%s", value);
            } else if (strcasecmp(key, "socket") == 0) {
                snprintf(u->socket, sizeof(u->socket), "%s", value);
            } else if (strcasecmp(key, "connect-timeout") == 0) {
                u->connect_timeout_ms = (int64_t)atol(value) * 1000;
            }
        }
        query = amp;
    }
    return true;
}

static void mysql_uri_free(MysqlUri* u) {
    if (u->password) {
        // RDB9: the plaintext password is not retained past the connect
        memset(u->password, 0, strlen(u->password));
        free(u->password);
        u->password = NULL;
    }
}

static void mysql_target(const MysqlUri* u, RdbTarget* t) {
    memset(t, 0, sizeof(*t));
    if (u->socket[0]) {
        t->kind = RDB_PEER_UNIX_SOCKET;
        snprintf(t->path, sizeof(t->path), "%s", u->socket);
        return;
    }
    t->kind = RDB_PEER_TCP;
    t->port = u->port;
    snprintf(t->host, sizeof(t->host), "%s", u->host);
}

static int mysql_resolve_targets(const char* uri, RdbTarget* out, int cap, int* out_count) {
    MysqlUri u;
    bool ok = cap > 0 && mysql_parse_uri(uri, &u);
    if (ok) mysql_target(&u, &out[0]);
    mysql_uri_free(&u);
    *out_count = ok ? 1 : 0;
    return ok ? RDB_OK : RDB_ERROR;
}

/* ══════════════════════════════════════════════════════════════════════
 * Connection
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    MYSQL* my;
    bool   mariadb;
} MysqlConn;

static const RdbDialect mysql_dialect = { RDB_PLACEHOLDER_QMARK, '`', "'\\\\'" };

static bool mysql_command(MYSQL* my, const char* sql) {
    if (mysql_query(my, sql) == 0) return true;
    rdb_mod_log(RDB_LOG_ERROR, "rdb mysql: '%s' failed: %s", sql, mysql_error(my));
    return false;
}

static int mysql_open(const RdbHostAPI* host, void* open_ctx, const char* uri,
                      const RdbOpenOptions* opts, void** out_conn) {
    rdb_mod_host = host;
    MysqlUri u;
    if (!mysql_parse_uri(uri, &u)) {
        mysql_uri_free(&u);
        return RDB_ERROR;
    }
    RdbTarget target;
    mysql_target(&u, &target);
    RdbTunnelSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.struct_size = sizeof(spec);
    spec.protocol = RDB_WIRE_MYSQL;
    spec.tls = target.kind == RDB_PEER_UNIX_SOCKET ? RDB_TLS_DISABLE : u.tls;
    spec.ca_file = u.ca_file[0] ? u.ca_file : NULL;
    spec.connect_timeout_ms = u.connect_timeout_ms;
    char endpoint[1024];
    if (host->tunnel_open(open_ctx, &target, &spec, endpoint, sizeof(endpoint)) != RDB_OK) {
        mysql_uri_free(&u);
        return RDB_ERROR;
    }

    MYSQL* my = mysql_init(NULL);
    unsigned int protocol = MYSQL_PROTOCOL_SOCKET;
    my_bool off = 0;
    unsigned int timeout = (unsigned int)(u.connect_timeout_ms / 1000);
    mysql_optionsv(my, MYSQL_OPT_PROTOCOL, &protocol);
    // reconnects would bypass host registration (JA16.1); LOCAL INFILE reads client files
    mysql_optionsv(my, MYSQL_OPT_RECONNECT, &off);
    mysql_optionsv(my, MYSQL_OPT_LOCAL_INFILE, &off);
    mysql_optionsv(my, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    if (timeout) mysql_optionsv(my, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_upstream_tls = spec.tls >= RDB_TLS_REQUIRE;
    MYSQL* connected = mysql_real_connect(my, "localhost", u.user, u.password ? u.password : "",
                                          u.database[0] ? u.database : NULL, 0, endpoint, 0);
    mysql_upstream_tls = false;
    mysql_uri_free(&u);
    if (!connected) {
        rdb_mod_log(RDB_LOG_ERROR, "rdb mysql: connection failed: %s", mysql_error(my));
        mysql_close(my);
        return RDB_ERROR;
    }
    if (opts->readonly && !mysql_command(my, "SET SESSION TRANSACTION READ ONLY")) {
        mysql_close(my);
        return RDB_ERROR;
    }

    MysqlConn* conn = (MysqlConn*)calloc(1, sizeof(MysqlConn));
    conn->my = my;
    const char* version = mysql_get_server_info(my);
    conn->mariadb = version && strstr(version, "MariaDB");
    RdbConnInfo info;
    memset(&info, 0, sizeof(info));
    info.peer = target;
    info.socket_fd = -1;            // the network socket belongs to the host bridge
    info.backend_id = (int64_t)mysql_thread_id(my);
    snprintf(info.server_version, sizeof(info.server_version), "%s", version ? version : "");
    if (host->conn_register(open_ctx, conn, &info) != RDB_OK) {
        mysql_close(my);
        free(conn);
        return RDB_ERROR;
    }
    *out_conn = conn;
    return RDB_OK;
}

static void mysql_close_conn(void* conn) {
    MysqlConn* c = (MysqlConn*)conn;
    if (!c) return;
    mysql_close(c->my);
    free(c);
}

static int mysql_ping_conn(void* conn) {
    return mysql_ping(((MysqlConn*)conn)->my) == 0 ? RDB_OK : RDB_CONN_LOST;
}

static int mysql_reset_conn(void* conn) {
    return mysql_reset_connection(((MysqlConn*)conn)->my) == 0 ? RDB_OK : RDB_ERROR;
}

static int mysql_set_timeout(void* conn, int64_t statement_ms) {
    MysqlConn* c = (MysqlConn*)conn;
    char sql[96];
    // MariaDB counts seconds in max_statement_time; MySQL milliseconds in max_execution_time
    if (c->mariadb) {
        snprintf(sql, sizeof(sql), "SET SESSION max_statement_time = %.3f", statement_ms / 1000.0);
    } else {
        snprintf(sql, sizeof(sql), "SET SESSION max_execution_time = %" PRId64, statement_ms);
    }
    return mysql_command(c->my, sql) ? RDB_OK : RDB_ERROR;
}

static const char* mysql_error_text(void* conn) {
    return mysql_error(((MysqlConn*)conn)->my);
}

static bool mysql_lost(unsigned int err) {
    return err == 2006 || err == 2013;     // CR_SERVER_GONE_ERROR, CR_SERVER_LOST
}

/* ══════════════════════════════════════════════════════════════════════
 * Types
 * ══════════════════════════════════════════════════════════════════════ */

static RdbType mysql_type_for_field(const MYSQL_FIELD* f) {
    switch (f->type) {
        case MYSQL_TYPE_TINY:
            return f->length == 1 ? RDB_TYPE_BOOL : RDB_TYPE_INT;   // TINYINT(1) is BOOLEAN
        case MYSQL_TYPE_SHORT: case MYSQL_TYPE_LONG: case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24: case MYSQL_TYPE_YEAR:
            return RDB_TYPE_INT;
        case MYSQL_TYPE_FLOAT: case MYSQL_TYPE_DOUBLE:
            return RDB_TYPE_FLOAT;
        case MYSQL_TYPE_DECIMAL: case MYSQL_TYPE_NEWDECIMAL:
            return RDB_TYPE_DECIMAL;
        case MYSQL_TYPE_DATE: case MYSQL_TYPE_DATETIME: case MYSQL_TYPE_TIMESTAMP:
        case MYSQL_TYPE_NEWDATE:
            return RDB_TYPE_DATETIME;
        case MYSQL_TYPE_JSON:
            return RDB_TYPE_JSON;
        case MYSQL_TYPE_TINY_BLOB: case MYSQL_TYPE_MEDIUM_BLOB: case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB: case MYSQL_TYPE_STRING: case MYSQL_TYPE_VAR_STRING:
            return f->charsetnr == 63 ? RDB_TYPE_BLOB : RDB_TYPE_STRING;  // 63 = binary
        default:
            return RDB_TYPE_STRING;
    }
}

/** schema type from information_schema DATA_TYPE / COLUMN_TYPE */
static RdbType mysql_type_for_decl(const char* data_type, const char* column_type) {
    static const struct { const char* name; RdbType type; } map[] = {
        { "tinyint", RDB_TYPE_INT }, { "smallint", RDB_TYPE_INT }, { "mediumint", RDB_TYPE_INT },
        { "int", RDB_TYPE_INT }, { "bigint", RDB_TYPE_INT }, { "year", RDB_TYPE_INT },
        { "float", RDB_TYPE_FLOAT }, { "double", RDB_TYPE_FLOAT }, { "real", RDB_TYPE_FLOAT },
        { "decimal", RDB_TYPE_DECIMAL }, { "numeric", RDB_TYPE_DECIMAL },
        { "date", RDB_TYPE_DATETIME }, { "datetime", RDB_TYPE_DATETIME },
        { "timestamp", RDB_TYPE_DATETIME }, { "json", RDB_TYPE_JSON },
        { "tinyblob", RDB_TYPE_BLOB }, { "blob", RDB_TYPE_BLOB }, { "mediumblob", RDB_TYPE_BLOB },
        { "longblob", RDB_TYPE_BLOB }, { "binary", RDB_TYPE_BLOB }, { "varbinary", RDB_TYPE_BLOB },
    };
    if (column_type && strncasecmp(column_type, "tinyint(1)", 10) == 0) return RDB_TYPE_BOOL;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcasecmp(data_type, map[i].name) == 0) return map[i].type;
    }
    return RDB_TYPE_STRING;
}

/* ══════════════════════════════════════════════════════════════════════
 * Statements: server-side prepared, results buffered as text
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    MysqlConn*     conn;
    MYSQL_STMT*    st;
    int            param_count;
    char**         params;          // text values; NULL = SQL NULL
    bool           executed;
    MYSQL_RES*     meta;
    int            column_count;
    MYSQL_FIELD*   fields;
    MYSQL_BIND*    results;
    char**         buffers;
    unsigned long* lengths;
    my_bool*       nulls;
} MysqlStmt;

static int mysql_prepare_stmt(void* conn, const char* sql, void** out_stmt) {
    MysqlConn* c = (MysqlConn*)conn;
    MYSQL_STMT* st = mysql_stmt_init(c->my);
    if (!st || mysql_stmt_prepare(st, sql, strlen(sql)) != 0) {
        rdb_mod_log(RDB_LOG_ERROR, "rdb mysql: prepare failed: %s",
                    st ? mysql_stmt_error(st) : mysql_error(c->my));
        if (st) mysql_stmt_close(st);
        return RDB_ERROR;
    }
    MysqlStmt* s = (MysqlStmt*)calloc(1, sizeof(MysqlStmt));
    s->conn = c;
    s->st = st;
    s->param_count = (int)mysql_stmt_param_count(st);
    s->params = (char**)calloc((size_t)(s->param_count ? s->param_count : 1), sizeof(char*));
    *out_stmt = s;
    return RDB_OK;
}

static int mysql_bind(void* stmt, int index, const RdbParam* param) {
    MysqlStmt* s = (MysqlStmt*)stmt;
    if (index < 1 || index > s->param_count || s->executed) return RDB_ERROR;
    free(s->params[index - 1]);
    s->params[index - 1] = rdb_mod_param_text(param);
    return RDB_OK;
}

static int mysql_execute(MysqlStmt* s) {
    if (s->executed) return RDB_OK;
    s->executed = true;
    MYSQL_BIND* binds = (MYSQL_BIND*)calloc((size_t)(s->param_count ? s->param_count : 1), sizeof(MYSQL_BIND));
    for (int i = 0; i < s->param_count; i++) {
        binds[i].buffer_type = s->params[i] ? MYSQL_TYPE_STRING : MYSQL_TYPE_NULL;
        binds[i].buffer = s->params[i];
        binds[i].buffer_length = s->params[i] ? strlen(s->params[i]) : 0;
    }
    my_bool update_max = 1;
    bool ok = (s->param_count == 0 || mysql_stmt_bind_param(s->st, binds) == 0) &&
        mysql_stmt_attr_set(s->st, STMT_ATTR_UPDATE_MAX_LENGTH, &update_max) == 0 &&
        mysql_stmt_execute(s->st) == 0 && mysql_stmt_store_result(s->st) == 0;
    free(binds);
    if (!ok) {
        rdb_mod_log(RDB_LOG_ERROR, "rdb mysql: query failed: %s", mysql_stmt_error(s->st));
        return mysql_lost(mysql_stmt_errno(s->st)) ? RDB_CONN_LOST : RDB_ERROR;
    }
    s->meta = mysql_stmt_result_metadata(s->st);
    if (!s->meta) return RDB_OK;        // a statement without a result set
    s->column_count = (int)mysql_num_fields(s->meta);
    s->fields = mysql_fetch_fields(s->meta);
    size_t n = (size_t)(s->column_count ? s->column_count : 1);
    s->results = (MYSQL_BIND*)calloc(n, sizeof(MYSQL_BIND));
    s->buffers = (char**)calloc(n, sizeof(char*));
    s->lengths = (unsigned long*)calloc(n, sizeof(unsigned long));
    s->nulls = (my_bool*)calloc(n, sizeof(my_bool));
    for (int c = 0; c < s->column_count; c++) {
        // every column is fetched as text sized by the stored result's max length
        unsigned long size = s->fields[c].max_length + 1;
        if (size < 64) size = 64;
        s->buffers[c] = (char*)calloc(1, size);
        s->results[c].buffer_type = MYSQL_TYPE_STRING;
        s->results[c].buffer = s->buffers[c];
        s->results[c].buffer_length = size;
        s->results[c].length = &s->lengths[c];
        s->results[c].is_null = &s->nulls[c];
    }
    if (mysql_stmt_bind_result(s->st, s->results) != 0) {
        rdb_mod_log(RDB_LOG_ERROR, "rdb mysql: result binding failed: %s", mysql_stmt_error(s->st));
        return RDB_ERROR;
    }
    return RDB_OK;
}

static int mysql_step(void* stmt) {
    MysqlStmt* s = (MysqlStmt*)stmt;
    int rc = mysql_execute(s);
    if (rc != RDB_OK) return rc;
    if (!s->meta) return RDB_DONE;
    int fetched = mysql_stmt_fetch(s->st);
    if (fetched == 0 || fetched == MYSQL_DATA_TRUNCATED) return RDB_ROW;
    if (fetched == MYSQL_NO_DATA) return RDB_DONE;
    rdb_mod_log(RDB_LOG_ERROR, "rdb mysql: fetch failed: %s", mysql_stmt_error(s->st));
    return mysql_lost(mysql_stmt_errno(s->st)) ? RDB_CONN_LOST : RDB_ERROR;
}

static int mysql_column_count_stmt(void* stmt) {
    MysqlStmt* s = (MysqlStmt*)stmt;
    return mysql_execute(s) == RDB_OK ? s->column_count : 0;
}

static int mysql_column_desc(void* stmt, int col, RdbColumnDesc* out) {
    MysqlStmt* s = (MysqlStmt*)stmt;
    if (mysql_execute(s) != RDB_OK || col < 0 || col >= s->column_count) return RDB_ERROR;
    out->name = s->fields[col].name;
    out->type = mysql_type_for_field(&s->fields[col]);
    return RDB_OK;
}

static RdbValue mysql_column_value(void* stmt, int col) {
    MysqlStmt* s = (MysqlStmt*)stmt;
    RdbValue v;
    memset(&v, 0, sizeof(v));
    v.type = RDB_TYPE_NULL;
    v.is_null = true;
    if (!s->meta || col < 0 || col >= s->column_count || s->nulls[col]) return v;
    const char* text = s->buffers[col];
    v.is_null = false;
    v.type = mysql_type_for_field(&s->fields[col]);
    switch (v.type) {
        case RDB_TYPE_INT:   v.int_val = strtoll(text, NULL, 10); break;
        case RDB_TYPE_FLOAT: v.float_val = strtod(text, NULL); break;
        case RDB_TYPE_BOOL:  v.bool_val = text[0] != '0'; break;
        case RDB_TYPE_BLOB:  v.is_null = true; break;   // binary columns arrive in Phase 2
        default:
            v.str_val = text;
            v.str_len = (int)s->lengths[col];
            break;
    }
    return v;
}

static void mysql_finalize(void* stmt) {
    MysqlStmt* s = (MysqlStmt*)stmt;
    if (!s) return;
    if (s->meta) mysql_free_result(s->meta);
    mysql_stmt_close(s->st);
    for (int i = 0; i < s->param_count; i++) free(s->params[i]);
    for (int c = 0; c < s->column_count; c++) free(s->buffers[c]);
    free(s->params);
    free(s->results);
    free(s->buffers);
    free(s->lengths);
    free(s->nulls);
    free(s);
}

/* ══════════════════════════════════════════════════════════════════════
 * Schema (information_schema, current database)
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    MYSQL_RES* res;
    int        count;
    MYSQL_ROW* rows;            // row data stays valid until the result is freed
} MysqlRows;

static bool mysql_rows(MYSQL* my, const char* sql, MysqlRows* out) {
    memset(out, 0, sizeof(*out));
    if (mysql_query(my, sql) != 0 || !(out->res = mysql_store_result(my))) {
        rdb_mod_log(RDB_LOG_ERROR, "rdb mysql: catalog query failed: %s", mysql_error(my));
        return false;
    }
    out->count = (int)mysql_num_rows(out->res);
    out->rows = (MYSQL_ROW*)calloc((size_t)(out->count ? out->count : 1), sizeof(MYSQL_ROW));
    for (int i = 0; i < out->count; i++) out->rows[i] = mysql_fetch_row(out->res);
    return true;
}

static void mysql_rows_free(MysqlRows* r) {
    if (r->res) mysql_free_result(r->res);
    free(r->rows);
}

static const char* mysql_cell(const MysqlRows* r, int row, int col) {
    const char* v = r->rows[row][col];
    return v ? v : "";
}

static int mysql_rows_for(const MysqlRows* r, const char* table) {
    int n = 0;
    for (int i = 0; i < r->count; i++) n += strcmp(mysql_cell(r, i, 0), table) == 0;
    return n;
}

static int mysql_load_schema(void* conn, const RdbHostAPI* host, void* meta, RdbSchema* out) {
    MYSQL* my = ((MysqlConn*)conn)->my;
    RdbModMeta m = { host, meta };
    MysqlRows tables, columns, indexes, fks, triggers, functions;
    if (!mysql_rows(my, "SELECT TABLE_NAME, TABLE_TYPE FROM information_schema.TABLES "
                        "WHERE TABLE_SCHEMA = DATABASE() ORDER BY TABLE_NAME", &tables)) {
        return RDB_ERROR;
    }
    mysql_rows(my,
        "SELECT c.TABLE_NAME, c.COLUMN_NAME, c.COLUMN_TYPE, c.DATA_TYPE, c.IS_NULLABLE, "
        "COALESCE(k.ORDINAL_POSITION, 0) FROM information_schema.COLUMNS c "
        "LEFT JOIN information_schema.KEY_COLUMN_USAGE k ON k.TABLE_SCHEMA = c.TABLE_SCHEMA "
        "AND k.TABLE_NAME = c.TABLE_NAME AND k.COLUMN_NAME = c.COLUMN_NAME "
        "AND k.CONSTRAINT_NAME = 'PRIMARY' "
        "WHERE c.TABLE_SCHEMA = DATABASE() ORDER BY c.TABLE_NAME, c.ORDINAL_POSITION", &columns);
    // index columns are joined by the 0x1f unit separator
    mysql_rows(my,
        "SELECT TABLE_NAME, INDEX_NAME, MIN(NON_UNIQUE), "
        "GROUP_CONCAT(COLUMN_NAME ORDER BY SEQ_IN_INDEX SEPARATOR '\x1f') "
        "FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = DATABASE() "
        "GROUP BY TABLE_NAME, INDEX_NAME ORDER BY TABLE_NAME, INDEX_NAME", &indexes);
    mysql_rows(my,
        "SELECT TABLE_NAME, COLUMN_NAME, REFERENCED_TABLE_NAME, REFERENCED_COLUMN_NAME "
        "FROM information_schema.KEY_COLUMN_USAGE WHERE TABLE_SCHEMA = DATABASE() "
        "AND REFERENCED_TABLE_NAME IS NOT NULL "
        "ORDER BY TABLE_NAME, CONSTRAINT_NAME, ORDINAL_POSITION", &fks);
    mysql_rows(my,
        "SELECT EVENT_OBJECT_TABLE, TRIGGER_NAME, ACTION_TIMING, EVENT_MANIPULATION "
        "FROM information_schema.TRIGGERS WHERE TRIGGER_SCHEMA = DATABASE() "
        "ORDER BY EVENT_OBJECT_TABLE, TRIGGER_NAME", &triggers);
    mysql_rows(my,
        "SELECT r.ROUTINE_NAME, (SELECT COUNT(*) FROM information_schema.PARAMETERS p "
        " WHERE p.SPECIFIC_SCHEMA = r.ROUTINE_SCHEMA AND p.SPECIFIC_NAME = r.SPECIFIC_NAME "
        " AND p.ORDINAL_POSITION > 0) FROM information_schema.ROUTINES r "
        "WHERE r.ROUTINE_SCHEMA = DATABASE() AND r.ROUTINE_TYPE = 'FUNCTION' "
        "ORDER BY r.ROUTINE_NAME", &functions);

    out->table_count = tables.count;
    out->tables = (RdbTable*)rdb_mod_meta_calloc(&m, (size_t)tables.count, sizeof(RdbTable));
    for (int t = 0; t < tables.count; t++) {
        RdbTable* tbl = &out->tables[t];
        tbl->name = rdb_mod_meta_dup(&m, mysql_cell(&tables, t, 0));
        tbl->is_view = strcmp(mysql_cell(&tables, t, 1), "VIEW") == 0;

        tbl->column_count = mysql_rows_for(&columns, tbl->name);
        tbl->columns = (RdbColumn*)rdb_mod_meta_calloc(&m, (size_t)tbl->column_count, sizeof(RdbColumn));
        for (int r = 0, i = 0; r < columns.count; r++) {
            if (strcmp(mysql_cell(&columns, r, 0), tbl->name) != 0) continue;
            RdbColumn* c = &tbl->columns[i++];
            c->name = rdb_mod_meta_dup(&m, mysql_cell(&columns, r, 1));
            c->type_decl = rdb_mod_meta_dup(&m, mysql_cell(&columns, r, 2));
            c->type = mysql_type_for_decl(mysql_cell(&columns, r, 3), mysql_cell(&columns, r, 2));
            c->nullable = strcmp(mysql_cell(&columns, r, 4), "YES") == 0;
            c->pk_index = atoi(mysql_cell(&columns, r, 5));
            c->primary_key = c->pk_index > 0;
        }

        tbl->index_count = mysql_rows_for(&indexes, tbl->name);
        tbl->indexes = (RdbIndex*)rdb_mod_meta_calloc(&m, (size_t)tbl->index_count, sizeof(RdbIndex));
        for (int r = 0, i = 0; r < indexes.count; r++) {
            if (strcmp(mysql_cell(&indexes, r, 0), tbl->name) != 0) continue;
            RdbIndex* x = &tbl->indexes[i++];
            x->name = rdb_mod_meta_dup(&m, mysql_cell(&indexes, r, 1));
            x->unique = strcmp(mysql_cell(&indexes, r, 2), "0") == 0;
            const char* cols = mysql_cell(&indexes, r, 3);
            x->column_count = cols[0] ? 1 : 0;
            for (const char* p = cols; *p; p++) x->column_count += *p == '\x1f';
            x->columns = (const char**)rdb_mod_meta_calloc(&m, (size_t)x->column_count, sizeof(char*));
            for (int k = 0; k < x->column_count; k++) {
                size_t len = strcspn(cols, "\x1f");
                char name[256];
                snprintf(name, sizeof(name), "%.*s", (int)len, cols);
                x->columns[k] = rdb_mod_meta_dup(&m, name);
                cols += len + (cols[len] ? 1 : 0);
            }
        }

        tbl->fk_count = mysql_rows_for(&fks, tbl->name);
        tbl->foreign_keys = (RdbForeignKey*)rdb_mod_meta_calloc(&m, (size_t)tbl->fk_count,
                                                                sizeof(RdbForeignKey));
        for (int r = 0, i = 0; r < fks.count; r++) {
            if (strcmp(mysql_cell(&fks, r, 0), tbl->name) != 0) continue;
            RdbForeignKey* fk = &tbl->foreign_keys[i++];
            fk->column = rdb_mod_meta_dup(&m, mysql_cell(&fks, r, 1));
            fk->ref_table = rdb_mod_meta_dup(&m, mysql_cell(&fks, r, 2));
            fk->ref_column = rdb_mod_meta_dup(&m, mysql_cell(&fks, r, 3));
        }

        tbl->trigger_count = mysql_rows_for(&triggers, tbl->name);
        tbl->triggers = (RdbTrigger*)rdb_mod_meta_calloc(&m, (size_t)tbl->trigger_count, sizeof(RdbTrigger));
        for (int r = 0, i = 0; r < triggers.count; r++) {
            if (strcmp(mysql_cell(&triggers, r, 0), tbl->name) != 0) continue;
            RdbTrigger* tg = &tbl->triggers[i++];
            tg->name = rdb_mod_meta_dup(&m, mysql_cell(&triggers, r, 1));
            tg->timing = rdb_mod_meta_word(&m, mysql_cell(&triggers, r, 2));
            tg->event = rdb_mod_meta_word(&m, mysql_cell(&triggers, r, 3));
        }
    }
    out->function_count = functions.count;
    out->functions = (RdbFunction*)rdb_mod_meta_calloc(&m, (size_t)functions.count, sizeof(RdbFunction));
    for (int r = 0; r < functions.count; r++) {
        out->functions[r].name = rdb_mod_meta_dup(&m, mysql_cell(&functions, r, 0));
        out->functions[r].type = rdb_mod_meta_word(&m, "scalar");
        out->functions[r].narg = atoi(mysql_cell(&functions, r, 1));
        out->functions[r].builtin = false;  // only the database's own functions are listed
    }
    mysql_rows_free(&tables);
    mysql_rows_free(&columns);
    mysql_rows_free(&indexes);
    mysql_rows_free(&fks);
    mysql_rows_free(&triggers);
    mysql_rows_free(&functions);
    return RDB_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * Driver table
 * ══════════════════════════════════════════════════════════════════════ */

const RdbDriver rdb_mysql_driver = {
    sizeof(RdbDriver),
    RDB_DRIVER_API_VERSION,
    "mysql",
    &mysql_dialect,
    RDB_CAP_STATEMENT_TIMEOUT,
    mysql_resolve_targets,
    mysql_open,
    mysql_close_conn,
    mysql_ping_conn,
    mysql_reset_conn,
    NULL,       // cancel: KILL QUERY needs a registered helper session (deferred)
    mysql_set_timeout,
    mysql_load_schema,
    mysql_prepare_stmt,
    mysql_bind,
    mysql_step,
    mysql_column_count_stmt,
    mysql_column_desc,
    mysql_column_value,
    mysql_finalize,
    mysql_error_text,
    NULL,       // result_schema: row driver, served by the host adapter
    NULL,       // fetch_batch
};
