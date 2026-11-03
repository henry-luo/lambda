/*
 * File Module Test Suite (GTest)
 * ===============================
 *
 * Comprehensive tests for the extended lib/file.h + lib/file_utils.h:
 *
 *  §1  Write operations       — write_binary_file, append_text_file,
 *                                append_binary_file, write_text_file_atomic
 *  §2  File operations        — file_copy, file_move, file_delete, file_touch,
 *                                file_symlink, file_chmod, file_rename
 *  §3  Queries & metadata     — file_exists, file_is_file, file_is_dir,
 *                                file_is_symlink, file_stat, file_size,
 *                                file_realpath
 *  §4  Streaming reads        — file_read_lines
 *  §5  Temp files             — file_temp_path, file_temp_create, dir_temp_create
 *  §6  Path utilities         — file_path_join, file_path_dirname,
 *                                file_path_basename, file_path_ext
 *  §7  Directory operations   — dir_list, dir_walk, dir_delete, dir_copy
 *  §8  Glob & find            — file_glob, file_find
 *  §9  Edge cases             — NULL args, non-existent paths, existing files
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <climits>
#include <errno.h>
#include <pthread.h>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/syscall.h>
#endif
#ifdef _WIN32
#include <windows.h>
static inline char* realpath(const char* path, char* resolved) {
    static char buf[MAX_PATH];
    char* out = resolved ? resolved : buf;
    if (GetFullPathNameA(path, MAX_PATH, out, NULL) == 0) return NULL;
    return out;
}
#endif

extern "C" {
#include "../../lib/file.h"
#include "../../lib/file_utils.h"
#include "../../lib/arraylist.h"
#include "../../lib/log.h"
#include "../../lib/path_str.h"
}

#ifndef _WIN32
#if defined(__APPLE__) || defined(__linux__)
struct AnchorSyncTrace;
static thread_local AnchorSyncTrace* anchor_sync_trace = nullptr;
struct AnchorSyncTrace {
    struct Event { uint64_t device, inode; bool directory; } events[32] = {};
    uint32_t calls = 0;
    uint32_t fail_at;
    uint32_t close_at = 0;
    explicit AnchorSyncTrace(uint32_t fail = 0) : fail_at(fail) { anchor_sync_trace = this; }
    ~AnchorSyncTrace() { anchor_sync_trace = nullptr; }
    AnchorSyncTrace(const AnchorSyncTrace&) = delete;
    AnchorSyncTrace& operator=(const AnchorSyncTrace&) = delete;
};

// test-executable interposition only: disabled calls still reach the real
// kernel; enabled cases can fail one sync without a production test hook
extern "C" int fsync(int fd) {
    AnchorSyncTrace* trace = anchor_sync_trace;
    if (trace) {
        struct stat st;
        if (fstat(fd, &st)) return -1;
        uint32_t call = trace->calls++;
        if (call < 32) trace->events[call] = {(uint64_t)st.st_dev, (uint64_t)st.st_ino, S_ISDIR(st.st_mode)};
        if (trace->calls == trace->fail_at) { errno = EIO; return -1; }
    }
    int result = (int)syscall(SYS_fsync, fd);
    if (!result && trace && trace->calls == trace->close_at) close(fd);
    return result;
}
#endif

class FileAnchorTest : public ::testing::Test {
protected:
    char* fixture = nullptr;
    char* root = nullptr;
    char* project = nullptr;
    FileAnchor* handles[16] = {};
    size_t handle_count = 0;

    void SetUp() override {
        log_init(nullptr);
        fixture = dir_temp_create("file_anchor");
        ASSERT_NE(fixture, nullptr);
        root = file_realpath(fixture);
        ASSERT_NE(root, nullptr);
        project = file_path_join(root, "project");
        ASSERT_NE(project, nullptr);
        ASSERT_EQ(mkdir(project, 0700), 0);
    }

    void TearDown() override {
        for (size_t i = 0; i < handle_count; i++) file_anchor_close(handles[i]);
        if (fixture) dir_delete(fixture);
        mem_free(project); mem_free(root); mem_free(fixture);
    }

    FileAnchor* keep(FileAnchor* anchor) {
        if (anchor && handle_count < 16) handles[handle_count++] = anchor;
        return anchor;
    }

    FileAnchor* open(uint32_t flags = FILE_ANCHOR_SAME_DEVICE,
            FileAnchorGuard guard = nullptr, void* context = nullptr) {
        FileAnchorOptions options = {flags, guard, context};
        return keep(file_anchor_open(project, &options));
    }

    void seed(const char* relative, const char* content) {
        char* path = file_path_join(root, relative);
        ASSERT_NE(path, nullptr);
        EXPECT_EQ(write_binary_file(path, content, strlen(content)), 0);
        mem_free(path);
    }

    void expect_text(FileAnchor* anchor, const char* relative, const char* expected) {
        char* data = nullptr;
        size_t size = 0;
        ASSERT_TRUE(file_anchor_read_all(anchor, relative, 1024, MEM_CAT_TEMP, &data, &size, nullptr));
        EXPECT_EQ(size, strlen(expected));
        EXPECT_STREQ(data, expected);
        mem_free(data);
    }
};

TEST_F(FileAnchorTest, ReadCreateAndRevisionUseHeldObjects) {
    FileAnchor* anchor = open();
    ASSERT_NE(anchor, nullptr);
    FILE* created = file_anchor_create_regular(anchor, "new");
    ASSERT_NE(created, nullptr);
    EXPECT_NE(fcntl(fileno(created), F_GETFD) & FD_CLOEXEC, 0);
    ASSERT_EQ(fwrite("created", 1, 7, created), 7U);
    ASSERT_EQ(fclose(created), 0);
    EXPECT_EQ(file_anchor_create_regular(anchor, "new"), nullptr);
    EXPECT_EQ(errno, EEXIST);
    FileRevision revision;
    ASSERT_TRUE(file_anchor_stat(anchor, "new", &revision));
    EXPECT_TRUE(revision.stat.is_file);
    EXPECT_NE(revision.inode, 0U);
    FILE* reader = file_anchor_open_read(anchor, "new");
    ASSERT_NE(reader, nullptr);
    FileRevision held = file_revision_stream(reader);
    EXPECT_TRUE(file_revision_equal(&revision, &held));
    fclose(reader);
    expect_text(anchor, "new", "created");
    char* data = (char*)1;
    size_t size = 99;
    EXPECT_FALSE(file_anchor_read_all(anchor, "new", 2, MEM_CAT_TEMP, &data, &size, nullptr));
    EXPECT_EQ(errno, EFBIG);
    EXPECT_EQ(data, nullptr);
    EXPECT_EQ(size, 0U);
}

TEST_F(FileAnchorTest, ExclusiveDirectoryCreationNeverAdoptsAnExistingNamespace) {
    const uint32_t flags = FILE_ANCHOR_PRIVATE | FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_SAME_DEVICE;
    FileAnchor* anchor = open(flags);
    ASSERT_NE(anchor, nullptr);
    FileAnchor* child = keep(file_anchor_create_directory(anchor, "owned", FILE_ANCHOR_PRIVATE |
        FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_SAME_DEVICE)); ASSERT_NE(child, nullptr);
    EXPECT_EQ(file_anchor_create_directory(anchor, "owned", flags), nullptr); EXPECT_EQ(errno, EEXIST);
    EXPECT_EQ(file_anchor_create_directory(anchor, "nested/child", flags), nullptr); EXPECT_EQ(errno, EINVAL);
    FileRevision value; EXPECT_FALSE(file_anchor_stat(anchor, "nested", &value));
    char* alias = file_path_join(project, "alias"); ASSERT_EQ(symlink("owned", alias), 0);
    EXPECT_EQ(file_anchor_create_directory(anchor, "alias", flags), nullptr); EXPECT_EQ(errno, EEXIST);
    mem_free(alias);
}

#if defined(__APPLE__) || defined(__linux__)
TEST_F(FileAnchorTest, SyncControlObservesAtomicPublicationAndRealKernelErrors) {
    FileAnchor* anchor = open(); ASSERT_NE(anchor, nullptr);
    {
        AnchorSyncTrace trace;
        ASSERT_EQ(file_anchor_write_atomic(anchor, "control", "data", 4, nullptr), 0);
        ASSERT_EQ(trace.calls, 2U); EXPECT_FALSE(trace.events[0].directory); EXPECT_TRUE(trace.events[1].directory);
        EXPECT_EQ(trace.events[1].inode, file_anchor_identity(anchor).inode);
    }
    {
        AnchorSyncTrace trace(1);
        EXPECT_EQ(file_anchor_write_atomic(anchor, "failed", "data", 4, nullptr), -1); EXPECT_EQ(errno, EIO);
    }
    FileRevision revision; EXPECT_FALSE(file_anchor_stat(anchor, "failed", &revision)); EXPECT_EQ(errno, ENOENT);
    AnchorSyncTrace invalid;
    EXPECT_EQ(fsync(-1), -1); EXPECT_EQ(errno, EBADF);
}

TEST_F(FileAnchorTest, DirectoryCreationSyncsChildBeforeItsParent) {
    FileAnchor* anchor = open(); ASSERT_NE(anchor, nullptr);
    AnchorSyncTrace trace;
    FileAnchor* child = keep(file_anchor_create_directory(anchor, "durable", FILE_ANCHOR_SAME_DEVICE));
    ASSERT_NE(child, nullptr); ASSERT_EQ(trace.calls, 2U);
    EXPECT_TRUE(trace.events[0].directory); EXPECT_TRUE(trace.events[1].directory);
    EXPECT_EQ(trace.events[0].inode, file_anchor_identity(child).inode);
    EXPECT_EQ(trace.events[1].inode, file_anchor_identity(anchor).inode);
}

TEST_F(FileAnchorTest, NestedDirectoryCreationSyncsEveryNewEdge) {
    FileAnchor* anchor = open(); ASSERT_NE(anchor, nullptr);
    AnchorSyncTrace trace;
    FileAnchor* leaf = keep(file_anchor_directory(anchor, "one/two/three", true, FILE_ANCHOR_SAME_DEVICE));
    ASSERT_NE(leaf, nullptr); ASSERT_EQ(trace.calls, 6U);
    FileAnchor* first = keep(file_anchor_directory(anchor, "one", false, FILE_ANCHOR_SAME_DEVICE)); ASSERT_NE(first, nullptr);
    FileAnchor* second = keep(file_anchor_directory(first, "two", false, FILE_ANCHOR_SAME_DEVICE)); ASSERT_NE(second, nullptr);
    FileAnchor* children[] = {first, second, leaf}; FileAnchor* parents[] = {anchor, first, second};
    for (unsigned i = 0; i < 3; i++) {
        EXPECT_EQ(trace.events[i * 2].inode, file_anchor_identity(children[i]).inode);
        EXPECT_EQ(trace.events[i * 2 + 1].inode, file_anchor_identity(parents[i]).inode);
    }
    EXPECT_EQ(trace.calls, 6U);
}

TEST_F(FileAnchorTest, CreateModeResyncsExistingDirectoriesButReadOnlyOpenDoesNot) {
    FileAnchor* anchor = open(); ASSERT_NE(anchor, nullptr);
    FileAnchor* child = keep(file_anchor_create_directory(anchor, "retained", FILE_ANCHOR_SAME_DEVICE)); ASSERT_NE(child, nullptr);
    AnchorSyncTrace trace;
    ASSERT_NE(keep(file_anchor_directory(anchor, "retained", false, FILE_ANCHOR_SAME_DEVICE)), nullptr);
    EXPECT_EQ(trace.calls, 0U);
    ASSERT_NE(keep(file_anchor_directory(anchor, "retained", true, FILE_ANCHOR_SAME_DEVICE)), nullptr);
    ASSERT_EQ(trace.calls, 2U);
    EXPECT_EQ(trace.events[0].inode, file_anchor_identity(child).inode);
    EXPECT_EQ(trace.events[1].inode, file_anchor_identity(anchor).inode);
}

TEST_F(FileAnchorTest, DirectorySyncFailureNeverPublishesAnAnchorOrCreatesDeeperChildren) {
    FileAnchor* anchor = open(); ASSERT_NE(anchor, nullptr);
    for (unsigned fail = 1; fail <= 2; fail++) {
        const char* prefix = fail == 1 ? "first" : "second";
        const char* nested = fail == 1 ? "first/child" : "second/child";
        {
            AnchorSyncTrace trace(fail);
            EXPECT_EQ(file_anchor_directory(anchor, nested, true, FILE_ANCHOR_SAME_DEVICE), nullptr);
            EXPECT_EQ(errno, EIO); EXPECT_EQ(trace.calls, fail);
        }
        FileRevision revision; ASSERT_TRUE(file_anchor_stat(anchor, prefix, &revision)); EXPECT_TRUE(revision.stat.is_dir);
        EXPECT_FALSE(file_anchor_stat(anchor, nested, &revision)); EXPECT_EQ(errno, ENOENT);
        // failure may leave the newly created object: never replace or erase it
        EXPECT_EQ(file_anchor_create_directory(anchor, prefix, FILE_ANCHOR_SAME_DEVICE), nullptr); EXPECT_EQ(errno, EEXIST);
        ASSERT_NE(keep(file_anchor_directory(anchor, nested, true, FILE_ANCHOR_SAME_DEVICE)), nullptr);
    }
}

TEST_F(FileAnchorTest, ExclusiveDirectorySyncFailureLeavesItsUnacknowledgedObject) {
    FileAnchor* anchor = open(); ASSERT_NE(anchor, nullptr);
    for (unsigned fail = 1; fail <= 2; fail++) {
        const char* name = fail == 1 ? "first" : "second";
        AnchorSyncTrace trace(fail);
        EXPECT_EQ(file_anchor_create_directory(anchor, name, FILE_ANCHOR_SAME_DEVICE), nullptr);
        EXPECT_EQ(errno, EIO); EXPECT_EQ(trace.calls, fail);
        FileRevision revision; ASSERT_TRUE(file_anchor_stat(anchor, name, &revision)); EXPECT_TRUE(revision.stat.is_dir);
    }
}

TEST_F(FileAnchorTest, NewRegularFileSyncsBeforeReturningAStream) {
    FileAnchor* anchor = open(); ASSERT_NE(anchor, nullptr);
    AnchorSyncTrace trace;
    FILE* stream = file_anchor_create_regular(anchor, "lock"); ASSERT_NE(stream, nullptr);
    FileRevision identity = file_revision_stream(stream); fclose(stream);
    ASSERT_EQ(trace.calls, 2U); EXPECT_FALSE(trace.events[0].directory); EXPECT_TRUE(trace.events[1].directory);
    EXPECT_EQ(trace.events[0].inode, identity.inode); EXPECT_EQ(trace.events[1].inode, file_anchor_identity(anchor).inode);
    stream = file_anchor_open_read(anchor, "lock"); ASSERT_NE(stream, nullptr); fclose(stream); EXPECT_EQ(trace.calls, 2U);
}

TEST_F(FileAnchorTest, NewRegularFileSyncFailureNeverPublishesAStreamOrRetriesCreation) {
    FileAnchor* anchor = open(); ASSERT_NE(anchor, nullptr);
    for (unsigned fail = 1; fail <= 2; fail++) {
        const char* name = fail == 1 ? "first" : "second";
        AnchorSyncTrace trace(fail);
        EXPECT_EQ(file_anchor_create_regular(anchor, name), nullptr); EXPECT_EQ(errno, EIO); EXPECT_EQ(trace.calls, fail);
        FileRevision revision; ASSERT_TRUE(file_anchor_stat(anchor, name, &revision)); EXPECT_TRUE(revision.stat.is_file);
        EXPECT_EQ(file_anchor_create_regular(anchor, name), nullptr); EXPECT_EQ(errno, EEXIST);
    }
}
#ifdef __APPLE__
TEST_F(FileAnchorTest, FullSyncFailureAfterPublicationCannotBeReportedAsDurable) {
    FileAnchor* anchor = open(FILE_ANCHOR_STRICT_STORAGE); ASSERT_NE(anchor, nullptr);
    {
        // Force the later F_FULLFSYNC to fail after fsync itself succeeds.
        // The parent here is a temporary duplicate, not the held root anchor.
        AnchorSyncTrace trace; trace.close_at = 2;
        EXPECT_EQ(file_anchor_write_atomic(anchor, "committed", "data", 4, nullptr), 1);
        EXPECT_EQ(errno, EBADF); EXPECT_EQ(trace.calls, 2U);
    }
    expect_text(anchor, "committed", "data");
}

TEST_F(FileAnchorTest, FullSyncCannotBeDroppedWhenDerivingAChildAnchor) {
    FileAnchor* anchor = open(FILE_ANCHOR_STRICT_STORAGE); ASSERT_NE(anchor, nullptr);
    EXPECT_EQ(file_anchor_create_directory(anchor, "weaker", FILE_ANCHOR_STRICT_STORAGE & ~FILE_ANCHOR_FULL_SYNC), nullptr);
    EXPECT_EQ(errno, EINVAL);
    FileRevision revision; EXPECT_FALSE(file_anchor_stat(anchor, "weaker", &revision)); EXPECT_EQ(errno, ENOENT);
    ASSERT_NE(keep(file_anchor_create_directory(anchor, "strong", FILE_ANCHOR_STRICT_STORAGE)), nullptr);
}
#endif
#endif

TEST_F(FileAnchorTest, CleanupRequiresTheRecordedFileAndEmptyDirectory) {
    FileAnchor* anchor = open(); ASSERT_NE(anchor, nullptr);
    FileAnchor* child = keep(file_anchor_create_directory(anchor, "owned", FILE_ANCHOR_SAME_DEVICE));
    ASSERT_NE(child, nullptr); FileRevision directory = file_anchor_identity(child);
    ASSERT_EQ(file_anchor_write_atomic(child, "data", "first", 5, nullptr), 0);
    FileRevision first; ASSERT_TRUE(file_anchor_stat(child, "data", &first));
    EXPECT_FALSE(file_anchor_remove(anchor, "owned", &directory)); EXPECT_EQ(errno, ENOTEMPTY);
    ASSERT_EQ(file_anchor_write_atomic(child, "data", "second", 6, &first), 0);
    EXPECT_FALSE(file_anchor_remove(child, "data", &first)); EXPECT_EQ(errno, ESTALE);
    expect_text(child, "data", "second");
    FileRevision current; ASSERT_TRUE(file_anchor_stat(child, "data", &current));
    ASSERT_TRUE(file_anchor_remove(child, "data", &current));
    EXPECT_TRUE(file_anchor_remove(anchor, "owned", &directory));
}

TEST_F(FileAnchorTest, CleanupLeavesCallbackTimeReplacementsUntouched) {
    struct Switch { const char* path; bool replaced; };
    char* path = file_path_join(project, "data");
    Switch change = {path, false};
    auto guard = [](void* context, const char*, FileAnchorAction action, const FileRevision*) -> bool {
        Switch* state = (Switch*)context;
        if (action == FILE_ANCHOR_REMOVE && !state->replaced) {
            state->replaced = true;
            return write_binary_file_atomic(state->path, "new owner", 9) == 0;
        }
        return true;
    };
    FileAnchor* anchor = open(FILE_ANCHOR_SAME_DEVICE, guard, &change); ASSERT_NE(anchor, nullptr);
    ASSERT_EQ(file_anchor_write_atomic(anchor, "data", "old", 3, nullptr), 0);
    FileRevision revision; ASSERT_TRUE(file_anchor_stat(anchor, "data", &revision));
    EXPECT_FALSE(file_anchor_remove(anchor, "data", &revision)); EXPECT_EQ(errno, ESTALE);
    EXPECT_TRUE(change.replaced); expect_text(anchor, "data", "new owner"); mem_free(path);
}

TEST_F(FileAnchorTest, CleanupNeedsRemoveAuthorityAndStrictEnumerationRejectsHiddenAliases) {
    auto guard = [](void*, const char*, FileAnchorAction action, const FileRevision*) -> bool {
        return action != FILE_ANCHOR_REMOVE;
    };
    FileAnchor* anchor = open(FILE_ANCHOR_SAME_DEVICE, guard, nullptr); ASSERT_NE(anchor, nullptr);
    ASSERT_EQ(file_anchor_write_atomic(anchor, "kept", "old", 3, nullptr), 0);
    FileRevision revision; ASSERT_TRUE(file_anchor_stat(anchor, "kept", &revision));
    EXPECT_FALSE(file_anchor_remove(anchor, "kept", &revision)); EXPECT_EQ(errno, EACCES);
    expect_text(anchor, "kept", "old");
    char* alias = file_path_join(project, "alias"); ASSERT_EQ(symlink("kept", alias), 0);
    auto visit = [](void*, const char*, const FileRevision*) -> bool { return true; };
    EXPECT_TRUE(file_anchor_visit(anchor, 8, visit, nullptr));
    EXPECT_FALSE(file_anchor_visit_strict(anchor, 8, visit, nullptr)); EXPECT_EQ(errno, EACCES); mem_free(alias);
}

TEST_F(FileAnchorTest, BoundedEnumerationRevalidatesBeforeDelivery) {
    seed("project/one", "1"); seed("project/two", "2");
    FileAnchor* anchor = open(); ASSERT_NE(anchor, nullptr);
    auto count_entry = [](void* context, const char*, const FileRevision* revision) -> bool {
        EXPECT_TRUE(revision->stat.is_file);
        (*(unsigned*)context)++;
        return true;
    };
    unsigned count = 0;
    ASSERT_TRUE(file_anchor_visit(anchor, 8, count_entry, &count)); EXPECT_EQ(count, 2U);
    count = 0;
    ASSERT_TRUE(file_anchor_visit(anchor, 8, count_entry, &count)); EXPECT_EQ(count, 2U);
    EXPECT_FALSE(file_anchor_visit(anchor, 1, count_entry, &count)); EXPECT_EQ(errno, EFBIG);
    auto mutate_entry = [](void* context, const char*, const FileRevision*) -> bool {
        char* path = file_path_join((const char*)context, "created-during-listing");
        bool ok = path && write_binary_file(path, "x", 1) == 0;
        mem_free(path); return ok;
    };
    EXPECT_FALSE(file_anchor_visit(anchor, 8, mutate_entry, project)); EXPECT_EQ(errno, ESTALE);
}

TEST_F(FileAnchorTest, InvalidRequestsCannotCreatePartialDirectories) {
    FileAnchor* anchor = open();
    ASSERT_NE(anchor, nullptr);
    const char* paths[] = {"", "/outside", "../outside", "part/../outside", "part/./file", "part//file", "part/"};
    for (const char* path : paths) {
        EXPECT_EQ(file_anchor_create_regular(anchor, path), nullptr) << path;
        EXPECT_EQ(file_anchor_directory(anchor, path, true, FILE_ANCHOR_SAME_DEVICE), nullptr) << path;
    }
    char* path = file_path_join(project, "part");
    EXPECT_FALSE(file_exists(path));
    mem_free(path);
    FileAnchorOptions invalid = {UINT32_MAX, nullptr, nullptr};
    EXPECT_EQ(file_anchor_open(project, &invalid), nullptr);
    EXPECT_FALSE(file_anchor_validate(nullptr));
}

TEST_F(FileAnchorTest, IntermediateFinalAndHardLinkAliasesAreRejected) {
    char* private_dir = file_path_join(root, "private");
    ASSERT_EQ(mkdir(private_dir, 0700), 0);
    seed("private/secret", "secret");
    char* escape = file_path_join(project, "escape");
    ASSERT_EQ(symlink("../private", escape), 0);
    char* alias = file_path_join(project, "alias");
    ASSERT_EQ(symlink("../private/secret", alias), 0);
    char* hard = file_path_join(project, "hard");
    char* secret = file_path_join(private_dir, "secret");
    ASSERT_EQ(link(secret, hard), 0);
    FileAnchor* anchor = open();
    ASSERT_NE(anchor, nullptr);
    EXPECT_EQ(file_anchor_open_read(anchor, "escape/secret"), nullptr);
    EXPECT_EQ(file_anchor_create_regular(anchor, "escape/new"), nullptr);
    EXPECT_EQ(file_anchor_open_read(anchor, "alias"), nullptr);
    EXPECT_EQ(file_anchor_open_read(anchor, "hard"), nullptr);
    FileRevision revision;
    EXPECT_FALSE(file_anchor_stat(anchor, "alias", &revision));
    EXPECT_FALSE(file_anchor_stat(anchor, "hard", &revision));
    EXPECT_EQ(file_anchor_write_atomic(anchor, "alias", "wrong", 5, nullptr), -1);
    EXPECT_TRUE(file_is_symlink(alias));
    mem_free(secret); mem_free(hard); mem_free(alias); mem_free(escape); mem_free(private_dir);
}

TEST_F(FileAnchorTest, ReplacedRootAndAncestorSymlinksCannotRedirectAccess) {
    seed("project/old", "original");
    FileAnchor* anchor = open();
    ASSERT_NE(anchor, nullptr);
    char* moved = file_path_join(root, "moved");
    ASSERT_EQ(rename(project, moved), 0);
    ASSERT_EQ(mkdir(project, 0700), 0);
    seed("project/old", "replacement");
    EXPECT_FALSE(file_anchor_validate(anchor));
    EXPECT_EQ(file_anchor_open_read(anchor, "old"), nullptr);
    EXPECT_EQ(file_anchor_write_atomic(anchor, "new", "x", 1, nullptr), -1);
    char* fresh = file_path_join(moved, "new");
    EXPECT_FALSE(file_exists(fresh));
    char* link_path = file_path_join(root, "parent-link");
    ASSERT_EQ(symlink("moved", link_path), 0);
    char* child = file_path_join(link_path, "child");
    char* actual_child = file_path_join(moved, "child");
    ASSERT_EQ(mkdir(actual_child, 0700), 0);
    EXPECT_EQ(file_anchor_open(child, nullptr), nullptr);
    mem_free(actual_child); mem_free(child); mem_free(link_path); mem_free(fresh); mem_free(moved);
}

TEST_F(FileAnchorTest, PrivatePermissionsAndUnsafeAncestorsFailClosed) {
    FileAnchor* anchor = open(FILE_ANCHOR_PRIVATE | FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_SAME_DEVICE);
    ASSERT_NE(anchor, nullptr);
    seed("project/public", "private bytes");
    char* public_file = file_path_join(project, "public");
    ASSERT_EQ(chmod(public_file, 0644), 0);
    EXPECT_EQ(file_anchor_open_read(anchor, "public"), nullptr);
    ASSERT_EQ(chmod(public_file, 0600), 0);
    expect_text(anchor, "public", "private bytes");
    char* shared = file_path_join(project, "shared");
    ASSERT_EQ(mkdir(shared, 0755), 0);
    EXPECT_EQ(file_anchor_directory(anchor, "shared/child", true,
        FILE_ANCHOR_PRIVATE | FILE_ANCHOR_TRUSTED_PARENTS | FILE_ANCHOR_SAME_DEVICE), nullptr);
    char* child = file_path_join(shared, "child");
    EXPECT_FALSE(file_exists(child));
    ASSERT_EQ(chmod(project, 0777), 0);
    EXPECT_FALSE(file_anchor_validate(anchor));
    EXPECT_EQ(open(FILE_ANCHOR_TRUSTED_PARENTS), nullptr);
    ASSERT_EQ(chmod(project, 0700), 0);
    mem_free(child); mem_free(shared); mem_free(public_file);
}

struct AnchorGuardState {
    uint64_t protected_device;
    uint64_t protected_inode;
    char read_path[PATH_MAX];
    const char* external_change;
    bool revoke_after_dispatch;
    unsigned create_checks;
    unsigned read_checks;
};

static bool protect_anchor_object(void* context, const char* relative,
        FileAnchorAction action, const FileRevision* object) {
    AnchorGuardState* state = (AnchorGuardState*)context;
    if (state->revoke_after_dispatch &&
        ((action == FILE_ANCHOR_CREATE && ++state->create_checks > 1) ||
         (action == FILE_ANCHOR_READ && ++state->read_checks > 1))) return false;
    if (object && object->device == state->protected_device && object->inode == state->protected_inode)
        return false;
    if (action == FILE_ANCHOR_CREATE && !strcmp(relative, "denied")) return false;
    if (action == FILE_ANCHOR_READ) path_str_copy(state->read_path, sizeof(state->read_path), relative);
    if (action == FILE_ANCHOR_REPLACE && state->external_change)
        return write_binary_file(state->external_change, "external changed bytes", 22) == 0;
    return true;
}

TEST_F(FileAnchorTest, GuardChecksEveryDirectoryAndKeepsChildScope) {
    AnchorGuardState guard = {};
    FileAnchor* anchor = open(FILE_ANCHOR_SAME_DEVICE, protect_anchor_object, &guard);
    ASSERT_NE(anchor, nullptr);
    FileAnchor* child = keep(file_anchor_directory(anchor, "safe/deep", true, FILE_ANCHOR_SAME_DEVICE));
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(file_anchor_write_atomic(child, "file", "ok", 2, nullptr), 0);
    expect_text(child, "file", "ok");
    EXPECT_STREQ(guard.read_path, "safe/deep/file");
    EXPECT_EQ(file_anchor_directory(anchor, "denied/child", true, FILE_ANCHOR_SAME_DEVICE), nullptr);
    char* denied = file_path_join(project, "denied");
    EXPECT_FALSE(file_exists(denied));
    char* safe = file_path_join(project, "safe");
    FileRevision protected_dir = file_revision(safe);
    guard.protected_device = protected_dir.device;
    guard.protected_inode = protected_dir.inode;
    EXPECT_EQ(file_anchor_open_read(anchor, "safe/deep/file"), nullptr);
    char* renamed = file_path_join(project, "renamed");
    ASSERT_EQ(rename(safe, renamed), 0);
    EXPECT_EQ(file_anchor_open_read(anchor, "renamed/deep/file"), nullptr);
    mem_free(renamed); mem_free(safe); mem_free(denied);
}

TEST_F(FileAnchorTest, AtomicPublicationRequiresUnchangedAdmittedRevision) {
    AnchorGuardState guard = {};
    FileAnchor* anchor = open(FILE_ANCHOR_SAME_DEVICE, protect_anchor_object, &guard);
    ASSERT_NE(anchor, nullptr);
    ASSERT_EQ(file_anchor_write_atomic(anchor, "file", "before", 6, nullptr), 0);
    EXPECT_EQ(file_anchor_write_atomic(anchor, "file", "wrong", 5, nullptr), -1);
    EXPECT_EQ(errno, EEXIST);
    FileRevision version;
    ASSERT_TRUE(file_anchor_stat(anchor, "file", &version));
    ASSERT_EQ(file_anchor_write_atomic(anchor, "file", "after", 5, &version), 0);
    expect_text(anchor, "file", "after");
    EXPECT_EQ(file_anchor_write_atomic(anchor, "file", "stale", 5, &version), -1);
    EXPECT_EQ(errno, ESTALE);
    ASSERT_TRUE(file_anchor_stat(anchor, "file", &version));
    char* target = file_path_join(project, "file");
    guard.external_change = target;
    EXPECT_EQ(file_anchor_write_atomic(anchor, "file", "unapproved", 10, &version), -1);
    EXPECT_EQ(errno, ESTALE);
    guard.external_change = nullptr;
    expect_text(anchor, "file", "external changed bytes");
    mem_free(target);
}

TEST_F(FileAnchorTest, SpecialFilesAreNeverOpenedAsContent) {
    char* fifo = file_path_join(project, "fifo");
    ASSERT_EQ(mkfifo(fifo, 0600), 0);
    FileAnchor* anchor = open();
    ASSERT_NE(anchor, nullptr);
    EXPECT_EQ(file_anchor_open_read(anchor, "fifo"), nullptr);
    FileRevision revision;
    EXPECT_FALSE(file_anchor_stat(anchor, "fifo", &revision));
    EXPECT_EQ(file_anchor_open_read(anchor, "missing"), nullptr);
    EXPECT_EQ(errno, ENOENT);
    mem_free(fifo);
}

TEST_F(FileAnchorTest, RevocationBeforePublicationOrReadDeliveryDiscardsResults) {
    AnchorGuardState guard = {};
    FileAnchor* anchor = open(FILE_ANCHOR_SAME_DEVICE, protect_anchor_object, &guard);
    ASSERT_NE(anchor, nullptr);
    seed("project/readable", "private bytes");
    guard.revoke_after_dispatch = true;
    EXPECT_EQ(file_anchor_write_atomic(anchor, "new", "private bytes", 13, nullptr), -1);
    EXPECT_EQ(errno, EACCES);
    char* path = file_path_join(project, "new");
    EXPECT_FALSE(file_exists(path));
    char* bytes = nullptr;
    EXPECT_FALSE(file_anchor_read_all(anchor, "readable", 1024, MEM_CAT_TEMP, &bytes, nullptr, nullptr));
    EXPECT_EQ(errno, EACCES);
    EXPECT_EQ(bytes, nullptr);
    mem_free(path);
}
#endif

// Test directory root — all tests write under this
static const char* TEST_DIR = "temp/test_file_module";

// Helper: create test directory tree
static void setup_test_dir() {
    create_dir(TEST_DIR);
}

// Helper: remove test directory tree
static void cleanup_test_dir() {
    dir_delete(TEST_DIR);
}

// Helper: build path under test dir
static std::string test_path(const char* relative) {
    return std::string(TEST_DIR) + "/" + relative;
}

/* ================================================================== *
 *  §1  Write Operations                                              *
 * ================================================================== */

class FileWriteTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
        setup_test_dir();
    }
    void TearDown() override {
        cleanup_test_dir();
    }
};

TEST_F(FileWriteTest, WriteBinaryFile) {
    std::string path = test_path("binary.dat");
    const char data[] = {0x00, 0x01, 0x02, (char)0xFF, (char)0xFE};
    ASSERT_EQ(write_binary_file(path.c_str(), data, sizeof(data)), 0);

    size_t out_size = 0;
    char* buf = read_binary_file(path.c_str(), &out_size);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(out_size, sizeof(data));
    EXPECT_EQ(memcmp(buf, data, sizeof(data)), 0);
    free(buf);
}

TEST_F(FileWriteTest, WriteBinaryNull) {
    EXPECT_EQ(write_binary_file(NULL, "data", 4), -1);
    EXPECT_EQ(write_binary_file("test.bin", NULL, 4), -1);
}

TEST_F(FileWriteTest, ReadAllPreservesBinaryData) {
    const char* path = "temp/test_file_module/read_all.bin";
    const char payload[] = {'a', '\0', 'b', (char)0xFF};
    ASSERT_EQ(write_binary_file(path, payload, sizeof(payload)), 0);

    char* data = NULL;
    size_t size = 0;
    ASSERT_TRUE(file_read_all(path, MEM_CAT_NETWORK, &data, &size));
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(size, sizeof(payload));
    EXPECT_EQ(memcmp(data, payload, sizeof(payload)), 0);

    mem_free(data);
}

