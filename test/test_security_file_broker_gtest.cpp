#include <gtest/gtest.h>
#include "../lambda/host/safety/file_broker.h"
#include "lib/file_utils.h"
#include "lib/log.h"
#include "lib/digest.h"
#include <errno.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>

class SecurityFileBrokerTest : public ::testing::Test {
protected:
    char* temporary = nullptr;
    char* directory = nullptr;
    char* project = nullptr;
    char* protected_path = nullptr;
    SecurityFileBroker* broker = nullptr;
    SecurityIOClient client = {};
    uint64_t clock_value = 100;
    uint64_t project_id = 0;

    static uint64_t clock(void* context) { return *(uint64_t*)context; }

    void SetUp() override {
        log_init(nullptr);
        temporary = dir_temp_create("security_file_broker");
        ASSERT_NE(temporary, nullptr);
        directory = file_realpath(temporary);
        ASSERT_NE(directory, nullptr);
        project = file_path_join(directory, "project");
        protected_path = file_path_join(project, "protected");
        ASSERT_TRUE(create_dir(protected_path));
        reset_broker();
    }

    void reset_broker(uint64_t total_bytes = 1024 * 1024, uint32_t operations = 128) {
        security_file_broker_destroy(broker);
        SecurityFileBrokerConfig config = {{31, 1031, SECURITY_ALL_OPERATIONS, 64, 64},
            4096, total_bytes, 8192, 4, operations, 60, clock, &clock_value};
        broker = security_file_broker_create(&config);
        ASSERT_NE(broker, nullptr);
        ASSERT_TRUE(security_file_broker_protect(broker, protected_path, SECURITY_ALL_OPERATIONS));
        client = security_file_broker_client(broker);
    }

    void TearDown() override {
        security_file_broker_destroy(broker);
        if (temporary) dir_delete(temporary);
        mem_free(protected_path); mem_free(project); mem_free(directory); mem_free(temporary);
    }

    uint64_t admit_project() {
        SecurityFileRootSpec spec = {project, SECURITY_SCOPE_PROJECT, SECURITY_ALL_OPERATIONS,
            0, 0, true, true, nullptr};
        project_id = security_file_broker_admit_root(broker, &spec);
        EXPECT_NE(project_id, 0U);
        return project_id;
    }

    void seed(const char* relative, const char* bytes) {
        char* path = file_path_join(project, relative);
        ASSERT_NE(path, nullptr);
        ASSERT_EQ(write_binary_file(path, bytes, strlen(bytes)), 0);
        mem_free(path);
    }

    void expect_text(const char* relative, const char* expected) {
        char* path = file_path_join(project, relative);
        ASSERT_NE(path, nullptr);
        char* bytes = nullptr; size_t size = 0;
        ASSERT_TRUE(file_read_all(path, MEM_CAT_TEMP, &bytes, &size));
        EXPECT_EQ(size, strlen(expected));
        EXPECT_EQ(memcmp(bytes, expected, size), 0);
        mem_free(bytes); mem_free(path);
    }

    SecurityIOResult write(const char* path, const char* text, bool append = false, bool atomic = true) {
        return client.write(client.context, path, text, strlen(text), append, atomic);
    }

    SecurityIOResult read(const char* path, bool source = false, uint64_t limit = 4096) {
        char* bytes = nullptr; size_t size = 0;
        SecurityIOResult result = client.read(client.context, path, source, limit, MEM_CAT_TEMP, &bytes, &size);
        if (result.decision != SECURITY_ALLOWED || result.system_error) {
            EXPECT_EQ(bytes, nullptr); EXPECT_EQ(size, 0U);
        }
        mem_free(bytes);
        return result;
    }
};

