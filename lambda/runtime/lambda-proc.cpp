#include "../io/fs_node.hpp"
#include "io_file_search.hpp"
#include "../io/mark_output_builder.hpp"
#include "transpiler.hpp"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/mem_factory.h"
#include "../../lib/file.h"
#include "../../lib/shell.h"
#include "../../lib/str.h"
#include "../../lib/time_util.h"
#include "../core/utf_string.h"
#include "../format/format.h"
#include "../io/script_format.hpp"
#include "radiant_event_hook.h"

#include <stdarg.h>
#include <time.h>
#include <errno.h>  // for errno checking
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#endif
#include <string.h>  // for strlen
#include "../input/input.hpp"
#include "concurrency.h"
#include "re2_wrapper.hpp"
#include "../../lib/mime-detect.h"
#include <curl/curl.h>
#include <ctype.h>

extern __thread EvalContext* context;

// Global dry-run flag: when true, IO operations return fabricated results.
// Its declaration is runtime-owned in runtime-state.h.
bool g_dry_run = false;

// ============================================================================
// Dry-run fabricated results
// These return realistic mock data so code paths that process IO results
// are still exercised during fuzzy testing.
// ============================================================================

// Fabricated HTTP response body
static const char* DRY_RUN_HTTP_BODY = "{\"status\": \"ok\", \"data\": {\"id\": 1, \"message\": \"dry-run response\"}, \"timestamp\": 1700000000}";
// Fabricated command output
static const char* DRY_RUN_CMD_OUTPUT = "dry-run-output";

static Item dry_run_fabricated_output() {
    log_debug("dry-run: fabricated output() call");
    // return fabricated bytes-written count
    return {.item = i2it(42)};
}

static Item dry_run_fabricated_cmd() {
    log_debug("dry-run: fabricated cmd() call");
    String* output = heap_strcpy((char*)DRY_RUN_CMD_OUTPUT, strlen(DRY_RUN_CMD_OUTPUT));
    return {.item = s2it(output)};
}

// ============================================================================

Item pn_print(Item item) {
    TypeId type_id = get_type_id(item);
    log_debug("pn_print: %d", type_id);
    if (type_id == LMD_TYPE_ERROR) {
        // S7.9.3: print participates in an error -- it inspects instead of
        // propagating. fn_string has no text for an error (string(err) is the
        // error), so render it as the printer does inside any value (LR12-25).
        StrBuf* sb = strbuf_new();
        print_item(sb, item, 0, null);
        printf("%s", sb->str); // PRINTF_OK: Lambda's procedural print() builtin.
        strbuf_free(sb);
        return ItemNull;
    }
    uint64_t before = virtual_failure_version;
    String *str = fn_string(item);
    if (virtual_failure_version != before) return err2it_or_error(context ? context->last_error : nullptr);
    if (str) {
        printf("%s", str->chars); // PRINTF_OK: Lambda's procedural print() builtin.
        // print() is line-buffered wherever stdout goes: on a pipe, stdio would
        // hold whole blocks, so a parent reading a Lambda child's lines (io.exec
        // progress, AR-E5v2) saw nothing until the child exited.
        if (memchr(str->chars, '\n', str->len)) fflush(stdout);
    }
    return ItemNull;
}

/**
 * emit(event_name, event_data) — Dispatch a custom event to the nearest
 * parent template handler that matches the event name.
 * The optional Radiant handler is registered by the upper UI layer.
 */
#ifndef LAMBDA_NO_EMIT
Item pn_emit(Item event_name, Item event_data) {
    return lambda_radiant_emit(event_name, event_data);
}
#endif

double pn_clock() {
    // elapsed-time APIs share the monotonic clock on every platform.
    return time_now_seconds();
}

// Helper: Create parent directories recursively for a file path
static int create_parent_dirs(const char* file_path) {
    char* dir = file_path_dirname(file_path);
    if (!dir) return 0;
    bool ok = create_dir(dir);
    mem_free(dir);
    return ok ? 0 : -1;
}

static bool write_raw_bytes(const char* file_path, const char* mode, bool atomic,
                            const void* data, size_t length) {
    // The shared writer checks close/flush and reserves atomic destinations exclusively.
    return (atomic ? write_binary_file_atomic(file_path, data, length)
        : mode[0] == 'a' ? append_binary_file(file_path, (const char*)data, length)
        : write_binary_file(file_path, (const char*)data, length)) == 0;
}

