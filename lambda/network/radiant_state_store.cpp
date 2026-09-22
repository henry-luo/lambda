// radiant_state_store.cpp
// One SQLite authority for Radiant browsing and agent profile state.

#include "radiant_state_store.h"

#include "../../lib/arraylist.h"
#include "../../lib/file.h"
#include "../../lib/log.h"
#include "../../lib/mem.h"
#include "../../lib/uuid.h"
#include "../../lib/sqlite/sqlite3.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

enum {
    RADIANT_STATE_SCHEMA_VERSION = 1,
    RADIANT_STATE_ORIGIN_LIMIT_BYTES = 5 * 1024 * 1024,
    RADIANT_STATE_PROFILE_LIMIT_BYTES = 50 * 1024 * 1024,
    RADIANT_STATE_HISTORY_LIMIT = 100,
    RADIANT_STATE_VISIT_LIMIT = 10000
};

typedef enum StateCookieCommandKind {
    STATE_COOKIE_UPSERT = 0,
    STATE_COOKIE_DELETE = 1
} StateCookieCommandKind;

typedef struct StateCookieCommand {
    StateCookieCommandKind kind;
    RadiantStateCookie cookie;
    char* name;
    char* domain;
    char* path;
} StateCookieCommand;

struct RadiantStateStore {
    sqlite3* db;
    char* path;
    char* profile_name;
    char session_id[UUID_STR_LEN];
    char* browsing_context_id;
    int64_t profile_id;
    pthread_t owner_thread;
    ArrayList* pending_cookie_commands;
    pthread_mutex_t pending_cookie_lock;
};

static int64_t state_now_ms(void) {
    return (int64_t)time(NULL) * 1000;
}

static void state_make_id(char out[UUID_STR_LEN]) {
    uint8_t bytes[16] = {};
    sqlite3_randomness((int)sizeof(bytes), bytes);
    uuid_v4_format(bytes, out);
}

static bool state_is_owner(const RadiantStateStore* store) {
    if (!store) return false;
    if (pthread_equal(store->owner_thread, pthread_self()) != 0) return true;
    log_error("radiant_state: SQLite used outside its owner thread");
    return false;
}

static bool state_exec(RadiantStateStore* store, const char* sql) {
    if (!store || !state_is_owner(store)) return false;
    char* error = NULL;
    int rc = sqlite3_exec(store->db, sql, NULL, NULL, &error);
    if (rc == SQLITE_OK) return true;
    log_error("radiant_state: SQL failed: %s", error ? error : sqlite3_errmsg(store->db));
    sqlite3_free(error);
    return false;
}

static bool state_prepare(RadiantStateStore* store, const char* sql,
                          sqlite3_stmt** out_statement) {
    if (!store || !state_is_owner(store) || !out_statement) return false;
    *out_statement = NULL;
    int rc = sqlite3_prepare_v2(store->db, sql, -1, out_statement, NULL);
    if (rc == SQLITE_OK) return true;
    log_error("radiant_state: prepare failed: %s", sqlite3_errmsg(store->db));
    return false;
}

static bool state_step_done(RadiantStateStore* store, sqlite3_stmt* statement) {
    int rc = sqlite3_step(statement);
    if (rc == SQLITE_DONE) return true;
    log_error("radiant_state: statement failed: %s", sqlite3_errmsg(store->db));
    return false;
}

static bool state_begin(RadiantStateStore* store) {
    return state_exec(store, "BEGIN IMMEDIATE");
}

static void state_rollback(RadiantStateStore* store) {
    (void)state_exec(store, "ROLLBACK");
}

static bool state_commit(RadiantStateStore* store) {
    return state_exec(store, "COMMIT");
}

static char* state_copy_text(const unsigned char* value) {
    return value ? mem_strdup((const char*)value, MEM_CAT_NETWORK) : NULL;
}

static void state_cookie_command_free(StateCookieCommand* command) {
    if (!command) return;
    mem_free((char*)command->cookie.name);
    mem_free((char*)command->cookie.value);
    mem_free((char*)command->cookie.domain);
    mem_free((char*)command->cookie.path);
    mem_free(command->name);
    mem_free(command->domain);
    mem_free(command->path);
    mem_free(command);
}

static StateCookieCommand* state_cookie_command_new(StateCookieCommandKind kind) {
    StateCookieCommand* command =
        (StateCookieCommand*)mem_calloc(1, sizeof(StateCookieCommand), MEM_CAT_NETWORK);
    if (command) {
        // A zeroed delete command otherwise executes as an empty upsert.
        command->kind = kind;
    }
    return command;
}

