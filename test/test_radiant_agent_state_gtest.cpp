// Focused persistence tests for the Radiant browser and agent state store.

#include <gtest/gtest.h>

#include "../lambda/network/cookie_jar.h"
#include "../lambda/network/enhanced_file_cache.h"
#include "../lambda/network/radiant_state_store.h"
#include "../lib/file.h"
#include "../lib/mem.h"
#include "../lib/sqlite/sqlite3.h"

#include <string.h>
#include <time.h>

typedef struct StateEntryCapture {
    int count;
    char key[64];
    char value[64];
} StateEntryCapture;

typedef struct HistoryCapture {
    int count;
    char url[256];
    char last_url[256];
    char title[128];
    float scroll_y;
} HistoryCapture;

typedef struct OrderedStorageCapture {
    int count;
    char keys[3][64];
    char values[3][64];
    int ordinals[3];
} OrderedStorageCapture;

static bool capture_storage_entry(const char* key, const char* value, int ordinal,
                                  void* user_data) {
    (void)ordinal;
    StateEntryCapture* capture = (StateEntryCapture*)user_data;
    if (!capture) return false;
    capture->count++;
    if (capture->count != 1) return true;
    strncpy(capture->key, key ? key : "", sizeof(capture->key) - 1);
    capture->key[sizeof(capture->key) - 1] = '\0';
    strncpy(capture->value, value ? value : "", sizeof(capture->value) - 1);
    capture->value[sizeof(capture->value) - 1] = '\0';
    return true;
}

static bool capture_history_entry(const char* url, const char* title, float scroll_x,
                                  float scroll_y, void* user_data) {
    (void)scroll_x;
    HistoryCapture* capture = (HistoryCapture*)user_data;
    if (!capture) return false;
    capture->count++;
    if (capture->count == 1) {
        strncpy(capture->url, url ? url : "", sizeof(capture->url) - 1);
        capture->url[sizeof(capture->url) - 1] = '\0';
        strncpy(capture->title, title ? title : "", sizeof(capture->title) - 1);
        capture->title[sizeof(capture->title) - 1] = '\0';
        capture->scroll_y = scroll_y;
    }
    strncpy(capture->last_url, url ? url : "", sizeof(capture->last_url) - 1);
    capture->last_url[sizeof(capture->last_url) - 1] = '\0';
    return true;
}

static bool capture_ordered_storage_entry(const char* key, const char* value, int ordinal,
                                          void* user_data) {
    OrderedStorageCapture* capture = (OrderedStorageCapture*)user_data;
    if (!capture || capture->count >= 3) return false;
    int index = capture->count++;
    strncpy(capture->keys[index], key ? key : "", sizeof(capture->keys[index]) - 1);
    capture->keys[index][sizeof(capture->keys[index]) - 1] = '\0';
    strncpy(capture->values[index], value ? value : "", sizeof(capture->values[index]) - 1);
    capture->values[index][sizeof(capture->values[index]) - 1] = '\0';
    capture->ordinals[index] = ordinal;
    return true;
}