TEST_F(SecurityFileBrokerTest, NewFilesAreExclusiveAndEveryReopenNeedsApproval) {
    admit_project();
    SecurityIOResult fresh = write("one/two/report.txt", "first");
    EXPECT_EQ(fresh.decision, SECURITY_ALLOWED);
    EXPECT_EQ(fresh.system_error, 0);
    EXPECT_TRUE(fresh.committed);
    expect_text("one/two/report.txt", "first");
    SecurityIOResult next = write("one/two/report.txt", "second");
    EXPECT_EQ(next.decision, SECURITY_APPROVAL_REQUIRED);
    EXPECT_FALSE(next.committed);
    expect_text("one/two/report.txt", "first");
    ASSERT_TRUE(security_file_broker_cancel(broker, next.approval));
    EXPECT_EQ(security_file_broker_approve(broker, next.approval).decision, SECURITY_REVOKED);
    expect_text("one/two/report.txt", "first");
}

TEST_F(SecurityFileBrokerTest, ApprovalBindsPrivatePayloadTargetRevisionAndOperation) {
    admit_project(); seed("report", "old");
    char payload[] = "new";
    SecurityIOResult pending = client.write(client.context, "report", payload, 3, false, true);
    ASSERT_EQ(pending.decision, SECURITY_APPROVAL_REQUIRED);
    SecurityOverwriteChallenge challenge = {};
    ASSERT_TRUE(security_file_broker_describe(broker, pending.approval, &challenge));
    EXPECT_EQ(challenge.principal, 31U);
    EXPECT_EQ(challenge.operation, SECURITY_REPLACE);
    EXPECT_EQ(challenge.size, 3U);
    uint8_t digest[32]; ASSERT_TRUE(digest_sha256(payload, 3, digest));
    EXPECT_EQ(memcmp(digest, challenge.digest, 32), 0);
    memset(payload, 'x', 3);
    SecurityIOResult approved = security_file_broker_approve(broker, pending.approval);
    EXPECT_EQ(approved.decision, SECURITY_ALLOWED);
    EXPECT_EQ(approved.system_error, 0);
    EXPECT_TRUE(approved.committed);
    expect_text("report", "new");
    EXPECT_EQ(security_file_broker_approve(broker, pending.approval).decision, SECURITY_REVOKED);
    EXPECT_EQ(write("report", "again").decision, SECURITY_APPROVAL_REQUIRED);
}

TEST_F(SecurityFileBrokerTest, AppendAndInPlaceWritesWaitForApproval) {
    admit_project(); seed("report", "old");
    SecurityIOResult pending = write("report", "more", true);
    ASSERT_EQ(pending.decision, SECURITY_APPROVAL_REQUIRED);
    expect_text("report", "old");
    SecurityOverwriteChallenge description;
    ASSERT_TRUE(security_file_broker_describe(broker, pending.approval, &description));
    EXPECT_EQ(description.operation, SECURITY_APPEND);
    EXPECT_EQ(security_file_broker_approve(broker, pending.approval).system_error, 0);
    expect_text("report", "oldmore");
    pending = write("report", "replace", false, false);
    ASSERT_EQ(pending.decision, SECURITY_APPROVAL_REQUIRED);
    EXPECT_EQ(security_file_broker_approve(broker, pending.approval).system_error, 0);
    expect_text("report", "replace");
}

TEST_F(SecurityFileBrokerTest, ChangedTargetOrNamespaceCannotUseOldConsent) {
    admit_project(); seed("report", "old");
    SecurityIOResult pending = write("report", "approved");
    ASSERT_EQ(pending.decision, SECURITY_APPROVAL_REQUIRED);
    seed("report", "external change");
    SecurityIOResult result = security_file_broker_approve(broker, pending.approval);
    EXPECT_EQ(result.system_error, ESTALE);
    EXPECT_FALSE(result.committed);
    expect_text("report", "external change");
    pending = write("report", "approved", false, false);
    ASSERT_EQ(pending.decision, SECURITY_APPROVAL_REQUIRED);
    char* report = file_path_join(project, "report");
    char* moved = file_path_join(project, "moved");
    ASSERT_EQ(rename(report, moved), 0);
    seed("report", "replacement object");
    result = security_file_broker_approve(broker, pending.approval);
    EXPECT_EQ(result.system_error, ESTALE);
    EXPECT_FALSE(result.committed);
    expect_text("report", "replacement object");
    expect_text("moved", "external change");
    mem_free(report); mem_free(moved);
}

