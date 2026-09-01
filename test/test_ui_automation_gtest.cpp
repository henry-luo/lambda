/**
 * GTest runner for UI Automation Tests
 *
 * Loads manifest-owned HTML/JSON fixtures from test/ui/ and test/view/ and
 * runs each as a separate GTest case via `./lambda.exe view <html> --event-file <json>`.
 *
 * Exit code convention (enforced by radiant/window.cpp):
 *   0 = all assertions passed
 *   1 = one or more assertions failed
 *
 * The event sim prints a summary to stderr:
 *   ========================================
 *    Assertions: N passed, M failed
 *    Result: PASS / FAIL
 *   ========================================
 *
 * Usage:
 *   ./test/test_ui_automation_gtest.exe
 *   ./test/test_ui_automation_gtest.exe --gtest_filter=UIAutomation.*click*
 *   ./test/test_ui_automation_gtest.exe --suite dom --test smoke
 *   ./test/test_ui_automation_gtest.exe --suite native-gui --native-gui
 *
 * Note: Tests require a graphical display. On headless CI, set the
 *       environment variable DISPLAY or use xvfb-run.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>

#include "../lambda/input/input.hpp"
#include "../lambda/input/input-parsers.h"
#include "../lambda/core/mark_reader.hpp"
#include "../lib/mempool.h"

extern "C" {
#include "../lib/shell.h"
}

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #include <windows.h>
    #include <io.h>
    #include <direct.h>
    #define popen  _popen
    #define pclose _pclose
    #define WEXITSTATUS(s) (s)
    #define LAMBDA_EXE "lambda.exe"
    #define UI_TESTS_DIR "test\\ui"
    #define PATH_SEP "\\"
#else
    #include <unistd.h>
    #include <dirent.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #ifdef __APPLE__
        #include <sys/sysctl.h>
    #endif
    #define LAMBDA_EXE "./lambda.exe"
    #define UI_TESTS_DIR "test/ui"
    #define PATH_SEP "/"
#endif

// ============================================================================
// Test info struct
// ============================================================================

struct UiTestInfo {
    std::string html_path;   // e.g. "test/ui/test_click_text.html"
    std::string json_path;   // e.g. "test/ui/test_click_text.json"
    std::string test_name;   // e.g. "test_click_text"
    std::string suite;       // manifest-owned suite
    std::vector<std::string> tags;
    std::vector<std::string> output_contains;
    std::vector<std::string> output_not_contains;
    bool skip_headless;      // requires native GUI window (e.g. WKWebView tests)
    int estimated_wait_ms;   // explicit event waits, used only for launch scheduling
    int explicit_wait_count;
    int assertion_count;

    friend std::ostream& operator<<(std::ostream& os, const UiTestInfo& info) {
        return os << info.test_name;
    }
};

struct UiSuiteSpec {
    std::string name;
    std::vector<std::string> globs;
    std::vector<std::string> excludes;
    std::string default_page;
    std::vector<std::string> tags;
    bool headless = true;
};

struct UiManifest {
    std::string font_dir = "test/layout/data/font";
    int jobs = 0;
    std::vector<UiSuiteSpec> suites;
};

static UiManifest g_ui_manifest;
static std::string g_ui_discovery_error;
static std::mutex g_ui_result_parse_mutex;

// ============================================================================
// Test discovery
// ============================================================================

static bool file_exists(const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (f) { fclose(f); return true; }
    return false;
}

static bool ui_directory_exists(const std::string& path) {
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat st = {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static bool ui_safe_repo_path(const std::string& path) {
    if (path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) return false;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        if (path.substr(start, end - start) == "..") return false;
        if (end == path.size()) break;
        start = end + 1;
    }
    return true;
}

static char* read_json_text(const std::string& json_path, size_t limit) {
    FILE* file = fopen(json_path.c_str(), "rb");
    if (!file) return nullptr;

    size_t capacity = limit;
    if (capacity == 0) {
        if (fseek(file, 0, SEEK_END) != 0) {
            fclose(file);
            return nullptr;
        }
        long file_size = ftell(file);
        if (file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
            fclose(file);
            return nullptr;
        }
        capacity = (size_t)file_size;
    }

    char* text = (char*)malloc(capacity + 1);
    if (!text) {
        fclose(file);
        return nullptr;
    }
    size_t length = fread(text, 1, capacity, file);
    fclose(file);
    text[length] = '\0';
    return text;
}

typedef struct UiWaitStats {
    int total_ms;
    int count;
} UiWaitStats;

// Match manifest paths without allowing a single '*' to cross a directory.
// '**' is intentionally supported for future suite roots.
static bool ui_manifest_match(const char* pattern, const char* text) {
    if (!pattern || !text) return false;
    if (*pattern == '\0') return *text == '\0';
    if (*pattern == '*') {
        bool recursive = pattern[1] == '*';
        const char* next = pattern + (recursive ? 2 : 1);
        if (ui_manifest_match(next, text)) return true;
        if (*text == '\0' || (!recursive && *text == '/')) return false;
        return ui_manifest_match(pattern, text + 1);
    }
    if (*pattern == '?') return *text && *text != '/' && ui_manifest_match(pattern + 1, text + 1);
    return *pattern == *text && ui_manifest_match(pattern + 1, text + 1);
}

static bool ui_read_string_array(ItemReader value, std::vector<std::string>& out) {
    if (!value.isArray() && !value.isList()) return false;
    ArrayReader array = value.asArray();
    for (int64_t i = 0; i < array.length(); i++) {
        ItemReader item = array.get(i);
        if (!item.isString() || !item.cstring()) return false;
        out.emplace_back(item.cstring());
    }
    return true;
}

static void ui_dispose_json_input(Pool* pool, Input* input) {
    if (input) input_release_auxiliary_resources(input);
    if (pool) pool_destroy(pool);
}

static bool ui_parse_json_file(const std::string& path, Item* root,
                               Pool** out_pool, Input** out_input) {
    *root = Item{.item = ITEM_NULL};
    *out_pool = nullptr;
    *out_input = nullptr;
    char* text = read_json_text(path, 0);
    if (!text) return false;
    Pool* pool = pool_create();
    Input* input = pool ? Input::create(pool, nullptr) : nullptr;
    bool ok = false;
    if (input) *root = parse_json_to_item_strict(input, text, &ok);
    free(text);
    if (!ok || !input) {
        ui_dispose_json_input(pool, input);
        return false;
    }
    *out_pool = pool;
    *out_input = input;
    return true;
}

static void ui_ensure_result_directory() {
#ifdef _WIN32
    _mkdir("temp");
    _mkdir("temp\\ui-test-results");
#else
    mkdir("./temp", 0755);
    mkdir("./temp/ui-test-results", 0755);
#endif
}

static bool ui_parse_manifest(UiManifest* manifest) {
    Item root_item;
    Pool* pool = nullptr;
    Input* input = nullptr;
    if (!ui_parse_json_file("test/ui/ui_test_manifest.json", &root_item, &pool, &input)) {
        g_ui_discovery_error = "manifest is missing or invalid JSON: test/ui/ui_test_manifest.json";
        return false;
    }
    MarkReader doc(root_item);
    ItemReader root = doc.getRoot();
    if (!root.isMap()) {
        g_ui_discovery_error = "manifest root must be an object";
        ui_dispose_json_input(pool, input);
        return false;
    }
    MapReader root_map = root.asMap();
    ItemReader version = root_map.get("version");
    if (!version.isInt() || version.asInt() != 1) {
        g_ui_discovery_error = "manifest version must be 1";
        ui_dispose_json_input(pool, input);
        return false;
    }
    ItemReader defaults = root_map.get("defaults");
    if (defaults.isMap()) {
        MapReader defaults_map = defaults.asMap();
        ItemReader font_dir = defaults_map.get("font_dir");
        if (!font_dir.isString() || !font_dir.cstring() ||
            !ui_safe_repo_path(font_dir.cstring())) {
            g_ui_discovery_error = "manifest default font_dir must be a safe repository path";
            ui_dispose_json_input(pool, input);
            return false;
        }
        manifest->font_dir = font_dir.cstring();
        ItemReader jobs = defaults_map.get("jobs");
        if (!jobs.isNull() && (!jobs.isInt() || jobs.asInt() < 0)) {
            g_ui_discovery_error = "manifest default jobs must be a non-negative integer";
            ui_dispose_json_input(pool, input);
            return false;
        }
        if (jobs.isInt()) manifest->jobs = jobs.asInt32();
    } else if (!defaults.isNull()) {
        g_ui_discovery_error = "manifest defaults must be an object";
        ui_dispose_json_input(pool, input);
        return false;
    }
    if (!ui_directory_exists(manifest->font_dir)) {
        g_ui_discovery_error = "manifest font directory does not exist: " + manifest->font_dir;
        ui_dispose_json_input(pool, input);
        return false;
    }
    ItemReader suites = root_map.get("suites");
    if (!suites.isArray() && !suites.isList()) {
        g_ui_discovery_error = "manifest suites must be an array";
        ui_dispose_json_input(pool, input);
        return false;
    }
    ArrayReader suite_array = suites.asArray();
    for (int64_t i = 0; i < suite_array.length(); i++) {
        ItemReader suite_item = suite_array.get(i);
        if (!suite_item.isMap()) {
            g_ui_discovery_error = "manifest suite entry must be an object";
            ui_dispose_json_input(pool, input);
            return false;
        }
        MapReader suite_map = suite_item.asMap();
        UiSuiteSpec suite;
        ItemReader name = suite_map.get("name");
        if (!name.isString() || !name.cstring() || !*name.cstring()) {
            g_ui_discovery_error = "manifest suite is missing a name";
            ui_dispose_json_input(pool, input);
            return false;
        }
        suite.name = name.cstring();
        for (const UiSuiteSpec& prior : manifest->suites) {
            if (prior.name == suite.name) {
                g_ui_discovery_error = "manifest contains duplicate suite: " + suite.name;
                ui_dispose_json_input(pool, input);
                return false;
            }
        }
        if (!ui_read_string_array(suite_map.get("globs"), suite.globs) || suite.globs.empty()) {
            g_ui_discovery_error = "manifest suite has no valid globs: " + suite.name;
            ui_dispose_json_input(pool, input);
            return false;
        }
        for (const std::string& glob : suite.globs) {
            if (!ui_safe_repo_path(glob)) {
                g_ui_discovery_error = "manifest suite has unsafe glob: " + suite.name;
                ui_dispose_json_input(pool, input);
                return false;
            }
        }
        ItemReader exclude = suite_map.get("exclude");
        if (!exclude.isNull() && !ui_read_string_array(exclude, suite.excludes)) {
            g_ui_discovery_error = "manifest suite has invalid exclude globs: " + suite.name;
            ui_dispose_json_input(pool, input);
            return false;
        }
        for (const std::string& glob : suite.excludes) {
            if (!ui_safe_repo_path(glob)) {
                g_ui_discovery_error = "manifest suite has unsafe exclude glob: " + suite.name;
                ui_dispose_json_input(pool, input);
                return false;
            }
        }
        ItemReader default_page = suite_map.get("default_page");
        if (!default_page.isNull() &&
            (!default_page.isString() || !default_page.cstring())) {
            g_ui_discovery_error = "manifest suite default_page must be a string: " + suite.name;
            ui_dispose_json_input(pool, input);
            return false;
        }
        if (default_page.isString() && default_page.cstring()) suite.default_page = default_page.cstring();
        ItemReader headless = suite_map.get("headless");
        if (!headless.isNull() && !headless.isBool()) {
            g_ui_discovery_error = "manifest suite headless must be boolean: " + suite.name;
            ui_dispose_json_input(pool, input);
            return false;
        }
        if (headless.isBool()) suite.headless = headless.asBool();
        ItemReader tags = suite_map.get("tags");
        if (!tags.isNull() && !ui_read_string_array(tags, suite.tags)) {
            g_ui_discovery_error = "manifest suite has invalid tags: " + suite.name;
            ui_dispose_json_input(pool, input);
            return false;
        }
        if (!suite.default_page.empty() &&
            (!ui_safe_repo_path(suite.default_page) || !file_exists(suite.default_page))) {
            g_ui_discovery_error = "manifest default page does not exist: " + suite.default_page;
            ui_dispose_json_input(pool, input);
            return false;
        }
        manifest->suites.push_back(suite);
    }
    ui_dispose_json_input(pool, input);
    return !manifest->suites.empty();
}

static void ui_collect_json_files(const std::string& directory,
                                  std::vector<std::string>& files) {
#ifdef _WIN32
    std::string pattern = directory + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const char* name = fd.cFileName;
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
        std::string path = directory + "\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ui_collect_json_files(path, files);
        else if (path.size() > 5 && path.substr(path.size() - 5) == ".json") {
            for (char& c : path) if (c == '\\') c = '/';
            files.push_back(path);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* dir = opendir(directory.c_str());
    if (!dir) return;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        std::string path = directory + "/" + entry->d_name;
        struct stat st = {};
        if (stat(path.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) ui_collect_json_files(path, files);
        else if (path.size() > 5 && path.substr(path.size() - 5) == ".json") files.push_back(path);
    }
    closedir(dir);
#endif
}

static std::string ui_fixture_id(const std::string& path) {
    std::string id = path;
    if (id.size() > 5) id.resize(id.size() - 5);
    const char* roots[] = {"test/ui/", "test/view/"};
    for (const char* root : roots) {
        size_t pos = id.find(root);
        if (pos == 0) { id.erase(0, strlen(root)); break; }
    }
    for (char& c : id) if (c == '/' || c == '-' || c == '.' || c == ' ') c = '_';
    return id;
}

static bool ui_load_fixture(const std::string& json_path, const UiSuiteSpec& suite,
                            UiTestInfo* info) {
    Item root_item;
    Pool* pool = nullptr;
    Input* input = nullptr;
    if (!ui_parse_json_file(json_path, &root_item, &pool, &input)) {
        g_ui_discovery_error = "invalid JSON fixture: " + json_path;
        return false;
    }
    MarkReader doc(root_item);
    ItemReader root = doc.getRoot();
    if (!root.isMap()) {
        g_ui_discovery_error = "fixture root must be an object: " + json_path;
        ui_dispose_json_input(pool, input);
        return false;
    }
    MapReader root_map = root.asMap();
    ItemReader events_item = root_map.get("events");
    if (!events_item.isArray() && !events_item.isList()) {
        g_ui_discovery_error = "fixture is missing an events array: " + json_path;
        ui_dispose_json_input(pool, input);
        return false;
    }
    std::string html_path;
    ItemReader html = root_map.get("html");
    if (!html.isNull() && (!html.isString() || !html.cstring())) {
        g_ui_discovery_error = "fixture html must be a string: " + json_path;
        ui_dispose_json_input(pool, input);
        return false;
    }
    if (html.isString() && html.cstring()) html_path = html.cstring();
    if (html_path.empty() && !suite.default_page.empty()) html_path = suite.default_page;
    if (html_path.empty()) {
        html_path = json_path.substr(0, json_path.size() - 5) + ".html";
    }
    if (!ui_safe_repo_path(html_path) || !file_exists(html_path)) {
        g_ui_discovery_error = "fixture page does not exist: " + json_path + " -> " + html_path;
        ui_dispose_json_input(pool, input);
        return false;
    }
    info->html_path = html_path;
    info->json_path = json_path;
    info->test_name = ui_fixture_id(json_path);
    info->suite = suite.name;
    info->tags = suite.tags;
    info->skip_headless = !suite.headless;
    ItemReader skip_headless = root_map.get("skip_headless");
    if (!skip_headless.isNull() && !skip_headless.isBool()) {
        g_ui_discovery_error = "fixture skip_headless must be boolean: " + json_path;
        ui_dispose_json_input(pool, input);
        return false;
    }
    if (skip_headless.isBool() && skip_headless.asBool()) info->skip_headless = true;
    ItemReader assertions = root_map.get("assertions");
    if (!assertions.isNull()) {
        if (!assertions.isArray() && !assertions.isList()) {
            g_ui_discovery_error = "fixture assertions must be an array: " + json_path;
            ui_dispose_json_input(pool, input);
            return false;
        }
        ArrayReader assertion_array = assertions.asArray();
        for (int64_t i = 0; i < assertion_array.length(); i++) {
            ItemReader clause_item = assertion_array.get(i);
            if (!clause_item.isMap()) {
                g_ui_discovery_error = "fixture assertion clause must be an object: " + json_path;
                ui_dispose_json_input(pool, input);
                return false;
            }
            MapReader clause = clause_item.asMap();
            ItemReader contains = clause.get("output_contains");
            ItemReader not_contains = clause.get("output_not_contains");
            bool has_contains = contains.isString() && contains.cstring() && *contains.cstring();
            bool has_not_contains = not_contains.isString() && not_contains.cstring() && *not_contains.cstring();
            if (has_contains == has_not_contains) {
                g_ui_discovery_error = "fixture assertion clause must contain exactly one output predicate: " + json_path;
                ui_dispose_json_input(pool, input);
                return false;
            }
            if (has_contains) info->output_contains.emplace_back(contains.cstring());
            else info->output_not_contains.emplace_back(not_contains.cstring());
        }
    }
    UiWaitStats stats = {0, 0};
    ArrayReader events = events_item.asArray();
    for (int64_t i = 0; i < events.length(); i++) {
        ItemReader event_item = events.get(i);
        if (!event_item.isMap()) {
            g_ui_discovery_error = "fixture event must be an object: " + json_path;
            ui_dispose_json_input(pool, input);
            return false;
        }
        MapReader event = event_item.asMap();
        ItemReader type_item = event.get("type");
        if (!type_item.isString() || !type_item.cstring()) {
            g_ui_discovery_error = "fixture event is missing a type: " + json_path;
            ui_dispose_json_input(pool, input);
            return false;
        }
        const char* type = type_item.cstring();
        bool assertion = strncmp(type, "assert_", 7) == 0;
        if (strcmp(type, "wait") == 0) {
            stats.count++;
            ItemReader ms = event.get("ms");
            if (ms.isNumber() && ms.asInt() > 0) stats.total_ms += ms.asInt32();
        }
        if (assertion) info->assertion_count++;
    }
    info->assertion_count += (int)info->output_contains.size() + (int)info->output_not_contains.size();
    if (info->assertion_count == 0) {
        g_ui_discovery_error = "fixture has zero assertions: " + json_path;
        ui_dispose_json_input(pool, input);
        return false;
    }
    info->estimated_wait_ms = stats.total_ms;
    info->explicit_wait_count = stats.count;
    ui_dispose_json_input(pool, input);
    return true;
}

static std::string sanitize_gtest_param_name(const std::string& test_name);

static std::vector<UiTestInfo> discover_ui_tests() {
    std::vector<UiTestInfo> tests;
    g_ui_manifest = UiManifest();
    if (!ui_parse_manifest(&g_ui_manifest)) return tests;
    std::vector<std::string> files;
    ui_collect_json_files("test/ui", files);
    ui_collect_json_files("test/view", files);
    std::sort(files.begin(), files.end());
    for (const std::string& path : files) {
        if (path == "test/ui/ui_test_manifest.json") continue;
        const UiSuiteSpec* owner = nullptr;
        int owner_count = 0;
        for (const UiSuiteSpec& suite : g_ui_manifest.suites) {
            bool included = false;
            for (const std::string& glob : suite.globs) {
                if (ui_manifest_match(glob.c_str(), path.c_str())) { included = true; break; }
            }
            if (!included) continue;
            for (const std::string& exclude : suite.excludes) {
                if (ui_manifest_match(exclude.c_str(), path.c_str())) { included = false; break; }
            }
            if (included) { owner = &suite; owner_count++; }
        }
        if (owner_count != 1) {
            g_ui_discovery_error = owner_count == 0
                ? "fixture has no manifest owner: " + path
                : "fixture has duplicate manifest owners: " + path;
            tests.clear();
            return tests;
        }
        UiTestInfo info = {};
        info.assertion_count = 0;
        if (!ui_load_fixture(path, *owner, &info)) {
            tests.clear();
            return tests;
        }
        for (const UiTestInfo& prior : tests) {
            if (prior.test_name == info.test_name) {
                g_ui_discovery_error = "duplicate UI test id: " + info.test_name;
                tests.clear();
                return tests;
            }
            if (sanitize_gtest_param_name(prior.test_name) == sanitize_gtest_param_name(info.test_name)) {
                g_ui_discovery_error = "duplicate sanitized GTest name: " + info.test_name;
                tests.clear();
                return tests;
            }
        }
        tests.push_back(info);
    }
    return tests;
}

// Keep static GTest registration side-effect free.  Full JSON parsing is
// intentionally deferred until main(), after the test process has initialized
// its runtime and logging state.
static std::vector<UiTestInfo> ui_discover_paths_only() {
    std::vector<UiTestInfo> tests;
    std::vector<std::string> files;
    ui_collect_json_files("test/ui", files);
    ui_collect_json_files("test/view", files);
    std::sort(files.begin(), files.end());
    for (const std::string& path : files) {
        if (path == "test/ui/ui_test_manifest.json") continue;
        UiTestInfo info = {};
        info.json_path = path;
        info.test_name = ui_fixture_id(path);
        tests.push_back(info);
    }
    return tests;
}

// ============================================================================
// Global test list (populated once in main)
// ============================================================================

static std::vector<UiTestInfo> g_ui_tests = ui_discover_paths_only();
static std::string g_ui_suite_filter;
static std::string g_ui_tag_filter;
static std::string g_ui_exclude_tag_filter;
static std::string g_ui_test_filter;
static bool g_ui_allow_native_gui = false;

static std::string sanitize_gtest_param_name(const std::string& test_name) {
    std::string name = test_name;
    for (char& c : name) {
        if (c == '-' || c == '.' || c == ' ') c = '_';
    }
    return name;
}

static bool gtest_wildcard_match(const char* pattern, const char* text) {
    const char* star = nullptr;
    const char* retry_text = nullptr;
    while (*text) {
        if (*pattern == '?' || *pattern == *text) {
            pattern++;
            text++;
        } else if (*pattern == '*') {
            star = pattern++;
            retry_text = text;
        } else if (star) {
            pattern = star + 1;
            text = ++retry_text;
        } else {
            return false;
        }
    }
    while (*pattern == '*') pattern++;
    return *pattern == '\0';
}

static bool gtest_pattern_list_matches(const std::string& patterns, const std::string& name) {
    size_t start = 0;
    while (start <= patterns.size()) {
        size_t end = patterns.find(':', start);
        if (end == std::string::npos) end = patterns.size();
        if (end > start) {
            std::string pattern = patterns.substr(start, end - start);
            if (gtest_wildcard_match(pattern.c_str(), name.c_str())) return true;
        }
        if (end == patterns.size()) break;
        start = end + 1;
    }
    return false;
}

static bool gtest_filter_matches_ui_test(const UiTestInfo& info, const std::string& filter) {
    std::string positive = filter.empty() ? "*" : filter;
    std::string negative;
    size_t dash = positive.find('-');
    if (dash != std::string::npos) {
        negative = positive.substr(dash + 1);
        positive = positive.substr(0, dash);
        if (positive.empty()) positive = "*";
    }

    std::string full_name = "UIAutomation/UIAutomationTest.RunTest/" + sanitize_gtest_param_name(info.test_name);
    if (!gtest_pattern_list_matches(positive, full_name)) return false;
    if (!negative.empty() && gtest_pattern_list_matches(negative, full_name)) return false;
    return true;
}

static bool ui_filter_list_matches(const std::string& patterns, const std::string& value) {
    if (patterns.empty()) return false;
    size_t start = 0;
    while (start <= patterns.size()) {
        size_t end = patterns.find(',', start);
        if (end == std::string::npos) end = patterns.size();
        if (end > start) {
            std::string pattern = patterns.substr(start, end - start);
            if (pattern == "all") return true;
            if (gtest_wildcard_match(pattern.c_str(), value.c_str())) return true;
            // Keep the directory-independent `--test smoke` form used by the
            // old DOM runner: it selects `dom_smoke` without reintroducing
            // path parsing in Make targets.
            if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos) {
                std::string normalized = pattern;
                for (char& c : normalized) if (c == '-' || c == '/' || c == '.') c = '_';
                if (value == normalized || (value.size() > normalized.size() &&
                    value.compare(value.size() - normalized.size(), normalized.size(), normalized) == 0 &&
                    value[value.size() - normalized.size() - 1] == '_')) return true;
            }
        }
        if (end == patterns.size()) break;
        start = end + 1;
    }
    return false;
}

static bool ui_suite_tag_matches(const UiTestInfo& info) {
    if (!g_ui_suite_filter.empty() && !ui_filter_list_matches(g_ui_suite_filter, info.suite)) return false;
    if (!g_ui_test_filter.empty() && !ui_filter_list_matches(g_ui_test_filter, info.test_name)) return false;
    if (!g_ui_tag_filter.empty()) {
        bool found = false;
        for (const std::string& tag : info.tags) {
            if (ui_filter_list_matches(g_ui_tag_filter, tag)) { found = true; break; }
        }
        if (!found) return false;
    }
    if (!g_ui_exclude_tag_filter.empty()) {
        for (const std::string& tag : info.tags) {
            if (ui_filter_list_matches(g_ui_exclude_tag_filter, tag)) return false;
        }
    }
    return true;
}

// ============================================================================
// Run a single UI test via lambda.exe view
// ============================================================================

struct UiTestResult {
    bool executed = false;
    int exit_code = -1;
    int assertions_passed = 0;
    int assertions_failed = 0;
    long elapsed_ms = 0;
    std::string output;    // combined stdout + stderr
    std::string result_path;
};

static bool ui_read_machine_result(const std::string& path, UiTestResult* result) {
    if (!result) return false;
    std::lock_guard<std::mutex> lock(g_ui_result_parse_mutex);
    Item root_item;
    Pool* pool = nullptr;
    Input* input = nullptr;
    if (!ui_parse_json_file(path, &root_item, &pool, &input)) return false;
    MarkReader doc(root_item);
    ItemReader root = doc.getRoot();
    bool valid = false;
    if (root.isMap()) {
        MapReader map = root.asMap();
        ItemReader version = map.get("version");
        ItemReader events = map.get("events");
        ItemReader result_name = map.get("result");
        ItemReader assertions = map.get("assertions");
        if (version.isInt() && version.asInt() == 1 &&
            events.isNumber() && events.asInt() >= 0 &&
            result_name.isString() && result_name.cstring() &&
            (strcmp(result_name.cstring(), "PASS") == 0 || strcmp(result_name.cstring(), "FAIL") == 0) &&
            assertions.isMap()) {
            MapReader counts = assertions.asMap();
            ItemReader passed = counts.get("passed");
            ItemReader failed = counts.get("failed");
            if (passed.isNumber() && failed.isNumber() && passed.asInt() >= 0 && failed.asInt() >= 0) {
                result->assertions_passed = passed.asInt32();
                result->assertions_failed = failed.asInt32();
                valid = (strcmp(result_name.cstring(), "PASS") == 0) ==
                    (result->assertions_failed == 0);
            }
        }
    }
    ui_dispose_json_input(pool, input);
    return valid;
}

static UiTestResult run_ui_test(const UiTestInfo& info) {
    UiTestResult result;
    result.executed = true;
    auto t0 = std::chrono::steady_clock::now();
    ui_ensure_result_directory();
    std::string result_path = std::string("temp/ui-test-results/") + info.test_name + ".json";
    remove(result_path.c_str());
    result.result_path = result_path;

    // Build command: ./lambda.exe view <html> --event-file <json>
    // The window auto-closes when simulation completes (auto_close=true in EventSimContext).
    // Exit code: 0 = all assertions passed, 1 = one or more failed.
    const char* args[16];
    int arg_count = 0;
    args[arg_count++] = LAMBDA_EXE;
    args[arg_count++] = "view";
    args[arg_count++] = info.html_path.c_str();
    args[arg_count++] = "--event-file";
    args[arg_count++] = info.json_path.c_str();
    args[arg_count++] = "--event-result";
    args[arg_count++] = result_path.c_str();
    if (!info.skip_headless) args[arg_count++] = "--headless";
    args[arg_count++] = "--no-log";
    args[arg_count++] = "--font-dir";
    args[arg_count++] = g_ui_manifest.font_dir.c_str();
    args[arg_count] = NULL;
    ShellOptions options = {0};
    options.merge_stderr = true;
    // Worker threads must launch argv directly; a shell adds process and quoting overhead.
    ShellResult shell_result = shell_exec(LAMBDA_EXE, args, &options);
    if (shell_result.stdout_buf) {
        result.output.assign(shell_result.stdout_buf, shell_result.stdout_len);
    }
    auto t1 = std::chrono::steady_clock::now();
    result.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    result.exit_code = shell_result.exit_code;
    shell_result_free(&shell_result);

    return result;
}

// ============================================================================
// Parallel execution: run all lambda.exe processes upfront
// ============================================================================

static std::vector<UiTestResult> g_ui_results;

static bool ui_gtest_list_only_requested(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gtest_list_tests") == 0 ||
            strcmp(argv[i], "--gtest_list_tests=1") == 0) {
            return true;
        }
    }
    return false;
}

static unsigned long long ui_physical_memory_bytes() {
#ifdef _WIN32
    MEMORYSTATUSEX status = {};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
#elif defined(__APPLE__)
    unsigned long long bytes = 0;
    size_t size = sizeof(bytes);
    return sysctlbyname("hw.memsize", &bytes, &size, NULL, 0) == 0 ? bytes : 0;
#else
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page_size <= 0) return 0;
    return (unsigned long long)pages * (unsigned long long)page_size;
#endif
}

static int ui_memory_job_limit_for(unsigned long long physical_bytes,
                                   int worker_mb) {
    if (worker_mb <= 0) return 1;
    if (physical_bytes == 0) return 1;
    unsigned long long worker_bytes =
        (unsigned long long)worker_mb * 1024ULL * 1024ULL;
    int limit = (int)((physical_bytes / 2ULL) / worker_bytes);
    return limit > 0 ? limit : 1;
}

static int ui_memory_job_limit() {
    // external JS bundles retain source, transpiler state, and JIT pages at
    // the same time; the old 1 GiB estimate allowed concurrent MIR builds to
    // enter an unstable memory-pressure regime before the child could report
    // a normal test failure.
    int worker_mb = 2048;
    const char* env_worker_mb = getenv("LAMBDA_UI_TEST_WORKER_MB");
    if (env_worker_mb && *env_worker_mb) {
        int requested_mb = atoi(env_worker_mb);
        if (requested_mb > 0) worker_mb = requested_mb;
    }
    return ui_memory_job_limit_for(ui_physical_memory_bytes(), worker_mb);
}

static int ui_memory_safe_jobs(int requested_jobs, int memory_job_limit,
                               bool allow_oversubscribe) {
    if (requested_jobs <= 0) requested_jobs = 1;
    if (memory_job_limit <= 0) memory_job_limit = 1;
    // CPU-derived parallelism must not outrun the resident-memory budget of
    // compiler-heavy fixtures unless the caller explicitly accepts that risk.
    return !allow_oversubscribe && requested_jobs > memory_job_limit
        ? memory_job_limit : requested_jobs;
}

TEST(UIAutomationSchedulerTest, DetectsListOnlyBeforeGTestConsumesFlag) {
    char executable[] = "ui-tests";
    char list_flag[] = "--gtest_list_tests";
    char list_value_flag[] = "--gtest_list_tests=1";
    char filter_flag[] = "--gtest_filter=UIAutomation.*";
    char* list_argv[] = {executable, list_flag};
    char* list_value_argv[] = {executable, list_value_flag};
    char* run_argv[] = {executable, filter_flag};

    EXPECT_TRUE(ui_gtest_list_only_requested(2, list_argv));
    EXPECT_TRUE(ui_gtest_list_only_requested(2, list_value_argv));
    EXPECT_FALSE(ui_gtest_list_only_requested(2, run_argv));
}

TEST(UIAutomationSchedulerTest, MemoryBudgetClampsCpuDerivedWorkers) {
    const unsigned long long gib = 1024ULL * 1024ULL * 1024ULL;
    EXPECT_EQ(ui_memory_job_limit_for(16ULL * gib, 1024), 8);
    EXPECT_EQ(ui_memory_job_limit_for(8ULL * gib, 1024), 4);
    EXPECT_EQ(ui_memory_job_limit_for(1ULL * gib, 1024), 1);
    EXPECT_EQ(ui_memory_job_limit_for(32ULL * gib, 2048), 8);
    EXPECT_EQ(ui_memory_job_limit_for(0, 1024), 1);

    EXPECT_EQ(ui_memory_safe_jobs(15, 8, false), 8);
    EXPECT_EQ(ui_memory_safe_jobs(4, 8, false), 4);
    EXPECT_EQ(ui_memory_safe_jobs(15, 8, true), 15);
}

static int compare_ui_indices_by_estimated_wait(const void* left, const void* right) {
    size_t left_index = *(const size_t*)left;
    size_t right_index = *(const size_t*)right;
    int left_wait = g_ui_tests[left_index].estimated_wait_ms;
    int right_wait = g_ui_tests[right_index].estimated_wait_ms;
    if (left_wait < right_wait) return 1;
    if (left_wait > right_wait) return -1;
    return strcmp(g_ui_tests[left_index].test_name.c_str(),
                  g_ui_tests[right_index].test_name.c_str());
}

static void run_ui_tests_parallel(const std::vector<size_t>& indices, int jobs) {
    size_t n = g_ui_tests.size();
    g_ui_results.resize(n);
    if (indices.empty()) return;

    int num_threads = std::min(jobs, (int)indices.size());
    std::atomic<size_t> next_idx{0};
    std::atomic<size_t> completed{0};
    std::mutex progress_mutex;

    auto worker = [&]() {
        while (true) {
            size_t idx = next_idx.fetch_add(1);
            if (idx >= indices.size()) break;
            size_t test_idx = indices[idx];
            g_ui_results[test_idx] = run_ui_test(g_ui_tests[test_idx]);

            const UiTestResult& result = g_ui_results[test_idx];
            size_t done = completed.fetch_add(1) + 1;

            std::lock_guard<std::mutex> lock(progress_mutex);
            std::cout << "  [" << done << "/" << indices.size() << "] "
                      << g_ui_tests[test_idx].test_name << " finished in "
                      << result.elapsed_ms << "ms";
            if (result.exit_code != 0) {
                std::cout << " (exit " << result.exit_code << ")";
            }
            std::cout << std::endl;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        t.join();
    }

    // Lambda's input/name pools are process-global; keep machine-result JSON
    // parsing on the coordinator thread after all child workers have joined.
    // Child execution remains parallel, while the one-parser preflight/result
    // path stays deterministic and avoids allocator contention.
    for (size_t position = 0; position < indices.size(); position++) {
        size_t test_idx = indices[position];
        UiTestResult& result = g_ui_results[test_idx];
        if (!ui_read_machine_result(result.result_path, &result)) {
            result.assertions_failed++;
            result.output += "\nUI runner did not receive a valid machine result: " + result.result_path + "\n";
        }
        for (const std::string& needle : g_ui_tests[test_idx].output_contains) {
            if (result.output.find(needle) != std::string::npos) result.assertions_passed++;
            else {
                result.assertions_failed++;
                result.output += "\nMissing required output: " + needle + "\n";
            }
        }
        for (const std::string& needle : g_ui_tests[test_idx].output_not_contains) {
            if (result.output.find(needle) == std::string::npos) result.assertions_passed++;
            else {
                result.assertions_failed++;
                result.output += "\nForbidden output present: " + needle + "\n";
            }
        }
        int total_assertions = result.assertions_passed + result.assertions_failed;
        std::cout << "  [" << g_ui_tests[test_idx].test_name << "] machine result "
                  << result.assertions_passed << "/" << total_assertions
                  << " assertions passed" << std::endl;
    }
}

// ============================================================================
// Parameterized test fixture
// ============================================================================

class UIAutomationTest : public ::testing::TestWithParam<size_t> {
protected:
    void SetUp() override {
        if (!file_exists(LAMBDA_EXE)) {
            GTEST_SKIP() << "lambda.exe not found - run 'make build' first";
        }
#ifndef _WIN32
        if (access(LAMBDA_EXE, X_OK) != 0) {
            GTEST_SKIP() << "lambda.exe is not executable";
        }
#endif
        if (g_ui_tests.empty()) {
            GTEST_SKIP() << "No UI tests found in " UI_TESTS_DIR;
        }
    }
};

TEST_P(UIAutomationTest, RunTest) {
    size_t idx = GetParam();
    if (idx >= g_ui_tests.size()) {
        GTEST_SKIP() << "No test at index " << idx;
    }
    const UiTestInfo& info = g_ui_tests[idx];

    if (!ui_suite_tag_matches(info)) {
        GTEST_SKIP() << "fixture is outside the selected suite/tag filters";
    }

    SCOPED_TRACE("UI test: " + info.test_name);

    if (info.skip_headless && !g_ui_allow_native_gui) {
        GTEST_SKIP() << info.test_name << " requires native GUI window (skip_headless=true)";
    }

    if (idx >= g_ui_results.size() || !g_ui_results[idx].executed) {
        if (g_ui_results.size() < g_ui_tests.size()) g_ui_results.resize(g_ui_tests.size());
        g_ui_results[idx] = run_ui_test(info);
    }
    const UiTestResult& result = g_ui_results[idx];

    // Always print assertion summary so we can verify tests actually assert
    int total_assertions = result.assertions_passed + result.assertions_failed;
    if (total_assertions > 0) {
        std::cout << "  [" << info.test_name << "] "
                  << result.assertions_passed << "/" << total_assertions
                  << " assertions passed" << std::endl;
    } else {
        std::cout << "  [" << info.test_name << "] WARNING: 0 assertions"
                  << std::endl;
    }

    // Print full output on failure for easier debugging
    if (result.exit_code != 0 || result.assertions_failed > 0) {
        std::cerr << "\n--- Output for " << info.test_name << " ---\n"
                  << result.output
                  << "--- End output ---\n";
    }

    EXPECT_EQ(result.exit_code, 0)
        << info.test_name << " exited with code " << result.exit_code;

    EXPECT_GT(total_assertions, 0)
        << info.test_name << ": test has no assertions - add assert_* events to the JSON";

    if (total_assertions > 0) {
        EXPECT_EQ(result.assertions_failed, 0)
            << info.test_name << ": "
            << result.assertions_failed << " assertion(s) failed, "
            << result.assertions_passed << " passed";
    }
}

// Instantiation: one test case per discovered HTML/JSON pair
INSTANTIATE_TEST_SUITE_P(
    UIAutomation,
    UIAutomationTest,
    ::testing::Range(size_t(0), g_ui_tests.size()),
    [](const ::testing::TestParamInfo<size_t>& info) {
        if (info.param < g_ui_tests.size()) {
            return sanitize_gtest_param_name(g_ui_tests[info.param].test_name);
        }
        return std::string("test_") + std::to_string(info.param);
    }
);

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    bool list_tests_only = ui_gtest_list_only_requested(argc, argv);
    bool preflight_only = false;
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strncmp(arg, "--suite=", 8) == 0) g_ui_suite_filter = arg + 8;
        else if (strcmp(arg, "--suite") == 0 && i + 1 < argc) g_ui_suite_filter = argv[++i];
        else if (strncmp(arg, "--tag=", 6) == 0) g_ui_tag_filter = arg + 6;
        else if (strcmp(arg, "--tag") == 0 && i + 1 < argc) g_ui_tag_filter = argv[++i];
        else if (strncmp(arg, "--exclude-tag=", 14) == 0) g_ui_exclude_tag_filter = arg + 14;
        else if (strcmp(arg, "--exclude-tag") == 0 && i + 1 < argc) g_ui_exclude_tag_filter = argv[++i];
        else if (strncmp(arg, "--test=", 7) == 0) g_ui_test_filter = arg + 7;
        else if (strcmp(arg, "--test") == 0 && i + 1 < argc) g_ui_test_filter = argv[++i];
        else if (strcmp(arg, "--preflight") == 0) preflight_only = true;
        else if (strcmp(arg, "--native-gui") == 0) g_ui_allow_native_gui = true;
    }
    ::testing::InitGoogleTest(&argc, argv);

    std::vector<UiTestInfo> validated_ui_tests = discover_ui_tests();
    if (!g_ui_discovery_error.empty()) {
        std::cerr << "UI manifest preflight failed: " << g_ui_discovery_error << "\n";
        return 2;
    }
    if (validated_ui_tests.size() != g_ui_tests.size()) {
        std::cerr << "UI manifest preflight failed: fixture set changed during validation\n";
        return 2;
    }
    g_ui_tests.swap(validated_ui_tests);

    // GTest listing is metadata-only and must never launch a UI child, but it
    // still uses the same strict manifest/fixture preflight as execution.
    if (list_tests_only) {
        if (::testing::GTEST_FLAG(filter) == "*" &&
            (!g_ui_suite_filter.empty() || !g_ui_tag_filter.empty() ||
             !g_ui_exclude_tag_filter.empty() || !g_ui_test_filter.empty())) {
            std::string list_filter = "UIAutomationSchedulerTest.*";
            for (const UiTestInfo& info : g_ui_tests) {
                if (ui_suite_tag_matches(info)) {
                    list_filter += ":UIAutomation/UIAutomationTest.RunTest/";
                    list_filter += sanitize_gtest_param_name(info.test_name);
                }
            }
            ::testing::GTEST_FLAG(filter) = list_filter;
        }
        return RUN_ALL_TESTS();
    }

    if (preflight_only) {
        std::cout << "UI manifest preflight passed: " << g_ui_tests.size()
                  << " fixture(s) owned and validated\n";
        for (const UiSuiteSpec& suite : g_ui_manifest.suites) {
            int count = 0;
            for (const UiTestInfo& info : g_ui_tests) if (info.suite == suite.name) count++;
            std::cout << "  " << suite.name << ": " << count << " fixture(s)\n";
        }
        return 0;
    }
    if (!g_ui_suite_filter.empty()) {
        bool known_suite = false;
        for (const UiSuiteSpec& suite : g_ui_manifest.suites) {
            if (ui_filter_list_matches(g_ui_suite_filter, suite.name)) {
                known_suite = true;
                break;
            }
        }
        if (!known_suite) {
            std::cerr << "UI suite filter matches no manifest suite: " << g_ui_suite_filter << "\n";
            return 2;
        }
    }
    if (!g_ui_allow_native_gui && !g_ui_suite_filter.empty() &&
        ui_filter_list_matches(g_ui_suite_filter, "native-gui")) {
        std::cerr << "UI native-gui suite requires --native-gui\n";
        return 2;
    }

    // Child startup and event I/O leave CPU gaps, so 1.5x logical CPUs keeps
    // the worker pool busy without encoding a machine-specific job count.
    int cpu_count = (int)std::thread::hardware_concurrency();
    if (cpu_count <= 0) cpu_count = 1;
    int jobs = (cpu_count * 3) / 2;
    if (jobs <= 0) jobs = 1;
    if (g_ui_manifest.jobs > 0) jobs = g_ui_manifest.jobs;
    const char* env_jobs = getenv("LAMBDA_UI_TEST_JOBS");
    if (env_jobs && *env_jobs) {
        jobs = atoi(env_jobs);
        if (jobs <= 0) jobs = 1;
    }
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-j") == 0 || strcmp(argv[i], "--jobs") == 0) && i + 1 < argc) {
            jobs = atoi(argv[++i]);
            if (jobs <= 0) jobs = 1;
        }
    }
    int memory_job_limit = ui_memory_job_limit();
    const char* allow_oversubscribe = getenv("LAMBDA_UI_TEST_ALLOW_OVERSUBSCRIBE");
    bool oversubscribe = allow_oversubscribe &&
        strcmp(allow_oversubscribe, "1") == 0;
    // Editor bundles peak near 0.7 GiB; reserving 1 GiB per child within half
    // of physical RAM prevents the CPU-based pool from causing OOM.
    jobs = ui_memory_safe_jobs(jobs, memory_job_limit, oversubscribe);

    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗\n";
    std::cout << "║     UI Automation Test Suite                             ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║  Runs manifest-owned JSON fixtures via:                  ║\n";
    std::cout << "║    ./lambda.exe view <html> --event-file <json>           ║\n";
    std::cout << "║  Window auto-closes when simulation completes.            ║\n";
    std::cout << "║                                                           ║\n";
    std::cout << "║  Requirements:                                            ║\n";
    std::cout << "║  • lambda.exe built (run 'make build')                    ║\n";
    std::cout << "║  • test/ui/ui_test_manifest.json and declared pages       ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";

    if (g_ui_tests.empty()) {
        std::cerr << "WARNING: No manifest-owned UI fixtures found\n\n";
    } else {
        std::string gtest_filter = ::testing::GTEST_FLAG(filter);
        std::vector<size_t> selected_indices;
        for (size_t idx = 0; idx < g_ui_tests.size(); idx++) {
            const UiTestInfo& info = g_ui_tests[idx];
            if ((g_ui_allow_native_gui || !info.skip_headless) &&
                ui_suite_tag_matches(info) && gtest_filter_matches_ui_test(info, gtest_filter)) {
                selected_indices.push_back(idx);
            }
        }
        // Explicit-wait scenarios sorted late alphabetically were dominating the
        // worker tail; longest-estimated-first minimizes idle workers at shutdown.
        if (selected_indices.size() > 1) {
            qsort(selected_indices.data(), selected_indices.size(), sizeof(size_t),
                  compare_ui_indices_by_estimated_wait);
        }

        int selected_wait_count = 0;
        int selected_wait_ms = 0;
        for (size_t idx : selected_indices) {
            selected_wait_count += g_ui_tests[idx].explicit_wait_count;
            selected_wait_ms += g_ui_tests[idx].estimated_wait_ms;
        }

        std::cout << "Found " << g_ui_tests.size() << " UI test(s), selected "
                  << selected_indices.size() << " for pre-run with "
                  << jobs << " parallel job(s)";
        if (gtest_filter != "*") std::cout << " using filter: " << gtest_filter;
        std::cout << ":\n";
        std::cout << "Runnable explicit JSON waits: " << selected_wait_count
                  << " events, " << selected_wait_ms << " ms aggregate\n";
        for (size_t idx : selected_indices) {
            std::cout << "  • " << g_ui_tests[idx].test_name << "\n";
        }
        std::cout << "\n";

        // Run selected tests in parallel before GTest checks results.
        auto t0 = std::chrono::steady_clock::now();
        run_ui_tests_parallel(selected_indices, jobs);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << selected_indices.size() << " selected test(s) executed in "
                  << ms << " ms (" << jobs << " parallel jobs)\n\n";

        int selected_passed = 0;
        int selected_failed = 0;
        for (size_t idx : selected_indices) {
            const UiTestResult& result = g_ui_results[idx];
            if (result.exit_code == 0 && result.assertions_failed == 0) selected_passed++;
            else selected_failed++;
        }
        std::cout << "UI fixture results: " << selected_passed << " passed, "
                  << selected_failed << " failed\n";

        // The scheduler is the only non-fixture test in this executable.  When
        // no explicit GTest filter was supplied, restrict GTest to the fixtures
        // already selected and pre-run above so unselected parameter instances
        // do not appear as hundreds of misleading skips.
        if (gtest_filter == "*") {
            std::string selected_gtest_filter = "UIAutomationSchedulerTest.*";
            for (size_t idx : selected_indices) {
                selected_gtest_filter += ":UIAutomation/UIAutomationTest.RunTest/";
                selected_gtest_filter += sanitize_gtest_param_name(g_ui_tests[idx].test_name);
            }
            ::testing::GTEST_FLAG(filter) = selected_gtest_filter;
        }
    }

    return RUN_ALL_TESTS();
}
