/**
 * @file rdb.c
 * @brief Generic relational database layer — driver registry, host-owned
 *        connection lifecycle (JA16.1–JA16.3), dialect-aware SQL rendering,
 *        credential redaction (RDB9), and wrappers that delegate to the
 *        connection's RdbDriver table.
 */

#include "rdb.h"
#include "arraylist.h"
#include "log.h"
#include "memtrack.h"
#include "rdb_batch.h"
#include "rdb_tunnel.h"
#include "digest.h"
#include "str.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════════════
 * Driver registry (static array, max 8 drivers) + resolver
 * ══════════════════════════════════════════════════════════════════════ */

#define RDB_MAX_DRIVERS 8
#define RDB_MAX_TARGETS 8

static const RdbDriver* rdb_drivers[RDB_MAX_DRIVERS];
static int rdb_driver_count = 0;
static RdbDriverResolver rdb_driver_resolver = NULL;
static const RdbRegistryHooks* rdb_registry_hooks = NULL;

bool rdb_register_driver(const RdbDriver* driver) {
    // a table predating the v2 ABI cannot be dispatched safely (RDB3)
    if (!driver || !driver->name || driver->struct_size < RDB_DRIVER_MIN_SIZE ||
            driver->api_version != RDB_DRIVER_API_VERSION ||
            !driver->resolve_targets || !driver->open || !driver->close ||
            !driver->prepare || !driver->step || !driver->column_value ||
            !driver->finalize) {
        log_error("rdb: rejected incompatible driver table '%s'",
                  driver && driver->name ? driver->name : "(null)");
        return false;
    }
    for (int i = 0; i < rdb_driver_count; i++) {
        if (strcmp(rdb_drivers[i]->name, driver->name) == 0) return rdb_drivers[i] == driver;
    }
    if (rdb_driver_count >= RDB_MAX_DRIVERS) {
        log_error("rdb: max drivers exceeded");
        return false;
    }
    rdb_drivers[rdb_driver_count++] = driver;
    log_debug("rdb: registered driver '%s'", driver->name);
    return true;
}

static const RdbDriver* rdb_find_registered(const char* name) {
    for (int i = 0; i < rdb_driver_count; i++) {
        if (strcmp(rdb_drivers[i]->name, name) == 0) return rdb_drivers[i];
    }
    return NULL;
}

const RdbDriver* rdb_get_driver(const char* name) {
    if (!name) return NULL;
    const RdbDriver* driver = rdb_find_registered(name);
    if (driver || !rdb_driver_resolver) return driver;
    // drivers outside the host arrive through the Jube provider index (RDB4)
    driver = rdb_driver_resolver(name);
    if (driver && rdb_register_driver(driver)) return rdb_find_registered(name);
    return NULL;
}

void rdb_set_driver_resolver(RdbDriverResolver resolver) {
    rdb_driver_resolver = resolver;
}

void rdb_set_registry_hooks(const RdbRegistryHooks* hooks) {
    rdb_registry_hooks = hooks;
}

/* ══════════════════════════════════════════════════════════════════════
 * Auto-detect driver from URI / file extension
 * ══════════════════════════════════════════════════════════════════════ */

