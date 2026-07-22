// test_mir_check_helpers.hpp
//
// Shared harness for MIR emission fixtures (MT1 of
// vibe/Lambda_Design_MIR_Emission_Test.md).
//
// A fixture is a Lambda or JS script plus a `.mir-check` JSON sidecar. The
// harness compiles the script with a private canonical MIR artifact path,
// slices the whole-context dump into (module, function, occurrence) scopes,
// and evaluates the sidecar's assertions against the selected scope.
//
// Design constraints this file encodes:
//  - Assertions match names and instruction shapes, never raw immediates:
//    the Lambda emitter bakes host pointers into `mov` operands, so full-file
//    goldens of script dumps are not reproducible across processes.
//  - The canonical stage is the finalized MIR (after MIR_finish_func and
//    MIR_finish_module, before link/generation) that mir_dump_finalized emits.
//  - No GTEST_SKIP: MT2 makes the artifact available in every build, so a
//    missing dump is a failure, not a skip.
//  - --no-log is the master gate that suppresses MIR artifacts, so a fixture
//    may never pass it. Sidecars that try are rejected.
//
// Test-only code: std:: containers are allowed here (the lib/ types rule
// governs lambda/ and radiant/ production code). The sidecar JSON reader is
// deliberately self-contained rather than reusing the runtime's own parser --
// the oracle that evaluates emission expectations should not depend on the
// runtime under test.

#ifndef TEST_MIR_CHECK_HELPERS_HPP
#define TEST_MIR_CHECK_HELPERS_HPP

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
    #define MIR_CHECK_GETPID() _getpid()
#else
    #include <dirent.h>
    #include <unistd.h>
    #define MIR_CHECK_GETPID() getpid()
#endif

#include "../lib/shell.h"

namespace mir_check {

// ---------------------------------------------------------------------------
// Minimal JSON reader (objects, arrays, strings, integers, booleans, null)
// ---------------------------------------------------------------------------

struct JsonValue {
    enum Kind { KNull, KBool, KInt, KStr, KArr, KObj };
    Kind kind = KNull;
    bool bool_value = false;
    long long int_value = 0;
    std::string str_value;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue> > fields;

    const JsonValue* find(const std::string& key) const {
        for (size_t i = 0; i < fields.size(); i++) {
            if (fields[i].first == key) return &fields[i].second;
        }
        return nullptr;
    }
    bool has(const std::string& key) const { return find(key) != nullptr; }
};

class JsonReader {
public:
    JsonReader(const std::string& text) : text_(text), pos_(0) {}

    bool parse(JsonValue* out) {
        skip_ws();
        if (!parse_value(out)) return false;
        skip_ws();
        if (pos_ != text_.size()) return fail("trailing content after top-level value");
        return true;
    }
    const std::string& error() const { return error_; }

private:
    const std::string& text_;
    size_t pos_;
    std::string error_;

