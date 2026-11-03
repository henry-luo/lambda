#include <gtest/gtest.h>
#include "../lambda/host/safety/update_controls.h"
#include "../lambda/host/safety/update_metadata.h"
#include "../lambda/host/safety/update_records.h"
#include "../lambda/host/safety/update_wire.h"
#include "../lambda/host/safety/state_store.h"
#include "../lib/digest.h"
#include "../lib/endian.h"
#include "../lib/hex.h"
#include "../lib/file_utils.h"
#include "../lib/log.h"
#include "../lib/mem.h"
#include "../lib/atomic.h"
#include "../lib/time_util.h"
#include "safety/async_fixture.h"
#include "safety/update_fixture.h"
#include <errno.h>
#ifndef _WIN32
#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

class SecurityUpdateTest : public ::testing::Test {
protected:
    char* temporary = nullptr;
    char* home = nullptr;
    char* record = nullptr;
    SecurityGrantStore* stores[8] = {};
    SecurityUpdateControl* controls[8] = {};
    SecurityUpdateJob* jobs[32] = {};
    uint64_t now = 1900000000;
    static uint64_t clock(void* context) { return *(uint64_t*)context; }
    void SetUp() override {
        log_init(nullptr); temporary = dir_temp_create("security_update");
        ASSERT_NE(temporary, nullptr); home = file_realpath(temporary); ASSERT_NE(home, nullptr);
        record = file_path_join(home, ".lambda/setting/package-updates.state"); ASSERT_NE(record, nullptr);
    }
    void TearDown() override {
        for (SecurityUpdateJob* job : jobs) security_update_job_free(job);
        for (SecurityUpdateControl* control : controls) security_update_close(control);
        for (SecurityGrantStore* store : stores) security_grant_store_close(store);
        if (temporary) dir_delete(temporary);
        mem_free(record); mem_free(home); mem_free(temporary);
    }
    SecurityGrantStore* store() {
        SecurityStoreResult result; SecurityGrantStore* value = security_grant_store_test_open(home, &result);
        EXPECT_EQ(result, SECURITY_STORE_OK); EXPECT_NE(value, nullptr);
        for (SecurityGrantStore*& slot : stores) if (!slot) { slot = value; return value; }
        ADD_FAILURE() << "store fixture slots exhausted"; security_grant_store_close(value); return nullptr;
    }
    SecurityUpdateControl* open(SecurityGrantStore* saved = nullptr, bool initialize = false) {
        if (!saved) saved = store();
        if (initialize) EXPECT_EQ(security_update_initialize(saved), SECURITY_UPDATE_OK);
        SecurityUpdateResult result;
        SecurityUpdateControl* value = security_update_open(saved, clock, &now, &result);
        EXPECT_EQ(result, SECURITY_UPDATE_OK); EXPECT_NE(value, nullptr);
        for (SecurityUpdateControl*& slot : controls) if (!slot) { slot = value; return value; }
        ADD_FAILURE() << "control fixture slots exhausted"; security_update_close(value); return nullptr;
    }
    SecurityUpdatePackageSpec spec(const char* name = "lambda.fixture") {
        return update_fixture_spec(name);
    }
    void admit(SecurityUpdateControl* control, const char* name = "lambda.fixture") {
        SecurityUpdatePackageSpec value = spec(name); ASSERT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_OK);
    }
    SecurityUpdateJob* begin(SecurityUpdateControl* control, SecurityUpdateResult expected = SECURITY_UPDATE_OK,
            bool manual = false, const char* name = "lambda.fixture") {
        SecurityUpdateResult result; SecurityUpdateJob* value = security_update_begin(control, name, manual, &result);
        EXPECT_EQ(result, expected);
        if (expected == SECURITY_UPDATE_OK) EXPECT_NE(value, nullptr); else EXPECT_EQ(value, nullptr);
        if (!value) return nullptr;
        for (SecurityUpdateJob*& slot : jobs) if (!slot) { slot = value; return value; }
        ADD_FAILURE() << "job fixture slots exhausted"; security_update_job_free(value); return nullptr;
    }
    void close(SecurityUpdateControl* control) {
        for (SecurityUpdateControl*& slot : controls) if (slot == control) slot = nullptr;
        security_update_close(control);
    }
    static constexpr const char* metadata_root = "opaque pinned root fixture";
    void initialize_metadata(SecurityUpdateControl* control) {
        SecurityUpdatePackageSpec value = spec();
        ASSERT_TRUE(digest_sha256(metadata_root, strlen(metadata_root), value.root));
        ASSERT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_OK);
        ASSERT_EQ(security_update_metadata_initialize(control, value.package, metadata_root, strlen(metadata_root)), SECURITY_UPDATE_OK);
    }
    SecurityUpdateResult metadata_replace(SecurityUpdateJob* job, uint64_t generation, const char* role,
            const char* bytes, uint64_t* next) {
        SecurityUpdateMetadata entries[2] = {};
        strcpy(entries[0].role, "root"); entries[0].bytes = (uint8_t*)metadata_root; entries[0].size = strlen(metadata_root);
        strcpy(entries[1].role, role); entries[1].bytes = (uint8_t*)bytes; entries[1].size = strlen(bytes);
        return security_update_metadata_replace(job, generation, entries, 2, next);
    }
    char* metadata_path(const char* package = "lambda.fixture") {
        uint8_t digest[32]; char name[81] = "update-";
        if (!digest_sha256(package, strlen(package), digest)) return nullptr;
        hex_encode(digest, 32, name + 7); strcpy(name + 71, ".metadata");
        char* directory = file_path_join(home, ".lambda/setting");
        char* path = file_path_join(directory, name); mem_free(directory); return path;
    }
};

TEST_F(SecurityUpdateTest, ExplicitEmptyInitializationDoesNotRecoverMissingOrExistingState) {
    SecurityGrantStore* saved = store(); SecurityUpdateResult result;
    EXPECT_EQ(security_update_open(saved, clock, &now, &result), nullptr); EXPECT_EQ(result, SECURITY_UPDATE_MISSING);
    EXPECT_EQ(security_update_initialize(saved), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_initialize(saved), SECURITY_UPDATE_CONFLICT);
    SecurityUpdateControl* control = open(saved);
    SecurityUpdateSnapshot snapshot = {};
    ASSERT_EQ(security_update_inspect(control, &snapshot), SECURITY_UPDATE_OK);
    EXPECT_EQ(snapshot.generation, 1U); EXPECT_EQ(snapshot.count, 0U); EXPECT_FALSE(snapshot.offline);
    security_update_snapshot_free(&snapshot);
    begin(control, SECURITY_UPDATE_MISSING);
    ASSERT_EQ(unlink(record), 0);
    EXPECT_EQ(security_update_inspect(control, &snapshot), SECURITY_UPDATE_MISSING);
    EXPECT_EQ(snapshot.packages, nullptr);
}

