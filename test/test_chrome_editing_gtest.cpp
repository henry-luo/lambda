/*
 * Chrome/Blink editing corpus runner.
 *
 * The corpus lives in the sibling lambda-test repo and is exposed here through
 * test/editing -> ../../lambda-test/editing. This runner is deliberately
 * gauge-style: every discovered runnable file must pass unless it is skipped
 * by capability filters, while imported coverage grows phase by phase in CE2.
 */

#include <gtest/gtest.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
    #define popen _popen
    #define pclose _pclose
    #define unlink _unlink
    #define WEXITSTATUS(status) (status)
#else
    #include <dirent.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

static const char* EDITING_ROOT = "test/editing";
static const char* HARNESS_PATH = "test/editing/resources/chrome-editing-harness.js";
static const char* HARNESS_PATCH_PATH = "test/chrome_editing_ce3_harness_patch.js";
static const char* RUNNABLE_PATH = "test/editing/RUNNABLE";
static const char* TEMP_DIR = "temp";
static const char* RESULT_ARTIFACT_PATH = "temp/chrome_editing_results.jsonl";

static bool starts_with(const char* text, const char* prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool ends_with(const std::string& text, const char* suffix) {
    size_t suffix_len = strlen(suffix);
    return text.size() >= suffix_len &&
        text.compare(text.size() - suffix_len, suffix_len, suffix) == 0;
}

static std::string trim_ascii(std::string text) {
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t' ||
           text[start] == '\r' || text[start] == '\n')) {
        start++;
    }
    size_t end = text.size();
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
           text[end - 1] == '\r' || text[end - 1] == '\n')) {
        end--;
    }
    return text.substr(start, end - start);
}

static std::string read_file_contents(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return "";
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        return "";
    }
    std::string result((size_t)sz, '\0');
    size_t read = fread(&result[0], 1, (size_t)sz, f);
    result.resize(read);
    fclose(f);
    return result;
}

static bool file_exists(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return false;
    fclose(f);
    return true;
}

static void write_file_contents(const char* path, const std::string& content) {
    FILE* f = fopen(path, "w");
    if (!f) return;
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
}

static std::string dir_of(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

static std::string extract_attr(const std::string& tag, const char* attr) {
    size_t p = 0;
    size_t attr_len = strlen(attr);
    while (true) {
        p = tag.find(attr, p);
        if (p == std::string::npos) return "";
        bool left_ok = p == 0 || tag[p - 1] == ' ' || tag[p - 1] == '\t' ||
            tag[p - 1] == '\r' || tag[p - 1] == '\n' || tag[p - 1] == '<';
        bool right_ok = p + attr_len >= tag.size() ||
            tag[p + attr_len] == '=' || tag[p + attr_len] == ' ' ||
            tag[p + attr_len] == '\t' || tag[p + attr_len] == '\r' ||
            tag[p + attr_len] == '\n';
        if (left_ok && right_ok) break;
        p += attr_len;
    }
    p += attr_len;
    while (p < tag.size() && (tag[p] == ' ' || tag[p] == '\t' ||
           tag[p] == '\r' || tag[p] == '\n')) {
        p++;
    }
    if (p >= tag.size() || tag[p] != '=') return "";
    p++;
    while (p < tag.size() && (tag[p] == ' ' || tag[p] == '\t' ||
           tag[p] == '\r' || tag[p] == '\n')) {
        p++;
    }
    if (p == std::string::npos) return "";
    if (p >= tag.size()) return "";
    char quote = tag[p];
    if (quote == '"' || quote == '\'') {
        size_t end = tag.find(quote, p + 1);
        if (end == std::string::npos) return "";
        return tag.substr(p + 1, end - p - 1);
    }
    size_t end = p;
    while (end < tag.size() && tag[end] != ' ' && tag[end] != '\t' &&
           tag[end] != '>' && tag[end] != '/') {
        end++;
    }
    return tag.substr(p, end - p);
}

static std::string normalize_relative_path(const std::string& base_dir,
                                           const std::string& rel) {
    std::vector<std::string> parts;
    std::string combined = base_dir + "/" + rel;
    size_t pos = 0;
    while (pos <= combined.size()) {
        size_t slash = combined.find('/', pos);
        if (slash == std::string::npos) slash = combined.size();
        std::string part = combined.substr(pos, slash - pos);
        if (part.empty() || part == ".") {
        } else if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.push_back(part);
        }
        pos = slash + 1;
        if (slash == combined.size()) break;
    }

    std::string out;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i) out += "/";
        out += parts[i];
    }
    return out;
}

