#include <gtest/gtest.h>
#include "../lambda/host/safety/admission.h"
#include "../lambda/host/safety/private_storage.h"
#include "../lambda/host/safety/state_store.h"
#include "lambda/io/security_channel.h"
#include "lib/file_utils.h"
#include "lib/digest.h"
#include "lib/hex.h"
#include "lib/endian.h"
#include "lib/log.h"
#include "lib/mem.h"
#include "lib/ipc_stream.h"
#include "safety/async_fixture.h"
#include "safety/child_fixture.h"
#include "safety/grant_fixture.h"
#include "safety/channel_wire_fixture.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <fcntl.h>

class SecurityAdmissionTest : public ::testing::Test {
protected:
    char* temporary = nullptr;
    char* base = nullptr;
    char* home = nullptr;
    char* project = nullptr;
    char* entry = nullptr;
    char* installation = nullptr;
    SecurityGrantStore* store = nullptr;
    SecurityAdmission* admission = nullptr;
    uint64_t now = 100;

    static uint64_t clock(void* context) { return *(uint64_t*)context; }
    void SetUp() override {
        log_init(nullptr);
        temporary = dir_temp_create("security_admission"); ASSERT_NE(temporary, nullptr);
        base = file_realpath(temporary); ASSERT_NE(base, nullptr);
        home = file_path_join(base, "home"); project = file_path_join(home, "project");
        entry = file_path_join(project, "entry.ls"); installation = file_path_join(base, "installation");
        ASSERT_TRUE(create_dir(project)); ASSERT_TRUE(create_dir(installation));
        ASSERT_EQ(chmod(home, 0700), 0);
        ASSERT_EQ(write_binary_file(entry, "pn main() { null }\n", 19), 0);
        SecurityStoreResult result;
        store = security_grant_store_test_open(home, &result);
        ASSERT_NE(store, nullptr); ASSERT_EQ(result, SECURITY_STORE_OK);
    }
    void TearDown() override {
        security_admission_close(admission); security_grant_store_close(store);
        if (temporary) dir_delete(temporary);
        mem_free(installation); mem_free(entry); mem_free(project); mem_free(home); mem_free(base); mem_free(temporary);
    }
    SecurityAdmissionConfig config(bool revalidate = false, bool loose = false) {
        return {store, entry, loose ? nullptr : project, installation,
            nullptr, 0, nullptr, 0, revalidate, clock, &now};
    }
    SecurityAdmissionReport open(bool revalidate = false, bool loose = false) {
        security_admission_close(admission); admission = nullptr;
        SecurityAdmissionConfig options = config(revalidate, loose);
        SecurityAdmissionReport report;
        admission = security_admission_open(&options, &report);
        return report;
    }
    char* seed(const char* relative, const char* bytes, bool in_project = false) {
        char* path = file_path_join(in_project ? project : home, relative);
        char* parent = file_path_dirname(path);
        EXPECT_TRUE(create_dir(parent)); mem_free(parent);
        EXPECT_EQ(write_binary_file(path, bytes, strlen(bytes)), 0);
        return path;
    }
    bool read(const char* path, bool source = false, SecurityIOResult* result = nullptr) {
        SecurityIOClient client = security_admission_client(admission);
        char* bytes = nullptr; size_t size = 0;
        bool valid = security_io_read_bytes(&client, path, source, 1024 * 1024,
            MEM_CAT_SYSTEM, &bytes, &size, result);
        mem_free(bytes); return valid;
    }
    static void expect_private_text(const SecurityIOClient* client, const char* path, const char* expected) {
        char* bytes = nullptr; size_t size = 0;
        bool valid = security_io_read_bytes(client, path, false, 1024, MEM_CAT_SYSTEM, &bytes, &size, nullptr);
        EXPECT_TRUE(valid) << path;
        if (valid) {
            EXPECT_EQ(size, strlen(expected)) << path;
            if (size == strlen(expected)) EXPECT_EQ(memcmp(bytes, expected, size), 0) << path;
        }
        mem_free(bytes);
    }
    uint64_t select(const char* path, bool persistent = true, bool directory = false,
            SecurityOperations operations = SECURITY_READ, uint64_t expires = 0) {
        SecurityFileSelection selection = {path, operations, expires, directory, directory, persistent};
        SecurityStoreResult result;
        uint64_t resource = security_admission_select(admission, &selection, &result);
        EXPECT_NE(resource, 0U); EXPECT_EQ(result, SECURITY_STORE_OK);
        return resource;
    }
    SecurityPrivateStorageConfig storage_config() {
        SecurityGrantBinding binding = {}; binding.principal_kind = SECURITY_PRINCIPAL_SCRIPT;
        memset(binding.principal, 1, 32); memset(binding.root, 2, 32); memset(binding.code, 3, 32);
        SecurityFileBrokerConfig broker = {{1, 2, SECURITY_ALL_OPERATIONS, 64, 64}, 1024, 4096, 1024, 8, 64, 60, clock, &now};
        return {store, binding, broker, {{8, 3}, {8, 3}, {16, 16}}};
    }
    bool quota_blob(const SecurityPrivateStorageConfig* config, char** bytes, size_t* size,
            const void* replacement = nullptr, size_t replacement_size = 0) {
        SecurityStoreResult result;
        SecurityStateTransaction* transaction = security_state_begin_principal(config->store, SECURITY_STATE_PRIVATE_QUOTA,
            config->binding.principal_kind, config->binding.principal, &result);
        if (!transaction) return false;
        char* captured = nullptr; size_t count = 0;
        result = security_state_read(transaction, &captured, &count);
        bool valid = result == SECURITY_STORE_OK;
        if (replacement && (valid || result == SECURITY_STORE_MISSING))
            valid = security_state_publish(transaction, replacement, replacement_size) == SECURITY_STORE_OK;
        security_state_end(transaction);
        if (valid && bytes && size) { *bytes = captured; *size = count; } else mem_free(captured);
        return valid;
    }
    char* private_directory(const SecurityGrantBinding* selected = nullptr, const char* leaf = "data") {
        SecurityGrantBinding binding = {};
        if (selected) binding = *selected;
        else EXPECT_TRUE(security_admission_binding(admission, &binding));
        char name[80] = "s-"; hex_encode(binding.principal, 32, name + 2);
        FileAnchor* root = security_grant_store_private_root(store);
        char* path = root ? file_anchor_path(root) : nullptr;
        char* owner = path ? file_path_join(path, name) : nullptr;
        char* result = owner ? file_path_join(owner, leaf) : nullptr;
        file_anchor_close(root); mem_free(path); mem_free(owner); return result;
    }
};

TEST_F(SecurityAdmissionTest, ProjectAndLooseEntryHaveDifferentBoundAuthority) {
    char* helper = seed("helper.ls", "pub let answer = 42\n", true);
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    EXPECT_TRUE(read(entry, true)); EXPECT_TRUE(read(helper, true));
    SecurityGrantBinding before; ASSERT_TRUE(security_admission_binding(admission, &before));
    EXPECT_NE(security_admission_session(admission), 0U);
    EXPECT_STREQ(security_admission_entry(admission), entry);
    EXPECT_EQ(open(false, true).store_result, SECURITY_STORE_IDENTITY_CHANGED);
    EXPECT_EQ(admission, nullptr);
    EXPECT_EQ(open(true, true).store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    EXPECT_TRUE(read(entry, true)); EXPECT_FALSE(read(helper, true));
    SecurityGrantBinding after; ASSERT_TRUE(security_admission_binding(admission, &after));
    EXPECT_EQ(memcmp(before.principal, after.principal, 32), 0);
    EXPECT_NE(memcmp(before.root, after.root, 32), 0); mem_free(helper);
}

TEST_F(SecurityAdmissionTest, BroadRootCannotReadThePrivateStorageNamespace) {
    SecurityAdmissionConfig options = config(); options.project = home;
    SecurityAdmissionReport report; admission = security_admission_open(&options, &report); ASSERT_NE(admission, nullptr);
    FileAnchor* storage = security_grant_store_private_root(store); ASSERT_NE(storage, nullptr);
    ASSERT_EQ(file_anchor_write_atomic(storage, "sentinel", "host", 4, nullptr), 0);
    char* directory = file_anchor_path(storage); char* path = file_path_join(directory, "sentinel");
    EXPECT_FALSE(read(path)); file_anchor_close(storage); mem_free(path); mem_free(directory);
}

TEST_F(SecurityAdmissionTest, PrivateDataRestoresOnlyForItsAdmittedPrincipal) {
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    const char* path = SECURITY_APP_DATA_PATH "/prefs.json";
    SecurityIOClient client = security_admission_client(admission);
    EXPECT_TRUE(client.write(client.context, path, "first", 5, false, true).committed);
    SecurityIOResult replaced = client.write(client.context, path, "second", 6, false, true);
    EXPECT_EQ(replaced.decision, SECURITY_ALLOWED); EXPECT_TRUE(replaced.committed); EXPECT_EQ(replaced.approval.serial, 0U);
    EXPECT_TRUE(read(path)); EXPECT_FALSE(read(path, true));
    FileStat metadata = {}; EXPECT_TRUE(security_io_stat(&client, path, &metadata, nullptr)); EXPECT_EQ(metadata.size, 6);
    SecurityDirectory listing = {}; EXPECT_TRUE(security_io_enumerate(&client, SECURITY_APP_DATA_PATH, 8, &listing, nullptr));
    ASSERT_EQ(listing.count, 1U); EXPECT_STREQ(listing.entries[0].name, "prefs.json"); security_io_directory_free(&listing);
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); EXPECT_TRUE(read(path));
    char* other = seed("other.ls", "pn main() { null }\n", true);
    // Adding a source requires revalidation but keeps this principal's data.
    ASSERT_EQ(open(true).store_result, SECURITY_STORE_OK); EXPECT_TRUE(read(path));
    security_admission_close(admission); admission = nullptr;
    SecurityAdmissionConfig second = config(); second.entry = other;
    SecurityAdmissionReport report; admission = security_admission_open(&second, &report); ASSERT_NE(admission, nullptr);
    EXPECT_FALSE(read(path)); client = security_admission_client(admission);
    EXPECT_TRUE(client.write(client.context, path, "other", 5, false, true).committed);
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); client = security_admission_client(admission);
    char* bytes = nullptr; size_t size = 0;
    EXPECT_TRUE(security_io_read_bytes(&client, path, false, 32, MEM_CAT_SYSTEM, &bytes, &size, nullptr));
    EXPECT_EQ(size, 6U); EXPECT_EQ(memcmp(bytes, "second", size), 0); mem_free(bytes); mem_free(other);
}

TEST_F(SecurityAdmissionTest, PrivateDataRejectsAliasesTraversalAndHostNamespaceAccess) {
    char* secret = seed("secret.txt", "private");
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    char* directory = private_directory(); ASSERT_NE(directory, nullptr);
    char* alias = file_path_join(directory, "alias"); char* hardlink = file_path_join(directory, "hardlink");
    ASSERT_EQ(symlink(secret, alias), 0); ASSERT_EQ(link(secret, hardlink), 0);
    EXPECT_FALSE(read(SECURITY_APP_DATA_PATH "/alias")); EXPECT_FALSE(read(SECURITY_APP_DATA_PATH "/hardlink"));
    EXPECT_FALSE(read(SECURITY_APP_DATA_PATH "/../owner")); EXPECT_FALSE(read(secret));
    SecurityIOClient client = security_admission_client(admission);
    const char* bad[] = {SECURITY_APP_DATA_PATH "/../owner", SECURITY_APP_DATA_PATH "/nested/../../owner",
        SECURITY_APP_DATA_PATH "/alias", SECURITY_APP_DATA_PATH "/hardlink"};
    for (const char* path : bad) EXPECT_FALSE(client.write(client.context, path, "x", 1, false, true).committed);
    char* nested = file_path_join(directory, "nested"); EXPECT_FALSE(file_exists(nested));
    mem_free(nested); mem_free(hardlink); mem_free(alias); mem_free(directory); mem_free(secret);
}

