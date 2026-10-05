// fts_search.cpp — scanning files for a query and ranking what matched
// (FTX1, FTX2, FTX7–FTX10).
//
// The walk is lib/grep's (grep_walk_paths): one thread pool for traversal and
// search, a file per job, per-slot state with no lock on the hot path. A file
// is read once; the prefilter runs over that buffer, and on a hit the same
// bytes are cut into documents (file, paragraphs or lines) and tokenised.
// Per-file results are merged on the calling thread. BM25's statistics are
// exact (FTX8): a skipped file holds no query term, so it adds only to the
// document count N and to the total length, which is measured in bytes and so
// needs no tokenising; df counts every document holding a scored term, whether
// or not it matched the whole query.

#include "fts_internal.hpp"
#include "../arraylist.h"
#include "../file.h"
#include "../log.h"
#include "../mem_grow.h"
#include "../memtrack.h"
#include "../str.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define FTS_BM25_K1 1.2
#define FTS_BM25_B 0.75

// one matching document, as recorded on a worker
struct FtsDocRec {
    uint64_t byte_offset, byte_length;
    uint64_t char_offset;
    uint64_t line;
    GrepLineEnding line_ending;
};

struct FtsFileHits {
    char* path;               // mem-owned
    size_t root_index;
    uint64_t kept;            // fts_search_finish: hits kept under limit_per_file
    FtsDocRec* docs;
    size_t count, cap;
    uint32_t* tf;             // count * item_count, per scored leaf
    size_t tf_cap;
};

// per walk slot: never shared while a visit runs
struct FtsSlot {
    GrepSearcher* prefilter;
    FtsTokenizer tok;
    FtsEval eval;
};

struct FtsSearch {
    const FtsQuery* q;
    pthread_mutex_t mu;       // guards files and the statistics during a walk
    ArrayList* files;         // FtsFileHits*
    FtsSlot* slots;
    int slot_count;
    uint64_t documents;       // N
    uint64_t length_sum;      // bytes over all documents
    uint64_t* df;             // per scored leaf
    bool failed;
    FtsHit* hits;             // fts_search_finish
    size_t hit_count;
};

// ── documents (FTX2) ───────────────────────────────────────────────────

struct FtsDocSpan {
    size_t start, end;        // without the last line's terminator
    uint64_t line;
    GrepLineEnding line_ending;
};

static bool blank_line(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c != ' ' && c != '\t' && c != '\f' && c != '\v') return false;
    }
    return true;
}

// Calls fn(span) for each document of `data`; stops when fn returns false.
// A line ends at "\n" or "\r\n" (GRP18); a final empty line is not a line.
template <typename Fn>
static void for_each_document(const char* data, size_t size, FtsUnit unit, Fn fn) {
    if (unit == FTS_UNIT_FILE) {
        FtsDocSpan span = {0, size, 1, GREP_EOL_NONE};
        fn(span);
        return;
    }
    size_t pos = 0;
    uint64_t line = 0;
    bool in_para = false;
    FtsDocSpan para = {0, 0, 0, GREP_EOL_NONE};
    while (pos < size) {
        line++;
        const char* nl = (const char*)memchr(data + pos, '\n', size - pos);
        size_t end = nl ? (size_t)(nl - data) : size;
        size_t content_end = end;
        GrepLineEnding eol = nl ? GREP_EOL_LF : GREP_EOL_NONE;
        if (nl && end > pos && data[end - 1] == '\r') {
            content_end = end - 1;
            eol = GREP_EOL_CRLF;
        }
        size_t next = nl ? end + 1 : size;
        if (unit == FTS_UNIT_LINE) {
            FtsDocSpan span = {pos, content_end, line, eol};
            if (!fn(span)) return;
        } else if (blank_line(data + pos, content_end - pos)) {
            if (in_para && !fn(para)) return;
            in_para = false;
        } else {
            if (!in_para) {
                para.start = pos;
                para.line = line;
                in_para = true;
            }
            para.end = content_end;
            para.line_ending = eol;
        }
        pos = next;
    }
    if (in_para) fn(para);
}

// ── the visitor ────────────────────────────────────────────────────────

static GrepAction prefilter_hit(void* ud, const GrepMatch*) {
    *(bool*)ud = true;
    return GREP_STOP;
}

