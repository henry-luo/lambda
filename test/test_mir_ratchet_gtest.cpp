// test_mir_ratchet_gtest.cpp
//
// Emission-size ratchet (MT7 of vibe/Lambda_Design_MIR_Emission_Test.md).
//
// Correct-but-bloated MIR passes every behavioral and pattern gate: the
// shadow-copy rooting episode emitted a 7.6x larger frameReview probe (58 ->
// 440 instructions, 84% of the growth root copy/store pairs) and stayed green
// everywhere. This binary is the only layer that catches that class.
//
// Policy is 0% slack. A probe that emits MORE than its recorded threshold
// fails; landing the growth requires reviewing the emission diff and lifting
// the threshold in test/mir/mir_budgets.json in the same commit, so the growth
// is visible in review. A probe that emits LESS passes and prints the tightened
// values for a manual re-baseline. This test never edits its own config.
//
// Two metric sources, deliberately not mixed:
//   - instruction counts come from the finalized MIR artifact;
//   - frame shape (roots, root stores, scalar homes, scratch, safepoints)
//     comes from the emitter's own LAMBDA_MIR_LOG_FRAME_SLOTS telemetry.
// The telemetry also reports a pre-finish instruction count; it describes a
// different stage and is never compared with the artifact's count.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_mir_check_helpers.hpp"

namespace {

using mir_check::JsonValue;
using mir_check::JsonReader;

// take the element count from the array itself: a hand-written count silently
// stops validating whichever keys were appended after it was written.
template <size_t N>
inline bool known_key(const char* const (&allowed)[N], const std::string& key) {
    return mir_check::known_key(allowed, N, key);
}

static const char* kBudgetsPath = "test/mir/mir_budgets.json";

// a metric is optional in the config; only recorded ones are enforced.
struct Metric {
    bool present = false;
    long long value = 0;
};

// instruction counts are addressed by the *artifact's* function names.
struct FunctionBudget {
    std::string name;  // may contain '#' digit wildcards
    Metric insns;
};

// frame shape is addressed by the *telemetry's* function names, which are a
// different namespace: LambdaJS labels several emitted MIR functions with one
// debug name (both `_js_makeBox_611_body` and `_js_makeBox_611` report as
// `_js_makeBox_611`), so a shape budget carries its own occurrence index
// instead of being keyed to an artifact function.
struct FrameBudget {
    std::string name;  // may contain '#' digit wildcards
    long long occurrence = 0;
    Metric roots, root_stores, scalar_homes, number_scratch, safepoints;
};

struct Probe {
    std::string name;
    std::string script;
    mir_check::Language language = mir_check::LANG_LAMBDA;
    std::string profile;            // resolved budget profile key
    Metric module_insns;
    std::vector<FunctionBudget> functions;
    std::vector<FrameBudget> frames;
};

// ---------------------------------------------------------------------------
// Build profile: emission may legitimately differ per platform (Windows commits
// side-stack pages in the prologue) and potentially per host build config, so
// thresholds are looked up by profile with no silent fallback slack: an
// unmatched profile falls back to "default" and simply fails loudly if the
// numbers differ there.
// ---------------------------------------------------------------------------

inline const char* platform_key() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "darwin";
#else
    return "linux";
#endif
}

// the host lambda.exe announces a debug build; thresholds may differ by config.
inline const char* host_config_key() {
    static std::string cached;
    if (!cached.empty()) return cached.c_str();
    const char* args[] = {"./lambda.exe", "--help", NULL};
    ShellOptions options = {0};
    options.merge_stderr = true;
    ShellResult result = shell_exec("./lambda.exe", args, &options);
    std::string text;
    if (result.stdout_buf) text.assign(result.stdout_buf, result.stdout_len);
    shell_result_free(&result);
    cached = (text.find("DEBUG build") != std::string::npos) ? "debug" : "release";
    return cached.c_str();
}

inline bool read_metric(const JsonValue& object, const char* key, Metric* out, std::string* error) {
    const JsonValue* found = object.find(key);
    if (!found) return true;
    if (found->kind != JsonValue::KInt || found->int_value < 0) {
        *error = std::string("'") + key + "' must be a non-negative integer";
        return false;
    }
    out->present = true;
    out->value = found->int_value;
    return true;
}

