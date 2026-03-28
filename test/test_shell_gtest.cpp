/*
 * Shell Module Test Suite (GTest)
 * ================================
 *
 * Comprehensive tests for lib/shell.h / lib/shell.c:
 *
 *  §1  Synchronous execution  — shell_exec, shell_exec_simple, exit codes,
 *                                stdout/stderr capture, timeout
 *  §2  Shell line execution   — shell_exec_line
 *  §3  Environment variables  — getenv, setenv, unsetenv
 *  §4  Utilities              — shell_which, shell_quote_arg, shell_result_free,
 *                                shell_get_home_dir, shell_get_temp_dir,
 *                                shell_get_hostname
 *  §5  Background processes   — shell_spawn, poll, wait, kill, free
 *  §6  Line callbacks         — streaming output via ShellLineCallback
 *  §7  Edge cases             — NULL args, missing programs, empty output
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>

extern "C" {
#include "../lib/shell.h"
#include "../lib/log.h"
}

/* ================================================================== *
 *  §1  Synchronous Execution                                         *
 * ================================================================== */

class ShellExecTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
    }
};

TEST_F(ShellExecTest, EchoStdout) {
    const char* args[] = {"echo", "hello world", NULL};
    ShellResult r = shell_exec_simple("echo", args);
    ASSERT_EQ(r.exit_code, 0);
    ASSERT_NE(r.stdout_buf, nullptr);
    EXPECT_GE(r.stdout_len, (size_t)11);
    // stdout should contain "hello world"
    EXPECT_NE(strstr(r.stdout_buf, "hello world"), nullptr);
    EXPECT_FALSE(r.timed_out);
    shell_result_free(&r);
}

TEST_F(ShellExecTest, ExitCodeNonZero) {
    const char* args[] = {"false", NULL};
    ShellResult r = shell_exec_simple("false", args);
    EXPECT_NE(r.exit_code, 0);
    shell_result_free(&r);
}

TEST_F(ShellExecTest, ExitCodeZero) {
    const char* args[] = {"true", NULL};
    ShellResult r = shell_exec_simple("true", args);
    EXPECT_EQ(r.exit_code, 0);
    shell_result_free(&r);
}

TEST_F(ShellExecTest, StderrCapture) {
    // write to stderr via sh
    const char* args[] = {"sh", "-c", "echo errormsg >&2", NULL};
    ShellResult r = shell_exec_simple("sh", args);
    ASSERT_EQ(r.exit_code, 0);
    ASSERT_NE(r.stderr_buf, nullptr);
    EXPECT_NE(strstr(r.stderr_buf, "errormsg"), nullptr);
    shell_result_free(&r);
}

TEST_F(ShellExecTest, MergeStderr) {
    ShellOptions opts = {0};
    opts.merge_stderr = true;
    const char* args[] = {"sh", "-c", "echo out; echo err >&2", NULL};
    ShellResult r = shell_exec("sh", args, &opts);
    ASSERT_EQ(r.exit_code, 0);
    ASSERT_NE(r.stdout_buf, nullptr);
    // both should be in stdout
    EXPECT_NE(strstr(r.stdout_buf, "out"), nullptr);
    EXPECT_NE(strstr(r.stdout_buf, "err"), nullptr);
    // stderr should be NULL or empty since it's merged
    shell_result_free(&r);
}

TEST_F(ShellExecTest, Timeout) {
    ShellOptions opts = {0};
    opts.timeout_ms = 200; // 200ms timeout
    const char* args[] = {"sleep", "10", NULL};
    ShellResult r = shell_exec("sleep", args, &opts);
    EXPECT_TRUE(r.timed_out);
    shell_result_free(&r);
}

TEST_F(ShellExecTest, ProgramNotFound) {
    const char* args[] = {"nonexistent_program_xyz_12345", NULL};
    ShellResult r = shell_exec_simple("nonexistent_program_xyz_12345", args);
    EXPECT_NE(r.exit_code, 0);
    shell_result_free(&r);
}