// Unified output implementation
// source: data to output
// target_item: file path (String, Symbol, or Path)
// format_str: optional format type ('json, 'yaml, 'html, etc.), NULL for auto-detect
// append: true for append mode, false for write (truncate) mode
// atomic: if true, write to temp file first then rename (only for write mode, not append)
// Returns: bytes written on success, ItemError on failure
static Item pn_output_internal(Item source, Item target_item, const char* format_str, bool append, bool atomic, const ZipWriteOptions* zip_options = nullptr) {
    if (g_dry_run) return dry_run_fabricated_output();
    const char* mode = append ? "a" : "w";
    const char* mode_binary = append ? "ab" : "wb";

    // atomic writes only make sense for write mode (not append)
    if (atomic && append) {
        log_debug("pn_output_internal: atomic mode ignored for append");
        atomic = false;
    }

    // Validate target type: must be string, symbol, or path
    TypeId target_type = get_type_id(target_item);
    if (!is_text_type_id(target_type) && target_type != LMD_TYPE_PATH) {
        log_error("pn_output_internal: target must be string, symbol, or path, got type %s", get_type_name(target_type));
        return ItemError;
    }

    // Convert target to unified Target struct
    Url* cwd = context ? (Url*)context->cwd : NULL;
    Target* target = item_to_target(target_item.item, cwd);
    if (!target) {
        log_error("pn_output_internal: failed to convert item to target");
        return ItemError;
    }

    // Check if target is writable (local file only)
    if (!target_is_local(target)) {
        log_error("pn_output_internal: cannot write to remote URL (scheme=%d)", target->scheme);
        target_free(target);
        return ItemError;
    }

    // Get local file path from target
    StrBuf* path_buf = (StrBuf*)target_to_local_path(target, cwd);
    if (!path_buf || !path_buf->str || path_buf->length == 0) {
        log_error("pn_output_internal: failed to resolve target to local path");
        target_free(target);
        if (path_buf) strbuf_free(path_buf);
        return ItemError;
    }

    const char* file_path = path_buf->str;
    log_debug("pn_output_internal: writing to %s (mode=%s, format=%s)", file_path, mode, format_str ? format_str : "auto");

    // Always try to create parent directories for resolved paths
    if (create_parent_dirs(file_path) != 0) {
        log_error("pn_output_internal: failed to create directories for %s", file_path);
        strbuf_free(path_buf);
        target_free(target);
        return ItemError;
    }

    // Clean up target (no longer needed)
    target_free(target);

    const char* extension = file_path_ext(file_path);
    bool zip = format_str ? !strcmp(format_str, "zip") : extension && !str_icmp_cstr(extension, ".zip");
    if (zip) {
        if (append) {
            strbuf_free(path_buf);
            return io_fs_error(ERR_INVALID_OPERATION, "output: ZIP append is unsupported");
        }
        RootFrame roots(1);
        Rooted<Item> source_root(roots, source);
        StrBuf* encoded = strbuf_new();
        ZipError error = {};
        bool ok = encoded && fs_zip_encode(source_root.get(), zip_options, encoded, &error);
        if (ok) ok = write_binary_file_atomic(file_path, encoded->str, encoded->length) == 0;
        size_t size = encoded ? encoded->length : 0;
        if (encoded) strbuf_free(encoded);
        strbuf_free(path_buf);
        return ok ? (Item){.item = i2it((int64_t)size)}
            : io_fs_error(ERR_FILE_WRITE_ERROR, "output ZIP: %s", error.message[0] ? error.message : "atomic write failed");
    }

    // handle error source: report and return error
    TypeId source_type = get_type_id(source);
    if (source_type == LMD_TYPE_ERROR) {
        log_error("pn_output_internal: cannot output error to file");
        strbuf_free(path_buf);
        return ItemError;
    }

    if (source_type == LMD_TYPE_ELEMENT && container_is_virtual_list(source.container)) {
        // A completed file owns already encoded bytes; formatting its element
        // would discard the payload, and text I/O could alter binary streams.
        const char* bytes = NULL;
        size_t size = 0;
        bool ok = virtual_output_bytes(source, &bytes, &size) &&
            write_raw_bytes(file_path, mode_binary, atomic, bytes, size);
        strbuf_free(path_buf);
        return ok ? (Item){.item = i2it((int64_t)size)} : ItemError;
    }

    // string source: output as raw text (ignore format)
    if (source_type == LMD_TYPE_STRING) {
        String* str = it2s(source);
        if (!str) {
            log_error("pn_output_internal: source string is null");
            strbuf_free(path_buf);
            return ItemError;
        }

        if (!write_raw_bytes(file_path, mode, atomic, str->chars, str->len)) {
            strbuf_free(path_buf);
            return ItemError;
        }
        log_debug("pn_output_internal: wrote %zu bytes (text) to %s", (size_t)str->len, file_path);
        strbuf_free(path_buf);
        return {.item = i2it((int64_t)str->len)};
    }

    // binary source: output as raw binary data (ignore format)
    if (source_type == LMD_TYPE_BINARY) {
        // it2s accepts only text-tagged values; using it here discarded valid
        // binary pointers before the raw length-delimited write could occur.
        Binary* bin = source.get_safe_binary();
        if (!bin) {
            log_error("pn_output_internal: source binary is null");
            strbuf_free(path_buf);
            return ItemError;
        }

        size_t bin_len = binary_length(bin);
        if (!write_raw_bytes(file_path, mode_binary, atomic, binary_data(bin), bin_len)) {
            strbuf_free(path_buf);
            return ItemError;
        }

        log_debug("pn_output_internal: wrote %zu bytes (binary) to %s", bin_len, file_path);
        strbuf_free(path_buf);
        return {.item = i2it((int64_t)bin_len)};
    }

    // determine format for structured data
    const char* effective_format = format_str;
    if (!effective_format) {
        // auto-detect format from file extension
        const char* extension = file_path_ext(file_path);
        if (extension) {
            const char* ext = extension + 1;
            if (strcmp(ext, "json") == 0) effective_format = "json";
            else if (strcmp(ext, "yaml") == 0 || strcmp(ext, "yml") == 0) effective_format = "yaml";
            else if (strcmp(ext, "xml") == 0) effective_format = "xml";
            else if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) effective_format = "html";
            else if (strcmp(ext, "md") == 0) effective_format = "markdown";
            else if (strcmp(ext, "csv") == 0) effective_format = "csv";
            else if (strcmp(ext, "wiki") == 0 || strcmp(ext, "mediawiki") == 0) effective_format = "wiki";
            else if (const char* script_target = script_format_target(ext)) effective_format = script_target;
            else if (strcmp(ext, "typ") == 0) effective_format = "typst";
            else if (strcmp(ext, "asc") == 0) effective_format = "asciidoc";
            else if (ext[0] >= '1' && ext[0] <= '9' && !ext[1]) effective_format = "man";
            else if (strcmp(ext, "txt") == 0) effective_format = "text";
            else if (strcmp(ext, "toml") == 0) effective_format = "toml";
            else if (strcmp(ext, "ini") == 0) effective_format = "ini";
            // D2.9.1: Mark filenames have one canonical extension.
            else if (strcmp(ext, "mark") == 0) effective_format = "mark";
            // unknown extension: leave as NULL for fallback handling below
        }
        // no extension or unknown extension: effective_format remains NULL
    }

    // fallback when format not detected: based on data type
    if (!effective_format) {
        if (source_type == LMD_TYPE_STRING) {
            effective_format = "text";
        } else if (source_type == LMD_TYPE_BINARY) {
            effective_format = "binary";
        } else {
            effective_format = "mark";  // default to mark for structured data
        }
    }

    log_debug("pn_output_internal: using format '%s'", effective_format);

    // Complete formatting before opening the destination, including lazy read failures.
    RootFrame roots(2);
    Rooted<Item> source_root(roots, source);
    Rooted<Item> script_result(roots, ItemNull);
    Pool* temp_pool = mem_pool_create(NULL, MEM_ROLE_TEMP, "proc.output.format");
    size_t format_length = strlen(effective_format);
    String* format = (String*)pool_calloc(temp_pool, sizeof(String) + format_length + 1);
    if (!format) { mem_pool_destroy(temp_pool); strbuf_free(path_buf); return ItemError; }
    format->len = format_length; format->is_ascii = 1;
    memcpy(format->chars, effective_format, format_length);
    String* formatted = nullptr;
    if (script_format_route(effective_format).target) {
        script_result.set(fn_format2(source_root.get(), Item{.item = s2it(format)}));
        if (item_is_error(script_result.get())) {
            mem_pool_destroy(temp_pool); strbuf_free(path_buf); return script_result.get();
        }
        formatted = script_result.get().get_string();
    } else formatted = format_data(source_root.get(), format, nullptr, temp_pool);
    if (!formatted) {
        mem_pool_destroy(temp_pool);
        strbuf_free(path_buf);
        return io_fs_error(ERR_FORMAT_ERROR, "output: formatting failed");
    }
    StrBuf* mark = nullptr;
    const char* data = formatted->chars;
    size_t length = formatted->len;
    if (!strcmp(effective_format, "mark")) {
        mark = strbuf_new_cap(length + 2);
        if (!mark) { mem_pool_destroy(temp_pool); strbuf_free(path_buf); return ItemError; }
        strbuf_append_str_n(mark, data, length);
        strbuf_append_char(mark, '\n');
        data = mark->str; length = mark->length;
    }
    bool ok = write_raw_bytes(file_path, mode_binary, atomic, data, length);
    if (mark) strbuf_free(mark);
    mem_pool_destroy(temp_pool);
    strbuf_free(path_buf);
    return ok ? (Item){.item = i2it((int64_t)length)} : ItemError;
}

// 2-parameter wrapper: output(source, trg) - writes data to target (default format, write mode)
// Returns: bytes written on success, error on failure
Item pn_output2(Item source, Item target_item) {
    return pn_output_internal(source, target_item, NULL, false, false);
}

// Helper: Get string from map field (returns NULL if not found or wrong type)
static const char* get_map_string_field(Map* map, const char* field_name) {
    if (!map) return NULL;

    String* key = heap_strcpy((char*)field_name, strlen(field_name));
    Item key_item = {.item = s2it(key)};
    Item value = map_get(map, key_item);

    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_SYMBOL || value_type == LMD_TYPE_STRING) {
        const char* chars = value.get_chars();
        if (chars) {
            return chars;
        }
    }
    return NULL;
}

// Helper: Get boolean from map field (returns default_val if not found or wrong type)
static bool get_map_bool_field(Map* map, const char* field_name, bool default_val) {
    if (!map) return default_val;

    String* key = heap_strcpy((char*)field_name, strlen(field_name));
    Item key_item = {.item = s2it(key)};
    Item value = map_get(map, key_item);

    TypeId value_type = get_type_id(value);
    if (value_type == LMD_TYPE_BOOL) {
        return value.bool_val;
    }
    return default_val;
}

// 3-parameter wrapper: output(source, target, options) - with options map, symbol, or string
// If 3rd arg is a symbol or string, it is treated as the output format.
// If 3rd arg is a map, it supports:
//   format: symbol - output format ('json, 'yaml, 'xml, 'html, 'markdown, 'text, 'mark, etc.)
//   mode: symbol - 'write or 'append (default: 'write)
//   atomic: bool - write to temp file first, then rename (default: false)
// Returns: bytes written on success, error on failure
Item pn_output3(Item source, Item target_item, Item options_item) {
    ZipWriteOptions zip_options = zip_default_write_options();
    ZipLimits limits = zip_options.limits;
    ZipError zip_error = {};
    if (get_type_id(options_item) == LMD_TYPE_MAP) {
        IoFsOptionMap option_map = {};
        Item error = ItemNull;
        if (!io_fs_options_open(options_item, "output", SYSPROC_OUTPUT3, &option_map, &error)) return error;
    }
    if (!fs_zip_options(options_item, &limits, &zip_options, &zip_error))
        return io_fs_error(ERR_TYPE_MISMATCH, "output: %s", zip_error.message);
    const char* format_str = NULL;
    bool append = false;
    bool atomic = false;

    TypeId options_type = get_type_id(options_item);
    if (options_type == LMD_TYPE_MAP) {
        Map* options = options_item.map;

        // extract format option
        format_str = get_map_string_field(options, "format");

        // extract mode option
        const char* mode_str = get_map_string_field(options, "mode");
        if (mode_str && strcmp(mode_str, "append") == 0) {
            append = true;
        }

        // extract atomic option
        atomic = get_map_bool_field(options, "atomic", false);
    } else if (options_type == LMD_TYPE_SYMBOL || options_type == LMD_TYPE_STRING) {
        // treat symbol or string as the format
        const char* fmt_chars = options_item.get_chars();
        uint32_t fmt_len = options_item.get_len();
        if (fmt_chars && fmt_len > 0) {
            format_str = fmt_chars;
        }
    } else if (options_type != LMD_TYPE_NULL) {
        log_error("pn_output3: options must be a map, symbol, string, or null, got type %s", get_type_name(options_type));
        return ItemError;
    }

    return pn_output_internal(source, target_item, format_str, append, atomic, &zip_options);
}