static std::string js_string_literal(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += (char)c;
            }
            break;
        }
    }
    out += "\"";
    return out;
}

static std::string json_string_literal(const std::string& value) {
    return js_string_literal(value);
}

static bool contains_text(const std::string& text, const char* needle) {
    return text.find(needle) != std::string::npos;
}

static bool has_tag(const std::vector<std::string>& tags, const char* tag) {
    for (const std::string& candidate : tags) {
        if (candidate == tag) return true;
    }
    return false;
}

static void add_tag(std::vector<std::string>& tags, const char* tag) {
    if (!has_tag(tags, tag)) tags.push_back(tag);
}

static std::string extract_scripts(const std::string& html,
                                   const std::string& html_dir) {
    std::string result;
    size_t pos = 0;
    while (pos < html.size()) {
        size_t tag_start = html.find("<script", pos);
        if (tag_start == std::string::npos) break;
        size_t tag_end = html.find('>', tag_start);
        if (tag_end == std::string::npos) break;

        std::string tag = html.substr(tag_start, tag_end - tag_start + 1);
        std::string src = extract_attr(tag, "src");
        if (!src.empty()) {
            if (ends_with(src, "assert_selection.js")) {
                pos = tag_end + 1;
                continue;
            }
            if (src.find("://") == std::string::npos && src[0] != '/') {
                std::string full = normalize_relative_path(html_dir, src);
                std::string body = read_file_contents(full.c_str());
                if (!body.empty()) {
                    result += "\n// ---- inlined " + src + " ----\n";
                    result += body;
                    result += "\n";
                }
            }
            pos = tag_end + 1;
            continue;
        }

        size_t close = html.find("</script>", tag_end);
        if (close == std::string::npos) break;
        result += html.substr(tag_end + 1, close - tag_end - 1);
        result += "\n";
        pos = close + strlen("</script>");
    }
    return result;
}

static std::string strip_script_elements(const std::string& html) {
    std::string result;
    size_t pos = 0;
    while (pos < html.size()) {
        size_t tag_start = html.find("<script", pos);
        if (tag_start == std::string::npos) {
            result += html.substr(pos);
            break;
        }
        result += html.substr(pos, tag_start - pos);
        size_t tag_end = html.find('>', tag_start);
        if (tag_end == std::string::npos) break;
        size_t close = html.find("</script>", tag_end);
        if (close == std::string::npos) {
            pos = tag_end + 1;
            continue;
        }
        pos = close + strlen("</script>");
    }
    return result;
}

static std::string execute_js_with_doc(const char* js_path,
                                       const char* html_path,
                                       int* exit_code) {
    char command[1200];
    const char* timeout_env = getenv("LAMBDA_CHROME_EDITING_TIMEOUT");
    int timeout_seconds = timeout_env && timeout_env[0] ? atoi(timeout_env) : 0;
#ifdef _WIN32
    snprintf(command, sizeof(command),
             "lambda.exe js \"%s\" --document \"%s\" --no-log 2>&1",
             js_path, html_path);
#else
    if (timeout_seconds > 0) {
        snprintf(command, sizeof(command),
                 "perl -e 'alarm shift; exec @ARGV' %d ./lambda.exe js \"%s\" --document \"%s\" --no-log 2>&1",
                 timeout_seconds, js_path, html_path);
    } else {
        snprintf(command, sizeof(command),
                 "./lambda.exe js \"%s\" --document \"%s\" --no-log 2>&1",
                 js_path, html_path);
    }
#endif

    FILE* pipe = popen(command, "r");
    if (!pipe) {
        *exit_code = -1;
        return "";
    }

    std::string output;
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    int raw = pclose(pipe);
#ifndef _WIN32
    if (WIFEXITED(raw)) {
        *exit_code = WEXITSTATUS(raw);
    } else if (WIFSIGNALED(raw)) {
        *exit_code = 128 + WTERMSIG(raw);
    } else {
        *exit_code = raw;
    }
#else
    *exit_code = WEXITSTATUS(raw);
#endif
    return output;
}

