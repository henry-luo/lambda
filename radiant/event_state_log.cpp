/* Radiant Event/State Log — Phase 1 implementation.
 *
 * See radiant/event_state_log.hpp and
 * vibe/radiant/Radiant_Design_State_Machine.md.
 */

#include "event.hpp"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lambda/input/css/dom_element.hpp"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
  #include <direct.h>
  #include <process.h>
  #define getpid _getpid
#else
  #include <unistd.h>
#endif

/* ================================================================== */
/* JSON writer                                                         */
/* ================================================================== */

static inline void jw_putc(JsonWriter* w, char c) {
    if (w->overflow) return;
    if (w->pos + 1 >= w->cap) { w->overflow = true; return; }
    w->buf[w->pos++] = c;
}

static inline void jw_puts(JsonWriter* w, const char* s) {
    if (w->overflow || !s) return;
    size_t n = strlen(s);
    if (w->pos + n + 1 > w->cap) { w->overflow = true; return; }
    memcpy(w->buf + w->pos, s, n);
    w->pos += n;
}

/* Bit i of need_comma_stack tracks whether the container at depth i+1
 * already has at least one element. */
static inline bool jw_need_comma(const JsonWriter* w) {
    if (w->depth <= 0) return false;
    return (w->need_comma_stack & (1u << (w->depth - 1))) != 0;
}

static inline void jw_set_need_comma(JsonWriter* w, bool v) {
    if (w->depth <= 0) return;
    uint32_t bit = 1u << (w->depth - 1);
    if (v)  w->need_comma_stack |= bit;
    else    w->need_comma_stack &= ~bit;
}

static inline void jw_before_value(JsonWriter* w) {
    if (jw_need_comma(w)) jw_putc(w, ',');
    jw_set_need_comma(w, true);
}

void jw_init(JsonWriter* w, char* buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->pos = 0;
    w->depth = 0;
    w->need_comma_stack = 0;
    w->overflow = (cap == 0);
    if (cap > 0) buf[0] = '\0';
}

static void jw_open(JsonWriter* w, char open_char) {
    jw_before_value(w);
    jw_putc(w, open_char);
    if (w->depth >= EVENT_LOG_JSON_MAX_DEPTH) { w->overflow = true; return; }
    w->depth++;
    jw_set_need_comma(w, false);
}

static void jw_close(JsonWriter* w, char close_char) {
    if (w->depth <= 0) { w->overflow = true; return; }
    jw_putc(w, close_char);
    w->depth--;
}

void jw_obj_begin(JsonWriter* w) { jw_open(w, '{'); }
void jw_obj_end(JsonWriter* w)   { jw_close(w, '}'); }
void jw_arr_begin(JsonWriter* w) { jw_open(w, '['); }
void jw_arr_end(JsonWriter* w)   { jw_close(w, ']'); }

static void jw_write_escaped_str(JsonWriter* w, const char* s) {
    jw_putc(w, '"');
    if (s) {
        for (const unsigned char* p = (const unsigned char*)s; *p && !w->overflow; p++) {
            unsigned char c = *p;
            switch (c) {
                case '"':  jw_puts(w, "\\\""); break;
                case '\\': jw_puts(w, "\\\\"); break;
                case '\b': jw_puts(w, "\\b");  break;
                case '\f': jw_puts(w, "\\f");  break;
                case '\n': jw_puts(w, "\\n");  break;
                case '\r': jw_puts(w, "\\r");  break;
                case '\t': jw_puts(w, "\\t");  break;
                default:
                    if (c < 0x20) {
                        char esc[8];
                        snprintf(esc, sizeof(esc), "\\u%04x", c);
                        jw_puts(w, esc);
                    } else {
                        jw_putc(w, (char)c);
                    }
                    break;
            }
        }
    }
    jw_putc(w, '"');
}

void jw_key(JsonWriter* w, const char* key) {
    jw_before_value(w);
    jw_write_escaped_str(w, key);
    jw_putc(w, ':');
    /* a key consumed the comma slot for this position; the value that
     * follows is emitted with jw_*-value, which calls jw_before_value()
     * again. To avoid emitting a second comma between key and value,
     * clear the need_comma flag — the next value should not prepend a
     * comma. The subsequent sibling key/value pair will be a new
     * "next entry" and will need a comma, which the value emission
     * sets via jw_set_need_comma(true). */
    jw_set_need_comma(w, false);
}

