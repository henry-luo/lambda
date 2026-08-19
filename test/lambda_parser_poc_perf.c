// Phase 1 parser timing utility.  It preloads the manifest before timing so
// the numbers isolate parse work from filesystem and process startup costs.

#include "lambda/runtime/parser/lambda_rd_parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <tree_sitter/api.h>

const TSLanguage* tree_sitter_lambda(void);

enum {
    WARMUP_RUNS = 1,
    MEASURED_RUNS = 5,
    LARGEST_SOURCE_COUNT = 16,
};

typedef struct CorpusEntry {
    char* source;
    size_t length;
    bool in_largest_cohort;
} CorpusEntry;

typedef struct Corpus {
    CorpusEntry* entries;
    size_t count;
    size_t capacity;
    size_t bytes;
    size_t largest_bytes;
    size_t largest_count;
} Corpus;

typedef struct ParseRun {
    double elapsed_seconds;
    double p50_file_seconds;
    double p95_file_seconds;
    double largest_seconds;
    unsigned int ok_count;
    unsigned int incomplete_count;
    unsigned int error_count;
} ParseRun;

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

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

static void corpus_destroy(Corpus* corpus) {
    for (size_t i = 0; i < corpus->count; i++) free(corpus->entries[i].source);
    free(corpus->entries);
    memset(corpus, 0, sizeof(*corpus));
}

static bool corpus_append(Corpus* corpus, char* source, size_t length) {
    if (corpus->count == corpus->capacity) {
        size_t capacity = corpus->capacity ? corpus->capacity * 2 : 256;
        CorpusEntry* entries = (CorpusEntry*)realloc(corpus->entries,
            capacity * sizeof(*entries));
        if (!entries) return false;
        corpus->entries = entries;
        corpus->capacity = capacity;
    }
    CorpusEntry* entry = &corpus->entries[corpus->count++];
    entry->source = source;
    entry->length = length;
    entry->in_largest_cohort = false;
    corpus->bytes += length;
    return true;
}

static bool corpus_load(Corpus* corpus, const char* manifest_path) {
    FILE* manifest = fopen(manifest_path, "rb");
    if (!manifest) return false;
    char line[4096];
    while (fgets(line, sizeof(line), manifest)) {
        if (line[0] == '#' || strncmp(line, "path\t", 5) == 0) continue;
        char* tab = strchr(line, '\t');
        if (!tab) { fclose(manifest); return false; }
        *tab = '\0';
        size_t length = 0;
        char* source = read_source(line, &length);
        if (!source || !corpus_append(corpus, source, length)) {
            free(source);
            fclose(manifest);
            return false;
        }
    }
    fclose(manifest);
    return corpus->count != 0;
}

static void corpus_mark_largest(Corpus* corpus) {
    for (size_t selected = 0;
            selected < LARGEST_SOURCE_COUNT && selected < corpus->count; selected++) {
        size_t largest_index = corpus->count;
        for (size_t i = 0; i < corpus->count; i++) {
            CorpusEntry* entry = &corpus->entries[i];
            if (entry->in_largest_cohort) continue;
            if (largest_index == corpus->count ||
                    entry->length > corpus->entries[largest_index].length) {
                largest_index = i;
            }
        }
        CorpusEntry* entry = &corpus->entries[largest_index];
        entry->in_largest_cohort = true;
        corpus->largest_count++;
        corpus->largest_bytes += entry->length;
    }
}

static int compare_double(const void* left, const void* right) {
    double left_value = *(const double*)left;
    double right_value = *(const double*)right;
    return (left_value > right_value) - (left_value < right_value);
}

static double percentile(double* samples, size_t sample_count, unsigned int percent) {
    qsort(samples, sample_count, sizeof(*samples), compare_double);
    size_t index = (sample_count - 1) * percent / 100u;
    return samples[index];
}

static double median(const double* values, size_t count) {
    double sorted[MEASURED_RUNS];
    if (count > MEASURED_RUNS) return 0.0;
    for (size_t i = 0; i < count; i++) sorted[i] = values[i];
    return percentile(sorted, count, 50);
}

static bool run_tree_sitter(TSParser* parser, const Corpus* corpus, ParseRun* run) {
    double* file_seconds = (double*)malloc(corpus->count * sizeof(*file_seconds));
    if (!file_seconds) return false;
    memset(run, 0, sizeof(*run));
    double started = monotonic_seconds();
    for (size_t i = 0; i < corpus->count; i++) {
        const CorpusEntry* entry = &corpus->entries[i];
        double file_started = monotonic_seconds();
        TSTree* tree = ts_parser_parse_string(parser, NULL, entry->source,
            (uint32_t)entry->length);
        double file_finished = monotonic_seconds();
        if (!tree) { free(file_seconds); return false; }
        file_seconds[i] = file_finished - file_started;
        if (entry->in_largest_cohort) run->largest_seconds += file_seconds[i];
        if (ts_node_has_error(ts_tree_root_node(tree))) run->error_count++;
        else run->ok_count++;
        ts_tree_delete(tree);
    }
    run->elapsed_seconds = monotonic_seconds() - started;
    run->p50_file_seconds = percentile(file_seconds, corpus->count, 50);
    run->p95_file_seconds = percentile(file_seconds, corpus->count, 95);
    free(file_seconds);
    return true;
}