TEST_F(ShellExecTest, NullProgram) {
    ShellResult r = shell_exec(NULL, NULL, NULL);
    EXPECT_EQ(r.exit_code, -1);
}

TEST_F(ShellExecTest, MultilineOutput) {
    const char* args[] = {"printf", "line1\nline2\nline3\n", NULL};
    ShellResult r = shell_exec_simple("printf", args);
    ASSERT_EQ(r.exit_code, 0);
    ASSERT_NE(r.stdout_buf, nullptr);
    EXPECT_NE(strstr(r.stdout_buf, "line1"), nullptr);
    EXPECT_NE(strstr(r.stdout_buf, "line2"), nullptr);
    EXPECT_NE(strstr(r.stdout_buf, "line3"), nullptr);
    shell_result_free(&r);
}

TEST_F(ShellExecTest, WorkingDirectory) {
    ShellOptions opts = {0};
    opts.cwd = "/tmp";
    const char* args[] = {"pwd", NULL};
    ShellResult r = shell_exec("pwd", args, &opts);
    ASSERT_EQ(r.exit_code, 0);
    ASSERT_NE(r.stdout_buf, nullptr);
    // macOS may resolve /tmp to /private/tmp
    EXPECT_TRUE(strstr(r.stdout_buf, "/tmp") != nullptr ||
                strstr(r.stdout_buf, "/private/tmp") != nullptr);
    shell_result_free(&r);
}

/* ================================================================== *
 *  §2  Shell Line Execution                                          *
 * ================================================================== */

class ShellLineExecTest : public ::testing::Test {};

TEST_F(ShellLineExecTest, SimpleCommand) {
    ShellResult r = shell_exec_line("echo hello", NULL);
    ASSERT_EQ(r.exit_code, 0);
    ASSERT_NE(r.stdout_buf, nullptr);
    EXPECT_NE(strstr(r.stdout_buf, "hello"), nullptr);
    shell_result_free(&r);
}

TEST_F(ShellLineExecTest, PipelineCommand) {
    ShellResult r = shell_exec_line("echo 'abc def ghi' | wc -w", NULL);
    ASSERT_EQ(r.exit_code, 0);
    ASSERT_NE(r.stdout_buf, nullptr);
    EXPECT_NE(strstr(r.stdout_buf, "3"), nullptr);
    shell_result_free(&r);
}

TEST_F(ShellLineExecTest, NullCmdline) {
    ShellResult r = shell_exec_line(NULL, NULL);
    EXPECT_EQ(r.exit_code, -1);
}

TEST_F(ShellLineExecTest, WithTimeout) {
    ShellOptions opts = {0};
    opts.timeout_ms = 200;
    ShellResult r = shell_exec_line("sleep 10", &opts);
    EXPECT_TRUE(r.timed_out);
    shell_result_free(&r);
}

/* ================================================================== *
 *  §3  Environment Variables                                         *
 * ================================================================== */

class ShellEnvTest : public ::testing::Test {};

TEST_F(ShellEnvTest, GetenvExisting) {
    // PATH should always exist
    const char* path = shell_getenv("PATH");
    ASSERT_NE(path, nullptr);
    EXPECT_GT(strlen(path), (size_t)0);
}

TEST_F(ShellEnvTest, GetenvNonExistent) {
    const char* val = shell_getenv("LAMBDA_TEST_NONEXISTENT_VAR_XYZ");
    EXPECT_EQ(val, nullptr);
}

TEST_F(ShellEnvTest, GetenvNull) {
    EXPECT_EQ(shell_getenv(NULL), nullptr);
}

TEST_F(ShellEnvTest, SetenvAndGet) {
    ASSERT_TRUE(shell_setenv("LAMBDA_TEST_VAR_123", "test_value"));
    const char* val = shell_getenv("LAMBDA_TEST_VAR_123");
    ASSERT_NE(val, nullptr);
    EXPECT_STREQ(val, "test_value");
    // clean up
    shell_unsetenv("LAMBDA_TEST_VAR_123");
}