// 2-parameter append wrapper: used by |>> pipe operator
// Directly calls pn_output_internal with append=true, no format, no atomic
Item pn_output_append(Item source, Item target_item) {
    return pn_output_internal(source, target_item, NULL, true, false);
}

extern void free_fetch_response(FetchResponse* response);
Item _map_get(TypeMap* type, void* data, const char* key, bool* found);
extern "C" StrBuf* lambda_get_local_path_from_item(Item item);

// ---------------------------------------------------------------------------
// fetch / io.fetch (AR-E1, AR-E2, AR-E3v2). The transfer runs on the thread
// pool while the task parks; the result is one map {status, ok, url, headers,
// body, bytes}; `to` streams the body to disk; `progress` is a callback run
// on the task's own stack between parks, never a message the runtime sends.
// ---------------------------------------------------------------------------

#define FETCH_DEFAULT_USER_AGENT "Lambda/0.1"
#define FETCH_LOCAL_CHUNK ((size_t)256 * 1024)

struct FetchJob {
    // the request, all mem-owned: the worker thread holds no managed values (D5.3.3)
    char* url;
    char* method;
    char* body;
    size_t body_size;
    char** header_lines;
    int header_count;
    char* user_agent;
    char* local_path;          // a local source is read or copied instead of fetched
    char* sink_path;           // `to`: the final path
    char* part_path;           // sink_path + ".part", written first
    bool resume;
    int64_t resume_from;
    int64_t timeout_seconds;
    bool follow_redirects;
    bool verify_tls;
    int64_t max_bytes;
    // the outcome, written by the worker
    FetchResponse* response;
    FetchFailure failure;
    int local_error;           // errno of a failed local read or copy
    char* local_data;          // body of a local read (mem-owned)
    int64_t local_size;        // bytes read or copied from a local source
    char* mime;                // content type of a local source
    long local_status;         // 200, or 206/416 for a Range request on a local source
    char content_range[96];    // the 206 response's content-range header
    // progress (AR-E3v2): the worker writes the counters, the task reads them
    LambdaOffload* job;
    int64_t received;
    int64_t total;
    uint64_t started_ms;
    Rooted<Item>* callback;    // the progress function, rooted by the caller's frame
    int64_t progress_ms;
    int64_t delivered;         // `received` of the last level the callback saw
    bool delivered_any;
};

static void fetch_job_free(FetchJob* job) {
    if (!job) return;
    mem_free(job->url);
    mem_free(job->method);
    mem_free(job->body);
    for (int i = 0; job->header_lines && i < job->header_count; i++) mem_free(job->header_lines[i]);
    mem_free(job->header_lines);
    mem_free(job->user_agent);
    mem_free(job->local_path);
    mem_free(job->sink_path);
    mem_free(job->part_path);
    mem_free(job->local_data);
    mem_free(job->mime);
    if (job->response) free_fetch_response(job->response);
    mem_free(job);
}

static void fetch_release_orphan(void* data) {
    fetch_job_free((FetchJob*)data);
}

static char* fetch_dup(const char* chars, size_t length) {
    return mem_dup_n(chars ? chars : "", length, MEM_CAT_EVAL);
}

// Worker thread: forward the counters and ask for a tick.
static int fetch_progress_hook(void* data, int64_t received, int64_t total) {
    FetchJob* job = (FetchJob*)data;
    __atomic_store_n(&job->received, received, __ATOMIC_RELAXED);
    __atomic_store_n(&job->total, total, __ATOMIC_RELAXED);
    if (job->callback) lambda_offload_notify(job->job);
    return 0;
}

// The first `Range: bytes=a-b` or `bytes=a-` header line, as an inclusive
// [start, end] (end -1 when open); false when there is none or it is not a
// single byte range, which a server would also ignore.
static bool fetch_local_range(const FetchJob* job, int64_t* start, int64_t* end) {
    for (int i = 0; i < job->header_count; i++) {
        const char* line = job->header_lines[i];
        if (!line || !str_istarts_with_cstr(line, "range:")) continue;
        const char* spec = line + 6;
        while (*spec == ' ' || *spec == '\t') spec++;
        if (!str_istarts_with_cstr(spec, "bytes=")) return false;
        spec += 6;
        char* rest = NULL;
        long long a = strtoll(spec, &rest, 10);
        if (rest == spec || *rest != '-' || a < 0) return false;
        const char* tail = rest + 1;
        long long b = -1;
        if (*tail) {
            char* after = NULL;
            b = strtoll(tail, &after, 10);
            if (after == tail || *after || b < a) return false;
        }
        *start = a;
        *end = b;
        return true;
    }
    return false;
}

// Worker thread: a local source is read whole, or copied in chunks with the
// same progress calls a network transfer makes. A single byte range is
// honoured as an HTTP server would (206 with content-range, 416 past the end),
// so HLS byte-range segments work over file: URLs.
static void fetch_run_local(FetchJob* job, const int* cancel_flag) {
    FILE* in = fopen(job->local_path, "rb");
    if (!in) { job->local_error = errno ? errno : EIO; return; }
    int64_t size = file_size(job->local_path);
    if (size < 0) size = 0;
    job->local_status = 200;
    int64_t range_start = 0, range_end = -1;
    if (!job->part_path && fetch_local_range(job, &range_start, &range_end)) {
        if (range_start >= size) {
            job->local_status = 416;
            job->local_data = (char*)mem_calloc(1, 1, MEM_CAT_EVAL);
            job->local_size = 0;
            fclose(in);
            return;
        }
        if (range_end < 0 || range_end >= size) range_end = size - 1;
        if (fseek(in, (long)range_start, SEEK_SET) != 0) { job->local_error = errno ? errno : EIO; fclose(in); return; }
        job->local_status = 206;
        snprintf(job->content_range, sizeof(job->content_range), "bytes %lld-%lld/%lld",
            (long long)range_start, (long long)range_end, (long long)size);
        size = range_end - range_start + 1;
    }
    __atomic_store_n(&job->total, size, __ATOMIC_RELAXED);
    if (!job->part_path) {
        if (job->max_bytes > 0 && size > job->max_bytes) {
            job->failure.over_limit = true;
            snprintf(job->failure.message, sizeof(job->failure.message), "file exceeds the byte limit");
            fclose(in);
            return;
        }
        char* data = (char*)mem_alloc((size_t)size + 1, MEM_CAT_EVAL);
        if (!data) { job->local_error = ENOMEM; fclose(in); return; }
        size_t got = size ? fread(data, 1, (size_t)size, in) : 0;
        data[got] = '\0';
        job->local_data = data;
        job->local_size = (int64_t)got;
        fetch_progress_hook(job, (int64_t)got, size);
        fclose(in);
        return;
    }
    bool resume = job->resume && job->resume_from > 0 && job->resume_from <= size;
    FILE* out = fopen(job->part_path, resume ? "ab" : "wb");
    if (!out) { job->local_error = errno ? errno : EIO; fclose(in); return; }
    int64_t received = 0;
    if (resume) {
        if (fseek(in, (long)job->resume_from, SEEK_SET) == 0) received = job->resume_from;
        else { fclose(out); out = fopen(job->part_path, "wb"); if (!out) { job->local_error = errno; fclose(in); return; } }
    }
    char* buffer = (char*)mem_alloc(FETCH_LOCAL_CHUNK, MEM_CAT_EVAL);
    if (!buffer) { job->local_error = ENOMEM; fclose(out); fclose(in); return; }
    while (true) {
        if (cancel_flag && __atomic_load_n(cancel_flag, __ATOMIC_ACQUIRE)) { job->failure.aborted = true; break; }
        size_t got = fread(buffer, 1, FETCH_LOCAL_CHUNK, in);
        if (got == 0) break;
        if (job->max_bytes > 0 && received + (int64_t)got > job->max_bytes) {
            job->failure.over_limit = true;
            snprintf(job->failure.message, sizeof(job->failure.message), "file exceeds the byte limit");
            break;
        }
        if (fwrite(buffer, 1, got, out) != got) { job->local_error = errno ? errno : EIO; break; }
        received += (int64_t)got;
        fetch_progress_hook(job, received, size);
    }
    mem_free(buffer);
    if (fclose(out) != 0 && !job->local_error) job->local_error = errno ? errno : EIO;
    fclose(in);
    job->local_size = received;
}