struct ChromeEditingParam {
    std::string html_path;
    std::string rel_path;
    std::string test_name;
    bool skip;
};

static std::string expected_text_path_for(const ChromeEditingParam& p) {
    if (!ends_with(p.html_path, ".html")) return "";
    std::string path = p.html_path.substr(0, p.html_path.size() - 5);
    path += "-expected.txt";
    return path;
}

struct ChromeEditingResult {
    ChromeEditingParam param;
    bool skipped;
    bool failed;
    bool timeout;
    bool abort;
    int pass_count;
    int total_count;
    int exit_code;
    double seconds;
    std::string skip_reason;
    std::string output;
    std::string bucket;
    std::string classifier;
    std::vector<std::string> flavor_tags;
    std::vector<std::string> failures;
};

static std::string top_level_bucket(const std::string& rel_path) {
    size_t slash = rel_path.find('/');
    if (slash == std::string::npos) return "(root)";
    return rel_path.substr(0, slash);
}

static std::vector<std::string> classify_flavors(const std::string& rel_path,
                                                 const std::string& html,
                                                 const std::string& scripts,
                                                 bool has_expected) {
    std::string source = html + "\n" + scripts;
    std::vector<std::string> tags;
    if (contains_text(source, "assert_selection") ||
        contains_text(source, "assertSelection") ||
        contains_text(rel_path, "assert_selection")) {
        add_tag(tags, "assert_selection");
    }
    if (contains_text(source, "runEditingTest") ||
        contains_text(source, "runDumpAsTextEditingTest") ||
        contains_text(source, "editing.js")) {
        add_tag(tags, "editing.js");
    }
    if (contains_text(source, "js-test.js") ||
        contains_text(source, "shouldBe") ||
        contains_text(source, "successfullyParsed")) {
        add_tag(tags, "js-test");
    }
    if (has_expected || contains_text(source, "dumpAsText") ||
        contains_text(source, "dumpAsMarkup")) {
        add_tag(tags, "dump-baseline");
    }
    if (contains_text(source, "dumpAsLayout") ||
        contains_text(source, "dumpAsLayoutWithPixelResults") ||
        starts_with(rel_path.c_str(), "caret/")) {
        add_tag(tags, "visual-layout");
    }
    if (contains_text(source, "eventSender.mouse") ||
        contains_text(source, "eventSender.leapForward") ||
        contains_text(source, "dragMode") ||
        contains_text(rel_path, "drag") ||
        contains_text(rel_path, "mouse")) {
        add_tag(tags, "pointer-drag");
    }
    if (contains_text(source, "clipboard") ||
        contains_text(source, "Clipboard") ||
        contains_text(source, "paste") ||
        contains_text(source, "Paste") ||
        contains_text(source, "copy") ||
        contains_text(source, "Copy") ||
        contains_text(source, "cut") ||
        contains_text(source, "Cut") ||
        starts_with(rel_path.c_str(), "pasteboard/")) {
        add_tag(tags, "clipboard");
    }
    if (contains_text(source, "internals")) {
        add_tag(tags, "internals");
    }
    if (contains_text(source, "shadowRoot") ||
        contains_text(source, "ShadowRoot") ||
        starts_with(rel_path.c_str(), "shadow/")) {
        add_tag(tags, "shadow");
    }
    if (tags.empty()) add_tag(tags, "plain-script");
    return tags;
}

static std::string first_failure_line(const ChromeEditingResult& result) {
    if (!result.failures.empty()) return result.failures[0];
    size_t pos = 0;
    while (pos < result.output.size()) {
        size_t eol = result.output.find('\n', pos);
        if (eol == std::string::npos) eol = result.output.size();
        std::string line = result.output.substr(pos, eol - pos);
        if (!line.empty()) return line;
        pos = eol + 1;
        if (eol == result.output.size()) break;
    }
    if (!result.skip_reason.empty()) return result.skip_reason;
    return "";
}