void jw_str(JsonWriter* w, const char* val) {
    jw_before_value(w);
    if (val) jw_write_escaped_str(w, val);
    else     jw_puts(w, "null");
}

void jw_int(JsonWriter* w, int64_t val) {
    jw_before_value(w);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%" PRId64, val);
    jw_puts(w, tmp);
}

void jw_uint(JsonWriter* w, uint64_t val) {
    jw_before_value(w);
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%" PRIu64, val);
    jw_puts(w, tmp);
}

void jw_double(JsonWriter* w, double val) {
    jw_before_value(w);
    if (!isfinite(val)) {
        /* JSON has no NaN/Infinity; emit null for safety. */
        jw_puts(w, "null");
        return;
    }
    char tmp[40];
    /* %.17g preserves round-trip precision for IEEE-754 doubles. */
    snprintf(tmp, sizeof(tmp), "%.17g", val);
    jw_puts(w, tmp);
}

void jw_bool(JsonWriter* w, bool val) {
    jw_before_value(w);
    jw_puts(w, val ? "true" : "false");
}

void jw_null(JsonWriter* w) {
    jw_before_value(w);
    jw_puts(w, "null");
}

void jw_kv_str(JsonWriter* w, const char* key, const char* val) {
    jw_key(w, key); jw_str(w, val);
}
void jw_kv_int(JsonWriter* w, const char* key, int64_t val) {
    jw_key(w, key); jw_int(w, val);
}
void jw_kv_uint(JsonWriter* w, const char* key, uint64_t val) {
    jw_key(w, key); jw_uint(w, val);
}
void jw_kv_double(JsonWriter* w, const char* key, double val) {
    jw_key(w, key); jw_double(w, val);
}
void jw_kv_bool(JsonWriter* w, const char* key, bool val) {
    jw_key(w, key); jw_bool(w, val);
}

const char* jw_finish(JsonWriter* w) {
    if (w->overflow || w->depth != 0) return NULL;
    if (w->pos >= w->cap) return NULL;
    w->buf[w->pos] = '\0';
    return w->buf;
}

/* ================================================================== */
/* EventStateLog                                                       */
/* ================================================================== */

#define EVENT_LOG_RECORD_BUFSZ 4096
#define EVENT_LOG_PATH_BUFSZ   512
#define EVENT_LOG_DOC_ID_BUFSZ 64

struct EventStateLog {
    FILE*    out;
    log_category_t* category;
    char     path[EVENT_LOG_PATH_BUFSZ];
    char     doc_id[EVENT_LOG_DOC_ID_BUFSZ];
    char*    doc_url;        /* mem_alloc'd; logged once in session_start */
    uint64_t seq;
    uint64_t cascade_seq;
    uint64_t mono_start_ns;  /* monotonic clock origin for this session */
    int      pid;
    bool     enabled;

    bool init(const char* doc_name, const char* doc_url);
    void destroy();
};

/* dropped-record diagnostic counter (per process). Surfaced via
 * a logger.warning record on next successful write. */
static uint64_t g_event_log_dropped = 0;

/* ------------------------------ utils ------------------------------ */

static void make_temp_dir(void) {
    /* best-effort mkdir; ignore EEXIST. */
#if defined(_WIN32) && !defined(__MINGW32__)
    _mkdir("temp");
#elif defined(_WIN32)
    mkdir("temp");
#else
    mkdir("temp", 0755);
#endif
}

/* Sanitize a doc_name into a filesystem-safe slug.
 * Strips directory prefix, keeps [A-Za-z0-9._-], replaces others with '_'.
 * Truncates to a reasonable length. */
static void sanitize_doc_name(const char* in, char* out, size_t out_sz) {
    if (out_sz == 0) return;
    out[0] = '\0';
    if (!in || !*in) { snprintf(out, out_sz, "doc"); return; }

    /* basename: take after last '/' or '\\' */
    const char* base = in;
    for (const char* p = in; *p; p++) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }

    size_t i = 0;
    for (const char* p = base; *p && i + 1 < out_sz && i < 64; p++) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') {
            out[i++] = c;
        } else {
            out[i++] = '_';
        }
    }
    if (i == 0) i = snprintf(out, out_sz, "doc");
    else        out[i] = '\0';
}

static uint64_t now_mono_ns(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    }
#endif
    /* fallback: not monotonic, but good enough for relative timing. */
    return (uint64_t)time(NULL) * 1000000000ull;
}

