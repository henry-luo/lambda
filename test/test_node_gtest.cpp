#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <thread>
#include <atomic>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #define NOHELP
    #define NOMCX
    #include <windows.h>
    #include <process.h>
    #include <io.h>
    #include <direct.h>
    #define getcwd _getcwd
    #define chdir _chdir
    #define unlink _unlink
    #define access _access
    #define popen _popen
    #define pclose _pclose
    #define WEXITSTATUS(status) (status)
    #ifndef F_OK
    #define F_OK 0
    #endif
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <dirent.h>
#endif

// Helper function to execute a JavaScript file with lambda js and capture output
static char* execute_js_script(const char* script_path) {
    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js \"%s\" --no-log", script_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe js \"%s\" --no-log", script_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        fprintf(stderr, "Error: Could not execute command: %s\n", command);
        return nullptr;
    }

    char buffer[1024];
    size_t total_size = 0;
    char* full_output = nullptr;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        size_t len = strlen(buffer);
        char* new_output = (char*)realloc(full_output, total_size + len + 1);
        if (!new_output) {
            free(full_output);
            pclose(pipe);
            return nullptr;
        }
        full_output = new_output;
        strcpy(full_output + total_size, buffer);
        total_size += len;
    }

    int exit_code = pclose(pipe);
    if (WEXITSTATUS(exit_code) != 0) {
        fprintf(stderr, "Error: lambda.exe js exited with code %d for script: %s\n",
                WEXITSTATUS(exit_code), script_path);
        free(full_output);
        return nullptr;
    }

    if (!full_output) {
        return strdup("");
    }
    char* marker = strstr(full_output, "##### Script");
    if (marker) {
        char* result_start = strchr(marker, '\n');
        if (result_start) {
            result_start++;
            char* result = strdup(result_start);
            free(full_output);
            return result;
        }
    }

    return full_output;
}

// Helper function to trim trailing whitespace
static void trim_trailing_whitespace(char* str) {
    if (!str) return;
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

// Helper function to read expected output from file
static char* read_expected_output(const char* expected_file_path) {
    FILE* file = fopen(expected_file_path, "r");
    if (!file) {
        return nullptr;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char* content = (char*)malloc(file_size + 1);
    if (!content) {
        fclose(file);
        return nullptr;
    }

    size_t read_size = fread(content, 1, file_size, file);
    content[read_size] = '\0';
    fclose(file);

    trim_trailing_whitespace(content);
    return content;
}

// ---------------------------------------------------------------------------
// Batch mode infrastructure for js-test-batch
// ---------------------------------------------------------------------------

struct NodeBatchResult {
    std::string output;
    int status;
};

static const size_t NODE_BATCH_CHUNK_SIZE = 50;

static void run_node_sub_batch(
    const std::vector<std::string>& scripts,
    size_t start, size_t end,
    int batch_id,
    std::unordered_map<std::string, NodeBatchResult>& results)
{
    char manifest_path[256];
    snprintf(manifest_path, sizeof(manifest_path), "./temp/node_batch_%d_%d.txt", (int)getpid(), batch_id);
    FILE* manifest = fopen(manifest_path, "w");
    if (!manifest) return;

    for (size_t i = start; i < end; i++) {
        fprintf(manifest, "%s\n", scripts[i].c_str());
    }
    fclose(manifest);

    char command[512];
#ifdef _WIN32
    snprintf(command, sizeof(command), "lambda.exe js-test-batch --timeout=10 < \"%s\"", manifest_path);
#else
    snprintf(command, sizeof(command), "./lambda.exe js-test-batch --timeout=10 < \"%s\"", manifest_path);
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        unlink(manifest_path);
        return;
    }

    char buffer[4096];
    std::string current_script;
    std::string current_output;
    bool in_script = false;

    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        if (buffer[0] == '\x01') {
            if (strncmp(buffer + 1, "BATCH_START ", 12) == 0) {
                current_script = std::string(buffer + 13);
                while (!current_script.empty() &&
                       (current_script.back() == '\n' || current_script.back() == '\r'))
                    current_script.pop_back();
                current_output.clear();
                in_script = true;
            } else if (strncmp(buffer + 1, "BATCH_END ", 10) == 0) {
                int status = atoi(buffer + 11);
                results[current_script] = {current_output, status};
                in_script = false;
            }
        } else if (in_script) {
            current_output += buffer;
        }
    }

    pclose(pipe);
    unlink(manifest_path);
}

static std::unordered_map<std::string, NodeBatchResult> execute_node_batch(
    const std::vector<std::string>& scripts,
    size_t chunk_size = NODE_BATCH_CHUNK_SIZE)
{
    std::unordered_map<std::string, NodeBatchResult> results;
    if (scripts.empty()) return results;

    struct SubBatch { size_t start; size_t end; int id; };
    std::vector<SubBatch> batches;
    int batch_id = 0;
    for (size_t start = 0; start < scripts.size(); start += chunk_size) {
        size_t end = std::min(start + chunk_size, scripts.size());
        batches.push_back({start, end, batch_id++});
    }

    std::vector<std::unordered_map<std::string, NodeBatchResult>> thread_results(batches.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < batches.size(); i++) {
        threads.emplace_back([&, i]() {
            run_node_sub_batch(scripts, batches[i].start, batches[i].end,
                               batches[i].id, thread_results[i]);
        });
    }
    for (auto& t : threads) t.join();

    for (auto& partial : thread_results) {
        for (auto& kv : partial) {
            results[kv.first] = std::move(kv.second);
        }
    }

    return results;
}