static std::string classify_failure(const ChromeEditingResult& result) {
    if (result.skipped) {
        return result.classifier.empty() ? "skipped" : result.classifier;
    }
    if (!result.failed) return "passed";
    if (result.timeout) return "timeout";
    if (result.abort) return "process_abort";
    std::string signal = result.output;
    for (const std::string& failure : result.failures) {
        signal += "\n";
        signal += failure;
    }
    if (contains_text(signal, "ReferenceError") ||
        contains_text(signal, " is not defined") ||
        contains_text(signal, "not a function") ||
        contains_text(signal, "Cannot find variable")) {
        return "harness_missing_api";
    }
    if (has_tag(result.flavor_tags, "internals")) return "unsupported_internals";
    if (has_tag(result.flavor_tags, "shadow")) return "unsupported_shadow";
    if (has_tag(result.flavor_tags, "visual-layout")) return "unsupported_layout_visual";
    if (result.total_count == 0) return "no_results";
    if (!result.failures.empty() || result.pass_count != result.total_count) {
        return "assertion_mismatch";
    }
    return "process_exit";
}

static std::string outcome_for_result(const ChromeEditingResult& result) {
    if (result.skipped) return "skip";
    if (result.failed) return "fail";
    return "pass";
}

static std::string result_to_jsonl(const ChromeEditingResult& result) {
    std::string json = "{";
    json += "\"rel_path\":" + json_string_literal(result.param.rel_path);
    json += ",\"bucket\":" + json_string_literal(result.bucket);
    json += ",\"flavor_tags\":[";
    for (size_t i = 0; i < result.flavor_tags.size(); i++) {
        if (i) json += ",";
        json += json_string_literal(result.flavor_tags[i]);
    }
    json += "]";
    json += ",\"outcome\":" + json_string_literal(outcome_for_result(result));
    json += ",\"classifier\":" + json_string_literal(result.classifier);
    json += ",\"pass_count\":" + std::to_string(result.pass_count);
    json += ",\"total_count\":" + std::to_string(result.total_count);
    json += ",\"exit_code\":" + std::to_string(result.exit_code);
    json += ",\"timeout\":" + std::string(result.timeout ? "true" : "false");
    json += ",\"abort\":" + std::string(result.abort ? "true" : "false");
    json += ",\"seconds\":" + std::to_string(result.seconds);
    json += ",\"first_failure_line\":" + json_string_literal(first_failure_line(result));
    json += "}\n";
    return json;
}

struct ChromeEditingCounter {
    std::string name;
    int count;
};

static void bump_counter(std::vector<ChromeEditingCounter>& counters,
                         const std::string& name) {
    for (ChromeEditingCounter& counter : counters) {
        if (counter.name == name) {
            counter.count++;
            return;
        }
    }
    ChromeEditingCounter counter;
    counter.name = name;
    counter.count = 1;
    counters.push_back(counter);
}

static void write_result_artifact(const std::vector<ChromeEditingResult>& results) {
    std::string content;
    for (const ChromeEditingResult& result : results) {
        content += result_to_jsonl(result);
    }
    write_file_contents(RESULT_ARTIFACT_PATH, content);
}

static void print_classifier_summary(const std::vector<ChromeEditingResult>& results) {
    std::vector<ChromeEditingCounter> counters;
    for (const ChromeEditingResult& result : results) {
        bump_counter(counters, result.classifier);
    }
    std::sort(counters.begin(), counters.end(),
              [](const ChromeEditingCounter& a,
                 const ChromeEditingCounter& b) {
                  if (a.count != b.count) return a.count > b.count;
                  return a.name < b.name;
              });
    printf("[----------] Chrome editing result artifact: %s\n",
           RESULT_ARTIFACT_PATH);
    for (const ChromeEditingCounter& counter : counters) {
        printf("[----------]   %-28s %d\n", counter.name.c_str(),
               counter.count);
    }
}

static std::vector<std::string> load_runnable_files() {
    std::vector<std::string> paths;
    std::string content = read_file_contents(RUNNABLE_PATH);
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string::npos) eol = content.size();
        std::string line = trim_ascii(content.substr(pos, eol - pos));
        if (!line.empty() && line[0] != '#') paths.push_back(line);
        pos = eol + 1;
        if (eol == content.size()) break;
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

static bool is_runnable_file(const std::string& rel_path,
                             const std::vector<std::string>& runnable_files) {
    return std::binary_search(runnable_files.begin(), runnable_files.end(),
                              rel_path);
}

static bool run_all_imported_chrome_editing_tests() {
    const char* env = getenv("LAMBDA_CHROME_EDITING_RUN_ALL");
    return env && env[0] && strcmp(env, "0") != 0;
}