TEST_F(SecurityFileBrokerTest, ExpiredCrossSessionAndRevokedConsentCannotCommit) {
    admit_project(); seed("report", "old");
    SecurityIOResult pending = write("report", "new");
    ASSERT_EQ(pending.decision, SECURITY_APPROVAL_REQUIRED);
    SecurityApprovalRef wrong = pending.approval; wrong.session++;
    EXPECT_EQ(security_file_broker_approve(broker, wrong).decision, SECURITY_REVOKED);
    clock_value = 160;
    EXPECT_EQ(security_file_broker_approve(broker, pending.approval).decision, SECURITY_REVOKED);
    pending = write("report", "new");
    ASSERT_EQ(pending.decision, SECURITY_APPROVAL_REQUIRED);
    ASSERT_TRUE(security_file_broker_revoke_root(broker, project_id));
    EXPECT_EQ(security_file_broker_approve(broker, pending.approval).decision, SECURITY_REVOKED);
    EXPECT_EQ(write("report", "new").decision, SECURITY_DENIED);
    expect_text("report", "old");
}

TEST_F(SecurityFileBrokerTest, PermissionReductionInvalidatesPendingWork) {
    admit_project(); seed("report", "old");
    SecurityIOResult pending = write("report", "new");
    ASSERT_EQ(pending.decision, SECURITY_APPROVAL_REQUIRED);
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_READ));
    EXPECT_EQ(security_file_broker_approve(broker, pending.approval).decision, SECURITY_REVOKED);
    EXPECT_EQ(write("no/created/file", "new").decision, SECURITY_DENIED);
    char* no = file_path_join(project, "no"); EXPECT_FALSE(file_exists(no)); mem_free(no);
    expect_text("report", "old");
}

TEST_F(SecurityFileBrokerTest, DataAndSourceByteRequestsShareAuthority) {
    admit_project(); seed("module.ls", "42");
    EXPECT_EQ(read("module.ls").system_error, 0);
    EXPECT_EQ(read("module.ls", true).system_error, 0);
    EXPECT_EQ(read("module.ls", false, 1).system_error, EFBIG);
    EXPECT_EQ(read("https://remote/module.ls", true).decision, SECURITY_DENIED);
    EXPECT_EQ(write("../escape", "bad").decision, SECURITY_DENIED);
    EXPECT_EQ(write("nested/../escape", "bad").decision, SECURITY_DENIED);
    char* nested = file_path_join(project, "nested"); EXPECT_FALSE(file_exists(nested)); mem_free(nested);
}

TEST_F(SecurityFileBrokerTest, LooseEntryHasNoSiblingOrMutationAuthority) {
    seed("entry.ls", "42"); seed("other.ls", "43");
    SecurityFileRootSpec spec = {project, SECURITY_SCOPE_ENTRY,
        SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE, 0, 0, true, true, "entry.ls"};
    ASSERT_NE(security_file_broker_admit_root(broker, &spec), 0U);
    EXPECT_EQ(read("entry.ls", true).system_error, 0);
    EXPECT_EQ(read("other.ls", true).decision, SECURITY_DENIED);
    EXPECT_EQ(write("new/file", "bad").decision, SECURITY_DENIED);
    seed("entry.ls", "changed");
    EXPECT_NE(read("entry.ls", true).system_error, 0);
}

TEST_F(SecurityFileBrokerTest, SelectedDataFileDoesNotAdmitScriptsOrSiblings) {
    seed("selected", "data"); seed("other", "private");
    SecurityFileRootSpec spec = {project, SECURITY_SCOPE_SELECTED, SECURITY_READ,
        SECURITY_READ, 150, false, true, "selected"};
    ASSERT_NE(security_file_broker_admit_root(broker, &spec), 0U);
    EXPECT_EQ(read("selected").system_error, 0);
    EXPECT_EQ(read("selected", true).decision, SECURITY_DENIED);
    char* canonical = nullptr;
    EXPECT_EQ(client.canonical_source(client.context, "selected", &canonical).decision, SECURITY_DENIED);
    EXPECT_EQ(canonical, nullptr);
    mem_free(canonical);
    EXPECT_EQ(read("other").decision, SECURITY_DENIED);
    clock_value = 150;
    EXPECT_EQ(read("selected").decision, SECURITY_DENIED);
}