TEST_F(SecurityUpdateTest, AdmittedPublicConfigurationSurvivesASeparateHostRestart) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    EXPECT_EQ(security_update_set_mode(control, "lambda.fixture", SECURITY_UPDATE_DISABLED, 0), SECURITY_UPDATE_OK);
    close(control);
    pid_t child = fork(); ASSERT_GE(child, 0);
    if (!child) {
        SecurityStoreResult stored; SecurityUpdateResult result;
        SecurityGrantStore* saved = security_grant_store_test_open(home, &stored);
        SecurityUpdateControl* restarted = saved ? security_update_open(saved, clock, &now, &result) : nullptr;
        SecurityUpdateSnapshot snapshot = {};
        bool valid = restarted && security_update_inspect(restarted, &snapshot) == SECURITY_UPDATE_OK &&
            snapshot.count == 1 && snapshot.packages[0].mode == SECURITY_UPDATE_DISABLED &&
            !strcmp(snapshot.packages[0].spec.metadata_base, "https://updates.example.org/repository/");
        security_update_snapshot_free(&snapshot); security_update_close(restarted); security_grant_store_close(saved);
        _exit(valid ? 0 : 1);
    }
    int status; ASSERT_EQ(waitpid(child, &status, 0), child); ASSERT_TRUE(WIFEXITED(status)); EXPECT_EQ(WEXITSTATUS(status), 0);
    control = open(); begin(control, SECURITY_UPDATE_DISABLE_ACTIVE);
    EXPECT_EQ(security_update_set_mode(control, "lambda.fixture", SECURITY_UPDATE_ENABLED, 0), SECURITY_UPDATE_OK);
    SecurityUpdateJob* job = begin(control); ASSERT_NE(job, nullptr);
    SecurityUpdatePackageSpec public_config = {};
    EXPECT_EQ(security_update_job_describe(job, &public_config), SECURITY_UPDATE_OK);
    EXPECT_STREQ(public_config.channel, "stable"); EXPECT_EQ(public_config.origin_count, 2U);
}

TEST_F(SecurityUpdateTest, GlobalAndPackagePauseDisableRestoreAndExpiryIntersect) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    EXPECT_EQ(security_update_set_mode(control, nullptr, SECURITY_UPDATE_PAUSED, now + 20), SECURITY_UPDATE_OK);
    begin(control, SECURITY_UPDATE_PAUSE_ACTIVE);
    now += 20;
    EXPECT_EQ(security_update_set_mode(control, "lambda.fixture", SECURITY_UPDATE_PAUSED, 0), SECURITY_UPDATE_OK);
    begin(control, SECURITY_UPDATE_PAUSE_ACTIVE);
    EXPECT_EQ(security_update_set_mode(control, "lambda.fixture", SECURITY_UPDATE_ENABLED, 0), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_set_mode(control, nullptr, SECURITY_UPDATE_DISABLED, 0), SECURITY_UPDATE_OK);
    begin(control, SECURITY_UPDATE_DISABLE_ACTIVE);
    EXPECT_EQ(security_update_set_mode(control, nullptr, SECURITY_UPDATE_ENABLED, 0), SECURITY_UPDATE_OK);
    EXPECT_NE(begin(control), nullptr);
}

TEST_F(SecurityUpdateTest, OfflineOverridesTrustedManualChecksAndRestoration) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    EXPECT_EQ(security_update_set_mode(control, nullptr, SECURITY_UPDATE_DISABLED, 0), SECURITY_UPDATE_OK);
    SecurityUpdateJob* manual = begin(control, SECURITY_UPDATE_OK, true); ASSERT_NE(manual, nullptr);
    EXPECT_EQ(security_update_job_finish(manual, true), SECURITY_UPDATE_OK);
    SecurityUpdateSnapshot snapshot = {};
    ASSERT_EQ(security_update_inspect(control, &snapshot), SECURITY_UPDATE_OK);
    EXPECT_EQ(snapshot.mode, SECURITY_UPDATE_DISABLED); security_update_snapshot_free(&snapshot);
    EXPECT_EQ(security_update_set_offline(control, true), SECURITY_UPDATE_OK);
    begin(control, SECURITY_UPDATE_OFFLINE, true);
    EXPECT_EQ(security_update_set_mode(control, nullptr, SECURITY_UPDATE_ENABLED, 0), SECURITY_UPDATE_OK);
    begin(control, SECURITY_UPDATE_OFFLINE);
    close(control); control = open(); begin(control, SECURITY_UPDATE_OFFLINE, true);
    EXPECT_EQ(security_update_set_offline(control, false), SECURITY_UPDATE_OK);
    now += 3600; EXPECT_NE(begin(control), nullptr);
}

TEST_F(SecurityUpdateTest, ControlsCancelAllInflightTicketsWithoutResurrectionAfterRestore) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control); admit(control, "lambda.second");
    SecurityUpdateJob* first = begin(control), *second = begin(control, SECURITY_UPDATE_OK, false, "lambda.second");
    ASSERT_NE(first, nullptr); ASSERT_NE(second, nullptr);
    EXPECT_EQ(security_update_job_check(first), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_job_check(second), SECURITY_UPDATE_OK);
    SecurityUpdateControl* another = open();
    EXPECT_EQ(security_update_set_offline(another, true), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_job_check(first), SECURITY_UPDATE_REVOKED);
    EXPECT_EQ(security_update_job_check(second), SECURITY_UPDATE_REVOKED);
    EXPECT_EQ(security_update_set_offline(another, false), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_job_check(first), SECURITY_UPDATE_REVOKED);
    EXPECT_EQ(security_update_job_finish(first, true), SECURITY_UPDATE_REVOKED);
    SecurityUpdateResult result;
    EXPECT_EQ(security_update_job_enter(second, &result), nullptr); EXPECT_EQ(result, SECURITY_UPDATE_REVOKED);
}

TEST_F(SecurityUpdateTest, ChangedEndpointPublisherChannelAndTrustRequireRevalidationAndKeepDisable) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    EXPECT_EQ(security_update_set_mode(control, "lambda.fixture", SECURITY_UPDATE_DISABLED, 0), SECURITY_UPDATE_OK);
    SecurityUpdatePackageSpec next = spec(); strcpy(next.metadata_base, "https://updates.example.org/next/");
    memset(next.root, 0x55, 32); memset(next.publisher, 0x66, 32); strcpy(next.channel, "next");
    EXPECT_EQ(security_update_admit(control, &next, false), SECURITY_UPDATE_IDENTITY_CHANGED);
    EXPECT_EQ(security_update_admit(control, &next, true), SECURITY_UPDATE_OK);
    begin(control, SECURITY_UPDATE_DISABLE_ACTIVE);
    SecurityUpdateSnapshot snapshot = {};
    ASSERT_EQ(security_update_inspect(control, &snapshot), SECURITY_UPDATE_OK);
    ASSERT_EQ(snapshot.count, 1U); EXPECT_EQ(snapshot.packages[0].mode, SECURITY_UPDATE_DISABLED);
    EXPECT_STREQ(snapshot.packages[0].spec.channel, "next"); security_update_snapshot_free(&snapshot);
}