static bool keep_chrome_editing_temp_scripts() {
    const char* env = getenv("LAMBDA_CHROME_EDITING_KEEP_TEMP");
    return env && env[0] && strcmp(env, "0") != 0;
}

static bool should_skip_file(const std::string& rel_path,
                             const std::vector<std::string>& runnable_files) {
    if (rel_path.find("/resources/") != std::string::npos) return true;
    if (ends_with(rel_path, "-expected.html")) return true;
    if (ends_with(rel_path, "-expected.txt")) return true;
    if (ends_with(rel_path, "-ref.html")) return true;
    if (rel_path.find("-manual") != std::string::npos) return true;
    if (run_all_imported_chrome_editing_tests()) return false;
    if (!is_runnable_file(rel_path, runnable_files)) return true;
    if (rel_path.find("/deferred/") != std::string::npos ||
        starts_with(rel_path.c_str(), "deferred/")) return true;
    return false;
}

static std::string sanitize_test_name(std::string name) {
    if (ends_with(name, ".html")) name.resize(name.size() - 5);
    for (char& c : name) {
        if (!isalnum((unsigned char)c)) c = '_';
    }
    if (name.empty()) name = "empty";
    return name;
}

static void scan_dir(const std::string& dir,
                     const std::string& rel_prefix,
                     const std::vector<std::string>& runnable_files,
                     std::vector<ChromeEditingParam>& params) {
#ifdef _WIN32
    std::string pattern = dir + "\\*";
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern.c_str(), &fd);
    if (handle == -1) return;
    do {
        const char* name = fd.name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        std::string rel = rel_prefix.empty() ? std::string(name)
                                             : rel_prefix + "/" + name;
        if (fd.attrib & _A_SUBDIR) {
            scan_dir(dir + "/" + name, rel, runnable_files, params);
            continue;
        }
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        const char* name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        std::string rel = rel_prefix.empty() ? std::string(name)
                                             : rel_prefix + "/" + name;
        if (entry->d_type == DT_DIR) {
            scan_dir(dir + "/" + name, rel, runnable_files, params);
            continue;
        }
#endif

        if (!ends_with(name, ".html")) continue;
        ChromeEditingParam p;
        p.html_path = dir + "/" + name;
        p.rel_path = rel;
        p.test_name = sanitize_test_name(rel);
        p.skip = should_skip_file(rel, runnable_files);
        params.push_back(p);

#ifdef _WIN32
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
#else
    }
    closedir(d);
#endif
}

static std::vector<ChromeEditingParam> discover_chrome_editing_tests() {
    std::vector<ChromeEditingParam> params;
    std::vector<std::string> runnable_files = load_runnable_files();
    if (!run_all_imported_chrome_editing_tests()) {
        for (const std::string& rel : runnable_files) {
            ChromeEditingParam p;
            p.html_path = std::string(EDITING_ROOT) + "/" + rel;
            p.rel_path = rel;
            p.test_name = sanitize_test_name(rel);
            p.skip = false;
            params.push_back(p);
        }
    }
    scan_dir(EDITING_ROOT, "", runnable_files, params);
    std::sort(params.begin(), params.end(),
              [](const ChromeEditingParam& a,
                 const ChromeEditingParam& b) {
                  if (a.test_name != b.test_name)
                      return a.test_name < b.test_name;
                  return a.rel_path < b.rel_path;
              });
    params.erase(std::unique(params.begin(), params.end(),
                 [](const ChromeEditingParam& a, const ChromeEditingParam& b) {
                     return a.rel_path == b.rel_path;
                 }), params.end());
    return params;
}

