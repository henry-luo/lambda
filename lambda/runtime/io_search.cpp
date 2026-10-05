// io_search.cpp — io.search(source, query, options?) over lib/fts
// (vibe/Lambda_IO_Fulltext_Search.md, FTX11).
//
// A procedure, as io.grep is: its result depends on the file system, and fn
// must be deterministic (S12.1.1v2). It takes io.grep's options where they
// apply, with ignore_case, word and text defaulting to true (FTX12). Results
// are documents, best first (FTX7–FTX9); the fields that need a document's
// text (text, context lines, matches, snippet) are read back from the file
// for the returned documents only, each file once.

#include "io_file_search.hpp"
#include "re2_wrapper.hpp"
#include "../../lib/fts/fts.h"
#include "../../lib/arraylist.h"
#include "../../lib/file.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/str.h"

#include <stdlib.h>
#include <string.h>

namespace {

struct IoSearchOptions {
    IoFsCommonOptions c;
    FtsOptions fts;
    bool matches;
    int64_t snippet;
    ArrayList* stopwords;     // mem-owned strings
    char* language;           // mem-owned
};

// ── options ────────────────────────────────────────────────────────────

bool text_equals(Item v, const char* s) {
    size_t n = strlen(s);
    return is_text_type_id(get_type_id(v)) && v.get_len() == n && memcmp(v.get_chars(), s, n) == 0;
}

bool read_unit(const IoFsOptionMap* m, FtsUnit* unit, Item* error) {
    Item v = io_fs_option_get(m, "unit");
    if (get_type_id(v) == LMD_TYPE_NULL) return true;
    if (text_equals(v, "file")) *unit = FTS_UNIT_FILE;
    else if (text_equals(v, "paragraph")) *unit = FTS_UNIT_PARAGRAPH;
    else if (text_equals(v, "line")) *unit = FTS_UNIT_LINE;
    else {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.search: option 'unit' must be \"file\", \"paragraph\" or \"line\"");
        return false;
    }
    return true;
}

bool read_rank(const IoFsOptionMap* m, FtsRank* rank, Item* error) {
    Item v = io_fs_option_get(m, "rank");
    TypeId tid = get_type_id(v);
    if (tid == LMD_TYPE_NULL) return true;
    if (tid == LMD_TYPE_BOOL) *rank = it2b(v) ? FTS_RANK_BM25 : FTS_RANK_NONE;
    else if (text_equals(v, "bm25")) *rank = FTS_RANK_BM25;
    else if (text_equals(v, "tf")) *rank = FTS_RANK_TF;
    else {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.search: option 'rank' must be \"bm25\", \"tf\" or false");
        return false;
    }
    return true;
}

bool read_text_list(const IoFsOptionMap* m, const char* name, ArrayList** out, Item* error) {
    Item v = io_fs_option_get(m, name);
    TypeId tid = get_type_id(v);
    if (tid == LMD_TYPE_NULL || (tid == LMD_TYPE_BOOL && !it2b(v))) return true;
    if (tid == LMD_TYPE_BOOL) {
        // FTX4: `true` is the stop-word list of `language`; the lists come with
        // the stemmers (FTX5), which are not built yet
        *error = io_fs_error(ERR_NOT_IMPLEMENTED, "io.search: no built-in stop-word list yet; give the words as an array");
        return false;
    }
    ArrayList* list = arraylist_new(8);
    bool ok = list != NULL;
    auto push = [&](Item s) {
        if (!is_text_type_id(get_type_id(s))) return false;
        char* copy = mem_dup_n(s.get_chars(), s.get_len(), MEM_CAT_TEMP);
        return copy && arraylist_append(list, copy) != 0;
    };
    if (ok && tid == LMD_TYPE_ARRAY) {
        for (int64_t i = 0; ok && i < v.array->length; i++) ok = push(array_get(v.array, i));
    } else if (ok) {
        ok = push(v);
    }
    if (!ok) {
        for (int i = 0; list && i < arraylist_length(list); i++) mem_free(arraylist_get(list, i));
        if (list) arraylist_free(list);
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.search: option '%s' must be a bool, a string or an array of strings", name);
        return false;
    }
    *out = list;
    return true;
}