static bool search_begin(void* ud, int slot_count) {
    FtsSearch* s = (FtsSearch*)ud;
    s->slots = (FtsSlot*)mem_calloc((size_t)slot_count, sizeof(FtsSlot), MEM_CAT_TEMP);
    if (!s->slots) return false;
    s->slot_count = slot_count;
    for (int i = 0; i < slot_count; i++) {
        fts_tokenizer_init(&s->slots[i].tok, s->q->options.ignore_case, s->q->options.unaccent);
        fts_eval_init(&s->slots[i].eval);
    }
    return true;
}

static void release_slots(FtsSearch* s) {
    for (int i = 0; i < s->slot_count; i++) {
        if (s->slots[i].prefilter) grep_searcher_destroy(s->slots[i].prefilter);
        fts_tokenizer_release(&s->slots[i].tok);
        fts_eval_release(&s->slots[i].eval);
    }
    if (s->slots) mem_free(s->slots);
    s->slots = NULL;
    s->slot_count = 0;
}

static void free_file_hits(FtsFileHits* f) {
    if (!f) return;
    if (f->path) mem_free(f->path);
    if (f->docs) mem_free(f->docs);
    if (f->tf) mem_free(f->tf);
    mem_free(f);
}

static bool record_doc(FtsFileHits* f, const FtsDocRec* doc, const uint32_t* tf, int items) {
    if (!mem_grow_array_raw((void**)&f->docs, sizeof(FtsDocRec), &f->cap, f->count + 1, 8, MEM_CAT_TEMP)) return false;
    if (items && !mem_grow_array_raw((void**)&f->tf, sizeof(uint32_t), &f->tf_cap, (f->count + 1) * (size_t)items, 8 * (size_t)items, MEM_CAT_TEMP)) return false;
    f->docs[f->count] = *doc;
    for (int i = 0; i < items; i++) f->tf[f->count * (size_t)items + (size_t)i] = tf[i];
    f->count++;
    return true;
}

static GrepAction search_visit(void* ud, int slot_index, const char* open_path, const char* label,
                               size_t root_index) {
    FtsSearch* s = (FtsSearch*)ud;
    const FtsQuery* q = s->q;
    const FtsOptions* o = &q->options;
    FtsSlot* slot = &s->slots[slot_index];
    char* data = NULL;
    size_t size = 0;
    if (!file_read_all(open_path, MEM_CAT_TEMP, &data, &size)) {
        log_error("fts: cannot read '%s'", open_path);
        return GREP_CONTINUE;
    }
    if (!o->binary && grep_input_is_binary(data, size)) {
        mem_free(data);
        return GREP_CONTINUE;
    }
    bool hit = q->prefilter == NULL;
    if (!hit) {
        if (!slot->prefilter) slot->prefilter = grep_searcher_create(q->prefilter);
        if (slot->prefilter) {
            GrepSink sink = {&hit, prefilter_hit, NULL, NULL};
            grep_search_buffer(slot->prefilter, label, data, size, &sink);
        } else {
            hit = true;  // no searcher: evaluate the file in full
        }
    }

    // the statistics only BM25 needs: a skipped file adds documents and bytes
    bool stats = o->rank == FTS_RANK_BM25;
    int items = q->item_count;
    uint64_t documents = 0, length_sum = 0;
    uint64_t* df = NULL;
    FtsFileHits* f = NULL;
    bool oom = false;
    if (hit && q->root >= 0) {
        df = items ? (uint64_t*)mem_calloc((size_t)items, sizeof(uint64_t), MEM_CAT_TEMP) : NULL;
        f = (FtsFileHits*)mem_calloc(1, sizeof(FtsFileHits), MEM_CAT_TEMP);
        oom = !f || (items && !df);
        if (f) {
            f->path = mem_strdup(label, MEM_CAT_TEMP);
            f->root_index = root_index;
            oom = oom || !f->path;
        }
    }
    size_t cursor = 0;
    uint64_t chars = 0;
    for_each_document(data, size, o->unit, [&](const FtsDocSpan& doc) -> bool {
        documents++;
        length_sum += doc.end - doc.start;
        if (!f || oom) return true;
        if (!fts_tokenize(&slot->tok, data + doc.start, doc.end - doc.start)) {
            oom = true;
            return false;
        }
        fts_mark_stop(q, &slot->tok, 0, slot->tok.count);
        bool eval_oom = false;
        bool match = fts_eval_document(q, &slot->tok, 0, slot->tok.count, &slot->eval, &eval_oom);
        if (eval_oom) {
            oom = true;
            return false;
        }
        for (int l = 0; l < q->leaf_count && df; l++) {
            int item = q->leaves[l].item;
            if (item >= 0 && slot->eval.tf[l] > 0) df[item]++;
        }
        if (!match) return true;
        chars += str_utf8_count(data + cursor, doc.start - cursor);
        cursor = doc.start;
        uint32_t tf_items[64];
        uint32_t* tf = items <= 64 ? tf_items : (uint32_t*)mem_alloc((size_t)items * sizeof(uint32_t), MEM_CAT_TEMP);
        if (!tf) {
            oom = true;
            return false;
        }
        for (int l = 0; l < q->leaf_count; l++) {
            int item = q->leaves[l].item;
            if (item >= 0) tf[item] = slot->eval.tf[l];
        }
        FtsDocRec rec = {doc.start, doc.end - doc.start, chars, doc.line, doc.line_ending};
        if (!record_doc(f, &rec, tf, items)) oom = true;
        if (tf != tf_items) mem_free(tf);
        // without ranking a file can stop once it holds enough (FTX9)
        return !oom && !(o->rank == FTS_RANK_NONE && o->file_cap && f->count >= o->file_cap);
    });
    mem_free(data);

    pthread_mutex_lock(&s->mu);
    if (oom) s->failed = true;
    if (stats) {
        s->documents += documents;
        s->length_sum += length_sum;
        for (int i = 0; df && i < items; i++) s->df[i] += df[i];
    }
    if (f && !oom && f->count) {
        if (arraylist_append(s->files, f)) f = NULL;
        else s->failed = true;
    }
    bool failed = s->failed;
    pthread_mutex_unlock(&s->mu);
    free_file_hits(f);
    if (df) mem_free(df);
    return failed ? GREP_STOP : GREP_CONTINUE;
}