TEST_F(SecurityFileBrokerTest, ProtectedDirectoryIsDeniedThroughBroadParentRoot) {
    admit_project(); seed("protected/secret", "private");
    EXPECT_NE(read("protected/secret").system_error, 0);
    EXPECT_FALSE(write("protected/new/file", "bad").committed);
    expect_text("protected/secret", "private");
    SecurityFileRootSpec spec = {protected_path, SECURITY_SCOPE_PROJECT, SECURITY_ALL_OPERATIONS,
        0, 0, true, false, nullptr};
    EXPECT_EQ(security_file_broker_admit_root(broker, &spec), 0U);
    char* sub = file_path_join(protected_path, "sub"); ASSERT_EQ(mkdir(sub, 0700), 0);
    spec.path = sub;
    EXPECT_EQ(security_file_broker_admit_root(broker, &spec), 0U);
    mem_free(sub);
}

TEST_F(SecurityFileBrokerTest, CaseAliasCannotAdmitProtectedDescendant) {
    char* descendant = file_path_join(protected_path, "sub");
    ASSERT_EQ(mkdir(descendant, 0700), 0);
    char* alias = file_path_join(project, "PROTECTED/sub");
    if (!file_is_dir(alias)) { mem_free(descendant); mem_free(alias); GTEST_SKIP() << "case-sensitive filesystem"; }
    SecurityFileRootSpec spec = {alias, SECURITY_SCOPE_PROJECT, SECURITY_ALL_OPERATIONS,
        0, 0, true, true, nullptr};
    EXPECT_EQ(security_file_broker_admit_root(broker, &spec), 0U);
    mem_free(descendant); mem_free(alias);
}

TEST_F(SecurityFileBrokerTest, LinksAndUnpublishedStagingNamesNeverBecomeAuthority) {
    admit_project(); seed("protected/secret", "private"); seed("ordinary", "ordinary");
    char* link_path = file_path_join(project, "link");
    ASSERT_EQ(symlink("protected/secret", link_path), 0);
    EXPECT_NE(read("link").system_error, 0);
    EXPECT_FALSE(write("link", "bad").committed);
    ASSERT_EQ(unlink(link_path), 0);
    char* ordinary = file_path_join(project, "ordinary");
    ASSERT_EQ(link(ordinary, link_path), 0);
    EXPECT_NE(read("link").system_error, 0);
    EXPECT_NE(read("ordinary").system_error, 0);
    seed(".lambda-stage-visible", "unpublished");
    EXPECT_EQ(read(".lambda-stage-visible").decision, SECURITY_DENIED);
    EXPECT_EQ(read(".LAMBDA-STAGE-visible").decision, SECURITY_DENIED);
    EXPECT_EQ(read(".lambda-ſtage-visible").decision, SECURITY_DENIED);
    EXPECT_EQ(write("a/.lambda-stage-forged/file", "bad").decision, SECURITY_DENIED);
    expect_text("protected/secret", "private");
    mem_free(ordinary); mem_free(link_path);
}

TEST_F(SecurityFileBrokerTest, ProtectedCatalogSubstitutionFailsClosed) {
    admit_project(); seed("ordinary", "readable");
    char* moved = file_path_join(project, "old-protected");
    ASSERT_EQ(rename(protected_path, moved), 0);
    ASSERT_EQ(mkdir(protected_path, 0700), 0);
    EXPECT_NE(read("ordinary").system_error, 0);
    EXPECT_FALSE(write("new", "bad").committed);
    mem_free(moved);
}