static uint64_t now_wall_ms(void) {
    struct timespec ts;
#if defined(CLOCK_REALTIME)
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
    }
#endif
    return (uint64_t)time(NULL) * 1000ull;
}

/* Open a record envelope and emit common fields. cascade_id == 0 omits
 * the cascade field. */
static void write_envelope(EventStateLog* log, JsonWriter* w,
                            const char* type, uint64_t cascade_id) {
    uint64_t mono_now = now_mono_ns();
    double mono_ms = (double)(mono_now - log->mono_start_ns) / 1.0e6;
    uint64_t wall_ms = now_wall_ms();

    jw_obj_begin(w);
    jw_kv_int(w, "v", 1);
    jw_kv_uint(w, "seq", log->seq++);
    jw_key(w, "time");
    jw_obj_begin(w);
        jw_kv_double(w, "mono_ms", mono_ms);
        jw_kv_uint(w, "wall_ms", wall_ms);
    jw_obj_end(w);
    jw_kv_str(w, "doc", log->doc_id);
    if (cascade_id != 0) {
        jw_key(w, "cascade");
        jw_obj_begin(w);
            jw_kv_uint(w, "id", cascade_id);
        jw_obj_end(w);
    }
    jw_kv_str(w, "type", type);
}

/* ----------------------------- public ------------------------------ */

bool EventStateLog::init(const char* doc_name, const char* doc_url) {
    make_temp_dir();

    pid = (int)getpid();
    mono_start_ns = now_mono_ns();
    seq = 0;
    cascade_seq = 0;

    char slug[96];
    sanitize_doc_name(doc_name, slug, sizeof(slug));

    snprintf(path, sizeof(path),
             "./temp/events_%d_%s.jsonl", pid, slug);

    /* truncate on open: each session/document starts fresh. */
    out = fopen(path, "w");
    if (!out) {
        log_error("event_state_log: failed to open %s: %s",
                   path, strerror(errno));
        return false;
    }

    snprintf(doc_id, sizeof(doc_id), "%s", slug);

    char category_name[64];
    snprintf(category_name, sizeof(category_name), "event_state.%s", slug);
    category = log_get_category(category_name);
    if (!category || strcmp(category->name, category_name) != 0) {
        log_error("event_state_log: failed to create log category %s", category_name);
        fclose(out);
        out = NULL;
        return false;
    }
    category->enabled = 1;
    category->level = LOG_LEVEL_DEBUG;
    category->output = out;
    strncpy(category->output_filename, path,
            sizeof(category->output_filename) - 1);
    category->output_filename[sizeof(category->output_filename) - 1] = '\0';

    if (doc_url && *doc_url) {
        size_t n = strlen(doc_url);
        this->doc_url = (char*)mem_alloc(n + 1, MEM_CAT_SYSTEM);
        if (this->doc_url) memcpy(this->doc_url, doc_url, n + 1);
    }

    enabled = true;

    log_info("event_state_log: opened %s (doc_id=%s)", path, doc_id);
    return true;
}

EventStateLog* event_state_log_open(const char* doc_name, const char* doc_url) {
    EventStateLog* log = (EventStateLog*)mem_calloc(1, sizeof(EventStateLog), MEM_CAT_SYSTEM);
    if (!log) return NULL;

    if (!log->init(doc_name, doc_url)) {
        mem_free(log);
        return NULL;
    }
    return log;
}

void event_state_log_close(EventStateLog* log) {
    if (!log) return;
    log->destroy();
    mem_free(log);
}

void EventStateLog::destroy() {
    if (enabled) event_state_log_session_end(this);
    if (category) {
        category->enabled = 0;
        category->output = NULL;
        category->output_filename[0] = '\0';
    }
    if (out) {
        fflush(out);
        fclose(out);
        out = NULL;
    }
    mem_free(doc_url);
    doc_url = NULL;
    enabled = false;
}

bool event_state_log_enabled(EventStateLog* log) {
    return log && log->enabled && log->out && log->category;
}

const char* event_state_log_path(EventStateLog* log) {
    return (log && log->path[0]) ? log->path : NULL;
}

const char* event_state_log_doc_id(EventStateLog* log) {
    return event_state_log_enabled(log) ? log->doc_id : NULL;
}

