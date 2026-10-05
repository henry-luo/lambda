// io_file_search.cpp — helpers io.grep and io.text_search share (see the header).

#include "io_file_search.hpp"
#include "lambda-number-runtime.hpp"
#include "../core/lambda-path.h"
#include "../../lib/str.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/file.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern "C" StrBuf* lambda_get_local_path_from_item(Item item);
Item _map_get(TypeMap* map_type, void* map_data, const char* key, bool* is_found);

Item io_fs_error(LambdaErrorCode code, const char* fmt, ...) {
    char msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    log_error("%s", msg);
    return err2it_or_error(err_create_heap(code, msg, NULL));
}

// ── options ────────────────────────────────────────────────────────────

bool io_fs_options_open(Item options, const char* fn, SysFunc id, IoFsOptionMap* out, Item* error) {
    memset(out, 0, sizeof(*out));
    out->fn = fn;
    out->id = id;
    TypeId tid = get_type_id(options);
    if (tid == LMD_TYPE_NULL) return true;
    if (tid != LMD_TYPE_MAP) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "%s: options must be a map", fn);
        return false;
    }
    out->shape = lambda_attr_shape(tid, options.map);
    out->data = lambda_attr_data(tid, options.map);
    if (!out->shape || !out->data) {
        out->shape = NULL;
        out->data = NULL;
        return true;
    }
    // S17.8.1: an unknown name in an options value is ignored, with a warning
    // (a literal at the call was already checked at compile time)
    FOR_EACH_MAP_FIELD(out->shape, entry) {
        if (entry->name && !sys_func_option_known(id, entry->name->str, entry->name->length)) {
            log_warn("%s: unknown option '%.*s' ignored (S17.8.1)", fn, (int)entry->name->length, entry->name->str);
        }
    }
    return true;
}

Item io_fs_option_get(const IoFsOptionMap* m, const char* name) {
    if (!m->shape) return ItemNull;
    bool found = false;
    Item v = _map_get(m->shape, m->data, name, &found);
    return found ? v : ItemNull;
}

bool io_fs_option_bool(const IoFsOptionMap* m, const char* name, bool* out, Item* error) {
    Item v = io_fs_option_get(m, name);
    if (get_type_id(v) == LMD_TYPE_NULL) return true;
    if (get_type_id(v) != LMD_TYPE_BOOL) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "%s: option '%s' must be a bool", m->fn, name);
        return false;
    }
    *out = it2b(v);
    return true;
}

bool io_fs_option_count(const IoFsOptionMap* m, const char* name, int64_t* out, Item* error) {
    Item v = io_fs_option_get(m, name);
    if (get_type_id(v) == LMD_TYPE_NULL) return true;
    int64_t n = 0;
    if (!lambda_item_to_int64_exact(v, &n) || n < 0) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "%s: option '%s' must be a non-negative int", m->fn, name);
        return false;
    }
    *out = n;
    return true;
}

static bool push_text(ArrayList* list, Item v) {
    if (!is_text_type_id(get_type_id(v))) return false;
    char* copy = mem_dup_n(v.get_chars(), v.get_len(), MEM_CAT_TEMP);
    return copy && arraylist_append(list, copy) != 0;
}

static void free_globs(ArrayList* list) {
    if (!list) return;
    for (int i = 0; i < arraylist_length(list); i++) mem_free(arraylist_get(list, i));
    arraylist_free(list);
}

static bool option_globs(const IoFsOptionMap* m, const char* name, ArrayList** out, Item* error) {
    Item v = io_fs_option_get(m, name);
    if (get_type_id(v) == LMD_TYPE_NULL) return true;
    ArrayList* list = arraylist_new(4);
    bool ok = list != NULL;
    if (ok && get_type_id(v) == LMD_TYPE_ARRAY) {
        Array* arr = v.array;
        for (int64_t i = 0; ok && i < arr->length; i++) ok = push_text(list, array_get(arr, i));
    } else if (ok) {
        ok = push_text(list, v);
    }
    if (!ok) {
        free_globs(list);
        *error = io_fs_error(ERR_TYPE_MISMATCH, "%s: option '%s' must be a string or an array of strings", m->fn, name);
        return false;
    }
    *out = list;
    return true;
}

void io_fs_common_init(IoFsCommonOptions* o) {
    memset(o, 0, sizeof(*o));
    o->walk.max_depth = -1;
    o->walk.sorted = true;    // a stable order on every run
}