TEST_F(SecurityUpdateTest, UnsafeAndNoncanonicalEndpointsCannotEnterTheCatalog) {
    SecurityUpdateControl* control = open(nullptr, true);
    const char* rejected[] = {"http://updates.example.org/", "https://user:secret@updates.example.org/",
        "https://updates.example.org/?id=user", "https://updates.example.org/#token", "https://updates.example.org/%2e%2e/",
        "https://updates.example.org/a/../", "https://UPDATES.example.org/", "https://updates.example.org",
        "https://localhost/", "https://127.0.0.1/", "https://[::1]/", "https://0x7f000001/",
        "https://updates.local/", "https://updates.internal/", "https://home.arpa/", "https://updates.home.arpa/",
        "https://updates.example.org./", "https://updates.example.org/\n"};
    for (const char* endpoint : rejected) {
        SecurityUpdatePackageSpec value = spec(); strcpy(value.metadata_base, endpoint);
        EXPECT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_INVALID) << endpoint;
    }
    SecurityUpdatePackageSpec value = spec(); strcpy(value.origins[1], "https://cdn.example.org/path");
    EXPECT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_INVALID);
    value = spec(); strcpy(value.origins[1], value.origins[0]);
    EXPECT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_INVALID);
    value = spec(); strcpy(value.origins[0], "https://other.example.org");
    EXPECT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_INVALID);
    value = spec(); value.poll_seconds = 299;
    EXPECT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_INVALID);
    value = spec(); memset(value.root, 0, 32);
    EXPECT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_INVALID);
}

TEST_F(SecurityUpdateTest, ScheduledReservationsCadenceAndBackoffSurviveRestart) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    SecurityUpdateJob* job = begin(control); ASSERT_NE(job, nullptr);
    SecurityUpdateControl* another = open(); begin(another, SECURITY_UPDATE_BUSY);
    EXPECT_EQ(security_update_job_finish(job, false), SECURITY_UPDATE_OK);
    begin(another, SECURITY_UPDATE_NOT_DUE); now += 300;
    job = begin(another); ASSERT_NE(job, nullptr);
    EXPECT_EQ(security_update_job_finish(job, false), SECURITY_UPDATE_OK);
    now += 599; begin(control, SECURITY_UPDATE_NOT_DUE); now++;
    job = begin(control); ASSERT_NE(job, nullptr); EXPECT_EQ(security_update_job_finish(job, true), SECURITY_UPDATE_OK);
    begin(another, SECURITY_UPDATE_NOT_DUE);
    SecurityUpdateSnapshot snapshot = {};
    ASSERT_EQ(security_update_inspect(another, &snapshot), SECURITY_UPDATE_OK);
    EXPECT_EQ(snapshot.packages[0].failures, 0U); EXPECT_EQ(snapshot.packages[0].next_poll, now + 3600);
    security_update_snapshot_free(&snapshot);
}

TEST_F(SecurityUpdateTest, ExpiredReservationDoesNotLetOldTicketFinishNewJob) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    SecurityUpdateJob* old = begin(control); ASSERT_NE(old, nullptr);
    now += 121; EXPECT_EQ(security_update_job_check(old), SECURITY_UPDATE_REVOKED);
    SecurityUpdateJob* fresh = begin(control, SECURITY_UPDATE_OK, true); ASSERT_NE(fresh, nullptr);
    EXPECT_EQ(security_update_job_finish(old, true), SECURITY_UPDATE_REVOKED);
    EXPECT_EQ(security_update_job_check(fresh), SECURITY_UPDATE_OK);
}

TEST_F(SecurityUpdateTest, CloseInvalidatesTicketsBeforeTheStoreCanBeDestroyed) {
    SecurityGrantStore* saved = store(); SecurityUpdateControl* control = open(saved, true); admit(control);
    SecurityUpdateJob* job = begin(control); ASSERT_NE(job, nullptr);
    close(control);
    for (SecurityGrantStore*& slot : stores) if (slot == saved) slot = nullptr;
    security_grant_store_close(saved);
    EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_CLOSED);
    EXPECT_EQ(security_update_job_finish(job, true), SECURITY_UPDATE_CLOSED);
}

TEST_F(SecurityUpdateTest, CorruptionMissingStateAndReinitializationCannotReviveOldTickets) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    SecurityUpdateJob* job = begin(control); ASSERT_NE(job, nullptr);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(file_read_all(record, MEM_CAT_SYSTEM, &bytes, &size));
    bytes[size - 1] ^= 1; ASSERT_EQ(write_binary_file(record, bytes, size), 0); mem_free(bytes);
    EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_CORRUPT);
    EXPECT_EQ(security_update_initialize(stores[0]), SECURITY_UPDATE_CONFLICT);
    ASSERT_EQ(unlink(record), 0); EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_MISSING);
    EXPECT_EQ(security_update_initialize(stores[0]), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_IDENTITY_CHANGED);
}

TEST_F(SecurityUpdateTest, ChecksumValidUnknownSchemaAndMalformedFieldsStillFailClosed) {
    SecurityGrantStore* saved = store(); SecurityUpdateControl* control = open(saved, true); admit(control);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(file_read_all(record, MEM_CAT_SYSTEM, &bytes, &size));
    char* original = mem_dup_n(bytes, size, MEM_CAT_SYSTEM); ASSERT_NE(original, nullptr);
    // v1 schema: version, reserved bits, modes, count, text and row mode.
    const size_t offsets[] = {8, 12, 80, 84, 96, 100, 104, 119, 104 + 129 + 1025 + 33, 3415};
    for (size_t offset : offsets) {
        memcpy(bytes, original, size); bytes[offset] = (char)0xff;
        ASSERT_TRUE(digest_sha256(bytes, size - 32, (uint8_t*)bytes + size - 32));
        ASSERT_EQ(write_binary_file(record, bytes, size), 0);
        SecurityUpdateSnapshot snapshot = {};
        EXPECT_EQ(security_update_inspect(control, &snapshot), SECURITY_UPDATE_CORRUPT) << offset;
        EXPECT_EQ(snapshot.packages, nullptr);
    }
    mem_free(original); mem_free(bytes);
}