TEST_F(SecurityFileBrokerTest, PendingWorkIsBoundedAndClosedSlotsRemainReusable) {
    admit_project(); seed("report", "old");
    SecurityApprovalRef refs[4];
    for (int i = 0; i < 4; i++) {
        SecurityIOResult result = write("report", "new");
        ASSERT_EQ(result.decision, SECURITY_APPROVAL_REQUIRED); refs[i] = result.approval;
    }
    EXPECT_EQ(write("report", "new").decision, SECURITY_LIMIT_EXCEEDED);
    for (SecurityApprovalRef ref : refs) ASSERT_TRUE(security_file_broker_cancel(broker, ref));
    for (int i = 0; i < 512; i++) {
        EXPECT_EQ(read("report").system_error, 0);
        SecurityIOResult result = write("report", "new");
        ASSERT_EQ(result.decision, SECURITY_APPROVAL_REQUIRED);
        ASSERT_TRUE(security_file_broker_cancel(broker, result.approval));
    }
    char large[4097] = {};
    EXPECT_EQ(client.write(client.context, "new", large, sizeof(large), false, true).decision, SECURITY_LIMIT_EXCEEDED);
    expect_text("report", "old");
}

TEST_F(SecurityFileBrokerTest, AggregateBudgetsCoverEmptyFilesAndApprovedWrites) {
    reset_broker(5, 3); admit_project();
    EXPECT_TRUE(write("one", "123").committed);
    EXPECT_EQ(write("too-large", "123").decision, SECURITY_LIMIT_EXCEEDED);
    EXPECT_TRUE(write("empty-one", "").committed);
    EXPECT_TRUE(write("empty-two", "").committed);
    EXPECT_EQ(write("empty-three", "").decision, SECURITY_LIMIT_EXCEEDED);
    SecurityIOResult pending = write("one", "xx");
    ASSERT_EQ(pending.decision, SECURITY_APPROVAL_REQUIRED);
    EXPECT_EQ(security_file_broker_approve(broker, pending.approval).decision, SECURITY_LIMIT_EXCEEDED);
    expect_text("one", "123");
}

TEST_F(SecurityFileBrokerTest, PrivateStorageSupportsRepeatedMutationWithPrivatePermissions) {
    ASSERT_EQ(chmod(project, 0700), 0);
    SecurityFileRootSpec spec = {project, SECURITY_SCOPE_PRIVATE, SECURITY_ALL_OPERATIONS,
        0, 0, false, true, nullptr};
    ASSERT_NE(security_file_broker_admit_root(broker, &spec), 0U);
    EXPECT_TRUE(write("nested/file", "one").committed);
    EXPECT_TRUE(write("nested/file", "two").committed);
    EXPECT_TRUE(write("nested/file", "more", true).committed);
    expect_text("nested/file", "twomore");
    EXPECT_EQ(read("nested/file", true).decision, SECURITY_DENIED);
}

TEST_F(SecurityFileBrokerTest, ProtectedObjectsCannotBeDowngradedByAnotherAdmission) {
    admit_project();
    EXPECT_FALSE(security_file_broker_protect(broker, project, SECURITY_ALL_OPERATIONS));
    SecurityFileRootSpec spec = {protected_path, SECURITY_SCOPE_SELECTED, SECURITY_READ,
        SECURITY_READ, 0, false, false, nullptr};
    EXPECT_EQ(security_file_broker_admit_root(broker, &spec), 0U);
    spec.path = project; spec.scope = -1;
    EXPECT_EQ(security_file_broker_admit_root(broker, &spec), 0U);
}
TEST_F(SecurityFileBrokerTest, DirectoryListingsHideProtectedAndAliasedObjects) {
    admit_project();
    seed("ordinary", "value");
    seed(".lambda-stage-secret", "staged");
    seed("hard-original", "secret");
    char* original = file_path_join(project, "hard-original");
    char* alias = file_path_join(project, "hard-alias");
    ASSERT_EQ(link(original, alias), 0);
    mem_free(original); mem_free(alias);
    char* linked = file_path_join(project, "link");
    ASSERT_EQ(symlink(protected_path, linked), 0); mem_free(linked);
    char* child = file_path_join(project, "child");
    ASSERT_TRUE(create_dir(child)); mem_free(child);
    for (int repeat = 0; repeat < 2; repeat++) {
        SecurityDirectory listing = {};
        ASSERT_TRUE(security_io_enumerate(&client, project, 32, &listing, nullptr));
        ASSERT_EQ(listing.count, 2U);
        for (uint32_t i = 0; i < listing.count; i++) {
            SecurityDirectoryEntry* entry = &listing.entries[i];
            EXPECT_TRUE(!strcmp(entry->name, "ordinary") || !strcmp(entry->name, "child"));
            EXPECT_TRUE(entry->has_metadata);
            EXPECT_EQ(entry->is_directory, !strcmp(entry->name, "child"));
        }
        security_io_directory_free(&listing);
    }
    SecurityDirectory limited = {};
    SecurityIOResult result = {};
    EXPECT_FALSE(security_io_enumerate(&client, ".", 1, &limited, &result));
    EXPECT_EQ(result.system_error, EFBIG);
    EXPECT_EQ(limited.count, 0U); EXPECT_EQ(limited.entries, nullptr);
}