inline bool load_budgets(const std::string& path, std::vector<Probe>* out, std::string* error) {
    std::string text;
    if (!mir_check::read_file_text(path, &text)) { *error = "cannot read " + path; return false; }

    JsonValue root;
    JsonReader reader(text);
    if (!reader.parse(&root)) { *error = "malformed JSON: " + reader.error(); return false; }
    if (root.kind != JsonValue::KObj) { *error = "budgets root must be an object"; return false; }

    static const char* kTopKeys[] = { "schema_version", "description", "probes" };
    for (size_t i = 0; i < root.fields.size(); i++) {
        if (!known_key(kTopKeys, root.fields[i].first)) {
            *error = "unknown top-level field '" + root.fields[i].first + "'";
            return false;
        }
    }
    const JsonValue* version = root.find("schema_version");
    if (!version || version->kind != JsonValue::KInt || version->int_value != 1) {
        *error = "missing or unsupported 'schema_version' (this harness understands version 1)";
        return false;
    }
    const JsonValue* probes = root.find("probes");
    if (!probes || probes->kind != JsonValue::KArr || probes->items.empty()) {
        *error = "missing required non-empty array field 'probes'";
        return false;
    }

    static const char* kProbeKeys[] = { "name", "script", "language", "description", "budgets" };
    static const char* kBudgetKeys[] = { "module_insns", "functions", "frames" };
    static const char* kFuncKeys[] = { "insns" };
    static const char* kFrameKeys[] = {
        "name", "occurrence", "roots", "root_stores", "scalar_homes",
        "number_scratch", "safepoints",
    };

    std::string profile_specific = std::string(platform_key()) + "-" + host_config_key();

    for (size_t p = 0; p < probes->items.size(); p++) {
        const JsonValue& entry = probes->items[p];
        if (entry.kind != JsonValue::KObj) { *error = "each probe must be an object"; return false; }
        for (size_t i = 0; i < entry.fields.size(); i++) {
            if (!known_key(kProbeKeys, entry.fields[i].first)) {
                *error = "unknown field '" + entry.fields[i].first + "' in a probe";
                return false;
            }
        }
        Probe probe;
        const JsonValue* name = entry.find("name");
        const JsonValue* script = entry.find("script");
        if (!name || name->kind != JsonValue::KStr) { *error = "probe needs a string 'name'"; return false; }
        if (!script || script->kind != JsonValue::KStr) { *error = "probe needs a string 'script'"; return false; }
        probe.name = name->str_value;
        probe.script = script->str_value;

        const JsonValue* language = entry.find("language");
        if (language) {
            if (language->kind != JsonValue::KStr) { *error = "'language' must be a string"; return false; }
            if (language->str_value == "js") probe.language = mir_check::LANG_JS;
            else if (language->str_value == "lambda") probe.language = mir_check::LANG_LAMBDA;
            else { *error = "'language' must be \"lambda\" or \"js\""; return false; }
        }

        const JsonValue* budgets = entry.find("budgets");
        if (!budgets || budgets->kind != JsonValue::KObj) {
            *error = "probe '" + probe.name + "' needs a 'budgets' object";
            return false;
        }
        const JsonValue* selected = budgets->find(profile_specific);
        probe.profile = profile_specific;
        if (!selected) { selected = budgets->find(platform_key()); probe.profile = platform_key(); }
        if (!selected) { selected = budgets->find("default"); probe.profile = "default"; }
        if (!selected || selected->kind != JsonValue::KObj) {
            *error = "probe '" + probe.name + "' has no budget for profile '" +
                     profile_specific + "' and no 'default'";
            return false;
        }
        for (size_t i = 0; i < selected->fields.size(); i++) {
            if (!known_key(kBudgetKeys, selected->fields[i].first)) {
                *error = "unknown field '" + selected->fields[i].first + "' in a budget";
                return false;
            }
        }
        if (!read_metric(*selected, "module_insns", &probe.module_insns, error)) return false;

        const JsonValue* functions = selected->find("functions");
        if (functions) {
            if (functions->kind != JsonValue::KObj) { *error = "'functions' must be an object"; return false; }
            for (size_t f = 0; f < functions->fields.size(); f++) {
                FunctionBudget budget;
                budget.name = functions->fields[f].first;
                const JsonValue& metrics = functions->fields[f].second;
                if (metrics.kind != JsonValue::KObj) {
                    *error = "function budget for '" + budget.name + "' must be an object";
                    return false;
                }
                for (size_t i = 0; i < metrics.fields.size(); i++) {
                    if (!known_key(kFuncKeys, metrics.fields[i].first)) {
                        *error = "unknown metric '" + metrics.fields[i].first +
                                 "' for function '" + budget.name + "'";
                        return false;
                    }
                }
                if (!read_metric(metrics, "insns", &budget.insns, error)) return false;
                probe.functions.push_back(budget);
            }
        }

        const JsonValue* frames = selected->find("frames");
        if (frames) {
            if (frames->kind != JsonValue::KArr) { *error = "'frames' must be an array"; return false; }
            for (size_t f = 0; f < frames->items.size(); f++) {
                const JsonValue& metrics = frames->items[f];
                if (metrics.kind != JsonValue::KObj) { *error = "each 'frames' entry must be an object"; return false; }
                for (size_t i = 0; i < metrics.fields.size(); i++) {
                    if (!known_key(kFrameKeys, metrics.fields[i].first)) {
                        *error = "unknown metric '" + metrics.fields[i].first + "' in a frames entry";
                        return false;
                    }
                }
                FrameBudget budget;
                const JsonValue* fname = metrics.find("name");
                if (!fname || fname->kind != JsonValue::KStr) {
                    *error = "each 'frames' entry needs a string 'name'";
                    return false;
                }
                budget.name = fname->str_value;
                const JsonValue* occurrence = metrics.find("occurrence");
                if (occurrence) {
                    if (occurrence->kind != JsonValue::KInt || occurrence->int_value < 0) {
                        *error = "'occurrence' must be a non-negative integer";
                        return false;
                    }
                    budget.occurrence = occurrence->int_value;
                }
                if (!read_metric(metrics, "roots", &budget.roots, error)) return false;
                if (!read_metric(metrics, "root_stores", &budget.root_stores, error)) return false;
                if (!read_metric(metrics, "scalar_homes", &budget.scalar_homes, error)) return false;
                if (!read_metric(metrics, "number_scratch", &budget.number_scratch, error)) return false;
                if (!read_metric(metrics, "safepoints", &budget.safepoints, error)) return false;
                probe.frames.push_back(budget);
            }
        }

        if (!probe.module_insns.present && probe.functions.empty() && probe.frames.empty()) {
            *error = "probe '" + probe.name + "' records no thresholds";
            return false;
        }
        out->push_back(probe);
    }
    return true;
}