void release_options(IoSearchOptions* o) {
    io_fs_common_release(&o->c);
    for (int i = 0; o->stopwords && i < arraylist_length(o->stopwords); i++) mem_free(arraylist_get(o->stopwords, i));
    if (o->stopwords) arraylist_free(o->stopwords);
    if (o->language) mem_free(o->language);
    o->stopwords = NULL;
    o->language = NULL;
}

bool parse_options(Item options, IoSearchOptions* o, Item* error) {
    memset(o, 0, sizeof(*o));
    io_fs_common_init(&o->c);
    // the defaults full-text search differs in from io.grep (FTX12)
    o->c.ignore_case = true;
    o->c.word = true;
    o->c.want_text = true;
    o->fts.unit = FTS_UNIT_FILE;
    o->fts.rank = FTS_RANK_BM25;
    IoFsOptionMap m;
    if (!io_fs_options_open(options, "io.search", SYSPROC_IO_SEARCH, &m, error)) return false;
    bool ok = io_fs_common_read(&m, &o->c, error) &&
        read_unit(&m, &o->fts.unit, error) &&
        read_rank(&m, &o->fts.rank, error) &&
        read_text_list(&m, "stopwords", &o->stopwords, error) &&
        io_fs_option_bool(&m, "unaccent", &o->fts.unaccent, error) &&
        io_fs_option_bool(&m, "matches", &o->matches, error) &&
        io_fs_option_count(&m, "snippet", &o->snippet, error);
    if (!ok) return false;
    Item lang = io_fs_option_get(&m, "language");
    if (get_type_id(lang) != LMD_TYPE_NULL) {
        if (!is_text_type_id(get_type_id(lang))) {
            *error = io_fs_error(ERR_TYPE_MISMATCH, "io.search: option 'language' must be a string");
            return false;
        }
        o->language = mem_dup_n(lang.get_chars(), lang.get_len(), MEM_CAT_TEMP);
        if (!o->language) return false;
    }
    o->fts.ignore_case = o->c.ignore_case;
    o->fts.word = o->c.word;
    o->fts.binary = o->c.binary;
    o->fts.language = o->language;
    if (o->stopwords) {
        o->fts.stopwords = (const char* const*)o->stopwords->data;
        o->fts.stopword_count = (size_t)arraylist_length(o->stopwords);
    }
    if (o->snippet > 1000000) o->snippet = 1000000;
    return true;
}

// ── results ────────────────────────────────────────────────────────────

enum IoSearchField {
    F_FILE, F_SCORE, F_INDEX, F_LINE, F_BYTE, F_TEXT, F_LINE_ENDING, F_BEFORE, F_AFTER,
    F_MATCHES, F_SNIPPET, F_COUNT, F_NFIELDS
};

struct IoSearchRun {
    const IoSearchOptions* o;
    const FtsQuery* query;
    const IoFsSource* sources;
    TypeMap* shape;
    TypeMap* match_shape;     // {value, index[, line][, byte_offset]}
    int slot[F_NFIELDS];      // position in the shape, or -1
    int fields;
    int match_fields;
    bool sub_file;            // documents are paragraphs or lines
};

Item file_item(const IoSearchRun* run, const FtsHit* h) {
    IoFsRoot root;
    io_fs_root_of(&run->sources[h->root_index], &root);
    return io_fs_file_value(&root, h->path);
}

// the text of the line before `start` (a line's first byte), or false at the top
bool line_before(const char* data, size_t start, size_t* ls, size_t* le) {
    if (start == 0) return false;
    size_t end = start - 1;  // the '\n' ending the line before
    size_t content = end > 0 && data[end - 1] == '\r' ? end - 1 : end;
    size_t s = end;
    while (s > 0 && data[s - 1] != '\n') s--;
    *ls = s;
    *le = content;
    return true;
}

// the line starting at `start`, or false at the end of the data
bool line_at(const char* data, size_t size, size_t start, size_t* le, size_t* next) {
    if (start >= size) return false;
    const char* nl = (const char*)memchr(data + start, '\n', size - start);
    size_t end = nl ? (size_t)(nl - data) : size;
    *le = nl && end > start && data[end - 1] == '\r' ? end - 1 : end;
    *next = nl ? end + 1 : size;
    return true;
}