// ── the API ────────────────────────────────────────────────────────────

FtsSearch* fts_search_create(const FtsQuery* query) {
    if (!query) return NULL;
    FtsSearch* s = (FtsSearch*)mem_calloc(1, sizeof(FtsSearch), MEM_CAT_TEMP);
    if (!s) return NULL;
    s->q = query;
    pthread_mutex_init(&s->mu, NULL);
    s->files = arraylist_new(64);
    s->df = (uint64_t*)mem_calloc((size_t)(query->item_count ? query->item_count : 1), sizeof(uint64_t), MEM_CAT_TEMP);
    if (!s->files || !s->df) {
        fts_search_destroy(s);
        return NULL;
    }
    return s;
}

void fts_search_destroy(FtsSearch* s) {
    if (!s) return;
    release_slots(s);
    for (int i = 0; s->files && i < arraylist_length(s->files); i++) free_file_hits((FtsFileHits*)arraylist_get(s->files, i));
    if (s->files) arraylist_free(s->files);
    if (s->df) mem_free(s->df);
    if (s->hits) mem_free(s->hits);
    pthread_mutex_destroy(&s->mu);
    mem_free(s);
}

FtsStatus fts_search_add(FtsSearch* s, const char* const* paths, size_t count, size_t root_base,
                         const GrepWalkOptions* walk) {
    if (!s || !paths) return FTS_ERR_ARGUMENT;
    // root indexes are offset after the walk, which numbers its own roots
    size_t before = (size_t)arraylist_length(s->files);
    GrepWalkVisitor visitor = {s, search_begin, search_visit};
    GrepStatus st = grep_walk_paths(paths, count, walk, &visitor);
    release_slots(s);
    for (int i = (int)before; i < arraylist_length(s->files); i++) ((FtsFileHits*)arraylist_get(s->files, i))->root_index += root_base;
    if (st == GREP_ERR_IO) return FTS_ERR_IO;
    if (st != GREP_OK || s->failed) return st == GREP_ERR_ARGUMENT ? FTS_ERR_ARGUMENT : FTS_ERR_MEMORY;
    return FTS_OK;
}

// a hit while ranking, with its file for the per-file limit
struct FtsRanked {
    FtsHit hit;
    FtsFileHits* file;
};