static void fetch_run(LambdaOffload* offload, void* data) {
    FetchJob* job = (FetchJob*)data;
    job->job = offload;
    job->started_ms = time_now_ms();
    if (job->local_path) { fetch_run_local(job, lambda_offload_cancel_flag(offload)); return; }
    FetchConfig config = {};
    config.method = job->method ? job->method : "GET";
    config.body = job->body;
    config.body_size = job->body_size;
    config.headers = job->header_lines;
    config.header_count = job->header_count;
    config.timeout_seconds = (long)job->timeout_seconds;
    config.max_redirects = job->follow_redirects ? 5 : 0;
    config.user_agent = job->user_agent ? job->user_agent : FETCH_DEFAULT_USER_AGENT;
    config.verify_ssl = job->verify_tls;
    config.enable_compression = true;
    config.max_response_bytes = job->max_bytes > 0 ? (size_t)job->max_bytes : 0;
    config.sink_path = job->part_path;
    config.resume_from = job->resume ? job->resume_from : 0;
    config.progress = fetch_progress_hook;
    config.progress_data = job;
    config.cancel_flag = lambda_offload_cancel_flag(offload);
    job->response = http_fetch2(job->url, &config, &job->failure);
}

static bool fetch_succeeded(const FetchJob* job) {
    if (job->local_path) return !job->local_error && !job->failure.over_limit && !job->failure.aborted;
    return job->response != NULL;
}

// Loop thread, on the task's stack: hand the callback the latest level.
static Item fetch_progress(void* data, bool final) {
    FetchJob* job = (FetchJob*)data;
    if (!job->callback) return ItemNull;
    if (final && !fetch_succeeded(job)) return ItemNull;
    int64_t received = __atomic_load_n(&job->received, __ATOMIC_RELAXED);
    int64_t total = __atomic_load_n(&job->total, __ATOMIC_RELAXED);
    // the final level is delivered once: skip it when the last tick already carried it
    if (final && job->delivered_any && job->delivered == received && total == received) return ItemNull;
    job->delivered = received;
    job->delivered_any = true;
    uint64_t now = time_now_ms();
    double elapsed = now > job->started_ms ? (double)(now - job->started_ms) / 1000.0 : 0.0;
    double rate = elapsed > 0 ? (double)received / elapsed : 0.0;
    RootFrame roots(3);
    Rooted<Item> url(roots, ItemNull), rate_item(roots, ItemNull);
    Rooted<Map*> record(roots, (Map*)NULL);
    String* url_string = heap_strcpy(job->url ? job->url : "", job->url ? (int64_t)strlen(job->url) : 0);
    if (!url_string) return ItemNull;
    url.set((Item){.item = s2it(url_string)});
    rate_item.set(push_d(rate));
    static const char* names[] = {"url", "received", "total", "rate"};
    static const TypeId types[] = {LMD_TYPE_STRING, LMD_TYPE_INT, LMD_TYPE_ANY, LMD_TYPE_ANY};
    record.set(map_alloc_for_type(runtime_result_shape(names, types, 4), NULL, 0));
    if (!record.get()) return ItemNull;
    Item values[] = {url.get(), (Item){.item = i2it(received)},
        total >= 0 ? (Item){.item = i2it(total)} : ItemNull, rate_item.get()};
    map_fill_items(record.get(), values, 4);
    Item callback = job->callback->get();
    Item outcome = fn_call1((Function*)(uintptr_t)callback.item, (Item){.map = record.get()});
    return get_type_id(outcome) == LMD_TYPE_ERROR ? outcome : ItemNull;
}

// Whether a body with this content type is text to Lambda (a string) or bytes.
static bool fetch_content_is_text(const char* content_type, const char* data, size_t size) {
    if (content_type && *content_type) {
        const char* end = strchr(content_type, ';');
        size_t length = end ? (size_t)(end - content_type) : strlen(content_type);
        while (length && content_type[length - 1] == ' ') length--;
        char type[96];
        if (length >= sizeof(type)) length = sizeof(type) - 1;
        for (size_t i = 0; i < length; i++) type[i] = (char)tolower((unsigned char)content_type[i]);
        type[length] = '\0';
        if (strncmp(type, "text/", 5) == 0) return true;
        static const char* const text_types[] = {"json", "xml", "javascript", "ecmascript", "yaml",
            "toml", "x-www-form-urlencoded", "sql", "graphql", "csv", "x-sh", "x-ndjson", "ld+json",
            "mpegurl", NULL};  // HLS playlists (application/vnd.apple.mpegurl, audio/x-mpegurl) are UTF-8 text
        for (int i = 0; text_types[i]; i++) if (strstr(type, text_types[i])) return true;
        if (strncmp(type, "image/", 6) == 0 && !strstr(type, "svg")) return false;
        if (strncmp(type, "audio/", 6) == 0 || strncmp(type, "video/", 6) == 0 ||
            strncmp(type, "font/", 5) == 0) return false;
        if (strcmp(type, "application/octet-stream") != 0 && strncmp(type, "application/", 12) == 0 &&
            !strstr(type, "svg")) return false;
    }
    // no usable type: a NUL byte near the start says bytes
    size_t probe = size < 8192 ? size : 8192;
    return !(data && memchr(data, 0, probe));
}

// Builds a map of header names (lowercased; a repeated name keeps its last
// value) to values, on the runtime shape tree.
static Map* fetch_headers_map(char** lines, int count, const char* type_name, const char* type_value) {
    int capacity = count + 1;
    char** names = (char**)mem_calloc((size_t)capacity, sizeof(char*), MEM_CAT_EVAL);
    char** values = (char**)mem_calloc((size_t)capacity, sizeof(char*), MEM_CAT_EVAL);
    int used = 0;
    auto put = [&](const char* name, size_t name_len, const char* value, size_t value_len) {
        while (value_len && (value[0] == ' ' || value[0] == '\t')) { value++; value_len--; }
        while (value_len && (value[value_len - 1] == '\r' || value[value_len - 1] == '\n' ||
            value[value_len - 1] == ' ')) value_len--;
        char* key = fetch_dup(name, name_len);
        if (!key) return;
        for (size_t i = 0; i < name_len; i++) key[i] = (char)tolower((unsigned char)key[i]);
        for (int i = 0; i < used; i++) {
            if (strcmp(names[i], key) == 0) {
                mem_free(key);
                mem_free(values[i]);
                values[i] = fetch_dup(value, value_len);
                return;
            }
        }
        names[used] = key;
        values[used] = fetch_dup(value, value_len);
        used++;
    };
    if (names && values) {
        for (int i = 0; i < count; i++) {
            const char* line = lines[i];
            const char* colon = line ? strchr(line, ':') : NULL;
            if (!colon || colon == line) continue;
            put(line, (size_t)(colon - line), colon + 1, strlen(colon + 1));
        }
        if (type_name && type_value) put(type_name, strlen(type_name), type_value, strlen(type_value));
    }
    Map* result = NULL;
    Input* tree = runtime_shape_tree();
    TypeMap* shape = NULL;
    if (names && values && tree) {
        RootSpan span((size_t)(used > 0 ? used : 1));
        Item* slots = span.items();
        bool ok = span.valid();
        for (int i = 0; ok && i < used; i++) {
            shape = type_tree_add_map_field_chars(tree, shape, names[i], (uint32_t)strlen(names[i]),
                LMD_TYPE_STRING, NULL);
            String* text = heap_strcpy(values[i], (int64_t)strlen(values[i]));
            if (!shape || !text) ok = false;
            else slots[i] = (Item){.item = s2it(text)};
        }
        if (ok && used == 0) {
            shape = context && context->pool
                ? (TypeMap*)alloc_type(context->pool, LMD_TYPE_MAP, sizeof(TypeMap)) : NULL;
        }
        if (ok && shape) {
            result = map_alloc_for_type(shape, NULL, 0);
            if (result && used > 0) map_fill_items(result, slots, used);
        }
    }
    for (int i = 0; i < used; i++) { mem_free(names[i]); mem_free(values[i]); }
    mem_free(names);
    mem_free(values);
    return result;
}