TEST_F(FileWriteTest, AppendTextFile) {
    std::string path = test_path("append.txt");
    write_text_file(path.c_str(), "hello");
    ASSERT_EQ(append_text_file(path.c_str(), " world"), 0);

    char* content = read_text_file(path.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "hello world");
    free(content);
}

TEST_F(FileWriteTest, AppendToNewFile) {
    std::string path = test_path("append_new.txt");
    ASSERT_EQ(append_text_file(path.c_str(), "first"), 0);

    char* content = read_text_file(path.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "first");
    free(content);
}

TEST_F(FileWriteTest, AppendTextNull) {
    EXPECT_EQ(append_text_file(NULL, "data"), -1);
    EXPECT_EQ(append_text_file("test.txt", NULL), -1);
}

TEST_F(FileWriteTest, AppendBinaryFile) {
    std::string path = test_path("append_bin.dat");
    const char d1[] = {0x01, 0x02};
    const char d2[] = {0x03, 0x04};
    ASSERT_EQ(write_binary_file(path.c_str(), d1, 2), 0);
    ASSERT_EQ(append_binary_file(path.c_str(), d2, 2), 0);

    size_t out_size = 0;
    char* buf = read_binary_file(path.c_str(), &out_size);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(out_size, (size_t)4);
    EXPECT_EQ((unsigned char)buf[0], 0x01);
    EXPECT_EQ((unsigned char)buf[3], 0x04);
    free(buf);
}

