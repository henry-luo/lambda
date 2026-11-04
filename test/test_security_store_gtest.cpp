#include <gtest/gtest.h>
#include "safety/child_fixture.h"
#include "../lambda/host/safety/grant_store.h"
#include "../lib/file.h"
#include "../lib/file_utils.h"
#include "../lib/digest.h"
#include "../lib/hex.h"
#include "../lib/mem.h"
#include "../lib/log.h"
#include "../lib/atomic.h"
#include "safety/async_fixture.h"
#include "safety/grant_fixture.h"
#include <errno.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <pwd.h>
#include <pthread.h>
#include <time.h>

class SecurityStoreTest : public ::testing::Test {
protected:
    char* temporary = nullptr;
    char* home = nullptr;
    char* setting = nullptr;
    SecurityGrantStore* stores[16] = {};
    size_t store_count = 0;
    SecurityGrantBinding binding = {};

    void SetUp() override {
        log_init(nullptr);
        temporary = dir_temp_create("security_store");
        ASSERT_NE(temporary, nullptr);
        home = file_realpath(temporary);
        ASSERT_NE(home, nullptr);
        setting = file_path_join(home, ".lambda/setting");
        ASSERT_NE(setting, nullptr);
        binding.principal_kind = SECURITY_PRINCIPAL_SCRIPT;
        memset(binding.principal, 0x31, 32);
        memset(binding.code, 0x42, 32);
        memset(binding.root, 0x53, 32);
    }

    void TearDown() override {
        for (size_t i = 0; i < store_count; i++) security_grant_store_close(stores[i]);
        if (temporary) dir_delete(temporary);
        mem_free(setting); mem_free(home); mem_free(temporary);
    }

    SecurityGrantStore* open_store() {
        SecurityStoreResult result;
        SecurityGrantStore* store = security_grant_store_test_open(home, &result);
        EXPECT_EQ(result, SECURITY_STORE_OK);
        EXPECT_NE(store, nullptr);
        if (store && store_count < 16) stores[store_count++] = store;
        return store;
    }

    void close_store(SecurityGrantStore* store) {
        for (size_t i = 0; i < store_count; i++) if (stores[i] == store) stores[i] = nullptr;
        security_grant_store_close(store);
    }

    SecurityPersistentGrant grant(SecurityOperations operations = SECURITY_READ) {
        SecurityPersistentGrant value = {};
        memset(value.resource, 0x64, 32);
        value.operations = operations;
        value.expires_at = 123456789;
        value.kind = SECURITY_SAVED_FILE;
        value.flags = SECURITY_SAVED_PERSISTENT;
        value.locator = "file:///fixture/report";
        return value;
    }

    char* record_path(const SecurityGrantBinding* owner = nullptr) {
        char hex[65]; char name[80];
        hex_encode((owner ? owner : &binding)->principal, 32, hex);
        snprintf(name, sizeof(name), "%s.grant", hex);
        return file_path_join(setting, name);
    }

    void expect_empty(SecurityGrantStore* store, uint64_t generation) {
        SecurityGrantSnapshot snapshot = {};
        ASSERT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_OK);
        EXPECT_EQ(snapshot.generation, generation);
        EXPECT_EQ(snapshot.count, 0U);
        EXPECT_EQ(snapshot.grants, nullptr);
        security_grant_snapshot_destroy(&snapshot);
    }
};

TEST_F(SecurityStoreTest, PersistentGrantsRoundTripThroughReopen) {
    SecurityGrantStore* store = open_store();
    ASSERT_NE(store, nullptr);
    SecurityGrantSnapshot snapshot = {};
    EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_MISSING);
    EXPECT_EQ(snapshot.count, 0U);
    SecurityPersistentGrant records[] = {grant(), grant(SECURITY_READ | SECURITY_CREATE)};
    records[1].resource[0]++;
    records[1].kind = SECURITY_SAVED_DIRECTORY;
    records[1].flags |= SECURITY_SAVED_RECURSIVE;
    records[1].locator = "file:///fixture/project";
    uint64_t generation = 0;
    ASSERT_EQ(security_grant_store_save(store, &binding, 0, records, 2, &generation), SECURITY_STORE_OK);
    EXPECT_EQ(generation, 1U);
    close_store(store);
    store = open_store();
    ASSERT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_OK);
    ASSERT_EQ(snapshot.count, 2U);
    EXPECT_EQ(snapshot.generation, 1U);
    EXPECT_STREQ(snapshot.grants[0].locator, records[0].locator);
    EXPECT_EQ(snapshot.grants[0].expires_at, records[0].expires_at);
    EXPECT_EQ(snapshot.grants[1].flags, records[1].flags);
    EXPECT_EQ(snapshot.grants[1].operations, records[1].operations);
    security_grant_snapshot_destroy(&snapshot);
    EXPECT_EQ(snapshot.count, 0U);
}