static bool state_schema_create(RadiantStateStore* store) {
    static const char* schema_sql =
        "CREATE TABLE IF NOT EXISTS profiles ("
        "profile_id INTEGER PRIMARY KEY, profile_name TEXT NOT NULL UNIQUE,"
        "created_at_ms INTEGER NOT NULL, last_opened_at_ms INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS browsing_contexts ("
        "context_id TEXT PRIMARY KEY, profile_id INTEGER NOT NULL REFERENCES profiles(profile_id),"
        "session_id TEXT NOT NULL, kind TEXT NOT NULL, current_index INTEGER NOT NULL DEFAULT -1,"
        "is_open INTEGER NOT NULL CHECK (is_open IN (0,1)), created_at_ms INTEGER NOT NULL,"
        "updated_at_ms INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS visits ("
        "visit_id INTEGER PRIMARY KEY, profile_id INTEGER NOT NULL REFERENCES profiles(profile_id),"
        "context_id TEXT NOT NULL REFERENCES browsing_contexts(context_id), url TEXT NOT NULL,"
        "title TEXT, transition TEXT NOT NULL, visited_at_ms INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS navigation_entries ("
        "context_id TEXT NOT NULL REFERENCES browsing_contexts(context_id), entry_index INTEGER NOT NULL,"
        "visit_id INTEGER NOT NULL REFERENCES visits(visit_id), url TEXT NOT NULL, title TEXT,"
        "scroll_x REAL NOT NULL DEFAULT 0, scroll_y REAL NOT NULL DEFAULT 0,"
        "PRIMARY KEY(context_id, entry_index));"
        "CREATE TABLE IF NOT EXISTS cookies ("
        "profile_id INTEGER NOT NULL REFERENCES profiles(profile_id), name TEXT NOT NULL, value TEXT NOT NULL,"
        "domain TEXT NOT NULL, path TEXT NOT NULL, expires_at_ms INTEGER,"
        "secure INTEGER NOT NULL CHECK (secure IN (0,1)), http_only INTEGER NOT NULL CHECK (http_only IN (0,1)),"
        "same_site INTEGER NOT NULL, creation_at_ms INTEGER NOT NULL, session_id TEXT,"
        "PRIMARY KEY(profile_id,name,domain,path));"
        "CREATE TABLE IF NOT EXISTS local_storage ("
        "profile_id INTEGER NOT NULL REFERENCES profiles(profile_id), origin TEXT NOT NULL, key TEXT NOT NULL,"
        "value TEXT NOT NULL, ordinal INTEGER NOT NULL, modified_at_ms INTEGER NOT NULL,"
        "PRIMARY KEY(profile_id,origin,key));"
        "CREATE TABLE IF NOT EXISTS session_storage ("
        "session_id TEXT NOT NULL, context_id TEXT NOT NULL REFERENCES browsing_contexts(context_id),"
        "origin TEXT NOT NULL, key TEXT NOT NULL, value TEXT NOT NULL, ordinal INTEGER NOT NULL,"
        "modified_at_ms INTEGER NOT NULL, PRIMARY KEY(session_id,context_id,origin,key));"
        "CREATE INDEX IF NOT EXISTS visits_profile_time ON visits(profile_id,visited_at_ms DESC);"
        "CREATE INDEX IF NOT EXISTS cookies_profile_expiry ON cookies(profile_id,expires_at_ms);"
        "CREATE INDEX IF NOT EXISTS local_storage_lookup ON local_storage(profile_id,origin,ordinal);"
        "CREATE INDEX IF NOT EXISTS session_storage_lookup ON session_storage(session_id,context_id,origin,ordinal);";
    if (!state_exec(store, schema_sql)) return false;
    char version_sql[64] = {};
    snprintf(version_sql, sizeof(version_sql), "PRAGMA user_version=%d", RADIANT_STATE_SCHEMA_VERSION);
    return state_exec(store, version_sql);
}

static bool state_schema_migrate(RadiantStateStore* store) {
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store, "PRAGMA user_version", &statement)) return false;
    int version = -1;
    if (sqlite3_step(statement) == SQLITE_ROW) version = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    if (version < 0 || version > RADIANT_STATE_SCHEMA_VERSION) {
        log_error("radiant_state: unsupported schema version %d", version);
        return false;
    }
    // v1 is idempotent so interrupted first creation is recovered by the
    // surrounding transaction without accepting a newer unknown schema.
    return state_schema_create(store);
}

static bool state_integrity_ok(RadiantStateStore* store) {
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store, "PRAGMA integrity_check", &statement)) return false;
    bool valid = false;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* result = sqlite3_column_text(statement, 0);
        valid = result && strcmp((const char*)result, "ok") == 0;
    }
    sqlite3_finalize(statement);
    if (!valid) log_error("radiant_state: integrity check failed");
    return valid;
}

static bool state_select_or_create_profile(RadiantStateStore* store) {
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store, "SELECT profile_id FROM profiles WHERE profile_name=?1", &statement)) return false;
    sqlite3_bind_text(statement, 1, store->profile_name, -1, SQLITE_STATIC);
    int rc = sqlite3_step(statement);
    if (rc == SQLITE_ROW) {
        store->profile_id = sqlite3_column_int64(statement, 0);
        sqlite3_finalize(statement);
    } else {
        sqlite3_finalize(statement);
        if (rc != SQLITE_DONE) return false;
        if (!state_prepare(store,
                "INSERT INTO profiles(profile_name,created_at_ms,last_opened_at_ms) VALUES(?1,?2,?2)",
                &statement)) return false;
        sqlite3_bind_text(statement, 1, store->profile_name, -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 2, state_now_ms());
        if (!state_step_done(store, statement)) {
            sqlite3_finalize(statement);
            return false;
        }
        sqlite3_finalize(statement);
        store->profile_id = sqlite3_last_insert_rowid(store->db);
    }
    if (!state_prepare(store, "UPDATE profiles SET last_opened_at_ms=?1 WHERE profile_id=?2", &statement)) return false;
    sqlite3_bind_int64(statement, 1, state_now_ms());
    sqlite3_bind_int64(statement, 2, store->profile_id);
    bool updated = state_step_done(store, statement);
    sqlite3_finalize(statement);
    return updated;
}

static bool state_clear_stale_session_rows(RadiantStateStore* store) {
    sqlite3_stmt* statement = NULL;
    // A live process may have several profiles open against the same file.
    // Only reap data whose browsing context was cleanly closed; a fresh
    // session ID already prevents stale rows from being observed after a
    // crash without disrupting another active profile.
    if (!state_prepare(store,
            "DELETE FROM session_storage WHERE session_id IN ("
            "SELECT session_id FROM browsing_contexts WHERE is_open=0)",
            &statement)) return false;
    bool ok = state_step_done(store, statement);
    sqlite3_finalize(statement);
    if (!ok) return false;
    if (!state_prepare(store,
            "DELETE FROM cookies WHERE expires_at_ms IS NULL AND session_id IN ("
            "SELECT session_id FROM browsing_contexts WHERE is_open=0)",
            &statement)) return false;
    ok = state_step_done(store, statement);
    sqlite3_finalize(statement);
    return ok;
}