std::string g_budgets_error;
std::vector<Probe> load_probes() {
    std::vector<Probe> probes;
    if (!load_budgets(kBudgetsPath, &probes, &g_budgets_error)) probes.clear();
    return probes;
}
std::vector<Probe> g_probes = load_probes();

// ---------------------------------------------------------------------------
// Comparison: over budget fails, under budget passes and reports.
// ---------------------------------------------------------------------------

struct Report {
    bool over = false;
    bool under = false;
    std::string detail;
};

void compare(const char* metric, const std::string& where, const Metric& budget,
             long long actual, Report* report) {
    if (!budget.present) return;
    char line[256];
    if (actual > budget.value) {
        report->over = true;
        snprintf(line, sizeof(line), "  GREW  %-16s %-28s %lld -> %lld  (+%lld)\n",
                 metric, where.c_str(), budget.value, actual, actual - budget.value);
        report->detail += line;
    } else if (actual < budget.value) {
        report->under = true;
        snprintf(line, sizeof(line), "  shrank %-15s %-28s %lld -> %lld  (%lld)\n",
                 metric, where.c_str(), budget.value, actual, actual - budget.value);
        report->detail += line;
    }
}

class MirRatchetTest : public ::testing::TestWithParam<Probe> {};

TEST_P(MirRatchetTest, WithinBudget) {
    const Probe& probe = GetParam();

    mir_check::CompileOptions options;
    options.language = probe.language;
    // frame shape comes from the emitter's finalizers, instruction counts from
    // the artifact; both are needed for a meaningful ratchet.
    options.collect_frame_slots = true;

    mir_check::CompileResult compiled = mir_check::compile_and_dump(probe.script, options);
    ASSERT_TRUE(compiled.ok)
        << "probe " << probe.name << " (" << probe.script << ")\n" << compiled.error
        << "\n--- lambda.exe output ---\n" << compiled.output;
    ASSERT_FALSE(compiled.dump.modules.empty())
        << "probe " << probe.name << ": artifact declares no module";

    const std::string& module = compiled.dump.modules[0];
    Report report;

    if (probe.module_insns.present) {
        compare("module_insns", module, probe.module_insns,
                mir_check::count_module_instructions(compiled.dump, module), &report);
    }

    for (size_t f = 0; f < probe.functions.size(); f++) {
        const FunctionBudget& budget = probe.functions[f];

        std::vector<const mir_check::MirScope*> scopes;
        for (size_t i = 0; i < compiled.dump.functions.size(); i++) {
            if (compiled.dump.functions[i].module == module &&
                mir_check::function_name_matches(budget.name, compiled.dump.functions[i].function)) {
                scopes.push_back(&compiled.dump.functions[i]);
            }
        }
        ASSERT_EQ(scopes.size(), 1u)
            << "probe " << probe.name << ": function selector '" << budget.name << "' matched "
            << scopes.size() << " functions in module " << module
            << " (a budget must name exactly one function)";

        if (budget.insns.present) {
            compare("insns", budget.name, budget.insns,
                    mir_check::count_instructions(scopes[0]->lines), &report);
        }
    }

    for (size_t f = 0; f < probe.frames.size(); f++) {
        const FrameBudget& budget = probe.frames[f];
        std::vector<const mir_check::FrameSlots*> slots;
        for (size_t i = 0; i < compiled.frame_slots.size(); i++) {
            if (mir_check::function_name_matches(budget.name, compiled.frame_slots[i].function)) {
                slots.push_back(&compiled.frame_slots[i]);
            }
        }
        ASSERT_LT((size_t)budget.occurrence, slots.size())
            << "probe " << probe.name << ": frame selector '" << budget.name
            << "' occurrence " << budget.occurrence << " does not exist ("
            << slots.size() << " telemetry records matched)";
        const mir_check::FrameSlots* slot = slots[(size_t)budget.occurrence];

        char where[128];
        snprintf(where, sizeof(where), "%s[%lld]", budget.name.c_str(), budget.occurrence);
        compare("roots", where, budget.roots, slot->roots, &report);
        compare("root_stores", where, budget.root_stores, slot->root_stores, &report);
        compare("scalar_homes", where, budget.scalar_homes, slot->scalar_homes, &report);
        compare("number_scratch", where, budget.number_scratch, slot->number_scratch, &report);
        compare("safepoints", where, budget.safepoints, slot->safepoints, &report);
    }

    const std::string context =
        "\nprobe:    " + probe.name +
        "\nscript:   " + probe.script +
        "\nprofile:  " + probe.profile +
        "\nbudgets:  " + kBudgetsPath +
        "\nartifact: " + compiled.dump_path +
        "\n" + report.detail;

    // 0% slack: any growth is a failure that must be reviewed and the threshold
    // lifted in the same commit.
    EXPECT_FALSE(report.over)
        << "emitted MIR grew beyond its recorded budget." << context
        << "\nIf the growth is intended, review the emission diff and update "
        << kBudgetsPath << " in the same commit.";

    if (report.under && !report.over) {
        // a shrink is good news, but the budget stays pinned to actuals only if
        // someone commits the tighter numbers; the test never edits its config.
        std::cout << "[ RATCHET  ] " << probe.name
                  << " emits less than its budget; re-baseline " << kBudgetsPath
                  << " to lock the improvement in:\n" << report.detail;
    }
}

std::string probe_name(const ::testing::TestParamInfo<Probe>& info) {
    return info.param.name;
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MirRatchetTest);

INSTANTIATE_TEST_SUITE_P(Probes, MirRatchetTest, ::testing::ValuesIn(g_probes), probe_name);

// a malformed or unreadable budgets file must not read as "no probes to check".
TEST(MirRatchetConfig, LoadsProbes) {
    EXPECT_TRUE(g_budgets_error.empty()) << kBudgetsPath << ": " << g_budgets_error;
    EXPECT_FALSE(g_probes.empty()) << "no probes configured in " << kBudgetsPath;
}

}  // namespace