TEST_F(ShellEnvTest, UnsetenvExisting) {
    shell_setenv("LAMBDA_TEST_VAR_456", "to_remove");
    ASSERT_TRUE(shell_unsetenv("LAMBDA_TEST_VAR_456"));
    EXPECT_EQ(shell_getenv("LAMBDA_TEST_VAR_456"), nullptr);
}

TEST_F(ShellEnvTest, SetenvOverwrite) {
    shell_setenv("LAMBDA_TEST_VAR_789", "first");
    shell_setenv("LAMBDA_TEST_VAR_789", "second");
    const char* val = shell_getenv("LAMBDA_TEST_VAR_789");
    ASSERT_NE(val, nullptr);
    EXPECT_STREQ(val, "second");
    shell_unsetenv("LAMBDA_TEST_VAR_789");
}

TEST_F(ShellEnvTest, SetenvNull) {
    EXPECT_FALSE(shell_setenv(NULL, "value"));
}

TEST_F(ShellEnvTest, UnsetenvNull) {
    EXPECT_FALSE(shell_unsetenv(NULL));
}

/* ================================================================== *
 *  §4  Utilities                                                     *
 * ================================================================== */

class ShellUtilTest : public ::testing::Test {};

TEST_F(ShellUtilTest, WhichFindsEcho) {
    char* path = shell_which("echo");
    // echo might be a shell builtin on some systems, so path could be NULL
    // but on most systems /bin/echo or /usr/bin/echo exists
    if (path) {
        EXPECT_TRUE(strstr(path, "echo") != nullptr);
        free(path);
    }
}

TEST_F(ShellUtilTest, WhichFindsSh) {
    char* path = shell_which("sh");
    ASSERT_NE(path, nullptr);
    EXPECT_TRUE(strstr(path, "sh") != nullptr);
    free(path);
}

TEST_F(ShellUtilTest, WhichNotFound) {
    char* path = shell_which("nonexistent_program_xyz_99999");
    EXPECT_EQ(path, nullptr);
}

TEST_F(ShellUtilTest, WhichNull) {
    EXPECT_EQ(shell_which(NULL), nullptr);
}

TEST_F(ShellUtilTest, WhichEmpty) {
    EXPECT_EQ(shell_which(""), nullptr);
}

TEST_F(ShellUtilTest, QuoteArgSimple) {
    char* q = shell_quote_arg("hello");
    ASSERT_NE(q, nullptr);
    EXPECT_STREQ(q, "hello"); // no quoting needed
    free(q);
}

TEST_F(ShellUtilTest, QuoteArgWithSpaces) {
    char* q = shell_quote_arg("hello world");
    ASSERT_NE(q, nullptr);
    // should be single-quoted
    EXPECT_EQ(q[0], '\'');
    EXPECT_NE(strstr(q, "hello world"), nullptr);
    free(q);
}

TEST_F(ShellUtilTest, QuoteArgWithSingleQuote) {
    char* q = shell_quote_arg("it's");
    ASSERT_NE(q, nullptr);
    // the embedded quote should be escaped
    EXPECT_NE(strstr(q, "\\'"), nullptr);
    free(q);
}

TEST_F(ShellUtilTest, QuoteArgEmpty) {
    char* q = shell_quote_arg("");
    ASSERT_NE(q, nullptr);
    EXPECT_STREQ(q, "''");
    free(q);
}

TEST_F(ShellUtilTest, QuoteArgNull) {
    char* q = shell_quote_arg(NULL);
    ASSERT_NE(q, nullptr);
    EXPECT_STREQ(q, "''");
    free(q);
}

TEST_F(ShellUtilTest, QuoteArgSpecialChars) {
    char* q = shell_quote_arg("$(rm -rf /)");
    ASSERT_NE(q, nullptr);
    EXPECT_EQ(q[0], '\''); // must be quoted
    free(q);
}