TEST_F(SecurityFileBrokerTest, ReadOnlyClassificationDoesNotRequireOrGrantMetadata) {
    seed("data", "value");
    SecurityFileRootSpec spec = {project, SECURITY_SCOPE_SELECTED, SECURITY_READ,
        SECURITY_READ, 0, false, true, "data"};
    uint64_t root = security_file_broker_admit_root(broker, &spec);
    ASSERT_NE(root, 0U);
    int32_t kind = -1;
    ASSERT_TRUE(security_io_input_kind(&client, "data", &kind, nullptr));
    EXPECT_EQ(kind, SECURITY_OBJECT_FILE);
    EXPECT_EQ(read("data").system_error, 0);
    EXPECT_FALSE(security_io_input_kind(&client, project, &kind, nullptr));
    EXPECT_EQ(kind, -1);
    ASSERT_TRUE(security_file_broker_revoke_root(broker, root));
    EXPECT_FALSE(security_io_input_kind(&client, "data", &kind, nullptr));
}

TEST_F(SecurityFileBrokerTest, EnumerateOnlyGrantReturnsNamesWithoutMetadataOrBytes) {
    seed("data", "value");
    SecurityFileRootSpec spec = {project, SECURITY_SCOPE_SELECTED, SECURITY_ENUMERATE,
        SECURITY_ENUMERATE, 0, false, true, nullptr};
    ASSERT_NE(security_file_broker_admit_root(broker, &spec), 0U);
    int32_t kind;
    EXPECT_TRUE(security_io_input_kind(&client, project, &kind, nullptr));
    EXPECT_EQ(kind, SECURITY_OBJECT_DIRECTORY);
    SecurityDirectory listing = {};
    ASSERT_TRUE(security_io_enumerate(&client, project, 16, &listing, nullptr));
    ASSERT_EQ(listing.count, 1U);
    EXPECT_STREQ(listing.entries[0].name, "data");
    EXPECT_FALSE(listing.entries[0].has_metadata);
    EXPECT_FALSE(listing.entries[0].metadata.exists);
    security_io_directory_free(&listing);
    EXPECT_EQ(read("data").decision, SECURITY_DENIED);
    EXPECT_FALSE(security_io_input_kind(&client, "data", &kind, nullptr));
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_READ));
    EXPECT_FALSE(security_io_enumerate(&client, project, 16, &listing, nullptr));
    EXPECT_EQ(listing.entries, nullptr);
}
TEST_F(SecurityFileBrokerTest, MetadataAuthorityIsIndependentAndAbsenceIsAuthorized) {
    admit_project(); seed("data", "value");
    FileStat metadata = {};
    ASSERT_TRUE(security_io_stat(&client, "data", &metadata, nullptr));
    EXPECT_TRUE(metadata.is_file); EXPECT_EQ(metadata.size, 5);
    ASSERT_TRUE(security_io_stat(&client, "missing", &metadata, nullptr));
    EXPECT_FALSE(metadata.exists);
    EXPECT_FALSE(security_io_stat(&client, protected_path, &metadata, nullptr));
    EXPECT_FALSE(metadata.exists);
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_READ));
    EXPECT_FALSE(security_io_stat(&client, "data", &metadata, nullptr));
    EXPECT_FALSE(metadata.exists);
    EXPECT_EQ(read("data").system_error, 0);
}