TEST_F(SecurityStoreTest, PrivateStorageUsesTheAcquiredProfile) {
    SecurityGrantStore* store = open_store(); ASSERT_NE(store, nullptr);
    FileAnchor* workspace = security_grant_store_private_root(store); ASSERT_NE(workspace, nullptr);
    char* path = file_anchor_path(workspace); ASSERT_NE(path, nullptr);
    char* expected = file_path_join(home, ".lambda/app-data"); EXPECT_STREQ(path, expected);
    EXPECT_EQ(file_anchor_identity(workspace).stat.mode & 0077, 0);
    file_anchor_close(workspace);
    ASSERT_EQ(chmod(path, 0755), 0);
    EXPECT_EQ(security_grant_store_private_root(store), nullptr);
    ASSERT_EQ(chmod(path, 0700), 0); ASSERT_EQ(rmdir(path), 0);
    ASSERT_EQ(symlink(home, path), 0);
    EXPECT_EQ(security_grant_store_private_root(store), nullptr);
    mem_free(path); mem_free(expected);
}

TEST_F(SecurityStoreTest, StaleWriterCannotRestoreRevokedGrants) {
    SecurityGrantStore* first = open_store();
    SecurityGrantStore* second = open_store();
    ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
    SecurityPersistentGrant original = grant();
    uint64_t generation = 0;
    ASSERT_EQ(security_grant_store_save(first, &binding, 0, &original, 1, &generation), SECURITY_STORE_OK);
    ASSERT_EQ(security_grant_store_save(second, &binding, 1, nullptr, 0, &generation), SECURITY_STORE_OK);
    EXPECT_EQ(generation, 2U);
    EXPECT_EQ(security_grant_store_save(first, &binding, 1, &original, 1, &generation), SECURITY_STORE_CONFLICT);
    EXPECT_EQ(generation, 0U);
    expect_empty(first, 2);
    close_store(first); close_store(second);
    expect_empty(open_store(), 2);
}

TEST_F(SecurityStoreTest, ChangedCodeOrRootRequiresReadmissionAndClearsOldAuthority) {
    SecurityGrantStore* store = open_store();
    SecurityPersistentGrant original = grant();
    uint64_t generation = 0;
    ASSERT_EQ(security_grant_store_save(store, &binding, 0, &original, 1, &generation), SECURITY_STORE_OK);
    SecurityGrantBinding changed = binding;
    changed.code[0]++;
    SecurityGrantSnapshot snapshot = {};
    EXPECT_EQ(security_grant_store_load(store, &changed, &snapshot), SECURITY_STORE_IDENTITY_CHANGED);
    EXPECT_EQ(snapshot.count, 0U);
    EXPECT_EQ(security_grant_store_save(store, &changed, 1, &original, 1, &generation), SECURITY_STORE_IDENTITY_CHANGED);
    changed = binding; changed.root[0]++;
    EXPECT_EQ(security_grant_store_load(store, &changed, &snapshot), SECURITY_STORE_IDENTITY_CHANGED);
    ASSERT_EQ(security_grant_store_rebind(store, &binding, &changed, 1, &generation), SECURITY_STORE_OK);
    EXPECT_EQ(generation, 2U);
    ASSERT_EQ(security_grant_store_load(store, &changed, &snapshot), SECURITY_STORE_OK);
    EXPECT_EQ(snapshot.count, 0U);
    security_grant_snapshot_destroy(&snapshot);
    EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_IDENTITY_CHANGED);
    EXPECT_EQ(security_grant_store_save(store, &binding, 2, &original, 1, &generation), SECURITY_STORE_IDENTITY_CHANGED);
}