    bool fail(const std::string& message) {
        if (error_.empty()) {
            char where[64];
            snprintf(where, sizeof(where), " (at byte %zu)", pos_);
            error_ = message + where;
        }
        return false;
    }
    void skip_ws() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { pos_++; continue; }
            break;
        }
    }
    bool literal(const char* word) {
        size_t n = strlen(word);
        if (text_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }
    bool parse_value(JsonValue* out) {
        skip_ws();
        if (pos_ >= text_.size()) return fail("unexpected end of input");
        char c = text_[pos_];
        if (c == '{') return parse_object(out);
        if (c == '[') return parse_array(out);
        if (c == '"') {
            out->kind = JsonValue::KStr;
            return parse_string(&out->str_value);
        }
        if (c == 't') {
            if (!literal("true")) return fail("invalid literal");
            out->kind = JsonValue::KBool; out->bool_value = true; return true;
        }
        if (c == 'f') {
            if (!literal("false")) return fail("invalid literal");
            out->kind = JsonValue::KBool; out->bool_value = false; return true;
        }
        if (c == 'n') {
            if (!literal("null")) return fail("invalid literal");
            out->kind = JsonValue::KNull; return true;
        }
        return parse_int(out);
    }
    bool parse_object(JsonValue* out) {
        out->kind = JsonValue::KObj;
        pos_++;  // consume '{'
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}') { pos_++; return true; }
        while (true) {
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != '"') return fail("expected object key");
            std::string key;
            if (!parse_string(&key)) return false;
            for (size_t i = 0; i < out->fields.size(); i++) {
                if (out->fields[i].first == key) return fail("duplicate key '" + key + "'");
            }
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != ':') return fail("expected ':' after key");
            pos_++;
            JsonValue value;
            if (!parse_value(&value)) return false;
            out->fields.push_back(std::make_pair(key, value));
            skip_ws();
            if (pos_ < text_.size() && text_[pos_] == ',') { pos_++; continue; }
            if (pos_ < text_.size() && text_[pos_] == '}') { pos_++; return true; }
            return fail("expected ',' or '}' in object");
        }
    }
    bool parse_array(JsonValue* out) {
        out->kind = JsonValue::KArr;
        pos_++;  // consume '['
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']') { pos_++; return true; }
        while (true) {
            JsonValue value;
            if (!parse_value(&value)) return false;
            out->items.push_back(value);
            skip_ws();
            if (pos_ < text_.size() && text_[pos_] == ',') { pos_++; continue; }
            if (pos_ < text_.size() && text_[pos_] == ']') { pos_++; return true; }
            return fail("expected ',' or ']' in array");
        }
    }
    bool parse_string(std::string* out) {
        out->clear();
        pos_++;  // consume opening quote
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return true;
            if (c != '\\') { out->push_back(c); continue; }
            if (pos_ >= text_.size()) return fail("unterminated escape");
            char esc = text_[pos_++];
            switch (esc) {
            case '"':  out->push_back('"');  break;
            case '\\': out->push_back('\\'); break;
            case '/':  out->push_back('/');  break;
            case 'b':  out->push_back('\b'); break;
            case 'f':  out->push_back('\f'); break;
            case 'n':  out->push_back('\n'); break;
            case 'r':  out->push_back('\r'); break;
            // \t matters: MIR dump lines are tab-separated, so patterns use it.
            case 't':  out->push_back('\t'); break;
            case 'u': {
                if (pos_ + 4 > text_.size()) return fail("truncated \\u escape");
                unsigned code = 0;
                for (int i = 0; i < 4; i++) {
                    char h = text_[pos_++];
                    unsigned digit;
                    if (h >= '0' && h <= '9') digit = (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') digit = (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') digit = (unsigned)(h - 'A' + 10);
                    else return fail("invalid \\u escape");
                    code = (code << 4) | digit;
                }
                // dump text is ASCII; encode as UTF-8 for completeness.
                if (code < 0x80) {
                    out->push_back((char)code);
                } else if (code < 0x800) {
                    out->push_back((char)(0xC0 | (code >> 6)));
                    out->push_back((char)(0x80 | (code & 0x3F)));
                } else {
                    out->push_back((char)(0xE0 | (code >> 12)));
                    out->push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                    out->push_back((char)(0x80 | (code & 0x3F)));
                }
                break;
            }
            default: return fail("invalid escape character");
            }
        }
        return fail("unterminated string");
    }
    bool parse_int(JsonValue* out) {
        size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) pos_++;
        size_t digits = 0;
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') { pos_++; digits++; }
        if (digits == 0) return fail("expected a value");
        if (pos_ < text_.size() && (text_[pos_] == '.' || text_[pos_] == 'e' || text_[pos_] == 'E')) {
            // counts and exit codes are integers; a float is a schema error.
            return fail("fractional numbers are not supported in .mir-check files");
        }
        out->kind = JsonValue::KInt;
        out->int_value = strtoll(text_.substr(start, pos_ - start).c_str(), nullptr, 10);
        return true;
    }
};

// ---------------------------------------------------------------------------
// Dump model: whole-context MIR text sliced into scopes
// ---------------------------------------------------------------------------

struct MirLine {
    std::string text;
    int number = 0;  // 1-based line number in the dump file
};

struct MirScope {
    std::string module;
    std::string function;   // empty for the module-level item stream
    int occurrence = 0;     // index among same-named functions in the module
    std::vector<MirLine> lines;
};

struct MirDump {
    std::string path;
    std::vector<MirLine> all_lines;
    std::vector<MirScope> functions;
    std::vector<std::string> modules;  // in appearance order

    bool empty() const { return all_lines.empty(); }
};

// a MIR dump line for a named item looks like "<name>:\t<kind>..."; the
// module/function openers and their terminators are the only structure the
// harness needs.
inline bool parse_named_item(const std::string& line, std::string* name, std::string* kind) {
    if (line.empty() || line[0] == '\t' || line[0] == ' ') return false;
    size_t colon = line.find(':');
    if (colon == std::string::npos) return false;
    size_t tab = line.find('\t', colon);
    if (tab == std::string::npos || tab != colon + 1) return false;
    *name = line.substr(0, colon);
    size_t kind_end = line.find('\t', tab + 1);
    *kind = line.substr(tab + 1, kind_end == std::string::npos ? std::string::npos : kind_end - tab - 1);
    return true;
}

inline bool read_file_text(const std::string& path, std::string* out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buffer[8192];
    size_t n;
    out->clear();
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) out->append(buffer, n);
    fclose(f);
    return true;
}

inline MirDump parse_mir_dump(const std::string& path, const std::string& text) {
    MirDump dump;
    dump.path = path;

    std::vector<std::string> raw;
    std::string current;
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '\n') { raw.push_back(current); current.clear(); }
        else if (text[i] != '\r') { current.push_back(text[i]); }
    }
    if (!current.empty()) raw.push_back(current);

    std::string module;
    MirScope* open_function = nullptr;
    std::vector<int> name_counts_index;  // parallel to dump.functions

    for (size_t i = 0; i < raw.size(); i++) {
        MirLine line;
        line.text = raw[i];
        line.number = (int)i + 1;
        dump.all_lines.push_back(line);

        std::string name, kind;
        if (parse_named_item(raw[i], &name, &kind)) {
            if (kind == "module") {
                module = name;
                dump.modules.push_back(module);
                open_function = nullptr;
                continue;
            }
            if (kind == "func") {
                MirScope scope;
                scope.module = module;
                scope.function = name;
                scope.occurrence = 0;
                for (size_t j = 0; j < dump.functions.size(); j++) {
                    if (dump.functions[j].module == module && dump.functions[j].function == name) {
                        scope.occurrence++;
                    }
                }
                scope.lines.push_back(line);
                dump.functions.push_back(scope);
                open_function = &dump.functions.back();
                continue;
            }
        }
        if (raw[i] == "\tendfunc") {
            if (open_function) open_function->lines.push_back(line);
            open_function = nullptr;
            continue;
        }
        if (raw[i] == "\tendmodule") {
            open_function = nullptr;
            module.clear();
            continue;
        }
        if (open_function) open_function->lines.push_back(line);
    }
    return dump;
}

