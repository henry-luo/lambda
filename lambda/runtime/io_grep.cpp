// io_grep.cpp — io.grep(source, pattern, options?) over lib/grep
// (vibe/Lambda_Lib_Grep.md §9B, GRP26).
//
// A procedure: its result depends on the file system, and fn must be
// deterministic (S12.1.1v2). Matching is line-oriented: a match never spans a
// line terminator (GRP1). Results come in a stable order — sources in the
// order given, files in path order, matches in position order — so an
// unchanged tree gives the same array on every run (GRP25).

#include "transpiler.hpp"
#include "re2_wrapper.hpp"
#include "lambda-number-runtime.hpp"
#include "sys_func_registry.h"
#include "lambda-error.h"
#include "../core/lambda-path.h"
#include "../../lib/grep/grep.h"
#include "../../lib/byte_builder.h"
#include "../../lib/strbuf.h"
#include "../../lib/str.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/file.h"

#include <stdio.h>
#include <string.h>

extern "C" StrBuf* lambda_get_local_path_from_item(Item item);
Item _map_get(TypeMap* map_type, void* map_data, const char* key, bool* is_found);

namespace {

struct IoGrepOptions {
    GrepOptions grep;
    GrepWalkOptions walk;
    bool want_line;
    bool want_byte_offset;
    bool want_text;
    bool want_line_ending;
    bool files_only;
    bool count;               // one {file, count} per file instead of matches (GRP30)
    int before;
    int after;
    ArrayList* include;       // mem-owned glob strings
    ArrayList* exclude;
};

// one buffered record of the current file (sink data lives only for a callback)
struct IoGrepRec {
    bool context;
    bool has_line;
    GrepLineEnding line_ending;
    uint64_t byte_offset;
    uint64_t char_offset;
    uint64_t line_number;
    size_t text_off, text_len;
    size_t line_off, line_len;
};

enum IoGrepField {
    F_FILE, F_VALUE, F_INDEX, F_LINE, F_BYTE, F_TEXT, F_LINE_ENDING, F_BEFORE, F_AFTER, F_COUNT,
    F_NFIELDS
};

struct IoGrepRun {
    const IoGrepOptions* o;
    TypeMap* shape;
    int slot[F_NFIELDS];      // position in the shape, or -1
    int fields;
    uint64_t selected;        // what a total limit counts: matches, files, or lines counted
    Rooted<Array*>* result;   // the caller's rooted result array (reloaded after every allocation)
    // the source being searched
    Path* root_path;          // NULL when the source was given as text
    Item root_text;           // a text source as given (rooted by the caller)
    const char* root_os;
    size_t root_os_len;
    bool multi;               // results carry `file`
    // the current file's records
    IoGrepRec* recs;
    size_t nrec, cap;
    ByteBuilder bytes;
    bool failed;
};

Item io_grep_error(LambdaErrorCode code, const char* fmt, const char* detail) {
    char msg[256];
    snprintf(msg, sizeof(msg), fmt, detail ? detail : "");
    log_error("%s", msg);
    return err2it_or_error(err_create_heap(code, msg, NULL));
}

// ── options ────────────────────────────────────────────────────────────

bool option_bool(TypeMap* shape, void* data, const char* name, bool* out, Item* error) {
    bool found = false;
    Item v = _map_get(shape, data, name, &found);
    if (!found || get_type_id(v) == LMD_TYPE_NULL) return true;
    if (get_type_id(v) != LMD_TYPE_BOOL) {
        *error = io_grep_error(ERR_TYPE_MISMATCH, "io.grep: option '%s' must be a bool", name);
        return false;
    }
    *out = it2b(v);
    return true;
}

bool option_count(TypeMap* shape, void* data, const char* name, int64_t* out, Item* error) {
    bool found = false;
    Item v = _map_get(shape, data, name, &found);
    if (!found || get_type_id(v) == LMD_TYPE_NULL) return true;
    int64_t n = 0;
    if (!lambda_item_to_int64_exact(v, &n) || n < 0) {
        *error = io_grep_error(ERR_TYPE_MISMATCH, "io.grep: option '%s' must be a non-negative int", name);
        return false;
    }
    *out = n;
    return true;
}

bool push_text(ArrayList* list, Item v) {
    if (!is_text_type_id(get_type_id(v))) return false;
    char* copy = mem_dup_n(v.get_chars(), v.get_len(), MEM_CAT_TEMP);
    return copy && arraylist_append(list, copy) != 0;
}

bool option_globs(TypeMap* shape, void* data, const char* name, ArrayList** out, Item* error) {
    bool found = false;
    Item v = _map_get(shape, data, name, &found);
    if (!found || get_type_id(v) == LMD_TYPE_NULL) return true;
    ArrayList* list = arraylist_new(4);
    bool ok = list != NULL;
    if (ok && get_type_id(v) == LMD_TYPE_ARRAY) {
        Array* arr = v.array;
        for (int64_t i = 0; ok && i < arr->length; i++) ok = push_text(list, array_get(arr, i));
    } else if (ok) {
        ok = push_text(list, v);
    }
    if (!ok) {
        if (list) {
            for (int i = 0; i < arraylist_length(list); i++) mem_free(arraylist_get(list, i));
            arraylist_free(list);
        }
        *error = io_grep_error(ERR_TYPE_MISMATCH, "io.grep: option '%s' must be a string or an array of strings", name);
        return false;
    }
    *out = list;
    return true;
}

void free_globs(ArrayList* list) {
    if (!list) return;
    for (int i = 0; i < arraylist_length(list); i++) mem_free(arraylist_get(list, i));
    arraylist_free(list);
}

bool parse_options(Item options, IoGrepOptions* o, Item* error) {
    memset(o, 0, sizeof(*o));
    o->walk.max_depth = -1;
    o->walk.sorted = true;    // a stable order on every run
    o->grep.char_offsets = true;  // `index` is in code points, as in-memory find (GRP4)
    TypeId tid = get_type_id(options);
    if (tid == LMD_TYPE_NULL) return true;
    if (tid != LMD_TYPE_MAP) {
        *error = io_grep_error(ERR_TYPE_MISMATCH, "io.grep: options must be a map%s", NULL);
        return false;
    }
    TypeMap* shape = lambda_attr_shape(tid, options.map);
    void* data = lambda_attr_data(tid, options.map);
    if (!shape || !data) return true;

    // S17.8.1: an unknown name in an options value is ignored, with a warning
    // (a literal at the call was already checked at compile time)
    FOR_EACH_MAP_FIELD(shape, entry) {
        if (entry->name && !sys_func_option_known(SYSPROC_IO_GREP, entry->name->str, entry->name->length)) {
            log_warn("io.grep: unknown option '%.*s' ignored (S17.8.1)", (int)entry->name->length, entry->name->str);
        }
    }

    bool ignore_files = true;
    int64_t context = 0, before = -1, after = -1, limit = 0, per_file = 0, depth = -1, size = 0;
    bool ok =
        option_bool(shape, data, "ignore_case", &o->grep.ignore_case, error) &&
        option_bool(shape, data, "word", &o->grep.word, error) &&
        option_bool(shape, data, "whole_line", &o->grep.whole_line, error) &&
        option_bool(shape, data, "invert", &o->grep.invert, error) &&
        option_bool(shape, data, "line", &o->want_line, error) &&
        option_bool(shape, data, "byte_offset", &o->want_byte_offset, error) &&
        option_bool(shape, data, "text", &o->want_text, error) &&
        option_bool(shape, data, "line_ending", &o->want_line_ending, error) &&
        option_bool(shape, data, "files", &o->files_only, error) &&
        option_bool(shape, data, "count", &o->count, error) &&
        option_bool(shape, data, "hidden", &o->walk.hidden, error) &&
        option_bool(shape, data, "ignore", &ignore_files, error) &&
        option_bool(shape, data, "binary", &o->grep.binary_as_text, error) &&
        option_count(shape, data, "context", &context, error) &&
        option_count(shape, data, "before", &before, error) &&
        option_count(shape, data, "after", &after, error) &&
        option_count(shape, data, "limit", &limit, error) &&
        option_count(shape, data, "limit_per_file", &per_file, error) &&
        option_count(shape, data, "max_size", &size, error) &&
        option_globs(shape, data, "include", &o->include, error) &&
        option_globs(shape, data, "exclude", &o->exclude, error);
    if (!ok) return false;
    bool found = false;
    Item depth_item = _map_get(shape, data, "max_depth", &found);
    if (found && get_type_id(depth_item) != LMD_TYPE_NULL && !option_count(shape, data, "max_depth", &depth, error)) return false;

    o->walk.no_ignore = !ignore_files;
    o->walk.max_matches_total = (uint64_t)limit;
    o->walk.max_file_size = (uint64_t)size;
    o->walk.max_depth = found ? (int)depth : -1;
    o->grep.max_matches_per_file = (uint64_t)per_file;
    o->before = (int)(before >= 0 ? before : context);
    o->after = (int)(after >= 0 ? after : context);
    o->grep.before_context = o->before;
    o->grep.after_context = o->after;
    // both say what a result is: a file, or a file with its count
    if (o->files_only && o->count) {
        *error = io_grep_error(ERR_INVALID_OPERATION, "io.grep: options 'files' and 'count' cannot be combined%s", NULL);
        return false;
    }
    o->grep.count_lines = o->count;
    // a file's paths are the result: its first match is enough (GRP21)
    if (o->files_only) o->grep.max_matches_per_file = 1;
    return true;
}

// ── patterns ───────────────────────────────────────────────────────────

struct PatternList {
    StrBuf* text;             // every pattern's regex text, back to back
    ArrayList* spans;         // (offset, length) pairs, as uintptr_t
};

// A string is literal text and a pattern is a pattern, as in-memory find reads
// them (§9B.3); a text operand is escaped once any pattern needs regex syntax.
bool add_pattern(PatternList* list, Item p, bool escape_text, Item* error) {
    size_t start = list->text->length;
    TypeId tid = get_type_id(p);
    if (tid == LMD_TYPE_TYPE) {
        TypePattern* pattern = runtime_pattern_from_type((Type*)(p.item & 0x00FFFFFFFFFFFFFF));
        const char* src = NULL;
        size_t len = 0;
        if (!pattern || pattern->is_symbol || !pattern_unanchored_source(pattern, &src, &len)) {
            *error = io_grep_error(ERR_TYPE_MISMATCH, "io.grep: pattern must be a string or a string pattern%s", NULL);
            return false;
        }
        strbuf_append_str_n(list->text, src, len);
    } else if (is_text_type_id(tid)) {
        if (escape_text) {
            String* s = heap_strcpy((char*)p.get_chars(), p.get_len());
            if (!s) return false;
            escape_regex_literal(list->text, s);
        } else {
            strbuf_append_str_n(list->text, p.get_chars(), p.get_len());
        }
    } else {
        *error = io_grep_error(ERR_TYPE_MISMATCH, "io.grep: pattern must be a string or a string pattern%s", NULL);
        return false;
    }
    arraylist_append(list->spans, (void*)(uintptr_t)start);
    arraylist_append(list->spans, (void*)(uintptr_t)(list->text->length - start));
    return true;
}

GrepMatcher* compile_patterns(Item pattern, IoGrepOptions* o, Item* error) {
    bool any_pattern = get_type_id(pattern) == LMD_TYPE_TYPE;
    if (get_type_id(pattern) == LMD_TYPE_ARRAY) {
        for (int64_t i = 0; i < pattern.array->length; i++) {
            any_pattern = any_pattern || get_type_id(array_get(pattern.array, i)) == LMD_TYPE_TYPE;
        }
    }
    PatternList list = {strbuf_new(), arraylist_new(8)};
    bool ok = list.text && list.spans;
    if (ok && get_type_id(pattern) == LMD_TYPE_ARRAY) {
        for (int64_t i = 0; ok && i < pattern.array->length; i++) {
            ok = add_pattern(&list, array_get(pattern.array, i), any_pattern, error);
        }
    } else if (ok) {
        ok = add_pattern(&list, pattern, any_pattern, error);
    }
    GrepMatcher* matcher = NULL;
    size_t count = ok ? (size_t)arraylist_length(list.spans) / 2 : 0;
    if (ok && count == 0) {
        *error = io_grep_error(ERR_TYPE_MISMATCH, "io.grep: no pattern given%s", NULL);
        ok = false;
    }
    if (ok) {
        const char** pats = (const char**)mem_calloc(count, sizeof(char*), MEM_CAT_TEMP);
        size_t* lens = (size_t*)mem_calloc(count, sizeof(size_t), MEM_CAT_TEMP);
        if (pats && lens) {
            for (size_t i = 0; i < count; i++) {
                pats[i] = list.text->str + (uintptr_t)arraylist_get(list.spans, (int)(2 * i));
                lens[i] = (size_t)(uintptr_t)arraylist_get(list.spans, (int)(2 * i + 1));
            }
            // all-text patterns go in as fixed strings; otherwise they were escaped
            o->grep.fixed_string = !any_pattern;
            char detail[256];
            GrepStatus st = grep_matcher_create(pats, lens, count, &o->grep, &matcher, detail, sizeof(detail));
            if (st != GREP_OK) {
                *error = io_grep_error(ERR_INVALID_REGEX, "io.grep: invalid pattern: %s", detail);
                matcher = NULL;
            }
        }
        if (pats) mem_free(pats);
        if (lens) mem_free(lens);
    }
    if (list.text) strbuf_free(list.text);
    if (list.spans) arraylist_free(list.spans);
    return matcher;
}

// ── building results ───────────────────────────────────────────────────

Item make_string(const char* s, size_t len) {
    String* str = heap_strcpy((char*)s, len);
    return str ? (Item){.item = s2it(str)} : ItemError;
}

// the part of a reported path below the source, "" for the source itself
const char* relative_part(IoGrepRun* run, const char* reported) {
    size_t n = strlen(reported);
    if (n < run->root_os_len || strncmp(reported, run->root_os, run->root_os_len) != 0) return NULL;
    const char* rel = reported + run->root_os_len;
    while (*rel == '/') rel++;
    return rel;
}

// the reported file as the caller named its source: a path below a path
// source, or the source text joined with the file's place below it
Item file_value(IoGrepRun* run, const char* reported) {
    const char* rel = relative_part(run, reported);
    size_t n = strlen(reported);
    if (!run->root_path) {
        if (!rel) return make_string(reported, n);
        const char* given = run->root_text.get_chars();
        size_t given_len = run->root_text.get_len();
        if (!*rel) return make_string(given, given_len);
        StrBuf* joined = strbuf_new();
        if (!joined) return ItemError;
        strbuf_append_str_n(joined, given, given_len);
        if (given_len && given[given_len - 1] != '/') strbuf_append_char(joined, '/');
        strbuf_append_str(joined, rel);
        Item out = make_string(joined->str, joined->length);
        strbuf_free(joined);
        return out;
    }
    Path* p = run->root_path;
    if (rel) {
        while (*rel && p) {
            const char* slash = strchr(rel, '/');
            size_t seg = slash ? (size_t)(slash - rel) : strlen(rel);
            if (seg) p = path_append_len(p, rel, seg);
            rel += seg;
            while (*rel == '/') rel++;
        }
    }
    return p ? (Item){.path = p} : make_string(reported, n);
}

// the text of line `line_no` among the buffered records, if reported
bool find_line(IoGrepRun* run, uint64_t line_no, const char** text, size_t* len) {
    for (size_t i = 0; i < run->nrec; i++) {
        const IoGrepRec* r = &run->recs[i];
        if (r->line_number == line_no && r->has_line) {
            *text = (const char*)run->bytes.data + r->line_off;
            *len = r->line_len;
            return true;
        }
        if (r->line_number > line_no) break;
    }
    return false;
}

Item context_lines(IoGrepRun* run, uint64_t from, uint64_t to) {
    RootFrame roots(2);
    Rooted<Array*> lines(roots, array());
    Rooted<Item> text_item(roots, ItemNull);
    for (uint64_t n = from; n <= to && n > 0; n++) {
        const char* text = NULL;
        size_t len = 0;
        if (!find_line(run, n, &text, &len)) continue;
        text_item.set(make_string(text, len));
        if (get_type_id(text_item.get()) == LMD_TYPE_ERROR) return ItemError;
        array_push_verbatim(lines.get(), text_item.get());
    }
    return (Item){.array = lines.get()};
}

// the line's terminator as text (GRP31); null when the input ended without one
Item line_ending_item(GrepLineEnding e) {
    if (e == GREP_EOL_CRLF) return make_string("\r\n", 2);
    if (e == GREP_EOL_LF) return make_string("\n", 1);
    return ItemNull;
}

// One result map. `file` is the file value; a match record also takes `r`, a
// count record (GRP30) only `count`.
bool emit_record(IoGrepRun* run, const IoGrepRec* r, Item file, uint64_t count) {
    RootFrame roots(F_NFIELDS + 2);
    Rooted<Item> v0(roots, ItemNull), v1(roots, ItemNull), v2(roots, ItemNull), v3(roots, ItemNull),
        v4(roots, ItemNull), v5(roots, ItemNull), v6(roots, ItemNull), v7(roots, ItemNull),
        v8(roots, ItemNull), v9(roots, ItemNull);
    Rooted<Item>* vals[F_NFIELDS] = {&v0, &v1, &v2, &v3, &v4, &v5, &v6, &v7, &v8, &v9};
    Rooted<Map*> map(roots, (Map*)NULL);
    Rooted<Item> file_item(roots, file);
    const char* base = (const char*)run->bytes.data;
    if (run->slot[F_FILE] >= 0) vals[run->slot[F_FILE]]->set(file_item.get());
    if (run->slot[F_COUNT] >= 0) vals[run->slot[F_COUNT]]->set((Item){.item = i2it((int64_t)count)});
    if (r) {
        vals[run->slot[F_VALUE]]->set(make_string(base + r->text_off, r->text_len));
        vals[run->slot[F_INDEX]]->set((Item){.item = i2it((int64_t)r->char_offset)});
    }
    if (run->slot[F_LINE] >= 0) vals[run->slot[F_LINE]]->set((Item){.item = i2it((int64_t)r->line_number)});
    if (run->slot[F_BYTE] >= 0) vals[run->slot[F_BYTE]]->set((Item){.item = i2it((int64_t)r->byte_offset)});
    if (run->slot[F_TEXT] >= 0) {
        vals[run->slot[F_TEXT]]->set(r->has_line ? make_string(base + r->line_off, r->line_len) : ItemNull);
    }
    if (run->slot[F_LINE_ENDING] >= 0) vals[run->slot[F_LINE_ENDING]]->set(line_ending_item(r->line_ending));
    if (run->slot[F_BEFORE] >= 0) {
        uint64_t from = r->line_number > (uint64_t)run->o->before ? r->line_number - run->o->before : 1;
        vals[run->slot[F_BEFORE]]->set(context_lines(run, from, r->line_number - 1));
    }
    if (run->slot[F_AFTER] >= 0) {
        vals[run->slot[F_AFTER]]->set(context_lines(run, r->line_number + 1, r->line_number + run->o->after));
    }
    Item values[F_NFIELDS];
    for (int i = 0; i < run->fields; i++) {
        values[i] = vals[i]->get();
        if (get_type_id(values[i]) == LMD_TYPE_ERROR) return false;
    }
    map.set(map_alloc_for_type(run->shape, NULL, 0));
    if (!map.get()) return false;
    for (int i = 0; i < run->fields; i++) values[i] = vals[i]->get();
    map_fill_items(map.get(), values, run->fields);
    array_push_verbatim(run->result->get(), (Item){.map = map.get()});
    return true;
}

// ── the sink ───────────────────────────────────────────────────────────

GrepAction buffer(IoGrepRun* run, const GrepMatch* gm, bool context) {
    if (run->failed) return GREP_STOP;
    if (run->nrec == run->cap) {
        size_t cap = run->cap ? run->cap * 2 : 32;
        IoGrepRec* grown = (IoGrepRec*)mem_realloc(run->recs, cap * sizeof(IoGrepRec), MEM_CAT_TEMP);
        if (!grown) {
            run->failed = true;
            return GREP_STOP;
        }
        run->recs = grown;
        run->cap = cap;
    }
    IoGrepRec* r = &run->recs[run->nrec];
    memset(r, 0, sizeof(*r));
    r->context = context;
    r->line_ending = gm->line_ending;
    r->byte_offset = gm->byte_offset;
    r->char_offset = gm->char_offset;
    r->line_number = gm->line_number;
    r->text_off = run->bytes.length;
    r->text_len = gm->length;
    bool ok = byte_builder_append(&run->bytes, gm->text, gm->length);
    if (ok && gm->line) {
        r->line_off = run->bytes.length;
        r->line_len = gm->line_length;
        r->has_line = true;
        ok = byte_builder_append(&run->bytes, gm->line, gm->line_length);
    }
    if (!ok) {
        run->failed = true;
        return GREP_STOP;
    }
    run->nrec++;
    return GREP_CONTINUE;
}

GrepAction on_match(void* ud, const GrepMatch* gm) { return buffer((IoGrepRun*)ud, gm, false); }
GrepAction on_context(void* ud, const GrepMatch* gm) { return buffer((IoGrepRun*)ud, gm, true); }

GrepAction on_file_done(void* ud, const char* path, uint64_t count) {
    IoGrepRun* run = (IoGrepRun*)ud;
    if (run->failed) return GREP_STOP;
    // count is what the file contributed to a total limit, in that limit's unit
    run->selected += count;
    RootFrame roots(1);
    Rooted<Item> file(roots, ItemNull);
    // a files or count result is about the file, so it always names it
    if (run->multi || run->o->files_only || run->o->count) file.set(file_value(run, path ? path : ""));
    if (run->o->files_only) {
        if (count > 0) array_push_verbatim(run->result->get(), file.get());
    } else if (run->o->count) {
        if (count > 0 && !emit_record(run, NULL, file.get(), count)) run->failed = true;
    } else {
        for (size_t i = 0; i < run->nrec && !run->failed; i++) {
            if (!run->recs[i].context && !emit_record(run, &run->recs[i], file.get(), 0)) run->failed = true;
        }
    }
    run->nrec = 0;
    run->bytes.length = 0;
    return run->failed ? GREP_STOP : GREP_CONTINUE;
}

// ── sources ────────────────────────────────────────────────────────────

struct Source {
    Item item;                // as given
    Path* path;               // NULL for a text source
    StrBuf* os;               // local path to search
    int max_depth;            // -1, or 1 for a trailing `*` wildcard
    bool is_dir;
};

bool resolve_source(Item item, Source* src, Item* error) {
    memset(src, 0, sizeof(*src));
    src->item = item;
    src->max_depth = -1;
    TypeId tid = get_type_id(item);
    Item local = item;
    if (tid == LMD_TYPE_PATH) {
        src->path = item.path;
        // a trailing `*` searches the directory's files, `**` everything below
        if (path_ends_with_wildcard(src->path) && src->path->parent) {
            src->max_depth = path_is_wildcard_recursive(src->path) ? -1 : 1;
            src->path = src->path->parent;
            src->is_dir = true;
            local = (Item){.path = src->path};
        }
    } else if (!is_text_type_id(tid)) {
        *error = io_grep_error(ERR_TYPE_MISMATCH, "io.grep: a source must be a path or a string%s", NULL);
        return false;
    }
    src->os = lambda_get_local_path_from_item(local);
    if (!src->os) {
        *error = io_grep_error(ERR_FILE_NOT_FOUND, "io.grep: source is not a local path%s", NULL);
        return false;
    }
    FileStat st = file_stat(src->os->str);
    if (!st.exists) {
        *error = io_grep_error(ERR_FILE_NOT_FOUND, "io.grep: no such file or directory: '%s'", src->os->str);
        return false;
    }
    src->is_dir = src->is_dir || st.is_dir;
    return true;
}

}  // namespace

