//==============================================================================
// Bash Transpiler Integration Tests (test/bash/*.sh)
//
// Auto-discovers *.sh / *.txt file pairs from test/bash/. Each test runs
// the .sh script through lambda-jube.exe bash and compares stdout against
// the corresponding .txt expected-output file.
//
// Usage:
//   ./test_bash_run_gtest.exe                                         # all tests
//   ./test_bash_run_gtest.exe --gtest_filter=Bash/BashRunTest.ExecuteAndCompare/source_cmd
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
    #define LAMBDA_EXE "lambda-jube.exe"
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <signal.h>
    #include <sys/select.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #define LAMBDA_EXE "./lambda-jube.exe"
#endif

static const char* BASH_SCRIPT_DIR = "test/bash";
static const int TEST_TIMEOUT_SECONDS = 10;

//==============================================================================
// Test Info
//==============================================================================

struct BashRunTestInfo {
    std::string script_path;    // e.g. "test/bash/source_cmd.sh"
    std::string expected_path;  // e.g. "test/bash/source_cmd.txt"
    std::string test_name;      // e.g. "source_cmd"

    friend std::ostream& operator<<(std::ostream& os, const BashRunTestInfo& info) {
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

static std::string filter_lambda_noise(const std::string& output) {
    std::string result;
    result.reserve(output.size());

    size_t pos = 0;
    while (pos < output.size()) {
        size_t eol = output.find('\n', pos);
        if (eol == std::string::npos) eol = output.size();

        std::string line = output.substr(pos, eol - pos);

        bool skip = false;
        if (line.size() >= 14 &&
            line[2] == ':' && line[5] == ':' && line[9] == '[') {
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

static std::string make_test_name(const std::string& filename) {
    std::string name = filename;
    size_t dot = name.rfind(".sh");
    if (dot != std::string::npos) {
        name = name.substr(0, dot);
    }
    for (char& c : name) {
        if (c == '-') c = '_';
        else if (!isalnum(c) && c != '_') c = '_';
    }
    return name;
}

//==============================================================================
// Test Discovery
//==============================================================================

static std::vector<BashRunTestInfo> discover_bash_run_tests() {
    std::vector<BashRunTestInfo> tests;

    std::string test_dir = BASH_SCRIPT_DIR;

#ifdef _WIN32
    std::string search = test_dir + "\\*.sh";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return tests;

    do {
        std::string fname = fd.cFileName;
        std::string base = fname.substr(0, fname.size() - 3); // strip ".sh"
        std::string expected_path = test_dir + "/" + base + ".txt";

        if (file_exists(expected_path)) {
            BashRunTestInfo info;
            info.script_path = test_dir + "/" + fname;
            info.expected_path = expected_path;
            info.test_name = make_test_name(fname);
            tests.push_back(info);
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    DIR* dir = opendir(test_dir.c_str());
    if (!dir) return tests;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string fname = entry->d_name;

        if (fname.size() < 4 || fname.substr(fname.size() - 3) != ".sh")
            continue;

        std::string base = fname.substr(0, fname.size() - 3);
        std::string expected_path = test_dir + "/" + base + ".txt";

        if (file_exists(expected_path)) {
            BashRunTestInfo info;
            info.script_path = test_dir + "/" + fname;
            info.expected_path = expected_path;
            info.test_name = make_test_name(fname);
            tests.push_back(info);
        }
    }
    closedir(dir);
#endif

    std::sort(tests.begin(), tests.end(),
              [](const BashRunTestInfo& a, const BashRunTestInfo& b) {
                  return a.test_name < b.test_name;
              });

    return tests;
}

//==============================================================================
// Execute a bash script via lambda-jube.exe
//==============================================================================

#ifndef _WIN32

static std::string execute_bash_script(const std::string& script_path) {
    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) return "";

    std::string abs_lambda = std::string(cwd) + "/" + LAMBDA_EXE;

    int pipefd[2];
    if (pipe(pipefd) == -1) return "";

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return "";
    }

    if (pid == 0) {
        setpgid(0, 0);

        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        // Discard stderr (matches original test runner behavior)
        int devnull_err = open("/dev/null", O_WRONLY);
        if (devnull_err != -1) {
            dup2(devnull_err, STDERR_FILENO);
            close(devnull_err);
        }

        close(STDIN_FILENO);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull != -1 && devnull != STDIN_FILENO) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }

        // Run from project root so relative paths in scripts work
        // (e.g. "source test/bash/source/source_lib.sh")
        execl(abs_lambda.c_str(), abs_lambda.c_str(), "bash", script_path.c_str(), nullptr);
        _exit(127);
    }

    setpgid(pid, pid);
    close(pipefd[1]);

    std::string output;
    char buffer[4096];
    bool timed_out = false;

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
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(pipefd[0] + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0) {
            ssize_t n = read(pipefd[0], buffer, sizeof(buffer) - 1);
            if (n <= 0) break;
            buffer[n] = '\0';
            output += buffer;

            if (output.size() > 1024 * 1024) {
                timed_out = true;
                kill(-pid, SIGKILL);
                break;
            }
        }
    }

    close(pipefd[0]);

    int status;
    waitpid(pid, &status, 0);

    if (timed_out) {
        output += "\n[TIMEOUT after " + std::to_string(TEST_TIMEOUT_SECONDS) + "s]";
    }

    return output;
}

#else

static std::string execute_bash_script(const std::string& script_path) {
    char cwd[1024];
    if (!_getcwd(cwd, sizeof(cwd))) return "";

    std::string abs_lambda = std::string(cwd) + "\\" + LAMBDA_EXE;

    char command[2048];
    snprintf(command, sizeof(command), "\"%s\" bash \"%s\" 2>&1",
             abs_lambda.c_str(), script_path.c_str());

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

static std::vector<BashRunTestInfo> g_bash_run_tests;

class BashRunTest : public ::testing::TestWithParam<BashRunTestInfo> {
};

TEST_P(BashRunTest, ExecuteAndCompare) {
    const BashRunTestInfo& info = GetParam();

    ASSERT_TRUE(file_exists(info.script_path))
        << "Test script not found: " << info.script_path;

    std::string raw_output = execute_bash_script(info.script_path);
    std::string actual = filter_lambda_noise(raw_output);

    char* expected = read_file_contents(info.expected_path.c_str());
    ASSERT_NE(expected, nullptr)
        << "Could not read expected output: " << info.expected_path;

    char* actual_cstr = strdup(actual.c_str());
    trim_trailing_whitespace(actual_cstr);
    trim_trailing_whitespace(expected);

    EXPECT_STREQ(expected, actual_cstr)
        << "Output mismatch for: " << info.script_path;

    free(expected);
    free(actual_cstr);
}

std::string BashRunTestNameGenerator(
    const ::testing::TestParamInfo<BashRunTestInfo>& info) {
    return info.param.test_name;
}

INSTANTIATE_TEST_SUITE_P(
    Bash,
    BashRunTest,
    ::testing::ValuesIn(g_bash_run_tests),
    BashRunTestNameGenerator
);

//==============================================================================
// Main
//==============================================================================

int main(int argc, char** argv) {
    g_bash_run_tests = discover_bash_run_tests();

    if (g_bash_run_tests.empty()) {
        printf("WARNING: No bash integration tests found in %s\n", BASH_SCRIPT_DIR);
        printf("Ensure test/bash/*.sh scripts have matching *.txt expected-output files.\n\n");
    } else {
        printf("Discovered %zu Bash integration tests (test/bash/):\n", g_bash_run_tests.size());
        for (const auto& t : g_bash_run_tests) {
            printf("  - %s\n", t.test_name.c_str());
        }
        printf("\n");
    }

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