const char* rdb_detect_driver(const char* uri) {
    if (!uri) return NULL;
    size_t len = strlen(uri);

    // scheme-based detection
    if (len > 14 && strncmp(uri, "postgresql://", 13) == 0) return "postgresql";
    if (len > 11 && strncmp(uri, "postgres://", 11) == 0)   return "postgresql";
    if (len > 8  && strncmp(uri, "mysql://", 8) == 0)       return "mysql";
    if (len > 10 && strncmp(uri, "mariadb://", 10) == 0)    return "mysql";
    if (len > 9  && strncmp(uri, "duckdb://", 9) == 0)      return "duckdb";

    // extension-based detection
    if (len > 3 && strcmp(uri + len - 3, ".db") == 0)        return "sqlite";
    if (len > 7 && strcmp(uri + len - 7, ".sqlite") == 0)    return "sqlite";
    if (len > 8 && strcmp(uri + len - 8, ".sqlite3") == 0)   return "sqlite";
    if (len > 4 && strcmp(uri + len - 4, ".ddb") == 0)       return "duckdb";
    if (len > 7 && strcmp(uri + len - 7, ".duckdb") == 0)    return "duckdb";

    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * Credential redaction (RDB9)
 * ══════════════════════════════════════════════════════════════════════ */

static bool rdb_is_secret_key(const char* key, size_t len) {
    static const char* secret_keys[] = { "password", "sslpassword", "passwd", "pwd" };
    for (size_t i = 0; i < sizeof(secret_keys) / sizeof(secret_keys[0]); i++) {
        if (str_ieq_const(key, len, secret_keys[i])) {
            return true;
        }
    }
    return false;
}

typedef struct {
    char*  out;
    size_t cap;
    size_t len;
} RdbRedactBuf;

static void redact_put(RdbRedactBuf* b, const char* s, size_t n) {
    for (size_t i = 0; i < n && b->len + 1 < b->cap; i++) b->out[b->len++] = s[i];
}

/** mask `key=value` pairs separated by `sep` chars; values may be quoted */
static const char* redact_pairs(RdbRedactBuf* b, const char* p, const char* seps) {
    while (*p) {
        // copy separators verbatim
        while (*p && strchr(seps, *p)) redact_put(b, p++, 1);
        const char* key = p;
        while (*p && *p != '=' && !strchr(seps, *p)) p++;
        size_t key_len = (size_t)(p - key);
        while (key_len > 0 && isspace((unsigned char)key[key_len - 1])) key_len--;
        redact_put(b, key, (size_t)(p - key));
        // libpq allows blanks between a key and its '='
        const char* eq = p;
        while (*eq == ' ' || *eq == '\t') eq++;
        if (*eq != '=') continue;
        redact_put(b, p, (size_t)(eq - p));
        p = eq;
        redact_put(b, p++, 1);
        while (*p == ' ') redact_put(b, p++, 1);
        // libpq values may be single-quoted with backslash escapes
        const char* value = p;
        if (*p == '\'') {
            p++;
            while (*p && *p != '\'') p += (*p == '\\' && p[1]) ? 2 : 1;
            if (*p == '\'') p++;
        } else {
            while (*p && !strchr(seps, *p)) p++;
        }
        if (rdb_is_secret_key(key, key_len)) redact_put(b, "***", 3);
        else redact_put(b, value, (size_t)(p - value));
    }
    return p;
}

char* rdb_redact_uri(const char* uri, char* out, size_t cap) {
    if (!out || cap == 0) return out;
    RdbRedactBuf b = { out, cap, 0 };
    if (!uri) uri = "";
    const char* scheme_end = strstr(uri, "://");
    if (!scheme_end) {
        // a libpq key/value conninfo carries `password=`; a plain path does not
        if (strchr(uri, '=')) redact_pairs(&b, uri, " \t");
        else redact_put(&b, uri, strlen(uri));
        out[b.len] = '\0';
        return out;
    }
    const char* authority = scheme_end + 3;
    redact_put(&b, uri, (size_t)(authority - uri));
    // the userinfo ends at the last '@' before any query, so a password with an
    // unescaped '/' is still masked rather than mistaken for a path
    const char* query_start = authority + strcspn(authority, "?#");
    const char* at = NULL;
    for (const char* q = authority; q < query_start; q++) {
        if (*q == '@') at = q;
    }
    const char* authority_end = (at ? at : authority) + strcspn(at ? at : authority, "/?#");
    if (at) {
        const char* colon = memchr(authority, ':', (size_t)(at - authority));
        if (colon) {
            redact_put(&b, authority, (size_t)(colon + 1 - authority));
            redact_put(&b, "***", 3);
            redact_put(&b, at, (size_t)(authority_end - at));
        } else {
            redact_put(&b, authority, (size_t)(authority_end - authority));
        }
    } else {
        redact_put(&b, authority, (size_t)(authority_end - authority));
    }
    const char* rest = authority_end;
    const char* query = strchr(rest, '?');
    if (query) {
        redact_put(&b, rest, (size_t)(query + 1 - rest));
        const char* fragment = strchr(query + 1, '#');
        size_t query_len = fragment ? (size_t)(fragment - query - 1) : strlen(query + 1);
        char query_copy[2048];
        if (query_len >= sizeof(query_copy)) query_len = sizeof(query_copy) - 1;
        memcpy(query_copy, query + 1, query_len);
        query_copy[query_len] = '\0';
        redact_pairs(&b, query_copy, "&");
        if (fragment) redact_put(&b, fragment, strlen(fragment));
    } else {
        redact_put(&b, rest, strlen(rest));
    }
    out[b.len] = '\0';
    return out;
}

/* ══════════════════════════════════════════════════════════════════════
 * Host API offered to drivers
 * ══════════════════════════════════════════════════════════════════════ */

/** lifecycle record; the registry's close callback reaches the driver here */
struct RdbConnLife {
    const RdbDriver* driver;
    void*      handle;          /* native connection, set at registration */
    ArrayList* stmts;           /* live driver statement handles */
    bool       closed;
    bool       owner_closing;   /* rdb_close() rather than registry teardown */
    uint32_t   rid;
    void*      registry;        /* runtime table holding the row (opaque) */
    RdbTunnel* tunnels[RDB_MAX_TARGETS];   /* host TLS bridges opened for this connection */
    int        tunnel_count;
    bool       tls_guaranteed;  /* every tunnel requires TLS upstream */
    char       uri[256];        /* redacted, for leak diagnostics */
};

typedef struct {
    RdbConn*          conn;
    RdbConnLife*      life;
    const RdbTarget*  targets;
    int               target_count;
    bool              registered;
    void*             native;
} RdbOpenCtx;

struct RdbStmt {
    RdbConn* conn;
    void*    handle;            /* NULL once finalized (directly or by close cascade) */
    RdbBatchShred* shred;       /* row->column adapter state (rdb_batch.c) */
};

static void* rdb_host_meta_alloc(void* meta, size_t size) {
    return pool_calloc((Pool*)meta, size);
}

static char* rdb_host_meta_strdup(void* meta, const char* s) {
    return pool_strdup((Pool*)meta, s ? s : "");
}

static bool rdb_target_equal(const RdbTarget* a, const RdbTarget* b) {
    if (a->kind != b->kind) return false;
    switch (a->kind) {
        case RDB_PEER_TCP:
            return a->port == b->port && str_ieq_cstr(a->host, b->host);
        case RDB_PEER_FILE:
        case RDB_PEER_UNIX_SOCKET:
            return strcmp(a->path, b->path) == 0;
        case RDB_PEER_MEMORY:
            return true;
        default:
            return false;
    }
}

static int rdb_host_conn_register(void* open_ctx, void* native_conn, const RdbConnInfo* info) {
    RdbOpenCtx* ctx = (RdbOpenCtx*)open_ctx;
    if (!ctx || !native_conn || !info || ctx->registered) {
        log_error("rdb-registry: malformed or repeated registration");
        return RDB_ERROR;
    }
    // JA16.3: the connected peer must be one the host authorised
    bool authorised = false;
    for (int i = 0; i < ctx->target_count && !authorised; i++) {
        authorised = rdb_target_equal(&ctx->targets[i], &info->peer);
    }
    if (!authorised) {
        log_error("rdb-registry: '%s' connected to an unauthorised peer", ctx->life->uri);
        return RDB_ERROR;
    }
    ctx->life->handle = native_conn;
    uint32_t rid = 0;
    if (rdb_registry_hooks && rdb_registry_hooks->add &&
            rdb_registry_hooks->add(ctx->life, ctx->conn->uri, &rid, &ctx->life->registry) != RDB_OK) {
        ctx->life->handle = NULL;
        log_error("rdb-registry: host refused connection '%s'", ctx->life->uri);
        return RDB_ERROR;
    }
    ctx->life->rid = rid;
    ctx->conn->rid = rid;
    ctx->conn->info = *info;
    ctx->native = native_conn;
    ctx->registered = true;
    return RDB_OK;
}

static void rdb_host_log(int level, const char* message) {
    if (level == RDB_LOG_ERROR) log_error("rdb driver: %s", message ? message : "");
    else if (level == RDB_LOG_INFO) log_info("rdb driver: %s", message ? message : "");
    else log_debug("rdb driver: %s", message ? message : "");
}

/** RDB11: the host owns the upstream socket; only authorised targets qualify */
static int rdb_host_tunnel_open(void* open_ctx, const RdbTarget* target, const RdbTunnelSpec* spec,
                                char* out_endpoint, size_t cap) {
    RdbOpenCtx* ctx = (RdbOpenCtx*)open_ctx;
    if (!ctx || !target || !spec || ctx->life->tunnel_count >= RDB_MAX_TARGETS) return RDB_ERROR;
    bool authorised = false;
    for (int i = 0; i < ctx->target_count && !authorised; i++) {
        authorised = rdb_target_equal(&ctx->targets[i], target);
    }
    if (!authorised) {
        log_error("rdb-registry: tunnel to an unauthorised target refused");
        return RDB_ERROR;
    }
    RdbTunnel* tunnel = rdb_tunnel_open(target, spec, out_endpoint, cap);
    if (!tunnel) return RDB_ERROR;
    RdbConnLife* life = ctx->life;
    life->tls_guaranteed = (life->tunnel_count == 0 || life->tls_guaranteed) &&
        spec->tls >= RDB_TLS_REQUIRE;
    life->tunnels[life->tunnel_count++] = tunnel;
    return RDB_OK;
}

static bool rdb_host_sha256(const void* data, size_t len, uint8_t out[32]) {
    return digest_sha256(data, len, out);
}

static const char* rdb_host_ca_bundle_path(void) {
    // no host CA store is published yet; drivers fall back to URI-named CAs
    return NULL;
}

static const RdbHostAPI rdb_host_api = {
    sizeof(RdbHostAPI),
    RDB_HOST_API_VERSION,
    rdb_host_meta_alloc,
    rdb_host_meta_strdup,
    rdb_host_conn_register,
    rdb_host_log,
    rdb_host_ca_bundle_path,
    rdb_host_tunnel_open,
    rdb_host_sha256,
};

/* ══════════════════════════════════════════════════════════════════════
 * Connection lifecycle (JA16.1–JA16.3)
 * ══════════════════════════════════════════════════════════════════════ */

/** the bridges outlive the driver's connection by exactly its close */
static void rdb_life_close_tunnels(RdbConnLife* life) {
    for (int i = 0; i < life->tunnel_count; i++) rdb_tunnel_close(life->tunnels[i]);
    life->tunnel_count = 0;
}

void rdb_conn_life_close(RdbConnLife* life) {
    if (!life || life->closed) return;
    life->closed = true;
    if (!life->owner_closing) {
        // GC/teardown is a backstop, never the closing mechanism (S12.4.3)
        log_error("rdb-registry: closing connection rid=%u '%s' left open by its owner",
                  life->rid, life->uri);
    }
    if (life->stmts) {
        for (int i = 0; i < life->stmts->length; i++) {
            life->driver->finalize(life->stmts->data[i]);
        }
        arraylist_clear(life->stmts);
    }
    if (life->handle) life->driver->close(life->handle);
    life->handle = NULL;
    rdb_life_close_tunnels(life);
}

static void rdb_life_free(RdbConnLife* life) {
    if (!life) return;
    rdb_life_close_tunnels(life);   // a failed open may leave bridges behind
    if (life->stmts) arraylist_free(life->stmts);
    mem_free(life);
}

RdbConn* rdb_open(Pool* pool, const char* uri, const char* type, bool readonly) {
    if (!pool || !uri) return NULL;
    char redacted[1024];
    rdb_redact_uri(uri, redacted, sizeof(redacted));
    const char* driver_name = type ? type : rdb_detect_driver(uri);
    if (!driver_name) {
        log_error("rdb: cannot detect driver for '%s'", redacted);
        return NULL;
    }
    const RdbDriver* driver = rdb_get_driver(driver_name);
    if (!driver) {
        log_error("rdb: driver '%s' not registered", driver_name);
        return NULL;
    }

    RdbConn* conn = (RdbConn*)pool_calloc(pool, sizeof(RdbConn));
    if (!conn) {
        log_error("rdb: allocation failed for RdbConn");
        return NULL;
    }
    conn->driver   = driver;
    conn->pool     = pool;
    conn->readonly = readonly;
    conn->uri      = pool_strdup(pool, redacted);

    // JA16.3: resolve the URI into concrete targets and authorise them first
    RdbTarget targets[RDB_MAX_TARGETS];
    memset(targets, 0, sizeof(targets));
    int target_count = 0;
    if (driver->resolve_targets(uri, targets, RDB_MAX_TARGETS, &target_count) != RDB_OK ||
            target_count <= 0) {
        log_error("rdb: driver '%s' cannot resolve '%s'", driver_name, redacted);
        return NULL;
    }
    if (rdb_registry_hooks && rdb_registry_hooks->authorize &&
            rdb_registry_hooks->authorize(driver_name, targets, target_count) != RDB_OK) {
        log_error("rdb: connection to '%s' is not authorised", redacted);
        return NULL;
    }

    RdbConnLife* life = (RdbConnLife*)mem_calloc(1, sizeof(RdbConnLife), MEM_CAT_INPUT_OTHER);
    if (!life) return NULL;
    life->driver = driver;
    life->stmts = arraylist_new(4);
    snprintf(life->uri, sizeof(life->uri), "%s", redacted);

    RdbOpenCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.conn = conn;
    ctx.life = life;
    ctx.targets = targets;
    ctx.target_count = target_count;
    RdbOpenOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.struct_size = sizeof(opts);
    opts.readonly = readonly;

    void* handle = NULL;
    int rc = driver->open(&rdb_host_api, &ctx, uri, &opts, &handle);
    // registration is structural: success without registering this handle is a
    // contract violation, so the host closes it rather than trusting it
    bool violation = rc == RDB_OK && (!ctx.registered || ctx.native != handle);
    if (rc != RDB_OK || violation) {
        if (violation) {
            log_error("rdb: driver '%s' opened '%s' without registering it", driver_name, redacted);
            if (handle && handle != ctx.native) driver->close(handle);
        }
        // a failed open leaves no native connection: the driver closed its own,
        // so releasing the registry row must not close it again
        if (ctx.registered) {
            if (violation) {
                life->owner_closing = true;
                rdb_conn_life_close(life);
            }
            life->closed = true;
            if (life->rid && rdb_registry_hooks && rdb_registry_hooks->remove) {
                rdb_registry_hooks->remove(life->registry, life->rid);
            }
        }
        rdb_life_free(life);
        log_error("rdb: failed to open connection to '%s'", redacted);
        return NULL;
    }
    conn->handle = handle;
    conn->life = life;
    // the host, not the driver, knows whether its bridge guarantees TLS
    if (life->tunnel_count > 0) conn->info.tls = life->tls_guaranteed;
    log_debug("rdb: opened '%s' with driver '%s' (readonly=%d, rid=%u)",
              redacted, driver_name, readonly, conn->rid);
    return conn;
}

void rdb_close(RdbConn* conn) {
    if (!conn || !conn->life) return;
    RdbConnLife* life = conn->life;
    log_debug("rdb: closing connection to '%s'", conn->uri ? conn->uri : "(null)");
    life->owner_closing = true;
    // the registry is the close authority: releasing its row runs the close
    if (life->rid && !life->closed && rdb_registry_hooks && rdb_registry_hooks->remove) {
        rdb_registry_hooks->remove(life->registry, life->rid);
    }
    rdb_conn_life_close(life);
    rdb_life_free(life);
    conn->life = NULL;
    conn->handle = NULL;
    conn->rid = 0;
}

static bool rdb_conn_live(const RdbConn* conn) {
    return conn && conn->life && !conn->life->closed;
}

int rdb_ping(RdbConn* conn) {
    if (!rdb_conn_live(conn)) return RDB_CONN_LOST;
    return conn->driver->ping ? conn->driver->ping(conn->handle) : RDB_UNSUPPORTED;
}

int rdb_reset(RdbConn* conn) {
    if (!rdb_conn_live(conn)) return RDB_CONN_LOST;
    return conn->driver->reset ? conn->driver->reset(conn->handle) : RDB_UNSUPPORTED;
}

int rdb_cancel(RdbConn* conn) {
    if (!rdb_conn_live(conn)) return RDB_CONN_LOST;
    return conn->driver->cancel ? conn->driver->cancel(conn->handle) : RDB_UNSUPPORTED;
}

int rdb_set_timeout(RdbConn* conn, int64_t statement_ms) {
    if (!rdb_conn_live(conn)) return RDB_CONN_LOST;
    return conn->driver->set_timeout ?
        conn->driver->set_timeout(conn->handle, statement_ms) : RDB_UNSUPPORTED;
}

/* ══════════════════════════════════════════════════════════════════════
 * Schema access
 * ══════════════════════════════════════════════════════════════════════ */

/** navigation name of a forward FK: the column minus `_id`, else the target table */
static void rdb_derive_link_names(Pool* pool, RdbSchema* schema) {
    for (int t = 0; t < schema->table_count; t++) {
        RdbTable* tbl = &schema->tables[t];
        for (int f = 0; f < tbl->fk_count; f++) {
            RdbForeignKey* fk = &tbl->foreign_keys[f];
            if (fk->link_name) continue;
            const char* col = fk->column ? fk->column : "";
            size_t len = strlen(col);
            if (len > 3 && strcmp(col + len - 3, "_id") == 0) {
                char* link = (char*)pool_calloc(pool, len - 2);
                memcpy(link, col, len - 3);
                fk->link_name = link;
            } else {
                fk->link_name = pool_strdup(pool, fk->ref_table ? fk->ref_table : "");
            }
        }
    }
}

/** reverse FKs are derived from forward ones, identically for every backend */
static void rdb_build_reverse_fks(Pool* pool, RdbSchema* schema) {
    for (int pass = 0; pass < 2; pass++) {
        for (int t = 0; t < schema->table_count; t++) {
            RdbTable* tbl = &schema->tables[t];
            for (int f = 0; f < tbl->fk_count; f++) {
                RdbForeignKey* fk = &tbl->foreign_keys[f];
                RdbTable* ref = NULL;
                for (int r = 0; r < schema->table_count && !ref; r++) {
                    if (strcmp(schema->tables[r].name, fk->ref_table) == 0) ref = &schema->tables[r];
                }
                if (!ref) continue;
                if (pass == 0) {
                    ref->reverse_fk_count++;
                    continue;
                }
                RdbForeignKey* rev = &ref->reverse_fks[ref->reverse_fk_count++];
                rev->column     = pool_strdup(pool, fk->ref_column);
                rev->ref_table  = pool_strdup(pool, tbl->name);
                rev->ref_column = pool_strdup(pool, fk->column);
                rev->link_name  = pool_strdup(pool, tbl->name);
            }
        }
        if (pass == 0) {
            for (int t = 0; t < schema->table_count; t++) {
                RdbTable* tbl = &schema->tables[t];
                if (tbl->reverse_fk_count > 0) {
                    tbl->reverse_fks = (RdbForeignKey*)pool_calloc(pool,
                        (size_t)tbl->reverse_fk_count * sizeof(RdbForeignKey));
                }
                tbl->reverse_fk_count = 0;
            }
        }
    }
}

int rdb_load_schema(RdbConn* conn) {
    if (!rdb_conn_live(conn) || !conn->driver->load_schema) return RDB_ERROR;
    memset(&conn->schema, 0, sizeof(conn->schema));
    int rc = conn->driver->load_schema(conn->handle, &rdb_host_api, conn->pool, &conn->schema);
    if (rc == RDB_OK) {
        rdb_derive_link_names(conn->pool, &conn->schema);
        rdb_build_reverse_fks(conn->pool, &conn->schema);
    }
    return rc;
}

RdbTable* rdb_get_table(RdbConn* conn, const char* table_name) {
    if (!conn || !table_name) return NULL;
    for (int i = 0; i < conn->schema.table_count; i++) {
        if (strcmp(conn->schema.tables[i].name, table_name) == 0)
            return &conn->schema.tables[i];
    }
    return NULL;
}

RdbColumn* rdb_get_column(RdbTable* table, const char* column_name) {
    if (!table || !column_name) return NULL;
    for (int i = 0; i < table->column_count; i++) {
        if (strcmp(table->columns[i].name, column_name) == 0)
            return &table->columns[i];
    }
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
 * SQL rendering through the driver dialect (RDB5)
 * ══════════════════════════════════════════════════════════════════════ */

static const RdbDialect rdb_default_dialect = { RDB_PLACEHOLDER_QNUM, '"' };

const RdbDialect* rdb_dialect(const RdbConn* conn) {
    return conn && conn->driver && conn->driver->dialect ? conn->driver->dialect
                                                         : &rdb_default_dialect;
}

void rdb_append_ident(StrBuf* sb, const RdbDialect* dialect, const char* name) {
    char quote = dialect ? dialect->ident_quote : '"';
    strbuf_append_char(sb, quote);
    for (const char* p = name ? name : ""; *p; p++) {
        // a quote inside an identifier is escaped by doubling it
        if (*p == quote) strbuf_append_char(sb, quote);
        strbuf_append_char(sb, *p);
    }
    strbuf_append_char(sb, quote);
}

void rdb_append_placeholder(StrBuf* sb, const RdbDialect* dialect, int index) {
    RdbPlaceholderStyle style = dialect ? dialect->placeholder : RDB_PLACEHOLDER_QNUM;
    if (style == RDB_PLACEHOLDER_QMARK) {
        strbuf_append_char(sb, '?');
        return;
    }
    strbuf_append_char(sb, style == RDB_PLACEHOLDER_DOLLAR ? '$' : '?');
    strbuf_append_int(sb, index);
}

/* ══════════════════════════════════════════════════════════════════════
 * Query execution — wrappers delegating to the driver table
 * ══════════════════════════════════════════════════════════════════════ */

RdbStmt* rdb_prepare(RdbConn* conn, const char* sql) {
    if (!rdb_conn_live(conn) || !sql) return NULL;
    void* handle = NULL;
    if (conn->driver->prepare(conn->handle, sql, &handle) != RDB_OK || !handle) return NULL;
    RdbStmt* stmt = (RdbStmt*)pool_calloc(conn->pool, sizeof(RdbStmt));
    if (!stmt) {
        conn->driver->finalize(handle);
        return NULL;
    }
    stmt->conn = conn;
    stmt->handle = handle;
    // the close cascade finalizes whatever the owner leaves live
    arraylist_append(conn->life->stmts, handle);
    return stmt;
}

/** a statement is usable while its handle and connection are both live */
static bool rdb_stmt_live(const RdbStmt* stmt) {
    return stmt && stmt->handle && rdb_conn_live(stmt->conn);
}

int rdb_bind_int(RdbStmt* stmt, int index, int64_t value) {
    RdbParam p = {0};
    p.type    = RDB_TYPE_INT;
    p.int_val = value;
    return rdb_bind_param(stmt, index, &p);
}

int rdb_bind_float(RdbStmt* stmt, int index, double value) {
    RdbParam p = {0};
    p.type      = RDB_TYPE_FLOAT;
    p.float_val = value;
    return rdb_bind_param(stmt, index, &p);
}

int rdb_bind_string(RdbStmt* stmt, int index, const char* value) {
    RdbParam p = {0};
    p.type    = RDB_TYPE_STRING;
    p.str_val = value;
    return rdb_bind_param(stmt, index, &p);
}

int rdb_bind_null(RdbStmt* stmt, int index) {
    RdbParam p = {0};
    p.type = RDB_TYPE_NULL;
    return rdb_bind_param(stmt, index, &p);
}

int rdb_bind_param(RdbStmt* stmt, int index, const RdbParam* param) {
    if (!rdb_stmt_live(stmt) || !param) return RDB_ERROR;
    return stmt->conn->driver->bind_param(stmt->handle, index, param);
}

int rdb_step(RdbStmt* stmt) {
    if (!rdb_stmt_live(stmt)) return RDB_ERROR;
    return stmt->conn->driver->step(stmt->handle);
}

RdbValue rdb_column_value(RdbStmt* stmt, int col_index) {
    RdbValue val;
    memset(&val, 0, sizeof(val));
    val.type = RDB_TYPE_NULL;
    val.is_null = true;
    if (!rdb_stmt_live(stmt)) return val;
    return stmt->conn->driver->column_value(stmt->handle, col_index);
}

int rdb_column_count(RdbStmt* stmt) {
    if (!rdb_stmt_live(stmt)) return 0;
    return stmt->conn->driver->column_count(stmt->handle);
}

int rdb_column_desc(RdbStmt* stmt, int col_index, RdbColumnDesc* out) {
    if (!rdb_stmt_live(stmt) || !out) return RDB_ERROR;
    if (!stmt->conn->driver->column_desc) return RDB_UNSUPPORTED;
    return stmt->conn->driver->column_desc(stmt->handle, col_index, out);
}

void rdb_finalize(RdbStmt* stmt) {
    if (!stmt) return;
    rdb_batch_shred_free(stmt->shred);
    stmt->shred = NULL;
    if (!stmt->handle) return;
    void* handle = stmt->handle;
    stmt->handle = NULL;
    // a closed connection already finalized every live statement
    if (!rdb_conn_live(stmt->conn)) return;
    ArrayList* live = stmt->conn->life->stmts;
    for (int i = 0; i < live->length; i++) {
        if (live->data[i] == handle) {
            arraylist_remove(live, i);
            break;
        }
    }
    stmt->conn->driver->finalize(handle);
}

int64_t rdb_row_count(RdbConn* conn, const char* table_name) {
    // only schema-known names reach SQL, so a table name cannot inject
    if (!rdb_get_table(conn, table_name)) {
        log_error("rdb: row_count for unknown table '%s'", table_name ? table_name : "(null)");
        return -1;
    }
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, "SELECT COUNT(*) FROM ");
    rdb_append_ident(sb, rdb_dialect(conn), table_name);
    RdbStmt* stmt = rdb_prepare(conn, sb->str);
    strbuf_free(sb);
    if (!stmt) return -1;
    int64_t count = -1;
    if (rdb_step(stmt) == RDB_ROW) {
        RdbValue v = rdb_column_value(stmt, 0);
        if (!v.is_null && v.type == RDB_TYPE_INT) count = v.int_val;
    }
    rdb_finalize(stmt);
    return count;
}

const char* rdb_error_msg(RdbConn* conn) {
    if (!rdb_conn_live(conn) || !conn->driver->error_msg) return "rdb: unknown error";
    return conn->driver->error_msg(conn->handle);
}

/* ══════════════════════════════════════════════════════════════════════
 * Column batches — native driver path; rdb_batch.c holds the adapter
 * ══════════════════════════════════════════════════════════════════════ */

static bool rdb_driver_has_batches(const RdbDriver* driver) {
    size_t end = offsetof(RdbDriver, fetch_batch) + sizeof(driver->fetch_batch);
    return (driver->caps & RDB_CAP_COLUMNAR) && driver->struct_size >= end &&
        driver->result_schema && driver->fetch_batch;
}

int rdb_result_schema(RdbStmt* stmt, struct ArrowSchema* out) {
    if (!rdb_stmt_live(stmt) || !out) return RDB_ERROR;
    if (rdb_driver_has_batches(stmt->conn->driver)) {
        return stmt->conn->driver->result_schema(stmt->handle, out);
    }
    return rdb_batch_shred_schema(stmt, &stmt->shred, out);
}

int rdb_fetch_batch(RdbStmt* stmt, int64_t max_rows, struct ArrowArray* out) {
    if (!rdb_stmt_live(stmt) || !out || max_rows <= 0) return RDB_ERROR;
    if (rdb_driver_has_batches(stmt->conn->driver)) {
        return stmt->conn->driver->fetch_batch(stmt->handle, max_rows, out);
    }
    return rdb_batch_shred_fetch(stmt, &stmt->shred, max_rows, out);
}