TEST_F(SecurityUpdateTest, ProtectedStateCannotUseLinksOrAReplacedDirectory) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    char* other = file_path_join(home, "outside"); ASSERT_NE(other, nullptr);
    ASSERT_EQ(write_binary_file(other, "private", 7), 0);
    ASSERT_EQ(unlink(record), 0); ASSERT_EQ(symlink(other, record), 0);
    SecurityUpdateSnapshot snapshot = {};
    EXPECT_EQ(security_update_inspect(control, &snapshot), SECURITY_UPDATE_UNSAFE_PATH);
    ASSERT_EQ(unlink(record), 0); ASSERT_EQ(link(other, record), 0);
    EXPECT_EQ(security_update_set_offline(control, false), SECURITY_UPDATE_UNSAFE_PATH);
    ASSERT_EQ(unlink(record), 0);
    char* setting = file_path_dirname(record); char* moved = file_path_join(home, "old-setting");
    ASSERT_EQ(rename(setting, moved), 0); ASSERT_EQ(mkdir(setting, 0700), 0);
    EXPECT_EQ(security_update_inspect(control, &snapshot), SECURITY_UPDATE_UNSAFE_PATH);
    ASSERT_EQ(rmdir(setting), 0); ASSERT_EQ(rename(moved, setting), 0);
    mem_free(moved); mem_free(setting); mem_free(other);
}

struct UpdateReservationWriter {
    SecurityUpdateControl* control;
    int start;
    SecurityUpdateJob* job;
    SecurityUpdateResult result;
};
static void* update_reserve_together(void* context) {
    UpdateReservationWriter* writer = (UpdateReservationWriter*)context;
    char byte;
    if (read(writer->start, &byte, 1) != 1) return nullptr;
    writer->job = security_update_begin(writer->control, "lambda.fixture", false, &writer->result);
    return nullptr;
}
TEST_F(SecurityUpdateTest, IndependentHostsCannotReserveTheSamePackageTwice) {
    SecurityUpdateControl* a = open(nullptr, true); admit(a); SecurityUpdateControl* b = open();
    int start[2]; ASSERT_EQ(pipe(start), 0);
    UpdateReservationWriter writers[] = {{a, start[0], nullptr, SECURITY_UPDATE_INVALID}, {b, start[0], nullptr, SECURITY_UPDATE_INVALID}};
    pthread_t threads[2];
    for (unsigned i = 0; i < 2; i++) ASSERT_EQ(pthread_create(&threads[i], nullptr, update_reserve_together, &writers[i]), 0);
    ASSERT_EQ(write(start[1], "xx", 2), 2);
    for (pthread_t thread : threads) pthread_join(thread, nullptr);
    EXPECT_EQ((writers[0].result == SECURITY_UPDATE_OK) + (writers[1].result == SECURITY_UPDATE_OK), 1);
    EXPECT_TRUE(writers[0].result == SECURITY_UPDATE_BUSY || writers[1].result == SECURITY_UPDATE_BUSY);
    for (UpdateReservationWriter& writer : writers) security_update_job_free(writer.job);
    ::close(start[0]); ::close(start[1]);
}

struct UpdatePauseWriter { SecurityUpdateControl* control; SecurityUpdateResult result; };
static void update_pause_during_publication(void* context) {
    UpdatePauseWriter* writer = (UpdatePauseWriter*)context;
    writer->result = security_update_set_mode(writer->control, nullptr, SECURITY_UPDATE_DISABLED, 0);
}
TEST_F(SecurityUpdateTest, DisableAcknowledgmentWaitsForPriorBoundedPublicationLease) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control); SecurityUpdateControl* other = open();
    SecurityUpdateJob* job = begin(control); ASSERT_NE(job, nullptr);
    SecurityUpdateResult result; SecurityUpdateLease* lease = security_update_job_enter(job, &result);
    ASSERT_NE(lease, nullptr); ASSERT_EQ(result, SECURITY_UPDATE_OK);
    UpdatePauseWriter writer = {other, SECURITY_UPDATE_INVALID}; SafetyAsync thread;
    ASSERT_EQ(thread.start(update_pause_during_publication, &writer), 0);
    EXPECT_TRUE(SafetyAsync::wait(&thread.started));
    struct timespec wait = {0, 20000000}; nanosleep(&wait, nullptr);
    EXPECT_EQ(atomic_load32(&thread.done), 0);
    security_update_job_leave(lease); thread.join();
    EXPECT_EQ(writer.result, SECURITY_UPDATE_OK); EXPECT_EQ(atomic_load32(&thread.done), 1);
    EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_REVOKED);
}

TEST_F(SecurityUpdateTest, FixedStateSlotsAndReadBeforePublishProtectGrantRecords) {
    SecurityGrantStore* saved = store(); SecurityStoreResult status;
    EXPECT_EQ(security_state_begin(saved, (SecurityStateSlot)99, &status), nullptr); EXPECT_EQ(status, SECURITY_STORE_INVALID);
    SecurityStateTransaction* transaction = security_state_begin(saved, SECURITY_STATE_PACKAGE_UPDATES, &status);
    ASSERT_NE(transaction, nullptr);
    EXPECT_EQ(security_state_publish(transaction, "bad", 3), SECURITY_STORE_INVALID);
    char* bytes = nullptr; size_t size = 0; EXPECT_EQ(security_state_read(transaction, &bytes, &size), SECURITY_STORE_MISSING);
    EXPECT_EQ(security_state_publish(transaction, "bad", 3), SECURITY_STORE_OK);
    EXPECT_EQ(security_state_publish(transaction, "again", 5), SECURITY_STORE_INVALID);
    security_state_end(transaction);
    SecurityUpdateResult result;
    EXPECT_EQ(security_update_open(saved, clock, &now, &result), nullptr); EXPECT_EQ(result, SECURITY_UPDATE_CORRUPT);
}

TEST_F(SecurityUpdateTest, UserFileGrantsAreIndependentOfUpdaterControlsAndRestoration) {
    SecurityGrantStore* saved = store();
    SecurityGrantBinding binding = {}; binding.principal_kind = SECURITY_PRINCIPAL_SCRIPT;
    memset(binding.principal, 1, 32); memset(binding.code, 2, 32); memset(binding.root, 3, 32);
    SecurityPersistentGrant grant = {}; memset(grant.resource, 4, 32); grant.operations = SECURITY_READ;
    grant.kind = SECURITY_SAVED_FILE; grant.flags = SECURITY_SAVED_PERSISTENT; grant.locator = "/fixture/report";
    uint64_t generation;
    ASSERT_EQ(security_grant_store_save(saved, &binding, 0, &grant, 1, &generation), SECURITY_STORE_OK);
    SecurityUpdateControl* control = open(saved, true); admit(control);
    EXPECT_EQ(security_update_set_mode(control, nullptr, SECURITY_UPDATE_DISABLED, 0), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_set_offline(control, true), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_set_mode(control, nullptr, SECURITY_UPDATE_ENABLED, 0), SECURITY_UPDATE_OK);
    SecurityGrantSnapshot snapshot = {};
    ASSERT_EQ(security_grant_store_load(saved, &binding, &snapshot), SECURITY_STORE_OK);
    ASSERT_EQ(snapshot.count, 1U); EXPECT_EQ(snapshot.generation, generation);
    EXPECT_EQ(snapshot.grants[0].operations, (SecurityOperations)SECURITY_READ);
    security_grant_snapshot_destroy(&snapshot);
}

