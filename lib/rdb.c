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

    // extension-based detection
    if (len > 3 && strcmp(uri + len - 3, ".db") == 0)        return "sqlite";
    if (len > 7 && strcmp(uri + len - 7, ".sqlite") == 0)    return "sqlite";
    if (len > 8 && strcmp(uri + len - 8, ".sqlite3") == 0)   return "sqlite";

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
    bool       free_pending;
    unsigned   active_calls;
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
    bool              reserved;
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
    if (!ctx->reserved) {
        uint32_t rid = 0;
        if (rdb_registry_hooks && rdb_registry_hooks->add &&
                rdb_registry_hooks->add(ctx->life, ctx->conn->uri, &rid, &ctx->life->registry) != RDB_OK) {
            ctx->life->handle = NULL;
            log_error("rdb-registry: host refused connection '%s'", ctx->life->uri);
            return RDB_ERROR;
        }
        ctx->life->rid = rid;
        ctx->conn->rid = rid;
    }
    ctx->conn->info = *info;
    ctx->native = native_conn;
    ctx->registered = true;
    return RDB_OK;
}

static void rdb_host_log(int level, const char* message) {
    // vendor messages (libpq) span lines; keep each driver message on one log line
    char line[1024];
    snprintf(line, sizeof(line), "%s", message ? message : "");
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == ' ')) line[--len] = '\0';
    for (char* p = line; *p; p++) {
        if (*p == '\n' || *p == '\t') *p = ' ';
    }
    if (level == RDB_LOG_ERROR) log_error("rdb driver: %s", line);
    else if (level == RDB_LOG_INFO) log_info("rdb driver: %s", line);
    else log_debug("rdb driver: %s", line);
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