TEST_F(FileWriteTest, AtomicWrite) {
    std::string path = test_path("atomic.txt");
    write_text_file(path.c_str(), "original");
    ASSERT_EQ(write_text_file_atomic(path.c_str(), "updated"), 0);

    char* content = read_text_file(path.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "updated");
    free(content);
}

TEST_F(FileWriteTest, AtomicWriteNew) {
    std::string path = test_path("atomic_new.txt");
    ASSERT_EQ(write_text_file_atomic(path.c_str(), "brand new"), 0);

    char* content = read_text_file(path.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "brand new");
    free(content);
}

TEST_F(FileWriteTest, AtomicWriteNull) {
    EXPECT_EQ(write_text_file_atomic(NULL, "data"), -1);
    EXPECT_EQ(write_text_file_atomic("test.txt", NULL), -1);
}

TEST_F(FileWriteTest, AtomicNewPreservesExistingAndPublishesEmptyOrBinaryFiles) {
    char path[256];
    ASSERT_GT(path_str_join_posix_into(path, sizeof(path), TEST_DIR, "exclusive.bin"), 0);
    const char bytes[] = {'A', '\0', 'B'};
    ASSERT_EQ(write_binary_file_atomic_new(path, bytes, sizeof(bytes)), 0);
    errno = 0;
    EXPECT_EQ(write_binary_file_atomic_new(path, "replacement", 11), -1);
    EXPECT_EQ(errno, EEXIST);
    char* content = nullptr;
    size_t size = 0;
    ASSERT_TRUE(file_read_all(path, MEM_CAT_TEMP, &content, &size));
    EXPECT_EQ(size, sizeof(bytes));
    if (size == sizeof(bytes)) EXPECT_EQ(memcmp(content, bytes, sizeof(bytes)), 0);
    mem_free(content);
    ASSERT_EQ(file_delete(path), 0);
    ASSERT_EQ(write_binary_file_atomic_new(path, "", 0), 0);
    EXPECT_EQ(file_size(path), 0);
    EXPECT_EQ(write_binary_file_atomic_new(nullptr, "x", 1), -1);
    EXPECT_EQ(write_binary_file_atomic_new(path, nullptr, 0), -1);
}

TEST_F(FileWriteTest, AtomicNewCannotReplaceDirectoryOrSymlink) {
    char path[256];
    ASSERT_GT(path_str_join_posix_into(path, sizeof(path), TEST_DIR, "directory"), 0);
    ASSERT_TRUE(create_dir(path));
    EXPECT_EQ(write_binary_file_atomic_new(path, "x", 1), -1);
    EXPECT_TRUE(file_is_dir(path));
#ifndef _WIN32
    char target[256];
    ASSERT_GT(path_str_join_posix_into(target, sizeof(target), TEST_DIR, "target"), 0);
    ASSERT_EQ(write_binary_file_atomic_new(target, "original", 8), 0);
    ASSERT_GT(path_str_join_posix_into(path, sizeof(path), TEST_DIR, "link"), 0);
    ASSERT_EQ(symlink("target", path), 0);
    EXPECT_EQ(write_binary_file_atomic_new(path, "replacement", 11), -1);
    EXPECT_EQ(errno, EEXIST);
    EXPECT_TRUE(file_is_symlink(path));
    char* content = nullptr;
    ASSERT_TRUE(file_read_all(target, MEM_CAT_TEMP, &content, nullptr));
    EXPECT_STREQ(content, "original");
    mem_free(content);
#endif
}

/* ================================================================== *
 *  §2  File Operations                                               *
 * ================================================================== */

class FileOpsTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
        setup_test_dir();
    }
    void TearDown() override {
        cleanup_test_dir();
    }
};