bool io_fs_common_read(const IoFsOptionMap* m, IoFsCommonOptions* o, Item* error) {
    bool ignore_files = true;
    int64_t context = 0, before = -1, after = -1, limit = 0, per_file = 0, depth = -1, size = 0;
    bool ok =
        io_fs_option_bool(m, "ignore_case", &o->ignore_case, error) &&
        io_fs_option_bool(m, "word", &o->word, error) &&
        io_fs_option_bool(m, "line", &o->want_line, error) &&
        io_fs_option_bool(m, "byte_offset", &o->want_byte_offset, error) &&
        io_fs_option_bool(m, "text", &o->want_text, error) &&
        io_fs_option_bool(m, "line_ending", &o->want_line_ending, error) &&
        io_fs_option_bool(m, "files", &o->files_only, error) &&
        io_fs_option_bool(m, "count", &o->count, error) &&
        io_fs_option_bool(m, "hidden", &o->walk.hidden, error) &&
        io_fs_option_bool(m, "ignore", &ignore_files, error) &&
        io_fs_option_bool(m, "binary", &o->binary, error) &&
        io_fs_option_count(m, "context", &context, error) &&
        io_fs_option_count(m, "before", &before, error) &&
        io_fs_option_count(m, "after", &after, error) &&
        io_fs_option_count(m, "limit", &limit, error) &&
        io_fs_option_count(m, "limit_per_file", &per_file, error) &&
        io_fs_option_count(m, "max_size", &size, error) &&
        option_globs(m, "include", &o->include, error) &&
        option_globs(m, "exclude", &o->exclude, error);
    if (!ok) return false;
    bool has_depth = get_type_id(io_fs_option_get(m, "max_depth")) != LMD_TYPE_NULL;
    if (has_depth && !io_fs_option_count(m, "max_depth", &depth, error)) return false;

    o->walk.no_ignore = !ignore_files;
    o->walk.max_file_size = (uint64_t)size;
    o->walk.max_depth = has_depth ? (int)depth : -1;
    o->limit = (uint64_t)limit;
    o->limit_per_file = (uint64_t)per_file;
    o->before = (int)(before >= 0 ? before : context);
    o->after = (int)(after >= 0 ? after : context);
    if (o->include) {
        o->walk.include_globs = (const char* const*)o->include->data;
        o->walk.include_count = (size_t)arraylist_length(o->include);
    }
    if (o->exclude) {
        o->walk.exclude_globs = (const char* const*)o->exclude->data;
        o->walk.exclude_count = (size_t)arraylist_length(o->exclude);
    }
    // both say what a result is: a file, or a file with its count
    if (o->files_only && o->count) {
        *error = io_fs_error(ERR_INVALID_OPERATION, "%s: options 'files' and 'count' cannot be combined", m->fn);
        return false;
    }
    return true;
}

void io_fs_common_release(IoFsCommonOptions* o) {
    free_globs(o->include);
    free_globs(o->exclude);
    o->include = o->exclude = NULL;
}

// ── sources ────────────────────────────────────────────────────────────

bool io_fs_resolve_source(const char* fn, Item item, IoFsSource* src, Item* error) {
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
        *error = io_fs_error(ERR_TYPE_MISMATCH, "%s: a source must be a path or a string", fn);
        return false;
    }
    src->os = lambda_get_local_path_from_item(local);
    if (!src->os) {
        *error = io_fs_error(ERR_FILE_NOT_FOUND, "%s: source is not a local path", fn);
        return false;
    }
    FileStat st = file_stat(src->os->str);
    if (!st.exists) {
        *error = io_fs_error(ERR_FILE_NOT_FOUND, "%s: no such file or directory: '%s'", fn, src->os->str);
        return false;
    }
    src->is_dir = src->is_dir || st.is_dir;
    return true;
}

bool io_fs_resolve_sources(const char* fn, Item source, IoFsSource** out, int* count, Item* error) {
    bool is_array = get_type_id(source) == LMD_TYPE_ARRAY;
    int n = is_array ? (int)source.array->length : 1;
    IoFsSource* sources = (IoFsSource*)mem_calloc(n > 0 ? n : 1, sizeof(IoFsSource), MEM_CAT_TEMP);
    bool ok = sources != NULL;
    for (int i = 0; ok && i < n; i++) {
        ok = io_fs_resolve_source(fn, is_array ? array_get(source.array, i) : source, &sources[i], error);
    }
    *out = sources;
    *count = sources ? n : 0;
    return ok;
}

void io_fs_sources_free(IoFsSource* sources, int count) {
    for (int i = 0; sources && i < count; i++) {
        if (sources[i].os) strbuf_free(sources[i].os);
    }
    if (sources) mem_free(sources);
}

// ── result values ──────────────────────────────────────────────────────

void io_fs_root_of(const IoFsSource* src, IoFsRoot* root) {
    root->root_path = src->path;
    root->root_text = src->path ? ItemNull : src->item;
    root->root_os = src->os->str;
    root->root_os_len = src->os->length;
}

Item io_fs_make_string(const char* s, size_t len) {
    String* str = heap_strcpy((char*)s, len);
    return str ? (Item){.item = s2it(str)} : ItemError;
}

// the part of a reported path below the source, "" for the source itself
static const char* relative_part(const IoFsRoot* root, const char* reported) {
    size_t n = strlen(reported);
    if (n < root->root_os_len || strncmp(reported, root->root_os, root->root_os_len) != 0) return NULL;
    const char* rel = reported + root->root_os_len;
    while (*rel == '/') rel++;
    return rel;
}

Item io_fs_file_value(const IoFsRoot* root, const char* reported) {
    const char* rel = relative_part(root, reported);
    size_t n = strlen(reported);
    if (!root->root_path) {
        if (!rel) return io_fs_make_string(reported, n);
        const char* given = root->root_text.get_chars();
        size_t given_len = root->root_text.get_len();
        if (!*rel) return io_fs_make_string(given, given_len);
        StrBuf* joined = strbuf_new();
        if (!joined) return ItemError;
        strbuf_append_str_n(joined, given, given_len);
        if (given_len && given[given_len - 1] != '/') strbuf_append_char(joined, '/');
        strbuf_append_str(joined, rel);
        Item out = io_fs_make_string(joined->str, joined->length);
        strbuf_free(joined);
        return out;
    }
    Path* p = root->root_path;
    if (rel) {
        while (*rel && p) {
            const char* slash = strchr(rel, '/');
            size_t seg = slash ? (size_t)(slash - rel) : strlen(rel);
            if (seg) p = path_append_len(p, rel, seg);
            rel += seg;
            while (*rel == '/') rel++;
        }
    }
    return p ? (Item){.path = p} : io_fs_make_string(reported, n);
}

Item io_fs_line_ending_item(GrepLineEnding e) {
    if (e == GREP_EOL_CRLF) return io_fs_make_string("\r\n", 2);
    if (e == GREP_EOL_LF) return io_fs_make_string("\n", 1);
    return ItemNull;
}