TEST_F(SecurityAdmissionTest, PrivateDataClosesWithSavedAuthorityRevocation) {
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    SecurityIOClient client = security_admission_client(admission);
    const char* paths[] = {SECURITY_APP_DATA_PATH "/data", SECURITY_APP_CACHE_PATH "/data", SECURITY_APP_TEMP_PATH "/data"};
    for (const char* path : paths) EXPECT_TRUE(client.write(client.context, path, "old", 3, false, true).committed);
    SecurityGrantBinding binding = {}; ASSERT_TRUE(security_admission_binding(admission, &binding));
    SecurityGrantSnapshot snapshot = {}; ASSERT_EQ(security_admission_inspect(admission, &snapshot), SECURITY_STORE_OK);
    uint64_t generation = 0;
    ASSERT_EQ(security_grant_store_save(store, &binding, snapshot.generation, nullptr, 0, &generation), SECURITY_STORE_OK);
    security_grant_snapshot_destroy(&snapshot);
    for (const char* path : paths) {
        SecurityIOResult status = client.write(client.context, path, "new", 3, false, true);
        EXPECT_EQ(status.decision, SECURITY_REVOKED); EXPECT_FALSE(status.committed); EXPECT_FALSE(read(path));
    }
}

TEST_F(SecurityAdmissionTest, CachePersistsButTemporaryFilesEndWithAdmission) {
    char* other = seed("other.ls", "pn main() { null }\n", true);
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    SecurityIOClient client = security_admission_client(admission);
    const struct Mount { const char* path; bool persistent; } mounts[] = {
        {SECURITY_APP_DATA_PATH "/work", true}, {SECURITY_APP_CACHE_PATH "/work", true}, {SECURITY_APP_TEMP_PATH "/work", false}};
    for (const Mount& mount : mounts) {
        ASSERT_TRUE(client.write(client.context, mount.path, "first", 5, false, true).committed);
        SecurityIOResult replaced = client.write(client.context, mount.path, "second", 6, false, true);
        EXPECT_TRUE(replaced.committed); EXPECT_EQ(replaced.approval.serial, 0U);
        expect_private_text(&client, mount.path, "second"); EXPECT_FALSE(read(mount.path, true));
        char* canonical = nullptr;
        EXPECT_EQ(client.canonical_source(client.context, mount.path, &canonical).decision, SECURITY_DENIED);
        EXPECT_EQ(canonical, nullptr);
    }
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    for (const Mount& mount : mounts) EXPECT_EQ(read(mount.path), mount.persistent);
    security_admission_close(admission); admission = nullptr;
    SecurityAdmissionConfig options = config(); options.entry = other; SecurityAdmissionReport report;
    admission = security_admission_open(&options, &report); ASSERT_NE(admission, nullptr);
    client = security_admission_client(admission);
    for (const Mount& mount : mounts) {
        EXPECT_FALSE(read(mount.path)); EXPECT_TRUE(client.write(client.context, mount.path, "foreign", 7, false, true).committed);
    }
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    client = security_admission_client(admission);
    for (const Mount& mount : mounts) {
        if (mount.persistent) expect_private_text(&client, mount.path, "second"); else EXPECT_FALSE(read(mount.path));
    }
    mem_free(other);
}

TEST_F(SecurityAdmissionTest, PrivateStorageCountsBytesAndDirectoriesAcrossReopen) {
    SecurityPrivateStorageConfig options = storage_config(); SecurityGrantBinding binding = options.binding;
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    SecurityIOClient client = security_private_storage_client(storage);
    const char* path = SECURITY_APP_DATA_PATH "/deep/path/file";
    EXPECT_TRUE(client.write(client.context, path, "1234", 4, false, true).committed);
    EXPECT_TRUE(client.write(client.context, path, "5678", 4, true, false).committed);
    EXPECT_FALSE(client.write(client.context, path, "9", 1, true, false).committed);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_DATA_PATH "/new", "", 0, false, true).committed);
    SecurityPrivateStorage* other = security_private_storage_open(&options); ASSERT_NE(other, nullptr);
    SecurityIOClient other_client = security_private_storage_client(other);
    EXPECT_FALSE(other_client.write(other_client.context, path, "9", 1, true, false).committed);
    char* data_directory = private_directory(&binding); char* owner_directory = file_path_dirname(data_directory);
    char* lock_path = file_path_join(owner_directory, ".storage.lock"); FILE* lock = file_open_regular_read(lock_path);
    ASSERT_NE(lock, nullptr); ASSERT_EQ(flock(fileno(lock), LOCK_EX | LOCK_NB), 0);
    SecurityIOResult busy = other_client.write(other_client.context, path, "x", 1, false, true);
    EXPECT_FALSE(busy.committed); EXPECT_EQ(busy.system_error, EWOULDBLOCK);
    ASSERT_EQ(flock(fileno(lock), LOCK_UN), 0); fclose(lock);
    mem_free(lock_path); mem_free(owner_directory); mem_free(data_directory);
    security_private_storage_close(other);
    EXPECT_TRUE(client.write(client.context, path, "x", 1, false, true).committed);
    security_private_storage_close(storage);
    storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    client = security_private_storage_client(storage);
    EXPECT_FALSE(client.write(client.context, path, "12345678", 8, true, false).committed);
    EXPECT_TRUE(client.write(client.context, path, "1234567", 7, true, false).committed);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_DATA_PATH "/new", "", 0, false, true).committed);
    security_private_storage_close(storage);
}

TEST_F(SecurityAdmissionTest, PrivateMountQuotasAndTempIsolationSpanLiveAdmissions) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* first = security_private_storage_open(&options); ASSERT_NE(first, nullptr);
    SecurityPrivateStorage* second = security_private_storage_open(&options); ASSERT_NE(second, nullptr);
    SecurityIOClient a = security_private_storage_client(first), b = security_private_storage_client(second);
    EXPECT_TRUE(a.write(a.context, SECURITY_APP_DATA_PATH "/kept", "data", 4, false, true).committed);
    EXPECT_TRUE(a.write(a.context, SECURITY_APP_CACHE_PATH "/preview", "12345678", 8, false, true).committed);
    EXPECT_FALSE(b.write(b.context, SECURITY_APP_CACHE_PATH "/next", "x", 1, false, true).committed);
    EXPECT_TRUE(a.write(a.context, SECURITY_APP_TEMP_PATH "/same", "1234567890", 10, false, true).committed);
    EXPECT_FALSE(b.write(b.context, SECURITY_APP_TEMP_PATH "/same", "1234567", 7, false, true).committed);
    EXPECT_TRUE(b.write(b.context, SECURITY_APP_TEMP_PATH "/same", "abcdef", 6, false, true).committed);
    expect_private_text(&a, SECURITY_APP_TEMP_PATH "/same", "1234567890");
    expect_private_text(&b, SECURITY_APP_TEMP_PATH "/same", "abcdef");
    EXPECT_TRUE(security_private_storage_clear_cache(first));
    char* bytes = nullptr; size_t size = 0;
    EXPECT_FALSE(security_io_read_bytes(&b, SECURITY_APP_CACHE_PATH "/preview", false, 32, MEM_CAT_SYSTEM, &bytes, &size, nullptr));
    EXPECT_TRUE(b.write(b.context, SECURITY_APP_CACHE_PATH "/next", "12345678", 8, false, true).committed);
    expect_private_text(&a, SECURITY_APP_DATA_PATH "/kept", "data");
    expect_private_text(&a, SECURITY_APP_TEMP_PATH "/same", "1234567890");
    security_private_storage_close(second); security_private_storage_close(first);
}

TEST_F(SecurityAdmissionTest, PrivateCleanupRefusesAliasesAndReportsBusyOwnership) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    SecurityIOClient client = security_private_storage_client(storage);
    char* outside = seed("outside", "private");
    char* cache = private_directory(&options.binding, "cache"); char* alias = file_path_join(cache, "alias");
    ASSERT_EQ(symlink(outside, alias), 0);
    EXPECT_FALSE(security_private_storage_clear_cache(storage)); EXPECT_EQ(errno, EACCES);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_CACHE_PATH "/new", "x", 1, false, true).committed);
    EXPECT_TRUE(file_exists(outside)); ASSERT_EQ(unlink(alias), 0);
    EXPECT_TRUE(security_private_storage_clear_cache(storage));
    EXPECT_TRUE(client.write(client.context, SECURITY_APP_TEMP_PATH "/work", "x", 1, false, true).committed);
    char* owner = file_path_dirname(cache); char* path = file_path_join(owner, ".storage.lock");
    FILE* lock = file_open_regular_read(path); ASSERT_NE(lock, nullptr); ASSERT_EQ(flock(fileno(lock), LOCK_EX | LOCK_NB), 0);
    EXPECT_FALSE(security_private_storage_finish(storage)); EXPECT_EQ(errno, EWOULDBLOCK);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_DATA_PATH "/late", "x", 1, false, true).committed);
    ASSERT_EQ(flock(fileno(lock), LOCK_UN), 0); fclose(lock);
    EXPECT_TRUE(security_private_storage_finish(storage)); EXPECT_TRUE(security_private_storage_finish(storage));
    security_private_storage_close(storage);
    mem_free(path); mem_free(owner); mem_free(alias); mem_free(cache); mem_free(outside);
}

TEST_F(SecurityAdmissionTest, IncompleteDiscardableMountIsReplacedButCorruptMarkerFailsClosed) {
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    SecurityIOClient client = security_admission_client(admission);
    EXPECT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/kept", "data", 4, false, true).committed);
    EXPECT_TRUE(client.write(client.context, SECURITY_APP_CACHE_PATH "/incomplete", "discard", 7, false, true).committed);
    char* cache = private_directory(nullptr, "cache"); char* owner = file_path_dirname(cache);
    char* marker = file_path_join(owner, "cache.owner");
    security_admission_close(admission); admission = nullptr;
    ASSERT_EQ(unlink(marker), 0);
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    EXPECT_TRUE(read(SECURITY_APP_DATA_PATH "/kept")); EXPECT_FALSE(read(SECURITY_APP_CACHE_PATH "/incomplete"));
    security_admission_close(admission); admission = nullptr;
    ASSERT_EQ(write_binary_file(marker, "forged", 6), 0);
    open(); EXPECT_EQ(admission, nullptr);
    mem_free(marker); mem_free(owner); mem_free(cache);
}