static Item fetch_error(LambdaErrorCode code, const char* what, const char* detail) {
    return io_fs_error(code, "io.fetch: %s%s%s", what, detail && *detail ? ": " : "", detail ? detail : "");
}

// The result map, or the error the transfer produced.
static Item fetch_result(FetchJob* job) {
    if (job->local_path) {
        if (job->local_error) {
            LambdaErrorCode code = job->local_error == ENOENT ? ERR_FILE_NOT_FOUND
                : (job->local_error == EACCES ? ERR_FILE_ACCESS_DENIED : ERR_FILE_READ_ERROR);
            return fetch_error(code, job->local_path, strerror(job->local_error));
        }
        if (job->failure.over_limit) return fetch_error(ERR_IO_ERROR, job->local_path, job->failure.message);
        if (job->failure.aborted) return fetch_error(ERR_CANCELLED, job->local_path, "transfer aborted");
    } else if (!job->response) {
        LambdaErrorCode code = job->failure.curl_code == CURLE_OPERATION_TIMEDOUT
            ? ERR_NETWORK_TIMEOUT : (job->failure.over_limit ? ERR_IO_ERROR : ERR_NETWORK_ERROR);
        return fetch_error(code, job->url, job->failure.message);
    }
    if (job->part_path) {
        // the body is complete: give it its final name
        if (file_rename(job->part_path, job->sink_path) != 0) {
            return fetch_error(ERR_FILE_WRITE_ERROR, job->sink_path, strerror(errno));
        }
    }
    long status = job->local_path ? job->local_status : job->response->status_code;
    const char* final_url = job->local_path ? job->url
        : (job->response->effective_url ? job->response->effective_url : job->url);
    const char* content_type = job->local_path ? job->mime : job->response->content_type;
    const char* data = job->local_path ? job->local_data : job->response->data;
    int64_t size = job->local_path ? job->local_size : (int64_t)job->response->size;
    RootFrame roots(4);
    Rooted<Item> url(roots, ItemNull), body(roots, ItemNull);
    Rooted<Map*> headers(roots, (Map*)NULL), result(roots, (Map*)NULL);
    String* url_string = heap_strcpy(final_url ? final_url : "", final_url ? (int64_t)strlen(final_url) : 0);
    if (!url_string) return fetch_error(ERR_OUT_OF_MEMORY, job->url, NULL);
    url.set((Item){.item = s2it(url_string)});
    if (!job->part_path) {
        if (fetch_content_is_text(content_type, data, (size_t)size)) {
            String* text = heap_strcpy(data ? data : "", size);
            if (!text) return fetch_error(ERR_OUT_OF_MEMORY, job->url, NULL);
            body.set((Item){.item = s2it(text)});
        } else if (size > 0) {
            // S2.2.2v2: an empty byte body is absent, represented by null
            body.set((Item){.item = x2it(heap_binary_from_bytes(data, size))});
        }
    }
    char content_range_line[112];
    char* local_lines[1] = {content_range_line};
    int local_line_count = 0;
    if (job->local_path && job->content_range[0]) {
        snprintf(content_range_line, sizeof(content_range_line), "content-range: %s", job->content_range);
        local_line_count = 1;
    }
    headers.set(job->local_path
        ? fetch_headers_map(local_lines, local_line_count, "content-type", content_type ? content_type : "application/octet-stream")
        : fetch_headers_map(job->response->response_headers, job->response->response_header_count, NULL, NULL));
    if (!headers.get()) return fetch_error(ERR_OUT_OF_MEMORY, job->url, "headers");
    static const char* names[] = {"status", "ok", "url", "headers", "body", "bytes"};
    static const TypeId types[] = {LMD_TYPE_INT, LMD_TYPE_BOOL, LMD_TYPE_STRING, LMD_TYPE_MAP,
        LMD_TYPE_ANY, LMD_TYPE_INT};
    result.set(map_alloc_for_type(runtime_result_shape(names, types, 6), NULL, 0));
    if (!result.get()) return fetch_error(ERR_OUT_OF_MEMORY, job->url, NULL);
    Item values[] = {(Item){.item = i2it((int64_t)status)}, (Item){.item = b2it(status >= 200 && status < 300)},
        url.get(), (Item){.map = headers.get()}, body.get(), (Item){.item = i2it(size)}};
    map_fill_items(result.get(), values, 6);
    return (Item){.map = result.get()};
}

// Runs the job while the task parks, then builds its result; a partial sink
// is removed unless the caller asked to resume it later.
static Item fetch_execute(FetchJob* job) {
    static const LambdaOffloadOps ops = {fetch_run, fetch_progress, fetch_release_orphan};
    Item outcome = lambda_task_offload(&ops, job, job->progress_ms);
    Item result = get_type_id(outcome) == LMD_TYPE_ERROR ? outcome : fetch_result(job);
    if (get_type_id(result) == LMD_TYPE_ERROR && job->part_path && !job->resume) remove(job->part_path);
    return result;
}

// Reads the options map into the job; `callback` receives the progress function.
static bool fetch_job_configure(FetchJob* job, Item options, Item* callback, Item* error) {
    IoFsOptionMap opts;
    if (!io_fs_options_open(options, "io.fetch", SYSPROC_IO_FETCH, &opts, error)) return false;
    job->timeout_seconds = 30;
    job->follow_redirects = true;
    job->verify_tls = true;
    job->progress_ms = 250;
    bool resume = false;
    if (!io_fs_option_text(&opts, "method", &job->method, error) ||
        !io_fs_option_text(&opts, "user_agent", &job->user_agent, error) ||
        !io_fs_option_count(&opts, "timeout", &job->timeout_seconds, error) ||
        !io_fs_option_count(&opts, "max_bytes", &job->max_bytes, error) ||
        !io_fs_option_count(&opts, "progress_ms", &job->progress_ms, error) ||
        !io_fs_option_bool(&opts, "follow_redirects", &job->follow_redirects, error) ||
        !io_fs_option_bool(&opts, "verify_tls", &job->verify_tls, error) ||
        !io_fs_option_bool(&opts, "resume", &resume, error)) return false;
    job->resume = resume;
    Item body = io_fs_option_get(&opts, "body");
    TypeId body_type = get_type_id(body);
    if (body_type == LMD_TYPE_BINARY) {
        Binary* bytes = body.get_safe_binary();
        job->body = fetch_dup(bytes ? (const char*)binary_data(bytes) : "", bytes ? binary_length(bytes) : 0);
        job->body_size = bytes ? binary_length(bytes) : 0;
    } else if (body_type != LMD_TYPE_NULL) {
        String* text = is_text_type_id(body_type) ? NULL : fn_string(body);
        const char* chars = is_text_type_id(body_type) ? body.get_chars() : (text ? text->chars : "");
        size_t length = is_text_type_id(body_type) ? body.get_len() : (text ? text->len : 0);
        job->body = fetch_dup(chars, length);
        job->body_size = length;
    }
    Item headers = io_fs_option_get(&opts, "headers");
    if (get_type_id(headers) == LMD_TYPE_MAP) {
        TypeMap* shape = lambda_attr_shape(LMD_TYPE_MAP, headers.map);
        int count = (int)fn_len(headers);
        job->header_lines = (char**)mem_calloc((size_t)count + 1, sizeof(char*), MEM_CAT_EVAL);
        if (!job->header_lines) return false;
        FOR_EACH_MAP_FIELD(shape, field) {
            if (!field->name || job->header_count >= count) continue;
            char* key = fetch_dup(field->name->str, field->name->length);
            bool found = false;
            Item value = key ? _map_get(shape, headers.map->data, key, &found) : ItemNull;
            if (!key || !is_text_type_id(get_type_id(value))) {
                mem_free(key);
                *error = io_fs_error(ERR_TYPE_MISMATCH, "io.fetch: header values must be text");
                return false;
            }
            size_t line_len = strlen(key) + 2 + value.get_len();
            char* line = (char*)mem_alloc(line_len + 1, MEM_CAT_EVAL);
            if (line) snprintf(line, line_len + 1, "%s: %.*s", key, (int)value.get_len(), value.get_chars());
            mem_free(key);
            if (!line) return false;
            job->header_lines[job->header_count++] = line;
        }
    } else if (get_type_id(headers) != LMD_TYPE_NULL) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.fetch: option 'headers' must be a map");
        return false;
    }
    Item to = io_fs_option_get(&opts, "to");
    if (get_type_id(to) != LMD_TYPE_NULL) {
        StrBuf* path = lambda_get_local_path_from_item(to);
        if (!path) {
            *error = io_fs_error(ERR_TYPE_MISMATCH, "io.fetch: option 'to' must be a local path");
            return false;
        }
        job->sink_path = fetch_dup(path->str, path->length);
        size_t part_len = path->length + 5;
        job->part_path = (char*)mem_alloc(part_len + 1, MEM_CAT_EVAL);
        if (job->part_path) snprintf(job->part_path, part_len + 1, "%s.part", path->str);
        strbuf_free(path);
        if (!job->sink_path || !job->part_path) return false;
        if (create_parent_dirs(job->part_path) != 0) {
            *error = io_fs_error(ERR_FILE_WRITE_ERROR, "io.fetch: cannot create the directory of %s", job->sink_path);
            return false;
        }
        if (job->resume) {
            int64_t present = file_size(job->part_path);
            job->resume_from = present > 0 ? present : 0;
        }
    }
    *callback = io_fs_option_get(&opts, "progress");
    TypeId callback_type = get_type_id(*callback);
    if (callback_type != LMD_TYPE_NULL && callback_type != LMD_TYPE_FUNC) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.fetch: option 'progress' must be a function");
        return false;
    }
    return true;
}