// ---------------------------------------------------------------------------
// Sidecar model
// ---------------------------------------------------------------------------

struct CountExpectation {
    std::string pattern;
    bool has_exact = false; long long exact = 0;
    bool has_min = false;   long long min_value = 0;
    bool has_max = false;   long long max_value = 0;
};

struct SeqExpectation {
    std::string pattern;
    bool next_line = false;
};

struct CheckGroup {
    bool has_module = false;
    std::string module;
    std::string function = "main";
    bool whole_module = false;     // in_func == "*"
    bool has_occurrence = false;
    long long occurrence = 0;
    std::vector<std::string> expect;
    std::vector<std::string> forbid;
    std::vector<SeqExpectation> expect_seq;
    std::vector<CountExpectation> counts;
};

struct Sidecar {
    std::string path;
    std::string description;
    long long expect_exit_code = 0;
    std::vector<std::string> extra_args;
    std::vector<CheckGroup> checks;
};

// every key is validated, so an unknown or misspelled field fails the fixture
// instead of silently weakening it.
inline bool known_key(const char* const* allowed, size_t count, const std::string& key) {
    for (size_t i = 0; i < count; i++) {
        if (key == allowed[i]) return true;
    }
    return false;
}

inline bool load_sidecar(const std::string& path, Sidecar* out, std::string* error) {
    std::string text;
    if (!read_file_text(path, &text)) { *error = "cannot read sidecar file"; return false; }

    JsonValue root;
    JsonReader reader(text);
    if (!reader.parse(&root)) { *error = "malformed JSON: " + reader.error(); return false; }
    if (root.kind != JsonValue::KObj) { *error = "sidecar root must be an object"; return false; }

    out->path = path;

    static const char* kTopKeys[] = {
        "schema_version", "description", "expect_exit_code", "args", "checks",
    };
    for (size_t i = 0; i < root.fields.size(); i++) {
        if (!known_key(kTopKeys, sizeof(kTopKeys) / sizeof(kTopKeys[0]), root.fields[i].first)) {
            *error = "unknown top-level field '" + root.fields[i].first + "'";
            return false;
        }
    }

    const JsonValue* version = root.find("schema_version");
    if (!version || version->kind != JsonValue::KInt) {
        *error = "missing required integer field 'schema_version'";
        return false;
    }
    if (version->int_value != 1) {
        *error = "unsupported schema_version (this harness understands version 1)";
        return false;
    }

    const JsonValue* description = root.find("description");
    if (description) {
        if (description->kind != JsonValue::KStr) { *error = "'description' must be a string"; return false; }
        out->description = description->str_value;
    }

    const JsonValue* exit_code = root.find("expect_exit_code");
    if (exit_code) {
        if (exit_code->kind != JsonValue::KInt) { *error = "'expect_exit_code' must be an integer"; return false; }
        out->expect_exit_code = exit_code->int_value;
    }

    const JsonValue* args = root.find("args");
    if (args) {
        if (args->kind != JsonValue::KArr) { *error = "'args' must be an array of strings"; return false; }
        for (size_t i = 0; i < args->items.size(); i++) {
            if (args->items[i].kind != JsonValue::KStr) { *error = "'args' entries must be strings"; return false; }
            const std::string& arg = args->items[i].str_value;
            // --no-log is the master gate that suppresses MIR artifacts, so a
            // fixture asking for both a dump and --no-log is contradictory.
            if (arg == "--no-log") {
                *error = "'args' may not contain --no-log: it suppresses the MIR artifact this check reads";
                return false;
            }
            out->extra_args.push_back(arg);
        }
    }

    const JsonValue* checks = root.find("checks");
    if (!checks || checks->kind != JsonValue::KArr || checks->items.empty()) {
        *error = "missing required non-empty array field 'checks'";
        return false;
    }

    static const char* kCheckKeys[] = {
        "in_module", "in_func", "occurrence", "expect", "forbid", "expect_seq", "count",
    };
    static const char* kSeqKeys[] = { "pattern", "next_line" };
    static const char* kBoundKeys[] = { "min", "max" };

    for (size_t c = 0; c < checks->items.size(); c++) {
        const JsonValue& group_json = checks->items[c];
        if (group_json.kind != JsonValue::KObj) { *error = "each entry of 'checks' must be an object"; return false; }
        CheckGroup group;
        for (size_t i = 0; i < group_json.fields.size(); i++) {
            if (!known_key(kCheckKeys, sizeof(kCheckKeys) / sizeof(kCheckKeys[0]), group_json.fields[i].first)) {
                *error = "unknown field '" + group_json.fields[i].first + "' in checks entry";
                return false;
            }
        }
        const JsonValue* in_module = group_json.find("in_module");
        if (in_module) {
            if (in_module->kind != JsonValue::KStr) { *error = "'in_module' must be a string"; return false; }
            group.has_module = true;
            group.module = in_module->str_value;
        }
        const JsonValue* in_func = group_json.find("in_func");
        if (in_func) {
            if (in_func->kind != JsonValue::KStr) { *error = "'in_func' must be a string"; return false; }
            group.function = in_func->str_value;
            group.whole_module = (group.function == "*");
        }
        const JsonValue* occurrence = group_json.find("occurrence");
        if (occurrence) {
            if (occurrence->kind != JsonValue::KInt || occurrence->int_value < 0) {
                *error = "'occurrence' must be a non-negative integer";
                return false;
            }
            group.has_occurrence = true;
            group.occurrence = occurrence->int_value;
        }

        const JsonValue* expect = group_json.find("expect");
        if (expect) {
            if (expect->kind != JsonValue::KArr) { *error = "'expect' must be an array of strings"; return false; }
            for (size_t i = 0; i < expect->items.size(); i++) {
                if (expect->items[i].kind != JsonValue::KStr) { *error = "'expect' entries must be strings"; return false; }
                group.expect.push_back(expect->items[i].str_value);
            }
        }
        const JsonValue* forbid = group_json.find("forbid");
        if (forbid) {
            if (forbid->kind != JsonValue::KArr) { *error = "'forbid' must be an array of strings"; return false; }
            for (size_t i = 0; i < forbid->items.size(); i++) {
                if (forbid->items[i].kind != JsonValue::KStr) { *error = "'forbid' entries must be strings"; return false; }
                group.forbid.push_back(forbid->items[i].str_value);
            }
        }
        const JsonValue* seq = group_json.find("expect_seq");
        if (seq) {
            if (seq->kind != JsonValue::KArr) { *error = "'expect_seq' must be an array"; return false; }
            for (size_t i = 0; i < seq->items.size(); i++) {
                const JsonValue& entry = seq->items[i];
                SeqExpectation step;
                if (entry.kind == JsonValue::KStr) {
                    step.pattern = entry.str_value;
                } else if (entry.kind == JsonValue::KObj) {
                    for (size_t k = 0; k < entry.fields.size(); k++) {
                        if (!known_key(kSeqKeys, sizeof(kSeqKeys) / sizeof(kSeqKeys[0]), entry.fields[k].first)) {
                            *error = "unknown field '" + entry.fields[k].first + "' in expect_seq entry";
                            return false;
                        }
                    }
                    const JsonValue* pattern = entry.find("pattern");
                    if (!pattern || pattern->kind != JsonValue::KStr) {
                        *error = "expect_seq entries need a string 'pattern'";
                        return false;
                    }
                    step.pattern = pattern->str_value;
                    const JsonValue* next_line = entry.find("next_line");
                    if (next_line) {
                        if (next_line->kind != JsonValue::KBool) { *error = "'next_line' must be a boolean"; return false; }
                        step.next_line = next_line->bool_value;
                    }
                } else {
                    *error = "expect_seq entries must be strings or objects";
                    return false;
                }
                if (step.next_line && i == 0) {
                    *error = "the first expect_seq entry cannot set next_line";
                    return false;
                }
                group.expect_seq.push_back(step);
            }
        }
        const JsonValue* count = group_json.find("count");
        if (count) {
            if (count->kind != JsonValue::KObj) { *error = "'count' must be an object"; return false; }
            for (size_t i = 0; i < count->fields.size(); i++) {
                CountExpectation expectation;
                expectation.pattern = count->fields[i].first;
                const JsonValue& bound = count->fields[i].second;
                if (bound.kind == JsonValue::KInt) {
                    if (bound.int_value < 0) { *error = "'count' values must be non-negative"; return false; }
                    expectation.has_exact = true;
                    expectation.exact = bound.int_value;
                } else if (bound.kind == JsonValue::KObj) {
                    for (size_t k = 0; k < bound.fields.size(); k++) {
                        if (!known_key(kBoundKeys, sizeof(kBoundKeys) / sizeof(kBoundKeys[0]), bound.fields[k].first)) {
                            *error = "unknown field '" + bound.fields[k].first + "' in count bound";
                            return false;
                        }
                    }
                    const JsonValue* min_value = bound.find("min");
                    const JsonValue* max_value = bound.find("max");
                    if (!min_value && !max_value) { *error = "'count' bound needs 'min' and/or 'max'"; return false; }
                    if (min_value) {
                        if (min_value->kind != JsonValue::KInt || min_value->int_value < 0) {
                            *error = "'min' must be a non-negative integer"; return false;
                        }
                        expectation.has_min = true;
                        expectation.min_value = min_value->int_value;
                    }
                    if (max_value) {
                        if (max_value->kind != JsonValue::KInt || max_value->int_value < 0) {
                            *error = "'max' must be a non-negative integer"; return false;
                        }
                        expectation.has_max = true;
                        expectation.max_value = max_value->int_value;
                    }
                    if (expectation.has_min && expectation.has_max &&
                        expectation.min_value > expectation.max_value) {
                        *error = "'count' bound has min greater than max"; return false;
                    }
                } else {
                    *error = "'count' values must be integers or {min,max} objects";
                    return false;
                }
                group.counts.push_back(expectation);
            }
        }

        if (group.expect.empty() && group.forbid.empty() &&
            group.expect_seq.empty() && group.counts.empty()) {
            *error = "checks entry has no assertions";
            return false;
        }
        out->checks.push_back(group);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Compile-and-dump
// ---------------------------------------------------------------------------

enum Language { LANG_LAMBDA, LANG_JS };

// per-function frame shape reported by the emitter's own finalizers via
// LAMBDA_MIR_LOG_FRAME_SLOTS. `instructions` here is a *pre-finish* count and
// is deliberately never compared against the finalized dump's instruction
// count -- the two describe different stages.
struct FrameSlots {
    std::string function;
    long long roots = 0;
    long long root_stores = 0;
    long long scalar_homes = 0;
    long long number_scratch = 0;
    long long safepoints = 0;
};

struct CompileOptions {
    Language language = LANG_LAMBDA;
    long long expect_exit_code = 0;
    std::vector<std::string> extra_args;
    // ask the emitter to report per-function frame shape into a private log
    bool collect_frame_slots = false;
};

struct CompileResult {
    bool ok = false;
    std::string error;
    int exit_code = 0;
    std::string output;      // merged stdout/stderr from the child
    std::string dump_path;
    std::string telemetry_path;
    MirDump dump;
    std::vector<FrameSlots> frame_slots;
};

inline std::string basename_of(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

inline std::string private_dump_path(const std::string& script_path, const char* extension) {
    // the shared default artifact is truncated by every concurrent lambda.exe
    // run, so each fixture writes its own file; the pid keeps parallel test
    // binaries from colliding on the same fixture name.
    char suffix[32];
    snprintf(suffix, sizeof(suffix), ".%d", (int)MIR_CHECK_GETPID());
    return "temp/mir_check/" + basename_of(script_path) + suffix + extension;
}

inline void ensure_dump_dir() {
#ifdef _WIN32
    CreateDirectoryA("temp", NULL);
    CreateDirectoryA("temp\\mir_check", NULL);
#else
    (void)system("mkdir -p temp/mir_check");
#endif
}

// pull one long-long field out of a "key=value" telemetry line.
inline bool telemetry_field(const std::string& line, const char* key, long long* out) {
    std::string needle = std::string(" ") + key + "=";
    size_t at = line.find(needle);
    if (at == std::string::npos) return false;
    *out = strtoll(line.c_str() + at + needle.size(), nullptr, 10);
    return true;
}

inline std::vector<FrameSlots> parse_frame_slots(const std::string& log_text) {
    std::vector<FrameSlots> slots;
    size_t pos = 0;
    while (pos < log_text.size()) {
        size_t end = log_text.find('\n', pos);
        if (end == std::string::npos) end = log_text.size();
        std::string line = log_text.substr(pos, end - pos);
        pos = end + 1;
        size_t marker = line.find("mir-function: function=");
        if (marker == std::string::npos) continue;
        FrameSlots entry;
        size_t name_start = marker + strlen("mir-function: function=");
        size_t name_end = line.find(' ', name_start);
        entry.function = line.substr(name_start, name_end == std::string::npos
                                     ? std::string::npos : name_end - name_start);
        telemetry_field(line, "roots", &entry.roots);
        telemetry_field(line, "root_stores", &entry.root_stores);
        telemetry_field(line, "scalar_homes", &entry.scalar_homes);
        telemetry_field(line, "number_scratch", &entry.number_scratch);
        telemetry_field(line, "safepoints", &entry.safepoints);
        slots.push_back(entry);
    }
    return slots;
}

// how to launch one lambda.exe run. Both the emission harness and the
// forced-GC stress sweep go through this so the subcommand rules (js, run,
// --mir-interp placement) live in exactly one place.
struct ProcessSpec {
    Language language = LANG_LAMBDA;
    bool procedural = false;          // Lambda script with a `pn main()`
    bool compile_only = false;        // Lambda only: --transpile-only
    bool mir_interp = false;
    bool quiet = false;               // --no-log; suppresses MIR artifacts too
    std::vector<std::string> extra_args;
    std::vector<std::pair<std::string, std::string> > env;
};

struct ProcessResult {
    int exit_code = 0;
    std::string output;               // stdout with stderr merged in
};

inline ProcessResult run_lambda_process(const std::string& script_path, const ProcessSpec& spec) {
    std::vector<ShellEnvEntry> env;
    for (size_t i = 0; i < spec.env.size(); i++) {
        env.push_back(ShellEnvEntry{spec.env[i].first.c_str(), spec.env[i].second.c_str()});
    }
    env.push_back(ShellEnvEntry{NULL, NULL});

    std::vector<std::string> argv_storage;
    argv_storage.push_back("./lambda.exe");
    if (spec.language == LANG_JS) {
        argv_storage.push_back("js");
        if (spec.mir_interp) argv_storage.push_back("--mir-interp");
        if (spec.quiet) argv_storage.push_back("--no-log");
        argv_storage.push_back(script_path);
    } else {
        // a procedural script needs the `run` subcommand to execute main().
        if (spec.procedural) argv_storage.push_back("run");
        if (spec.mir_interp) argv_storage.push_back("--mir-interp");
        argv_storage.push_back(script_path);
        if (spec.quiet) argv_storage.push_back("--no-log");
        if (spec.compile_only) argv_storage.push_back("--transpile-only");
    }
    for (size_t i = 0; i < spec.extra_args.size(); i++) {
        argv_storage.push_back(spec.extra_args[i]);
    }

    std::vector<const char*> argv;
    for (size_t i = 0; i < argv_storage.size(); i++) argv.push_back(argv_storage[i].c_str());
    argv.push_back(NULL);

    ShellOptions shell_options = {0};
    shell_options.env = &env[0];
    shell_options.merge_stderr = true;
    ShellResult shell_result = shell_exec("./lambda.exe", &argv[0], &shell_options);

    ProcessResult result;
    result.exit_code = shell_result.exit_code;
    if (shell_result.stdout_buf) result.output.assign(shell_result.stdout_buf, shell_result.stdout_len);
    shell_result_free(&shell_result);
    return result;
}

// a Lambda script that declares `pn main()` must be run through the `run`
// subcommand; compiling it is fine either way.
inline bool script_is_procedural(const std::string& script_path) {
    std::string text;
    if (!read_file_text(script_path, &text)) return false;
    return text.find("pn main(") != std::string::npos;
}

inline CompileResult compile_and_dump(const std::string& script_path,
                                      const CompileOptions& options) {
    CompileResult result;
    ensure_dump_dir();
    result.dump_path = private_dump_path(script_path, ".mir");
    remove(result.dump_path.c_str());
    if (options.collect_frame_slots) {
        result.telemetry_path = private_dump_path(script_path, ".slots.log");
        remove(result.telemetry_path.c_str());
    }

    ProcessSpec spec;
    spec.language = options.language;
    // Lambda compiles without running, so nothing depends on the snippet's
    // runtime behavior to produce its MIR. JS has no compile-only entry point.
    spec.compile_only = (options.language == LANG_LAMBDA);
    spec.extra_args = options.extra_args;
    spec.env.push_back(std::make_pair(std::string("LAMBDA_MIR_DUMP_PATH"), result.dump_path));
    // a module-cache hit skips emission entirely, which would leave the caller
    // reading a stale or absent artifact.
    spec.env.push_back(std::make_pair(std::string("LAMBDA_DISABLE_MIR_CACHE"), std::string("1")));
    if (options.collect_frame_slots) {
        // log.txt is shared and truncated per process, so frame telemetry goes
        // to a private file instead of racing other lambda.exe runs.
        spec.env.push_back(std::make_pair(std::string("LAMBDA_LOG_FILE"), result.telemetry_path));
        spec.env.push_back(std::make_pair(std::string("LAMBDA_MIR_LOG_FRAME_SLOTS"), std::string("1")));
    }

    ProcessResult process = run_lambda_process(script_path, spec);
    result.exit_code = process.exit_code;
    result.output = process.output;

    if (result.exit_code != options.expect_exit_code) {
        char message[128];
        snprintf(message, sizeof(message), "lambda.exe exited %d, expected %lld",
                 result.exit_code, options.expect_exit_code);
        result.error = message;
        return result;
    }

    std::string text;
    if (!read_file_text(result.dump_path, &text)) {
        result.error = "no MIR artifact was written to " + result.dump_path +
                       " (MT2 makes the dump available in every build, so this is a real failure)";
        return result;
    }
    result.dump = parse_mir_dump(result.dump_path, text);
    if (result.dump.empty()) {
        result.error = "MIR artifact " + result.dump_path + " is empty";
        return result;
    }

    if (options.collect_frame_slots) {
        std::string log_text;
        if (!read_file_text(result.telemetry_path, &log_text)) {
            result.error = "no frame-slot telemetry at " + result.telemetry_path;
            return result;
        }
        result.frame_slots = parse_frame_slots(log_text);
        if (result.frame_slots.empty()) {
            result.error = "frame-slot telemetry at " + result.telemetry_path +
                           " reported no functions";
            return result;
        }
    }
    result.ok = true;
    return result;
}

inline CompileResult compile_and_dump(const std::string& script_path, Language language,
                                      const Sidecar& sidecar) {
    CompileOptions options;
    options.language = language;
    options.expect_exit_code = sidecar.expect_exit_code;
    options.extra_args = sidecar.extra_args;
    return compile_and_dump(script_path, options);
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

// '#' in an in_func selector matches a run of one or more digits. Both
// frontends name generated symbols with a source-offset id (_accumulate_371,
// _js_helper_752_body), so a fixture that spelled the id would break whenever
// its own comments moved. A selector without '#' is matched exactly.
inline bool function_name_matches(const std::string& pattern, size_t p,
                                  const std::string& name, size_t n) {
    while (p < pattern.size()) {
        if (pattern[p] == '#') {
            size_t digits = 0;
            while (n + digits < name.size() && name[n + digits] >= '0' && name[n + digits] <= '9') {
                digits++;
            }
            if (digits == 0) return false;
            // try the longest run first, then shorter ones, so a '#' followed by
            // a digit-adjacent literal still resolves.
            for (size_t take = digits; take >= 1; take--) {
                if (function_name_matches(pattern, p + 1, name, n + take)) return true;
            }
            return false;
        }
        if (n >= name.size() || name[n] != pattern[p]) return false;
        p++; n++;
    }
    return n == name.size();
}

inline bool function_name_matches(const std::string& pattern, const std::string& name) {
    if (pattern.find('#') == std::string::npos) return pattern == name;
    return function_name_matches(pattern, 0, name, 0);
}

inline std::string render_scope(const std::vector<MirLine>& lines, size_t limit = 120) {
    std::string text;
    for (size_t i = 0; i < lines.size() && i < limit; i++) {
        char prefix[16];
        snprintf(prefix, sizeof(prefix), "%5d| ", lines[i].number);
        text += prefix;
        text += lines[i].text;
        text += "\n";
    }
    if (lines.size() > limit) {
        char more[64];
        snprintf(more, sizeof(more), "  ... %zu more lines\n", lines.size() - limit);
        text += more;
    }
    return text;
}

// resolve a check group's scope, reporting missing and ambiguous selections
// rather than silently checking the wrong function.
inline bool select_scope(const MirDump& dump, const CheckGroup& group,
                         std::vector<MirLine>* out_lines, std::string* out_label,
                         std::string* error) {
    std::string module = group.module;
    if (!group.has_module) {
        if (dump.modules.empty()) { *error = "the MIR artifact declares no module"; return false; }
        module = dump.modules[0];
    } else {
        bool found = false;
        for (size_t i = 0; i < dump.modules.size(); i++) {
            if (dump.modules[i] == module) { found = true; break; }
        }
        if (!found) {
            *error = "module '" + module + "' is not in the artifact";
            return false;
        }
    }

    if (group.whole_module) {
        *out_lines = dump.all_lines;
        *out_label = "module " + module + ", whole artifact";
        return true;
    }

    std::vector<const MirScope*> matches;
    for (size_t i = 0; i < dump.functions.size(); i++) {
        if (dump.functions[i].module == module &&
            function_name_matches(group.function, dump.functions[i].function)) {
            matches.push_back(&dump.functions[i]);
        }
    }
    if (matches.empty()) {
        std::string available;
        for (size_t i = 0; i < dump.functions.size() && i < 24; i++) {
            if (dump.functions[i].module != module) continue;
            if (!available.empty()) available += ", ";
            available += dump.functions[i].function;
        }
        *error = "function '" + group.function + "' is not in module '" + module +
                 "'. Functions present: " + (available.empty() ? "(none)" : available);
        return false;
    }
    size_t index = 0;
    if (group.has_occurrence) {
        if ((size_t)group.occurrence >= matches.size()) {
            char message[192];
            snprintf(message, sizeof(message),
                     "occurrence %lld of function '%s' does not exist (%zu present)",
                     group.occurrence, group.function.c_str(), matches.size());
            *error = message;
            return false;
        }
        index = (size_t)group.occurrence;
    } else if (matches.size() > 1) {
        char message[192];
        snprintf(message, sizeof(message),
                 "function '%s' appears %zu times in module '%s'; set \"occurrence\" to disambiguate",
                 group.function.c_str(), matches.size(), module.c_str());
        *error = message;
        return false;
    }

    *out_lines = matches[index]->lines;
    char label[256];
    snprintf(label, sizeof(label), "module %s, function %s, occurrence %zu",
             module.c_str(), group.function.c_str(), index);
    *out_label = label;
    return true;
}

inline bool line_contains(const std::string& line, const std::string& pattern) {
    return line.find(pattern) != std::string::npos;
}

// count emitted instructions in a slice of dump text. Instruction lines start
// with a tab; `local` declarations, the `endfunc` terminator, item headers,
// labels, and the emitter's stats comment are not instructions.
inline long long count_instructions(const std::vector<MirLine>& lines) {
    long long total = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string& text = lines[i].text;
        if (text.empty() || text[0] != '\t') continue;
        size_t opcode_end = text.find('\t', 1);
        std::string opcode = text.substr(1, opcode_end == std::string::npos
                                         ? std::string::npos : opcode_end - 1);
        if (opcode == "local" || opcode == "endfunc") continue;
        total++;
    }
    return total;
}

// total emitted instructions across every function of a module.
inline long long count_module_instructions(const MirDump& dump, const std::string& module) {
    long long total = 0;
    for (size_t i = 0; i < dump.functions.size(); i++) {
        if (dump.functions[i].module != module) continue;
        total += count_instructions(dump.functions[i].lines);
    }
    return total;
}

inline int count_matches(const std::vector<MirLine>& lines, const std::string& pattern) {
    int total = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        if (line_contains(lines[i].text, pattern)) total++;
    }
    return total;
}

// run one fixture: compile, slice, evaluate. Failures are reported through
// GTest with the sidecar path, the resolved scope, and the scoped MIR text so
// a broken expectation can be diagnosed without re-running by hand.
inline void run_fixture(const std::string& script_path, const std::string& sidecar_path,
                        Language language) {
    Sidecar sidecar;
    std::string error;
    ASSERT_TRUE(load_sidecar(sidecar_path, &sidecar, &error))
        << "sidecar " << sidecar_path << ": " << error;

    CompileResult compiled = compile_and_dump(script_path, language, sidecar);
    ASSERT_TRUE(compiled.ok)
        << "fixture " << script_path << "\nsidecar " << sidecar_path
        << "\n" << compiled.error
        << "\n--- lambda.exe output ---\n" << compiled.output;

    for (size_t c = 0; c < sidecar.checks.size(); c++) {
        const CheckGroup& group = sidecar.checks[c];
        std::vector<MirLine> lines;
        std::string label;
        std::string scope_error;
        ASSERT_TRUE(select_scope(compiled.dump, group, &lines, &label, &scope_error))
            << "sidecar " << sidecar_path << " check #" << c << ": " << scope_error
            << "\nartifact: " << compiled.dump_path;

        const std::string context =
            "\nfixture: " + script_path +
            "\nsidecar: " + sidecar_path + " (check #" + std::to_string(c) + ")" +
            "\nscope:   " + label +
            "\nartifact:" + compiled.dump_path +
            "\n--- scoped MIR ---\n" + render_scope(lines);

        for (size_t i = 0; i < group.expect.size(); i++) {
            EXPECT_GT(count_matches(lines, group.expect[i]), 0)
                << "expected pattern not emitted: \"" << group.expect[i] << "\"" << context;
        }
        for (size_t i = 0; i < group.forbid.size(); i++) {
            EXPECT_EQ(count_matches(lines, group.forbid[i]), 0)
                << "forbidden pattern was emitted: \"" << group.forbid[i] << "\"" << context;
        }
        for (size_t i = 0; i < group.counts.size(); i++) {
            const CountExpectation& expectation = group.counts[i];
            int actual = count_matches(lines, expectation.pattern);
            if (expectation.has_exact) {
                EXPECT_EQ(actual, (int)expectation.exact)
                    << "pattern \"" << expectation.pattern << "\" matched " << actual
                    << " lines, expected exactly " << expectation.exact << context;
            }
            if (expectation.has_min) {
                EXPECT_GE(actual, (int)expectation.min_value)
                    << "pattern \"" << expectation.pattern << "\" matched " << actual
                    << " lines, expected at least " << expectation.min_value << context;
            }
            if (expectation.has_max) {
                EXPECT_LE(actual, (int)expectation.max_value)
                    << "pattern \"" << expectation.pattern << "\" matched " << actual
                    << " lines, expected at most " << expectation.max_value << context;
            }
        }
        if (!group.expect_seq.empty()) {
            size_t cursor = 0;
            bool matched_all = true;
            std::string trace;
            for (size_t step = 0; step < group.expect_seq.size(); step++) {
                const SeqExpectation& want = group.expect_seq[step];
                size_t found = std::string::npos;
                if (want.next_line) {
                    // CHECK-NEXT analogue: the very next line must match.
                    if (cursor < lines.size() && line_contains(lines[cursor].text, want.pattern)) {
                        found = cursor;
                    }
                } else {
                    for (size_t i = cursor; i < lines.size(); i++) {
                        if (line_contains(lines[i].text, want.pattern)) { found = i; break; }
                    }
                }
                if (found == std::string::npos) {
                    matched_all = false;
                    trace += "  step " + std::to_string(step) + " NOT FOUND: \"" + want.pattern + "\"" +
                             (want.next_line ? " (required on the immediately next line)" : "") + "\n";
                    break;
                }
                trace += "  step " + std::to_string(step) + " matched line " +
                         std::to_string(lines[found].number) + ": \"" + want.pattern + "\"\n";
                cursor = found + 1;
            }
            EXPECT_TRUE(matched_all)
                << "expect_seq did not match in order\n" << trace << context;
        }
    }
}

// ---------------------------------------------------------------------------
// Fixture discovery
// ---------------------------------------------------------------------------

struct Fixture {
    std::string script_path;
    std::string sidecar_path;
    std::string name;  // GTest-safe fixture name
};

inline std::string gtest_safe_name(const std::string& file_name) {
    std::string name;
    for (size_t i = 0; i < file_name.size(); i++) {
        char c = file_name[i];
        if (c == '.') break;  // stop at the extension
        name.push_back((c == '-' || c == ' ') ? '_' : c);
    }
    return name.empty() ? "unnamed" : name;
}

inline bool has_extension(const std::string& name, const std::string& extension) {
    return name.size() > extension.size() &&
           name.compare(name.size() - extension.size(), extension.size(), extension) == 0;
}

inline std::vector<Fixture> discover_fixtures(const std::string& dir, const std::string& extension) {
    std::vector<Fixture> fixtures;
#ifdef _WIN32
    WIN32_FIND_DATAA find_data;
    std::string pattern = dir + "\\*" + extension;
    HANDLE handle = FindFirstFileA(pattern.c_str(), &find_data);
    if (handle == INVALID_HANDLE_VALUE) return fixtures;
    do {
        std::string file_name = find_data.cFileName;
        if (!has_extension(file_name, extension)) continue;
#else
    DIR* handle = opendir(dir.c_str());
    if (!handle) return fixtures;
    struct dirent* entry;
    while ((entry = readdir(handle)) != nullptr) {
        std::string file_name = entry->d_name;
        if (!has_extension(file_name, extension)) continue;
#endif
        Fixture fixture;
        fixture.script_path = dir + "/" + file_name;
        fixture.sidecar_path = fixture.script_path.substr(
            0, fixture.script_path.size() - extension.size()) + ".mir-check";
        fixture.name = gtest_safe_name(file_name);
        fixtures.push_back(fixture);
#ifdef _WIN32
    } while (FindNextFileA(handle, &find_data));
    FindClose(handle);
#else
    }
    closedir(handle);
#endif
    return fixtures;
}

}  // namespace mir_check

#endif  // TEST_MIR_CHECK_HELPERS_HPP