TEST_F(SecurityAdmissionTest, KilledHostTemporaryStorageIsRecoveredWithoutTouchingLiveSessions) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* live = security_private_storage_open(&options); ASSERT_NE(live, nullptr);
    SecurityIOClient client = security_private_storage_client(live);
    struct ChildConfig { const char* home; SecurityPrivateStorageConfig options; } config = {home, options};
    auto action = [](void* context, int ready) -> int {
        ChildConfig* config = (ChildConfig*)context; SecurityStoreResult result;
        SecurityPrivateStorageConfig options = config->options;
        options.store = security_grant_store_test_open(config->home, &result);
        SecurityPrivateStorage* orphan = options.store ? security_private_storage_open(&options) : nullptr;
        if (!orphan) return 2;
        SecurityIOClient io = security_private_storage_client(orphan);
        if (!io.write(io.context, SECURITY_APP_TEMP_PATH "/orphan", "123456789", 9, false, true).committed ||
                !io.write(io.context, SECURITY_APP_DATA_PATH "/kept", "data", 4, false, true).committed ||
                !io.write(io.context, SECURITY_APP_CACHE_PATH "/kept", "cache", 5, false, true).committed) return 3;
        char signal = 1;
        if (!SafetyChild::publish(ready, &signal, 1)) return 4;
        for (;;) pause();
    };
    SafetyChild child; ASSERT_EQ(child.start(action, &config), 0);
    char signal = 0; ASSERT_TRUE(child.receive(&signal, 1)); ASSERT_EQ(signal, 1);
    SecurityPrivateStorageStatus observed;
    ASSERT_TRUE(security_private_storage_inspect(&options, &observed));
    EXPECT_EQ(observed.live_sessions, 2U); EXPECT_EQ(observed.abandoned_sessions, 0U);
    EXPECT_EQ(observed.usage[SECURITY_PRIVATE_TEMP].bytes, 9U);
    // The child is alive and owns its lease: recovery must preserve it, and
    // quota must count its bytes even though this channel cannot read them.
    char* bytes = nullptr; size_t size = 0;
    EXPECT_FALSE(security_io_read_bytes(&client, SECURITY_APP_TEMP_PATH "/orphan", false, 32,
        MEM_CAT_SYSTEM, &bytes, &size, nullptr));
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_TEMP_PATH "/live", "12345678", 8, false, true).committed);
    SecurityPrivateStorage* concurrent = security_private_storage_open(&options); ASSERT_NE(concurrent, nullptr);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_TEMP_PATH "/live", "12345678", 8, false, true).committed);
    security_private_storage_close(concurrent);
    int status = 0; ASSERT_EQ(child.reap(&status, true), 0); EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGKILL);
    ASSERT_TRUE(security_private_storage_inspect(&options, &observed));
    EXPECT_EQ(observed.live_sessions, 1U); EXPECT_EQ(observed.abandoned_sessions, 1U);
    EXPECT_EQ(observed.usage[SECURITY_PRIVATE_TEMP].bytes, 9U);
    SecurityPrivateStorage* recovered = security_private_storage_open(&options); ASSERT_NE(recovered, nullptr);
    EXPECT_TRUE(client.write(client.context, SECURITY_APP_TEMP_PATH "/live", "12345678", 8, false, true).committed);
    expect_private_text(&client, SECURITY_APP_DATA_PATH "/kept", "data");
    expect_private_text(&client, SECURITY_APP_CACHE_PATH "/kept", "cache");
    security_private_storage_close(recovered); security_private_storage_close(live);
    char* temp = private_directory(&options.binding, "temp");
    FileAnchorOptions flags = {FILE_ANCHOR_PRIVATE | FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_SAME_DEVICE};
    FileAnchor* root = file_anchor_open(temp, &flags); ASSERT_NE(root, nullptr);
    uint32_t entries = 0;
    auto count = [](void* context, const char*, const FileRevision*) -> bool { ++*(uint32_t*)context; return true; };
    EXPECT_TRUE(file_anchor_visit_strict(root, 16, count, &entries)); EXPECT_EQ(entries, 0U);
    file_anchor_close(root); mem_free(temp);
}

TEST_F(SecurityAdmissionTest, PrivateInspectionDoesNotCreateAdmissionAndReportsQuotaExcess) {
    SecurityPrivateStorageConfig options = storage_config(); SecurityPrivateStorageStatus status;
    EXPECT_FALSE(security_private_storage_inspect(&options, &status)); EXPECT_EQ(errno, ENOENT);
    char* directory = private_directory(&options.binding); char* owner = file_path_dirname(directory);
    EXPECT_NE(access(owner, F_OK), 0); EXPECT_EQ(errno, ENOENT);
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    SecurityIOClient client = security_private_storage_client(storage);
    ASSERT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/one/a", "data", 4, false, true).committed);
    ASSERT_TRUE(client.write(client.context, SECURITY_APP_TEMP_PATH "/one", "temp", 4, false, true).committed);
    ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_EQ(status.live_sessions, 1U); EXPECT_EQ(status.abandoned_sessions, 0U);
    EXPECT_EQ(status.usage[SECURITY_PRIVATE_DATA].bytes, 4U); EXPECT_EQ(status.usage[SECURITY_PRIVATE_DATA].entries, 2U);
    EXPECT_EQ(status.usage[SECURITY_PRIVATE_TEMP].bytes, 4U); EXPECT_EQ(status.usage[SECURITY_PRIVATE_TEMP].entries, 4U);
    EXPECT_FALSE(status.reset_pending); EXPECT_FALSE(status.usage[SECURITY_PRIVATE_DATA].over_limit);
    SecurityPrivateStorageConfig reduced = options; reduced.limits[SECURITY_PRIVATE_DATA] = {2, 1};
    ASSERT_TRUE(security_private_storage_inspect(&reduced, &status));
    EXPECT_TRUE(status.usage[SECURITY_PRIVATE_DATA].over_limit);
    security_private_storage_close(storage);
    ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_EQ(status.live_sessions, 0U); EXPECT_EQ(status.usage[SECURITY_PRIVATE_TEMP].entries, 0U);
    EXPECT_EQ(status.usage[SECURITY_PRIVATE_DATA].bytes, 4U);
    mem_free(owner); mem_free(directory);
}

TEST_F(SecurityAdmissionTest, SavedPrivateQuotaSurvivesHostRestartAndControlsAllThreeMounts) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    security_private_storage_close(storage);
    SecurityPrivateStorageLimit limits[] = {{4, 2}, {3, 2}, {4, 8}}; uint64_t generation = 99;
    ASSERT_TRUE(security_private_storage_set_quota(&options, limits, 0, &generation)); ASSERT_EQ(generation, 1U);
    security_grant_store_close(store); SecurityStoreResult result; store = security_grant_store_test_open(home, &result);
    ASSERT_NE(store, nullptr); options.store = store;
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_EQ(status.quota_generation, 1U); EXPECT_TRUE(status.custom_quota); EXPECT_EQ(status.limits[0].max_bytes, 4U);
    storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    SecurityIOClient client = security_private_storage_client(storage);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_DATA_PATH "/data", "12345", 5, false, true).committed);
    EXPECT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/data", "1234", 4, false, true).committed);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_CACHE_PATH "/data", "1234", 4, false, true).committed);
    EXPECT_TRUE(client.write(client.context, SECURITY_APP_CACHE_PATH "/data", "123", 3, false, true).committed);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_TEMP_PATH "/data", "12345", 5, false, true).committed);
    EXPECT_TRUE(client.write(client.context, SECURITY_APP_TEMP_PATH "/data", "1234", 4, false, true).committed);
    security_private_storage_close(storage);
}

TEST_F(SecurityAdmissionTest, PrivateQuotaChangesRefuseLiveSessionsAndStaleGenerations) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* first = security_private_storage_open(&options); ASSERT_NE(first, nullptr);
    SecurityPrivateStorage* second = security_private_storage_open(&options); ASSERT_NE(second, nullptr);
    SecurityIOClient client = security_private_storage_client(first);
    ASSERT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/kept", "data", 4, false, true).committed);
    uint64_t generation = 99;
    EXPECT_FALSE(security_private_storage_set_quota(&options, options.limits, 0, &generation)); EXPECT_EQ(errno, EBUSY); EXPECT_EQ(generation, 0U);
    security_private_storage_close(first);
    EXPECT_FALSE(security_private_storage_set_quota(&options, options.limits, 0, &generation)); EXPECT_EQ(errno, EBUSY);
    security_private_storage_close(second);
    ASSERT_TRUE(security_private_storage_set_quota(&options, options.limits, 0, &generation)); EXPECT_EQ(generation, 1U);
    EXPECT_FALSE(security_private_storage_set_quota(&options, nullptr, 0, &generation)); EXPECT_EQ(errno, ESTALE); EXPECT_EQ(generation, 0U);
    ASSERT_TRUE(security_private_storage_set_quota(&options, nullptr, 1, &generation)); EXPECT_EQ(generation, 2U);
    EXPECT_FALSE(security_private_storage_set_quota(&options, options.limits, 1, &generation)); EXPECT_EQ(errno, ESTALE);
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_FALSE(status.custom_quota); EXPECT_EQ(status.quota_generation, 2U); EXPECT_EQ(status.usage[0].bytes, 4U);
    SecurityPrivateStorageLimit invalid[SECURITY_PRIVATE_COUNT]; memcpy(invalid, options.limits, sizeof(invalid));
    invalid[SECURITY_PRIVATE_TEMP].max_entries = 2;
    EXPECT_FALSE(security_private_storage_set_quota(&options, invalid, 2, &generation)); EXPECT_EQ(errno, EINVAL);
    invalid[SECURITY_PRIVATE_TEMP].max_entries = SECURITY_PRIVATE_MAX_ENTRIES + 1;
    EXPECT_FALSE(security_private_storage_set_quota(&options, invalid, 2, &generation)); EXPECT_EQ(errno, EINVAL);
    invalid[SECURITY_PRIVATE_TEMP] = options.limits[SECURITY_PRIVATE_TEMP]; invalid[0].max_bytes = 0;
    EXPECT_FALSE(security_private_storage_set_quota(&options, invalid, 2, &generation)); EXPECT_EQ(errno, EINVAL);
}

TEST_F(SecurityAdmissionTest, PrivateQuotaReductionPreservesFilesAndAllowsAQuotaCompliantShrink) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    SecurityIOClient client = security_private_storage_client(storage);
    ASSERT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/kept", "12345678", 8, false, true).committed);
    security_private_storage_close(storage);
    SecurityPrivateStorageLimit limits[SECURITY_PRIVATE_COUNT]; memcpy(limits, options.limits, sizeof(limits)); limits[0] = {4, 1};
    uint64_t generation; ASSERT_TRUE(security_private_storage_set_quota(&options, limits, 0, &generation));
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_TRUE(status.usage[0].over_limit); EXPECT_EQ(status.usage[0].bytes, 8U);
    storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr); client = security_private_storage_client(storage);
    expect_private_text(&client, SECURITY_APP_DATA_PATH "/kept", "12345678");
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_DATA_PATH "/extra", "x", 1, false, true).committed);
    EXPECT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/kept", "1234", 4, false, true).committed);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_DATA_PATH "/kept", "5", 1, true, false).committed);
    expect_private_text(&client, SECURITY_APP_DATA_PATH "/kept", "1234");
    security_private_storage_close(storage);
    ASSERT_TRUE(security_private_storage_inspect(&options, &status)); EXPECT_FALSE(status.usage[0].over_limit);
}

TEST_F(SecurityAdmissionTest, PrivateQuotaIsPrincipalAndProfileBoundAndSurvivesCodeChanges) {
    SecurityPrivateStorageConfig options = storage_config(), peer = options; peer.binding.principal[0]++;
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr); security_private_storage_close(storage);
    storage = security_private_storage_open(&peer); ASSERT_NE(storage, nullptr); security_private_storage_close(storage);
    SecurityPrivateStorageLimit limits[SECURITY_PRIVATE_COUNT]; memcpy(limits, options.limits, sizeof(limits)); limits[0].max_bytes = 4;
    uint64_t generation; ASSERT_TRUE(security_private_storage_set_quota(&options, limits, 0, &generation));
    options.binding.code[0]++; options.binding.root[0]++;
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_TRUE(status.custom_quota); EXPECT_EQ(status.limits[0].max_bytes, 4U);
    ASSERT_TRUE(security_private_storage_inspect(&peer, &status)); EXPECT_FALSE(status.custom_quota); EXPECT_EQ(status.limits[0].max_bytes, 8U);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(quota_blob(&options, &bytes, &size));
    ASSERT_TRUE(quota_blob(&peer, nullptr, nullptr, bytes, size));
    EXPECT_FALSE(security_private_storage_inspect(&peer, &status)); EXPECT_EQ(errno, EBADMSG);
    EXPECT_EQ(security_private_storage_open(&peer), nullptr); EXPECT_EQ(errno, EBADMSG);
    char* second_home = file_path_join(base, "second-home"); ASSERT_TRUE(create_dir(second_home)); ASSERT_EQ(chmod(second_home, 0700), 0);
    SecurityStoreResult result; SecurityGrantStore* second = security_grant_store_test_open(second_home, &result); ASSERT_NE(second, nullptr);
    SecurityPrivateStorageConfig elsewhere = options; elsewhere.store = second;
    storage = security_private_storage_open(&elsewhere); ASSERT_NE(storage, nullptr); security_private_storage_close(storage);
    ASSERT_TRUE(quota_blob(&elsewhere, nullptr, nullptr, bytes, size));
    EXPECT_FALSE(security_private_storage_inspect(&elsewhere, &status)); EXPECT_EQ(errno, EBADMSG);
    EXPECT_FALSE(security_private_storage_set_quota(&elsewhere, nullptr, 1, &generation)); EXPECT_EQ(errno, EBADMSG);
    security_grant_store_close(second); mem_free(second_home); mem_free(bytes);
}