static bool run_rd_parser(const Corpus* corpus, ParseRun* run) {
    double* file_seconds = (double*)malloc(corpus->count * sizeof(*file_seconds));
    if (!file_seconds) return false;
    memset(run, 0, sizeof(*run));
    double started = monotonic_seconds();
    for (size_t i = 0; i < corpus->count; i++) {
        const CorpusEntry* entry = &corpus->entries[i];
        double file_started = monotonic_seconds();
        LambdaParseStatus status = lambda_rd_parse_source(entry->source, entry->length,
            NULL, NULL, NULL, NULL);
        double file_finished = monotonic_seconds();
        file_seconds[i] = file_finished - file_started;
        if (entry->in_largest_cohort) run->largest_seconds += file_seconds[i];
        if (status == LAMBDA_PARSE_OK) run->ok_count++;
        else if (status == LAMBDA_PARSE_INCOMPLETE) run->incomplete_count++;
        else run->error_count++;
    }
    run->elapsed_seconds = monotonic_seconds() - started;
    run->p50_file_seconds = percentile(file_seconds, corpus->count, 50);
    run->p95_file_seconds = percentile(file_seconds, corpus->count, 95);
    free(file_seconds);
    return true;
}

static void print_run(const char* parser_name, unsigned int run_index,
        const ParseRun* run) {
    fprintf(stdout,
        "%s\trun=%u\ttotal_ms=%.3f\tp50_file_us=%.3f\tp95_file_us=%.3f"
        "\tlargest16_ms=%.3f\tok=%u\tincomplete=%u\terror=%u\n",
        parser_name, run_index, run->elapsed_seconds * 1000.0,
        run->p50_file_seconds * 1000000.0, run->p95_file_seconds * 1000000.0,
        run->largest_seconds * 1000.0, run->ok_count, run->incomplete_count,
        run->error_count);
}

static double print_summary(const char* parser_name, const ParseRun* runs,
        size_t source_bytes) {
    double elapsed[MEASURED_RUNS];
    double p50[MEASURED_RUNS];
    double p95[MEASURED_RUNS];
    double largest[MEASURED_RUNS];
    for (size_t i = 0; i < MEASURED_RUNS; i++) {
        elapsed[i] = runs[i].elapsed_seconds;
        p50[i] = runs[i].p50_file_seconds;
        p95[i] = runs[i].p95_file_seconds;
        largest[i] = runs[i].largest_seconds;
    }
    double elapsed_median = median(elapsed, MEASURED_RUNS);
    fprintf(stdout,
        "%s\tmedian_total_ms=%.3f\tmedian_mib_s=%.2f\tmedian_p50_file_us=%.3f"
        "\tmedian_p95_file_us=%.3f\tmedian_largest16_ms=%.3f\n",
        parser_name, elapsed_median * 1000.0,
        ((double)source_bytes / (1024.0 * 1024.0)) / elapsed_median,
        median(p50, MEASURED_RUNS) * 1000000.0,
        median(p95, MEASURED_RUNS) * 1000000.0,
        median(largest, MEASURED_RUNS) * 1000.0);
    return elapsed_median;
}

int main(int argc, char** argv) {
    if (argc != 2) return 3;

    Corpus corpus = {0};
    if (!corpus_load(&corpus, argv[1])) {
        corpus_destroy(&corpus);
        return 3;
    }
    corpus_mark_largest(&corpus);

    TSParser* tree_sitter = ts_parser_new();
    if (!tree_sitter || !ts_parser_set_language(tree_sitter, tree_sitter_lambda())) {
        ts_parser_delete(tree_sitter);
        corpus_destroy(&corpus);
        return 3;
    }

    fprintf(stdout, "corpus\tfiles=%zu\tbytes=%zu\tlargest16_files=%zu"
        "\tlargest16_bytes=%zu\twarmups=%u\truns=%u\n",
        corpus.count, corpus.bytes, corpus.largest_count, corpus.largest_bytes,
        WARMUP_RUNS, MEASURED_RUNS);

    ParseRun discarded = {0};
    for (unsigned int i = 0; i < WARMUP_RUNS; i++) {
        if (!run_tree_sitter(tree_sitter, &corpus, &discarded) ||
                !run_rd_parser(&corpus, &discarded)) {
            ts_parser_delete(tree_sitter);
            corpus_destroy(&corpus);
            return 3;
        }
    }

    ParseRun tree_runs[MEASURED_RUNS];
    ParseRun rd_runs[MEASURED_RUNS];
    for (unsigned int i = 0; i < MEASURED_RUNS; i++) {
        // Alternate first runner so a fixed cache/CPU order cannot favor one parser.
        bool tree_first = (i & 1u) == 0;
        bool ok = tree_first ?
            run_tree_sitter(tree_sitter, &corpus, &tree_runs[i]) &&
                run_rd_parser(&corpus, &rd_runs[i]) :
            run_rd_parser(&corpus, &rd_runs[i]) &&
                run_tree_sitter(tree_sitter, &corpus, &tree_runs[i]);
        if (!ok) {
            ts_parser_delete(tree_sitter);
            corpus_destroy(&corpus);
            return 3;
        }
        print_run("tree_sitter", i + 1, &tree_runs[i]);
        print_run("rd_pratt", i + 1, &rd_runs[i]);
    }

    double tree_median = print_summary("tree_sitter", tree_runs, corpus.bytes);
    double rd_median = print_summary("rd_pratt", rd_runs, corpus.bytes);
    fprintf(stdout, "relative\trd_over_tree=%.3f\trd_speedup=%.2fx\n",
        rd_median / tree_median, tree_median / rd_median);

    ts_parser_delete(tree_sitter);
    corpus_destroy(&corpus);
    return 0;
}