TEST(RadiantAgentState, PersistsProfileStateAndClearsSessionState) {
    char* cache_dir = dir_temp_create("radiant_agent_state");
    ASSERT_NE(cache_dir, nullptr);

    const char* profile = "agent-alpha";
    const char* origin = "https://example.test";
    RadiantStateStore* store = radiant_state_store_open(cache_dir, profile);
    ASSERT_NE(store, nullptr);
    EXPECT_NE(strstr(radiant_state_store_path(store), "radiant_state.sqlite3"), nullptr);

    int current_index = -1;
    const char* context_id = radiant_state_store_open_browsing_context(store, &current_index);
    ASSERT_NE(context_id, nullptr);
    EXPECT_EQ(current_index, -1);
    EXPECT_TRUE(radiant_state_store_history_append(store, context_id,
        "https://example.test/first", "First", 0.0f, 24.0f, "agent", &current_index));
    EXPECT_EQ(current_index, 0);
    EXPECT_TRUE(radiant_state_store_history_append(store, context_id,
        "https://example.test/second", "Second", 0.0f, 0.0f, "link", &current_index));
    EXPECT_EQ(current_index, 1);
    EXPECT_TRUE(radiant_state_store_history_select(store, context_id, 0));
    EXPECT_TRUE(radiant_state_store_history_append(store, context_id,
        "https://example.test/branch", "Branch", 0.0f, 0.0f, "link", &current_index));
    EXPECT_EQ(current_index, 1);
    EXPECT_TRUE(radiant_state_store_storage_set(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, origin, "local-key", "local-value"));
    EXPECT_TRUE(radiant_state_store_storage_set(store, RADIANT_STATE_STORAGE_SESSION,
        context_id, origin, "session-key", "session-value"));

    CookieJar* jar = cookie_jar_create(store);
    ASSERT_NE(jar, nullptr);
    cookie_jar_store(jar, "https://example.test/first",
        "persist=one; Max-Age=600; Secure; Path=/");
    cookie_jar_store(jar, "https://example.test/first",
        "hidden=two; Max-Age=600; HttpOnly; Path=/");
    cookie_jar_store(jar, "https://example.test/first", "session=three; Path=/");
    ASSERT_TRUE(cookie_jar_flush(jar));

    char* document_cookie = cookie_jar_build_document_cookie(jar, "https://example.test/first");
    ASSERT_NE(document_cookie, nullptr);
    EXPECT_NE(strstr(document_cookie, "persist=one"), nullptr);
    EXPECT_EQ(strstr(document_cookie, "hidden=two"), nullptr);
    mem_free(document_cookie);
    EXPECT_EQ(cookie_jar_count(jar), 3);
    cookie_jar_destroy(jar);
    radiant_state_store_close(store);

    store = radiant_state_store_open(cache_dir, profile);
    ASSERT_NE(store, nullptr);
    context_id = radiant_state_store_open_browsing_context(store, &current_index);
    ASSERT_NE(context_id, nullptr);
    EXPECT_EQ(current_index, 1);

    StateEntryCapture local = {};
    EXPECT_TRUE(radiant_state_store_storage_load(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, origin, capture_storage_entry, &local));
    EXPECT_EQ(local.count, 1);
    EXPECT_STREQ(local.key, "local-key");
    EXPECT_STREQ(local.value, "local-value");

    StateEntryCapture session = {};
    EXPECT_TRUE(radiant_state_store_storage_load(store, RADIANT_STATE_STORAGE_SESSION,
        context_id, origin, capture_storage_entry, &session));
    EXPECT_EQ(session.count, 0);

    HistoryCapture history = {};
    EXPECT_TRUE(radiant_state_store_history_load(store, context_id,
        capture_history_entry, &history));
    EXPECT_EQ(history.count, 2);
    EXPECT_STREQ(history.url, "https://example.test/first");
    EXPECT_STREQ(history.title, "First");
    EXPECT_FLOAT_EQ(history.scroll_y, 24.0f);
    EXPECT_STREQ(history.last_url, "https://example.test/branch");

    jar = cookie_jar_create(store);
    ASSERT_NE(jar, nullptr);
    EXPECT_EQ(cookie_jar_count(jar), 2);
    document_cookie = cookie_jar_build_document_cookie(jar, "https://example.test/first");
    ASSERT_NE(document_cookie, nullptr);
    EXPECT_NE(strstr(document_cookie, "persist=one"), nullptr);
    EXPECT_EQ(strstr(document_cookie, "hidden=two"), nullptr);
    EXPECT_EQ(strstr(document_cookie, "session=three"), nullptr);
    mem_free(document_cookie);
    cookie_jar_destroy(jar);
    radiant_state_store_close(store);

    store = radiant_state_store_open(cache_dir, "agent-beta");
    ASSERT_NE(store, nullptr);
    context_id = radiant_state_store_open_browsing_context(store, &current_index);
    ASSERT_NE(context_id, nullptr);
    StateEntryCapture isolated = {};
    EXPECT_TRUE(radiant_state_store_storage_load(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, origin, capture_storage_entry, &isolated));
    EXPECT_EQ(isolated.count, 0);
    jar = cookie_jar_create(store);
    ASSERT_NE(jar, nullptr);
    EXPECT_EQ(cookie_jar_count(jar), 0);
    cookie_jar_destroy(jar);
    radiant_state_store_close(store);

    EXPECT_EQ(file_delete_recursive(cache_dir), 0);
    mem_free(cache_dir);
}

TEST(RadiantAgentState, FailsClosedWithoutReplacingCorruptDatabase) {
    char* cache_dir = dir_temp_create("radiant_agent_state_corrupt");
    ASSERT_NE(cache_dir, nullptr);
    char* database_path = file_path_join(cache_dir, "radiant_state.sqlite3");
    ASSERT_NE(database_path, nullptr);
    const char invalid_database[] = "this is not a SQLite database";
    ASSERT_EQ(write_binary_file(database_path, invalid_database,
                                sizeof(invalid_database) - 1), 0);

    EXPECT_EQ(radiant_state_store_open(cache_dir, "agent-alpha"), nullptr);
    EXPECT_EQ(file_size(database_path), (int64_t)(sizeof(invalid_database) - 1));

    mem_free(database_path);
    EXPECT_EQ(file_delete_recursive(cache_dir), 0);
    mem_free(cache_dir);
}