// up to `before` lines before the document, or `after` lines after it
Item context_lines(const char* data, size_t size, const FtsHit* h, int before, int after) {
    RootFrame roots(2);
    Rooted<Array*> lines(roots, array());
    Rooted<Item> text(roots, ItemNull);
    size_t start = (size_t)h->byte_offset, le = 0, next = 0;
    size_t at = start;
    int count = 0;
    if (before > 0) {
        // back up to the first line wanted, then read forward to the document
        size_t ls = 0;
        while (count < before && line_before(data, at, &ls, &le)) {
            at = ls;
            count++;
        }
    } else {
        if (!line_at(data, size, start, &le, &next)) return (Item){.array = lines.get()};
        at = next;  // past the document's own line
        count = after;
    }
    for (int i = 0; i < count && line_at(data, size, at, &le, &next); i++) {
        text.set(io_fs_make_string(data + at, le - at));
        if (get_type_id(text.get()) == LMD_TYPE_ERROR) return ItemError;
        array_push_verbatim(lines.get(), text.get());
        at = next;
    }
    return (Item){.array = lines.get()};
}

bool collect_span(void* ud, const FtsSpan* span) {
    FtsSpan* copy = (FtsSpan*)mem_alloc(sizeof(FtsSpan), MEM_CAT_TEMP);
    if (!copy) return false;
    *copy = *span;
    return arraylist_append((ArrayList*)ud, copy) != 0;
}

// the token occurrences that satisfied a positive term, as match maps
Item match_list(const IoSearchRun* run, const FtsHit* h, const char* text, size_t length) {
    ArrayList* spans = arraylist_new(8);
    if (!spans) return ItemError;
    FtsStatus st = fts_document_matches(run->query, text, length, collect_span, spans);
    RootFrame roots(6);
    Rooted<Array*> list(roots, array());
    Rooted<Item> v0(roots, ItemNull), v1(roots, ItemNull), v2(roots, ItemNull), v3(roots, ItemNull);
    Rooted<Map*> map(roots, (Map*)NULL);
    bool ok = st == FTS_OK;
    uint64_t line = h->line;
    size_t counted = 0;
    for (int i = 0; ok && i < arraylist_length(spans); i++) {
        const FtsSpan* s = (const FtsSpan*)arraylist_get(spans, i);
        Rooted<Item>* vals[4] = {&v0, &v1, &v2, &v3};
        int k = 0;
        vals[k++]->set(io_fs_make_string(text + s->byte_offset, s->byte_length));
        vals[k++]->set((Item){.item = i2it((int64_t)(h->char_offset + s->char_offset))});
        if (run->o->c.want_line) {
            for (; counted < s->byte_offset; counted++) line += text[counted] == '\n';
            vals[k++]->set((Item){.item = i2it((int64_t)line)});
        }
        if (run->o->c.want_byte_offset) vals[k++]->set((Item){.item = i2it((int64_t)(h->byte_offset + s->byte_offset))});
        Item values[4];
        for (int j = 0; j < k; j++) {
            values[j] = vals[j]->get();
            if (get_type_id(values[j]) == LMD_TYPE_ERROR) ok = false;
        }
        if (!ok) break;
        map.set(map_alloc_for_type(run->match_shape, NULL, 0));
        if (!map.get()) {
            ok = false;
            break;
        }
        for (int j = 0; j < k; j++) values[j] = vals[j]->get();
        map_fill_items(map.get(), values, k);
        array_push_verbatim(list.get(), (Item){.map = map.get()});
    }
    for (int i = 0; i < arraylist_length(spans); i++) mem_free(arraylist_get(spans, i));
    arraylist_free(spans);
    return ok ? (Item){.array = list.get()} : ItemError;
}

Item snippet_text(const IoSearchRun* run, const char* text, size_t length) {
    size_t start = 0, end = 0;
    bool cut_before = false, cut_after = false;
    if (fts_document_snippet(run->query, text, length, (int)run->o->snippet, &start, &end, &cut_before, &cut_after) != FTS_OK) return ItemError;
    StrBuf* sb = strbuf_new();
    if (!sb) return ItemError;
    if (cut_before) strbuf_append_str(sb, "\xE2\x80\xA6");  // …
    strbuf_append_str_n(sb, text + start, end - start);
    if (cut_after) strbuf_append_str(sb, "\xE2\x80\xA6");
    Item out = io_fs_make_string(sb->str, sb->length);
    strbuf_free(sb);
    return out;
}