TEST_F(SecurityAdmissionTest, CorruptPrivateQuotaCannotFallBackOrBeReplacedByRestoreDefaults) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr); security_private_storage_close(storage);
    uint64_t generation; ASSERT_TRUE(security_private_storage_set_quota(&options, options.limits, 0, &generation));
    ASSERT_TRUE(quota_blob(&options, nullptr, nullptr, "broken", 6));
    SecurityPrivateStorageStatus status; memset(&status, 0xff, sizeof(status));
    EXPECT_FALSE(security_private_storage_inspect(&options, &status)); EXPECT_EQ(errno, EBADMSG);
    EXPECT_EQ(status.quota_generation, 0U); EXPECT_EQ(status.limits[0].max_bytes, 0U);
    EXPECT_FALSE(security_private_storage_set_quota(&options, nullptr, 1, &generation)); EXPECT_EQ(errno, EBADMSG);
    EXPECT_EQ(security_private_storage_open(&options), nullptr); EXPECT_EQ(errno, EBADMSG);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(quota_blob(&options, &bytes, &size)); EXPECT_EQ(size, 6U);
    EXPECT_EQ(memcmp(bytes, "broken", 6), 0); mem_free(bytes);
}

TEST_F(SecurityAdmissionTest, PersistentDataResetPreservesSavedQuotaAndRefusesConcurrentQuotaEdit) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr); security_private_storage_close(storage);
    uint64_t generation; ASSERT_TRUE(security_private_storage_set_quota(&options, options.limits, 0, &generation));
    char* before = nullptr; size_t size = 0; ASSERT_TRUE(quota_blob(&options, &before, &size));
    uint8_t id[16]; ASSERT_TRUE(security_private_storage_reset_begin(&options, id));
    EXPECT_FALSE(security_private_storage_set_quota(&options, nullptr, 1, &generation)); EXPECT_EQ(errno, EBUSY);
    bool complete = false; ASSERT_TRUE(security_private_storage_reset_step(&options, id, 8, &complete)); ASSERT_TRUE(complete);
    char* after = nullptr; size_t count = 0; ASSERT_TRUE(quota_blob(&options, &after, &count));
    ASSERT_EQ(size, count); EXPECT_EQ(memcmp(before, after, count), 0); mem_free(before); mem_free(after);
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_EQ(status.quota_generation, 1U); EXPECT_TRUE(status.custom_quota);
}

TEST_F(SecurityAdmissionTest, ConcurrentPrivateQuotaEditorsCannotLoseAnAcknowledgedUpdate) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr); security_private_storage_close(storage);
    SecurityStoreResult result; SecurityGrantStore* second_store = security_grant_store_test_open(home, &result); ASSERT_NE(second_store, nullptr);
    atomic_int32 go = {};
    struct Job { SecurityPrivateStorageConfig config; atomic_int32* go; bool success; int error; uint64_t generation; } jobs[2] = {};
    for (unsigned i = 0; i < 2; i++) {
        jobs[i].config = options; jobs[i].config.limits[0].max_bytes = 4 + i; jobs[i].go = &go;
    }
    jobs[1].config.store = second_store;
    auto action = [](void* context) {
        Job* job = (Job*)context;
        if (!SafetyAsync::wait(job->go)) { job->error = ETIMEDOUT; return; }
        job->success = security_private_storage_set_quota(&job->config, job->config.limits, 0, &job->generation); job->error = errno;
    };
    SafetyAsync first, second; ASSERT_EQ(first.start(action, &jobs[0]), 0); ASSERT_EQ(second.start(action, &jobs[1]), 0);
    ASSERT_TRUE(SafetyAsync::wait(&first.started)); ASSERT_TRUE(SafetyAsync::wait(&second.started)); atomic_store32(&go, 1);
    ASSERT_TRUE(SafetyAsync::wait(&first.done)); ASSERT_TRUE(SafetyAsync::wait(&second.done)); ASSERT_EQ(first.join(), 0); ASSERT_EQ(second.join(), 0);
    EXPECT_NE(jobs[0].success, jobs[1].success);
    for (const Job& job : jobs) if (!job.success) EXPECT_TRUE(job.error == ESTALE || job.error == EAGAIN || job.error == EWOULDBLOCK);
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status)); EXPECT_EQ(status.quota_generation, 1U);
    EXPECT_EQ(status.limits[0].max_bytes, jobs[0].success ? 4U : 5U); security_grant_store_close(second_store);
}

TEST_F(SecurityAdmissionTest, AdmittedScriptRestoresSavedPrivateQuotaAfterCodeRevalidation) {
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    SecurityPrivateStorageConfig options = storage_config(); ASSERT_TRUE(security_admission_binding(admission, &options.binding));
    security_admission_close(admission); admission = nullptr;
    options.limits[0].max_bytes = 4; uint64_t generation;
    ASSERT_TRUE(security_private_storage_set_quota(&options, options.limits, 0, &generation));
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    SecurityIOClient client = security_admission_client(admission);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_DATA_PATH "/data", "12345", 5, false, true).committed);
    security_admission_close(admission); admission = nullptr;
    const char* changed = "pn main() { 42 }\n"; ASSERT_EQ(write_binary_file(entry, changed, strlen(changed)), 0);
    ASSERT_EQ(open(true).store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr); client = security_admission_client(admission);
    EXPECT_FALSE(client.write(client.context, SECURITY_APP_DATA_PATH "/data", "12345", 5, false, true).committed);
    EXPECT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/data", "1234", 4, false, true).committed);
}

TEST_F(SecurityAdmissionTest, PrivateQuotaGenerationCannotWrapAndStateSlotsCannotNameOtherRecords) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr); security_private_storage_close(storage);
    uint64_t generation; ASSERT_TRUE(security_private_storage_set_quota(&options, options.limits, 0, &generation));
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(quota_blob(&options, &bytes, &size)); ASSERT_EQ(size, 160U);
    // construct the terminal valid generation without iterating 2^64 edits
    write_le64((uint8_t*)bytes + 80, UINT64_MAX); ASSERT_TRUE(digest_sha256(bytes, 128, (uint8_t*)bytes + 128));
    ASSERT_TRUE(quota_blob(&options, nullptr, nullptr, bytes, size)); mem_free(bytes);
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status)); EXPECT_EQ(status.quota_generation, UINT64_MAX);
    EXPECT_FALSE(security_private_storage_set_quota(&options, nullptr, UINT64_MAX, &generation)); EXPECT_EQ(errno, EOVERFLOW); EXPECT_EQ(generation, 0U);
    SecurityStoreResult result;
    EXPECT_EQ(security_state_begin(store, SECURITY_STATE_PRIVATE_QUOTA, &result), nullptr); EXPECT_EQ(result, SECURITY_STORE_INVALID);
    EXPECT_EQ(security_state_begin_principal(store, SECURITY_STATE_PACKAGE_UPDATES, SECURITY_PRINCIPAL_SCRIPT, options.binding.principal, &result), nullptr);
    EXPECT_EQ(result, SECURITY_STORE_INVALID);
    uint8_t absent[32] = {};
    EXPECT_EQ(security_state_begin_principal(store, SECURITY_STATE_PRIVATE_QUOTA, SECURITY_PRINCIPAL_SCRIPT, absent, &result), nullptr);
    EXPECT_EQ(result, SECURITY_STORE_INVALID);
}

TEST_F(SecurityAdmissionTest, FullPrivateQuotaRemainsRecoverableWithOneAbandonedAtomicStage) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr); security_private_storage_close(storage);
    options.limits[0].max_entries = SECURITY_PRIVATE_MAX_ENTRIES;
    uint64_t generation; ASSERT_TRUE(security_private_storage_set_quota(&options, options.limits, 0, &generation));
    char* path = private_directory(&options.binding);
    FileAnchorOptions flags = {FILE_ANCHOR_PRIVATE | FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_SAME_DEVICE};
    FileAnchor* directory = file_anchor_open(path, &flags); ASSERT_NE(directory, nullptr);
    // Build a reachable full class plus the one internal stage a killed,
    // owner-locked atomic replacement can leave. No payload is adopted.
    for (unsigned i = 0; i < SECURITY_PRIVATE_MAX_ENTRIES; i++) {
        char name[32]; int count = snprintf(name, sizeof(name), "file-%u", i);
        ASSERT_GT(count, 0); FILE* file = file_anchor_create_regular(directory, name); ASSERT_NE(file, nullptr); fclose(file);
    }
    FILE* stage = file_anchor_create_regular(directory, FILE_ATOMIC_STAGE_PREFIX "00000000000000000000000000000000");
    ASSERT_NE(stage, nullptr); fclose(stage); file_anchor_close(directory); mem_free(path);
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_EQ(status.usage[0].entries, SECURITY_PRIVATE_MAX_ENTRIES + 1U); EXPECT_TRUE(status.usage[0].over_limit);
    uint8_t id[16]; ASSERT_TRUE(security_private_storage_reset_begin(&options, id));
    bool complete = false; ASSERT_TRUE(security_private_storage_reset_step(&options, id, SECURITY_PRIVATE_MAX_ENTRIES, &complete));
    EXPECT_FALSE(complete); ASSERT_TRUE(security_private_storage_reset_step(&options, id, 1, &complete)); EXPECT_TRUE(complete);
    ASSERT_TRUE(security_private_storage_inspect(&options, &status)); EXPECT_EQ(status.usage[0].entries, 0U);
    EXPECT_EQ(status.quota_generation, 1U);
}

TEST_F(SecurityAdmissionTest, PersistentResetRefusesLiveSessionsAndLeavesDataIntact) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* first = security_private_storage_open(&options); ASSERT_NE(first, nullptr);
    SecurityPrivateStorage* second = security_private_storage_open(&options); ASSERT_NE(second, nullptr);
    SecurityIOClient client = security_private_storage_client(first);
    ASSERT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/kept", "data", 4, false, true).committed);
    uint8_t id[16]; EXPECT_FALSE(security_private_storage_reset_begin(&options, id)); EXPECT_EQ(errno, EBUSY);
    expect_private_text(&client, SECURITY_APP_DATA_PATH "/kept", "data");
    security_private_storage_close(second);
    EXPECT_FALSE(security_private_storage_reset_begin(&options, id)); EXPECT_EQ(errno, EBUSY);
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_FALSE(status.reset_pending); EXPECT_EQ(status.live_sessions, 1U);
    security_private_storage_close(first);
    ASSERT_TRUE(security_private_storage_reset_begin(&options, id));
    bool complete = false; ASSERT_TRUE(security_private_storage_reset_step(&options, id, 1, &complete)); EXPECT_TRUE(complete);
}

TEST_F(SecurityAdmissionTest, PersistentResetPreservesCacheOtherPrincipalsAndRejectsStaleIds) {
    SecurityPrivateStorageConfig options = storage_config(), other = options; other.binding.principal[0]++;
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    SecurityPrivateStorage* peer = security_private_storage_open(&other); ASSERT_NE(peer, nullptr);
    SecurityIOClient client = security_private_storage_client(storage), peer_client = security_private_storage_client(peer);
    ASSERT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/old", "old", 3, false, true).committed);
    ASSERT_TRUE(client.write(client.context, SECURITY_APP_CACHE_PATH "/kept", "cache", 5, false, true).committed);
    ASSERT_TRUE(peer_client.write(peer_client.context, SECURITY_APP_DATA_PATH "/kept", "peer", 4, false, true).committed);
    security_private_storage_close(storage);
    uint8_t first[16], second[16]; ASSERT_TRUE(security_private_storage_reset_begin(&options, first));
    EXPECT_FALSE(security_private_storage_reset_begin(&options, second)); EXPECT_EQ(errno, EALREADY);
    EXPECT_EQ(security_private_storage_open(&options), nullptr); EXPECT_EQ(errno, EBUSY);
    bool complete = true;
    EXPECT_FALSE(security_private_storage_reset_step(&other, first, 8, &complete)); EXPECT_FALSE(complete);
    EXPECT_FALSE(security_private_storage_reset_step(&options, first, 0, &complete)); EXPECT_EQ(errno, EINVAL);
    ASSERT_TRUE(security_private_storage_reset_step(&options, first, 1, &complete)); ASSERT_TRUE(complete);
    storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr); client = security_private_storage_client(storage);
    ASSERT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/new", "new", 3, false, true).committed);
    expect_private_text(&client, SECURITY_APP_CACHE_PATH "/kept", "cache");
    expect_private_text(&peer_client, SECURITY_APP_DATA_PATH "/kept", "peer");
    security_private_storage_close(storage);
    EXPECT_FALSE(security_private_storage_reset_step(&options, first, 8, &complete)); EXPECT_EQ(errno, ENOENT);
    ASSERT_TRUE(security_private_storage_reset_begin(&options, second)); EXPECT_NE(memcmp(first, second, 16), 0);
    EXPECT_FALSE(security_private_storage_reset_step(&options, first, 8, &complete)); EXPECT_EQ(errno, EACCES);
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_TRUE(status.reset_pending); EXPECT_EQ(status.usage[SECURITY_PRIVATE_DATA].bytes, 3U);
    ASSERT_TRUE(security_private_storage_reset_step(&options, second, 8, &complete)); EXPECT_TRUE(complete);
    security_private_storage_close(peer);
}