TEST_F(SecurityFileBrokerTest, EntryAndSiblingGrantsSelectTheRequestedOperation) {
    seed("entry.ls", "42"); seed("data", "value"); seed("unselected", "secret");
    SecurityFileRootSpec entry = {project, SECURITY_SCOPE_ENTRY,
        SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE, 0, 0, true, true, "entry.ls"};
    ASSERT_NE(security_file_broker_admit_root(broker, &entry), 0U);
    SecurityFileRootSpec selected = {project, SECURITY_SCOPE_SELECTED, SECURITY_METADATA,
        SECURITY_METADATA, 105, false, false, "data"};
    ASSERT_NE(security_file_broker_admit_root(broker, &selected), 0U);
    FileStat metadata = {};
    EXPECT_TRUE(security_io_stat(&client, "data", &metadata, nullptr));
    EXPECT_EQ(metadata.size, 5);
    EXPECT_EQ(read("data").decision, SECURITY_DENIED);
    selected.ceiling = SECURITY_READ; selected.selected_operations = SECURITY_READ; selected.expires_at = 0;
    ASSERT_NE(security_file_broker_admit_root(broker, &selected), 0U);
    EXPECT_EQ(read("data").system_error, 0);
    char* absolute = file_path_join(project, "data");
    EXPECT_EQ(read(absolute).system_error, 0); mem_free(absolute);
    EXPECT_EQ(read("data", true).decision, SECURITY_DENIED);
    EXPECT_EQ(read("unselected").decision, SECURITY_DENIED);
    EXPECT_EQ(read("entry.ls", true).system_error, 0);
    clock_value = 106;
    EXPECT_FALSE(security_io_stat(&client, "data", &metadata, nullptr));
    EXPECT_EQ(read("data").system_error, 0);
}

TEST_F(SecurityFileBrokerTest, RelativeQueriesCanUseAnIndependentlySelectedAncestor) {
    char* sub = file_path_join(project, "sub"); ASSERT_TRUE(create_dir(sub));
    seed("sub/entry.ls", "42"); seed("sub/data", "value");
    SecurityFileRootSpec entry = {sub, SECURITY_SCOPE_ENTRY, SECURITY_READ | SECURITY_LOAD_SOURCE,
        0, 0, true, true, "entry.ls"};
    ASSERT_NE(security_file_broker_admit_root(broker, &entry), 0U);
    SecurityFileRootSpec selected = {project, SECURITY_SCOPE_SELECTED, SECURITY_METADATA,
        SECURITY_METADATA, 0, false, false, nullptr};
    ASSERT_NE(security_file_broker_admit_root(broker, &selected), 0U);
    FileStat metadata = {};
    ASSERT_TRUE(security_io_stat(&client, "data", &metadata, nullptr));
    EXPECT_TRUE(metadata.is_file); EXPECT_EQ(metadata.size, 5);
    EXPECT_EQ(read("data").decision, SECURITY_DENIED);
    mem_free(sub);
}

TEST_F(SecurityFileBrokerTest, ReservedChildNamesAreDeniedBeforeCreationAndThroughAliases) {
    ASSERT_TRUE(security_file_broker_protect_name(broker, project, ".ssh", SECURITY_ALL_OPERATIONS));
    admit_project();
    const char* paths[] = {".ssh/key", ".SSH/key", ".s\xC5\xBFh/key", ".s\xE2\x80\x8Bsh/key"};
    for (const char* path : paths) {
        SCOPED_TRACE(path);
        SecurityIOResult result = write(path, "never"); EXPECT_FALSE(result.committed);
        char* parent = file_path_join(project, path); char* directory_path = file_path_dirname(parent);
        EXPECT_FALSE(file_exists(directory_path)); mem_free(directory_path); mem_free(parent);
    }
    char* directory_path = file_path_join(project, ".ssh"); ASSERT_TRUE(create_dir(directory_path));
    char* key = file_path_join(directory_path, "key"); ASSERT_EQ(write_binary_file(key, "secret", 6), 0);
    EXPECT_EQ(read(".ssh/key").system_error, EACCES);
    SecurityFileRootSpec selected = {directory_path, SECURITY_SCOPE_SELECTED, SECURITY_ALL_OPERATIONS,
        SECURITY_ALL_OPERATIONS, 0, false, false, nullptr};
    EXPECT_EQ(security_file_broker_admit_root(broker, &selected), 0U);
    SecurityDirectory listing = {}; ASSERT_TRUE(security_io_enumerate(&client, project, 4096, &listing, nullptr));
    for (uint32_t i = 0; i < listing.count; i++) EXPECT_STRNE(listing.entries[i].name, ".ssh");
    security_io_directory_free(&listing); mem_free(key); mem_free(directory_path);
}