static bool state_cookie_upsert_now(RadiantStateStore* store,
                                    const RadiantStateCookie* cookie) {
    sqlite3_stmt* statement = NULL;
    static const char* sql =
        "INSERT INTO cookies(profile_id,name,value,domain,path,expires_at_ms,secure,http_only,same_site,creation_at_ms,session_id)"
        " VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)"
        " ON CONFLICT(profile_id,name,domain,path) DO UPDATE SET value=excluded.value,"
        "expires_at_ms=excluded.expires_at_ms,secure=excluded.secure,http_only=excluded.http_only,"
        "same_site=excluded.same_site,session_id=excluded.session_id";
    if (!state_prepare(store, sql, &statement)) return false;
    sqlite3_bind_int64(statement, 1, store->profile_id);
    sqlite3_bind_text(statement, 2, cookie->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 3, cookie->value, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 4, cookie->domain, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 5, cookie->path, -1, SQLITE_STATIC);
    if (cookie->expires > 0) sqlite3_bind_int64(statement, 6, (int64_t)cookie->expires * 1000);
    else sqlite3_bind_null(statement, 6);
    sqlite3_bind_int(statement, 7, cookie->secure ? 1 : 0);
    sqlite3_bind_int(statement, 8, cookie->http_only ? 1 : 0);
    sqlite3_bind_int(statement, 9, cookie->same_site);
    sqlite3_bind_int64(statement, 10, (int64_t)cookie->creation_time * 1000);
    if (cookie->expires > 0) sqlite3_bind_null(statement, 11);
    else sqlite3_bind_text(statement, 11, store->session_id, -1, SQLITE_STATIC);
    bool ok = state_step_done(store, statement);
    sqlite3_finalize(statement);
    return ok;
}

static bool state_cookie_delete_now(RadiantStateStore* store, const char* name,
                                    const char* domain, const char* path) {
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store,
            "DELETE FROM cookies WHERE profile_id=?1 AND name=?2 AND domain=?3 AND path=?4",
            &statement)) return false;
    sqlite3_bind_int64(statement, 1, store->profile_id);
    sqlite3_bind_text(statement, 2, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 3, domain, -1, SQLITE_STATIC);
    sqlite3_bind_text(statement, 4, path, -1, SQLITE_STATIC);
    bool ok = state_step_done(store, statement);
    sqlite3_finalize(statement);
    return ok;
}

static const char* state_storage_table(RadiantStateStorageScope scope) {
    return scope == RADIANT_STATE_STORAGE_LOCAL ? "local_storage" : "session_storage";
}

static bool state_storage_scope_valid(RadiantStateStorageScope scope, const char* context_id) {
    return scope == RADIANT_STATE_STORAGE_LOCAL ||
        (scope == RADIANT_STATE_STORAGE_SESSION && context_id && context_id[0]);
}

static bool state_storage_size(RadiantStateStore* store, RadiantStateStorageScope scope,
                               const char* context_id, const char* origin,
                               int64_t* out_origin_bytes, int64_t* out_profile_bytes) {
    if (!out_origin_bytes || !out_profile_bytes) return false;
    *out_origin_bytes = 0;
    *out_profile_bytes = 0;
    const char* table = state_storage_table(scope);
    char sql[512] = {};
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        snprintf(sql, sizeof(sql),
                 "SELECT COALESCE(SUM(length(key)+length(value)),0) FROM %s WHERE profile_id=?1 AND origin=?2", table);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT COALESCE(SUM(length(key)+length(value)),0) FROM %s WHERE session_id=?1 AND context_id=?2 AND origin=?3", table);
    }
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store, sql, &statement)) return false;
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        sqlite3_bind_int64(statement, 1, store->profile_id);
        sqlite3_bind_text(statement, 2, origin, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_text(statement, 1, store->session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 2, context_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, origin, -1, SQLITE_STATIC);
    }
    bool ok = sqlite3_step(statement) == SQLITE_ROW;
    if (ok) *out_origin_bytes = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    if (!ok || scope != RADIANT_STATE_STORAGE_LOCAL) return ok;
    if (!state_prepare(store,
            "SELECT COALESCE(SUM(length(key)+length(value)),0) FROM local_storage WHERE profile_id=?1",
            &statement)) return false;
    sqlite3_bind_int64(statement, 1, store->profile_id);
    ok = sqlite3_step(statement) == SQLITE_ROW;
    if (ok) *out_profile_bytes = sqlite3_column_int64(statement, 0);
    sqlite3_finalize(statement);
    return ok;
}

static bool state_storage_existing_bytes(RadiantStateStore* store,
                                         RadiantStateStorageScope scope,
                                         const char* context_id, const char* origin,
                                         const char* key, int64_t* out_bytes) {
    *out_bytes = 0;
    const char* table = state_storage_table(scope);
    char sql[512] = {};
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        snprintf(sql, sizeof(sql),
                 "SELECT length(key)+length(value) FROM %s WHERE profile_id=?1 AND origin=?2 AND key=?3", table);
    } else {
        snprintf(sql, sizeof(sql),
                 "SELECT length(key)+length(value) FROM %s WHERE session_id=?1 AND context_id=?2 AND origin=?3 AND key=?4", table);
    }
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store, sql, &statement)) return false;
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        sqlite3_bind_int64(statement, 1, store->profile_id);
        sqlite3_bind_text(statement, 2, origin, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, key, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_text(statement, 1, store->session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 2, context_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, origin, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 4, key, -1, SQLITE_STATIC);
    }
    int rc = sqlite3_step(statement);
    if (rc == SQLITE_ROW) *out_bytes = sqlite3_column_int64(statement, 0);
    bool ok = rc == SQLITE_ROW || rc == SQLITE_DONE;
    sqlite3_finalize(statement);
    return ok;
}