TEST_F(SecurityAdmissionTest, InterruptedPersistentResetRequiresTrustedResumeAfterHostDeath) {
    SecurityPrivateStorageConfig options = storage_config(); options.limits[SECURITY_PRIVATE_DATA] = {64, 16};
    struct ChildConfig { const char* home; SecurityPrivateStorageConfig options; } config = {home, options};
    auto action = [](void* context, int ready) -> int {
        ChildConfig* config = (ChildConfig*)context; SecurityStoreResult result;
        SecurityPrivateStorageConfig options = config->options;
        options.store = security_grant_store_test_open(config->home, &result);
        SecurityPrivateStorage* storage = options.store ? security_private_storage_open(&options) : nullptr;
        if (!storage) return 2;
        SecurityIOClient io = security_private_storage_client(storage);
        if (!io.write(io.context, SECURITY_APP_DATA_PATH "/one/a", "a", 1, false, true).committed ||
            !io.write(io.context, SECURITY_APP_DATA_PATH "/two/b", "b", 1, false, true).committed ||
            !io.write(io.context, SECURITY_APP_CACHE_PATH "/kept", "cache", 5, false, true).committed) return 3;
        security_private_storage_close(storage);
        uint8_t id[16]; bool complete = true;
        if (!security_private_storage_reset_begin(&options, id) ||
            !security_private_storage_reset_step(&options, id, 1, &complete) || complete) return 4;
        if (!SafetyChild::publish(ready, id, sizeof(id))) return 5;
        for (;;) pause();
    };
    SafetyChild child; ASSERT_EQ(child.start(action, &config), 0);
    uint8_t id[16]; ASSERT_TRUE(child.receive(id, sizeof(id)));
    int result; ASSERT_EQ(child.reap(&result, true), 0); EXPECT_TRUE(WIFSIGNALED(result));
    EXPECT_EQ(WTERMSIG(result), SIGKILL);
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_TRUE(status.reset_pending); EXPECT_EQ(memcmp(status.reset_id, id, 16), 0);
    EXPECT_EQ(status.usage[SECURITY_PRIVATE_DATA].entries, 3U); EXPECT_EQ(status.live_sessions, 0U);
    EXPECT_EQ(security_private_storage_open(&options), nullptr); EXPECT_EQ(errno, EBUSY);
    bool complete = false;
    for (unsigned step = 0; !complete && step < 4; step++)
        ASSERT_TRUE(security_private_storage_reset_step(&options, id, 1, &complete));
    ASSERT_TRUE(complete); ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_FALSE(status.reset_pending); EXPECT_EQ(status.usage[SECURITY_PRIVATE_DATA].entries, 0U);
    EXPECT_EQ(status.usage[SECURITY_PRIVATE_CACHE].bytes, 5U);
    SecurityPrivateStorage* reopened = security_private_storage_open(&options); ASSERT_NE(reopened, nullptr);
    SecurityIOClient io = security_private_storage_client(reopened); expect_private_text(&io, SECURITY_APP_CACHE_PATH "/kept", "cache");
    security_private_storage_close(reopened);
}

TEST_F(SecurityAdmissionTest, PersistentResetPreflightRefusesAliasesWithoutDeletingFiles) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    SecurityIOClient client = security_private_storage_client(storage);
    ASSERT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/kept", "data", 4, false, true).committed);
    security_private_storage_close(storage);
    char* directory = private_directory(&options.binding); char* alias = file_path_join(directory, "alias");
    char* outside = seed("outside", "private"); ASSERT_EQ(symlink(outside, alias), 0);
    uint8_t id[16]; EXPECT_FALSE(security_private_storage_reset_begin(&options, id));
    ASSERT_EQ(unlink(alias), 0);
    SecurityPrivateStorageStatus status; ASSERT_TRUE(security_private_storage_inspect(&options, &status));
    EXPECT_FALSE(status.reset_pending); EXPECT_EQ(status.usage[SECURITY_PRIVATE_DATA].bytes, 4U);
    storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr); client = security_private_storage_client(storage);
    expect_private_text(&client, SECURITY_APP_DATA_PATH "/kept", "data");
    security_private_storage_close(storage); mem_free(outside); mem_free(alias); mem_free(directory);
}

TEST_F(SecurityAdmissionTest, CorruptPersistentResetIntentFailsClosedWithoutPublishingInspection) {
    SecurityPrivateStorageConfig options = storage_config();
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    security_private_storage_close(storage);
    uint8_t id[16]; ASSERT_TRUE(security_private_storage_reset_begin(&options, id));
    char* directory = private_directory(&options.binding); char* owner = file_path_dirname(directory);
    char* intent = file_path_join(owner, "reset.pending");
    ASSERT_EQ(write_binary_file(intent, "broken", 6), 0);
    SecurityPrivateStorageStatus status; memset(&status, 0xff, sizeof(status));
    EXPECT_FALSE(security_private_storage_inspect(&options, &status)); EXPECT_EQ(errno, EBADMSG);
    EXPECT_EQ(status.usage[SECURITY_PRIVATE_DATA].bytes, 0U); EXPECT_FALSE(status.reset_pending);
    bool complete = true; EXPECT_FALSE(security_private_storage_reset_step(&options, id, 4, &complete));
    EXPECT_FALSE(complete); EXPECT_EQ(security_private_storage_open(&options), nullptr); EXPECT_EQ(errno, EBADMSG);
    mem_free(intent); mem_free(owner); mem_free(directory);
}

TEST_F(SecurityAdmissionTest, PersistentResetRecordCannotBeReplayedIntoAnotherPrincipal) {
    SecurityPrivateStorageConfig options = storage_config(), other = options; other.binding.principal[0]++;
    SecurityPrivateStorage* storage = security_private_storage_open(&options); ASSERT_NE(storage, nullptr);
    SecurityPrivateStorage* peer = security_private_storage_open(&other); ASSERT_NE(peer, nullptr);
    SecurityIOClient client = security_private_storage_client(peer);
    ASSERT_TRUE(client.write(client.context, SECURITY_APP_DATA_PATH "/kept", "peer", 4, false, true).committed);
    security_private_storage_close(storage); security_private_storage_close(peer);
    uint8_t id[16]; ASSERT_TRUE(security_private_storage_reset_begin(&options, id));
    char* record = private_directory(&options.binding, "reset.pending");
    char* replay = private_directory(&other.binding, "reset.pending");
    size_t size = 0; char* bytes = read_binary_file(record, &size); ASSERT_NE(bytes, nullptr); ASSERT_EQ(size, 160U);
    ASSERT_EQ(write_binary_file(replay, bytes, size), 0); ASSERT_EQ(chmod(replay, 0600), 0);
    SecurityPrivateStorageStatus status;
    EXPECT_FALSE(security_private_storage_inspect(&other, &status)); EXPECT_EQ(errno, EBADMSG);
    bool complete = true;
    EXPECT_FALSE(security_private_storage_reset_step(&other, id, 8, &complete)); EXPECT_EQ(errno, EBADMSG); EXPECT_FALSE(complete);
    EXPECT_EQ(security_private_storage_open(&other), nullptr); EXPECT_EQ(errno, EBADMSG);
    ASSERT_EQ(unlink(replay), 0);
    peer = security_private_storage_open(&other); ASSERT_NE(peer, nullptr); client = security_private_storage_client(peer);
    expect_private_text(&client, SECURITY_APP_DATA_PATH "/kept", "peer");
    security_private_storage_close(peer); mem_free(bytes); mem_free(replay); mem_free(record);
}

TEST_F(SecurityAdmissionTest, RecoveryDiscardsUnpublishedSessionsAndCapsLiveSessionCount) {
    SecurityPrivateStorageConfig options = storage_config(); options.limits[SECURITY_PRIVATE_TEMP] = {1024, 256};
    SecurityPrivateStorage* sessions[16] = {};
    sessions[0] = security_private_storage_open(&options); ASSERT_NE(sessions[0], nullptr);
    char* temp = private_directory(&options.binding, "temp");
    FileAnchorOptions flags = {FILE_ANCHOR_PRIVATE | FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_SAME_DEVICE};
    FileAnchor* root = file_anchor_open(temp, &flags); ASSERT_NE(root, nullptr);
    for (unsigned phase = 0; phase < 2; phase++) {
        char name[35] = "r-"; memset(name + 2, phase ? 'b' : 'a', 32); name[34] = 0;
        FileAnchor* incomplete = file_anchor_create_directory(root, name, flags.flags); ASSERT_NE(incomplete, nullptr);
        if (phase) { FILE* lease = file_anchor_create_regular(incomplete, ".lease"); ASSERT_NE(lease, nullptr); fclose(lease); }
        file_anchor_close(incomplete);
        SecurityPrivateStorage* restarted = security_private_storage_open(&options); ASSERT_NE(restarted, nullptr);
        FileRevision revision; EXPECT_FALSE(file_anchor_stat(root, name, &revision)); EXPECT_EQ(errno, ENOENT);
        security_private_storage_close(restarted);
    }
    for (unsigned i = 1; i < 16; i++) { sessions[i] = security_private_storage_open(&options); ASSERT_NE(sessions[i], nullptr); }
    EXPECT_EQ(security_private_storage_open(&options), nullptr); EXPECT_EQ(errno, EFBIG);
    security_private_storage_close(sessions[0]);
    sessions[0] = security_private_storage_open(&options); ASSERT_NE(sessions[0], nullptr);
    for (SecurityPrivateStorage* session : sessions) security_private_storage_close(session);
    file_anchor_close(root); mem_free(temp);
}

TEST_F(SecurityAdmissionTest, PrivateStorageNeverAdoptsASubstitutedOwnerRecord) {
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    char* directory = private_directory(); char* owner = file_path_dirname(directory);
    char* marker = file_path_join(owner, "owner"); ASSERT_EQ(write_binary_file(marker, "forged", 6), 0);
    open(); EXPECT_EQ(admission, nullptr);
    mem_free(marker); mem_free(owner); mem_free(directory);
}

TEST_F(SecurityAdmissionTest, NewOutputDoesNotInvalidateIdentityButExistingOutputChallenges) {
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    SecurityGrantBinding before; ASSERT_TRUE(security_admission_binding(admission, &before));
    char* output = file_path_join(project, "new/report.txt");
    SecurityIOClient client = security_admission_client(admission);
    SecurityIOResult result = client.write(client.context, output, "first", 5, false, true);
    EXPECT_TRUE(result.committed); EXPECT_EQ(result.decision, SECURITY_ALLOWED);
    result = client.write(client.context, output, "second", 6, false, true);
    EXPECT_EQ(result.decision, SECURITY_APPROVAL_REQUIRED); EXPECT_FALSE(result.committed);
    SecurityOverwriteChallenge challenge;
    EXPECT_TRUE(security_admission_describe(admission, result.approval, &challenge));
    EXPECT_TRUE(security_admission_cancel(admission, result.approval));
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    SecurityGrantBinding after; ASSERT_TRUE(security_admission_binding(admission, &after));
    EXPECT_EQ(memcmp(before.code, after.code, 32), 0); EXPECT_EQ(memcmp(before.root, after.root, 32), 0);
    mem_free(output);
}

