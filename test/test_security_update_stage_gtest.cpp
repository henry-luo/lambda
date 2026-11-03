#include <gtest/gtest.h>
#include "../lambda/host/safety/update_stage.h"
#include "../lambda/host/safety/update_wire.h"
#include "../lib/digest.h"
#include "../lib/hex.h"
#include "../lib/mem.h"
#include "../lib/time_util.h"
#include "safety/update_fixture.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

class SecurityUpdateStageTest : public ::testing::Test {
protected:
    char home[4096] = {};
    SecurityGrantStore* store = nullptr;
    SecurityUpdateControl* control = nullptr;
    SecurityUpdateJob* job = nullptr;
    SecurityVerifiedUpdate* artifact = nullptr;
    SecurityUpdateStageReport report = {};
    static uint64_t clock(void*) { return (uint64_t)time(nullptr); }
    void SetUp() override {
        char directory[3000]; ASSERT_NE(getcwd(directory, sizeof(directory)), nullptr);
        ASSERT_LT(snprintf(home, sizeof(home), "%s/temp/app-safety-stage-unit-XXXXXX", directory), (int)sizeof(home));
        ASSERT_NE(mkdtemp(home), nullptr);
        SecurityStoreResult stored; store = security_grant_store_test_open(home, &stored); ASSERT_NE(store, nullptr);
        ASSERT_EQ(security_update_initialize(store), SECURITY_UPDATE_OK);
        SecurityUpdateResult result; control = security_update_open(store, clock, nullptr, &result); ASSERT_NE(control, nullptr);
        SecurityUpdatePackageSpec spec = update_fixture_spec("lambda.stage");
        ASSERT_EQ(security_update_admit(control, &spec, false), SECURITY_UPDATE_OK);
        job = security_update_begin(control, spec.package, false, &result); ASSERT_NE(job, nullptr);
        artifact = security_update_verified_fixture(job, (const uint8_t*)"archive", 7); ASSERT_NE(artifact, nullptr);
    }
    void TearDown() override {
        security_update_stage_free(&report); security_update_verified_fixture_free(artifact); security_update_job_free(job);
        security_update_close(control); security_grant_store_close(store);
    }
    struct Peer {
        int descriptor;
        const char* mode;
        SecurityUpdateControl* control;
    };
    static void* peer(void* pointer) {
        Peer* p = (Peer*)pointer; IpcStream* stream = ipc_stream_adopt(p->descriptor);
        UpdateWire wire = {stream, 29, 0, 0, time_now_ms() + 20000, nullptr, nullptr};
        UpdateWireMessage m = {}; m.kind = UPDATE_WIRE_READY;
        bool valid = security_update_wire_send(&wire, &m) && security_update_wire_receive(&wire, update_wire_kind_mask(UPDATE_WIRE_STAGE_INIT), &m);
        uint8_t digest[32]; digest_sha256(m.bytes, m.size, digest); security_update_wire_free(&m);
        const char* paths[4] = {"data", "data/file", "data/deep", "data/deep/empty"};
        if (!strcmp(p->mode, "traversal")) paths[0] = "../outside";
        if (!strcmp(p->mode, "absolute")) paths[0] = "/outside";
        if (!strcmp(p->mode, "device")) paths[0] = "CONIN$";
        if (!strcmp(p->mode, "unicode")) paths[0] = "é";
        if (!strcmp(p->mode, "missing-parent")) paths[0] = "absent/child";
        if (!strcmp(p->mode, "case")) paths[1] = "DATA";
        if (!strcmp(p->mode, "duplicate")) paths[1] = "data";
        if (!strcmp(p->mode, "file-parent")) paths[2] = "data/file/child";
        for (unsigned i = 0; valid && i < 4; i++) {
            m = {}; m.kind = UPDATE_WIRE_STAGE_ENTRY; strcpy(m.path, paths[i]);
            m.artifact = i == 0 || i == 2;
            if (i == 1) { m.bytes = (uint8_t*)"payload"; m.size = 7; m.status = 1; }
            valid = security_update_wire_send(&wire, &m);
            m = {};
            if (valid) valid = security_update_wire_receive(&wire, update_wire_kind_mask(UPDATE_WIRE_STAGE_ACK), &m);
            security_update_wire_free(&m);
            if (valid && i == 0 && !strcmp(p->mode, "pause"))
                valid = security_update_set_mode(p->control, "lambda.stage", SECURITY_UPDATE_PAUSED, 0) == SECURITY_UPDATE_OK;
        }
        if (valid) {
            m = {}; m.kind = UPDATE_WIRE_STAGE_END; m.status = !strcmp(p->mode, "count") ? 3 : 4;
            m.limit = !strcmp(p->mode, "length") ? 8 : 7; memcpy(m.digest, digest, 32);
            if (!strcmp(p->mode, "digest")) m.digest[0] ^= 1;
            security_update_wire_send(&wire, &m);
        }
        ipc_stream_close(stream); return nullptr;
    }
    bool run(const char* mode) {
        int pair[2]; if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return false;
        Peer p = {pair[1], mode, control}; pthread_t thread;
        if (pthread_create(&thread, nullptr, peer, &p)) { close(pair[0]); close(pair[1]); return false; }
        bool valid = security_update_stage_fixture(pair[0], 29, job, artifact, &report);
        pthread_join(thread, nullptr); return valid;
    }
    char* stage_path(const char* suffix) {
        char id[33]; hex_encode(report.info.id, 16, id);
        char path[1024]; snprintf(path, sizeof(path), "%s/.lambda/package-staging/s-%s/%s", home, id, suffix);
        return mem_strdup(path, MEM_CAT_SYSTEM);
    }
    void cleanup(uint32_t budget = 1) {
        SecurityUpdateStageInfo info; ASSERT_EQ(security_update_stage_inspect(control, &info), SECURITY_UPDATE_OK);
        bool complete = false;
        for (unsigned i = 0; i < 20 && !complete; i++) ASSERT_EQ(security_update_stage_discard(control, info.id, budget, &complete), SECURITY_UPDATE_OK);
        ASSERT_TRUE(complete);
        EXPECT_EQ(security_update_stage_inspect(control, &info), SECURITY_UPDATE_MISSING);
    }
};
TEST_F(SecurityUpdateStageTest, PublishesOnlyInactivePrivateBytesAndReceipts) {
    ASSERT_TRUE(run("valid")) << security_update_result_name(report.result); ASSERT_NE(report.staged, nullptr); ASSERT_TRUE(report.info.ready);
    SecurityUpdateStageInfo info; ASSERT_TRUE(security_update_staged_info(report.staged, job, &info));
    EXPECT_EQ(info.entries, 4U); EXPECT_EQ(info.bytes, 7U);
    char* path = stage_path("image/data/file");
    FILE* file = fopen(path, "rb"); ASSERT_NE(file, nullptr); char bytes[8] = {}; EXPECT_EQ(fread(bytes, 1, 7, file), 7U); fclose(file);
    EXPECT_STREQ(bytes, "payload"); EXPECT_EQ(file_stat(path).mode & 0777, 0600); mem_free(path);
    EXPECT_EQ(security_update_stage_inspect(control, &info), SECURITY_UPDATE_BUSY);
    security_update_stage_free(&report); cleanup();
}
TEST_F(SecurityUpdateStageTest, IndependentlyRejectsUnsafeWorkerNamespaces) {
    const char* modes[] = {"traversal", "absolute", "device", "unicode", "missing-parent", "case", "duplicate", "file-parent"};
    for (const char* mode : modes) {
        EXPECT_FALSE(run(mode)) << mode; EXPECT_EQ(report.staged, nullptr); cleanup(); security_update_stage_free(&report);
    }
}
TEST_F(SecurityUpdateStageTest, CompletionMustMatchArtifactCountsAndBytes) {
    const char* modes[] = {"count", "length", "digest"};
    for (const char* mode : modes) { EXPECT_FALSE(run(mode)); EXPECT_FALSE(report.info.ready); cleanup(); security_update_stage_free(&report); }
}
TEST_F(SecurityUpdateStageTest, PauseCancelsWritesAndRecoveryRemainsAvailableOffline) {
    EXPECT_FALSE(run("pause")); EXPECT_FALSE(report.info.ready); EXPECT_EQ(report.staged, nullptr);
    ASSERT_EQ(security_update_set_offline(control, true), SECURITY_UPDATE_OK); cleanup();
}
TEST_F(SecurityUpdateStageTest, StaleArtifactCannotCreateANewStage) {
    ASSERT_EQ(security_update_job_finish(job, true), SECURITY_UPDATE_OK); security_update_job_free(job);
    SecurityUpdateResult result; job = security_update_begin(control, "lambda.stage", true, &result); ASSERT_NE(job, nullptr);
    SecurityUpdateStageConfig config = {};
    EXPECT_FALSE(security_update_stage(&config, job, artifact, &report)); EXPECT_FALSE(report.created);
    SecurityUpdateStageInfo info; EXPECT_EQ(security_update_stage_inspect(control, &info), SECURITY_UPDATE_MISSING);
}
TEST_F(SecurityUpdateStageTest, OnePendingImageAndIdentityBoundedRecovery) {
    ASSERT_TRUE(run("valid")); uint8_t id[16]; memcpy(id, report.info.id, 16);
    SecurityUpdateStageInfo info; bool complete;
    EXPECT_EQ(security_update_stage_discard(control, id, 1, &complete), SECURITY_UPDATE_BUSY);
    security_update_stage_free(&report);
    uint8_t wrong[16]; memcpy(wrong, id, 16); wrong[0] ^= 1;
    EXPECT_EQ(security_update_stage_discard(control, wrong, 1, &complete), SECURITY_UPDATE_CONFLICT);
    EXPECT_FALSE(run("valid")); EXPECT_EQ(report.result, SECURITY_UPDATE_BUSY); cleanup(2);
    ASSERT_TRUE(run("valid")); EXPECT_NE(memcmp(id, report.info.id, 16), 0);
    security_update_stage_free(&report);
    EXPECT_EQ(security_update_stage_discard(control, id, 1, &complete), SECURITY_UPDATE_CONFLICT); cleanup();
}
TEST_F(SecurityUpdateStageTest, TamperedReceiptCannotBeViewed) {
    ASSERT_TRUE(run("valid")); char* path = stage_path("manifest.bin");
    FILE* file = fopen(path, "r+b"); ASSERT_NE(file, nullptr); ASSERT_NE(fputc(0xff, file), EOF); fclose(file); mem_free(path);
    SecurityUpdateStageInfo info; EXPECT_FALSE(security_update_staged_info(report.staged, job, &info));
    security_update_stage_free(&report); cleanup();
}
TEST_F(SecurityUpdateStageTest, PauseInvalidatesEveryStagedView) {
    ASSERT_TRUE(run("valid")); SecurityUpdateStageInfo info;
    ASSERT_EQ(security_update_set_mode(control, "lambda.stage", SECURITY_UPDATE_PAUSED, 0), SECURITY_UPDATE_OK);
    EXPECT_FALSE(security_update_staged_info(report.staged, job, &info)); EXPECT_EQ(info.bytes, 0U);
    security_update_stage_free(&report); cleanup();
}
TEST_F(SecurityUpdateStageTest, FinishedReservationCannotConsumeEarlierStage) {
    ASSERT_TRUE(run("valid")); SecurityUpdateStageInfo info;
    ASSERT_EQ(security_update_job_finish(job, true), SECURITY_UPDATE_OK);
    EXPECT_FALSE(security_update_staged_info(report.staged, job, &info));
    security_update_job_free(job); SecurityUpdateResult result;
    job = security_update_begin(control, "lambda.stage", true, &result); ASSERT_NE(job, nullptr);
    EXPECT_FALSE(security_update_staged_info(report.staged, job, &info));
    security_update_stage_free(&report); cleanup();
}
TEST_F(SecurityUpdateStageTest, MissingPublisherConfigurationCannotCreateAStage) {
    SecurityUpdateStageConfig config = {};
    EXPECT_FALSE(security_update_stage(&config, job, artifact, &report));
    EXPECT_EQ(report.result, SECURITY_UPDATE_INVALID); EXPECT_FALSE(report.created);
    EXPECT_FALSE(report.process.launched); EXPECT_EQ(report.staged, nullptr);
}
TEST_F(SecurityUpdateStageTest, RefusesUnrecognizedOrSubstitutedStageObjects) {
    ASSERT_TRUE(run("valid")); char* image = stage_path("image"), *held = stage_path("old-image");
    security_update_stage_free(&report); ASSERT_EQ(rename(image, held), 0); ASSERT_EQ(mkdir(image, 0700), 0);
    SecurityUpdateStageInfo info; ASSERT_EQ(security_update_stage_inspect(control, &info), SECURITY_UPDATE_OK);
    bool complete = false;
    EXPECT_EQ(security_update_stage_discard(control, info.id, 4, &complete), SECURITY_UPDATE_UNSAFE_PATH);
    EXPECT_FALSE(complete); mem_free(image); mem_free(held);
}
TEST_F(SecurityUpdateStageTest, InterruptedInitializationHasIdentityBoundedRecovery) {
    char path[4096]; snprintf(path, sizeof(path), "%s/.lambda/package-staging", home);
    ASSERT_EQ(mkdir(path, 0700), 0);
    FileAnchorOptions options = {FILE_ANCHOR_STRICT_STORAGE}; FileAnchor* root = file_anchor_open(path, &options);
    ASSERT_NE(root, nullptr); uint8_t id[16]; memset(id, 0x17, sizeof(id)); char name[35] = "s-"; hex_encode(id, 16, name + 2);
    FileAnchor* orphan = file_anchor_create_directory(root, name, FILE_ANCHOR_STRICT_STORAGE); ASSERT_NE(orphan, nullptr);
    FILE* claim = file_anchor_create_regular(orphan, ".lease"); ASSERT_NE(claim, nullptr); fclose(claim);
    file_anchor_close(orphan); file_anchor_close(root);
    SecurityUpdateStageInfo info; ASSERT_EQ(security_update_stage_inspect(control, &info), SECURITY_UPDATE_OK);
    EXPECT_EQ(memcmp(info.id, id, 16), 0); EXPECT_FALSE(info.ready); cleanup();
}
TEST_F(SecurityUpdateStageTest, UnknownOrphanPayloadIsNeverTreatedAsInitializationDebris) {
    char path[4096]; snprintf(path, sizeof(path), "%s/.lambda/package-staging", home); ASSERT_EQ(mkdir(path, 0700), 0);
    FileAnchorOptions options = {FILE_ANCHOR_STRICT_STORAGE}; FileAnchor* root = file_anchor_open(path, &options); ASSERT_NE(root, nullptr);
    uint8_t id[16]; memset(id, 0x18, sizeof(id)); char name[35] = "s-"; hex_encode(id, 16, name + 2);
    FileAnchor* orphan = file_anchor_create_directory(root, name, FILE_ANCHOR_STRICT_STORAGE); ASSERT_NE(orphan, nullptr);
    ASSERT_EQ(file_anchor_write_atomic(orphan, "canary", "keep", 4, nullptr), 0);
    SecurityUpdateStageInfo info; EXPECT_EQ(security_update_stage_inspect(control, &info), SECURITY_UPDATE_UNSAFE_PATH);
    bool complete = false; EXPECT_EQ(security_update_stage_discard(control, id, 64, &complete), SECURITY_UPDATE_UNSAFE_PATH);
    FileRevision revision; EXPECT_TRUE(file_anchor_stat(orphan, "canary", &revision)); EXPECT_EQ(revision.stat.size, 4);
    file_anchor_close(orphan); file_anchor_close(root);
}
TEST_F(SecurityUpdateStageTest, CorruptIntentIsNotRepairedOrDeleted) {
    ASSERT_TRUE(run("valid")); uint8_t id[16]; memcpy(id, report.info.id, 16); char* path = stage_path("intent.bin");
    FILE* file = fopen(path, "r+b"); ASSERT_NE(file, nullptr); ASSERT_NE(fputc(0, file), EOF); fclose(file);
    security_update_stage_free(&report); SecurityUpdateStageInfo info;
    EXPECT_EQ(security_update_stage_inspect(control, &info), SECURITY_UPDATE_UNSAFE_PATH);
    bool complete = false; EXPECT_EQ(security_update_stage_discard(control, id, 64, &complete), SECURITY_UPDATE_UNSAFE_PATH);
    EXPECT_TRUE(file_stat(path).exists); mem_free(path);
}
static bool stage_emit(SecurityUpdateResult result, const SecurityUpdateStageInfo* info, bool success, bool viewed, bool complete) {
    char id[33]; hex_encode(info->id, 16, id);
    char output[512]; int length = snprintf(output, sizeof(output),
        "{\"ok\":%s,\"view\":%s,\"complete\":%s,\"ready\":%s,\"result\":\"%s\",\"id\":\"%s\",\"entries\":%u,\"bytes\":%llu}\n",
        success ? "true" : "false", viewed ? "true" : "false", complete ? "true" : "false", info->ready ? "true" : "false",
        security_update_result_name(result), id, info->entries, (unsigned long long)info->bytes);
    return length > 0 && (size_t)length < sizeof(output) && confined_process_emit(STDOUT_FILENO, output, (size_t)length);
}
static uint64_t stage_fixture_clock(void*) { return (uint64_t)time(nullptr); }
static int stage_fixture_main(char** args) {
    int descriptor = atoi(args[0]); const char* home = args[1]; const char* filename = args[2]; const char* mode = args[3];
    SecurityStoreResult stored; SecurityUpdateResult result; SecurityGrantStore* store = security_grant_store_test_open(home, &stored);
    if (!store) return 2;
    result = security_update_initialize(store);
    SecurityUpdateControl* control = result == SECURITY_UPDATE_OK ? security_update_open(store, stage_fixture_clock, nullptr, &result) : nullptr;
    // The real clock and admitted endpoint are host-owned fixture configuration.
    if (!control) { security_grant_store_close(store); return 2; }
    SecurityUpdatePackageSpec spec = update_fixture_spec("lambda.stage");
    result = security_update_admit(control, &spec, false);
    SecurityUpdateJob* job = result == SECURITY_UPDATE_OK ? security_update_begin(control, spec.package, false, &result) : nullptr;
    size_t size = 0; char* bytes = read_binary_file(filename, &size);
    SecurityVerifiedUpdate* artifact = job && bytes ? security_update_verified_fixture(job, (uint8_t*)bytes, size) : nullptr;
    SecurityUpdateStageReport report = {};
    bool success = artifact && security_update_stage_fixture(descriptor, 29, job, artifact, &report);
    SecurityUpdateStageInfo info = {}; bool viewed = success && security_update_staged_info(report.staged, job, &info);
    if (success && !strcmp(mode, "pause")) {
        security_update_set_mode(control, spec.package, SECURITY_UPDATE_PAUSED, 0);
        viewed = security_update_staged_info(report.staged, job, &info);
    }
    bool emitted = stage_emit(report.result, &report.info, success, viewed, false);
    security_update_stage_free(&report); security_update_verified_fixture_free(artifact); mem_free(bytes);
    security_update_job_free(job); security_update_close(control); security_grant_store_close(store);
    return emitted ? 0 : 2;
}
static int stage_manage(const char* home, const char* id, uint32_t budget) {
    SecurityStoreResult stored; SecurityUpdateResult result;
    SecurityGrantStore* store = security_grant_store_test_open(home, &stored);
    if (!store) return 2;
    SecurityUpdateControl* control = security_update_open(store, stage_fixture_clock, nullptr, &result);
    if (!control) { security_grant_store_close(store); return 2; }
    SecurityUpdateStageInfo info = {}; bool complete = false;
    if (id) {
        if (strlen(id) != 32 || !hex_decode(id, 32, info.id, nullptr)) result = SECURITY_UPDATE_INVALID;
        else result = security_update_stage_discard(control, info.id, budget, &complete);
    } else result = security_update_stage_inspect(control, &info);
    bool emitted = stage_emit(result, &info, result == SECURITY_UPDATE_OK, false, complete);
    security_update_close(control); security_grant_store_close(store); return emitted ? 0 : 2;
}
int main(int argc, char** argv) {
    if (argc == 3 && !strcmp(argv[1], "--inspect-stage")) return stage_manage(argv[2], nullptr, 0);
    if (argc == 5 && !strcmp(argv[1], "--discard-stage")) return stage_manage(argv[2], argv[3], (uint32_t)strtoul(argv[4], nullptr, 10));
    if (argc == 6 && !strcmp(argv[1], "--stage-fixture")) return stage_fixture_main(argv + 2);
    ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS();
}