TEST_F(SecurityUpdateTest, PackageTableAndRepeatedFailureBackoffStayBounded) {
    SecurityUpdateControl* control = open(nullptr, true);
    for (unsigned i = 0; i < SECURITY_UPDATE_MAX_PACKAGES; i++) {
        char name[32]; snprintf(name, sizeof(name), "lambda.fixture%u", i);
        SecurityUpdatePackageSpec value = spec(name);
        ASSERT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_OK);
    }
    SecurityUpdatePackageSpec extra = spec("lambda.extra");
    EXPECT_EQ(security_update_admit(control, &extra, false), SECURITY_UPDATE_INVALID);
    for (unsigned i = 0; i < 16; i++) {
        SecurityUpdateJob* job = begin(control, SECURITY_UPDATE_OK, false, "lambda.fixture0"); ASSERT_NE(job, nullptr);
        EXPECT_EQ(security_update_job_finish(job, false), SECURITY_UPDATE_OK);
        SecurityUpdateSnapshot snapshot = {};
        ASSERT_EQ(security_update_inspect(control, &snapshot), SECURITY_UPDATE_OK);
        ASSERT_EQ(snapshot.count, SECURITY_UPDATE_MAX_PACKAGES);
        EXPECT_LE(snapshot.packages[0].next_poll - now, 86400U);
        now = snapshot.packages[0].next_poll;
        security_update_snapshot_free(&snapshot);
    }
    now = UINT64_MAX;
    begin(control, SECURITY_UPDATE_INVALID, true, "lambda.fixture0");
}

TEST_F(SecurityUpdateTest, ExpiredJobsRemainRevokedWhenTheWallClockMovesBackwards) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    SecurityUpdateJob* job = begin(control); ASSERT_NE(job, nullptr);
    now += 121; EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_REVOKED);
    now -= 121; EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_REVOKED);
    SecurityUpdatePackageSpec value = spec(); value.job_seconds = 1;
    EXPECT_EQ(security_update_admit(control, &value, true), SECURITY_UPDATE_OK);
    job = begin(control, SECURITY_UPDATE_OK, true); ASSERT_NE(job, nullptr);
    now -= 1000;
    struct timespec wait = {1, 100000000}; nanosleep(&wait, nullptr);
    EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_REVOKED);
}

TEST_F(SecurityUpdateTest, ObservedPolicyRollbackPermanentlyInvalidatesTheLiveController) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    SecurityUpdateJob* job = begin(control); ASSERT_NE(job, nullptr);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(file_read_all(record, MEM_CAT_SYSTEM, &bytes, &size));
    EXPECT_EQ(security_update_set_offline(control, true), SECURITY_UPDATE_OK);
    ASSERT_EQ(write_binary_file(record, bytes, size), 0); mem_free(bytes);
    EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_IDENTITY_CHANGED);
    EXPECT_EQ(security_update_set_offline(control, false), SECURITY_UPDATE_IDENTITY_CHANGED);
    begin(control, SECURITY_UPDATE_IDENTITY_CHANGED, true);
}

TEST_F(SecurityUpdateTest, CopyingTheCatalogToAnotherProfileDoesNotAdmitItsPackages) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    char* other_home = file_path_join(home, "other-profile"); ASSERT_NE(other_home, nullptr);
    ASSERT_EQ(mkdir(other_home, 0700), 0);
    SecurityStoreResult stored;
    stores[1] = security_grant_store_test_open(other_home, &stored); ASSERT_NE(stores[1], nullptr);
    char* copied = file_path_join(other_home, ".lambda/setting/package-updates.state"); ASSERT_NE(copied, nullptr);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(file_read_all(record, MEM_CAT_SYSTEM, &bytes, &size));
    ASSERT_EQ(write_binary_file(copied, bytes, size), 0); ASSERT_EQ(chmod(copied, 0600), 0); mem_free(bytes);
    SecurityUpdateResult result;
    EXPECT_EQ(security_update_open(stores[1], clock, &now, &result), nullptr);
    EXPECT_EQ(result, SECURITY_UPDATE_IDENTITY_CHANGED);
    mem_free(copied); mem_free(other_home);
}

TEST_F(SecurityUpdateTest, MetadataBootstrapRequiresPinnedBytesAndNeverResetsExistingState) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control);
    EXPECT_EQ(security_update_metadata_initialize(control, "lambda.fixture", metadata_root, strlen(metadata_root)), SECURITY_UPDATE_CONFLICT);
    SecurityUpdateJob* job = begin(control); SecurityUpdateMetadataSnapshot snapshot = {};
    ASSERT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_OK);
    EXPECT_EQ(snapshot.generation, 1U); EXPECT_EQ(snapshot.observed_time, now); ASSERT_EQ(snapshot.count, 1U);
    EXPECT_STREQ(snapshot.entries[0].role, "root"); EXPECT_EQ(snapshot.entries[0].size, strlen(metadata_root));
    EXPECT_EQ(memcmp(snapshot.entries[0].bytes, metadata_root, strlen(metadata_root)), 0);
    security_update_metadata_free(&snapshot);
    SecurityUpdatePackageSpec value = spec("lambda.other"); ASSERT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_metadata_initialize(control, value.package, metadata_root, strlen(metadata_root)), SECURITY_UPDATE_IDENTITY_CHANGED);
}

TEST_F(SecurityUpdateTest, MetadataReplacementIsAtomicGenerationCheckedAndDropsRotatedRoles) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control); SecurityUpdateJob* job = begin(control);
    uint64_t next = 0;
    ASSERT_EQ(metadata_replace(job, 1, "timestamp", "signed metadata is opaque to this store", &next), SECURITY_UPDATE_OK);
    EXPECT_EQ(next, 2U);
    EXPECT_EQ(metadata_replace(job, 1, "timestamp", "stale", &next), SECURITY_UPDATE_CONFLICT); EXPECT_EQ(next, 0U);
    ASSERT_EQ(security_update_metadata_observe(job, 2, &next), SECURITY_UPDATE_OK); EXPECT_EQ(next, 3U);
    SecurityUpdateMetadata root = {}; strcpy(root.role, "root"); root.bytes = (uint8_t*)"rotated root"; root.size = 12;
    ASSERT_EQ(security_update_metadata_replace(job, 3, &root, 1, &next), SECURITY_UPDATE_OK); EXPECT_EQ(next, 4U);
    SecurityUpdateMetadataSnapshot snapshot = {}; ASSERT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_OK);
    EXPECT_EQ(snapshot.count, 1U); EXPECT_EQ(security_update_metadata_find(&snapshot, "timestamp"), nullptr);
    security_update_metadata_free(&snapshot);
}