TEST_F(FileOpsTest, CopyFile) {
    std::string src = test_path("copy_src.txt");
    std::string dst = test_path("copy_dst.txt");
    write_text_file(src.c_str(), "copy content");

    FileCopyOptions opts = {false, false};
    ASSERT_EQ(file_copy(src.c_str(), dst.c_str(), &opts), 0);

    char* content = read_text_file(dst.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "copy content");
    free(content);

    // source should still exist
    EXPECT_TRUE(file_exists(src.c_str()));
}

TEST_F(FileOpsTest, CopyFileNoOverwrite) {
    std::string src = test_path("copy_src2.txt");
    std::string dst = test_path("copy_dst2.txt");
    write_text_file(src.c_str(), "source");
    write_text_file(dst.c_str(), "existing");

    FileCopyOptions opts = {false, false};
    EXPECT_EQ(file_copy(src.c_str(), dst.c_str(), &opts), -1);

    // destination should be unchanged
    char* content = read_text_file(dst.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "existing");
    free(content);
}

TEST_F(FileOpsTest, CopyFileOverwrite) {
    std::string src = test_path("copy_src3.txt");
    std::string dst = test_path("copy_dst3.txt");
    write_text_file(src.c_str(), "new content");
    write_text_file(dst.c_str(), "old content");

    FileCopyOptions opts = {true, false};
    ASSERT_EQ(file_copy(src.c_str(), dst.c_str(), &opts), 0);

    char* content = read_text_file(dst.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "new content");
    free(content);
}

#ifndef _WIN32
TEST_F(FileOpsTest, CopyRejectsSymlinkDestination) {
    const char* src = "temp/test_file_module/copy_link_src.txt";
    const char* target = "temp/test_file_module/copy_target.txt";
    const char* link = "temp/test_file_module/copy_link.txt";
    write_text_file(src, "replacement");
    write_text_file(target, "original");
    ASSERT_TRUE(file_is_file(src));
    ASSERT_TRUE(file_is_file(target));
    ASSERT_EQ(symlink("copy_target.txt", link), 0);

    FileCopyOptions opts = {true, false};
    EXPECT_EQ(file_copy(src, link, &opts), -1);

    char* content = read_text_file(target);
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "original");
    free(content);
}
#endif

TEST_F(FileOpsTest, CopyFileCreatesDirs) {
    std::string src = test_path("copy_src4.txt");
    std::string dst = test_path("sub/dir/copy_dst4.txt");
    write_text_file(src.c_str(), "nested copy");

    FileCopyOptions opts = {false, false};
    ASSERT_EQ(file_copy(src.c_str(), dst.c_str(), &opts), 0);

    char* content = read_text_file(dst.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "nested copy");
    free(content);
}

TEST_F(FileOpsTest, CopyNull) {
    EXPECT_EQ(file_copy(NULL, "dst", NULL), -1);
    EXPECT_EQ(file_copy("src", NULL, NULL), -1);
}

TEST_F(FileOpsTest, MoveFile) {
    std::string src = test_path("move_src.txt");
    std::string dst = test_path("move_dst.txt");
    write_text_file(src.c_str(), "move me");

    ASSERT_EQ(file_move(src.c_str(), dst.c_str()), 0);
    EXPECT_FALSE(file_exists(src.c_str()));
    EXPECT_TRUE(file_exists(dst.c_str()));

    char* content = read_text_file(dst.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "move me");
    free(content);
}

TEST_F(FileOpsTest, MoveNull) {
    EXPECT_EQ(file_move(NULL, "dst"), -1);
    EXPECT_EQ(file_move("src", NULL), -1);
}

TEST_F(FileOpsTest, DeleteFile) {
    std::string path = test_path("delete_me.txt");
    write_text_file(path.c_str(), "bye");
    EXPECT_TRUE(file_exists(path.c_str()));
    ASSERT_EQ(file_delete(path.c_str()), 0);
    EXPECT_FALSE(file_exists(path.c_str()));
}

TEST_F(FileOpsTest, DeleteNonExistent) {
    EXPECT_EQ(file_delete(test_path("no_such_file.txt").c_str()), -1);
}

TEST_F(FileOpsTest, DeleteNull) {
    EXPECT_EQ(file_delete(NULL), -1);
}

TEST_F(FileOpsTest, TouchNew) {
    std::string path = test_path("touched.txt");
    ASSERT_EQ(file_touch(path.c_str()), 0);
    EXPECT_TRUE(file_exists(path.c_str()));
    EXPECT_EQ(file_size(path.c_str()), 0);
}

TEST_F(FileOpsTest, TouchExisting) {
    std::string path = test_path("touch_existing.txt");
    write_text_file(path.c_str(), "content");
    ASSERT_EQ(file_touch(path.c_str()), 0);
    // content should be preserved
    char* content = read_text_file(path.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "content");
    free(content);
}

TEST_F(FileOpsTest, TouchNull) {
    EXPECT_EQ(file_touch(NULL), -1);
}

TEST_F(FileOpsTest, Symlink) {
    std::string target = test_path("symlink_target.txt");
    std::string link = test_path("symlink_link.txt");
    write_text_file(target.c_str(), "symlink content");

    // symlink target must be relative to the link's directory or absolute
    char abs_target[PATH_MAX];
    realpath(target.c_str(), abs_target);

    ASSERT_EQ(file_symlink(abs_target, link.c_str()), 0);
    EXPECT_TRUE(file_exists(link.c_str()));
    EXPECT_TRUE(file_is_symlink(link.c_str()));

    char* content = read_text_file(link.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "symlink content");
    free(content);
}

TEST_F(FileOpsTest, SymlinkNull) {
    EXPECT_EQ(file_symlink(NULL, "link"), -1);
    EXPECT_EQ(file_symlink("target", NULL), -1);
}

TEST_F(FileOpsTest, Chmod) {
    std::string path = test_path("chmod_test.txt");
    write_text_file(path.c_str(), "chmod");
#ifndef _WIN32
    ASSERT_EQ(file_chmod(path.c_str(), 0644), 0);
    FileStat st = file_stat(path.c_str());
    EXPECT_EQ(st.mode & 0777, (uint16_t)0644);

    ASSERT_EQ(file_chmod(path.c_str(), 0755), 0);
    st = file_stat(path.c_str());
    EXPECT_EQ(st.mode & 0777, (uint16_t)0755);
#else
    // Windows: no-op, should return 0
    EXPECT_EQ(file_chmod(path.c_str(), 0644), 0);
#endif
}

TEST_F(FileOpsTest, ChmodNull) {
    EXPECT_EQ(file_chmod(NULL, 0644), -1);
}

TEST_F(FileOpsTest, RenameFile) {
    std::string old_p = test_path("rename_old.txt");
    std::string new_p = test_path("rename_new.txt");
    write_text_file(old_p.c_str(), "renamed");

    ASSERT_EQ(file_rename(old_p.c_str(), new_p.c_str()), 0);
    EXPECT_FALSE(file_exists(old_p.c_str()));

    char* content = read_text_file(new_p.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "renamed");
    free(content);
}

TEST_F(FileOpsTest, RenameNull) {
    EXPECT_EQ(file_rename(NULL, "new"), -1);
    EXPECT_EQ(file_rename("old", NULL), -1);
}

/* ================================================================== *
 *  §3  Queries & Metadata                                            *
 * ================================================================== */

class FileQueryTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
        setup_test_dir();
    }
    void TearDown() override {
        cleanup_test_dir();
    }
};

TEST_F(FileQueryTest, ExistsFile) {
    std::string path = test_path("exists.txt");
    write_text_file(path.c_str(), "hi");
    EXPECT_TRUE(file_exists(path.c_str()));
}

TEST_F(FileQueryTest, ExistsDir) {
    EXPECT_TRUE(file_exists(TEST_DIR));
}

TEST_F(FileQueryTest, ExistsNonExistent) {
    EXPECT_FALSE(file_exists(test_path("no_such_file.xyz").c_str()));
}

TEST_F(FileQueryTest, ExistsNull) {
    EXPECT_FALSE(file_exists(NULL));
}

TEST_F(FileQueryTest, IsFile) {
    std::string path = test_path("isfile.txt");
    write_text_file(path.c_str(), "file");
    EXPECT_TRUE(file_is_file(path.c_str()));
    EXPECT_FALSE(file_is_file(TEST_DIR));
}

TEST_F(FileQueryTest, IsDir) {
    EXPECT_TRUE(file_is_dir(TEST_DIR));
    std::string path = test_path("isdir.txt");
    write_text_file(path.c_str(), "not dir");
    EXPECT_FALSE(file_is_dir(path.c_str()));
}

TEST_F(FileQueryTest, IsSymlink) {
    std::string target = test_path("sym_target.txt");
    std::string link = test_path("sym_link.txt");
    write_text_file(target.c_str(), "t");
    file_symlink(target.c_str(), link.c_str());
    EXPECT_TRUE(file_is_symlink(link.c_str()));
    EXPECT_FALSE(file_is_symlink(target.c_str()));
}

TEST_F(FileQueryTest, IsSymlinkNull) {
    EXPECT_FALSE(file_is_symlink(NULL));
}

TEST_F(FileQueryTest, FileStatRegular) {
    std::string path = test_path("stat_test.txt");
    write_text_file(path.c_str(), "hello");

    FileStat st = file_stat(path.c_str());
    EXPECT_TRUE(st.exists);
    EXPECT_TRUE(st.is_file);
    EXPECT_FALSE(st.is_dir);
    EXPECT_EQ(st.size, 5);
    EXPECT_GT(st.modified, (time_t)0);
}

TEST_F(FileQueryTest, FileStatDir) {
    FileStat st = file_stat(TEST_DIR);
    EXPECT_TRUE(st.exists);
    EXPECT_FALSE(st.is_file);
    EXPECT_TRUE(st.is_dir);
}