static ChromeEditingResult run_chrome_editing_case(
        const ChromeEditingParam& p) {
    ChromeEditingResult result;
    result.param = p;
    result.skipped = false;
    result.failed = false;
    result.timeout = false;
    result.abort = false;
    result.pass_count = 0;
    result.total_count = 0;
    result.exit_code = 0;
    result.seconds = 0.0;
    result.bucket = top_level_bucket(p.rel_path);

    auto started = std::chrono::steady_clock::now();
    if (p.skip) {
        result.skipped = true;
        result.skip_reason = "skipped corpus support file/manual/baseline: " +
                             p.rel_path;
        result.flavor_tags = classify_flavors(p.rel_path, "", "", false);
        result.classifier = classify_failure(result);
        return result;
    }

    std::string html = read_file_contents(p.html_path.c_str());
    if (html.empty()) {
        result.failed = true;
        result.failures.push_back("Could not read test file: " + p.html_path);
        result.classifier = classify_failure(result);
        return result;
    }

    std::string harness = read_file_contents(HARNESS_PATH);
    if (harness.empty()) {
        result.failed = true;
        result.failures.push_back(std::string("Could not read harness: ") +
                                  HARNESS_PATH);
        result.classifier = classify_failure(result);
        return result;
    }
    std::string harness_patch = read_file_contents(HARNESS_PATCH_PATH);
    if (!harness_patch.empty()) {
        harness += "\n// ---- CE3 harness overlay ----\n";
        harness += harness_patch;
        harness += "\n";
    }

    std::string scripts = extract_scripts(html, dir_of(p.html_path));
    if (scripts.empty()) {
        result.skipped = true;
        result.skip_reason = "no script body in editing test: " + p.rel_path;
        result.flavor_tags = classify_flavors(p.rel_path, html, scripts, false);
        result.classifier = classify_failure(result);
        return result;
    }

    std::string expected_path = expected_text_path_for(p);
    bool has_expected = !expected_path.empty() && file_exists(expected_path.c_str());
    std::string expected_text = has_expected
        ? read_file_contents(expected_path.c_str())
        : "";
    result.flavor_tags = classify_flavors(p.rel_path, html, scripts,
        has_expected);

    std::string metadata;
    metadata += "\n_chrome_editing_test_path = " +
        js_string_literal(p.rel_path) + ";\n";
    metadata += "_chrome_editing_expected_path = " +
        js_string_literal(has_expected ? expected_path : "") + ";\n";
    metadata += "_chrome_editing_expected_text = " +
        js_string_literal(expected_text) + ";\n";

    std::string combined = harness + metadata + "\n" + scripts +
        "\n_chrome_editing_print_summary();\n";
    std::string temp_js = std::string(TEMP_DIR) + "/chrome_editing_" +
        p.test_name + ".js";
    write_file_contents(temp_js.c_str(), combined);
    std::string temp_html = std::string(TEMP_DIR) + "/chrome_editing_" +
        p.test_name + ".html";
    write_file_contents(temp_html.c_str(), strip_script_elements(html));

    int exit_code = 0;
    std::string output = execute_js_with_doc(temp_js.c_str(), temp_html.c_str(),
        &exit_code);
    if (!keep_chrome_editing_temp_scripts()) {
        unlink(temp_js.c_str());
        unlink(temp_html.c_str());
    }

    result.exit_code = exit_code;
    result.output = output;
    result.timeout = exit_code == 142 || contains_text(output, "Alarm clock") ||
        contains_text(output, "SIGALRM") ||
        contains_text(output, "timed out");
    result.abort = exit_code == 134 || contains_text(output, "Abort trap") ||
        contains_text(output, "SIGABRT");

    size_t pos = 0;
    while (pos < output.size()) {
        size_t eol = output.find('\n', pos);
        if (eol == std::string::npos) eol = output.size();
        std::string line = output.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.substr(0, 6) == "FAIL: ") result.failures.push_back(line);
        if (line.substr(0, 23) == "CHROME_EDITING_RESULT: ") {
            sscanf(line.c_str(), "CHROME_EDITING_RESULT: %d/%d",
                   &result.pass_count, &result.total_count);
        }
    }

    if (result.total_count == 0 && result.exit_code == 0 &&
        has_tag(result.flavor_tags, "visual-layout")) {
        result.skipped = true;
        result.skip_reason = "unsupported visual/layout-only Chrome editing case: " +
            p.rel_path;
        result.classifier = "unsupported_layout_visual";
    } else if (result.total_count == 0) {
        result.failed = true;
        result.failures.push_back("No Chrome editing results from " +
            p.rel_path + "\nExit code: " + std::to_string(exit_code) +
            "\nOutput (first 2KB):\n" + output.substr(0, 2048));
    } else {
        result.failed = exit_code != 0 || !result.failures.empty() ||
            result.pass_count != result.total_count;
    }

    auto ended = std::chrono::steady_clock::now();
    result.seconds = std::chrono::duration<double>(ended - started).count();
    result.classifier = classify_failure(result);
    return result;
}