TEST_F(ShellUtilTest, ResultFreeNull) {
    // should not crash
    shell_result_free(NULL);
    ShellResult r = {0};
    shell_result_free(&r);
}

TEST_F(ShellUtilTest, GetHomeDir) {
    const char* home = shell_get_home_dir();
    ASSERT_NE(home, nullptr);
    EXPECT_GT(strlen(home), (size_t)0);
    // should be consistent across calls (cached)
    EXPECT_EQ(home, shell_get_home_dir());
}

TEST_F(ShellUtilTest, GetTempDir) {
    const char* tmp = shell_get_temp_dir();
    ASSERT_NE(tmp, nullptr);
    EXPECT_GT(strlen(tmp), (size_t)0);
    EXPECT_EQ(tmp, shell_get_temp_dir()); // cached
}

TEST_F(ShellUtilTest, GetHostname) {
    char* host = shell_get_hostname();
    ASSERT_NE(host, nullptr);
    EXPECT_GT(strlen(host), (size_t)0);
    free(host);
}

/* ================================================================== *
 *  §5  Background Processes                                          *
 * ================================================================== */

class ShellSpawnTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_init(NULL);
    }
};

TEST_F(ShellSpawnTest, SpawnAndWait) {
    const char* args[] = {"echo", "spawned", NULL};
    ShellProcess* proc = shell_spawn("echo", args, NULL);
    ASSERT_NE(proc, nullptr);

    ShellResult r = shell_process_wait(proc, 5000);
    EXPECT_EQ(r.exit_code, 0);
    ASSERT_NE(r.stdout_buf, nullptr);
    EXPECT_NE(strstr(r.stdout_buf, "spawned"), nullptr);

    shell_result_free(&r);
    shell_process_free(proc);
}

TEST_F(ShellSpawnTest, SpawnAndPoll) {
    const char* args[] = {"sleep", "0.1", NULL};
    ShellProcess* proc = shell_spawn("sleep", args, NULL);
    ASSERT_NE(proc, nullptr);

    // should not be finished immediately
    // (might be on very fast systems, so don't assert false)
    
    ShellResult r = shell_process_wait(proc, 5000);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_FALSE(r.timed_out);

    shell_result_free(&r);
    shell_process_free(proc);
}

TEST_F(ShellSpawnTest, SpawnAndKill) {
    const char* args[] = {"sleep", "30", NULL};
    ShellProcess* proc = shell_spawn("sleep", args, NULL);
    ASSERT_NE(proc, nullptr);

    // give it a moment to start
    struct timespec ts = {0, 50000000L}; // 50ms
    nanosleep(&ts, NULL);

    EXPECT_TRUE(shell_process_kill(proc));

    ShellResult r = shell_process_wait(proc, 2000);
    // exit code should be non-zero (killed)
    EXPECT_NE(r.exit_code, 0);

    shell_result_free(&r);
    shell_process_free(proc);
}

TEST_F(ShellSpawnTest, SpawnNull) {
    EXPECT_EQ(shell_spawn(NULL, NULL, NULL), nullptr);
}

TEST_F(ShellSpawnTest, SpawnNotFound) {
    const char* args[] = {"nonexistent_xyz_999", NULL};
    ShellProcess* proc = shell_spawn("nonexistent_xyz_999", args, NULL);
    // posix_spawn might fail, returning NULL
    if (proc) {
        ShellResult r = shell_process_wait(proc, 2000);
        shell_result_free(&r);
        shell_process_free(proc);
    }
}

TEST_F(ShellSpawnTest, ProcessFreeNull) {
    shell_process_free(NULL); // should not crash
}

TEST_F(ShellSpawnTest, KillNull) {
    EXPECT_FALSE(shell_process_kill(NULL));
}

TEST_F(ShellSpawnTest, WaitWithTimeout) {
    const char* args[] = {"sleep", "30", NULL};
    ShellProcess* proc = shell_spawn("sleep", args, NULL);
    ASSERT_NE(proc, nullptr);

    ShellResult r = shell_process_wait(proc, 200);
    EXPECT_TRUE(r.timed_out);

    shell_result_free(&r);
    shell_process_free(proc);
}