extern "C" RadiantStateStore* radiant_state_store_open(const char* cache_dir,
                                                         const char* profile_name) {
    if (!cache_dir || !cache_dir[0]) return NULL;
    if (file_ensure_dir(cache_dir) != 0) {
        log_error("radiant_state: unable to create cache directory %s", cache_dir);
        return NULL;
    }
    RadiantStateStore* store = (RadiantStateStore*)mem_calloc(
        1, sizeof(RadiantStateStore), MEM_CAT_NETWORK);
    if (!store) return NULL;
    store->path = file_path_join(cache_dir, "radiant_state.sqlite3");
    store->profile_name = mem_strdup(profile_name && profile_name[0] ? profile_name : "default",
                                     MEM_CAT_NETWORK);
    store->owner_thread = pthread_self();
    state_make_id(store->session_id);
    if (!store->path || !store->profile_name ||
        pthread_mutex_init(&store->pending_cookie_lock, NULL) != 0) {
        mem_free(store->path);
        mem_free(store->profile_name);
        mem_free(store);
        return NULL;
    }
    int rc = sqlite3_open_v2(store->path, &store->db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        log_error("radiant_state: unable to open %s: %s", store->path,
                  store->db ? sqlite3_errmsg(store->db) : "unknown error");
        if (store->db) sqlite3_close(store->db);
        pthread_mutex_destroy(&store->pending_cookie_lock);
        mem_free(store->path);
        mem_free(store->profile_name);
        mem_free(store);
        return NULL;
    }
    sqlite3_busy_timeout(store->db, 2000);
    bool transaction_open = false;
    bool ready = state_exec(store, "PRAGMA foreign_keys=ON") &&
        state_exec(store, "PRAGMA journal_mode=DELETE") &&
        state_exec(store, "PRAGMA synchronous=FULL") && state_integrity_ok(store);
    if (ready) transaction_open = state_begin(store);
    ready = ready && transaction_open && state_schema_migrate(store) &&
        state_select_or_create_profile(store) && state_clear_stale_session_rows(store) &&
        state_commit(store);
    if (!ready) {
        if (transaction_open) state_rollback(store);
        sqlite3_close(store->db);
        pthread_mutex_destroy(&store->pending_cookie_lock);
        mem_free(store->path);
        mem_free(store->profile_name);
        mem_free(store);
        return NULL;
    }
    log_info("radiant_state: opened profile %s at %s", store->profile_name, store->path);
    return store;
}

extern "C" void radiant_state_store_close(RadiantStateStore* store) {
    if (!store) return;
    if (state_is_owner(store)) {
        (void)radiant_state_store_flush(store);
        if (state_begin(store)) {
            sqlite3_stmt* statement = NULL;
            bool ok = state_prepare(store, "DELETE FROM session_storage WHERE session_id=?1", &statement);
            if (ok) {
                sqlite3_bind_text(statement, 1, store->session_id, -1, SQLITE_STATIC);
                ok = state_step_done(store, statement);
                sqlite3_finalize(statement);
            }
            if (ok) ok = state_prepare(store, "DELETE FROM cookies WHERE session_id=?1", &statement);
            if (ok) {
                sqlite3_bind_text(statement, 1, store->session_id, -1, SQLITE_STATIC);
                ok = state_step_done(store, statement);
                sqlite3_finalize(statement);
            }
            if (ok && store->browsing_context_id) {
                ok = state_prepare(store,
                    "UPDATE browsing_contexts SET is_open=0,updated_at_ms=?1 WHERE context_id=?2", &statement);
                if (ok) {
                    sqlite3_bind_int64(statement, 1, state_now_ms());
                    sqlite3_bind_text(statement, 2, store->browsing_context_id, -1, SQLITE_STATIC);
                    ok = state_step_done(store, statement);
                    sqlite3_finalize(statement);
                }
            }
            if (ok) (void)state_commit(store);
            else state_rollback(store);
        }
    }
    pthread_mutex_lock(&store->pending_cookie_lock);
    if (store->pending_cookie_commands) {
        for (int i = 0; i < store->pending_cookie_commands->length; i++) {
            state_cookie_command_free((StateCookieCommand*)arraylist_get(store->pending_cookie_commands, i));
        }
        arraylist_free(store->pending_cookie_commands);
    }
    pthread_mutex_unlock(&store->pending_cookie_lock);
    pthread_mutex_destroy(&store->pending_cookie_lock);
    if (store->db) sqlite3_close(store->db);
    mem_free(store->path);
    mem_free(store->profile_name);
    mem_free(store->browsing_context_id);
    mem_free(store);
}

extern "C" const char* radiant_state_store_path(const RadiantStateStore* store) {
    return store ? store->path : NULL;
}

extern "C" const char* radiant_state_store_session_id(const RadiantStateStore* store) {
    return store ? store->session_id : NULL;
}