// ---------------------------------------------------------------------------
// Dynamic test discovery
// ---------------------------------------------------------------------------

struct NodeTestParam {
    std::string script_path;
    std::string expected_path;
    std::string test_name;
};

static std::vector<NodeTestParam> discover_node_tests_in_dir(const char* dir_path) {
    std::vector<NodeTestParam> params;

#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.js", dir_path);
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern, &fd);
    if (handle == -1) return params;
    do {
        if (fd.attrib & _A_SUBDIR) continue;
        const char* name = fd.name;
#else
    DIR* dir = opendir(dir_path);
    if (!dir) return params;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) continue;
        const char* name = entry->d_name;
#endif

        size_t len = strlen(name);
        if (len < 4 || strcmp(name + len - 3, ".js") != 0) continue;

        std::string base(name, len - 3);
        std::string dir_str(dir_path);

        std::string txt = dir_str + "/" + base + ".txt";
        if (access(txt.c_str(), F_OK) != 0) continue;

        NodeTestParam p;
        p.script_path  = dir_str + "/" + name;
        p.expected_path = txt;

        p.test_name = base;
        for (auto& c : p.test_name) {
            if (!isalnum((unsigned char)c)) c = '_';
        }

        params.push_back(p);

#ifdef _WIN32
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
#else
    }
    closedir(dir);
#endif

    return params;
}

static std::vector<NodeTestParam> discover_all_node_tests() {
    static const char* dirs[] = {
        "test/node",
    };
    std::vector<NodeTestParam> all;
    for (const char* d : dirs) {
        auto v = discover_node_tests_in_dir(d);
        all.insert(all.end(), v.begin(), v.end());
    }
    std::sort(all.begin(), all.end(),
              [](const NodeTestParam& a, const NodeTestParam& b) {
                  return a.test_name < b.test_name;
              });
    return all;
}

// ---------------------------------------------------------------------------
// Parameterised test — one test case per discovered .js file (batch mode)
// ---------------------------------------------------------------------------

class NodeFileTest : public testing::TestWithParam<NodeTestParam> {
public:
    static std::unordered_map<std::string, NodeBatchResult> batch_results;
    static bool batch_executed;

    static void SetUpTestSuite() {
        if (batch_executed) return;

        auto all = discover_all_node_tests();
        std::vector<std::string> batch_scripts;
        for (const auto& t : all) {
            batch_scripts.push_back(t.script_path);
        }

        if (!batch_scripts.empty()) {
            batch_results = execute_node_batch(batch_scripts);
        }
        batch_executed = true;
    }
};

std::unordered_map<std::string, NodeBatchResult> NodeFileTest::batch_results;
bool NodeFileTest::batch_executed = false;

TEST_P(NodeFileTest, Run) {
    const auto& p = GetParam();

    auto it = batch_results.find(p.script_path);
    if (it == batch_results.end()) {
        // fallback to single-script execution
        char* actual_output = execute_js_script(p.script_path.c_str());
        ASSERT_NE(actual_output, nullptr) << "Could not execute: " << p.script_path;
        trim_trailing_whitespace(actual_output);

        char* expected_output = read_expected_output(p.expected_path.c_str());
        ASSERT_NE(expected_output, nullptr) << "Could not read: " << p.expected_path;

        ASSERT_STREQ(expected_output, actual_output)
            << "Output mismatch for: " << p.script_path;

        free(expected_output);
        free(actual_output);
        return;
    }

    const NodeBatchResult& br = it->second;
    ASSERT_EQ(br.status, 0) << "Script execution failed (exit " << br.status << "): " << p.script_path;

    std::string actual = br.output;
    const char* marker = strstr(actual.c_str(), "##### Script");
    if (marker) {
        const char* nl = strchr(marker, '\n');
        if (nl) actual = std::string(nl + 1);
    }

    while (!actual.empty() && isspace((unsigned char)actual.back()))
        actual.pop_back();

    char* expected_output = read_expected_output(p.expected_path.c_str());
    ASSERT_NE(expected_output, nullptr) << "Could not read expected: " << p.expected_path;

    ASSERT_STREQ(expected_output, actual.c_str())
        << "Output mismatch for: " << p.script_path;

    free(expected_output);
}

INSTANTIATE_TEST_SUITE_P(
    NodeModuleTests,
    NodeFileTest,
    testing::ValuesIn(discover_all_node_tests()),
    [](const testing::TestParamInfo<NodeTestParam>& info) {
        return info.param.test_name;
    });

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