TEST(RadiantAgentState, HttpCacheClearDoesNotRemoveProfileDatabase) {
    char* cache_dir = dir_temp_create("radiant_agent_state_cache");
    ASSERT_NE(cache_dir, nullptr);
    RadiantStateStore* store = radiant_state_store_open(cache_dir, "agent-alpha");
    ASSERT_NE(store, nullptr);
    char* database_path = mem_strdup(radiant_state_store_path(store), MEM_CAT_NETWORK);
    ASSERT_NE(database_path, nullptr);
    radiant_state_store_close(store);

    EnhancedFileCache* cache = enhanced_cache_create(cache_dir, 1024, 1);
    ASSERT_NE(cache, nullptr);
    char* entry_path = enhanced_cache_store(cache, "https://example.test/resource",
        "cached", 6, nullptr);
    ASSERT_NE(entry_path, nullptr);
    mem_free(entry_path);
    enhanced_cache_clear(cache);
    EXPECT_TRUE(file_exists(database_path));
    enhanced_cache_destroy(cache);

    mem_free(database_path);
    EXPECT_EQ(file_delete_recursive(cache_dir), 0);
    mem_free(cache_dir);
}

TEST(RadiantAgentState, KeepsWebStorageOriginPartitionAndInsertionOrder) {
    char* cache_dir = dir_temp_create("radiant_agent_storage");
    ASSERT_NE(cache_dir, nullptr);
    RadiantStateStore* store = radiant_state_store_open(cache_dir, "agent-alpha");
    ASSERT_NE(store, nullptr);
    const char* context_id = radiant_state_store_open_browsing_context(store, nullptr);
    ASSERT_NE(context_id, nullptr);

    EXPECT_TRUE(radiant_state_store_storage_set(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, "https://alpha.test", "first", "one"));
    EXPECT_TRUE(radiant_state_store_storage_set(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, "https://alpha.test", "second", "two"));
    EXPECT_TRUE(radiant_state_store_storage_set(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, "https://alpha.test", "first", "updated"));
    EXPECT_TRUE(radiant_state_store_storage_set(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, "https://beta.test", "isolated", "value"));

    OrderedStorageCapture alpha = {};
    EXPECT_TRUE(radiant_state_store_storage_load(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, "https://alpha.test", capture_ordered_storage_entry, &alpha));
    ASSERT_EQ(alpha.count, 2);
    EXPECT_STREQ(alpha.keys[0], "first");
    EXPECT_STREQ(alpha.values[0], "updated");
    EXPECT_EQ(alpha.ordinals[0], 0);
    EXPECT_STREQ(alpha.keys[1], "second");
    EXPECT_EQ(alpha.ordinals[1], 1);

    EXPECT_TRUE(radiant_state_store_storage_clear(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, "https://alpha.test"));
    StateEntryCapture cleared = {};
    EXPECT_TRUE(radiant_state_store_storage_load(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, "https://alpha.test", capture_storage_entry, &cleared));
    EXPECT_EQ(cleared.count, 0);
    StateEntryCapture beta = {};
    EXPECT_TRUE(radiant_state_store_storage_load(store, RADIANT_STATE_STORAGE_LOCAL,
        context_id, "https://beta.test", capture_storage_entry, &beta));
    EXPECT_EQ(beta.count, 1);
    EXPECT_STREQ(beta.key, "isolated");

    radiant_state_store_close(store);
    EXPECT_EQ(file_delete_recursive(cache_dir), 0);
    mem_free(cache_dir);
}

TEST(RadiantAgentState, RejectsNewerSchemaWithoutReplacingDatabase) {
    char* cache_dir = dir_temp_create("radiant_agent_schema");
    ASSERT_NE(cache_dir, nullptr);
    RadiantStateStore* store = radiant_state_store_open(cache_dir, "agent-alpha");
    ASSERT_NE(store, nullptr);
    char* database_path = mem_strdup(radiant_state_store_path(store), MEM_CAT_NETWORK);
    ASSERT_NE(database_path, nullptr);
    radiant_state_store_close(store);

    sqlite3* database = nullptr;
    ASSERT_EQ(sqlite3_open(database_path, &database), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(database, "PRAGMA user_version=2", nullptr, nullptr, nullptr), SQLITE_OK);
    ASSERT_EQ(sqlite3_close(database), SQLITE_OK);
    int64_t original_size = file_size(database_path);

    EXPECT_EQ(radiant_state_store_open(cache_dir, "agent-alpha"), nullptr);
    EXPECT_EQ(file_size(database_path), original_size);

    mem_free(database_path);
    EXPECT_EQ(file_delete_recursive(cache_dir), 0);
    mem_free(cache_dir);
}
