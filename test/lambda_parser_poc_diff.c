// Parser corpus acceptance differential. This standalone utility keeps the
// Tree-sitter Lambda/JS/TS grammars out of production parser builds.

#include "lambda/js/parser/js_parser.h"
#include "lambda/runtime/parser/lambda_rd_parser.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

const TSLanguage* tree_sitter_lambda(void);
const TSLanguage* tree_sitter_javascript(void);
const TSLanguage* tree_sitter_typescript(void);

static char* read_source(const char* path, size_t* length_out) {
    FILE* file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    char* source = (char*)malloc((size_t)length + 1);
    if (!source) { fclose(file); return NULL; }
    if (fread(source, 1, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(source);
        return NULL;
    }
    fclose(file);
    source[length] = '\0';
    *length_out = (size_t)length;
    return source;
}

static bool parse_error_equal(const LambdaParseError* left,
                              const LambdaParseError* right) {
    const char* left_message = left->message ? left->message : "";
    const char* right_message = right->message ? right->message : "";
    return left->span.start_byte == right->span.start_byte &&
        left->span.end_byte == right->span.end_byte &&
        left->actual_kind == right->actual_kind &&
        strcmp(left_message, right_message) == 0;
}

static bool parse_metrics_equal(const LambdaParseMetrics* left,
                                const LambdaParseMetrics* right) {
    return left->token_count == right->token_count &&
        left->reduction_count == right->reduction_count &&
        left->max_recursion_depth == right->max_recursion_depth &&
        left->structural_hash == right->structural_hash;
}

static bool parse_report_equal(const LambdaParseReport* left,
                               const LambdaParseReport* right) {
    if (left->status != right->status || left->error_count != right->error_count ||
            left->recovered != right->recovered) return false;
    for (uint32_t index = 0; index < left->error_count; index++) {
        if (!parse_error_equal(&left->errors[index], &right->errors[index])) return false;
    }
    return true;
}

// The fuzz runner calls this path for arbitrary source. It makes parser
// determinism and bounded recovery a real test target instead of treating a
// C/Tree-sitter agreement as sufficient evidence (D1.9, D1.10, D8.1.2v3).
static bool lambda_parser_stable(const char* source, size_t length,
                                 LambdaParseStatus* status_out,
                                 LambdaParseError* error_out) {
    LambdaParseMetrics first_metrics = {0};
    LambdaParseMetrics second_metrics = {0};
    LambdaParseError first_error = {0};
    LambdaParseError second_error = {0};
    LambdaParseStatus first_status = lambda_rd_parse_source(source, length,
        NULL, NULL, &first_metrics, &first_error);
    LambdaParseStatus second_status = lambda_rd_parse_source(source, length,
        NULL, NULL, &second_metrics, &second_error);
    LambdaParseReport first_report = {0};
    LambdaParseReport second_report = {0};
    LambdaParseStatus first_recovery = lambda_rd_parse_recovering(source, length,
        &first_report);
    LambdaParseStatus second_recovery = lambda_rd_parse_recovering(source, length,
        &second_report);
    if (status_out) *status_out = first_status;
    if (error_out) *error_out = first_error;
    return first_status == second_status &&
        parse_error_equal(&first_error, &second_error) &&
        parse_metrics_equal(&first_metrics, &second_metrics) &&
        first_recovery == second_recovery &&
        parse_report_equal(&first_report, &second_report);
}

static int run_lambda_manifest(const char* manifest_path) {
    FILE* manifest = fopen(manifest_path, "rb");
    if (!manifest) return 3;
    TSParser* parser = ts_parser_new();
    if (!parser || !ts_parser_set_language(parser, tree_sitter_lambda())) return 3;

    unsigned int total = 0;
    unsigned int ts_ok = 0;
    unsigned int rd_ok = 0;
    unsigned int missing = 0;
    unsigned int extra = 0;
    unsigned int unstable = 0;
    char line[4096];
    while (fgets(line, sizeof(line), manifest)) {
        if (line[0] == '#' || strncmp(line, "path\t", 5) == 0) continue;
        char* tab = strchr(line, '\t');
        if (!tab) return 3;
        *tab = '\0';
        size_t length = 0;
        char* source = read_source(line, &length);
        if (!source) return 3;
        TSTree* tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)length);
        bool tree_ok = !ts_node_has_error(ts_tree_root_node(tree));
        LambdaParseError error = {0};
        LambdaParseStatus rd_status = LAMBDA_PARSE_ERROR;
        bool stable = lambda_parser_stable(source, length, &rd_status, &error);
        total++;
        if (tree_ok) ts_ok++;
        if (rd_status == LAMBDA_PARSE_OK) rd_ok++;
        if (tree_ok && rd_status != LAMBDA_PARSE_OK) {
            missing++;
            fprintf(stdout, "missing\t%s\t%s\t%u\n", line,
                error.message ? error.message : "no error message", error.span.start_byte);
        }
        if (!tree_ok && rd_status == LAMBDA_PARSE_OK) {
            extra++;
            fprintf(stdout, "extra\t%s\n", line);
        }
        if (!stable) {
            unstable++;
            fprintf(stdout, "unstable\t%s\n", line);
        }
        ts_tree_delete(tree);
        free(source);
    }
    fprintf(stderr, "total=%u ts_ok=%u rd_ok=%u missing=%u extra=%u unstable=%u\n",
        total, ts_ok, rd_ok, missing, extra, unstable);
    ts_parser_delete(parser);
    fclose(manifest);
    // Extra accepted files are reported for source-level classification; only
    // rejecting a Tree-sitter-valid source fails the current P1.3 valid-side gate.
    return missing || unstable ? 1 : 0;
}