// Resolves the target into the job: a URL to fetch, or a local path to read.
static bool fetch_job_target(FetchJob* job, Item url, Item* error) {
    TypeId url_type = get_type_id(url);
    if (!is_text_type_id(url_type) && url_type != LMD_TYPE_PATH) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.fetch: target must be a string, symbol, or path, got %s",
            get_type_name(url_type));
        return false;
    }
    Url* cwd = context ? (Url*)context->cwd : NULL;
    Target* target = item_to_target(url.item, cwd);
    if (!target) {
        *error = io_fs_error(ERR_INVALID_OPERATION, "io.fetch: failed to resolve target");
        return false;
    }
    if (target_is_local(target)) {
        // fetch() accepts local targets; routing them through HTTP makes offline tests depend on DNS.
        StrBuf* path = (StrBuf*)target_to_local_path(target, cwd);
        target_free(target);
        if (!path || !path->str || path->length == 0) {
            if (path) strbuf_free(path);
            *error = io_fs_error(ERR_INVALID_OPERATION, "io.fetch: failed to resolve local target path");
            return false;
        }
        job->local_path = fetch_dup(path->str, path->length);
        // the result's url is a file URL, so relative references resolve against it
        size_t url_len = path->length + 8;
        job->url = (char*)mem_alloc(url_len + 1, MEM_CAT_EVAL);
        if (job->url) snprintf(job->url, url_len + 1, "file://%s%s", path->str[0] == '/' ? "" : "/", path->str);
        strbuf_free(path);
        MimeDetector* detector = mime_detector_init();
        const char* mime = detector ? detect_mime_from_filename(detector, job->local_path) : NULL;
        if (mime && strcmp(mime, "application/octet-stream") != 0) job->mime = fetch_dup(mime, strlen(mime));
        if (detector) mime_detector_destroy(detector);
        return job->local_path && job->url;
    }
    if (!target_is_remote(target)) {
        *error = io_fs_error(ERR_INVALID_OPERATION, "io.fetch: unsupported target scheme %d", target->scheme);
        target_free(target);
        return false;
    }
    StrBuf* url_buf = strbuf_new();
    const char* url_str = target_to_url_string(target, url_buf);
    target_free(target);
    if (!url_str || url_buf->length == 0) {
        strbuf_free(url_buf);
        *error = io_fs_error(ERR_INVALID_OPERATION, "io.fetch: failed to render remote target URL");
        return false;
    }
    job->url = fetch_dup(url_buf->str, url_buf->length);
    strbuf_free(url_buf);
    return job->url != NULL;
}

static Item dry_run_fabricated_fetch() {
    log_debug("dry-run: fabricated fetch() call");
    FetchJob* job = (FetchJob*)mem_calloc(1, sizeof(FetchJob), MEM_CAT_EVAL);
    if (!job) return ItemError;
    job->local_path = fetch_dup("dry-run", 7);
    job->url = fetch_dup("file:///dry-run", 15);
    job->mime = fetch_dup("application/json", 16);
    job->local_data = fetch_dup(DRY_RUN_HTTP_BODY, strlen(DRY_RUN_HTTP_BODY));
    job->local_size = (int64_t)strlen(DRY_RUN_HTTP_BODY);
    Item result = fetch_result(job);
    fetch_job_free(job);
    return result;
}

Item pn_fetch(Item url, Item options) {
    GUARD_ERROR2(url, options);
    if (g_dry_run) return dry_run_fabricated_fetch();
    log_debug("pn_fetch called");
    RootFrame roots(3);
    Rooted<Item> rooted_url(roots, url), rooted_options(roots, options);
    Rooted<Item> rooted_callback(roots, ItemNull);
    FetchJob* job = (FetchJob*)mem_calloc(1, sizeof(FetchJob), MEM_CAT_EVAL);
    if (!job) return io_fs_error(ERR_OUT_OF_MEMORY, "io.fetch: allocation failed");
    Item error = ItemError;
    Item callback = ItemNull;
    if (!fetch_job_target(job, url, &error) || !fetch_job_configure(job, options, &callback, &error)) {
        fetch_job_free(job);
        return error;
    }
    if (get_type_id(callback) == LMD_TYPE_FUNC) {
        // read through the root at each call: the heap may move the closure while the task parks
        rooted_callback.set(callback);
        job->callback = &rooted_callback;
    }
    Item result = fetch_execute(job);
    fetch_job_free(job);
    return result;
}

// io.copy's remote branch: the same transfer, straight to disk.
static Item fetch_to_file(const char* url, size_t url_len, const char* dst_path) {
    FetchJob* job = (FetchJob*)mem_calloc(1, sizeof(FetchJob), MEM_CAT_EVAL);
    if (!job) return io_fs_error(ERR_OUT_OF_MEMORY, "io.copy: allocation failed");
    job->url = fetch_dup(url, url_len);
    job->sink_path = fetch_dup(dst_path, strlen(dst_path));
    size_t part_len = strlen(dst_path) + 5;
    job->part_path = (char*)mem_alloc(part_len + 1, MEM_CAT_EVAL);
    if (job->part_path) snprintf(job->part_path, part_len + 1, "%s.part", dst_path);
    job->timeout_seconds = 30;
    job->follow_redirects = true;
    job->verify_tls = true;
    job->progress_ms = 250;
    Item result = (job->url && job->sink_path && job->part_path) ? fetch_execute(job)
        : io_fs_error(ERR_OUT_OF_MEMORY, "io.copy: allocation failed");
    fetch_job_free(job);
    return result;
}