extern "C" const char* radiant_state_store_open_browsing_context(RadiantStateStore* store,
                                                                   int* out_current_index) {
    if (out_current_index) *out_current_index = -1;
    if (!store || !state_is_owner(store)) return NULL;
    if (store->browsing_context_id) {
        if (out_current_index) {
            sqlite3_stmt* statement = NULL;
            if (state_prepare(store, "SELECT current_index FROM browsing_contexts WHERE context_id=?1", &statement)) {
                sqlite3_bind_text(statement, 1, store->browsing_context_id, -1, SQLITE_STATIC);
                if (sqlite3_step(statement) == SQLITE_ROW) *out_current_index = sqlite3_column_int(statement, 0);
                sqlite3_finalize(statement);
            }
        }
        return store->browsing_context_id;
    }
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store,
            "SELECT context_id,current_index FROM browsing_contexts WHERE profile_id=?1 ORDER BY updated_at_ms DESC LIMIT 1",
            &statement)) return NULL;
    sqlite3_bind_int64(statement, 1, store->profile_id);
    if (sqlite3_step(statement) == SQLITE_ROW) {
        store->browsing_context_id = state_copy_text(sqlite3_column_text(statement, 0));
        if (out_current_index) *out_current_index = sqlite3_column_int(statement, 1);
    }
    sqlite3_finalize(statement);
    if (!store->browsing_context_id) {
        char id[UUID_STR_LEN] = {};
        state_make_id(id);
        store->browsing_context_id = mem_strdup(id, MEM_CAT_NETWORK);
        if (!store->browsing_context_id || !state_prepare(store,
                "INSERT INTO browsing_contexts(context_id,profile_id,session_id,kind,current_index,is_open,created_at_ms,updated_at_ms)"
                " VALUES(?1,?2,?3,'interactive',-1,1,?4,?4)", &statement)) {
            mem_free(store->browsing_context_id);
            store->browsing_context_id = NULL;
            return NULL;
        }
        sqlite3_bind_text(statement, 1, store->browsing_context_id, -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 2, store->profile_id);
        sqlite3_bind_text(statement, 3, store->session_id, -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 4, state_now_ms());
        bool inserted = state_step_done(store, statement);
        sqlite3_finalize(statement);
        if (!inserted) {
            mem_free(store->browsing_context_id);
            store->browsing_context_id = NULL;
            return NULL;
        }
    } else if (state_prepare(store,
            "UPDATE browsing_contexts SET session_id=?1,is_open=1,updated_at_ms=?2 WHERE context_id=?3",
            &statement)) {
        sqlite3_bind_text(statement, 1, store->session_id, -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 2, state_now_ms());
        sqlite3_bind_text(statement, 3, store->browsing_context_id, -1, SQLITE_STATIC);
        if (!state_step_done(store, statement)) {
            sqlite3_finalize(statement);
            mem_free(store->browsing_context_id);
            store->browsing_context_id = NULL;
            return NULL;
        }
        sqlite3_finalize(statement);
    } else {
        mem_free(store->browsing_context_id);
        store->browsing_context_id = NULL;
        return NULL;
    }
    return store->browsing_context_id;
}

extern "C" bool radiant_state_store_history_load(RadiantStateStore* store,
                                                   const char* context_id,
                                                   RadiantStateHistoryVisitor visitor,
                                                   void* user_data) {
    if (!store || !context_id || !visitor || !state_is_owner(store)) return false;
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store,
            "SELECT url,title,scroll_x,scroll_y FROM navigation_entries WHERE context_id=?1 ORDER BY entry_index",
            &statement)) return false;
    sqlite3_bind_text(statement, 1, context_id, -1, SQLITE_STATIC);
    bool ok = true;
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        const char* url = (const char*)sqlite3_column_text(statement, 0);
        const char* title = (const char*)sqlite3_column_text(statement, 1);
        if (!visitor(url ? url : "", title, (float)sqlite3_column_double(statement, 2),
                     (float)sqlite3_column_double(statement, 3), user_data)) {
            ok = false;
            break;
        }
    }
    if (rc != SQLITE_DONE && ok) {
        log_error("radiant_state: history read failed: %s", sqlite3_errmsg(store->db));
        ok = false;
    }
    sqlite3_finalize(statement);
    return ok;
}