TEST_F(SecurityStoreTest, AppsAndScriptsCannotClaimAnotherPrincipalRecord) {
    SecurityGrantStore* store = open_store();
    SecurityPersistentGrant original = grant();
    SecurityGrantBinding other = binding;
    other.principal_kind = SECURITY_PRINCIPAL_APP;
    other.principal[0]++;
    uint64_t generation = 0;
    ASSERT_EQ(security_grant_store_save(store, &binding, 0, &original, 1, &generation), SECURITY_STORE_OK);
    SecurityGrantSnapshot snapshot = {};
    EXPECT_EQ(security_grant_store_load(store, &other, &snapshot), SECURITY_STORE_MISSING);
    char* source = record_path(); char* destination = record_path(&other);
    char* bytes = nullptr; size_t size = 0;
    ASSERT_TRUE(file_read_all(source, MEM_CAT_TEMP, &bytes, &size));
    ASSERT_EQ(write_binary_file_atomic_new(destination, bytes, size), 0);
    EXPECT_EQ(security_grant_store_load(store, &other, &snapshot), SECURITY_STORE_CORRUPT);
    EXPECT_EQ(snapshot.count, 0U);
    mem_free(bytes); mem_free(destination); mem_free(source);
}

TEST_F(SecurityStoreTest, SessionFlagsAndInvalidScopesAreNeverPersisted) {
    SecurityGrantStore* store = open_store();
    SecurityPersistentGrant valid = grant();
    SecurityPersistentGrant invalids[9];
    for (SecurityPersistentGrant& item : invalids) item = valid;
    invalids[0].flags = 0;
    invalids[1].flags |= SECURITY_SAVED_RECURSIVE;
    invalids[2].flags |= 8;
    invalids[3].kind = 99;
    invalids[4].operations = SECURITY_CONNECT;
    invalids[5].operations = 1ULL << 63;
    invalids[6].operations = 0;
    invalids[7].locator = "";
    memset(invalids[8].resource, 0, 32);
    uint64_t generation = 99;
    for (const SecurityPersistentGrant& item : invalids) {
        EXPECT_EQ(security_grant_store_save(store, &binding, 0, &item, 1, &generation), SECURITY_STORE_INVALID);
        EXPECT_EQ(generation, 0U);
    }
    SecurityPersistentGrant duplicate[] = {valid, valid};
    EXPECT_EQ(security_grant_store_save(store, &binding, 0, duplicate, 2, &generation), SECURITY_STORE_INVALID);
    EXPECT_EQ(security_grant_store_save(store, &binding, 0, &valid, SECURITY_STORE_MAX_GRANTS + 1, &generation), SECURITY_STORE_INVALID);
    EXPECT_EQ(security_grant_store_save(store, &binding, UINT64_MAX, &valid, 1, &generation), SECURITY_STORE_INVALID);
    SecurityGrantSnapshot snapshot = {};
    EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_MISSING);
}

TEST_F(SecurityStoreTest, MalformedVersionedRecordsCannotFallBackToOldAuthority) {
    SecurityGrantStore* store = open_store();
    SecurityPersistentGrant original = grant();
    uint64_t generation = 0;
    ASSERT_EQ(security_grant_store_save(store, &binding, 0, &original, 1, &generation), SECURITY_STORE_OK);
    char* path = record_path();
    char* bytes = nullptr; size_t size = 0;
    ASSERT_TRUE(file_read_all(path, MEM_CAT_TEMP, &bytes, &size));
    ASSERT_GT(size, 192U + 32U);
    char* damaged = (char*)mem_alloc(size, MEM_CAT_TEMP);
    ASSERT_NE(damaged, nullptr);
    // V1 schema, generation, identity, count, reserved fields, operations, kind,
    // persistence flags, locator length and NUL bytes are all security-critical.
    const size_t offsets[] = {8, 16, 24, 120, 124, 128, 160, 176, 180, 184, 188, 192};
    for (size_t offset : offsets) {
        memcpy(damaged, bytes, size);
        if (offset == 16) memset(damaged + offset, 0, 8);
        else if (offset == 128) memset(damaged + offset, 0, 32);
        else damaged[offset] = offset == 192 || offset == 180 ? 0 : (char)0xff;
        ASSERT_TRUE(digest_sha256(damaged, size - 32, (uint8_t*)damaged + size - 32));
        ASSERT_EQ(write_binary_file(path, damaged, size), 0);
        SecurityGrantSnapshot snapshot = {};
        EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_CORRUPT) << offset;
        EXPECT_EQ(snapshot.count, 0U);
        EXPECT_EQ(snapshot.grants, nullptr);
        EXPECT_EQ(security_grant_store_save(store, &binding, 0, &original, 1, &generation), SECURITY_STORE_CORRUPT);
    }
    memcpy(damaged, bytes, size); damaged[size - 1] ^= 1;
    ASSERT_EQ(write_binary_file(path, damaged, size), 0);
    SecurityGrantSnapshot snapshot = {};
    EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_CORRUPT);
    ASSERT_EQ(write_binary_file(path, bytes, size - 1), 0);
    EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_CORRUPT);
    mem_free(damaged); mem_free(bytes); mem_free(path);
}

