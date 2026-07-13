/* Radiant Event/State Log — Phase 1
 *
 * Structured JSON Lines log of input events, FSM transitions, layout/render
 * stats, and end-of-cascade state snapshots, separate from the human-readable
 * log.txt channel.
 *
 * Design: vibe/radiant/Radiant_Design_State_Machine.md
 *
 * Output: ./temp/events_${pid}_${doc_name}.jsonl
 *   - one file per document (so iframes / multi-session runs do not interleave)
 *   - per-document `session_start` record carries the document URL and metadata;
 *     subsequent records do not repeat the URL
 *   - JSONL: one JSON object per line, append-only, streamable
 *
 * Logging is disabled by default; opened only when an explicit CLI flag
 * (e.g. --event-log) calls event_state_log_open().
 *
 * This module writes through a per-document lib/log.c category using
 * clog_raw(), so JSON Lines records are not decorated with timestamp/level
 * prefixes and are still available in release builds.
 */

#ifndef RADIANT_EVENT_STATE_LOG_HPP
#define RADIANT_EVENT_STATE_LOG_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* JSON writer — minimal, fixed-buffer, no allocation.                */
/* Builds one JSON object/array into a caller-provided buffer.        */
/* ------------------------------------------------------------------ */

#define EVENT_LOG_JSON_MAX_DEPTH 16

typedef struct JsonWriter {
    char*  buf;
    size_t cap;
    size_t pos;
    int    depth;
    /* one bit per nesting level: 1 = next entry needs leading comma */
    uint32_t need_comma_stack;
    bool   overflow;
} JsonWriter;

void jw_init(JsonWriter* w, char* buf, size_t cap);

void jw_obj_begin(JsonWriter* w);
void jw_obj_end(JsonWriter* w);
void jw_arr_begin(JsonWriter* w);
void jw_arr_end(JsonWriter* w);

void jw_key(JsonWriter* w, const char* key);

void jw_str(JsonWriter* w, const char* val);
void jw_int(JsonWriter* w, int64_t val);
void jw_uint(JsonWriter* w, uint64_t val);
void jw_double(JsonWriter* w, double val);
void jw_bool(JsonWriter* w, bool val);
void jw_null(JsonWriter* w);

/* convenience kv pairs */
void jw_kv_str(JsonWriter* w, const char* key, const char* val);
void jw_kv_int(JsonWriter* w, const char* key, int64_t val);
void jw_kv_uint(JsonWriter* w, const char* key, uint64_t val);
void jw_kv_double(JsonWriter* w, const char* key, double val);
void jw_kv_bool(JsonWriter* w, const char* key, bool val);

/* returns the buffer pointer if no overflow, NULL otherwise.
 * does not append a newline; caller can add one when emitting. */
const char* jw_finish(JsonWriter* w);

/* ------------------------------------------------------------------ */
/* Event/state log handle — one per document.                          */
/* ------------------------------------------------------------------ */

typedef struct EventStateLog EventStateLog;
struct DomNode;
struct EditingSurface;

/* Open a per-document log file. doc_name is sanitized into the path:
 *   ./temp/events_${pid}_${sanitized_doc_name}.jsonl
 * doc_url is recorded once in the session_start record.
 *
 * Returns NULL if the file cannot be opened. The caller owns the handle
 * and must call event_state_log_close().
 */
EventStateLog* event_state_log_open(const char* doc_name, const char* doc_url);

void event_state_log_close(EventStateLog* log);

bool event_state_log_enabled(EventStateLog* log);

/* Returns the absolute or workspace-relative path of the open log file,
 * or NULL if not open. Pointer is valid until close. */
const char* event_state_log_path(EventStateLog* log);

/* Returns the stable per-log document id used in record envelopes. */
const char* event_state_log_doc_id(EventStateLog* log);

/* ------------------------------------------------------------------ */
/* Cascade lifecycle.                                                  */
/* A cascade is the unit of work triggered by one external cause       */
/* (input event, navigation, timer, etc.).                             */
/* ------------------------------------------------------------------ */

/* cause: "input", "webdriver", "event_sim", "navigation", "timer",
 *        "script", "internal" */
uint64_t event_state_log_begin_cascade(EventStateLog* log, const char* cause);

/* Emits no record itself; pair with end-of-cascade snapshot/stats. */
void event_state_log_end_cascade(EventStateLog* log, uint64_t cascade_id);

/* ------------------------------------------------------------------ */
/* Emission primitives.                                                */
/* ------------------------------------------------------------------ */

/* Begin a record. Initializes `w` over the caller-provided `buf`/`cap`,
 * opens the top-level object, and writes envelope fields:
 *   v, seq, time, doc, cascade, type.
 * After return, the writer is positioned for additional top-level keys
 * (typically "data"). Caller must close the object via
 * event_state_log_finish_record().
 *
 * cascade_id may be 0 to omit the cascade field (e.g. for session_start).
 */
void event_state_log_begin_record(EventStateLog* log, JsonWriter* w,
                                   char* buf, size_t cap,
                                   const char* type, uint64_t cascade_id);

/* Closes the top-level object and writes the line to the file. */
void event_state_log_finish_record(EventStateLog* log, JsonWriter* w);

/* Emit an already-fully-formed JSON object string as one line.
 * Useful for tests and for code paths that want full control. */
void event_state_log_emit_raw(EventStateLog* log, const char* json_line);

/* Serialize a layered node reference:
 *   { "id": N, "stable_id": "...", "path": "...", "source": { ... }, ... }
 * The document URL is intentionally omitted from source; it belongs to
 * session_start. If key is non-null, writes it as an object property;
 * otherwise writes the object as the next array/value item.
 */
void event_state_log_write_node_ref(JsonWriter* w, const char* key,
                                    const struct DomNode* node);

void editing_log_write_surface_core_fields(JsonWriter* w,
                                           const struct EditingSurface* surface,
                                           bool include_state_flags);

/* ------------------------------------------------------------------ */
/* Convenience record helpers.                                         */
/* These are thin wrappers over begin_record/finish_record for the     */
/* most common record types defined in the design doc.                 */
/* ------------------------------------------------------------------ */

/* Emit the per-document start metadata once after opening the log. */
void event_state_log_session_start(EventStateLog* log,
                                    int viewport_w, int viewport_h,
                                    double zoom);

void event_state_log_session_end(EventStateLog* log);

/* document.* family */
void event_state_log_document(EventStateLog* log, const char* sub_type /* e.g. "load_start" */);

/* logger.warning record (e.g. dropped record, buffer overflow) */
void event_state_log_warning(EventStateLog* log, const char* code, const char* message);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RADIANT_EVENT_STATE_LOG_HPP */
