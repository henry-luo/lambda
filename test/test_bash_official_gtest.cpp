//==============================================================================
// Bash Official Test Suite - Tests from GNU Bash (ref/bash/tests/)
//
// Auto-discovers *.tests / *.right file pairs from the official Bash test suite
// cloned under ref/bash/tests/. Each test runs the .tests script through
// lambda.exe bash and compares combined stdout+stderr against the .right file.
//
// The official tests use ${THIS_SH} to invoke sub-scripts (.sub files).
// We set THIS_SH to a wrapper that calls lambda.exe bash.
//
// Usage:
//   ./test_bash_official_gtest.exe                                   # all tests
//   ./test_bash_official_gtest.exe --gtest_filter=Official/BashOfficialTest.ExecuteAndCompare/arith
//==============================================================================

#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(status) (status)
    #define LAMBDA_EXE "lambda.exe"
    #define PATH_SEP "\\"
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #define LAMBDA_EXE "./lambda.exe"
    #define PATH_SEP "/"
#endif

static const char* BASH_TESTS_DIR = "ref/bash/tests";

//==============================================================================
// Test Info
//==============================================================================

struct BashOfficialTestInfo {
    std::string tests_path;     // e.g. "ref/bash/tests/arith.tests"
    std::string right_path;     // e.g. "ref/bash/tests/arith.right"
    std::string test_name;      // e.g. "arith"

    friend std::ostream& operator<<(std::ostream& os, const BashOfficialTestInfo& info) {
        return os << info.test_name;
    }
};

//==============================================================================
// Helpers
//==============================================================================

static bool file_exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (f) { fclose(f); return true; }
    return false;
}

static char* read_file_contents(const char* path) {
    FILE* file = fopen(path, "r");
    if (!file) return nullptr;

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(file_size + 1);
    if (!content) { fclose(file); return nullptr; }

    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    return content;
}

static void trim_trailing_whitespace(char* str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' ||
                       str[len - 1] == ' '  || str[len - 1] == '\t')) {
        str[--len] = '\0';
    }
}

// Filter out Lambda's internal log/banner lines from captured output.
// These are stderr lines like "23:02:23 [NOTE]  ..." that Lambda emits
// in debug builds but are not part of the bash script's output.
static std::string filter_lambda_noise(const std::string& output) {
    std::string result;
    result.reserve(output.size());

    size_t pos = 0;
    while (pos < output.size()) {
        size_t eol = output.find('\n', pos);
        if (eol == std::string::npos) eol = output.size();

        std::string line = output.substr(pos, eol - pos);

        // Skip lines matching Lambda's log format: "HH:MM:SS [LEVEL]"
        bool skip = false;
        if (line.size() >= 14 &&
            line[2] == ':' && line[5] == ':' && line[9] == '[') {
            // Matches timestamp pattern like "23:02:23 [NOTE]"
            skip = true;
        }

        if (!skip) {
            result += line;
            if (eol < output.size()) result += '\n';
        }

        pos = (eol < output.size()) ? eol + 1 : output.size();
    }

    return result;
}

// Build test name from .tests filename: "arith.tests" -> "arith"
static std::string make_test_name(const std::string& filename) {
    std::string name = filename;
    size_t dot = name.find(".tests");
    if (dot != std::string::npos) {
        name = name.substr(0, dot);
    }
    // Replace hyphens with underscores for GTest compatibility
    for (char& c : name) {
        if (c == '-') c = '_';
        else if (!isalnum(c) && c != '_') c = '_';
    }
    return name;
}

//==============================================================================
// Test Discovery
//==============================================================================