// One document map. `data` is the file's content, or NULL when no field needs it.
Item document_map(const IoSearchRun* run, const FtsHit* h, const char* data, size_t size) {
    RootFrame roots(F_NFIELDS + 1);
    Rooted<Item> v0(roots, ItemNull), v1(roots, ItemNull), v2(roots, ItemNull), v3(roots, ItemNull),
        v4(roots, ItemNull), v5(roots, ItemNull), v6(roots, ItemNull), v7(roots, ItemNull),
        v8(roots, ItemNull), v9(roots, ItemNull), v10(roots, ItemNull), v11(roots, ItemNull);
    Rooted<Item>* vals[F_NFIELDS] = {&v0, &v1, &v2, &v3, &v4, &v5, &v6, &v7, &v8, &v9, &v10, &v11};
    Rooted<Map*> map(roots, (Map*)NULL);
    const IoSearchOptions* o = run->o;
    // the document's text, clamped should the file have changed since the scan
    size_t from = data && h->byte_offset < size ? (size_t)h->byte_offset : size;
    size_t len = data ? (size_t)(h->byte_length < size - from ? h->byte_length : size - from) : 0;
    const char* text = data ? data + from : "";
    vals[run->slot[F_FILE]]->set(file_item(run, h));
    if (run->slot[F_SCORE] >= 0) vals[run->slot[F_SCORE]]->set(push_d(h->score));
    if (run->slot[F_INDEX] >= 0) vals[run->slot[F_INDEX]]->set((Item){.item = i2it((int64_t)h->char_offset)});
    if (run->slot[F_LINE] >= 0) vals[run->slot[F_LINE]]->set((Item){.item = i2it((int64_t)h->line)});
    if (run->slot[F_BYTE] >= 0) vals[run->slot[F_BYTE]]->set((Item){.item = i2it((int64_t)h->byte_offset)});
    if (run->slot[F_TEXT] >= 0) vals[run->slot[F_TEXT]]->set(io_fs_make_string(text, len));
    if (run->slot[F_LINE_ENDING] >= 0) vals[run->slot[F_LINE_ENDING]]->set(io_fs_line_ending_item(h->line_ending));
    if (run->slot[F_BEFORE] >= 0) vals[run->slot[F_BEFORE]]->set(context_lines(data, size, h, o->c.before, 0));
    if (run->slot[F_AFTER] >= 0) vals[run->slot[F_AFTER]]->set(context_lines(data, size, h, 0, o->c.after));
    if (run->slot[F_MATCHES] >= 0) vals[run->slot[F_MATCHES]]->set(match_list(run, h, text, len));
    if (run->slot[F_SNIPPET] >= 0) vals[run->slot[F_SNIPPET]]->set(snippet_text(run, text, len));
    Item values[F_NFIELDS];
    for (int i = 0; i < run->fields; i++) {
        values[i] = vals[i]->get();
        if (get_type_id(values[i]) == LMD_TYPE_ERROR) return ItemError;
    }
    map.set(map_alloc_for_type(run->shape, NULL, 0));
    if (!map.get()) return ItemError;
    for (int i = 0; i < run->fields; i++) values[i] = vals[i]->get();
    map_fill_items(map.get(), values, run->fields);
    return (Item){.map = map.get()};
}

int hit_group_order(const void* va, const void* vb) {
    const FtsHit* a = *(const FtsHit* const*)va;
    const FtsHit* b = *(const FtsHit* const*)vb;
    if (a->path != b->path) return a->path < b->path ? -1 : 1;
    return a < b ? -1 : (a > b ? 1 : 0);
}