uint64_t event_state_log_begin_cascade(EventStateLog* log, const char* cause) {
    if (!event_state_log_enabled(log)) return 0;
    uint64_t id = ++log->cascade_seq;

    char buf[EVENT_LOG_RECORD_BUFSZ];
    JsonWriter w;
    event_state_log_begin_record(log, &w, buf, sizeof(buf),
                                  "cascade.begin", id);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        jw_kv_str(&w, "cause", cause ? cause : "internal");
    jw_obj_end(&w);
    event_state_log_finish_record(log, &w);
    return id;
}

void event_state_log_end_cascade(EventStateLog* log, uint64_t cascade_id) {
    if (!event_state_log_enabled(log) || cascade_id == 0) return;

    char buf[EVENT_LOG_RECORD_BUFSZ];
    JsonWriter w;
    event_state_log_begin_record(log, &w, buf, sizeof(buf),
                                  "cascade.end", cascade_id);
    event_state_log_finish_record(log, &w);
}

void event_state_log_begin_record(EventStateLog* log, JsonWriter* w,
                                   char* buf, size_t cap,
                                   const char* type, uint64_t cascade_id) {
    jw_init(w, buf, cap);
    if (!event_state_log_enabled(log)) return;
    write_envelope(log, w, type ? type : "unknown", cascade_id);
}

void event_state_log_finish_record(EventStateLog* log, JsonWriter* w) {
    if (!event_state_log_enabled(log)) return;
    jw_obj_end(w);
    const char* line = jw_finish(w);
    if (!line) {
        g_event_log_dropped++;
        return;
    }
    if (clog_raw(log->category, line) != LOG_OK) {
        g_event_log_dropped++;
        return;
    }
}

void event_state_log_emit_raw(EventStateLog* log, const char* json_line) {
    if (!event_state_log_enabled(log) || !json_line) return;
    if (clog_raw(log->category, json_line) != LOG_OK) g_event_log_dropped++;
}

static void build_node_path(const DomNode* node, char* buf, size_t buf_sz) {
    if (!buf || buf_sz == 0) return;
    buf[0] = '\0';
    if (!node) return;

    const DomNode* chain[64];
    int depth = 0;
    const DomNode* cur = node;
    while (cur && depth < 64) {
        chain[depth++] = cur;
        cur = cur->parent;
    }

    size_t pos = 0;
    for (int i = depth - 1; i >= 0; i--) {
        const DomNode* n = chain[i];
        const char* name = n->node_name();
        if (!name || !name[0]) name = "node";

        int sibling_index = 0;
        if (n->parent) {
            const DomNode* scan = n->parent->as_element()
                ? n->parent->as_element()->first_child : nullptr;
            while (scan && scan != n) {
                if (strcmp(scan->node_name(), name) == 0) sibling_index++;
                scan = scan->next_sibling;
            }
        }

        int written = snprintf(buf + pos, pos < buf_sz ? buf_sz - pos : 0,
            "%s%s.%d", pos == 0 ? "" : ".", name, sibling_index);
        if (written < 0) break;
        if ((size_t)written >= (pos < buf_sz ? buf_sz - pos : 0)) {
            pos = buf_sz - 1;
            break;
        }
        pos += (size_t)written;
    }
    buf[buf_sz - 1] = '\0';
}

static void build_stable_id(const DomNode* node, const char* path,
                            char* buf, size_t buf_sz) {
    if (!buf || buf_sz == 0) return;
    buf[0] = '\0';
    if (!node) return;

    const DomElement* el = node->as_element();
    if (el && el->id && el->id[0]) {
        snprintf(buf, buf_sz, "id:%s", el->id);
    } else if (node->source_line > 0) {
        snprintf(buf, buf_sz, "src:line=%d:%s", node->source_line, node->node_name());
    } else if (path && path[0]) {
        snprintf(buf, buf_sz, "path:%s", path);
    } else {
        snprintf(buf, buf_sz, "node:%u", node->id);
    }
}