TEST_F(SecurityStoreTest, RecordLinksAndNonprivateModesAreRejected) {
    SecurityGrantStore* store = open_store();
    SecurityPersistentGrant original = grant(); uint64_t generation = 0;
    ASSERT_EQ(security_grant_store_save(store, &binding, 0, &original, 1, &generation), SECURITY_STORE_OK);
    char* path = record_path(); char* backup = file_path_join(setting, "old-record");
    ASSERT_EQ(rename(path, backup), 0);
    ASSERT_EQ(symlink("old-record", path), 0);
    SecurityGrantSnapshot snapshot = {};
    EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_UNSAFE_PATH);
    ASSERT_EQ(unlink(path), 0); ASSERT_EQ(rename(backup, path), 0);
    ASSERT_EQ(link(path, backup), 0);
    EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_UNSAFE_PATH);
    ASSERT_EQ(unlink(backup), 0);
    ASSERT_EQ(chmod(path, 0644), 0);
    EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_UNSAFE_PATH);
    EXPECT_EQ(snapshot.count, 0U);
    mem_free(backup); mem_free(path);
}

TEST_F(SecurityStoreTest, AliasedOrUnsafeSettingsParentsAreRejectedBeforeCreation) {
    char* lambda = file_path_join(home, ".lambda");
    char* other = file_path_join(home, "other");
    ASSERT_EQ(mkdir(other, 0700), 0);
    ASSERT_EQ(symlink("other", lambda), 0);
    SecurityStoreResult result;
    EXPECT_EQ(security_grant_store_test_open(home, &result), nullptr);
    EXPECT_EQ(result, SECURITY_STORE_UNSAFE_PATH);
    char* unintended = file_path_join(other, "setting");
    EXPECT_FALSE(file_exists(unintended));
    ASSERT_EQ(unlink(lambda), 0); ASSERT_EQ(mkdir(lambda, 0755), 0);
    EXPECT_EQ(security_grant_store_test_open(home, &result), nullptr);
    EXPECT_EQ(result, SECURITY_STORE_UNSAFE_PATH);
    EXPECT_FALSE(file_exists(setting));
    mem_free(unintended); mem_free(other); mem_free(lambda);
}

TEST_F(SecurityStoreTest, ReplacedDirectoryAndLockNameCannotRedirectTransactions) {
    SecurityGrantStore* store = open_store();
    uint64_t generation = 0;
    ASSERT_EQ(security_grant_store_save(store, &binding, 0, nullptr, 0, &generation), SECURITY_STORE_OK);
    char* moved = file_path_join(home, "moved-setting");
    ASSERT_EQ(rename(setting, moved), 0); ASSERT_EQ(mkdir(setting, 0700), 0);
    SecurityGrantSnapshot snapshot = {};
    EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_UNSAFE_PATH);
    EXPECT_EQ(security_grant_store_save(store, &binding, 1, nullptr, 0, &generation), SECURITY_STORE_UNSAFE_PATH);
    ASSERT_EQ(rmdir(setting), 0); ASSERT_EQ(rename(moved, setting), 0);
    char* lock = file_path_join(setting, ".grants.lock");
    char* old_lock = file_path_join(setting, "old-lock");
    ASSERT_EQ(rename(lock, old_lock), 0);
    ASSERT_EQ(write_binary_file(lock, "", 0), 0); ASSERT_EQ(chmod(lock, 0600), 0);
    EXPECT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_UNSAFE_PATH);
    EXPECT_EQ(snapshot.count, 0U);
    mem_free(old_lock); mem_free(lock); mem_free(moved);
}