static int hit_order(const void* va, const void* vb) {
    const FtsHit* a = &((const FtsRanked*)va)->hit;
    const FtsHit* b = &((const FtsRanked*)vb)->hit;
    if (a->score != b->score) return a->score > b->score ? -1 : 1;
    if (a->root_index != b->root_index) return a->root_index < b->root_index ? -1 : 1;
    int c = a->path == b->path ? 0 : grep_path_order(a->path, b->path);
    if (c) return c;
    if (a->byte_offset != b->byte_offset) return a->byte_offset < b->byte_offset ? -1 : 1;
    return 0;
}

FtsStatus fts_search_finish(FtsSearch* s, uint64_t limit, uint64_t limit_per_file,
                            const FtsHit** hits, size_t* count) {
    if (!s || !hits || !count) return FTS_ERR_ARGUMENT;
    *hits = NULL;
    *count = 0;
    const FtsOptions* o = &s->q->options;
    int items = s->q->item_count;
    size_t total = 0;
    for (int i = 0; i < arraylist_length(s->files); i++) total += ((FtsFileHits*)arraylist_get(s->files, i))->count;
    if (s->hits) mem_free(s->hits);
    s->hits = (FtsHit*)mem_calloc(total ? total : 1, sizeof(FtsHit), MEM_CAT_TEMP);
    FtsRanked* ranked = (FtsRanked*)mem_calloc(total ? total : 1, sizeof(FtsRanked), MEM_CAT_TEMP);
    double idf_one[64];
    double* idf = items <= 64 ? idf_one : (double*)mem_alloc((size_t)items * sizeof(double), MEM_CAT_TEMP);
    if (!s->hits || !ranked || !idf) {
        if (ranked) mem_free(ranked);
        if (idf && idf != idf_one) mem_free(idf);
        return FTS_ERR_MEMORY;
    }

    // idf per scored leaf (FTX7); "tf" ranks with idf = 1 (FTX9)
    double n = (double)s->documents;
    double avglen = s->documents ? (double)s->length_sum / n : 0;
    for (int i = 0; i < items; i++) {
        double df = (double)s->df[i];
        idf[i] = o->rank == FTS_RANK_BM25 ? log(1.0 + (n - df + 0.5) / (df + 0.5)) : 1.0;
    }
    size_t k = 0;
    for (int i = 0; i < arraylist_length(s->files); i++) {
        FtsFileHits* f = (FtsFileHits*)arraylist_get(s->files, i);
        f->kept = 0;
        for (size_t d = 0; d < f->count; d++) {
            const FtsDocRec* rec = &f->docs[d];
            FtsRanked* r = &ranked[k++];
            r->file = f;
            FtsHit* h = &r->hit;
            h->path = f->path;
            h->root_index = f->root_index;
            h->byte_offset = rec->byte_offset;
            h->byte_length = rec->byte_length;
            h->char_offset = rec->char_offset;
            h->line = rec->line;
            h->line_ending = rec->line_ending;
            if (o->rank == FTS_RANK_NONE) continue;
            // length enters only as len / avglen (FTX8): with tf ranking, or an
            // empty corpus, every document counts as average
            double ratio = o->rank == FTS_RANK_BM25 && avglen > 0 ? (double)rec->byte_length / avglen : 1.0;
            double score = 0;
            for (int t = 0; t < items; t++) {
                double tf = (double)f->tf[d * (size_t)items + (size_t)t];
                if (tf > 0) score += idf[t] * tf * (FTS_BM25_K1 + 1) / (tf + FTS_BM25_K1 * (1 - FTS_BM25_B + FTS_BM25_B * ratio));
            }
            h->score = score;
        }
    }
    if (idf != idf_one) mem_free(idf);
    qsort(ranked, k, sizeof(FtsRanked), hit_order);

    // the best of each file, then the best in all
    size_t kept = 0;
    for (size_t i = 0; i < k && (!limit || kept < limit); i++) {
        if (limit_per_file && ranked[i].file->kept++ >= limit_per_file) continue;
        s->hits[kept++] = ranked[i].hit;
    }
    mem_free(ranked);
    s->hit_count = kept;
    *hits = s->hits;
    *count = kept;
    return FTS_OK;
}