#ifndef _WIN32
TEST_F(FileQueryTest, FileStatDistinguishesSubsecondRevisions) {
    char* path = file_path_join(TEST_DIR, "subsecond-revision.txt");
    ASSERT_NE(path, nullptr);
    ASSERT_EQ(write_binary_file(path, "red", 3), 0);
    struct timespec times[2] = {{1700000000, 100000000}, {1700000000, 100000000}};
    ASSERT_EQ(utimensat(AT_FDCWD, path, times, 0), 0);
    FileRevision before = file_revision(path);
    ASSERT_EQ(write_binary_file(path, "sky", 3), 0);
    times[1].tv_nsec = 200000000;
    ASSERT_EQ(utimensat(AT_FDCWD, path, times, 0), 0);
    FileRevision after = file_revision(path);
    EXPECT_EQ(before.stat.modified, after.stat.modified);
    EXPECT_EQ(before.stat.size, after.stat.size);
    EXPECT_EQ(before.inode, after.inode);
    EXPECT_EQ(before.modified_nsec, 100000000u);
    EXPECT_EQ(after.modified_nsec, 200000000u);
    mem_free(path);
}
#endif

TEST_F(FileQueryTest, FileStatNonExistent) {
    FileStat st = file_stat(test_path("nonexistent.xyz").c_str());
    EXPECT_FALSE(st.exists);
    EXPECT_EQ(st.size, -1);
}

TEST_F(FileQueryTest, FileStatNull) {
    FileStat st = file_stat(NULL);
    EXPECT_FALSE(st.exists);
}

TEST_F(FileQueryTest, FileSize) {
    std::string path = test_path("size_test.txt");
    write_text_file(path.c_str(), "12345");
    EXPECT_EQ(file_size(path.c_str()), 5);
}

TEST_F(FileQueryTest, FileSizeNonExistent) {
    EXPECT_EQ(file_size(test_path("no.txt").c_str()), -1);
}

TEST_F(FileQueryTest, FileSizeNull) {
    EXPECT_EQ(file_size(NULL), -1);
}

TEST_F(FileQueryTest, Realpath) {
    std::string path = test_path("realpath.txt");
    write_text_file(path.c_str(), "r");

    char* rp = file_realpath(path.c_str());
    ASSERT_NE(rp, nullptr);
    // should be an absolute path
    EXPECT_EQ(rp[0], '/');
    EXPECT_NE(strstr(rp, "realpath.txt"), nullptr);
    free(rp);
}

TEST_F(FileQueryTest, RealpathNull) {
    EXPECT_EQ(file_realpath(NULL), nullptr);
}

/* ================================================================== *
 *  §4  Streaming Reads                                               *
 * ================================================================== */

struct LineData {
    std::vector<std::string> lines;
    std::vector<int> line_numbers;
};

static bool line_collector(const char* line, size_t len, int line_number,
                           void* user_data) {
    LineData* ld = (LineData*)user_data;
    ld->lines.push_back(std::string(line, len));
    ld->line_numbers.push_back(line_number);
    return true;
}

static bool line_stop_at_2(const char* line, size_t len, int line_number,
                           void* user_data) {
    LineData* ld = (LineData*)user_data;
    ld->lines.push_back(std::string(line, len));
    ld->line_numbers.push_back(line_number);
    return line_number < 1; // stop after line 1 (0-indexed)
}

class FileStreamTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
        setup_test_dir();
    }
    void TearDown() override {
        cleanup_test_dir();
    }
};

TEST_F(FileStreamTest, ReadLines) {
    std::string path = test_path("lines.txt");
    write_text_file(path.c_str(), "alpha\nbeta\ngamma\n");

    LineData ld;
    ASSERT_EQ(file_read_lines(path.c_str(), line_collector, &ld), 0);
    ASSERT_EQ(ld.lines.size(), (size_t)3);
    EXPECT_EQ(ld.lines[0], "alpha");
    EXPECT_EQ(ld.lines[1], "beta");
    EXPECT_EQ(ld.lines[2], "gamma");
    EXPECT_EQ(ld.line_numbers[0], 0);
    EXPECT_EQ(ld.line_numbers[2], 2);
}

TEST_F(FileStreamTest, ReadLinesNoTrailingNewline) {
    std::string path = test_path("no_nl.txt");
    write_text_file(path.c_str(), "line1\nline2");

    LineData ld;
    ASSERT_EQ(file_read_lines(path.c_str(), line_collector, &ld), 0);
    ASSERT_EQ(ld.lines.size(), (size_t)2);
    EXPECT_EQ(ld.lines[0], "line1");
    EXPECT_EQ(ld.lines[1], "line2");
}

TEST_F(FileStreamTest, ReadLinesEarlyStop) {
    std::string path = test_path("early_stop.txt");
    write_text_file(path.c_str(), "a\nb\nc\nd\n");

    LineData ld;
    ASSERT_EQ(file_read_lines(path.c_str(), line_stop_at_2, &ld), 0);
    EXPECT_EQ(ld.lines.size(), (size_t)2);
}

TEST_F(FileStreamTest, ReadLinesEmpty) {
    std::string path = test_path("empty.txt");
    write_text_file(path.c_str(), "");

    LineData ld;
    ASSERT_EQ(file_read_lines(path.c_str(), line_collector, &ld), 0);
    EXPECT_EQ(ld.lines.size(), (size_t)0);
}

TEST_F(FileStreamTest, ReadLinesNonExistent) {
    EXPECT_EQ(file_read_lines("no_such_file.txt", line_collector, NULL), -1);
}

TEST_F(FileStreamTest, ReadLinesNull) {
    EXPECT_EQ(file_read_lines(NULL, line_collector, NULL), -1);
    std::string path = test_path("null_cb.txt");
    write_text_file(path.c_str(), "x");
    EXPECT_EQ(file_read_lines(path.c_str(), NULL, NULL), -1);
}

/* ================================================================== *
 *  §5  Temp Files                                                    *
 * ================================================================== */

class FileTempTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
    }
};

TEST_F(FileTempTest, TempPath) {
    char* path = file_temp_path("test", ".json");
    ASSERT_NE(path, nullptr);
    EXPECT_TRUE(strstr(path, "temp/") != nullptr);
    EXPECT_TRUE(strstr(path, "test") != nullptr);
    EXPECT_TRUE(strstr(path, ".json") != nullptr);
    file_delete(path);
    free(path);
}

TEST_F(FileTempTest, TempPathDefaults) {
    char* path = file_temp_path(NULL, NULL);
    ASSERT_NE(path, nullptr);
    EXPECT_TRUE(strstr(path, "temp/tmp") != nullptr);
    file_delete(path);
    free(path);
}

TEST_F(FileTempTest, TempPathUnique) {
    char* p1 = file_temp_path("u", ".txt");
    char* p2 = file_temp_path("u", ".txt");
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_STRNE(p1, p2);
    file_delete(p1);
    file_delete(p2);
    free(p1);
    free(p2);
}

TEST_F(FileTempTest, RejectsTraversalComponents) {
    EXPECT_EQ(file_temp_path("../escape", ".txt"), nullptr);
    EXPECT_EQ(file_temp_create("safe", "/escape"), nullptr);
    EXPECT_EQ(dir_temp_create("../escape"), nullptr);
}

TEST_F(FileTempTest, TempCreate) {
    char* path = file_temp_create("tc", ".dat");
    ASSERT_NE(path, nullptr);
    EXPECT_TRUE(file_exists(path));
    EXPECT_TRUE(file_is_file(path));
    file_delete(path);
    free(path);
}

TEST_F(FileTempTest, DirTempCreate) {
    char* path = dir_temp_create("td");
    ASSERT_NE(path, nullptr);
    EXPECT_TRUE(file_exists(path));
    EXPECT_TRUE(file_is_dir(path));
    dir_delete(path);
    free(path);
}

/* ================================================================== *
 *  §6  Path Utilities                                                *
 * ================================================================== */

class FilePathTest : public ::testing::Test {};

TEST_F(FilePathTest, JoinSimple) {
    char* p = file_path_join("src", "main.c");
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "src/main.c");
    free(p);
}

TEST_F(FilePathTest, LexicalNormalizationRejectsExcessSegments) {
    char input[1024] = {0};
    size_t pos = 0;
    for (int i = 0; i < 257; i++) {
        input[pos++] = 'a';
        input[pos++] = '/';
    }
    input[pos] = '\0';
    char output[1024];
    EXPECT_EQ(path_str_normalize_lexical_posix(input, output, sizeof(output), false), -1);
    EXPECT_EQ(path_str_normalize_lexical_win32(input, output, sizeof(output), false), -1);
}

TEST_F(FilePathTest, LexicalNormalizationRejectsSmallOutputBuffer) {
    char output[4] = {'x', 'x', 'x', '\0'};
    EXPECT_EQ(path_str_normalize_lexical_posix("abcdef", output, sizeof(output), false), -1);
    EXPECT_STREQ(output, "");
    EXPECT_EQ(path_str_normalize_lexical_win32("abcdef", output, sizeof(output), false), -1);
    EXPECT_STREQ(output, "");
}

TEST_F(FilePathTest, PathCopyAndJoinRejectTruncation) {
    char output[4] = {'x', 'x', 'x', '\0'};
    EXPECT_EQ(path_str_copy(output, sizeof(output), "abcd"), -1);
    EXPECT_STREQ(output, "");
    EXPECT_EQ(path_str_join_posix_into(output, sizeof(output), "ab", "cd"), -1);
    EXPECT_STREQ(output, "");
}

TEST_F(FilePathTest, JoinTrailingSlash) {
    char* p = file_path_join("src/", "main.c");
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "src/main.c");
    free(p);
}

TEST_F(FilePathTest, JoinLeadingSlash) {
    char* p = file_path_join("src", "/main.c");
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "src/main.c");
    free(p);
}

TEST_F(FilePathTest, JoinBothSlash) {
    char* p = file_path_join("src/", "/main.c");
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "src/main.c");
    free(p);
}

TEST_F(FilePathTest, JoinEmptyBase) {
    char* p = file_path_join("", "main.c");
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "main.c");
    free(p);
}

TEST_F(FilePathTest, JoinEmptyRelative) {
    char* p = file_path_join("src", "");
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "src");
    free(p);
}

TEST_F(FilePathTest, JoinNullBase) {
    char* p = file_path_join(NULL, "main.c");
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "main.c");
    free(p);
}