// Helper function to escape shell arguments for safety
String* escape_shell_arg(String* arg) {
    if (!arg || arg->len == 0) {
#ifdef _WIN32
        return heap_strcpy("\"\"", 2);
#else
        return heap_strcpy("''", 2);  // Empty string as quoted empty
#endif
    }

    // Check if argument needs escaping (contains spaces, special chars, etc.)
    bool needs_quoting = false;
    for (int i = 0; i < (int)arg->len; i++) {
        char c = arg->chars[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '\'' ||
            c == '\\' || c == '|' || c == '&' || c == ';' || c == '(' || c == ')' ||
            c == '<' || c == '>' || c == '`' || c == '$' || c == '*' || c == '?' ||
            c == '[' || c == ']' || c == '{' || c == '}' || c == '~') {
            needs_quoting = true;
            break;
        }
    }

    if (!needs_quoting) {
        // Return original string if no escaping needed
        return arg;
    }

#ifdef _WIN32
    // Use caret escaping for cmd.exe so shell builtins like echo do not print quotes.
    size_t escaped_len = arg->len;
    for (int i = 0; i < (int)arg->len; i++) {
        char c = arg->chars[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '\'' ||
            c == '|' || c == '&' || c == ';' || c == '(' || c == ')' ||
            c == '<' || c == '>' || c == '^' || c == '*' || c == '?' ||
            c == '[' || c == ']' || c == '{' || c == '}') {
            escaped_len++;
        }
    }

    String* escaped = (String*)heap_alloc(sizeof(String) + escaped_len + 1, LMD_TYPE_STRING);
    escaped->len = escaped_len;
    escaped->flags = 0;
    escaped->is_ascii = 1;

    char* dst = escaped->chars;
    for (int i = 0; i < (int)arg->len; i++) {
        char c = arg->chars[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '"' || c == '\'' ||
            c == '|' || c == '&' || c == ';' || c == '(' || c == ')' ||
            c == '<' || c == '>' || c == '^' || c == '*' || c == '?' ||
            c == '[' || c == ']' || c == '{' || c == '}') {
            *dst++ = '^';
        }
        *dst++ = c;
    }
    *dst = '\0';

    return escaped;
#else
    size_t escaped_len = str_shell_quote_posix(NULL, 0, arg->chars, arg->len);

    String* escaped = (String*)heap_alloc(sizeof(String) + escaped_len + 1, LMD_TYPE_STRING);
    escaped->len = escaped_len;
    escaped->flags = 0;
    escaped->is_ascii = 1;  // shell escaping produces ASCII

    str_shell_quote_posix(escaped->chars, escaped_len + 1, arg->chars, arg->len);

    return escaped;
#endif
}

// Helper function to format arguments into command line string
String* format_cmd_args(String* cmd, Item args) {
    StrBuf* sb = strbuf_new();
    strbuf_append_str(sb, cmd->chars);

    TypeId args_type = get_type_id(args);

    if (args_type == LMD_TYPE_NULL) {
        // No arguments
    }
    else if (args_type == LMD_TYPE_ARRAY) {
        List* arg_list = args.array;
        for (int i = 0; i < arg_list->length; i++) {
            Item arg_item = array_item_read(arg_list, i);
            String* arg_str = fn_string(arg_item);
            if (arg_str && arg_str->len > 0) {
                String* escaped = escape_shell_arg(arg_str);
                strbuf_append_all(sb, 2, " ", escaped->chars);
            }
        }
    }
    else if (args_type == LMD_TYPE_MAP) {
        // Handle map-style arguments for named parameters
        Map* arg_map = args.map;
        TypeMap* type_map = (TypeMap*)arg_map->type;

        // Iterate through map entries using ShapeEntry linked list
        ShapeEntry* field = type_map->shape;
        for (int i = 0; i < type_map->length && field; i++) {
            if (!field->name || !field->name->str) {
                field = typemap_next_field(type_map, field);
                continue;
            }

            Item value_item = map_shape_field_to_item(arg_map->data, field);
            TypeId value_type = get_type_id(value_item);
            if (value_type != LMD_TYPE_NULL && value_type != LMD_TYPE_BOOL &&
                    value_type != LMD_TYPE_INT && value_type != LMD_TYPE_STRING &&
                    value_type != LMD_TYPE_SYMBOL) {
                value_item = ItemNull;
            }

            // format as --key=value or --key value depending on convention
            strbuf_append_str(sb, " --");
            strbuf_append_str_n(sb, field->name->str, field->name->length);

            String* value_str = fn_string(value_item);
            if (value_str && value_str->len > 0) {
                if (!(value_item._type_id == LMD_TYPE_BOOL && value_item.bool_val == true)) {
                    String* escaped = escape_shell_arg(value_str);
                    strbuf_append_all(sb, 2, "=", escaped->chars);
                }
                // else skip boolean true values (just add the flag)
            }

            field = typemap_next_field(type_map, field);
        }
    }
    else {
        // Single argument - convert to string
        String* arg_str = fn_string(args);
        if (arg_str && arg_str->len > 0) {
            String* escaped = escape_shell_arg(arg_str);
            strbuf_append_all(sb, 2, " ", escaped->chars);
        }
    }

    String* result = heap_strcpy(sb->str, sb->length);
    strbuf_free(sb);
    return result;
}

Item pn_cmd2(Item cmd, Item args);

Item pn_cmd1(Item cmd) {
    if (g_dry_run) return dry_run_fabricated_cmd();
    return pn_cmd2(cmd, ItemNull);
}

Item pn_cmd2(Item cmd, Item args) {
    if (g_dry_run) return dry_run_fabricated_cmd();
    log_debug("pn_cmd called");
    if (get_type_id(cmd) != LMD_TYPE_STRING) {
        log_debug("pn_cmd: command must be a string");
        return ItemError;
    }

    String* cmd_str = cmd.get_string();
    if (!cmd_str || cmd_str->len == 0) {
        log_debug("pn_cmd: command string is empty");
        return ItemError;
    }

    // Format the complete command with arguments
    String* full_cmd = format_cmd_args(cmd_str, args);
    if (!full_cmd) {
        log_debug("pn_cmd: failed to format command arguments");
        return ItemError;
    }

    log_debug("pn_cmd: executing command: %s", full_cmd->chars);

    // Use shell_exec_line to capture stdout
    ShellResult sr = shell_exec_line(full_cmd->chars, NULL);

    int actual_exit_code = sr.exit_code;
    log_debug("pn_cmd: command completed with exit code: %d", actual_exit_code);

    // Create result string from captured output
    String* result_str = NULL;
    Item result;
    if (sr.stdout_buf && sr.stdout_len > 0) {
        // trim trailing newline from command output
        size_t len = sr.stdout_len;
        while (len > 0 && (sr.stdout_buf[len - 1] == '\n' || sr.stdout_buf[len - 1] == '\r')) {
            len--;
        }
        result_str = heap_strcpy(sr.stdout_buf, len);
        result = {.item = s2it(result_str)};
    } else {
        // return empty string for no output
        result_str = heap_strcpy("", 0);
        result = {.item = s2it(result_str)};
    }

    shell_result_free(&sr);

    // non-zero exit code means the command failed — return error
    if (actual_exit_code != 0) {
        log_debug("pn_cmd: command failed with exit code %d", actual_exit_code);
        return ItemError;
    }

    log_debug("pn_cmd: command output: %s", result_str->chars);
    return result;
}

// ============================================================================
// I/O Module Functions (io module)
// These are procedural functions for unified I/O operations
// Supports both local file system and remote URLs
// ============================================================================

// Helper to extract local path from Item using unified Target API
// Accepts: Path, String, Symbol
// Returns StrBuf* that caller must free with strbuf_free()
// Returns NULL if item cannot be converted to local path (e.g., http:// URLs)
extern "C" StrBuf* lambda_get_local_path_from_item(Item item) {
    Target* target = item_to_target(item.item, NULL);
    if (!target) {
        return NULL;
    }

    // Check if local - some operations only work on local files
    if (!target_is_local(target)) {
        log_error("io: cannot perform operation on remote URL");
        target_free(target);
        return NULL;
    }

    StrBuf* path_buf = (StrBuf*)target_to_local_path(target, NULL);
    target_free(target);
    return path_buf;
}

