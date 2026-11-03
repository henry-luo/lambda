#include <gtest/gtest.h>
#include "../lambda/host/safety/admission.h"
#include "../lambda/io/security_channel.h"
#include "../lib/file_utils.h"
#include "../lib/digest.h"
#include "../lib/hex.h"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/ipc_stream.h"
#include "safety/async_fixture.h"
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
    uint64_t select(const char* path, bool persistent = true, bool directory = false,
            SecurityOperations operations = SECURITY_READ, uint64_t expires = 0) {
        SecurityFileSelection selection = {path, operations, expires, directory, directory, persistent};
        SecurityStoreResult result;
        uint64_t resource = security_admission_select(admission, &selection, &result);
        EXPECT_NE(resource, 0U); EXPECT_EQ(result, SECURITY_STORE_OK);
        return resource;
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

// D7.5.2: exercise the real admission lease at the IPC publication boundary.
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

int main(int argc, char** argv) {
#ifndef _WIN32
    if (argc > 1 && !strcmp(argv[1], "--serve-admission-fixture")) return serve_fixture(argc, argv);
#endif
    ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