// The document maps, in rank order; each file is read once, for all of its
// documents, when a field needs the text.
bool emit_documents(const IoSearchRun* run, const FtsHit* hits, size_t count, Rooted<Array*>* result) {
    for (size_t i = 0; i < count; i++) array_push_verbatim(result->get(), ItemNull);
    bool needs_text = run->slot[F_TEXT] >= 0 || run->slot[F_BEFORE] >= 0 || run->slot[F_AFTER] >= 0 ||
                      run->slot[F_MATCHES] >= 0 || run->slot[F_SNIPPET] >= 0;
    const FtsHit** order = (const FtsHit**)mem_alloc((count ? count : 1) * sizeof(FtsHit*), MEM_CAT_TEMP);
    if (!order) return false;
    for (size_t i = 0; i < count; i++) order[i] = &hits[i];
    if (needs_text) qsort(order, count, sizeof(FtsHit*), hit_group_order);
    RootFrame roots(1);
    Rooted<Item> doc(roots, ItemNull);
    bool ok = true;
    const char* loaded = NULL;
    char* data = NULL;
    size_t size = 0;
    for (size_t i = 0; ok && i < count; i++) {
        const FtsHit* h = order[i];
        if (needs_text && h->path != loaded) {
            if (data) mem_free(data);
            data = NULL;
            size = 0;
            // a file gone since the scan reads as empty
            if (!file_read_all(h->path, MEM_CAT_TEMP, &data, &size)) data = NULL;
            loaded = h->path;
        }
        doc.set(document_map(run, h, needs_text ? (data ? data : "") : NULL, needs_text ? size : 0));
        if (get_type_id(doc.get()) == LMD_TYPE_ERROR) ok = false;
        else array_set(result->get(), (int64_t)(h - hits), doc.get());
    }
    if (data) mem_free(data);
    mem_free(order);
    return ok;
}

}  // namespace