TEST_F(SecurityAdmissionTest, ChangedTransitiveSourceRequiresExplicitRevalidationAndClearsGrants) {
    char* helper = seed("sub/helper.ls", "pub let answer = 41\n", true);
    char* data = seed("outside.txt", "private");
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); select(data);
    ASSERT_EQ(write_binary_file(helper, "pub let answer = 42\n", 19), 0);
    EXPECT_EQ(open().store_result, SECURITY_STORE_IDENTITY_CHANGED); EXPECT_EQ(admission, nullptr);
    SecurityAdmissionReport report = open(true); ASSERT_NE(admission, nullptr);
    EXPECT_EQ(report.store_result, SECURITY_STORE_OK); EXPECT_EQ(report.restored, 0U);
    EXPECT_FALSE(read(data)); EXPECT_TRUE(read(helper, true));
    mem_free(data); mem_free(helper);
}

TEST_F(SecurityAdmissionTest, LiveSourceChangesCloseTheAdmissionWithoutReturningNewBytes) {
    char* helper = seed("helper.ls", "pub let answer = 41\n", true);
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    ASSERT_EQ(write_binary_file(helper, "pub let answer = 42\n", 19), 0);
    SecurityIOResult result;
    EXPECT_FALSE(read(helper, true, &result)); EXPECT_EQ(result.decision, SECURITY_REVOKED);
    EXPECT_FALSE(read(entry, true)); mem_free(helper);
}

TEST_F(SecurityAdmissionTest, NewSourceNeedsFreshAdmissionButDataCanChange) {
    char* data = seed("data.txt", "first", true);
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    ASSERT_EQ(write_binary_file(data, "second", 6), 0); EXPECT_TRUE(read(data));
    char* generated = seed("generated.ls", "pub let value = 1\n", true);
    EXPECT_FALSE(read(generated, true)); EXPECT_FALSE(read(entry, true));
    EXPECT_EQ(open().store_result, SECURITY_STORE_IDENTITY_CHANGED);
    ASSERT_EQ(open(true).store_result, SECURITY_STORE_OK); EXPECT_TRUE(read(generated, true));
    mem_free(generated); mem_free(data);
}

TEST_F(SecurityAdmissionTest, PersistentFileSelectionRestoresOnlyTheAcquiredObject) {
    char* data = seed("data.txt", "private"); char* sibling = seed("sibling.txt", "other");
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); EXPECT_FALSE(read(data));
    select(data); EXPECT_TRUE(read(data)); EXPECT_FALSE(read(sibling));
    SecurityAdmissionReport report = open(); ASSERT_NE(admission, nullptr);
    EXPECT_EQ(report.restored, 1U); EXPECT_TRUE(read(data)); EXPECT_FALSE(read(sibling));
    SecurityGrantSnapshot snapshot = {};
    ASSERT_EQ(security_admission_inspect(admission, &snapshot), SECURITY_STORE_OK);
    ASSERT_EQ(snapshot.count, 1U); EXPECT_STREQ(snapshot.grants[0].locator, data);
    security_grant_snapshot_destroy(&snapshot);
    char* moved = file_path_join(home, "original.txt"); ASSERT_EQ(rename(data, moved), 0);
    ASSERT_EQ(write_binary_file(data, "replacement", 11), 0);
    report = open(); ASSERT_NE(admission, nullptr);
    EXPECT_EQ(report.restored, 0U); EXPECT_EQ(report.rejected, 1U); EXPECT_FALSE(read(data));
    mem_free(moved); mem_free(data); mem_free(sibling);
}

TEST_F(SecurityAdmissionTest, SessionAndExpiredSelectionsDoNotRestore) {
    char* data = seed("data.txt", "private");
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); select(data, false); EXPECT_TRUE(read(data));
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); EXPECT_FALSE(read(data));
    select(data, true, false, SECURITY_READ, 101); now = 102;
    SecurityAdmissionReport report = open(); ASSERT_NE(admission, nullptr);
    EXPECT_EQ(report.restored, 0U); EXPECT_EQ(report.rejected, 1U); EXPECT_FALSE(read(data)); mem_free(data);
}

TEST_F(SecurityAdmissionTest, RememberedDirectoryOverwriteIsIndependentOfTheDefaultChallenge) {
    char* report_path = seed("report.txt", "old", true);
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); select(project, true, true, SECURITY_REPLACE | SECURITY_APPEND);
    SecurityIOClient client = security_admission_client(admission);
    SecurityIOResult result = client.write(client.context, report_path, "new", 3, false, true);
    EXPECT_TRUE(result.committed); EXPECT_EQ(result.decision, SECURITY_ALLOWED);
    SecurityAdmissionReport report = open(); ASSERT_NE(admission, nullptr); EXPECT_EQ(report.restored, 1U);
    client = security_admission_client(admission);
    result = client.write(client.context, report_path, "+", 1, true, false);
    EXPECT_TRUE(result.committed); EXPECT_EQ(result.decision, SECURITY_ALLOWED); mem_free(report_path);
}

TEST_F(SecurityAdmissionTest, RevocationPersistsAndCannotBeRestoredByAStaleLiveAdmission) {
    char* data = seed("data.txt", "private");
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); uint64_t id = select(data);
    EXPECT_EQ(security_admission_revoke(admission, id), SECURITY_STORE_OK); EXPECT_FALSE(read(data));
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); EXPECT_FALSE(read(data));
    select(data);
    SecurityGrantSnapshot snapshot = {}; ASSERT_EQ(security_admission_inspect(admission, &snapshot), SECURITY_STORE_OK);
    uint64_t next;
    ASSERT_EQ(security_grant_store_save(store, &snapshot.binding, snapshot.generation, nullptr, 0, &next), SECURITY_STORE_OK);
    security_grant_snapshot_destroy(&snapshot);
    SecurityIOResult result;
    EXPECT_FALSE(read(data, false, &result)); EXPECT_EQ(result.decision, SECURITY_REVOKED);
    EXPECT_FALSE(read(entry, true)); mem_free(data);
}

TEST_F(SecurityAdmissionTest, CrossScriptAndCrossRootRecordsDoNotInheritAuthority) {
    char* data = seed("data.txt", "private");
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); select(data);
    char* other = seed("other.ls", "pn main() { null }\n", true);
    SecurityAdmissionConfig options = config(); options.entry = other;
    SecurityAdmissionReport report; SecurityAdmission* peer = security_admission_open(&options, &report);
    ASSERT_NE(peer, nullptr); EXPECT_EQ(report.restored, 0U);
    SecurityGrantBinding first, second;
    ASSERT_TRUE(security_admission_binding(admission, &first)); ASSERT_TRUE(security_admission_binding(peer, &second));
    EXPECT_NE(memcmp(first.principal, second.principal, 32), 0);
    SecurityIOClient client = security_admission_client(peer); char* bytes = nullptr; size_t size = 0;
    EXPECT_FALSE(security_io_read_bytes(&client, data, false, 4096, MEM_CAT_SYSTEM, &bytes, &size, nullptr));
    security_admission_close(peer); mem_free(other); mem_free(data);
}

TEST_F(SecurityAdmissionTest, ARecordFromAnotherProfileCannotClaimTheSameEntry) {
    char* data = seed("outside.txt", "private");
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); select(data);
    SecurityGrantSnapshot original = {}; ASSERT_EQ(security_admission_inspect(admission, &original), SECURITY_STORE_OK);
    char* other_home = file_path_join(base, "other-home"); ASSERT_TRUE(create_dir(other_home));
    ASSERT_EQ(chmod(other_home, 0700), 0);
    SecurityStoreResult status; SecurityGrantStore* other = security_grant_store_test_open(other_home, &status);
    ASSERT_NE(other, nullptr); uint64_t generation;
    ASSERT_EQ(security_grant_store_save(other, &original.binding, 0,
        original.grants, original.count, &generation), SECURITY_STORE_OK);
    SecurityAdmissionConfig options = config(); options.store = other;
    SecurityAdmissionReport report; SecurityAdmission* peer = security_admission_open(&options, &report);
    ASSERT_NE(peer, nullptr); EXPECT_EQ(report.restored, 0U);
    SecurityGrantBinding binding; ASSERT_TRUE(security_admission_binding(peer, &binding));
    EXPECT_NE(memcmp(binding.principal, original.binding.principal, 32), 0);
    SecurityIOClient client = security_admission_client(peer); char* bytes = nullptr; size_t size;
    EXPECT_FALSE(security_io_read_bytes(&client, data, false, 4096, MEM_CAT_SYSTEM, &bytes, &size, nullptr));
    security_admission_close(peer); security_grant_store_close(other); security_grant_snapshot_destroy(&original);
    mem_free(other_home); mem_free(data);
}

TEST_F(SecurityAdmissionTest, BroadHomeRootCannotReadOrCreateProtectedAccountNames) {
    SecurityAdmissionConfig options = config(); options.project = home;
    SecurityAdmissionReport report; admission = security_admission_open(&options, &report); ASSERT_NE(admission, nullptr);
    SecurityIOClient client = security_admission_client(admission);
    char* key = file_path_join(home, ".ssh/new-key");
    SecurityIOResult result = client.write(client.context, key, "key", 3, false, true);
    EXPECT_FALSE(result.committed); EXPECT_NE(result.decision, SECURITY_ALLOWED);
    char* ssh = file_path_join(home, ".ssh"); EXPECT_FALSE(file_exists(ssh));
    ASSERT_TRUE(create_dir(ssh)); ASSERT_EQ(write_binary_file(key, "secret", 6), 0);
    EXPECT_FALSE(read(key));
    char* setting = security_grant_store_directory(store);
    char* setting_file = file_path_join(setting, "forged.grant");
    result = client.write(client.context, setting_file, "forged", 6, false, true);
    EXPECT_FALSE(result.committed); EXPECT_FALSE(file_exists(setting_file));
    char* host_module = file_path_join(home, ".lambda/new-host-module.ls");
    result = client.write(client.context, host_module, "never", 5, false, true);
    EXPECT_FALSE(result.committed); EXPECT_FALSE(file_exists(host_module)); mem_free(host_module);
    SecurityDirectory directory = {};
    ASSERT_TRUE(security_io_enumerate(&client, home, 4096, &directory, nullptr));
    for (uint32_t i = 0; i < directory.count; i++) EXPECT_STRNE(directory.entries[i].name, ".ssh");
    security_io_directory_free(&directory);
    mem_free(setting_file); mem_free(setting); mem_free(ssh); mem_free(key);
}

TEST_F(SecurityAdmissionTest, StoredFingerprintsCannotOverrideProtectionOrRequestUnsupportedServices) {
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); ASSERT_NE(admission, nullptr);
    char* sensitive = seed(".ssh/private", "secret");
    SecurityFileSelection selection = {sensitive, SECURITY_READ, 0, false, false, true};
    SecurityStoreResult result;
    EXPECT_EQ(security_admission_select(admission, &selection, &result), 0U);
    selection.path = project; selection.is_directory = selection.recursive = true;
    const SecurityOperations unsupported[] = {SECURITY_LOAD_SOURCE, SECURITY_CONNECT, SECURITY_DELETE, SECURITY_RENAME, SECURITY_WATCH};
    for (SecurityOperations operations : unsupported) {
        selection.operations = operations;
        EXPECT_EQ(security_admission_select(admission, &selection, &result), 0U);
        EXPECT_EQ(result, SECURITY_STORE_INVALID);
    }
    mem_free(sensitive);
}

TEST_F(SecurityAdmissionTest, ShippedCodeIsAdmittedButItsEnumerationGrantIsNotPublished) {
    char* library = seed("shipped/helper.ls", "pub let value = 1\n");
    char* shipped = file_path_dirname(library);
    SecurityAdmissionConfig options = config(); const char* directories[] = {shipped};
    options.shipped_directories = directories; options.shipped_count = 1;
    SecurityAdmissionReport report; admission = security_admission_open(&options, &report); ASSERT_NE(admission, nullptr);
    EXPECT_TRUE(read(library, true));
    SecurityIOClient client = security_admission_client(admission); SecurityDirectory listing = {};
    EXPECT_FALSE(security_io_enumerate(&client, shipped, 4096, &listing, nullptr));
    SecurityIOResult result = client.write(client.context, library, "bad", 3, false, true);
    EXPECT_FALSE(result.committed); mem_free(shipped); mem_free(library);
}