void event_state_log_write_node_ref(JsonWriter* w, const char* key,
                                    const DomNode* node) {
    if (key) jw_key(w, key);
    if (!node) {
        jw_null(w);
        return;
    }

    char path[256];
    char stable_id[320];
    build_node_path(node, path, sizeof(path));
    build_stable_id(node, path, stable_id, sizeof(stable_id));

    jw_obj_begin(w);
        jw_kv_uint(w, "id", node->id);
        jw_kv_str(w, "stable_id", stable_id);
        if (path[0]) jw_kv_str(w, "path", path);
        jw_kv_str(w, "tag", node->node_name());
        if (node->source_line > 0) {
            jw_key(w, "source");
            jw_obj_begin(w);
                jw_kv_int(w, "line", node->source_line);
            jw_obj_end(w);
        }

        const DomElement* el = node->as_element();
        if (el && el->id && el->id[0]) jw_kv_str(w, "author_id", el->id);
        if (el && el->class_count > 0) {
            jw_key(w, "classes");
            jw_arr_begin(w);
            for (int i = 0; i < el->class_count; i++) {
                jw_str(w, el->class_names[i]);
            }
            jw_arr_end(w);
    }
    jw_obj_end(w);
}

void editing_log_write_surface_core_fields(JsonWriter* w,
                                           const EditingSurface* surface,
                                           bool include_state_flags) {
    jw_kv_str(w, "kind", editing_surface_kind_name(
        surface ? surface->kind : EDIT_SURFACE_NONE));
    jw_kv_str(w, "mode", editing_mode_name(
        surface ? surface->mode : EDIT_MODE_RICH));
    if (include_state_flags) {
        jw_kv_bool(w, "readonly", surface ? surface->readonly : false);
        jw_kv_bool(w, "disabled", surface ? surface->disabled : false);
        jw_kv_bool(w, "target_in_false_island",
                   surface ? surface->target_in_false_island : false);
    }
    event_state_log_write_node_ref(w, "owner",
        surface ? (const DomNode*)surface->owner : NULL);
    event_state_log_write_node_ref(w, "target",
        surface ? (const DomNode*)surface->view : NULL);
}

void event_state_log_session_start(EventStateLog* log,
                                    int viewport_w, int viewport_h,
                                    double zoom) {
    if (!event_state_log_enabled(log)) return;

    char buf[EVENT_LOG_RECORD_BUFSZ];
    JsonWriter w;
    /* session_start uses no cascade. */
    event_state_log_begin_record(log, &w, buf, sizeof(buf),
                                  "session_start", 0);
    jw_kv_int(&w, "pid", log->pid);
    jw_key(&w, "document");
    jw_obj_begin(&w);
        jw_kv_str(&w, "id", log->doc_id);
        if (log->doc_url) jw_kv_str(&w, "url", log->doc_url);
        else              jw_key(&w, "url"), jw_null(&w);
    jw_obj_end(&w);
    jw_key(&w, "viewport");
    jw_obj_begin(&w);
        jw_kv_int(&w, "w", viewport_w);
        jw_kv_int(&w, "h", viewport_h);
    jw_obj_end(&w);
    jw_kv_double(&w, "zoom", zoom);
    event_state_log_finish_record(log, &w);
}

void event_state_log_session_end(EventStateLog* log) {
    if (!event_state_log_enabled(log)) return;

    char buf[EVENT_LOG_RECORD_BUFSZ];
    JsonWriter w;
    event_state_log_begin_record(log, &w, buf, sizeof(buf),
                                  "session_end", 0);
    if (g_event_log_dropped > 0) {
        jw_key(&w, "data");
        jw_obj_begin(&w);
            jw_kv_uint(&w, "dropped", g_event_log_dropped);
        jw_obj_end(&w);
    }
    event_state_log_finish_record(log, &w);
}

void event_state_log_document(EventStateLog* log, const char* sub_type) {
    if (!event_state_log_enabled(log) || !sub_type) return;

    char type[64];
    snprintf(type, sizeof(type), "document.%s", sub_type);

    char buf[EVENT_LOG_RECORD_BUFSZ];
    JsonWriter w;
    event_state_log_begin_record(log, &w, buf, sizeof(buf), type, 0);
    event_state_log_finish_record(log, &w);
}

void event_state_log_warning(EventStateLog* log, const char* code,
                              const char* message) {
    if (!event_state_log_enabled(log)) return;

    char buf[EVENT_LOG_RECORD_BUFSZ];
    JsonWriter w;
    event_state_log_begin_record(log, &w, buf, sizeof(buf),
                                  "logger.warning", 0);
    jw_key(&w, "data");
    jw_obj_begin(&w);
        if (code)    jw_kv_str(&w, "code", code);
        if (message) jw_kv_str(&w, "message", message);
    jw_obj_end(&w);
    event_state_log_finish_record(log, &w);
}