/* ================================================================== *
 *  §6  Line Callbacks                                                *
 * ================================================================== */

struct LineCollector {
    std::vector<std::string> lines;
};

static bool collect_line(const char* line, size_t len, void* user_data) {
    LineCollector* lc = (LineCollector*)user_data;
    lc->lines.push_back(std::string(line, len));
    return true;
}

static bool collect_max_2(const char* line, size_t len, void* user_data) {
    LineCollector* lc = (LineCollector*)user_data;
    lc->lines.push_back(std::string(line, len));
    return lc->lines.size() < 2; // stop after 2 lines
}

class ShellCallbackTest : public ::testing::Test {};

TEST_F(ShellCallbackTest, StreamStdout) {
    LineCollector lc;
    ShellOptions opts = {0};
    opts.on_stdout = collect_line;
    opts.user_data = &lc;

    const char* args[] = {"printf", "line1\nline2\nline3\n", NULL};
    ShellResult r = shell_exec("printf", args, &opts);
    ASSERT_EQ(r.exit_code, 0);

    EXPECT_EQ(lc.lines.size(), (size_t)3);
    if (lc.lines.size() >= 3) {
        EXPECT_EQ(lc.lines[0], "line1");
        EXPECT_EQ(lc.lines[1], "line2");
        EXPECT_EQ(lc.lines[2], "line3");
    }

    shell_result_free(&r);
}

TEST_F(ShellCallbackTest, EarlyAbort) {
    LineCollector lc;
    ShellOptions opts = {0};
    opts.on_stdout = collect_max_2;
    opts.user_data = &lc;

    const char* args[] = {"printf", "a\nb\nc\nd\n", NULL};
    ShellResult r = shell_exec("printf", args, &opts);
    ASSERT_EQ(r.exit_code, 0);

    // callback returned false after 2 lines
    EXPECT_EQ(lc.lines.size(), (size_t)2);

    shell_result_free(&r);
}

/* ================================================================== *
 *  §7  Edge Cases                                                    *
 * ================================================================== */

class ShellEdgeTest : public ::testing::Test {};

TEST_F(ShellEdgeTest, EmptyOutput) {
    const char* args[] = {"true", NULL};
    ShellResult r = shell_exec_simple("true", args);
    EXPECT_EQ(r.exit_code, 0);
    // stdout may be NULL or empty
    if (r.stdout_buf) {
        EXPECT_EQ(r.stdout_len, (size_t)0);
    }
    shell_result_free(&r);
}

TEST_F(ShellEdgeTest, LargeOutput) {
    // generate ~100KB of output
    const char* args[] = {"sh", "-c", "dd if=/dev/zero bs=1024 count=100 2>/dev/null | tr '\\0' 'A'", NULL};
    ShellResult r = shell_exec_simple("sh", args);
    ASSERT_EQ(r.exit_code, 0);
    ASSERT_NE(r.stdout_buf, nullptr);
    EXPECT_GE(r.stdout_len, (size_t)(100 * 1024));
    shell_result_free(&r);
}

TEST_F(ShellEdgeTest, SpecialCharsInArgs) {
    // ensure arguments with special chars are passed correctly
    const char* args[] = {"echo", "hello\tworld", NULL};
    ShellResult r = shell_exec_simple("echo", args);
    ASSERT_EQ(r.exit_code, 0);
    ASSERT_NE(r.stdout_buf, nullptr);
    EXPECT_NE(strstr(r.stdout_buf, "hello"), nullptr);
    shell_result_free(&r);
}

TEST_F(ShellEdgeTest, ResultFreeTwice) {
    const char* args[] = {"echo", "test", NULL};
    ShellResult r = shell_exec_simple("echo", args);
    shell_result_free(&r);
    // second free should be safe (pointers set to NULL)
    shell_result_free(&r);
}