static Item io_search_impl(Item source, Item query, Item options) {
    GUARD_ERROR3(source, query, options);
    if (g_dry_run) { log_debug("dry-run: fabricated io.search()"); return (Item){.array = array()}; }
    Item error = ItemError;
    IoSearchOptions o;
    if (!parse_options(options, &o, &error)) {
        release_options(&o);
        return error;
    }
    if (!is_text_type_id(get_type_id(query))) {
        release_options(&o);
        return io_fs_error(ERR_TYPE_MISMATCH, "io.search: the query must be a string");
    }
    IoFsSource* sources = NULL;
    int nsources = 0;
    bool ok = io_fs_resolve_sources("io.search", source, &sources, &nsources, &error);

    // a files or count result is about files, in path order (GRP21, GRP30)
    uint64_t limit = o.c.limit, per_file = o.c.limit_per_file;
    if (o.c.files_only || o.c.count) {
        o.fts.rank = FTS_RANK_NONE;
        o.fts.file_cap = o.c.files_only ? 1 : per_file;
    } else if (o.fts.rank == FTS_RANK_NONE) {
        // unranked, a file needs no more documents than either limit keeps (FTX9)
        o.fts.file_cap = limit && (!per_file || limit < per_file) ? limit : per_file;
    }
    FtsQuery* q = NULL;
    if (ok) {
        char detail[256];
        FtsStatus st = fts_query_create(query.get_chars(), query.get_len(), &o.fts, &q, detail, sizeof(detail));
        if (st != FTS_OK) {
            error = st == FTS_ERR_LANGUAGE ? io_fs_error(ERR_INVALID_OPERATION, "io.search: %s", detail)
                                           : io_fs_error(ERR_IO_ERROR, "io.search: cannot compile the query");
            ok = false;
        }
    }
    FtsSearch* search = ok ? fts_search_create(q) : NULL;
    ok = ok && search;
    for (int i = 0; ok && i < nsources; i++) {
        const IoFsSource* src = &sources[i];
        GrepWalkOptions walk = o.c.walk;
        if (src->max_depth >= 0) walk.max_depth = src->max_depth;
        const char* roots_os[1] = {src->os->str};
        FtsStatus st = fts_search_add(search, roots_os, 1, (size_t)i, &walk);
        if (st != FTS_OK) {
            error = io_fs_error(st == FTS_ERR_IO ? ERR_FILE_NOT_FOUND : ERR_IO_ERROR,
                                "io.search: search failed in '%s'", src->os->str);
            ok = false;
        }
    }
    const FtsHit* hits = NULL;
    size_t nhits = 0;
    if (ok) {
        bool by_file = o.c.files_only || o.c.count;
        ok = fts_search_finish(search, by_file && o.c.count ? 0 : limit, by_file ? 0 : per_file, &hits, &nhits) == FTS_OK;
    }

    IoSearchRun run;
    memset(&run, 0, sizeof(run));
    run.o = &o;
    run.query = q;
    run.sources = sources;
    run.sub_file = o.fts.unit != FTS_UNIT_FILE;
    RootFrame roots(2);
    Rooted<Array*> result(roots, array());
    Rooted<Item> item(roots, ItemNull);
    if (ok && o.c.files_only) {
        for (size_t i = 0; ok && i < nhits; i++) {
            item.set(file_item(&run, &hits[i]));
            if (get_type_id(item.get()) == LMD_TYPE_ERROR) ok = false;
            else array_push_verbatim(result.get(), item.get());
        }
    } else if (ok && o.c.count) {
        // one {file, count} per file; a total limit cuts the count it ends in
        const char* names[2] = {"file", "count"};
        TypeId types[2] = {LMD_TYPE_ANY, LMD_TYPE_INT};
        run.shape = runtime_result_shape(names, types, 2);
        uint64_t total = 0;
        for (size_t i = 0; ok && i < nhits && (!limit || total < limit);) {
            size_t j = i;
            while (j < nhits && hits[j].path == hits[i].path) j++;
            uint64_t n = (uint64_t)(j - i);
            if (limit && n > limit - total) n = limit - total;
            total += n;
            RootFrame inner(2);
            Rooted<Item> file(inner, file_item(&run, &hits[i]));
            Rooted<Map*> map(inner, (Map*)NULL);
            map.set(get_type_id(file.get()) == LMD_TYPE_ERROR ? NULL : map_alloc_for_type(run.shape, NULL, 0));
            if (!map.get()) {
                ok = false;
                break;
            }
            Item values[2] = {file.get(), (Item){.item = i2it((int64_t)n)}};
            map_fill_items(map.get(), values, 2);
            array_push_verbatim(result.get(), (Item){.map = map.get()});
            i = j;
        }
    } else if (ok) {
        const char* names[F_NFIELDS];
        TypeId types[F_NFIELDS];
        for (int i = 0; i < F_NFIELDS; i++) run.slot[i] = -1;
        auto add_field = [&](IoSearchField f, const char* name, TypeId type) {
            run.slot[f] = run.fields;
            names[run.fields] = name;
            types[run.fields] = type;
            run.fields++;
        };
        bool line_unit = o.fts.unit == FTS_UNIT_LINE;
        add_field(F_FILE, "file", LMD_TYPE_ANY);
        if (o.fts.rank != FTS_RANK_NONE) add_field(F_SCORE, "score", LMD_TYPE_FLOAT);
        if (run.sub_file) add_field(F_INDEX, "index", LMD_TYPE_INT);
        if (run.sub_file && o.c.want_line) add_field(F_LINE, "line", LMD_TYPE_INT);
        if (run.sub_file && o.c.want_byte_offset) add_field(F_BYTE, "byte_offset", LMD_TYPE_INT);
        if (run.sub_file && o.c.want_text) add_field(F_TEXT, "text", LMD_TYPE_STRING);
        if (line_unit && o.c.want_line_ending) add_field(F_LINE_ENDING, "line_ending", LMD_TYPE_ANY);
        if (line_unit && o.c.before > 0) add_field(F_BEFORE, "before", LMD_TYPE_ANY);
        if (line_unit && o.c.after > 0) add_field(F_AFTER, "after", LMD_TYPE_ANY);
        if (o.matches) add_field(F_MATCHES, "matches", LMD_TYPE_ANY);
        if (o.snippet > 0) add_field(F_SNIPPET, "snippet", LMD_TYPE_STRING);
        run.shape = runtime_result_shape(names, types, run.fields);
        if (o.matches) {
            const char* mnames[4];
            TypeId mtypes[4];
            int k = 0;
            mnames[k] = "value"; mtypes[k++] = LMD_TYPE_STRING;
            mnames[k] = "index"; mtypes[k++] = LMD_TYPE_INT;
            if (o.c.want_line) { mnames[k] = "line"; mtypes[k++] = LMD_TYPE_INT; }
            if (o.c.want_byte_offset) { mnames[k] = "byte_offset"; mtypes[k++] = LMD_TYPE_INT; }
            run.match_shape = runtime_result_shape(mnames, mtypes, k);
            run.match_fields = k;
        }
        ok = run.shape && (!o.matches || run.match_shape) && emit_documents(&run, hits, nhits, &result);
    }

    fts_search_destroy(search);
    fts_query_destroy(q);
    io_fs_sources_free(sources, nsources);
    release_options(&o);
    if (!ok) return error;
    return (Item){.array = result.get()};
}

extern "C" Item pn_io_search2(Item source, Item query) {
    return io_search_impl(source, query, ItemNull);
}

extern "C" Item pn_io_search3(Item source, Item query, Item options) {
    return io_search_impl(source, query, options);
}