static std::vector<BashOfficialTestInfo> discover_bash_tests() {
    std::vector<BashOfficialTestInfo> tests;

    std::string tests_dir = BASH_TESTS_DIR;

#ifdef _WIN32
    std::string search = tests_dir + "\\*.tests";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return tests;

    do {
        std::string fname = fd.cFileName;
        std::string tests_path = tests_dir + "/" + fname;

        // Derive .right filename: strip .tests, add .right
        std::string base = fname.substr(0, fname.size() - 6); // strip ".tests"
        std::string right_path = tests_dir + "/" + base + ".right";

        if (file_exists(right_path)) {
            BashOfficialTestInfo info;
            info.tests_path = tests_path;
            info.right_path = right_path;
            info.test_name = make_test_name(fname);
            tests.push_back(info);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* dir = opendir(tests_dir.c_str());
    if (!dir) return tests;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fname = entry->d_name;

        // Only .tests files
        if (fname.size() < 7 || fname.substr(fname.size() - 6) != ".tests")
            continue;

        std::string tests_path = tests_dir + "/" + fname;

        // Derive .right filename
        std::string base = fname.substr(0, fname.size() - 6);
        std::string right_path = tests_dir + "/" + base + ".right";

        if (file_exists(right_path)) {
            BashOfficialTestInfo info;
            info.tests_path = tests_path;
            info.right_path = right_path;
            info.test_name = make_test_name(fname);
            tests.push_back(info);
        }
    }
    closedir(dir);
#endif

    std::sort(tests.begin(), tests.end(),
              [](const BashOfficialTestInfo& a, const BashOfficialTestInfo& b) {
                  return a.test_name < b.test_name;
              });

    return tests;
}

//==============================================================================
// Execute a single bash test via lambda.exe
//==============================================================================

// Per-test timeout in seconds (some tests may hang on interactive features)
static const int TEST_TIMEOUT_SECONDS = 10;

#ifndef _WIN32
#include <signal.h>
#include <sys/select.h>

static std::string execute_bash_test(const std::string& tests_path) {
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) return "";

    std::string abs_tests_dir = std::string(cwd) + "/" + BASH_TESTS_DIR;
    std::string abs_lambda = std::string(cwd) + "/" + LAMBDA_EXE;
    std::string script_name = tests_path.substr(tests_path.find_last_of('/') + 1);

    // Use fork/exec with a timeout to prevent hanging tests
    int pipefd[2];
    if (pipe(pipefd) == -1) return "";

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return "";
    }

    if (pid == 0) {
        // Child process — create new process group so we can kill all children
        setpgid(0, 0);

        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // Close stdin to prevent tests from waiting on input
        close(STDIN_FILENO);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull != -1 && devnull != STDIN_FILENO) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        chdir(abs_tests_dir.c_str());

        std::string this_sh = abs_lambda + " bash";
        setenv("THIS_SH", this_sh.c_str(), 1);

        std::string script = "./" + script_name;
        execl(abs_lambda.c_str(), abs_lambda.c_str(), "bash", script.c_str(), nullptr);
        _exit(127);
    }

    // Parent process — set child's process group (race-safe: do in both parent and child)
    setpgid(pid, pid);
    close(pipefd[1]);

    std::string output;
    char buffer[4096];
    bool timed_out = false;

    // Read with timeout using select()
    time_t start = time(nullptr);
    while (true) {
        int elapsed = (int)(time(nullptr) - start);
        if (elapsed >= TEST_TIMEOUT_SECONDS) {
            timed_out = true;
            kill(-pid, SIGKILL);
            break;
        }

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(pipefd[0], &fds);

        struct timeval tv;
        tv.tv_sec = 1;  // check every 1 second
        tv.tv_usec = 0;

        int ret = select(pipefd[0] + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0) {
            ssize_t n = read(pipefd[0], buffer, sizeof(buffer) - 1);
            if (n <= 0) break; // EOF or error
            buffer[n] = '\0';
            output += buffer;

            // Cap output to 1MB to prevent memory issues on infinite-output tests
            if (output.size() > 1024 * 1024) {
                timed_out = true;
                kill(-pid, SIGKILL);
                break;
            }
        }
        // ret == 0 means timeout on select — loop will recheck elapsed
    }

    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    return output;
}

#else
// Windows: use popen with a timeout wrapper
static std::string execute_bash_test(const std::string& tests_path) {
    char cwd[1024];
    if (!_getcwd(cwd, sizeof(cwd))) return "";

    std::string abs_tests_dir = std::string(cwd) + "\\" + BASH_TESTS_DIR;
    std::string abs_lambda = std::string(cwd) + "\\" + LAMBDA_EXE;
    std::string script_name = tests_path.substr(tests_path.find_last_of('/') + 1);

    char command[2048];
    snprintf(command, sizeof(command),
             "cd \"%s\" && set THIS_SH=%s bash && %s bash .\\%s 2>&1",
             abs_tests_dir.c_str(),
             abs_lambda.c_str(),
             abs_lambda.c_str(),
             script_name.c_str());

    FILE* pipe = popen(command, "r");
    if (!pipe) return "";

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    pclose(pipe);

    return output;
}
#endif

//==============================================================================
// Parameterized Test
//==============================================================================

static std::vector<BashOfficialTestInfo> g_bash_tests;

class BashOfficialTest : public ::testing::TestWithParam<BashOfficialTestInfo> {
};

TEST_P(BashOfficialTest, ExecuteAndCompare) {
    const BashOfficialTestInfo& info = GetParam();

    // Check that ref/bash/tests exists
    ASSERT_TRUE(file_exists(info.tests_path))
        << "Test script not found: " << info.tests_path
        << "\nRun setup to clone bash.git: see setup-mac-deps.sh or setup-linux-deps.sh";

    // Execute the test
    std::string raw_output = execute_bash_test(info.tests_path);

    // Filter out Lambda's internal log/banner lines
    std::string actual = filter_lambda_noise(raw_output);

    // Read expected output
    char* expected = read_file_contents(info.right_path.c_str());
    ASSERT_NE(expected, nullptr)
        << "Could not read expected output: " << info.right_path;

    // Trim trailing whitespace from both
    char* actual_cstr = strdup(actual.c_str());
    trim_trailing_whitespace(actual_cstr);
    trim_trailing_whitespace(expected);

    EXPECT_STREQ(expected, actual_cstr)
        << "Output mismatch for: " << info.tests_path;

    free(expected);
    free(actual_cstr);
}

std::string BashOfficialTestNameGenerator(
    const ::testing::TestParamInfo<BashOfficialTestInfo>& info) {
    return info.param.test_name;
}

INSTANTIATE_TEST_SUITE_P(
    Official,
    BashOfficialTest,
    ::testing::ValuesIn(g_bash_tests),
    BashOfficialTestNameGenerator
);

//==============================================================================
// Main
//==============================================================================

int main(int argc, char** argv) {
    g_bash_tests = discover_bash_tests();

    if (g_bash_tests.empty()) {
        printf("WARNING: No bash official tests found in %s\n", BASH_TESTS_DIR);
        printf("Run 'bash setup-mac-deps.sh' or 'bash setup-linux-deps.sh' to clone bash.git\n\n");
    } else {
        printf("Discovered %zu official Bash tests (ref/bash/tests/):\n", g_bash_tests.size());
        for (const auto& t : g_bash_tests) {
            printf("  - %s\n", t.test_name.c_str());
        }
        printf("\n");
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