TEST_F(SecurityUpdateTest, MetadataNamesDuplicatesAndLimitsCannotSelectOtherProtectedRecords) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control); SecurityUpdateJob* job = begin(control);
    uint64_t next = 0;
    for (const char* role : {"", "bad\nname", "\xC0\xAF", "root"})
        EXPECT_EQ(metadata_replace(job, 1, role, "bytes", &next), SECURITY_UPDATE_INVALID) << role;
    EXPECT_EQ(metadata_replace(job, 1, "delegated/project", "bytes", &next), SECURITY_UPDATE_OK);
    SecurityUpdateMetadata root = {}; strcpy(root.role, "root"); root.bytes = (uint8_t*)metadata_root;
    root.size = SECURITY_UPDATE_METADATA_ROOT_BYTES + 1;
    EXPECT_EQ(security_update_metadata_replace(job, 2, &root, 1, &next), SECURITY_UPDATE_INVALID);
    EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_OK);
}

TEST_F(SecurityUpdateTest, OpaqueMetadataLabelsNeverSelectPathsAndPreserveUnicodeIdentity) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control); SecurityUpdateJob* job = begin(control);
    char* canary = file_path_join(home, ".lambda/setting/opaque-canary.state"); ASSERT_NE(canary, nullptr);
    ASSERT_EQ(write_binary_file(canary, "protected", 9), 0);
    uint64_t generation = 1, next = 0;
    for (const char* role : {"../opaque-canary.state", "/root", "a//b", "a/../b", "a?label=x", "名/😀", "%2F"}) {
        ASSERT_EQ(metadata_replace(job, generation, role, "opaque metadata", &next), SECURITY_UPDATE_OK); generation = next;
        SecurityUpdateMetadataSnapshot snapshot = {}; ASSERT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_OK);
        ASSERT_EQ(snapshot.count, 2U); EXPECT_STREQ(snapshot.entries[1].role, role); security_update_metadata_free(&snapshot);
    }
    SecurityUpdateMetadata rows[3] = {};
    const char* names[] = {"root", "é", "e\xCC\x81"};
    for (unsigned i = 0; i < 3; i++) { strcpy(rows[i].role, names[i]); rows[i].bytes = (uint8_t*)metadata_root; rows[i].size = strlen(metadata_root); }
    ASSERT_EQ(security_update_metadata_replace(job, generation, rows, 3, &next), SECURITY_UPDATE_OK);
    ASSERT_EQ(security_update_job_finish(job, true), SECURITY_UPDATE_OK); close(control);
    control = open(); job = begin(control, SECURITY_UPDATE_OK, true);
    SecurityUpdateMetadataSnapshot snapshot = {}; ASSERT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_OK);
    ASSERT_EQ(snapshot.count, 3U); EXPECT_NE(security_update_metadata_find(&snapshot, names[1]), nullptr);
    EXPECT_NE(security_update_metadata_find(&snapshot, names[2]), nullptr); security_update_metadata_free(&snapshot);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(file_read_all(canary, MEM_CAT_SYSTEM, &bytes, &size));
    EXPECT_EQ(size, 9U); EXPECT_EQ(memcmp(bytes, "protected", 9), 0); mem_free(bytes); mem_free(canary);
}

TEST_F(SecurityUpdateTest, MetadataPauseAcknowledgementPreventsCheckpointsAndRestoreDoesNotReviveTicket) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control); SecurityUpdateJob* job = begin(control);
    ASSERT_EQ(security_update_set_mode(control, nullptr, SECURITY_UPDATE_PAUSED, 0), SECURITY_UPDATE_OK);
    uint64_t next = 0;
    EXPECT_EQ(metadata_replace(job, 1, "timestamp", "bytes", &next), SECURITY_UPDATE_REVOKED);
    ASSERT_EQ(security_update_set_mode(control, nullptr, SECURITY_UPDATE_ENABLED, 0), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_metadata_observe(job, 1, &next), SECURITY_UPDATE_REVOKED);
}

TEST_F(SecurityUpdateTest, MetadataClockWatermarkSurvivesControllerRestart) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control); SecurityUpdateJob* job = begin(control);
    uint64_t next = 0; now += 10;
    ASSERT_EQ(security_update_metadata_observe(job, 1, &next), SECURITY_UPDATE_OK);
    EXPECT_EQ(security_update_job_finish(job, true), SECURITY_UPDATE_OK); close(control);
    now -= 5; control = open(); job = begin(control, SECURITY_UPDATE_OK, true);
    SecurityUpdateMetadataSnapshot snapshot = {};
    EXPECT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_REVOKED);
    now += 5; EXPECT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_REVOKED);
}

TEST_F(SecurityUpdateTest, MetadataMissingOrCorruptStateInvalidatesCapturedJobAndIsNeverInitializedByRead) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control); SecurityUpdateJob* job = begin(control);
    char* path = metadata_path(); ASSERT_NE(path, nullptr);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(file_read_all(path, MEM_CAT_SYSTEM, &bytes, &size));
    bytes[size - 1] ^= 1; ASSERT_EQ(write_binary_file(path, bytes, size), 0);
    SecurityUpdateMetadataSnapshot snapshot = {};
    EXPECT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_CORRUPT);
    EXPECT_EQ(snapshot.entries, nullptr);
    EXPECT_EQ(security_update_metadata_initialize(control, "lambda.fixture", metadata_root, strlen(metadata_root)), SECURITY_UPDATE_CORRUPT);
    ASSERT_EQ(unlink(path), 0);
    EXPECT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_REVOKED);
    mem_free(bytes); mem_free(path);
}

TEST_F(SecurityUpdateTest, MetadataObservedBackupRollbackInvalidatesEntireController) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control); SecurityUpdateJob* job = begin(control);
    char* path = metadata_path(); ASSERT_NE(path, nullptr);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(file_read_all(path, MEM_CAT_SYSTEM, &bytes, &size));
    uint64_t next = 0; ASSERT_EQ(security_update_metadata_observe(job, 1, &next), SECURITY_UPDATE_OK);
    ASSERT_EQ(write_binary_file(path, bytes, size), 0);
    SecurityUpdateMetadataSnapshot snapshot = {};
    EXPECT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_IDENTITY_CHANGED);
    begin(control, SECURITY_UPDATE_IDENTITY_CHANGED, true);
    mem_free(bytes); mem_free(path);
}

TEST_F(SecurityUpdateTest, MetadataAuthorityChangesCannotReuseExistingTrustCache) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control);
    SecurityUpdatePackageSpec value = spec(); ASSERT_TRUE(digest_sha256(metadata_root, strlen(metadata_root), value.root));
    strcpy(value.channel, "next"); ASSERT_EQ(security_update_admit(control, &value, true), SECURITY_UPDATE_OK);
    SecurityUpdateJob* job = begin(control); SecurityUpdateMetadataSnapshot snapshot = {};
    EXPECT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_IDENTITY_CHANGED);
    EXPECT_EQ(snapshot.entries, nullptr);
}