extern "C" bool radiant_state_store_history_append(RadiantStateStore* store,
                                                     const char* context_id,
                                                     const char* url, const char* title,
                                                     float scroll_x, float scroll_y,
                                                     const char* transition,
                                                     int* out_current_index) {
    if (out_current_index) *out_current_index = -1;
    if (!store || !context_id || !url || !state_is_owner(store) || !state_begin(store)) return false;
    sqlite3_stmt* statement = NULL;
    int current_index = -1;
    bool ok = state_prepare(store, "SELECT current_index FROM browsing_contexts WHERE context_id=?1", &statement);
    if (ok) {
        sqlite3_bind_text(statement, 1, context_id, -1, SQLITE_STATIC);
        ok = sqlite3_step(statement) == SQLITE_ROW;
        if (ok) current_index = sqlite3_column_int(statement, 0);
        sqlite3_finalize(statement);
    }
    if (ok) ok = state_prepare(store,
        "DELETE FROM navigation_entries WHERE context_id=?1 AND entry_index>?2", &statement);
    if (ok) {
        sqlite3_bind_text(statement, 1, context_id, -1, SQLITE_STATIC);
        sqlite3_bind_int(statement, 2, current_index);
        ok = state_step_done(store, statement);
        sqlite3_finalize(statement);
    }
    if (ok) ok = state_prepare(store,
        "INSERT INTO visits(profile_id,context_id,url,title,transition,visited_at_ms) VALUES(?1,?2,?3,?4,?5,?6)",
        &statement);
    if (ok) {
        sqlite3_bind_int64(statement, 1, store->profile_id);
        sqlite3_bind_text(statement, 2, context_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, url, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 4, title, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 5, transition ? transition : "link", -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 6, state_now_ms());
        ok = state_step_done(store, statement);
        sqlite3_finalize(statement);
    }
    int64_t visit_id = ok ? sqlite3_last_insert_rowid(store->db) : 0;
    int entry_index = current_index + 1;
    if (ok) ok = state_prepare(store,
        "INSERT INTO navigation_entries(context_id,entry_index,visit_id,url,title,scroll_x,scroll_y) VALUES(?1,?2,?3,?4,?5,?6,?7)",
        &statement);
    if (ok) {
        sqlite3_bind_text(statement, 1, context_id, -1, SQLITE_STATIC);
        sqlite3_bind_int(statement, 2, entry_index);
        sqlite3_bind_int64(statement, 3, visit_id);
        sqlite3_bind_text(statement, 4, url, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 5, title, -1, SQLITE_STATIC);
        sqlite3_bind_double(statement, 6, scroll_x);
        sqlite3_bind_double(statement, 7, scroll_y);
        ok = state_step_done(store, statement);
        sqlite3_finalize(statement);
    }
    if (ok && entry_index >= RADIANT_STATE_HISTORY_LIMIT) {
        ok = state_prepare(store,
            "DELETE FROM navigation_entries WHERE context_id=?1 AND entry_index<?2", &statement);
    }
    if (ok && entry_index >= RADIANT_STATE_HISTORY_LIMIT) {
        sqlite3_bind_text(statement, 1, context_id, -1, SQLITE_STATIC);
        sqlite3_bind_int(statement, 2, entry_index - RADIANT_STATE_HISTORY_LIMIT + 1);
        ok = state_step_done(store, statement);
        sqlite3_finalize(statement);
        if (ok) {
            // Use a negative staging range so the UNIQUE context/index key
            // cannot collide while the retained entries are re-numbered.
            ok = state_prepare(store,
                "UPDATE navigation_entries SET entry_index=-entry_index-1 WHERE context_id=?1", &statement);
            if (ok) {
                sqlite3_bind_text(statement, 1, context_id, -1, SQLITE_STATIC);
                ok = state_step_done(store, statement);
                sqlite3_finalize(statement);
            }
            if (ok) ok = state_prepare(store,
                "UPDATE navigation_entries SET entry_index=-entry_index-2 WHERE context_id=?1", &statement);
            if (ok) {
                sqlite3_bind_text(statement, 1, context_id, -1, SQLITE_STATIC);
                ok = state_step_done(store, statement);
                sqlite3_finalize(statement);
            }
            entry_index--;
        }
    }
    if (ok) ok = state_prepare(store,
        "UPDATE browsing_contexts SET current_index=?1,updated_at_ms=?2 WHERE context_id=?3", &statement);
    if (ok) {
        sqlite3_bind_int(statement, 1, entry_index);
        sqlite3_bind_int64(statement, 2, state_now_ms());
        sqlite3_bind_text(statement, 3, context_id, -1, SQLITE_STATIC);
        ok = state_step_done(store, statement);
        sqlite3_finalize(statement);
    }
    if (ok) ok = state_prepare(store,
        "DELETE FROM visits WHERE visit_id IN (SELECT visit_id FROM visits "
        "WHERE profile_id=?1 ORDER BY visited_at_ms DESC LIMIT -1 OFFSET ?2) "
        "AND visit_id NOT IN (SELECT visit_id FROM navigation_entries)", &statement);
    if (ok) {
        sqlite3_bind_int64(statement, 1, store->profile_id);
        sqlite3_bind_int(statement, 2, RADIANT_STATE_VISIT_LIMIT);
        ok = state_step_done(store, statement);
        sqlite3_finalize(statement);
    }
    if (ok) ok = state_commit(store);
    else state_rollback(store);
    if (ok && out_current_index) *out_current_index = entry_index;
    return ok;
}

extern "C" bool radiant_state_store_history_select(RadiantStateStore* store,
                                                     const char* context_id,
                                                     int current_index) {
    if (!store || !context_id || current_index < 0 || !state_is_owner(store)) return false;
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store,
            "UPDATE browsing_contexts SET current_index=?1,updated_at_ms=?2 WHERE context_id=?3", &statement)) return false;
    sqlite3_bind_int(statement, 1, current_index);
    sqlite3_bind_int64(statement, 2, state_now_ms());
    sqlite3_bind_text(statement, 3, context_id, -1, SQLITE_STATIC);
    bool ok = state_step_done(store, statement);
    sqlite3_finalize(statement);
    return ok;
}

extern "C" bool radiant_state_store_history_update_current(RadiantStateStore* store,
                                                             const char* context_id,
                                                             const char* title,
                                                             float scroll_x, float scroll_y) {
    if (!store || !context_id || !state_is_owner(store)) return false;
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store,
            "UPDATE navigation_entries SET title=?1,scroll_x=?2,scroll_y=?3 "
            "WHERE context_id=?4 AND entry_index=(SELECT current_index FROM browsing_contexts WHERE context_id=?4)",
            &statement)) return false;
    sqlite3_bind_text(statement, 1, title, -1, SQLITE_STATIC);
    sqlite3_bind_double(statement, 2, scroll_x);
    sqlite3_bind_double(statement, 3, scroll_y);
    sqlite3_bind_text(statement, 4, context_id, -1, SQLITE_STATIC);
    bool ok = state_step_done(store, statement);
    sqlite3_finalize(statement);
    return ok;
}

