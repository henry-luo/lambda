// Phase 1 corpus acceptance differential. This is a standalone test utility,
// not the production parser, so it links both the shipped grammar archive and
// the first-party recognizer.

#include "lambda/runtime/parser/lambda_rd_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>

const TSLanguage* tree_sitter_lambda(void);

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

int main(int argc, char** argv) {
    if (argc != 2) return 3;
    FILE* manifest = fopen(argv[1], "rb");
    if (!manifest) return 3;
    TSParser* parser = ts_parser_new();
    if (!parser || !ts_parser_set_language(parser, tree_sitter_lambda())) return 3;

    unsigned int total = 0;
    unsigned int ts_ok = 0;
    unsigned int rd_ok = 0;
    unsigned int missing = 0;
    unsigned int extra = 0;
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
        LambdaParseStatus rd_status = lambda_rd_parse_source(source, length,
            NULL, NULL, NULL, &error);
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
        ts_tree_delete(tree);
        free(source);
    }
    fprintf(stderr, "total=%u ts_ok=%u rd_ok=%u missing=%u extra=%u\n",
        total, ts_ok, rd_ok, missing, extra);
    ts_parser_delete(parser);
    fclose(manifest);
    // Extra accepted files are reported for source-level classification; only
    // rejecting a Tree-sitter-valid source fails the current P1.3 valid-side gate.
    return missing ? 1 : 0;
}