TEST_F(SecurityUpdateTest, MetadataCopiedToAnotherAdmittedPackageCannotEstablishItsTrust) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control); admit(control, "lambda.other");
    char* path = metadata_path(); char* copied = metadata_path("lambda.other");
    ASSERT_NE(path, nullptr); ASSERT_NE(copied, nullptr);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(file_read_all(path, MEM_CAT_SYSTEM, &bytes, &size));
    ASSERT_EQ(write_binary_file(copied, bytes, size), 0); ASSERT_EQ(chmod(copied, 0600), 0);
    SecurityUpdateJob* job = begin(control, SECURITY_UPDATE_OK, false, "lambda.other");
    SecurityUpdateMetadataSnapshot snapshot = {};
    EXPECT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_IDENTITY_CHANGED);
    mem_free(bytes); mem_free(path); mem_free(copied);
}

TEST_F(SecurityUpdateTest, MetadataSameGenerationReplacementWithValidChecksumInvalidatesController) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control); SecurityUpdateJob* job = begin(control);
    char* path = metadata_path(); ASSERT_NE(path, nullptr);
    char* bytes = nullptr; size_t size = 0; ASSERT_TRUE(file_read_all(path, MEM_CAT_SYSTEM, &bytes, &size));
    ASSERT_GT(size, 33U); bytes[size - 33] ^= 1;
    ASSERT_TRUE(digest_sha256(bytes, size - 32, (uint8_t*)bytes + size - 32));
    ASSERT_EQ(write_binary_file(path, bytes, size), 0);
    SecurityUpdateMetadataSnapshot snapshot = {};
    EXPECT_EQ(security_update_metadata_read(job, &snapshot), SECURITY_UPDATE_IDENTITY_CHANGED);
    begin(control, SECURITY_UPDATE_IDENTITY_CHANGED, true);
    mem_free(bytes); mem_free(path);
}

TEST_F(SecurityUpdateTest, MetadataObservedHistoryCannotBeResetByReinitializingAMissingRecord) {
    SecurityUpdateControl* control = open(nullptr, true); initialize_metadata(control); SecurityUpdateJob* job = begin(control);
    uint64_t next = 0; ASSERT_EQ(security_update_metadata_observe(job, 1, &next), SECURITY_UPDATE_OK);
    char* path = metadata_path(); ASSERT_NE(path, nullptr); ASSERT_EQ(unlink(path), 0);
    EXPECT_EQ(security_update_metadata_initialize(control, "lambda.fixture", metadata_root, strlen(metadata_root)), SECURITY_UPDATE_IDENTITY_CHANGED);
    EXPECT_EQ(access(path, F_OK), -1); EXPECT_EQ(errno, ENOENT);
    mem_free(path);
}

TEST_F(SecurityUpdateTest, VerifiedReservationBindingsCannotSurviveFinishAndRebegin) {
    SecurityUpdateControl* control = open(nullptr, true); admit(control);
    SecurityUpdateJob* first = begin(control); uint8_t original[32], repeated[32], next[32];
    ASSERT_TRUE(security_update_job_binding(first, original)); ASSERT_TRUE(security_update_job_binding(first, repeated));
    EXPECT_EQ(memcmp(original, repeated, 32), 0);
    ASSERT_EQ(security_update_job_finish(first, true), SECURITY_UPDATE_OK);
    EXPECT_FALSE(security_update_job_binding(first, repeated));
    SecurityUpdateJob* second = begin(control, SECURITY_UPDATE_OK, true);
    ASSERT_TRUE(security_update_job_binding(second, next)); EXPECT_NE(memcmp(original, next, 32), 0);
    ASSERT_EQ(security_update_set_offline(control, true), SECURITY_UPDATE_OK);
    EXPECT_FALSE(security_update_job_binding(second, next));
}

TEST_F(SecurityUpdateTest, RoundedUtcReservationDoesNotExpireAtTheNextClockTick) {
    SecurityUpdateControl* control = open(nullptr, true);
    SecurityUpdatePackageSpec value = spec(); value.job_seconds = 1;
    ASSERT_EQ(security_update_admit(control, &value, false), SECURITY_UPDATE_OK);
    SecurityUpdateJob* job = begin(control); now++;
    EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_OK);
    begin(control, SECURITY_UPDATE_BUSY, true);
    now++; EXPECT_EQ(security_update_job_check(job), SECURITY_UPDATE_REVOKED);
}