struct StoreWriter {
    SecurityGrantStore* store;
    const SecurityGrantBinding* binding;
    SecurityPersistentGrant grant;
    int start;
    SecurityStoreResult result;
};

static void* save_store_together(void* context) {
    StoreWriter* writer = (StoreWriter*)context;
    char byte;
    if (read(writer->start, &byte, 1) != 1) { writer->result = SECURITY_STORE_IO_ERROR; return nullptr; }
    uint64_t generation = 0;
    writer->result = security_grant_store_save(writer->store, writer->binding, 1, &writer->grant, 1, &generation);
    return nullptr;
}

TEST_F(SecurityStoreTest, IndependentStoreInstancesSerializeConcurrentCompareAndSwap) {
    SecurityGrantStore* first = open_store(); SecurityGrantStore* second = open_store();
    uint64_t generation = 0;
    ASSERT_EQ(security_grant_store_save(first, &binding, 0, nullptr, 0, &generation), SECURITY_STORE_OK);
    int start[2]; ASSERT_EQ(pipe(start), 0);
    StoreWriter writers[] = {{first, &binding, grant(SECURITY_READ), start[0], SECURITY_STORE_INVALID},
        {second, &binding, grant(SECURITY_APPEND), start[0], SECURITY_STORE_INVALID}};
    pthread_t threads[2]; size_t created = 0;
    for (; created < 2; created++) if (pthread_create(&threads[created], nullptr, save_store_together, &writers[created]) != 0) break;
    EXPECT_EQ(write(start[1], "xx", created), (ssize_t)created);
    close(start[1]);
    for (size_t i = 0; i < created; i++) pthread_join(threads[i], nullptr);
    close(start[0]);
    ASSERT_EQ(created, 2U);
    EXPECT_TRUE((writers[0].result == SECURITY_STORE_OK && writers[1].result == SECURITY_STORE_CONFLICT) ||
        (writers[1].result == SECURITY_STORE_OK && writers[0].result == SECURITY_STORE_CONFLICT));
    SecurityGrantSnapshot snapshot = {};
    ASSERT_EQ(security_grant_store_load(first, &binding, &snapshot), SECURITY_STORE_OK);
    EXPECT_EQ(snapshot.generation, 2U); EXPECT_EQ(snapshot.count, 1U);
    security_grant_snapshot_destroy(&snapshot);
}

TEST_F(SecurityStoreTest, AcknowledgedRevocationSurvivesWriterProcessDeath) {
    SecurityGrantStore* store = open_store();
    SecurityPersistentGrant original = grant(); uint64_t generation = 0;
    ASSERT_EQ(security_grant_store_save(store, &binding, 0, &original, 1, &generation), SECURITY_STORE_OK);
    struct Writer { const char* home; SecurityGrantBinding binding; } writer_config = {home, binding};
    SafetyChild child;
    auto action = [](void* context, int ready) -> int {
        Writer* config = (Writer*)context;
        SecurityStoreResult result;
        SecurityGrantStore* writer = security_grant_store_test_open(config->home, &result);
        if (!writer) return 2;
        uint64_t current = 1;
        for (int i = 0; i < 10000; i++) {
            if (security_grant_store_save(writer, &config->binding, current, nullptr, 0, &current) != SECURITY_STORE_OK) return 3;
            if (i == 3 && !SafetyChild::publish(ready, &current, sizeof(current))) return 4;
        }
        return 0;
    };
    ASSERT_EQ(child.start(action, &writer_config), 0);
    uint64_t acknowledged = 0;
    ASSERT_TRUE(child.receive(&acknowledged, sizeof(acknowledged)));
    int status = 0; ASSERT_EQ(child.reap(&status, true), 0);
    ASSERT_GE(acknowledged, 2U);
    close_store(store); store = open_store();
    SecurityGrantSnapshot snapshot = {};
    ASSERT_EQ(security_grant_store_load(store, &binding, &snapshot), SECURITY_STORE_OK);
    EXPECT_GE(snapshot.generation, acknowledged);
    EXPECT_EQ(snapshot.count, 0U);
    security_grant_snapshot_destroy(&snapshot);
}