static int run_lambda_stability_manifest(const char* manifest_path) {
    FILE* manifest = fopen(manifest_path, "rb");
    if (!manifest) return 3;
    unsigned int total = 0;
    unsigned int unstable = 0;
    char line[4096];
    while (fgets(line, sizeof(line), manifest)) {
        if (line[0] == '#' || strncmp(line, "path\t", 5) == 0) continue;
        char* tab = strchr(line, '\t');
        if (tab) *tab = '\0';
        char* line_end = strpbrk(line, "\r\n");
        if (line_end) *line_end = '\0';
        size_t length = 0;
        char* source = read_source(line, &length);
        if (!source) { fclose(manifest); return 3; }
        LambdaParseStatus status = LAMBDA_PARSE_ERROR;
        if (!lambda_parser_stable(source, length, &status, NULL)) {
            unstable++;
            fprintf(stdout, "unstable\t%s\n", line);
        }
        total++;
        free(source);
    }
    fprintf(stderr, "total=%u unstable=%u\n", total, unstable);
    fclose(manifest);
    return unstable ? 1 : 0;
}

static bool js_direct_accepts(const char* source, size_t length,
                              bool typescript, JsParseError* error) {
    JsParseMode modes[2] = {
        typescript ? (JsParseMode)(JS_PARSE_SCRIPT | JS_PARSE_TYPESCRIPT)
                   : JS_PARSE_SCRIPT,
        typescript ? (JsParseMode)(JS_PARSE_MODULE | JS_PARSE_TYPESCRIPT)
                   : JS_PARSE_MODULE,
    };
    for (size_t i = 0; i < 2; i++) {
        JsParseError candidate = {0};
        if (js_parser_parse_source(source, length, modes[i], NULL, NULL, NULL,
                &candidate) == JS_PARSE_OK) {
            return true;
        }
        if (error) *error = candidate;
    }
    return false;
}

static int run_js_manifest(const char* manifest_path) {
    FILE* manifest = fopen(manifest_path, "rb");
    if (!manifest) return 3;
    TSParser* parser = ts_parser_new();
    if (!parser) return 3;

    unsigned int total = 0;
    unsigned int ts_ok = 0;
    unsigned int c_ok = 0;
    unsigned int missing = 0;
    unsigned int extra = 0;
    char line[4096];
    while (fgets(line, sizeof(line), manifest)) {
        if (line[0] == '#' || strncmp(line, "language\t", 9) == 0) continue;
        char* language = line;
        char* tab = strchr(language, '\t');
        if (!tab) return 3;
        *tab++ = '\0';
        char* path = tab;
        char* path_end = strpbrk(path, "\r\n\t");
        if (path_end) *path_end = '\0';
        bool typescript = strcmp(language, "typescript") == 0;
        const TSLanguage* grammar = typescript ? tree_sitter_typescript()
            : (strcmp(language, "javascript") == 0 ? tree_sitter_javascript() : NULL);
        if (!grammar || !ts_parser_set_language(parser, grammar)) return 3;

        size_t length = 0;
        char* source = read_source(path, &length);
        if (!source) return 3;
        TSTree* tree = ts_parser_parse_string(parser, NULL, source, (uint32_t)length);
        bool tree_ok = tree && !ts_node_has_error(ts_tree_root_node(tree));
        JsParseError error = {0};
        bool direct_ok = js_direct_accepts(source, length, typescript, &error);
        total++;
        if (tree_ok) ts_ok++;
        if (direct_ok) c_ok++;
        if (tree_ok && !direct_ok) {
            missing++;
            fprintf(stdout, "missing\t%s\t%s\t%u\n", path,
                error.message ? error.message : "no error message", error.span.start_byte);
        }
        if (!tree_ok && direct_ok) {
            extra++;
            fprintf(stdout, "extra\t%s\n", path);
        }
        if (tree) ts_tree_delete(tree);
        free(source);
    }
    fprintf(stderr, "total=%u ts_ok=%u c_ok=%u missing=%u extra=%u\n",
        total, ts_ok, c_ok, missing, extra);
    ts_parser_delete(parser);
    fclose(manifest);
    return missing ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc == 3 && strcmp(argv[1], "--lambda-stability") == 0) {
        return run_lambda_stability_manifest(argv[2]);
    }
    if (argc == 2) return run_lambda_manifest(argv[1]);
    if (argc == 3 && strcmp(argv[1], "--js") == 0) {
        return run_js_manifest(argv[2]);
    }
    return 3;
}