TEST_F(FilePathTest, JoinNullRelative) {
    char* p = file_path_join("src", NULL);
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(p, "src");
    free(p);
}

TEST_F(FilePathTest, DirnameSimple) {
    char* d = file_path_dirname("src/main.c");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, "src");
    free(d);
}

TEST_F(FilePathTest, DirnameNested) {
    char* d = file_path_dirname("/a/b/c/file.txt");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, "/a/b/c");
    free(d);
}

TEST_F(FilePathTest, DirnameRoot) {
    char* d = file_path_dirname("/file.txt");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, "/");
    free(d);
}

TEST_F(FilePathTest, DirnameNoSlash) {
    char* d = file_path_dirname("file.txt");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, ".");
    free(d);
}

TEST_F(FilePathTest, DirnameEmpty) {
    char* d = file_path_dirname("");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, ".");
    free(d);
}

TEST_F(FilePathTest, DirnameNull) {
    char* d = file_path_dirname(NULL);
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, ".");
    free(d);
}

TEST_F(FilePathTest, DirnameTrailingSlash) {
    char* d = file_path_dirname("src/dir/");
    ASSERT_NE(d, nullptr);
    EXPECT_STREQ(d, "src");
    free(d);
}

TEST_F(FilePathTest, BasenameSimple) {
    const char* b = file_path_basename("src/main.c");
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(b, "main.c");
}

TEST_F(FilePathTest, BasenameNoDir) {
    const char* b = file_path_basename("file.txt");
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(b, "file.txt");
}

TEST_F(FilePathTest, BasenameNull) {
    EXPECT_EQ(file_path_basename(NULL), nullptr);
}

TEST_F(FilePathTest, ExtSimple) {
    const char* e = file_path_ext("file.txt");
    ASSERT_NE(e, nullptr);
    EXPECT_STREQ(e, ".txt");
}

TEST_F(FilePathTest, ExtMultipleDots) {
    const char* e = file_path_ext("archive.tar.gz");
    ASSERT_NE(e, nullptr);
    EXPECT_STREQ(e, ".gz");
}

TEST_F(FilePathTest, ExtNoExt) {
    EXPECT_EQ(file_path_ext("Makefile"), nullptr);
}

TEST_F(FilePathTest, ExtHidden) {
    // hidden file with no extension after dot
    EXPECT_EQ(file_path_ext(".gitignore"), nullptr);
}

TEST_F(FilePathTest, ExtNull) {
    EXPECT_EQ(file_path_ext(NULL), nullptr);
}

TEST_F(FilePathTest, ExtInPath) {
    const char* e = file_path_ext("src/main.cpp");
    ASSERT_NE(e, nullptr);
    EXPECT_STREQ(e, ".cpp");
}

TEST_F(FilePathTest, ExtIgnoresDotInDirectory) {
    EXPECT_EQ(file_path_ext("src.v1/Makefile"), nullptr);
}

TEST_F(FilePathTest, ExtLenExplicitLength) {
    const char path[] = "archive.tar.gz?ignored";
    size_t ext_len = 0;
    const char* e = file_path_ext_len(path, strlen("archive.tar.gz"), &ext_len);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(ext_len, 3u);
    EXPECT_EQ(strncmp(e, ".gz", ext_len), 0);
}

TEST_F(FilePathTest, ExtLenTrailingSlashAndTrailingDot) {
    EXPECT_EQ(file_path_ext_len("dir/file.txt/", strlen("dir/file.txt/"), nullptr), nullptr);
    EXPECT_EQ(file_path_ext_len("dir/file.", strlen("dir/file."), nullptr), nullptr);
}

TEST_F(FilePathTest, HasExtCaseInsensitive) {
    EXPECT_TRUE(file_path_has_ext_ci("assets/ICON.SVG", "svg"));
    EXPECT_TRUE(file_path_has_ext_ci("assets/ICON.SVG", ".svg"));
    EXPECT_FALSE(file_path_has_ext_ci("assets.v2/Makefile", "v2"));
    EXPECT_FALSE(file_path_has_ext_ci("assets/ICON.SVG", ""));
}

/* ================================================================== *
 *  §7  Directory Operations                                          *
 * ================================================================== */

class DirOpsTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
        setup_test_dir();
    }
    void TearDown() override {
        cleanup_test_dir();
    }
};

TEST_F(DirOpsTest, DirList) {
    // create some files and a subdir
    write_text_file(test_path("a.txt").c_str(), "a");
    write_text_file(test_path("b.txt").c_str(), "b");
    create_dir(test_path("subdir").c_str());

    ArrayList* list = dir_list(TEST_DIR);
    ASSERT_NE(list, nullptr);
    EXPECT_GE(list->length, 3);

    bool found_a = false, found_b = false, found_sub = false;
    for (int i = 0; i < list->length; i++) {
        DirEntry* entry = (DirEntry*)list->data[i];
        if (strcmp(entry->name, "a.txt") == 0) found_a = true;
        if (strcmp(entry->name, "b.txt") == 0) found_b = true;
        if (strcmp(entry->name, "subdir") == 0) {
            found_sub = true;
            EXPECT_TRUE(entry->is_dir);
        }
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
    EXPECT_TRUE(found_sub);

    for (int i = 0; i < list->length; i++)
        dir_entry_free((DirEntry*)list->data[i]);
    arraylist_free(list);
}

TEST_F(DirOpsTest, DirListEmpty) {
    std::string empty = test_path("empty_dir");
    create_dir(empty.c_str());

    ArrayList* list = dir_list(empty.c_str());
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->length, 0);

    arraylist_free(list);
}

TEST_F(DirOpsTest, DirListNonExistent) {
    ArrayList* list = dir_list(test_path("no_such_dir").c_str());
    EXPECT_EQ(list, nullptr);
}

TEST_F(DirOpsTest, DirListNull) {
    EXPECT_EQ(dir_list(NULL), nullptr);
}

struct WalkData {
    std::vector<std::string> paths;
    std::vector<bool> is_dirs;
};

static bool walk_cb(const char* path, bool is_dir, void* user_data) {
    WalkData* wd = (WalkData*)user_data;
    wd->paths.push_back(path);
    wd->is_dirs.push_back(is_dir);
    return true;
}

TEST_F(DirOpsTest, DirWalk) {
    write_text_file(test_path("w1.txt").c_str(), "1");
    create_dir(test_path("wsub").c_str());
    write_text_file(test_path("wsub/w2.txt").c_str(), "2");

    WalkData wd;
    ASSERT_EQ(dir_walk(TEST_DIR, walk_cb, &wd), 0);

    EXPECT_GE(wd.paths.size(), (size_t)3); // w1.txt, wsub, wsub/w2.txt

    bool found_w1 = false, found_wsub = false, found_w2 = false;
    for (size_t i = 0; i < wd.paths.size(); i++) {
        if (wd.paths[i].find("w1.txt") != std::string::npos) found_w1 = true;
        if (wd.paths[i].find("wsub") != std::string::npos &&
            wd.is_dirs[i]) found_wsub = true;
        if (wd.paths[i].find("w2.txt") != std::string::npos) found_w2 = true;
    }
    EXPECT_TRUE(found_w1);
    EXPECT_TRUE(found_wsub);
    EXPECT_TRUE(found_w2);
}

TEST_F(DirOpsTest, DirWalkNull) {
    EXPECT_EQ(dir_walk(NULL, walk_cb, NULL), -1);
    EXPECT_EQ(dir_walk(TEST_DIR, NULL, NULL), -1);
}

TEST_F(DirOpsTest, DirDelete) {
    std::string sub = test_path("del_tree");
    create_dir(sub.c_str());
    write_text_file((sub + "/f1.txt").c_str(), "1");
    create_dir((sub + "/nested").c_str());
    write_text_file((sub + "/nested/f2.txt").c_str(), "2");

    ASSERT_EQ(dir_delete(sub.c_str()), 0);
    EXPECT_FALSE(file_exists(sub.c_str()));
}

TEST_F(DirOpsTest, DirDeleteNull) {
    EXPECT_EQ(dir_delete(NULL), -1);
}

TEST_F(DirOpsTest, DirCopy) {
    std::string src = test_path("copy_src_dir");
    std::string dst = test_path("copy_dst_dir");
    create_dir(src.c_str());
    write_text_file((src + "/a.txt").c_str(), "aaa");
    create_dir((src + "/sub").c_str());
    write_text_file((src + "/sub/b.txt").c_str(), "bbb");

    ASSERT_EQ(dir_copy(src.c_str(), dst.c_str()), 0);
    EXPECT_TRUE(file_exists((dst + "/a.txt").c_str()));
    EXPECT_TRUE(file_exists((dst + "/sub/b.txt").c_str()));

    char* content = read_text_file((dst + "/sub/b.txt").c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_STREQ(content, "bbb");
    free(content);
}

TEST_F(DirOpsTest, DirCopyNull) {
    EXPECT_EQ(dir_copy(NULL, "dst"), -1);
    EXPECT_EQ(dir_copy("src", NULL), -1);
}

/* ================================================================== *
 *  §8  Glob & Find                                                   *
 * ================================================================== */

class FileGlobTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
        setup_test_dir();
    }
    void TearDown() override {
        cleanup_test_dir();
    }
};

TEST_F(FileGlobTest, GlobTxtFiles) {
    write_text_file(test_path("g1.txt").c_str(), "1");
    write_text_file(test_path("g2.txt").c_str(), "2");
    write_text_file(test_path("g3.dat").c_str(), "3");

    std::string pattern = std::string(TEST_DIR) + "/*.txt";
    ArrayList* list = file_glob(pattern.c_str());
    ASSERT_NE(list, nullptr);
    EXPECT_GE(list->length, 2);

    bool found_g1 = false, found_g2 = false;
    for (int i = 0; i < list->length; i++) {
        char* p = (char*)list->data[i];
        if (strstr(p, "g1.txt")) found_g1 = true;
        if (strstr(p, "g2.txt")) found_g2 = true;
        free(p);
    }
    EXPECT_TRUE(found_g1);
    EXPECT_TRUE(found_g2);
    arraylist_free(list);
}