TEST_F(SecurityAdmissionTest, ManagedCodeProtectionWinsOverRememberedParentReplacement) {
    char* library = seed("managed/helper.ls", "pub let value = 1\n", true);
    char* shipped = file_path_dirname(library); const char* directories[] = {shipped};
    SecurityAdmissionConfig options = config(); options.shipped_directories = directories; options.shipped_count = 1;
    SecurityAdmissionReport report; admission = security_admission_open(&options, &report); ASSERT_NE(admission, nullptr);
    select(project, true, true, SECURITY_REPLACE | SECURITY_APPEND);
    SecurityIOClient client = security_admission_client(admission);
    SecurityIOResult result = client.write(client.context, library, "bad", 3, false, true);
    EXPECT_FALSE(result.committed); EXPECT_FALSE(result.decision == SECURITY_ALLOWED && !result.system_error);
    EXPECT_TRUE(read(library, true)); mem_free(shipped); mem_free(library);
}

TEST_F(SecurityAdmissionTest, ReplacedRootAtTheSamePathCannotReuseRememberedGrants) {
    char* data = seed("outside.txt", "private");
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK); select(data);
    char* previous = file_path_join(home, "old-project"); ASSERT_EQ(rename(project, previous), 0);
    ASSERT_TRUE(create_dir(project)); ASSERT_EQ(write_binary_file(entry, "pn main() { null }\n", 19), 0);
    EXPECT_EQ(open().store_result, SECURITY_STORE_IDENTITY_CHANGED); EXPECT_EQ(admission, nullptr);
    ASSERT_EQ(open(true).store_result, SECURITY_STORE_OK); EXPECT_FALSE(read(data));
    mem_free(previous); mem_free(data);
}

TEST_F(SecurityAdmissionTest, OversizedSourcesAndUnsafeEntriesNeverProduceAClient) {
    char* bytes = (char*)mem_calloc(1, 1024 * 1024 + 1, MEM_CAT_SYSTEM);
    ASSERT_NE(bytes, nullptr); ASSERT_EQ(write_binary_file(entry, bytes, 1024 * 1024 + 1), 0); mem_free(bytes);
    EXPECT_NE(open().store_result, SECURITY_STORE_OK); EXPECT_EQ(admission, nullptr);
    ASSERT_EQ(unlink(entry), 0);
    char* outside = seed("outside.ls", "null\n"); ASSERT_EQ(symlink(outside, entry), 0);
    EXPECT_NE(open().store_result, SECURITY_STORE_OK); EXPECT_EQ(admission, nullptr); mem_free(outside);
}

// D11.1.2: exercise the real admission lease at the IPC publication boundary.
class SecurityDeliveryTest : public SecurityAdmissionTest {
protected:
    enum Change { NONE, REVOKE, EXTERNAL, EXPIRE, BAD_CAPTURE, EXPIRE_DURING } change = NONE;
    char* secret = nullptr;
    uint64_t resource = 0;
    SecurityGrantStore* other = nullptr;
    SafetyGrantRevocation revocation = {};
    SecurityChannelDeliveryGuard native = {}, guarded = {};
    SecurityChannelConfig wire = {};
    SecurityIOClient remote = {};
    SecurityChannel* peer = nullptr;
    IpcStream* raw = nullptr;
    SafetyAsync server;
    int server_fd = -1;
    SecurityChannelReport report = {};
    bool success = false;
    SecurityStoreResult changed = SECURITY_STORE_INVALID;
    atomic_int32 acquired = {}, released = {}, checks = {}, captures = {};

    static SecurityIOResult capture(void* context, SecurityAuthorityStamp* stamp) {
        SecurityDeliveryTest* test = (SecurityDeliveryTest*)context;
        atomic_inc32(&test->captures);
        SecurityIOResult result = test->native.capture(test->native.context, stamp);
        if (test->change == BAD_CAPTURE) stamp->session++;
        return result;
    }
    static void* acquire(void* context, const SecurityAuthorityStamp* stamp, SecurityIOResult* failure) {
        SecurityDeliveryTest* test = (SecurityDeliveryTest*)context;
        if (test->change == REVOKE) test->changed = security_admission_revoke(test->admission, test->resource);
        if (test->change == EXTERNAL) { SafetyGrantRevocation::run(&test->revocation); test->changed = test->revocation.result; }
        if (test->change == EXPIRE) test->now = 111;
        void* lease = test->native.acquire(test->native.context, stamp, failure);
        if (lease) atomic_store32(&test->acquired, 1);
        return lease;
    }
    static bool check(void* context, void* lease) {
        SecurityDeliveryTest* test = (SecurityDeliveryTest*)context;
        int count = atomic_inc32(&test->checks);
        if (test->change == EXPIRE_DURING && count >= 6) test->now = 111;
        return test->native.check(test->native.context, lease);
    }
    static void release(void* context, void* lease) {
        SecurityDeliveryTest* test = (SecurityDeliveryTest*)context;
        test->native.release(test->native.context, lease); atomic_store32(&test->released, 1);
    }
    static void serve(void* context) {
        SecurityDeliveryTest* test = (SecurityDeliveryTest*)context;
        SecurityIOClient client = security_admission_client(test->admission);
        test->success = security_channel_serve_guarded(test->server_fd, &test->wire, &client, &test->guarded, &test->report);
    }
    void SetUp() override {
        SecurityAdmissionTest::SetUp(); if (HasFatalFailure()) return;
        secret = file_path_join(home, "secret.txt");
        char* data = (char*)mem_alloc(512 * 1024, MEM_CAT_SYSTEM); ASSERT_NE(data, nullptr);
        memset(data, 's', 512 * 1024);
        int result = write_binary_file(secret, data, 512 * 1024); mem_free(data); ASSERT_EQ(result, 0);
        ASSERT_EQ(open().store_result, SECURITY_STORE_OK);
        resource = select(secret, false, false, SECURITY_READ | SECURITY_METADATA, 110);
    }
    void stop() {
        security_channel_destroy(peer); peer = nullptr;
        ipc_stream_close(raw); raw = nullptr;
        EXPECT_EQ(server.join(), 0); server_fd = -1;
    }
    void TearDown() override {
        stop(); security_grant_store_close(other); mem_free(secret);
        SecurityAdmissionTest::TearDown();
    }
    void start(bool raw_peer = false) {
        int pair[2]; ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
        int small = 4096; ASSERT_EQ(setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small)), 0);
        native = security_admission_delivery_guard(admission);
        guarded = {this, capture, acquire, check, release, native.timeout_ms};
        wire = {security_admission_session(admission), 64 * 1024 * 1024, 1024 * 1024, 128, 60000};
        server_fd = pair[0];
        if (raw_peer) { raw = ipc_stream_adopt(pair[1]); ASSERT_NE(raw, nullptr); }
        else { peer = security_channel_create(pair[1], &wire); ASSERT_NE(peer, nullptr); remote = security_channel_client(peer); }
        ASSERT_EQ(server.start(serve, this), 0);
    }
    void prepare_revocation() {
        SecurityStoreResult status; other = security_grant_store_test_open(home, &status); ASSERT_NE(other, nullptr);
        SecurityGrantSnapshot snapshot = {}; ASSERT_EQ(security_admission_inspect(admission, &snapshot), SECURITY_STORE_OK);
        revocation = {other, snapshot.binding, snapshot.generation, 0, SECURITY_STORE_INVALID};
        security_grant_snapshot_destroy(&snapshot);
    }
    void request_read() {
        uint8_t request[8192]; uint32_t length = (uint32_t)strlen(secret), size = 4 + length + 12;
        safety_channel_header(request, wire.session, 1, size);
        write_le32(request + 32, length); memcpy(request + 36, secret, length);
        write_le64(request + 36 + length, 512 * 1024); write_le32(request + 44 + length, 0);
        ASSERT_TRUE(ipc_stream_transfer(raw, request, 32 + size, true, time_now_ms() + 2000, nullptr));
    }
    SecurityIOResult invoke(uint16_t operation) {
        SecurityIOResult result = {};
        if (operation == 1) {
            char* bytes = nullptr; size_t size = 0;
            bool ok = security_io_read_bytes(&remote, secret, false, 1024 * 1024, MEM_CAT_SYSTEM, &bytes, &size, &result);
            if (change != NONE) { EXPECT_FALSE(ok); EXPECT_EQ(bytes, nullptr); EXPECT_EQ(size, 0U); }
            else { EXPECT_TRUE(ok); EXPECT_EQ(size, 512U * 1024); if (bytes) EXPECT_EQ(bytes[0], 's'); }
            mem_free(bytes);
        } else if (operation == 3) {
            char* path = nullptr; bool ok = security_io_source_path(&remote, entry, &path, &result);
            if (change != NONE) { EXPECT_FALSE(ok); EXPECT_EQ(path, nullptr); } else { EXPECT_TRUE(ok); EXPECT_STREQ(path, entry); }
            mem_free(path);
        } else if (operation == 4) {
            int32_t kind = 99; bool ok = security_io_input_kind(&remote, secret, &kind, &result);
            if (change != NONE) { EXPECT_FALSE(ok); EXPECT_EQ(kind, -1); } else EXPECT_TRUE(ok);
        } else if (operation == 5) {
            SecurityDirectory directory = {}; bool ok = security_io_enumerate(&remote, project, 128, &directory, &result);
            if (change != NONE) { EXPECT_FALSE(ok); EXPECT_EQ(directory.count, 0U); } else EXPECT_TRUE(ok);
            security_io_directory_free(&directory);
        } else if (operation == 6) {
            FileStat stat = {}; bool ok = security_io_stat(&remote, secret, &stat, &result);
            if (change != NONE) { EXPECT_FALSE(ok); EXPECT_FALSE(stat.exists); } else { EXPECT_TRUE(ok); EXPECT_EQ(stat.size, 512U * 1024); }
        } else {
            SecurityAuthorityStamp stamp = {}; bool ok = security_io_authority_stamp(&remote, &stamp, &result);
            if (change != NONE) { EXPECT_FALSE(ok); EXPECT_EQ(stamp.session, 0U); } else EXPECT_TRUE(ok);
        }
        return result;
    }
};

TEST_F(SecurityDeliveryTest, GuardedOperationsKeepTheirExistingWireContract) {
    start(); ASSERT_FALSE(HasFatalFailure());
    for (uint16_t operation : {1, 3, 4, 5, 6, 7}) EXPECT_EQ(invoke(operation).decision, SECURITY_ALLOWED);
    stop(); EXPECT_TRUE(success); EXPECT_EQ(report.requests, 6U);
}

TEST_F(SecurityDeliveryTest, LocalRevocationSuppressesEveryBufferedResponseShape) {
    for (uint16_t operation : {1, 3, 4, 5, 6, 7}) {
        SCOPED_TRACE(operation); change = REVOKE; start(); ASSERT_FALSE(HasFatalFailure());
        EXPECT_EQ(invoke(operation).decision, SECURITY_REVOKED); stop(); EXPECT_TRUE(success);
        EXPECT_EQ(changed, SECURITY_STORE_OK);
        ASSERT_EQ(open().store_result, SECURITY_STORE_OK);
        resource = select(secret, false, false, SECURITY_READ | SECURITY_METADATA, 110);
    }
}

TEST_F(SecurityDeliveryTest, SavedRevocationAndExpirySuppressAlreadyReadBytes) {
    prepare_revocation(); ASSERT_FALSE(HasFatalFailure()); change = EXTERNAL; start(); ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(invoke(1).decision, SECURITY_REVOKED); stop(); EXPECT_EQ(changed, SECURITY_STORE_OK);
    ASSERT_EQ(open().store_result, SECURITY_STORE_OK);
    resource = select(secret, false, false, SECURITY_READ | SECURITY_METADATA, 110);
    change = EXPIRE; start(); ASSERT_FALSE(HasFatalFailure());
    EXPECT_EQ(invoke(1).decision, SECURITY_REVOKED); stop(); EXPECT_TRUE(success);
}

