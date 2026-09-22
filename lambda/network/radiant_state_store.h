// radiant_state_store.h
// Profile-owned durable browser and agent state backed by the bundled SQLite.

#ifndef RADIANT_STATE_STORE_H
#define RADIANT_STATE_STORE_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RadiantStateStore RadiantStateStore;

typedef enum RadiantStateStorageScope {
    RADIANT_STATE_STORAGE_LOCAL = 0,
    RADIANT_STATE_STORAGE_SESSION = 1
} RadiantStateStorageScope;

typedef struct RadiantStateCookie {
    const char* name;
    const char* value;
    const char* domain;
    const char* path;
    time_t expires;
    bool secure;
    bool http_only;
    int same_site;
    time_t creation_time;
} RadiantStateCookie;

typedef bool (*RadiantStateCookieVisitor)(const RadiantStateCookie* cookie,
                                          void* user_data);
typedef bool (*RadiantStateStorageVisitor)(const char* key, const char* value,
                                           int ordinal, void* user_data);
typedef bool (*RadiantStateHistoryVisitor)(const char* url, const char* title,
                                           float scroll_x, float scroll_y,
                                           void* user_data);

// Opens one profile database at <cache_dir>/radiant_state.sqlite3. The caller
// owns the returned store and must close it on the same thread.
RadiantStateStore* radiant_state_store_open(const char* cache_dir,
                                            const char* profile_name);
void radiant_state_store_close(RadiantStateStore* store);

const char* radiant_state_store_path(const RadiantStateStore* store);
const char* radiant_state_store_session_id(const RadiantStateStore* store);

// Creates or resumes the profile's most recently active top-level context.
// The returned string is store-owned and remains valid until store close.
const char* radiant_state_store_open_browsing_context(RadiantStateStore* store,
                                                      int* out_current_index);
bool radiant_state_store_history_load(RadiantStateStore* store,
                                      const char* context_id,
                                      RadiantStateHistoryVisitor visitor,
                                      void* user_data);
bool radiant_state_store_history_append(RadiantStateStore* store,
                                        const char* context_id,
                                        const char* url, const char* title,
                                        float scroll_x, float scroll_y,
                                        const char* transition,
                                        int* out_current_index);
bool radiant_state_store_history_select(RadiantStateStore* store,
                                        const char* context_id,
                                        int current_index);
bool radiant_state_store_history_update_current(RadiantStateStore* store,
                                                const char* context_id,
                                                const char* title,
                                                float scroll_x, float scroll_y);

// Cookie writes may come from network workers. They are copied into a queue;
// only radiant_state_store_flush() touches SQLite and it must run on the
// opening thread. Reads occur at profile creation on that owner thread.
bool radiant_state_store_load_cookies(RadiantStateStore* store,
                                      RadiantStateCookieVisitor visitor,
                                      void* user_data);
bool radiant_state_store_queue_cookie_upsert(RadiantStateStore* store,
                                             const RadiantStateCookie* cookie);
bool radiant_state_store_queue_cookie_delete(RadiantStateStore* store,
                                             const char* name, const char* domain,
                                             const char* path);
bool radiant_state_store_flush(RadiantStateStore* store);

// Web Storage is synchronous to script. These calls must run on the store's
// owner thread and are transactionally applied before the JS cache changes.
bool radiant_state_store_storage_load(RadiantStateStore* store,
                                      RadiantStateStorageScope scope,
                                      const char* context_id, const char* origin,
                                      RadiantStateStorageVisitor visitor,
                                      void* user_data);
bool radiant_state_store_storage_set(RadiantStateStore* store,
                                     RadiantStateStorageScope scope,
                                     const char* context_id, const char* origin,
                                     const char* key, const char* value);
bool radiant_state_store_storage_remove(RadiantStateStore* store,
                                        RadiantStateStorageScope scope,
                                        const char* context_id, const char* origin,
                                        const char* key);
bool radiant_state_store_storage_clear(RadiantStateStore* store,
                                       RadiantStateStorageScope scope,
                                       const char* context_id, const char* origin);

#ifdef __cplusplus
}
#endif

#endif // RADIANT_STATE_STORE_H