TEST_F(SecurityFileBrokerTest, TrustedSourceCaptureBindsNameBytesAndRevision) {
    seed("entry.ls", "42"); admit_project();
    char* entry = file_path_join(project, "entry.ls");
    SecuritySourceCapture capture = {};
    SecurityIOResult result = security_file_broker_capture_source(broker, entry, 4096, MEM_CAT_SYSTEM, &capture);
    EXPECT_EQ(result.decision, SECURITY_ALLOWED); EXPECT_EQ(result.system_error, 0);
    EXPECT_STREQ(capture.path, entry); ASSERT_EQ(capture.size, 2U); EXPECT_EQ(memcmp(capture.data, "42", 2), 0);
    FileRevision current = file_revision(entry); EXPECT_TRUE(file_revision_equal(&current, &capture.revision));
    security_source_capture_free(&capture); EXPECT_EQ(capture.path, nullptr); EXPECT_EQ(capture.data, nullptr);
    result = security_file_broker_capture_source(broker, entry, 1, MEM_CAT_SYSTEM, &capture);
    EXPECT_EQ(result.system_error, EFBIG); EXPECT_EQ(capture.path, nullptr); EXPECT_EQ(capture.data, nullptr);
    mem_free(entry);
}

TEST_F(SecurityFileBrokerTest, AuthorityStampsTrackGrantExpiryAndRejectClockRollback) {
    seed("entry.ls", "42"); seed("data", "value");
    SecurityFileRootSpec entry = {project, SECURITY_SCOPE_ENTRY,
        SECURITY_READ | SECURITY_LOAD_SOURCE, 0, 0, true, true, "entry.ls"};
    ASSERT_NE(security_file_broker_admit_root(broker, &entry), 0U);
    SecurityAuthorityStamp before = {}, selected_stamp = {}, after_read = {}, expired = {};
    ASSERT_TRUE(security_io_authority_stamp(&client, &before, nullptr));
    SecurityFileRootSpec selected = {project, SECURITY_SCOPE_SELECTED, SECURITY_READ,
        SECURITY_READ, 105, false, false, "data"};
    ASSERT_NE(security_file_broker_admit_root(broker, &selected), 0U);
    ASSERT_TRUE(security_io_authority_stamp(&client, &selected_stamp, nullptr));
    EXPECT_GT(selected_stamp.epoch, before.epoch);
    ASSERT_EQ(read("data").system_error, 0);
    ASSERT_TRUE(security_io_authority_stamp(&client, &after_read, nullptr));
    EXPECT_EQ(after_read.epoch, selected_stamp.epoch); // temporary acquisition grants are not policy changes
    clock_value = 106;
    ASSERT_TRUE(security_io_authority_stamp(&client, &expired, nullptr));
    EXPECT_GT(expired.epoch, selected_stamp.epoch);
    EXPECT_EQ(read("data").decision, SECURITY_DENIED);
    clock_value = 100;
    EXPECT_EQ(read("data").decision, SECURITY_DENIED);
    SecurityAuthorityStamp restricted = {};
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_LOAD_SOURCE));
    ASSERT_TRUE(security_io_authority_stamp(&client, &restricted, nullptr));
    EXPECT_GT(restricted.epoch, expired.epoch);
}
#endif