TEST_F(FileGlobTest, GlobNoMatch) {
    ArrayList* list = file_glob(test_path("*.nonexistent_ext_xyz").c_str());
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->length, 0);
    arraylist_free(list);
}

TEST_F(FileGlobTest, GlobNull) {
    EXPECT_EQ(file_glob(NULL), nullptr);
}

TEST_F(FileGlobTest, FindFiles) {
    create_dir(test_path("find_sub").c_str());
    write_text_file(test_path("find1.txt").c_str(), "1");
    write_text_file(test_path("find_sub/find2.txt").c_str(), "2");
    write_text_file(test_path("find_sub/other.dat").c_str(), "3");

    ArrayList* list = file_find(TEST_DIR, "*.txt", true);
    ASSERT_NE(list, nullptr);
    EXPECT_GE(list->length, 2);

    bool found1 = false, found2 = false;
    for (int i = 0; i < list->length; i++) {
        char* p = (char*)list->data[i];
        if (strstr(p, "find1.txt")) found1 = true;
        if (strstr(p, "find2.txt")) found2 = true;
        free(p);
    }
    EXPECT_TRUE(found1);
    EXPECT_TRUE(found2);
    arraylist_free(list);
}

TEST_F(FileGlobTest, FindFilesNonRecursive) {
    create_dir(test_path("find_nr_sub").c_str());
    write_text_file(test_path("nr_top.txt").c_str(), "1");
    write_text_file(test_path("find_nr_sub/nr_nested.txt").c_str(), "2");

    ArrayList* list = file_find(TEST_DIR, "*.txt", false);
    ASSERT_NE(list, nullptr);

    // should find top-level but not nested
    bool found_top = false, found_nested = false;
    for (int i = 0; i < list->length; i++) {
        char* p = (char*)list->data[i];
        if (strstr(p, "nr_top.txt")) found_top = true;
        if (strstr(p, "nr_nested.txt")) found_nested = true;
        free(p);
    }
    EXPECT_TRUE(found_top);
    EXPECT_FALSE(found_nested);
    arraylist_free(list);
}

TEST_F(FileGlobTest, FindNull) {
    EXPECT_EQ(file_find(NULL, "*.txt", true), nullptr);
    EXPECT_EQ(file_find(TEST_DIR, NULL, true), nullptr);
}

/* ================================================================== *
 *  §9  Edge Cases                                                    *
 * ================================================================== */

class FileEdgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
        setup_test_dir();
    }
    void TearDown() override {
        cleanup_test_dir();
    }
};

TEST_F(FileEdgeTest, CopyPreserveMetadata) {
    std::string src = test_path("meta_src.txt");
    std::string dst = test_path("meta_dst.txt");
    write_text_file(src.c_str(), "preserve");

#ifndef _WIN32
    chmod(src.c_str(), 0644);
    FileCopyOptions opts = {false, true};
    ASSERT_EQ(file_copy(src.c_str(), dst.c_str(), &opts), 0);

    FileStat st = file_stat(dst.c_str());
    EXPECT_EQ(st.mode & 0777, (uint16_t)0644);
#endif
}

TEST_F(FileEdgeTest, WriteAndReadBinaryRoundtrip) {
    std::string path = test_path("roundtrip.bin");
    // generate all byte values
    char data[256];
    for (int i = 0; i < 256; i++) data[i] = (char)i;

    ASSERT_EQ(write_binary_file(path.c_str(), data, 256), 0);

    size_t out_size;
    char* buf = read_binary_file(path.c_str(), &out_size);
    ASSERT_NE(buf, nullptr);
    EXPECT_EQ(out_size, (size_t)256);
    EXPECT_EQ(memcmp(buf, data, 256), 0);
    free(buf);
}

TEST_F(FileEdgeTest, MultipleAppends) {
    std::string path = test_path("multi_append.txt");
    write_text_file(path.c_str(), "");
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(append_text_file(path.c_str(), "x"), 0);
    }

    char* content = read_text_file(path.c_str());
    ASSERT_NE(content, nullptr);
    EXPECT_EQ(strlen(content), (size_t)100);
    free(content);
}

TEST_F(FileEdgeTest, CreateDirIdempotent) {
    std::string path = test_path("idem_dir/sub");
    EXPECT_TRUE(create_dir(path.c_str()));
    EXPECT_TRUE(create_dir(path.c_str())); // second call should succeed
    EXPECT_TRUE(file_is_dir(path.c_str()));
}

struct ConcurrentDirectoryGate {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    size_t ready;
    bool start;
    const char* path;
};

struct ConcurrentDirectoryJob {
    ConcurrentDirectoryGate* gate;
    bool success;
};

static void await_concurrent_path_start(ConcurrentDirectoryGate* gate) {
    pthread_mutex_lock(&gate->mutex);
    gate->ready++;
    pthread_cond_broadcast(&gate->changed);
    while (!gate->start) pthread_cond_wait(&gate->changed, &gate->mutex);
    pthread_mutex_unlock(&gate->mutex);
}

static void* create_directory_together(void* argument) {
    ConcurrentDirectoryJob* job = (ConcurrentDirectoryJob*)argument;
    ConcurrentDirectoryGate* gate = job->gate;
    await_concurrent_path_start(gate);
    job->success = create_dir(gate->path);
    return nullptr;
}

struct ConcurrentPublicationJob {
    ConcurrentDirectoryGate* gate;
    FileAnchor* anchor;
    char content[128];
    int result;
    int error;
};

static void* publish_file_together(void* argument) {
    ConcurrentPublicationJob* job = (ConcurrentPublicationJob*)argument;
    await_concurrent_path_start(job->gate);
    job->result = job->anchor
        ? file_anchor_write_atomic(job->anchor, job->gate->path, job->content, sizeof(job->content), nullptr)
        : write_binary_file_atomic_new(job->gate->path, job->content, sizeof(job->content));
    job->error = errno;
    return nullptr;
}

static void expect_single_publication_winner(FileAnchor* anchor, const char* path) {
    constexpr size_t count = 8;
    ConcurrentDirectoryGate gate = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, false, path};
    pthread_t threads[count];
    ConcurrentPublicationJob jobs[count] = {};
    size_t created = 0;
    for (; created < count; created++) {
        jobs[created].gate = &gate;
        jobs[created].anchor = anchor;
        memset(jobs[created].content, 'A' + created, sizeof(jobs[created].content));
        if (pthread_create(&threads[created], nullptr, publish_file_together, &jobs[created]) != 0) break;
    }
    pthread_mutex_lock(&gate.mutex);
    while (gate.ready < created) pthread_cond_wait(&gate.changed, &gate.mutex);
    gate.start = true;
    pthread_cond_broadcast(&gate.changed);
    pthread_mutex_unlock(&gate.mutex);
    size_t winners = 0;
    const char* winner = nullptr;
    for (size_t i = 0; i < created; i++) {
        pthread_join(threads[i], nullptr);
        if (jobs[i].result == 0) { winners++; winner = jobs[i].content; }
        else EXPECT_EQ(jobs[i].error, EEXIST);
    }
    pthread_cond_destroy(&gate.changed);
    pthread_mutex_destroy(&gate.mutex);
    ASSERT_EQ(created, count);
    ASSERT_EQ(winners, 1U);
    char* content = nullptr;
    size_t size = 0;
    ASSERT_TRUE(anchor ? file_anchor_read_all(anchor, path, 1024, MEM_CAT_TEMP, &content, &size, nullptr)
        : file_read_all(path, MEM_CAT_TEMP, &content, &size));
    EXPECT_EQ(size, sizeof(jobs[0].content));
    if (size == sizeof(jobs[0].content)) EXPECT_EQ(memcmp(content, winner, size), 0);
    mem_free(content);
}

TEST_F(FileEdgeTest, AtomicNewConcurrentPublishersPreserveExactlyOneWinner) {
    for (size_t round = 0; round < 4; round++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/publication_%zu", TEST_DIR, round);
        expect_single_publication_winner(nullptr, path);
    }
}

#ifndef _WIN32
TEST_F(FileAnchorTest, ConcurrentPublicationCannotReplaceAnotherCreator) {
    FileAnchor* anchor = open();
    ASSERT_NE(anchor, nullptr);
    for (size_t round = 0; round < 4; round++) {
        char relative[64];
        snprintf(relative, sizeof(relative), "publication_%zu", round);
        expect_single_publication_winner(anchor, relative);
    }
}
#endif

TEST_F(FileEdgeTest, CreateDirAcceptsConcurrentCreators) {
    // release creators together so the stat/mkdir race reaches both parents and leaves.
    constexpr size_t count = 8;
    for (size_t round = 0; round < 16; round++) {
        char path[256];
        snprintf(path, sizeof(path), "%s/concurrent_%zu/parent/child/leaf", TEST_DIR, round);
        ConcurrentDirectoryGate gate = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, false, path};
        pthread_t threads[count];
        ConcurrentDirectoryJob jobs[count] = {};
        size_t created = 0;
        for (; created < count; created++) {
            jobs[created].gate = &gate;
            if (pthread_create(&threads[created], nullptr, create_directory_together, &jobs[created]) != 0) break;
        }
        pthread_mutex_lock(&gate.mutex);
        while (gate.ready < created) pthread_cond_wait(&gate.changed, &gate.mutex);
        gate.start = true;
        pthread_cond_broadcast(&gate.changed);
        pthread_mutex_unlock(&gate.mutex);
        for (size_t i = 0; i < created; i++) {
            pthread_join(threads[i], nullptr);
            EXPECT_TRUE(jobs[i].success) << path;
        }
        pthread_cond_destroy(&gate.changed);
        pthread_mutex_destroy(&gate.mutex);
        ASSERT_EQ(created, count);
        EXPECT_TRUE(file_is_dir(path));
    }
}

TEST_F(FileEdgeTest, CreateDirRejectsExistingFile) {
    char path[256];
    snprintf(path, sizeof(path), "%s/occupied", TEST_DIR);
    ASSERT_EQ(write_binary_file(path, "x", 1), 0);
    EXPECT_FALSE(create_dir(path));
    snprintf(path, sizeof(path), "%s/occupied/child", TEST_DIR);
    EXPECT_FALSE(create_dir(path));
}