static const RdbHostAPI rdb_host_api = {
    sizeof(RdbHostAPI),
    RDB_HOST_API_VERSION,
    rdb_host_meta_alloc,
    rdb_host_meta_strdup,
    rdb_host_conn_register,
    rdb_host_log,
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

static void rdb_life_close_handle(RdbConnLife* life) {
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

void rdb_conn_life_close(RdbConnLife* life) {
    if (!life || life->closed) return;
    life->closed = true;
    if (!life->owner_closing) {
        // GC/teardown is a backstop, never the closing mechanism (S12.4.3)
        log_error("rdb-registry: closing connection rid=%u '%s' left open by its owner",
                  life->rid, life->uri);
    }
    // A synchronous row callback can trigger teardown while its statement is live.
    if (!life->active_calls) rdb_life_close_handle(life);
}

static void rdb_life_free(RdbConnLife* life) {
    if (!life) return;
    rdb_life_close_tunnels(life);   // a failed open may leave bridges behind
    if (life->stmts) arraylist_free(life->stmts);
    mem_free(life);
}

RdbConnLife* rdb_conn_life_hold(RdbConn* conn) {
    if (!conn || !conn->life || conn->life->closed) return NULL;
    conn->life->active_calls++;
    return conn->life;
}

void rdb_conn_life_release(RdbConnLife* life) {
    if (!life || !life->active_calls) return;
    life->active_calls--;
    if (!life->active_calls && life->closed) {
        rdb_life_close_handle(life);
        if (life->free_pending) rdb_life_free(life);
    }
}

static bool rdb_resolve_authorized(const RdbDriver* driver, const char* name, const char* uri,
                                   RdbTarget* targets, int* count) {
    memset(targets, 0, RDB_MAX_TARGETS * sizeof(RdbTarget));
    *count = 0;
    if (driver->resolve_targets(uri, targets, RDB_MAX_TARGETS, count) != RDB_OK ||
            *count <= 0 || *count > RDB_MAX_TARGETS) return false;
    return !rdb_registry_hooks || !rdb_registry_hooks->authorize ||
        rdb_registry_hooks->authorize(name, targets, *count) == RDB_OK;
}

bool rdb_target_authorized(const char* uri, const char* type) {
    if (!uri) return false;
    const char* name = type ? type : rdb_detect_driver(uri);
    const RdbDriver* driver = name ? rdb_get_driver(name) : NULL;
    if (!driver) return false;
    RdbTarget targets[RDB_MAX_TARGETS];
    int count = 0;
    return rdb_resolve_authorized(driver, name, uri, targets, &count);
}

struct RdbOpenWork {
    RdbConn* conn;
    RdbConnLife* life;
    RdbOpenCtx ctx;
    RdbTarget targets[RDB_MAX_TARGETS];
    RdbOpenOptions options;
    char* uri;
    void* handle;
    int status;
    bool ran;
};

static void rdb_open_wipe_uri(RdbOpenWork* work) {
    if (!work->uri) return;
    size_t length = strlen(work->uri);
    volatile char* bytes = work->uri;
    while (length--) *bytes++ = 0;
    mem_free(work->uri);
    work->uri = NULL;
}

static RdbOpenWork* rdb_open_work_prepare(Pool* pool, const char* uri, const char* type,
                                          bool readonly, bool sqlite_no_create, bool reserve) {
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

    RdbOpenWork* work = (RdbOpenWork*)mem_calloc(1, sizeof(RdbOpenWork), MEM_CAT_INPUT_OTHER);
    if (!work) return NULL;
    RdbConn* conn = (RdbConn*)pool_calloc(pool, sizeof(RdbConn));
    if (!conn) { mem_free(work); return NULL; }
    conn->driver   = driver;
    conn->pool     = pool;
    conn->readonly = readonly;
    conn->uri      = pool_strdup(pool, redacted);

    // JA16.3: resolve the URI into concrete targets and authorise them first
    int target_count = 0;
    if (!conn->uri || !rdb_resolve_authorized(driver, driver_name, uri, work->targets, &target_count)) {
        log_error("rdb: cannot resolve or authorize '%s'", redacted);
        mem_free(work);
        return NULL;
    }

    RdbConnLife* life = (RdbConnLife*)mem_calloc(1, sizeof(RdbConnLife), MEM_CAT_INPUT_OTHER);
    if (!life) { mem_free(work); return NULL; }
    life->driver = driver;
    life->stmts = arraylist_new(4);
    snprintf(life->uri, sizeof(life->uri), "%s", redacted);
    work->uri = mem_strdup(uri, MEM_CAT_INPUT_OTHER);
    if (!life->stmts || !work->uri) {
        rdb_open_wipe_uri(work);
        rdb_life_free(life);
        mem_free(work);
        return NULL;
    }
    // A pending driver call pins the registry record until its owner joins it.
    life->active_calls = 1;
    conn->life = life;
    work->conn = conn;
    work->life = life;
    work->ctx.conn = conn;
    work->ctx.life = life;
    work->ctx.targets = work->targets;
    work->ctx.target_count = target_count;
    work->ctx.reserved = reserve;
    work->options.struct_size = sizeof(RdbOpenOptions);
    work->options.readonly = readonly;
    work->options.sqlite_no_create = sqlite_no_create;
    work->status = RDB_ERROR;
    if (reserve && rdb_registry_hooks && rdb_registry_hooks->add &&
            rdb_registry_hooks->add(life, conn->uri, &life->rid, &life->registry) != RDB_OK) {
        rdb_open_wipe_uri(work);
        conn->life = NULL;
        rdb_life_free(life);
        mem_free(work);
        return NULL;
    }
    conn->rid = life->rid;
    if (life->closed) { (void)rdb_open_work_end(work, false); return NULL; }
    return work;
}

RdbOpenWork* rdb_open_work_begin(Pool* pool, const char* uri, const char* type, bool readonly) {
    return rdb_open_work_prepare(pool, uri, type, readonly, false, true);
}

int rdb_open_work_run(RdbOpenWork* work, const int* cancelled) {
    if (!work || work->ran) return RDB_ERROR;
    work->ran = true;
    if (cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) {
        rdb_open_wipe_uri(work);
        return RDB_ERROR;
    }
    const RdbDriver* driver = work->conn->driver;
    int rc = driver->open(&rdb_host_api, &work->ctx, work->uri, &work->options, &work->handle);
    rdb_open_wipe_uri(work);
    bool violation = rc == RDB_OK && (!work->ctx.registered || work->ctx.native != work->handle);
    if (violation) {
        log_error("rdb: driver '%s' opened '%s' without registering it", driver->name, work->conn->uri);
        if (work->handle && work->handle != work->ctx.native) driver->close(work->handle);
    } else if (rc != RDB_OK && work->ctx.registered) {
        // A failing driver owns and closes the native connection it registered.
        work->life->handle = NULL;
    }
    if (rc == RDB_OK && !violation &&
            (!cancelled || !__atomic_load_n(cancelled, __ATOMIC_ACQUIRE))) work->status = RDB_OK;
    return work->status;
}

RdbConn* rdb_open_work_end(RdbOpenWork* work, bool accept) {
    if (!work) return NULL;
    RdbConn* conn = work->conn;
    RdbConnLife* life = work->life;
    bool ok = accept && work->ran && work->status == RDB_OK && !life->closed;
    rdb_open_wipe_uri(work);
    if (ok) {
        conn->handle = work->handle;
        if (life->tunnel_count > 0) conn->info.tls = life->tls_guaranteed;
        rdb_conn_life_release(life);
        log_debug("rdb: opened '%s' with driver '%s' (readonly=%d, rid=%u)",
                  conn->uri, conn->driver->name, conn->readonly, conn->rid);
    } else {
        rdb_close(conn);
        rdb_conn_life_release(life);
        conn = NULL;
    }
    mem_free(work);
    return conn;
}

static RdbConn* rdb_open_impl(Pool* pool, const char* uri, const char* type, bool readonly,
                              bool sqlite_no_create) {
    RdbOpenWork* work = rdb_open_work_prepare(pool, uri, type, readonly, sqlite_no_create, false);
    if (!work) return NULL;
    (void)rdb_open_work_run(work, NULL);
    return rdb_open_work_end(work, true);
}

RdbConn* rdb_open(Pool* pool, const char* uri, const char* type, bool readonly) {
    return rdb_open_impl(pool, uri, type, readonly, false);
}

RdbConn* rdb_open_sqlite_existing(Pool* pool, const char* path) {
    return rdb_open_impl(pool, path, "sqlite", false, true);
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
    if (life->active_calls) life->free_pending = true;
    else rdb_life_free(life);
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

static int rdb_read_schema(const RdbDriver* driver, void* handle, Pool* pool, RdbSchema* schema) {
    memset(schema, 0, sizeof(*schema));
    int rc = driver->load_schema(handle, &rdb_host_api, pool, schema);
    if (rc == RDB_OK) {
        rdb_derive_link_names(pool, schema);
        rdb_build_reverse_fks(pool, schema);
    }
    return rc;
}

int rdb_load_schema(RdbConn* conn) {
    if (!rdb_conn_live(conn) || !conn->driver->load_schema) return RDB_ERROR;
    return rdb_read_schema(conn->driver, conn->handle, conn->pool, &conn->schema);
}

struct RdbSchemaWork {
    RdbConnLife* pin;
    const RdbDriver* driver;
    void* handle;
    Pool* pool;
    RdbSchema schema;
    int status;
    bool ran;
};

RdbSchemaWork* rdb_schema_work_begin(RdbConn* conn, Pool* result_pool) {
    if (!conn || !result_pool || !conn->driver || !conn->driver->load_schema) return NULL;
    RdbConnLife* pin = rdb_conn_life_hold(conn);
    if (!pin) return NULL;
    RdbSchemaWork* work = (RdbSchemaWork*)mem_calloc(1, sizeof(RdbSchemaWork), MEM_CAT_INPUT_OTHER);
    if (!work) { rdb_conn_life_release(pin); return NULL; }
    work->pin = pin;
    work->driver = conn->driver;
    work->handle = conn->handle;
    work->pool = result_pool;
    work->status = RDB_ERROR;
    return work;
}

int rdb_schema_work_run(RdbSchemaWork* work, const int* cancelled) {
    if (!work || work->ran) return RDB_ERROR;
    work->ran = true;
    if (cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) return RDB_ERROR;
    // The worker touches only its captured handle and private metadata pool.
    work->status = rdb_read_schema(work->driver, work->handle, work->pool, &work->schema);
    if (cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) work->status = RDB_ERROR;
    return work->status;
}

int rdb_schema_work_end(RdbSchemaWork* work, RdbSchema* out) {
    if (!work) return RDB_ERROR;
    int status = work->ran && !work->pin->closed ? work->status : RDB_ERROR;
    if (status == RDB_OK && out) *out = work->schema;
    rdb_conn_life_release(work->pin);
    mem_free(work);
    return status;
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

const char* rdb_type_name(RdbType type) {
    switch (type) {
        case RDB_TYPE_NULL: return "null";
        case RDB_TYPE_INT: return "integer";
        case RDB_TYPE_FLOAT: return "float";
        case RDB_TYPE_STRING: return "string";
        case RDB_TYPE_DECIMAL: return "decimal";
        case RDB_TYPE_DATETIME: return "datetime";
        case RDB_TYPE_JSON: return "json";
        case RDB_TYPE_BOOL: return "boolean";
        case RDB_TYPE_BLOB: return "binary";
        default: return "unknown";
    }
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

struct RdbPrepareWork {
    RdbConn* conn;
    RdbConnLife* pin;
    const RdbDriver* driver;
    void* connection;
    void* statement;
    char* sql;
    const RdbParam* params;
    int count;
    int status;
    bool ran;
    bool strict_params;
};

static RdbPrepareWork* rdb_prepare_work_create(RdbConn* conn, const char* sql,
                                               const RdbParam* params, int count, bool strict_params) {
    if (!sql || count < 0 || (count && !params) || !rdb_conn_live(conn)) return NULL;
    RdbConnLife* pin = rdb_conn_life_hold(conn);
    if (!pin) return NULL;
    RdbPrepareWork* work = (RdbPrepareWork*)mem_calloc(1, sizeof(RdbPrepareWork), MEM_CAT_INPUT_OTHER);
    if (!work) { rdb_conn_life_release(pin); return NULL; }
    work->sql = mem_strdup(sql, MEM_CAT_INPUT_OTHER);
    if (!work->sql) { mem_free(work); rdb_conn_life_release(pin); return NULL; }
    work->conn = conn;
    work->pin = pin;
    work->driver = conn->driver;
    work->connection = conn->handle;
    work->params = params;
    work->count = count;
    work->strict_params = strict_params;
    work->status = RDB_ERROR;
    return work;
}

RdbPrepareWork* rdb_prepare_work_begin(RdbConn* conn, const char* sql,
                                       const RdbParam* params, int count) {
    return rdb_prepare_work_create(conn, sql, params, count, true);
}

int rdb_prepare_work_run(RdbPrepareWork* work, const int* cancelled) {
    if (!work || work->ran) return RDB_ERROR;
    work->ran = true;
    if (cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) return RDB_ERROR;
    if (work->driver->prepare(work->connection, work->sql, &work->statement) != RDB_OK ||
            !work->statement || (work->count && !work->driver->bind_param)) return RDB_ERROR;
    if (work->strict_params && work->driver->struct_size >=
            offsetof(RdbDriver, parameter_count) + sizeof(work->driver->parameter_count) &&
            work->driver->parameter_count) {
        int expected = work->driver->parameter_count(work->statement);
        if (expected >= 0 && expected != work->count) return RDB_ERROR;
    }
    for (int i = 0; i < work->count; i++) {
        if ((cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) ||
                work->driver->bind_param(work->statement, i + 1, &work->params[i]) != RDB_OK)
            return RDB_ERROR;
    }
    if (cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) return RDB_ERROR;
    work->status = RDB_OK;
    return RDB_OK;
}

RdbStmt* rdb_prepare_work_end(RdbPrepareWork* work, bool accept) {
    if (!work) return NULL;
    RdbStmt* stmt = NULL;
    if (accept && work->ran && work->status == RDB_OK && !work->pin->closed &&
            work->conn->life == work->pin) {
        stmt = (RdbStmt*)pool_calloc(work->conn->pool, sizeof(RdbStmt));
        if (stmt && arraylist_append(work->pin->stmts, work->statement)) {
            stmt->conn = work->conn;
            stmt->handle = work->statement;
            work->statement = NULL;
        } else stmt = NULL;
    }
    if (work->statement) work->driver->finalize(work->statement);
    rdb_conn_life_release(work->pin);
    mem_free(work->sql);
    mem_free(work);
    return stmt;
}

RdbStmt* rdb_prepare(RdbConn* conn, const char* sql) {
    RdbPrepareWork* work = rdb_prepare_work_create(conn, sql, NULL, 0, false);
    if (!work) return NULL;
    (void)rdb_prepare_work_run(work, NULL);
    return rdb_prepare_work_end(work, true);
}

/** a statement is usable while its handle and connection are both live */
static bool rdb_stmt_live(const RdbStmt* stmt) {
    return stmt && stmt->handle && rdb_conn_live(stmt->conn);
}

struct RdbFetchWork {
    RdbConnLife* pin;
    const RdbDriver* driver;
    void* statement;
    int max_rows;
    int max_columns;
    Pool* pool;
    RdbRowFn row;
    void* user;
    RdbFetchResult result;
    int status;
    bool ran;
};

RdbFetchWork* rdb_fetch_work_begin(RdbStmt* stmt, int max_rows, int max_columns,
                                  Pool* result_pool, RdbRowFn row, void* user) {
    if (!rdb_stmt_live(stmt) || max_rows <= 0 || max_columns <= 0 || !result_pool || !row) return NULL;
    RdbConnLife* pin = rdb_conn_life_hold(stmt->conn);
    if (!pin) return NULL;
    RdbFetchWork* work = (RdbFetchWork*)mem_calloc(1, sizeof(RdbFetchWork), MEM_CAT_INPUT_OTHER);
    if (!work) { rdb_conn_life_release(pin); return NULL; }
    work->pin = pin;
    work->driver = stmt->conn->driver;
    work->statement = stmt->handle;
    work->max_rows = max_rows;
    work->max_columns = max_columns;
    work->pool = result_pool;
    work->row = row;
    work->user = user;
    work->status = RDB_ERROR;
    return work;
}

int rdb_fetch_work_run(RdbFetchWork* work, const int* cancelled) {
    if (!work || work->ran) return RDB_ERROR;
    work->ran = true;
    if ((cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) ||
            !work->driver->column_count || !work->driver->column_desc) return RDB_ERROR;
    int count = work->driver->column_count(work->statement);
    if (count < 0 || count > work->max_columns) return RDB_ERROR;
    work->result.column_count = count;
    if (count) {
        work->result.columns = (RdbColumnDesc*)pool_calloc(work->pool, (size_t)count * sizeof(RdbColumnDesc));
        if (!work->result.columns) return RDB_ERROR;
        for (int c = 0; c < count; c++) {
            RdbColumnDesc desc;
            if (work->driver->column_desc(work->statement, c, &desc) != RDB_OK ||
                    !desc.name || strlen(desc.name) > 4096) return RDB_ERROR;
            work->result.columns[c] = desc;
            work->result.columns[c].name = pool_strdup(work->pool, desc.name);
            if (!work->result.columns[c].name) return RDB_ERROR;
        }
    }
    RdbValue* values = count ? (RdbValue*)mem_calloc((size_t)count, sizeof(RdbValue), MEM_CAT_INPUT_OTHER) : NULL;
    if (count && !values) return RDB_ERROR;
    int rc = RDB_OK;
    while (work->result.row_count < work->max_rows) {
        if (cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) { rc = RDB_ERROR; break; }
        int step = work->driver->step(work->statement);
        if (step == RDB_DONE) { work->result.done = true; break; }
        if (step != RDB_ROW) { rc = step; break; }
        for (int c = 0; c < count; c++) values[c] = work->driver->column_value(work->statement, c);
        if (!work->row(work->user, count, values)) { rc = RDB_ERROR; break; }
        work->result.row_count++;
    }
    mem_free(values);
    if (cancelled && __atomic_load_n(cancelled, __ATOMIC_ACQUIRE)) rc = RDB_ERROR;
    work->status = rc;
    return rc;
}

int rdb_fetch_work_end(RdbFetchWork* work, RdbFetchResult* out) {
    if (!work) return RDB_ERROR;
    int status = work->ran && !work->pin->closed ? work->status : RDB_ERROR;
    if (status == RDB_OK && out) *out = work->result;
    rdb_conn_life_release(work->pin);
    mem_free(work);
    return status;
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

static void* rdb_detach_statement(RdbStmt* stmt) {
    if (!stmt) return NULL;
    rdb_batch_shred_free(stmt->shred);
    stmt->shred = NULL;
    if (!stmt->handle) return NULL;
    void* handle = stmt->handle;
    stmt->handle = NULL;
    // a closed connection already finalized every live statement
    if (!rdb_conn_live(stmt->conn)) return NULL;
    ArrayList* live = stmt->conn->life->stmts;
    for (int i = 0; i < live->length; i++) {
        if (live->data[i] == handle) {
            arraylist_remove(live, i);
            break;
        }
    }
    return handle;
}

struct RdbFinalizeWork {
    RdbConnLife* pin;
    const RdbDriver* driver;
    void* statement;
    bool ran;
};

RdbFinalizeWork* rdb_finalize_work_begin(RdbStmt* stmt) {
    if (!rdb_stmt_live(stmt)) return NULL;
    RdbConnLife* pin = rdb_conn_life_hold(stmt->conn);
    if (!pin) return NULL;
    RdbFinalizeWork* work = (RdbFinalizeWork*)mem_calloc(1, sizeof(RdbFinalizeWork), MEM_CAT_INPUT_OTHER);
    if (!work) { rdb_conn_life_release(pin); return NULL; }
    work->pin = pin;
    work->driver = stmt->conn->driver;
    work->statement = rdb_detach_statement(stmt);
    if (!work->statement) { rdb_conn_life_release(pin); mem_free(work); return NULL; }
    return work;
}

void rdb_finalize_work_run(RdbFinalizeWork* work) {
    if (!work || work->ran) return;
    work->ran = true;
    work->driver->finalize(work->statement);
}

void rdb_finalize_work_end(RdbFinalizeWork* work) {
    if (!work) return;
    // A cancelled queued job still has to release its native statement.
    if (!work->ran) rdb_finalize_work_run(work);
    rdb_conn_life_release(work->pin);
    mem_free(work);
}

void rdb_finalize(RdbStmt* stmt) {
    const RdbDriver* driver = stmt && stmt->conn ? stmt->conn->driver : NULL;
    void* handle = rdb_detach_statement(stmt);
    if (handle && driver) driver->finalize(handle);
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
