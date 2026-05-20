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
#include <climits>
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
#include "../lib/file.h"
#include "../lib/file_utils.h"
#include "../lib/arraylist.h"
#include "../lib/log.h"
}

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
    free(path);
}

TEST_F(FileTempTest, TempPathDefaults) {
    char* path = file_temp_path(NULL, NULL);
    ASSERT_NE(path, nullptr);
    EXPECT_TRUE(strstr(path, "temp/tmp") != nullptr);
    free(path);
}

TEST_F(FileTempTest, TempPathUnique) {
    char* p1 = file_temp_path("u", ".txt");
    char* p2 = file_temp_path("u", ".txt");
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_STRNE(p1, p2);
    free(p1);
    free(p2);
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