class SecurityUpdateWireTest : public ::testing::Test {
protected:
    UpdateWire peers[2] = {}; int writer = -1;
    void SetUp() override {
        int pair[2]; ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0); writer = pair[0];
        for (unsigned i = 0; i < 2; i++) {
            peers[i] = {ipc_stream_adopt(pair[i]), 7, 0, 0, time_now_ms() + 2000, nullptr, nullptr};
            ASSERT_NE(peers[i].stream, nullptr);
        }
    }
    void TearDown() override { for (auto& peer : peers) ipc_stream_close(peer.stream); }
    void header(uint32_t kind, uint64_t sequence, uint32_t length, uint64_t session = 7) {
        uint8_t bytes[36] = {'L','M','T','U','F','I','P','1'};
        write_le32(bytes + 8, 1); write_le32(bytes + 12, kind); write_le64(bytes + 16, session);
        write_le64(bytes + 24, sequence); write_le32(bytes + 32, length);
        ASSERT_EQ(write(writer, bytes, sizeof(bytes)), sizeof(bytes));
    }
    void refused() {
        UpdateWireMessage message = {};
        EXPECT_FALSE(security_update_wire_receive(&peers[1], update_wire_kind_mask(UPDATE_WIRE_READY), &message));
        EXPECT_TRUE(peers[1].failed); EXPECT_EQ(message.entries, nullptr); EXPECT_EQ(message.bytes, nullptr);
        EXPECT_FALSE(security_update_wire_receive(&peers[1], update_wire_kind_mask(UPDATE_WIRE_READY), &message));
    }
};
TEST_F(SecurityUpdateWireTest, ReadyAndMetadataUseSeparateMonotonicDirections) {
    UpdateWireMessage ready = {}; ready.kind = UPDATE_WIRE_READY;
    ASSERT_TRUE(security_update_wire_send(&peers[0], &ready));
    UpdateWireMessage received = {};
    ASSERT_TRUE(security_update_wire_receive(&peers[1], update_wire_kind_mask(UPDATE_WIRE_READY), &received));
    EXPECT_EQ(peers[0].sent, 1U); EXPECT_EQ(peers[1].received, 1U);
    SecurityUpdateMetadata row = {}; strcpy(row.role, "root"); row.bytes = (uint8_t*)"opaque"; row.size = 6;
    UpdateWireMessage init = {}; init.kind = UPDATE_WIRE_INIT; init.count = 1; init.entries = &row;
    strcpy(init.path, "lambda.pkg"); strcpy(init.utc, "2026-11-03T00:00:00Z"); init.limit = 20000;
    ASSERT_TRUE(security_update_wire_send(&peers[1], &init));
    ASSERT_TRUE(security_update_wire_receive(&peers[0], update_wire_kind_mask(UPDATE_WIRE_INIT), &received));
    ASSERT_EQ(received.count, 1U); EXPECT_STREQ(received.entries[0].role, "root");
    EXPECT_EQ(memcmp(received.entries[0].bytes, "opaque", 6), 0); security_update_wire_free(&received);
}
TEST_F(SecurityUpdateWireTest, WrongSessionPoisonsTheConnectionBeforeAllocation) {
    header(UPDATE_WIRE_READY, 1, UPDATE_WIRE_FIXED_BYTES, 8); refused();
}
TEST_F(SecurityUpdateWireTest, ReplayPoisonsTheConnectionBeforeAllocation) {
    header(UPDATE_WIRE_READY, 0, UPDATE_WIRE_FIXED_BYTES); refused();
}
TEST_F(SecurityUpdateWireTest, OversizedHeaderPoisonsTheConnectionBeforeAllocation) {
    header(UPDATE_WIRE_READY, 1, UINT32_MAX); refused();
}
TEST_F(SecurityUpdateWireTest, UnexpectedKindCannotCrossTheHandshake) {
    header(UPDATE_WIRE_RESPONSE, 1, UPDATE_WIRE_FIXED_BYTES); refused();
}
TEST_F(SecurityUpdateWireTest, UnknownKindCannotBecomeAnUndefinedMaskShift) {
    header(64, 1, UPDATE_WIRE_FIXED_BYTES); refused();
}
TEST_F(SecurityUpdateWireTest, StageTrafficCannotCrossTheVerifierHandshake) {
    header(UPDATE_WIRE_STAGE_INIT, 1, UPDATE_WIRE_FRAME_LIMIT); refused();
}
TEST_F(SecurityUpdateWireTest, StageMemberHeaderLimitPrecedesPayloadAllocation) {
    header(UPDATE_WIRE_STAGE_ENTRY, 1, UPDATE_WIRE_FIXED_BYTES + UPDATE_WIRE_STAGE_MEMBER_LIMIT + 1);
    UpdateWireMessage received = {};
    EXPECT_FALSE(security_update_wire_receive(&peers[1], update_wire_kind_mask(UPDATE_WIRE_STAGE_ENTRY), &received));
    EXPECT_TRUE(peers[1].failed); EXPECT_EQ(received.bytes, nullptr);
}
TEST_F(SecurityUpdateWireTest, StageDirectoryCannotCarryBytesOrExecutionFlags) {
    UpdateWireMessage message = {}; message.kind = UPDATE_WIRE_STAGE_ENTRY; strcpy(message.path, "data");
    message.artifact = 1; message.status = 1;
    EXPECT_FALSE(security_update_wire_send(&peers[0], &message));
    message.status = 0; message.size = 1; message.bytes = (uint8_t*)"x";
    EXPECT_FALSE(security_update_wire_send(&peers[0], &message));
    message.size = 0; message.bytes = nullptr;
    ASSERT_TRUE(security_update_wire_send(&peers[0], &message));
    UpdateWireMessage received = {};
    ASSERT_TRUE(security_update_wire_receive(&peers[1], update_wire_kind_mask(UPDATE_WIRE_STAGE_ENTRY), &received));
    EXPECT_EQ(received.artifact, 1U); EXPECT_STREQ(received.path, "data"); security_update_wire_free(&received);
}
TEST_F(SecurityUpdateWireTest, StageCompletionBindsTheArchiveAndBoundedCounts) {
    UpdateWireMessage message = {}; message.kind = UPDATE_WIRE_STAGE_END;
    EXPECT_FALSE(security_update_wire_send(&peers[0], &message));
    message.digest[0] = 1; message.status = 4097;
    EXPECT_FALSE(security_update_wire_send(&peers[0], &message));
    message.status = 4; message.limit = UPDATE_WIRE_BODY_LIMIT + 1ULL;
    EXPECT_FALSE(security_update_wire_send(&peers[0], &message));
    message.limit = 16;
    ASSERT_TRUE(security_update_wire_send(&peers[0], &message));
    UpdateWireMessage received = {};
    ASSERT_TRUE(security_update_wire_receive(&peers[1], update_wire_kind_mask(UPDATE_WIRE_STAGE_END), &received));
    EXPECT_EQ(received.status, 4U); EXPECT_EQ(received.limit, 16U); EXPECT_EQ(received.digest[0], 1U);
    security_update_wire_free(&received);
}
TEST_F(SecurityUpdateWireTest, StageAcknowledgementCannotAllocateABody) {
    header(UPDATE_WIRE_STAGE_ACK, 1, UPDATE_WIRE_FIXED_BYTES + 1);
    UpdateWireMessage received = {};
    EXPECT_FALSE(security_update_wire_receive(&peers[1], update_wire_kind_mask(UPDATE_WIRE_STAGE_ACK), &received));
    EXPECT_TRUE(peers[1].failed); EXPECT_EQ(received.bytes, nullptr);
}

static uint64_t update_seed_clock(void*) { return (uint64_t)time(nullptr); }
static int update_seed_fixture(const char* home) {
    SecurityStoreResult stored; SecurityUpdateResult result;
    SecurityGrantStore* saved = security_grant_store_test_open(home, &stored);
    if (!saved) return 2;
    result = security_update_initialize(saved);
    SecurityUpdateControl* control = (result == SECURITY_UPDATE_OK || result == SECURITY_UPDATE_CONFLICT)
        ? security_update_open(saved, update_seed_clock, nullptr, &result) : nullptr;
    if (control) {
        const char* names[] = {"lambda.fixture", "all"};
        for (const char* name : names) {
            SecurityUpdatePackageSpec value = update_fixture_spec(name);
            result = security_update_admit(control, &value, false);
            if (result != SECURITY_UPDATE_OK) break;
        }
    }
    security_update_close(control); security_grant_store_close(saved);
    return result == SECURITY_UPDATE_OK ? 0 : 2;
}
#else
TEST(SecurityUpdateUnavailable, UnsupportedPlatformDoesNotCreateAnAmbientUpdater) {
    SecurityStoreResult stored;
    EXPECT_EQ(security_grant_store_open(&stored), nullptr);
    EXPECT_EQ(stored, SECURITY_STORE_UNSUPPORTED);
    EXPECT_EQ(security_update_initialize(nullptr), SECURITY_UPDATE_INVALID);
    SecurityUpdateResult result;
    EXPECT_EQ(security_update_open(nullptr, nullptr, nullptr, &result), nullptr);
    EXPECT_EQ(result, SECURITY_UPDATE_INVALID);
}
#endif

int main(int argc, char** argv) {
#ifndef _WIN32
    if (argc == 3 && !strcmp(argv[1], "--seed-fixture")) return update_seed_fixture(argv[2]);
#endif
    ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