extern "C" bool radiant_state_store_load_cookies(RadiantStateStore* store,
                                                   RadiantStateCookieVisitor visitor,
                                                   void* user_data) {
    if (!store || !visitor || !state_is_owner(store)) return false;
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store,
            "SELECT name,value,domain,path,expires_at_ms,secure,http_only,same_site,creation_at_ms "
            "FROM cookies WHERE profile_id=?1 AND expires_at_ms>?2",
            &statement)) return false;
    sqlite3_bind_int64(statement, 1, store->profile_id);
    sqlite3_bind_int64(statement, 2, state_now_ms());
    bool ok = true;
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        RadiantStateCookie cookie = {};
        cookie.name = (const char*)sqlite3_column_text(statement, 0);
        cookie.value = (const char*)sqlite3_column_text(statement, 1);
        cookie.domain = (const char*)sqlite3_column_text(statement, 2);
        cookie.path = (const char*)sqlite3_column_text(statement, 3);
        cookie.expires = sqlite3_column_type(statement, 4) == SQLITE_NULL ? 0 :
            (time_t)(sqlite3_column_int64(statement, 4) / 1000);
        cookie.secure = sqlite3_column_int(statement, 5) != 0;
        cookie.http_only = sqlite3_column_int(statement, 6) != 0;
        cookie.same_site = sqlite3_column_int(statement, 7);
        cookie.creation_time = (time_t)(sqlite3_column_int64(statement, 8) / 1000);
        if (!visitor(&cookie, user_data)) {
            ok = false;
            break;
        }
    }
    if (rc != SQLITE_DONE && ok) {
        log_error("radiant_state: cookie read failed: %s", sqlite3_errmsg(store->db));
        ok = false;
    }
    sqlite3_finalize(statement);
    return ok;
}

extern "C" bool radiant_state_store_queue_cookie_upsert(RadiantStateStore* store,
                                                          const RadiantStateCookie* cookie) {
    if (!store || !cookie || !cookie->name || !cookie->value || !cookie->domain || !cookie->path) return false;
    StateCookieCommand* command = state_cookie_command_new(STATE_COOKIE_UPSERT);
    if (!command) return false;
    command->cookie = *cookie;
    command->cookie.name = mem_strdup(cookie->name, MEM_CAT_NETWORK);
    command->cookie.value = mem_strdup(cookie->value, MEM_CAT_NETWORK);
    command->cookie.domain = mem_strdup(cookie->domain, MEM_CAT_NETWORK);
    command->cookie.path = mem_strdup(cookie->path, MEM_CAT_NETWORK);
    if (!command->cookie.name || !command->cookie.value || !command->cookie.domain || !command->cookie.path) {
        state_cookie_command_free(command);
        return false;
    }
    pthread_mutex_lock(&store->pending_cookie_lock);
    if (!store->pending_cookie_commands) store->pending_cookie_commands = arraylist_new(16);
    bool queued = store->pending_cookie_commands && arraylist_append(store->pending_cookie_commands, command);
    pthread_mutex_unlock(&store->pending_cookie_lock);
    if (!queued) state_cookie_command_free(command);
    return queued;
}

extern "C" bool radiant_state_store_queue_cookie_delete(RadiantStateStore* store,
                                                          const char* name, const char* domain,
                                                          const char* path) {
    if (!store || !name || !domain || !path) return false;
    StateCookieCommand* command = state_cookie_command_new(STATE_COOKIE_DELETE);
    if (!command) return false;
    command->name = mem_strdup(name, MEM_CAT_NETWORK);
    command->domain = mem_strdup(domain, MEM_CAT_NETWORK);
    command->path = mem_strdup(path, MEM_CAT_NETWORK);
    if (!command->name || !command->domain || !command->path) {
        state_cookie_command_free(command);
        return false;
    }
    pthread_mutex_lock(&store->pending_cookie_lock);
    if (!store->pending_cookie_commands) store->pending_cookie_commands = arraylist_new(16);
    bool queued = store->pending_cookie_commands && arraylist_append(store->pending_cookie_commands, command);
    pthread_mutex_unlock(&store->pending_cookie_lock);
    if (!queued) state_cookie_command_free(command);
    return queued;
}

extern "C" bool radiant_state_store_flush(RadiantStateStore* store) {
    if (!store || !state_is_owner(store)) return false;
    pthread_mutex_lock(&store->pending_cookie_lock);
    ArrayList* commands = store->pending_cookie_commands;
    store->pending_cookie_commands = NULL;
    pthread_mutex_unlock(&store->pending_cookie_lock);
    if (!commands || commands->length == 0) {
        arraylist_free(commands);
        return true;
    }
    bool ok = state_begin(store);
    for (int i = 0; ok && i < commands->length; i++) {
        StateCookieCommand* command = (StateCookieCommand*)arraylist_get(commands, i);
        ok = command && (command->kind == STATE_COOKIE_UPSERT
            ? state_cookie_upsert_now(store, &command->cookie)
            : state_cookie_delete_now(store, command->name, command->domain, command->path));
    }
    if (ok) ok = state_commit(store);
    else state_rollback(store);
    for (int i = 0; i < commands->length; i++) {
        state_cookie_command_free((StateCookieCommand*)arraylist_get(commands, i));
    }
    arraylist_free(commands);
    return ok;
}

extern "C" bool radiant_state_store_storage_load(RadiantStateStore* store,
                                                   RadiantStateStorageScope scope,
                                                   const char* context_id, const char* origin,
                                                   RadiantStateStorageVisitor visitor,
                                                   void* user_data) {
    if (!store || !origin || !visitor || !state_storage_scope_valid(scope, context_id) || !state_is_owner(store)) return false;
    const char* table = state_storage_table(scope);
    char sql[512] = {};
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        snprintf(sql, sizeof(sql), "SELECT key,value,ordinal FROM %s WHERE profile_id=?1 AND origin=?2 ORDER BY ordinal", table);
    } else {
        snprintf(sql, sizeof(sql), "SELECT key,value,ordinal FROM %s WHERE session_id=?1 AND context_id=?2 AND origin=?3 ORDER BY ordinal", table);
    }
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store, sql, &statement)) return false;
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        sqlite3_bind_int64(statement, 1, store->profile_id);
        sqlite3_bind_text(statement, 2, origin, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_text(statement, 1, store->session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 2, context_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, origin, -1, SQLITE_STATIC);
    }
    bool ok = true;
    int rc = SQLITE_OK;
    while ((rc = sqlite3_step(statement)) == SQLITE_ROW) {
        const char* key = (const char*)sqlite3_column_text(statement, 0);
        const char* value = (const char*)sqlite3_column_text(statement, 1);
        if (!visitor(key ? key : "", value ? value : "", sqlite3_column_int(statement, 2), user_data)) {
            ok = false;
            break;
        }
    }
    if (rc != SQLITE_DONE && ok) {
        log_error("radiant_state: Web Storage read failed: %s", sqlite3_errmsg(store->db));
        ok = false;
    }
    sqlite3_finalize(statement);
    return ok;
}