TEST_F(SecurityStoreTest, GenerationLeaseSerializesAcknowledgedRevocation) {
    SecurityGrantStore* first = open_store(); SecurityGrantStore* second = open_store();
    SecurityPersistentGrant record = grant(); uint64_t generation;
    ASSERT_EQ(security_grant_store_save(first, &binding, 0, &record, 1, &generation), SECURITY_STORE_OK);
    SecurityStoreResult result;
    SecurityGrantLease* lease = security_grant_store_lease(first, &binding, 1, &result);
    ASSERT_NE(lease, nullptr); ASSERT_EQ(result, SECURITY_STORE_OK);
    SafetyGrantRevocation writer = {second, binding, 1, 0, SECURITY_STORE_INVALID};
    SafetyAsync thread; ASSERT_EQ(thread.start(SafetyGrantRevocation::run, &writer), 0);
    EXPECT_TRUE(SafetyAsync::wait(&thread.started));
    timespec pause = {0, 10000000}; nanosleep(&pause, nullptr);
    EXPECT_EQ(atomic_load32(&thread.done), 0);
    security_grant_store_release(lease);
    ASSERT_EQ(thread.join(), 0);
    EXPECT_EQ(writer.result, SECURITY_STORE_OK); EXPECT_EQ(writer.generation, 2U);
    EXPECT_EQ(security_grant_store_lease(first, &binding, 1, &result), nullptr);
    EXPECT_EQ(result, SECURITY_STORE_CONFLICT); expect_empty(first, 2);
}

TEST_F(SecurityStoreTest, HostInspectionReturnsStoredBindingWithoutGrantingChangedCode) {
    SecurityGrantStore* store = open_store(); SecurityPersistentGrant record = grant(); uint64_t generation;
    ASSERT_EQ(security_grant_store_save(store, &binding, 0, &record, 1, &generation), SECURITY_STORE_OK);
    char* directory = security_grant_store_directory(store); ASSERT_NE(directory, nullptr);
    EXPECT_STREQ(directory, setting); mem_free(directory);
    SecurityGrantSnapshot snapshot = {};
    ASSERT_EQ(security_grant_store_inspect(store, SECURITY_PRINCIPAL_SCRIPT, binding.principal, &snapshot), SECURITY_STORE_OK);
    ASSERT_EQ(snapshot.count, 1U); EXPECT_EQ(memcmp(snapshot.binding.code, binding.code, 32), 0);
    security_grant_snapshot_destroy(&snapshot);
    SecurityGrantBinding changed = binding; changed.code[0]++;
    EXPECT_EQ(security_grant_store_load(store, &changed, &snapshot), SECURITY_STORE_IDENTITY_CHANGED);
    EXPECT_EQ(snapshot.count, 0U);
    EXPECT_EQ(security_grant_store_inspect(store, SECURITY_PRINCIPAL_APP, binding.principal, &snapshot), SECURITY_STORE_IDENTITY_CHANGED);
}

TEST_F(SecurityStoreTest, AccountHomeResolutionIgnoresAppEnvironmentOverrides) {
    struct passwd* account = getpwuid(geteuid());
    ASSERT_NE(account, nullptr);
    char* expected = mem_strdup(account->pw_dir, MEM_CAT_TEMP);
    ASSERT_NE(expected, nullptr);
    struct Home { const char* fixture; const char* expected; } config = {home, expected};
    SafetyChild child;
    auto action = [](void* context, int) -> int {
        Home* config = (Home*)context;
        setenv("HOME", config->fixture, 1); setenv("LAMBDA_HOME", config->fixture, 1);
        char* actual = security_grant_store_account_home();
        return actual && !strcmp(actual, config->expected) && strcmp(actual, config->fixture) ? 0 : 1;
    };
    ASSERT_EQ(child.start(action, &config), 0);
    int status = 0; ASSERT_EQ(child.reap(&status), 0);
    EXPECT_TRUE(WIFEXITED(status)); EXPECT_EQ(WEXITSTATUS(status), 0);
    EXPECT_FALSE(file_exists(setting));
    mem_free(expected);
}
#else
TEST(SecurityStorePlatform, UnsupportedBackendFailsClosed) {
    SecurityStoreResult result;
    EXPECT_EQ(security_grant_store_open(&result), nullptr);
    EXPECT_EQ(result, SECURITY_STORE_UNSUPPORTED);
}
#endif