TEST_F(SecurityDeliveryTest, RevocationAfterWritePreservesTheCommittedOutcome) {
    change = REVOKE; start(); ASSERT_FALSE(HasFatalFailure()); char* output = file_path_join(project, "result.txt");
    SecurityIOResult result = remote.write(remote.context, output, "done", 4, false, true);
    EXPECT_EQ(result.decision, SECURITY_REVOKED); EXPECT_TRUE(result.committed); EXPECT_FALSE(result.outcome_unknown);
    stop(); char* bytes = nullptr; size_t size = 0;
    ASSERT_TRUE(file_read_all(output, MEM_CAT_SYSTEM, &bytes, &size));
    EXPECT_EQ(size, 4U); EXPECT_EQ(memcmp(bytes, "done", size), 0); mem_free(bytes); mem_free(output);
}

TEST_F(SecurityDeliveryTest, WrongCapturedSessionCannotDispatchAMutation) {
    change = BAD_CAPTURE; start(); ASSERT_FALSE(HasFatalFailure()); char* output = file_path_join(project, "absent.txt");
    SecurityIOResult result = remote.write(remote.context, output, "never", 5, false, true);
    EXPECT_EQ(result.decision, SECURITY_REVOKED); EXPECT_FALSE(result.committed); EXPECT_FALSE(file_exists(output));
    mem_free(output); stop(); EXPECT_EQ(atomic_load32(&acquired), 0);
}

TEST_F(SecurityDeliveryTest, InvalidGuardAndMalformedRequestCannotReachAdmission) {
    int pair[2]; ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    SecurityChannelDeliveryGuard invalid = security_admission_delivery_guard(admission); invalid.timeout_ms = 1001;
    SecurityChannelConfig config = {security_admission_session(admission), 65536, 4096, 16, 1000};
    SecurityIOClient client = security_admission_client(admission);
    EXPECT_FALSE(security_channel_serve_guarded(pair[0], &config, &client, &invalid, &report));
    EXPECT_EQ(errno, EINVAL); EXPECT_GE(fcntl(pair[0], F_GETFD), 0); close(pair[0]); close(pair[1]);
    start(true); ASSERT_FALSE(HasFatalFailure()); uint8_t request[33];
    safety_channel_header(request, wire.session, 7, 1); request[32] = 1;
    ASSERT_TRUE(ipc_stream_transfer(raw, request, sizeof(request), true, time_now_ms() + 2000, nullptr));
    EXPECT_TRUE(SafetyAsync::wait(&server.done)); stop(); EXPECT_EQ(report.system_error, EPROTO);
    EXPECT_EQ(atomic_load32(&captures), 0);
}

TEST_F(SecurityDeliveryTest, BlockedPeerCannotHoldRevocationForTheConsentTimeout) {
    prepare_revocation(); ASSERT_FALSE(HasFatalFailure()); start(true); ASSERT_FALSE(HasFatalFailure());
    uint64_t began = time_now_ms(); request_read(); ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(SafetyAsync::wait(&acquired));
    SafetyAsync writer; ASSERT_EQ(writer.start(SafetyGrantRevocation::run, &revocation), 0);
    EXPECT_TRUE(SafetyAsync::wait(&writer.started)); EXPECT_EQ(atomic_load32(&writer.done), 0);
    EXPECT_TRUE(SafetyAsync::wait(&server.done));
    EXPECT_TRUE(SafetyAsync::wait(&writer.done)); EXPECT_EQ(writer.join(), 0);
    EXPECT_EQ(report.system_error, ETIMEDOUT); EXPECT_EQ(atomic_load32(&released), 1);
    EXPECT_LT(time_now_ms() - began, 2000U); EXPECT_EQ(revocation.result, SECURITY_STORE_OK);
}

TEST_F(SecurityDeliveryTest, ExpiryInterruptsPublicationWhileThePeerIsBlocked) {
    change = EXPIRE_DURING; start(true); ASSERT_FALSE(HasFatalFailure()); request_read(); ASSERT_FALSE(HasFatalFailure());
    EXPECT_TRUE(SafetyAsync::wait(&server.done)); EXPECT_EQ(report.system_error, EACCES);
    EXPECT_EQ(atomic_load32(&released), 1); EXPECT_GE(atomic_load32(&checks), 6);
}

static uint64_t fixture_clock(void*) { return (uint64_t)time(nullptr); }
static SecurityAdmission* fixture_admission;
static bool fixture_approve;
static unsigned fixture_challenges;
static SecurityIOResult fixture_write(void*, const char* path, const void* data, size_t size, bool append, bool atomic) {
    SecurityIOClient raw = security_admission_client(fixture_admission);
    SecurityIOResult result = raw.write(raw.context, path, data, size, append, atomic);
    if (result.decision == SECURITY_APPROVAL_REQUIRED) {
        fixture_challenges++;
        SecurityOverwriteChallenge challenge;
        if (!security_admission_describe(fixture_admission, result.approval, &challenge))
            return {SECURITY_REVOKED, ESTALE, false, {}};
        if (fixture_approve) return security_admission_approve(fixture_admission, result.approval);
        security_admission_cancel(fixture_admission, result.approval);
        return {SECURITY_DENIED, EACCES, false, {}};
    }
    return result;
}

static int serve_fixture(int argc, char** argv) {
    // Test-only trusted launcher controls these paths and consent. No worker
    // request can select the store, identity, grant or approval result.
    if (argc != 9 && argc != 10) return 2;
    char* end = nullptr; long fd = strtol(argv[2], &end, 10);
    if (!end || *end || fd < 3 || fd > 100000 ||
            (strcmp(argv[8], "approve") && strcmp(argv[8], "deny") &&
                strcmp(argv[8], "remember") && strcmp(argv[8], "revoke"))) return 2;
    SecurityStoreResult status;
    SecurityGrantStore* store = security_grant_store_test_open(argv[3], &status);
    if (!store) return 2;
    SecurityProtectionSpec protection = {argv[7], SECURITY_ALL_OPERATIONS};
    SecurityAdmissionConfig options = {store, argv[5], argv[4], argv[6], nullptr, 0, &protection, 1,
        false, fixture_clock, nullptr};
    SecurityAdmissionReport report;
    fixture_admission = security_admission_open(&options, &report);
    if (!fixture_admission) {
        char failure[128];
        int size = snprintf(failure, sizeof(failure), "{\"admitted\":false,\"store_result\":%u,\"identity_changed\":%s}\n",
            report.store_result, report.store_result == SECURITY_STORE_IDENTITY_CHANGED ? "true" : "false");
        if (size > 0 && (size_t)size < sizeof(failure)) (void)write(STDOUT_FILENO, failure, size);
        security_grant_store_close(store); return 2;
    }
    if (argc == 10) {
        SecurityFileSelection selection = {argv[9], SECURITY_READ, 0, false, false, true};
        if (!security_admission_select(fixture_admission, &selection, &status)) {
            security_admission_close(fixture_admission); security_grant_store_close(store); return 2;
        }
        security_admission_close(fixture_admission);
        fixture_admission = security_admission_open(&options, &report);
        if (!fixture_admission) { security_grant_store_close(store); return 2; }
    }
    if (!strcmp(argv[8], "revoke")) {
        SecurityGrantSnapshot snapshot = {};
        status = security_admission_inspect(fixture_admission, &snapshot);
        uint64_t next;
        if (status == SECURITY_STORE_OK) status = security_grant_store_save(store, &snapshot.binding,
            snapshot.generation, nullptr, 0, &next);
        security_grant_snapshot_destroy(&snapshot);
    }
    if (!strcmp(argv[8], "remember") || !strcmp(argv[8], "revoke")) {
        security_admission_close(fixture_admission); fixture_admission = nullptr; security_grant_store_close(store);
        return status == SECURITY_STORE_OK ? 0 : 2;
    }
    char line[256];
    int length = snprintf(line, sizeof(line), "{\"admitted\":true,\"session\":%llu,\"generation\":%llu,\"restored\":%u}\n",
        (unsigned long long)security_admission_session(fixture_admission),
        (unsigned long long)report.generation, report.restored);
    if (length <= 0 || (size_t)length >= sizeof(line) || write(STDOUT_FILENO, line, length) != length) return 2;
    fixture_approve = !strcmp(argv[8], "approve"); fixture_challenges = 0;
    SecurityIOClient client = security_admission_client(fixture_admission); client.write = fixture_write;
    SecurityChannelConfig limits = {security_admission_session(fixture_admission),
        64 * 1024 * 1024, 1024 * 1024, 8192, 10000};
    SecurityChannelReport channel;
    SecurityChannelDeliveryGuard guard = security_admission_delivery_guard(fixture_admission);
    bool success = security_channel_serve_guarded((int)fd, &limits, &client, &guard, &channel);
    length = snprintf(line, sizeof(line), "{\"success\":%s,\"requests\":%llu,\"error\":%d,\"challenges\":%u}\n",
        success ? "true" : "false", (unsigned long long)channel.requests, channel.system_error, fixture_challenges);
    security_admission_close(fixture_admission); fixture_admission = nullptr; security_grant_store_close(store);
    if (length <= 0 || (size_t)length >= sizeof(line) || write(STDOUT_FILENO, line, length) != length) return 2;
    return success ? 0 : 3;
}
#endif

// process-level console fixtures use a protected test profile, never OS home
#ifndef _WIN32
static int seed_private_fixture(int argc, char** argv) {
    if (argc != 3 && (argc != 4 || (strcmp(argv[3], "--pending-reset") && strcmp(argv[3], "--hold-storage")))) return 2;
    bool hold = argc == 4 && !strcmp(argv[3], "--hold-storage");
    SecurityStoreResult result; SecurityPrivateStorageConfig options = {};
    options.store = security_grant_store_test_open(argv[2], &result);
    if (!options.store) return 2;
    options.binding.principal_kind = SECURITY_PRINCIPAL_SCRIPT;
    memset(options.binding.principal, 1, 32); memset(options.binding.code, 2, 32); memset(options.binding.root, 3, 32);
    options.broker = {{1, 2, SECURITY_ALL_OPERATIONS, 64, 64}, 1024, 4096, 1024, 8, 64, 60, fixture_clock, nullptr};
    security_private_storage_default_limits(options.limits);
    uint64_t generation;
    bool valid = security_grant_store_save(options.store, &options.binding, 0, nullptr, 0, &generation) == SECURITY_STORE_OK;
    SecurityPrivateStorage* storage = valid ? security_private_storage_open(&options) : nullptr;
    if (storage) {
        SecurityIOClient io = security_private_storage_client(storage);
        valid = io.write(io.context, SECURITY_APP_DATA_PATH "/one/a", "a", 1, false, true).committed &&
            io.write(io.context, SECURITY_APP_DATA_PATH "/two/b", "b", 1, false, true).committed &&
            io.write(io.context, SECURITY_APP_CACHE_PATH "/kept", "cache", 5, false, true).committed;
        if (!hold) { security_private_storage_close(storage); storage = nullptr; }
    } else valid = false;
    uint8_t id[16] = {}; bool complete = true;
    if (valid && argc == 4 && !hold) valid = security_private_storage_reset_begin(&options, id) &&
        security_private_storage_reset_step(&options, id, 1, &complete) && !complete;
    char principal[65], reset[33], output[192];
    hex_encode(options.binding.principal, 32, principal); hex_encode(id, 16, reset);
    int count = snprintf(output, sizeof(output), "{\"principal\":\"%s\",\"reset_id\":\"%s\"}\n", principal, reset);
    if (valid) valid = count > 0 && (size_t)count < sizeof(output) && SafetyChild::publish(STDOUT_FILENO, output, count);
    if (valid && hold) for (;;) pause();
    security_private_storage_close(storage);
    security_grant_store_close(options.store); return valid ? 0 : 2;
}
#endif

int main(int argc, char** argv) {
#ifndef _WIN32
    if (argc > 1 && !strcmp(argv[1], "--serve-admission-fixture")) return serve_fixture(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "--seed-private-fixture")) return seed_private_fixture(argc, argv);
#endif
    ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