class ChromeEditingTest :
    public testing::TestWithParam<ChromeEditingParam> {};

static void report_gtest_result(const ChromeEditingResult& result) {
    if (result.skipped) {
        GTEST_SKIP() << result.skip_reason;
        return;
    }
    printf("  %s: %d/%d passed\n", result.param.rel_path.c_str(),
           result.pass_count, result.total_count);
    for (const auto& failure : result.failures) {
        ADD_FAILURE() << failure;
    }
    EXPECT_EQ(result.exit_code, 0) << result.output;
    EXPECT_EQ(result.pass_count, result.total_count)
        << "Not all assertions passed in " << result.param.rel_path;
}

TEST_P(ChromeEditingTest, Run) {
    report_gtest_result(run_chrome_editing_case(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(
    ChromeEditing,
    ChromeEditingTest,
    testing::ValuesIn(discover_chrome_editing_tests()),
    [](const testing::TestParamInfo<ChromeEditingParam>& info) {
        return info.param.test_name;
    });

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ChromeEditingTest);

static int get_parallel_jobs() {
    const char* env_jobs = getenv("LAMBDA_CHROME_EDITING_JOBS");
    if (env_jobs && env_jobs[0]) {
        int jobs = atoi(env_jobs);
        if (jobs > 0) return jobs;
    }
    unsigned int cpus = std::thread::hardware_concurrency();
    if (cpus <= 1) return 1;
    return (int)cpus - 1;
}

static bool has_filtered_gtest_arg(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gtest_list_tests") == 0) return true;
        if (starts_with(argv[i], "--gtest_filter=")) {
            const char* filter = argv[i] + strlen("--gtest_filter=");
            if (strcmp(filter, "*") != 0 && strcmp(filter, "ChromeEditing*") != 0)
                return true;
        }
    }
    return false;
}

static int run_parallel_suite() {
    std::vector<ChromeEditingParam> params = discover_chrome_editing_tests();
    std::vector<ChromeEditingResult> results(params.size());
    std::atomic<size_t> next_index(0);
    std::mutex print_mutex;

    int jobs = get_parallel_jobs();
    if (jobs < 1) jobs = 1;
    if ((size_t)jobs > params.size() && !params.empty()) jobs = (int)params.size();

    printf("[==========] Running %zu Chrome editing cases with %d jobs\n",
           params.size(), jobs);
    auto started = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    for (int wi = 0; wi < jobs; wi++) {
        workers.emplace_back([&]() {
            while (true) {
                size_t index = next_index.fetch_add(1);
                if (index >= params.size()) break;
                const ChromeEditingParam& p = params[index];
                if (p.skip) {
                    results[index] = run_chrome_editing_case(p);
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lock(print_mutex);
                    printf("[ RUN      ] ChromeEditing/ChromeEditingTest.Run/%s\n",
                           p.test_name.c_str());
                }

                ChromeEditingResult result = run_chrome_editing_case(p);
                results[index] = result;
                {
                    std::lock_guard<std::mutex> lock(print_mutex);
                    const char* status = result.skipped ? "[  SKIPPED ]" :
                        (result.failed ? "[  FAILED  ]" : "[       OK ]");
                    printf("%s ChromeEditing/ChromeEditingTest.Run/%s (%.0f ms)\n",
                           status, p.test_name.c_str(),
                           result.seconds * 1000.0);
                }
            }
        });
    }
    for (auto& worker : workers) worker.join();

    auto ended = std::chrono::steady_clock::now();
    double total_seconds = std::chrono::duration<double>(ended - started).count();

    int failures = 0;
    int skipped = 0;
    for (const auto& result : results) {
        if (result.skipped) skipped++;
        else if (result.failed) failures++;
    }
    write_result_artifact(results);

    printf("[==========] %zu Chrome editing cases ran. (%.0f ms total)\n",
           params.size(), total_seconds * 1000.0);
    printf("[  PASSED  ] %zu tests.\n", params.size() - failures - skipped);
    if (skipped > 0) printf("[  SKIPPED ] %d tests.\n", skipped);
    if (failures > 0) printf("[  FAILED  ] %d tests.\n", failures);
    print_classifier_summary(results);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
    if (!has_filtered_gtest_arg(argc, argv)) {
        return run_parallel_suite();
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