static Item io_grep_impl(Item source, Item pattern, Item options) {
    GUARD_ERROR3(source, pattern, options);
    if (g_dry_run) { log_debug("dry-run: fabricated io.grep()"); return (Item){.array = array()}; }
    Item error = ItemError;
    IoGrepOptions o;
    if (!parse_options(options, &o, &error)) {
        free_globs(o.include);
        free_globs(o.exclude);
        return error;
    }
    if (o.include) {
        o.walk.include_globs = (const char* const*)o.include->data;
        o.walk.include_count = (size_t)arraylist_length(o.include);
    }
    if (o.exclude) {
        o.walk.exclude_globs = (const char* const*)o.exclude->data;
        o.walk.exclude_count = (size_t)arraylist_length(o.exclude);
    }
    // context arrays and line text need lines and their numbers internally; a
    // count reports none of them, nor a code-point index
    bool context = !o.count && (o.before > 0 || o.after > 0);
    o.grep.line_numbers = !o.count && (o.want_line || context);
    o.grep.line_text = !o.count && (o.want_text || context);
    if (o.count) o.grep.char_offsets = false;

    // the sources, in order; an array searches each (§9B.1)
    int nsources = get_type_id(source) == LMD_TYPE_ARRAY ? (int)source.array->length : 1;
    Source* sources = (Source*)mem_calloc(nsources > 0 ? nsources : 1, sizeof(Source), MEM_CAT_TEMP);
    bool ok = sources != NULL;
    for (int i = 0; ok && i < nsources; i++) {
        Item it = get_type_id(source) == LMD_TYPE_ARRAY ? array_get(source.array, i) : source;
        ok = resolve_source(it, &sources[i], &error);
    }
    GrepMatcher* matcher = ok ? compile_patterns(pattern, &o, &error) : NULL;
    ok = ok && matcher;

    IoGrepRun run;
    memset(&run, 0, sizeof(run));
    run.o = &o;
    run.multi = get_type_id(source) == LMD_TYPE_ARRAY || (nsources > 0 && sources[0].is_dir);
    // the record shape, built once for the whole call
    const char* names[F_NFIELDS];
    TypeId types[F_NFIELDS];
    for (int i = 0; i < F_NFIELDS; i++) run.slot[i] = -1;
    auto add_field = [&](IoGrepField f, const char* name, TypeId type) {
        run.slot[f] = run.fields;
        names[run.fields] = name;
        types[run.fields] = type;
        run.fields++;
    };
    if (o.count) {
        // {file, count}: the match-record options have nothing to apply to
        add_field(F_FILE, "file", LMD_TYPE_ANY);
        add_field(F_COUNT, "count", LMD_TYPE_INT);
    } else {
        if (run.multi) add_field(F_FILE, "file", LMD_TYPE_ANY);
        add_field(F_VALUE, "value", LMD_TYPE_STRING);
        add_field(F_INDEX, "index", LMD_TYPE_INT);
        if (o.want_line) add_field(F_LINE, "line", LMD_TYPE_INT);
        if (o.want_byte_offset) add_field(F_BYTE, "byte_offset", LMD_TYPE_INT);
        if (o.want_text) add_field(F_TEXT, "text", LMD_TYPE_ANY);
        if (o.want_line_ending) add_field(F_LINE_ENDING, "line_ending", LMD_TYPE_ANY);
        if (o.before > 0) add_field(F_BEFORE, "before", LMD_TYPE_ANY);
        if (o.after > 0) add_field(F_AFTER, "after", LMD_TYPE_ANY);
    }

    RootFrame roots(1);
    Rooted<Array*> result(roots, array());
    run.result = &result;
    if (ok) {
        run.shape = o.files_only ? NULL : runtime_result_shape(names, types, run.fields);
        ok = byte_builder_init(&run.bytes, 1024, MEM_CAT_TEMP, false);
    }
    uint64_t remaining = o.walk.max_matches_total;
    for (int i = 0; ok && i < nsources; i++) {
        Source* src = &sources[i];
        run.root_path = src->path;
        run.root_text = src->path ? ItemNull : src->item;
        run.root_os = src->os->str;
        run.root_os_len = src->os->length;
        GrepWalkOptions walk = o.walk;
        if (src->max_depth >= 0) walk.max_depth = src->max_depth;
        // a total limit spans every source (GRP25)
        uint64_t before = run.selected;
        walk.max_matches_total = remaining;
        GrepSink sink = {&run, on_match, context ? on_context : NULL, on_file_done};
        const char* roots_os[1] = {src->os->str};
        GrepStatus st = grep_search_paths(matcher, roots_os, 1, &walk, &sink);
        if (st != GREP_OK || run.failed) {
            error = io_grep_error(st == GREP_ERR_IO ? ERR_FILE_NOT_FOUND : ERR_IO_ERROR,
                                  "io.grep: search failed in '%s'", src->os->str);
            ok = false;
            break;
        }
        if (o.walk.max_matches_total) {
            uint64_t got = run.selected - before;
            if (got >= remaining) break;
            remaining -= got;
        }
    }

    grep_matcher_destroy(matcher);
    byte_builder_destroy(&run.bytes);
    if (run.recs) mem_free(run.recs);
    for (int i = 0; sources && i < nsources; i++) {
        if (sources[i].os) strbuf_free(sources[i].os);
    }
    if (sources) mem_free(sources);
    free_globs(o.include);
    free_globs(o.exclude);
    if (!ok) return error;
    return (Item){.array = result.get()};
}

extern "C" Item pn_io_grep2(Item source, Item pattern) {
    return io_grep_impl(source, pattern, ItemNull);
}

extern "C" Item pn_io_grep3(Item source, Item pattern, Item options) {
    return io_grep_impl(source, pattern, options);
}