extern "C" bool radiant_state_store_storage_set(RadiantStateStore* store,
                                                  RadiantStateStorageScope scope,
                                                  const char* context_id, const char* origin,
                                                  const char* key, const char* value) {
    if (!store || !origin || !key || !value || !state_storage_scope_valid(scope, context_id) ||
        !state_is_owner(store) || !state_begin(store)) return false;
    int64_t origin_bytes = 0;
    int64_t profile_bytes = 0;
    int64_t existing_bytes = 0;
    bool ok = state_storage_size(store, scope, context_id, origin, &origin_bytes, &profile_bytes) &&
        state_storage_existing_bytes(store, scope, context_id, origin, key, &existing_bytes);
    int64_t next_bytes = (int64_t)strlen(key) + (int64_t)strlen(value);
    int64_t delta = next_bytes - existing_bytes;
    if (ok && (origin_bytes + delta > RADIANT_STATE_ORIGIN_LIMIT_BYTES ||
        (scope == RADIANT_STATE_STORAGE_LOCAL && profile_bytes + delta > RADIANT_STATE_PROFILE_LIMIT_BYTES))) {
        log_error("radiant_state: Web Storage quota exceeded for %s", origin);
        state_rollback(store);
        return false;
    }
    const char* table = state_storage_table(scope);
    char sql[768] = {};
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        snprintf(sql, sizeof(sql),
            "INSERT INTO %s(profile_id,origin,key,value,ordinal,modified_at_ms) VALUES(?1,?2,?3,?4,"
            "COALESCE((SELECT MAX(ordinal)+1 FROM %s WHERE profile_id=?1 AND origin=?2),0),?5) "
            "ON CONFLICT(profile_id,origin,key) DO UPDATE SET value=excluded.value,modified_at_ms=excluded.modified_at_ms",
            table, table);
    } else {
        snprintf(sql, sizeof(sql),
            "INSERT INTO %s(session_id,context_id,origin,key,value,ordinal,modified_at_ms) VALUES(?1,?2,?3,?4,?5,"
            "COALESCE((SELECT MAX(ordinal)+1 FROM %s WHERE session_id=?1 AND context_id=?2 AND origin=?3),0),?6) "
            "ON CONFLICT(session_id,context_id,origin,key) DO UPDATE SET value=excluded.value,modified_at_ms=excluded.modified_at_ms",
            table, table);
    }
    sqlite3_stmt* statement = NULL;
    if (ok) ok = state_prepare(store, sql, &statement);
    if (ok && scope == RADIANT_STATE_STORAGE_LOCAL) {
        sqlite3_bind_int64(statement, 1, store->profile_id);
        sqlite3_bind_text(statement, 2, origin, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, key, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 4, value, -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 5, state_now_ms());
    } else if (ok) {
        sqlite3_bind_text(statement, 1, store->session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 2, context_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, origin, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 4, key, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 5, value, -1, SQLITE_STATIC);
        sqlite3_bind_int64(statement, 6, state_now_ms());
    }
    if (ok) {
        ok = state_step_done(store, statement);
        sqlite3_finalize(statement);
    }
    if (ok) ok = state_commit(store);
    else state_rollback(store);
    return ok;
}

extern "C" bool radiant_state_store_storage_remove(RadiantStateStore* store,
                                                     RadiantStateStorageScope scope,
                                                     const char* context_id, const char* origin,
                                                     const char* key) {
    if (!store || !origin || !key || !state_storage_scope_valid(scope, context_id) || !state_is_owner(store)) return false;
    const char* table = state_storage_table(scope);
    char sql[512] = {};
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE profile_id=?1 AND origin=?2 AND key=?3", table);
    } else {
        snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE session_id=?1 AND context_id=?2 AND origin=?3 AND key=?4", table);
    }
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store, sql, &statement)) return false;
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        sqlite3_bind_int64(statement, 1, store->profile_id);
        sqlite3_bind_text(statement, 2, origin, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, key, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_text(statement, 1, store->session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 2, context_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, origin, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 4, key, -1, SQLITE_STATIC);
    }
    bool ok = state_step_done(store, statement);
    sqlite3_finalize(statement);
    return ok;
}

extern "C" bool radiant_state_store_storage_clear(RadiantStateStore* store,
                                                    RadiantStateStorageScope scope,
                                                    const char* context_id, const char* origin) {
    if (!store || !origin || !state_storage_scope_valid(scope, context_id) || !state_is_owner(store)) return false;
    const char* table = state_storage_table(scope);
    char sql[512] = {};
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE profile_id=?1 AND origin=?2", table);
    } else {
        snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE session_id=?1 AND context_id=?2 AND origin=?3", table);
    }
    sqlite3_stmt* statement = NULL;
    if (!state_prepare(store, sql, &statement)) return false;
    if (scope == RADIANT_STATE_STORAGE_LOCAL) {
        sqlite3_bind_int64(statement, 1, store->profile_id);
        sqlite3_bind_text(statement, 2, origin, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_text(statement, 1, store->session_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 2, context_id, -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 3, origin, -1, SQLITE_STATIC);
    }
    bool ok = state_step_done(store, statement);
    sqlite3_finalize(statement);
    return ok;
}
