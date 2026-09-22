// Differential routing audit: production structural scanner versus AST POC.
#include <gtest/gtest.h>

#include <string.h>

#include "js_regex_router_poc.h"
#include "../lambda/js/js_regex_router_scanner.h"

extern "C" {
#include "../lib/file.h"
#include "../lib/shell.h"
#include "../lib/strbuf.h"
}

namespace {

enum { ROUTER_POC_EXAMPLES = 12, ROUTER_POC_PATTERN_CAP = 256 };

struct RouterStats {
    int valid_patterns;
    int parse_errors;
    int same_route;
    int scanner_only_backtrack;
    int poc_only_backtrack;
    int poc_nullable_discard_only;
    char scanner_examples[ROUTER_POC_EXAMPLES][ROUTER_POC_PATTERN_CAP];
    char poc_examples[ROUTER_POC_EXAMPLES][ROUTER_POC_PATTERN_CAP];
};

typedef void (*RouterPocCaseVisitor)(const char* pattern, bool multiline, void* user_data);

static void router_poc_copy_example(char out[ROUTER_POC_EXAMPLES][ROUTER_POC_PATTERN_CAP],
                                    int index, const char* pattern) {
    if (index >= ROUTER_POC_EXAMPLES) return;
    int len = (int)strlen(pattern);
    if (len >= ROUTER_POC_PATTERN_CAP) len = ROUTER_POC_PATTERN_CAP - 1;
    memcpy(out[index], pattern, (size_t)len);
    out[index][len] = '\0';
}

static void router_poc_compare(RouterStats* stats, const char* pattern, bool multiline) {
    int pattern_len = (int)strlen(pattern);
    JsRegexPocResult poc = js_regex_poc_route(pattern, pattern_len, multiline);
    if (poc.route == JS_REGEX_POC_SYNTAX_ERROR) {
        stats->parse_errors++;
        return;
    }
    stats->valid_patterns++;
    bool scanner_backtrack = js_regex_scanner_needs_backtrack(pattern, pattern_len, multiline);
    bool poc_backtrack = poc.route == JS_REGEX_POC_BACKTRACK_REQUIRED;
    if (scanner_backtrack == poc_backtrack) {
        stats->same_route++;
    } else if (scanner_backtrack) {
        router_poc_copy_example(stats->scanner_examples, stats->scanner_only_backtrack++, pattern);
    } else {
        router_poc_copy_example(stats->poc_examples, stats->poc_only_backtrack++, pattern);
        if (poc.features & JS_REGEX_POC_FEATURE_NULLABLE_DISCARD) {
            stats->poc_nullable_discard_only++;
        }
    }
}

static void router_poc_compare_visitor(const char* pattern, bool multiline, void* user_data) {
    router_poc_compare((RouterStats*)user_data, pattern, multiline);
}

static bool router_poc_append(char* buffer, int* length, const char* text) {
    int text_len = (int)strlen(text);
    if (*length + text_len >= ROUTER_POC_PATTERN_CAP) return false;
    memcpy(buffer + *length, text, (size_t)text_len);
    *length += text_len;
    buffer[*length] = '\0';
    return true;
}

static void router_poc_visit_generated(RouterPocCaseVisitor visitor, void* user_data) {
    static const char* bodies[] = {
        "a", "a?", "a*", "a+", "a{0,3}", "a{1,3}", "a{1,3}?",
        "a?b", "a*b", "(a?)b", "(?:a?)b", "(a?)(b)", "(a*)b", "a|b?",
    };
    static const char* outer_quantifiers[] = {
        "*", "+", "?", "{0}", "{0,1}", "{1}", "{1,1}", "{1,2}", "{2,}", "{2,4}",
    };
    int body_count = (int)(sizeof(bodies) / sizeof(bodies[0]));
    int quantifier_count = (int)(sizeof(outer_quantifiers) / sizeof(outer_quantifiers[0]));
    for (int left = 0; left < body_count; left++) {
        for (int quantifier = 0; quantifier < quantifier_count; quantifier++) {
            char pattern[ROUTER_POC_PATTERN_CAP] = {};
            int length = 0;
            if (!router_poc_append(pattern, &length, "(") ||
                !router_poc_append(pattern, &length, bodies[left]) ||
                !router_poc_append(pattern, &length, ")") ||
                !router_poc_append(pattern, &length, outer_quantifiers[quantifier])) continue;
            visitor(pattern, false, user_data);
            visitor(pattern, true, user_data);
        }
    }
    for (int left = 0; left < body_count; left++) {
        for (int right = 0; right < body_count; right++) {
            for (int quantifier = 0; quantifier < quantifier_count; quantifier++) {
                char pattern[ROUTER_POC_PATTERN_CAP] = {};
                int length = 0;
                if (!router_poc_append(pattern, &length, "((") ||
                    !router_poc_append(pattern, &length, bodies[left]) ||
                    !router_poc_append(pattern, &length, ")(") ||
                    !router_poc_append(pattern, &length, bodies[right]) ||
                    !router_poc_append(pattern, &length, "))") ||
                    !router_poc_append(pattern, &length, outer_quantifiers[quantifier])) continue;
                visitor(pattern, false, user_data);
                visitor(pattern, true, user_data);
            }
        }
    }
}

static void router_poc_visit_curated(RouterPocCaseVisitor visitor, void* user_data) {
    static const char* ordinary[] = {
        "", "abc", "[a?]+", "\\?", "\\(a\\)", "[\\1]", "\\p{ASCII}",
        "(a?){1}", "(a?){1,1}", "(a?){0,1}", "(a{1,3}?)*", "((a?)b)*",
        "((a?)(b))*", "(a{0,3})*", "(?:a{0,3}b)*", "(a*)*", "(a+?)*",
        "(a?b?\x3f)*", "(a?){1,2}", "(a?){2,}",
    };
    static const char* backtracking[] = {
        "(a)\\1", "(?<word>a)\\k<word>", "(?=a)a", "(?!a)a", "(?<=a)b", "(?<!a)b",
    };
    static const char* anchors[] = { "^a", "a$", "(^a$)", "[^^$]" };
    for (int i = 0; i < (int)(sizeof(ordinary) / sizeof(ordinary[0])); i++) {
        visitor(ordinary[i], false, user_data);
        visitor(ordinary[i], true, user_data);
    }
    for (int i = 0; i < (int)(sizeof(backtracking) / sizeof(backtracking[0])); i++) {
        visitor(backtracking[i], false, user_data);
        visitor(backtracking[i], true, user_data);
    }
    for (int i = 0; i < (int)(sizeof(anchors) / sizeof(anchors[0])); i++) {
        visitor(anchors[i], false, user_data);
        visitor(anchors[i], true, user_data);
    }
}

static void router_poc_run_curated(RouterStats* stats) {
    router_poc_visit_curated(router_poc_compare_visitor, stats);
}

static void router_poc_generate(RouterStats* stats) {
    router_poc_visit_generated(router_poc_compare_visitor, stats);
}

struct RouterPocSemanticScript {
    StrBuf* source;
    int pattern_count;
};

static void router_poc_append_js_string(StrBuf* source, const char* pattern) {
    strbuf_append_char(source, '\'');
    for (int i = 0; pattern[i]; i++) {
        char c = pattern[i];
        if (c == '\\' || c == '\'') strbuf_append_char(source, '\\');
        if (c == '\n') strbuf_append_str(source, "\\n");
        else if (c == '\r') strbuf_append_str(source, "\\r");
        else strbuf_append_char(source, c);
    }
    strbuf_append_char(source, '\'');
}

static bool router_poc_write_semantic_script(RouterPocSemanticScript* script) {
    static const char* patterns[] = {
        "(a?)?", "(a?){0,1}", "(a{0,3})*", "(a*)*", "(?:|a)*",
        "(?:(a)|b)+", "((a)?b)*", "(?:(a)|b){2}", "(a)\\1",
        "(ab)*", "((a?)b)*", "(a?){1}", "(?:a{0,3}b)*",
    };
    script->source = strbuf_new();
    if (!script->source) return false;
    strbuf_append_str(script->source, "var patterns = [\n");
    for (int i = 0; i < (int)(sizeof(patterns) / sizeof(patterns[0])); i++) {
        router_poc_append_js_string(script->source, patterns[i]);
        strbuf_append_str(script->source, ",\n");
        script->pattern_count++;
    }
    strbuf_append_str(script->source,
        "];\n"
        "var subjects = [''];\n"
        "var frontier = [''];\n"
        "for (var depth = 0; depth < 7; depth++) {\n"
        "  var next = [];\n"
        "  for (var i = 0; i < frontier.length; i++) {\n"
        "    next.push(frontier[i] + 'a'); next.push(frontier[i] + 'b');\n"
        "  }\n"
        "  for (var j = 0; j < next.length; j++) subjects.push(next[j]);\n"
        "  frontier = next;\n"
        "}\n"
        "var baseSubjects = subjects.slice(0);\n"
        "for (var k = 0; k < baseSubjects.length; k++) {\n"
        "  subjects.push('x' + baseSubjects[k]); subjects.push(baseSubjects[k] + 'x');\n"
        "}\n"
        "function foldText(text) {\n"
        "  for (var p = 0; p < text.length; p++) hash = (((hash ^ text.charCodeAt(p)) * 16777619) >>> 0);\n"
        "}\n"
        "for (var patternIndex = 0; patternIndex < patterns.length; patternIndex++) {\n"
        "  var regex = new RegExp(patterns[patternIndex]);\n"
        "  var hash = 2166136261;\n"
        "  for (var subjectIndex = 0; subjectIndex < subjects.length; subjectIndex++) {\n"
        "    var match = regex.exec(subjects[subjectIndex]);\n"
        "    if (match === null) { foldText('N;'); continue; }\n"
        "    foldText('M:'); foldText('' + match.index); foldText(':'); foldText('' + match.length);\n"
        "    for (var groupIndex = 0; groupIndex < match.length; groupIndex++) {\n"
        "      foldText(':'); foldText(match[groupIndex] === undefined ? 'U' : match[groupIndex]);\n"
        "    }\n"
        "    foldText(';');\n"
        "  }\n"
        "  console.log(hash >>> 0);\n"
        "}\n");
    return write_binary_file("temp/js_regex_router_poc_semantics.js",
        script->source->str, script->source->length) == 0;
}

static bool router_poc_next_digest(const char** cursor, const char** digest, int* digest_len) {
    if (!cursor || !*cursor || !**cursor) return false;
    *digest = *cursor;
    *digest_len = 0;
    while ((*cursor)[*digest_len] && (*cursor)[*digest_len] != '\n') (*digest_len)++;
    *cursor += *digest_len;
    if (**cursor == '\n') (*cursor)++;
    return true;
}

static int router_poc_count_digests(const char* output) {
    int count = 0;
    const char* cursor = output;
    const char* digest = NULL;
    int digest_len = 0;
    while (router_poc_next_digest(&cursor, &digest, &digest_len)) count++;
    return count;
}

static int router_poc_count_digest_differences(const char* node_output, const char* lambda_output) {
    int differences = 0;
    const char* node_cursor = node_output;
    const char* lambda_cursor = lambda_output;
    while (true) {
        const char* node_digest = NULL;
        const char* lambda_digest = NULL;
        int node_len = 0;
        int lambda_len = 0;
        bool has_node = router_poc_next_digest(&node_cursor, &node_digest, &node_len);
        bool has_lambda = router_poc_next_digest(&lambda_cursor, &lambda_digest, &lambda_len);
        if (!has_node && !has_lambda) return differences;
        if (!has_node || !has_lambda || node_len != lambda_len ||
            memcmp(node_digest, lambda_digest, (size_t)node_len) != 0) differences++;
    }
}

TEST(JsRegexRouterPoc, ScannerAndAstRouteAudit) {
    RouterStats stats = {};
    router_poc_run_curated(&stats);
    router_poc_generate(&stats);

    RecordProperty("valid_patterns", stats.valid_patterns);
    RecordProperty("parse_errors", stats.parse_errors);
    RecordProperty("same_route", stats.same_route);
    RecordProperty("scanner_only_backtrack", stats.scanner_only_backtrack);
    RecordProperty("poc_only_backtrack", stats.poc_only_backtrack);
    RecordProperty("poc_nullable_discard_only", stats.poc_nullable_discard_only);
    static const char* scanner_example_names[] = {
        "scanner_only_example_1", "scanner_only_example_2", "scanner_only_example_3",
    };
    static const char* poc_example_names[] = {
        "poc_only_example_1", "poc_only_example_2", "poc_only_example_3",
    };
    for (int i = 0; i < 3; i++) {
        RecordProperty(scanner_example_names[i], stats.scanner_examples[i]);
        RecordProperty(poc_example_names[i], stats.poc_examples[i]);
    }

    EXPECT_EQ(stats.parse_errors, 0);
    EXPECT_GT(stats.valid_patterns, 4000);
    EXPECT_GT(stats.same_route, 0);
    EXPECT_GT(stats.scanner_only_backtrack, 0);
    EXPECT_GT(stats.poc_only_backtrack, 0);
    EXPECT_GT(stats.poc_nullable_discard_only, 0);
}

TEST(JsRegexRouterPoc, StructuralRoutesPreserveNodeExecResults) {
    RouterPocSemanticScript script = {};
    ASSERT_TRUE(router_poc_write_semantic_script(&script));
    ASSERT_GT(script.pattern_count, 0);

    ShellOptions options = {};
    options.timeout_ms = 60000;
    options.merge_stderr = true;
    const char* node_args[] = { "node", "temp/js_regex_router_poc_semantics.js", NULL };
    const char* lambda_args[] = {
        "./lambda.exe", "js", "temp/js_regex_router_poc_semantics.js", "--no-log", NULL,
    };
    ShellResult node_result = shell_exec("node", node_args, &options);
    ShellResult lambda_result = shell_exec("./lambda.exe", lambda_args, &options);
    if (node_result.exit_code != 0 || lambda_result.exit_code != 0) {
        ADD_FAILURE() << "semantic differential runner failed: node=" << node_result.exit_code
                      << ", lambda=" << lambda_result.exit_code;
        shell_result_free(&node_result);
        shell_result_free(&lambda_result);
        strbuf_free(script.source);
        return;
    }

    const char* node_output = node_result.stdout_buf ? node_result.stdout_buf : "";
    const char* lambda_output = lambda_result.stdout_buf ? lambda_result.stdout_buf : "";
    int node_digests = router_poc_count_digests(node_output);
    int lambda_digests = router_poc_count_digests(lambda_output);
    int divergent_patterns = router_poc_count_digest_differences(node_output, lambda_output);
    RecordProperty("semantic_patterns", script.pattern_count);
    RecordProperty("semantic_subjects", 765);
    RecordProperty("semantic_exec_calls", script.pattern_count * 765);
    RecordProperty("semantic_divergent_patterns", divergent_patterns);
    RecordProperty("semantic_matching_patterns", script.pattern_count - divergent_patterns);
    EXPECT_EQ(node_digests, script.pattern_count);
    EXPECT_EQ(lambda_digests, script.pattern_count);
    EXPECT_EQ(divergent_patterns, 0);
    shell_result_free(&node_result);
    shell_result_free(&lambda_result);
    strbuf_free(script.source);
}

TEST(JsRegexRouterPoc, StructuralRoutesExactRepetitionShapes) {
    static const struct {
        const char* pattern;
        bool route_to_backtracker;
        unsigned int reason;
    } cases[] = {
        { "(ab)*", false, JS_REGEX_SCANNER_REASON_NONE },
        { "((a?)b)*", false, JS_REGEX_SCANNER_REASON_NONE },
        { "(a?){1}", false, JS_REGEX_SCANNER_REASON_NONE },
        { "(a?){2}", false, JS_REGEX_SCANNER_REASON_NONE },
        { "(?:a{0,3}b)*", false, JS_REGEX_SCANNER_REASON_NONE },
        { "(a?)?", true, JS_REGEX_SCANNER_REASON_EMPTY_OPTIONAL_ITERATION },
        { "(a?){0,1}", true, JS_REGEX_SCANNER_REASON_EMPTY_OPTIONAL_ITERATION },
        { "(a{0,3})*", true, JS_REGEX_SCANNER_REASON_EMPTY_OPTIONAL_ITERATION },
        { "(a*)*", true, JS_REGEX_SCANNER_REASON_EMPTY_OPTIONAL_ITERATION },
        { "(?:|a)*", true, JS_REGEX_SCANNER_REASON_EMPTY_OPTIONAL_ITERATION },
        { "(?:(a)|b)+", true, JS_REGEX_SCANNER_REASON_CAPTURE_RESET },
        { "((a)?b)*", true, JS_REGEX_SCANNER_REASON_CAPTURE_RESET },
        { "(?:(a)|b){2}", true, JS_REGEX_SCANNER_REASON_CAPTURE_RESET },
        { "[A-Za-z0-9_-]{80,16384}", true, JS_REGEX_SCANNER_REASON_RE2_REPEAT_LIMIT },
        { "[\\s\\S]{0,2048}", true, JS_REGEX_SCANNER_REASON_RE2_REPEAT_LIMIT },
        { "(a)\\1", true, JS_REGEX_SCANNER_REASON_BACKREFERENCE },
        { "(?=a)a", true, JS_REGEX_SCANNER_REASON_ASSERTION },
    };
    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        int pattern_len = (int)strlen(cases[i].pattern);
        JsRegexScannerAnalysis analysis = js_regex_scanner_analyze(
            cases[i].pattern, pattern_len, false);
        EXPECT_TRUE(analysis.complete) << cases[i].pattern;
        EXPECT_EQ(js_regex_scanner_needs_backtrack(cases[i].pattern, pattern_len, false),
                  cases[i].route_to_backtracker) << cases[i].pattern;
        if (cases[i].reason != JS_REGEX_SCANNER_REASON_NONE) {
            EXPECT_NE(analysis.reasons & cases[i].reason, 0u) << cases[i].pattern;
        }
    }
}

TEST(JsRegexRouterPoc, ResourceExhaustionIsNotReportedAsNoMatch) {
    static const char* source =
        "var input = '';\n"
        "for (var i = 0; i < 40; i++) input += 'a';\n"
        "new RegExp('((a+)+)\\\\1$').test(input + '!');\n"
        "console.log('unreachable');\n";
    ASSERT_EQ(write_binary_file("temp/js_regex_resource_exhaustion.js", source,
                                (int)strlen(source)), 0);

    ShellOptions options = {};
    options.timeout_ms = 60000;
    options.merge_stderr = true;
    const char* lambda_args[] = {
        "./lambda.exe", "js", "temp/js_regex_resource_exhaustion.js", "--no-log", NULL,
    };
    ShellResult result = shell_exec("./lambda.exe", lambda_args, &options);
    const char* output = result.stdout_buf ? result.stdout_buf : "";
    EXPECT_NE(result.exit_code, 0);
    EXPECT_NE(strstr(output, "RegExp match exceeded engine resources"), nullptr);
    EXPECT_EQ(strstr(output, "unreachable"), nullptr);
    shell_result_free(&result);
}

} // namespace