// io.copy(src, dst) - Copy file or directory
// src can be local path or remote URL (http/https)
// dst must be a local path
Item pn_io_copy(Item src_item, Item dst_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.copy()"); return ItemNull; }
    // First, check if source is remote
    Target* src_target = item_to_target(src_item.item, NULL);
    if (!src_target) {
        log_error("io.copy: invalid source argument");
        return ItemError;
    }

    // Destination must always be local
    StrBuf* dst_buf = lambda_get_local_path_from_item(dst_item);
    if (!dst_buf) {
        log_error("io.copy: destination must be a local path");
        target_free(src_target);
        return ItemError;
    }

    const char* dst_path = dst_buf->str;

    // Handle remote source: the io.fetch transfer, streamed to disk (AR-E3v2)
    if (target_is_remote(src_target)) {
        log_debug("io.copy: fetching remote source to %s", dst_path);
        StrBuf* url_buf = strbuf_new();
        target_to_url_string(src_target, url_buf);
        target_free(src_target);
        if (create_parent_dirs(dst_path) != 0) {
            log_error("io.copy: failed to create parent directories for %s", dst_path);
            strbuf_free(url_buf);
            strbuf_free(dst_buf);
            return ItemError;
        }
        Item fetched = fetch_to_file(url_buf->str, url_buf->length, dst_path);
        strbuf_free(url_buf);
        strbuf_free(dst_buf);
        return get_type_id(fetched) == LMD_TYPE_ERROR ? fetched : ItemNull;
    }

    // Local source - get local path
    StrBuf* src_buf = (StrBuf*)target_to_local_path(src_target, NULL);
    target_free(src_target);

    if (!src_buf) {
        log_error("io.copy: invalid source path");
        strbuf_free(dst_buf);
        return ItemError;
    }

    const char* src_path = src_buf->str;

    log_debug("io.copy: %s -> %s", src_path, dst_path);

    FileCopyOptions opts = {true, true};
    int result = file_copy(src_path, dst_path, &opts);

    if (result != 0) {
        log_error("io.copy: failed to copy %s to %s", src_path, dst_path);
    }

    strbuf_free(src_buf);
    strbuf_free(dst_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.move(src, dst) - Move/rename file or directory
Item pn_io_move(Item src_item, Item dst_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.move()"); return ItemNull; }
    StrBuf* src_buf = lambda_get_local_path_from_item(src_item);
    StrBuf* dst_buf = lambda_get_local_path_from_item(dst_item);

    if (!src_buf || !dst_buf) {
        log_error("io.move: invalid path argument");
        if (src_buf) strbuf_free(src_buf);
        if (dst_buf) strbuf_free(dst_buf);
        return ItemError;
    }

    const char* src_path = src_buf->str;
    const char* dst_path = dst_buf->str;

    log_debug("io.move: %s -> %s", src_path, dst_path);

    int result = file_rename(src_path, dst_path);

    // If rename fails (cross-device), fall back to copy+delete
    if (result != 0 && errno == EXDEV) {
        log_debug("io.move: cross-device move, using copy+delete");
        Item copy_result = pn_io_copy(src_item, dst_item);
        if (item_is_error(copy_result)) {
            strbuf_free(src_buf);
            strbuf_free(dst_buf);
            return ItemError;
        }
        // Delete source after successful copy
        result = file_delete_recursive(src_path);
    }

    if (result != 0) {
        log_error("io.move: failed to move %s to %s", src_path, dst_path);
    }

    strbuf_free(src_buf);
    strbuf_free(dst_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.delete(path) - Delete file or directory
Item pn_io_delete(Item path_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.delete()"); return ItemNull; }
    StrBuf* path_buf = lambda_get_local_path_from_item(path_item);

    if (!path_buf) {
        log_error("io.delete: invalid path argument");
        return ItemError;
    }

    const char* path = path_buf->str;

    log_debug("io.delete: %s", path);

    if (!file_exists(path)) {
        log_error("io.delete: path does not exist: %s", path);
        strbuf_free(path_buf);
        return ItemError;
    }

    int result = file_delete_recursive(path);

    if (result != 0) {
        log_error("io.delete: failed to delete %s", path);
    }

    strbuf_free(path_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.mkdir(path) - Create directory (recursive)
Item pn_io_mkdir(Item path_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.mkdir()"); return ItemNull; }
    StrBuf* path_buf = lambda_get_local_path_from_item(path_item);

    if (!path_buf) {
        log_error("io.mkdir: invalid path argument");
        return ItemError;
    }

    const char* path = path_buf->str;

    log_debug("fs.mkdir: %s", path);

    bool ok = create_dir(path);

    if (!ok) {
        log_error("io.mkdir: failed to create directory %s", path);
    }

    strbuf_free(path_buf);

    return ok ? ItemNull : ItemError;
}

// io.touch(path) - Create file or update modification time
Item pn_io_touch(Item path_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.touch()"); return ItemNull; }
    StrBuf* path_buf = lambda_get_local_path_from_item(path_item);

    if (!path_buf) {
        log_error("io.touch: invalid path argument");
        return ItemError;
    }

    const char* path = path_buf->str;

    log_debug("fs.touch: %s", path);

    int result = file_touch(path);
    if (result != 0) {
        log_error("io.touch: failed to touch %s", path);
        strbuf_free(path_buf);
        return ItemError;
    }

    strbuf_free(path_buf);
    return ItemNull;
}

// io.symlink(target, link) - Create symbolic link
Item pn_io_symlink(Item target_item, Item link_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.symlink()"); return ItemNull; }
    StrBuf* target_buf = lambda_get_local_path_from_item(target_item);
    StrBuf* link_buf = lambda_get_local_path_from_item(link_item);

    if (!target_buf || !link_buf) {
        log_error("io.symlink: invalid path argument");
        if (target_buf) strbuf_free(target_buf);
        if (link_buf) strbuf_free(link_buf);
        return ItemError;
    }

    const char* target_path = target_buf->str;
    const char* link_path = link_buf->str;

    log_debug("fs.symlink: %s -> %s", link_path, target_path);

    int result = file_symlink(target_path, link_path);

    if (result != 0) {
        log_error("io.symlink: failed to create symlink %s -> %s",
                  link_path, target_path);
    }

    strbuf_free(target_buf);
    strbuf_free(link_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.chmod(path, mode) - Change file permissions
// mode can be string like "755" or int like 0755
Item pn_io_chmod(Item path_item, Item mode_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.chmod()"); return ItemNull; }
    StrBuf* path_buf = lambda_get_local_path_from_item(path_item);

    if (!path_buf) {
        log_error("io.chmod: invalid path argument");
        return ItemError;
    }

    const char* path = path_buf->str;

    // Parse mode
    int mode = 0;
    TypeId mode_type = get_type_id(mode_item);
    if (is_integer_type_id(mode_type)) {
        mode = (int)it2l(mode_item);
    } else if (mode_type == LMD_TYPE_STRING) {
        String* mode_str = mode_item.get_string();
        if (mode_str) {
            // Parse octal string like "755"
            mode = (int)strtol(mode_str->chars, NULL, 8);
        }
    } else {
        log_error("io.chmod: mode must be int or string");
        strbuf_free(path_buf);
        return ItemError;
    }

    log_debug("io.chmod: %s mode=%o", path, mode);

    int result = file_chmod(path, (uint16_t)mode);

    if (result != 0) {
        log_error("io.chmod: failed to change mode of %s", path);
    }

    strbuf_free(path_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.rename(old_path, new_path) - Rename file or directory (same as move but clearer intent)
Item pn_io_rename(Item old_item, Item new_item) {
    if (g_dry_run) { log_debug("dry-run: fabricated io.rename()"); return ItemNull; }
    StrBuf* old_buf = lambda_get_local_path_from_item(old_item);
    StrBuf* new_buf = lambda_get_local_path_from_item(new_item);

    if (!old_buf || !new_buf) {
        log_error("io.rename: invalid path argument");
        if (old_buf) strbuf_free(old_buf);
        if (new_buf) strbuf_free(new_buf);
        return ItemError;
    }

    const char* old_path = old_buf->str;
    const char* new_path = new_buf->str;

    log_debug("io.rename: %s -> %s", old_path, new_path);

    int result = file_rename(old_path, new_path);

    if (result != 0) {
        log_error("io.rename: failed to rename %s to %s", old_path, new_path);
    }

    strbuf_free(old_buf);
    strbuf_free(new_buf);

    return (result != 0) ? ItemError : ItemNull;
}

// io.fetch(target) - Fetch content from URL or local file (1-arg version)
Item pn_io_fetch1(Item target_item) {
    return pn_fetch(target_item, ItemNull);
}

// io.fetch(target, options) - Fetch content from URL or local file (2-arg version)
Item pn_io_fetch2(Item target_item, Item options_item) {
    return pn_fetch(target_item, options_item);
}
