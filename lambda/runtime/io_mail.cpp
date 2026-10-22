// io_mail.cpp — lambda.io.mail: open, command, idle and send (D7.2.4v4;
// vibe/Lambda_Demo_Mail.md §5.3, ML1–ML4, ML9–ML11).
//
// curl connects, secures and logs in (ML1). For IMAP it stops there
// (CURLOPT_CONNECT_ONLY) and this file speaks the protocol over the
// authenticated connection: one command in flight, framing and parsing by
// lib/mail_imap, and socket waits on the event loop, so a long IDLE holds no
// pool thread. POP3 commands and SMTP sends are ordinary curl transfers on the
// offload pool (ML9).
//
// A session lives until the task that opened it ends: the S12.4 resource
// model (auto-close at block end) is not implemented yet (ML3).

#include "io_file_search.hpp"
#include "concurrency.h"
#include "lambda-number-runtime.hpp"
#include "re2_wrapper.hpp"
#include "transpiler.hpp"
#include "../network/curl_trust.h"
#include "../../lib/file.h"
#include "../../lib/log.h"
#include "../../lib/mail_imap.h"
#include "../../lib/memtrack.h"
#include "../../lib/strbuf.h"
#include "../../lib/time_util.h"
#include "../../lib/uv_loop.h"

#include <curl/curl.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <uv.h>

extern __thread EvalContext* context;
extern "C" StrBuf* lambda_get_local_path_from_item(Item item);
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);

#define MAIL_DEFAULT_TIMEOUT_S 60
#define MAIL_SPILL_THRESHOLD ((size_t)64 * 1024)
#define MAIL_IDLE_MAX_S 1740            // under the 30-minute limit of RFC 2177 servers
#define MAIL_RECV_CHUNK 16384
#define MAIL_ARG_DEPTH 16

enum MailProtocol { MAIL_IMAP, MAIL_POP3, MAIL_SMTP };

struct MailTarget {
    MailProtocol protocol;
    bool implicit_tls;
    bool loopback;
    char* url;                  // as curl takes it: scheme://authority/
};

// Options shared by open and send (mem-owned).
struct MailAuth {
    char* user;
    char* password;
    char* token;                // an OAuth access token (OAUTHBEARER / XOAUTH2)
    char* pin;                  // a public-key pin for a self-signed local server
    int64_t timeout_s;
    bool tls;
};

// One curl transfer on the offload pool: open's connect and login, a POP3
// command, an SMTP send. The worker sees only this record (D5.3.3).
struct MailTransfer {
    CURL* curl;
    bool owns_curl;             // open's handle, until the session takes it
    CURLcode code;
    const int* cancel;
    StrBuf* lines;              // the server's reply lines
    StrBuf* body;               // a POP3 payload held in memory ...
    FILE* file;                 // ... or streamed here once past the threshold
    char* path;
    const char* spill_dir;
    bool spill_failed;
    const char* upload;         // an SMTP message
    size_t upload_length;
    size_t upload_pos;
    char errbuf[CURL_ERROR_SIZE];
};

struct MailSession {
    MailProtocol protocol;
    CURL* curl;
    curl_socket_t socket;
    LambdaSocketWaiter* waiter;
    ImapReader* reader;
    ImapParser* parser;
    char* url;
    int64_t timeout_ms;
    uint32_t next_tag;
    char drain_tag[16];         // a command abandoned mid-flight: read its rest first
    bool idle_pending;          // an IDLE abandoned mid-flight: send DONE first
    size_t nonsync_limit;       // LITERAL+ / LITERAL- (RFC 7888)
    bool utf8;                  // UTF8=ACCEPT enabled (RFC 6855)
    bool busy;
    bool closed;
    bool broken;                // the connection state is unknown: refuse further use
    bool release_when_idle;     // the owner ended mid-operation: free when it ends
    Item handle;                // the session's VMap, rooted while open
    bool handle_rooted;
    char errbuf[CURL_ERROR_SIZE];
};

static Item mail_error(LambdaErrorCode code, const char* fn, const char* fmt, ...) {
    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    return io_fs_error(code, "%s: %s", fn, message);
}

static bool has_line_break(const char* s) {
    return s && (strchr(s, '\r') || strchr(s, '\n'));
}

// ── targets and options ────────────────────────────────────────────────

static void mail_target_free(MailTarget* target) {
    mem_free(target->url);
    target->url = NULL;
}

static bool mail_parse_target(const char* fn, Item url, bool for_send, MailTarget* out, Item* error) {
    static const struct { const char* name; MailProtocol protocol; bool tls; } schemes[] = {
        {"imaps", MAIL_IMAP, true}, {"imap", MAIL_IMAP, false},
        {"pop3s", MAIL_POP3, true}, {"pop3", MAIL_POP3, false},
        {"smtps", MAIL_SMTP, true}, {"smtp", MAIL_SMTP, false},
    };
    memset(out, 0, sizeof(*out));
    if (!is_text_type_id(get_type_id(url))) {
        *error = mail_error(ERR_TYPE_MISMATCH, fn, "the url must be text");
        return false;
    }
    const char* s = url.get_chars();
    size_t n = url.get_len();
    size_t scheme_len = 0;
    while (scheme_len + 2 < n && memcmp(s + scheme_len, "://", 3) != 0) scheme_len++;
    int match = -1;
    for (size_t i = 0; scheme_len + 2 < n && i < sizeof(schemes) / sizeof(schemes[0]); i++) {
        if (strlen(schemes[i].name) == scheme_len && strncasecmp(s, schemes[i].name, scheme_len) == 0) match = (int)i;
    }
    if (match < 0 || (schemes[match].protocol == MAIL_SMTP) != for_send) {
        *error = mail_error(ERR_INVALID_URL, fn, for_send ? "the url must start with smtps:// or smtp://"
                            : "the url must start with imaps://, imap://, pop3s:// or pop3://");
        return false;
    }
    // a system libcurl (Linux) or an old static one may lack the mail protocols
    bool supported = false;
    const curl_version_info_data* info = curl_version_info(CURLVERSION_NOW);
    for (const char* const* p = info ? info->protocols : NULL; p && *p && !supported; p++)
        supported = strcmp(*p, schemes[match].name) == 0;
    if (!supported) {
        *error = mail_error(ERR_NOT_IMPLEMENTED, fn, "this build's libcurl lacks the %s protocol",
                            schemes[match].name);
        return false;
    }
    const char* authority = s + scheme_len + 3;
    size_t rest = n - scheme_len - 3;
    size_t a = 0;
    while (a < rest && authority[a] != '/') a++;
    if (a == 0 || (rest > a && rest != a + 1)) {
        *error = mail_error(ERR_INVALID_URL, fn, "the url names a server only, as scheme://host[:port]");
        return false;
    }
    for (size_t i = 0; i < a; i++) {
        unsigned char c = (unsigned char)authority[i];
        // credentials go in the options, never the URL
        if (c <= 0x20 || c == 0x7f || c == '@' || c == '?' || c == '#') {
            *error = mail_error(ERR_INVALID_URL, fn, "the url's host holds an invalid character");
            return false;
        }
    }
    size_t host_len = 0;
    const char* host = authority;
    if (authority[0] == '[') {
        const char* close = (const char*)memchr(authority, ']', a);
        if (!close) {
            *error = mail_error(ERR_INVALID_URL, fn, "unclosed IPv6 address in the url");
            return false;
        }
        host = authority + 1;
        host_len = (size_t)(close - host);
    } else {
        while (host_len < a && authority[host_len] != ':') host_len++;
    }
    out->loopback = (host_len == 9 && strncmp(host, "127.0.0.1", 9) == 0) ||
                    (host_len == 9 && strncasecmp(host, "localhost", 9) == 0) ||
                    (host_len == 3 && strncmp(host, "::1", 3) == 0);
    out->protocol = schemes[match].protocol;
    out->implicit_tls = schemes[match].tls;
    StrBuf* built = strbuf_new_cap(scheme_len + a + 8);
    strbuf_append_str(built, schemes[match].name);
    strbuf_append_str(built, "://");
    strbuf_append_str_n(built, authority, a);
    strbuf_append_char(built, '/');
    out->url = mem_dup_n(built->str, built->length, MEM_CAT_NETWORK);
    strbuf_free(built);
    if (!out->url) {
        *error = mail_error(ERR_INTERNAL_ERROR, fn, "out of memory");
        return false;
    }
    return true;
}

static void mail_auth_free(MailAuth* auth) {
    mem_free(auth->user);
    mem_free(auth->password);
    mem_free(auth->token);
    mem_free(auth->pin);
    memset(auth, 0, sizeof(*auth));
}

static bool mail_auth_read(const char* fn, const IoFsOptionMap* opts, const MailTarget* target,
                           MailAuth* auth, Item* error) {
    memset(auth, 0, sizeof(*auth));
    auth->timeout_s = MAIL_DEFAULT_TIMEOUT_S;
    auth->tls = true;
    if (!io_fs_option_text(opts, "user", &auth->user, error) ||
        !io_fs_option_text(opts, "password", &auth->password, error) ||
        !io_fs_option_text(opts, "token", &auth->token, error) ||
        !io_fs_option_text(opts, "pin", &auth->pin, error) ||
        !io_fs_option_count(opts, "timeout", &auth->timeout_s, error) ||
        !io_fs_option_bool(opts, "tls", &auth->tls, error)) return false;
    if (!auth->user || !auth->user[0]) {
        *error = mail_error(ERR_TYPE_MISMATCH, fn, "option 'user' is required");
        return false;
    }
    if (!auth->password == !auth->token) {
        *error = mail_error(ERR_TYPE_MISMATCH, fn, "give exactly one of the options 'password' and 'token'");
        return false;
    }
    if (has_line_break(auth->user) || has_line_break(auth->password) || has_line_break(auth->token)) {
        *error = mail_error(ERR_TYPE_MISMATCH, fn, "credentials may not hold line breaks");
        return false;
    }
    if (!auth->tls && target->implicit_tls) {
        *error = mail_error(ERR_TYPE_MISMATCH, fn, "tls: false needs an imap://, pop3:// or smtp:// url");
        return false;
    }
    // without TLS, credentials may only travel to this machine
    if (!auth->tls && !target->loopback) {
        *error = mail_error(ERR_TYPE_MISMATCH, fn, "tls: false is allowed only for a loopback server");
        return false;
    }
    if (auth->timeout_s == 0) auth->timeout_s = MAIL_DEFAULT_TIMEOUT_S;
    return true;
}

static size_t mail_discard_cb(char* data, size_t size, size_t count, void* user) {
    (void)data;
    (void)user;
    return size * count;
}

static CURL* mail_curl_new(const MailTarget* target, const MailAuth* auth) {
    CURL* curl = curl_easy_init();
    if (!curl) return NULL;
    curl_easy_setopt(curl, CURLOPT_URL, target->url);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "imap,imaps,pop3,pop3s,smtp,smtps");
    curl_easy_setopt(curl, CURLOPT_USERNAME, auth->user);
    if (auth->token) curl_easy_setopt(curl, CURLOPT_XOAUTH2_BEARER, auth->token);
    else curl_easy_setopt(curl, CURLOPT_PASSWORD, auth->password);
    // without implicit TLS, STARTTLS is required unless a loopback server waived it
    if (!target->implicit_tls)
        curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)(auth->tls ? CURLUSESSL_ALL : CURLUSESSL_NONE));
    curl_use_host_trust_store(curl);
    if (auth->pin) {
        // a pinned key (a local bridge's self-signed certificate) replaces chain verification
        curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, auth->pin);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)auth->timeout_s);
    curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, (long)auth->timeout_s);
    // an inactivity limit, not a total one: a large RETR may take a while
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, (long)auth->timeout_s);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mail_discard_cb);
    return curl;
}

// curl's own disconnect sends LOGOUT or QUIT and waits for the reply: keep that
// wait off the loop thread. Its reply lines must reach no freed record.
static void mail_curl_dispose(CURL* curl) {
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mail_discard_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, NULL);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, NULL);
    curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, 5L);
    uv_work_t* request = (uv_work_t*)mem_calloc(1, sizeof(uv_work_t), MEM_CAT_NETWORK);
    if (request && lambda_uv_loop()) {
        request->data = curl;
        if (uv_queue_work(lambda_uv_loop(), request,
                [](uv_work_t* work) { curl_easy_cleanup((CURL*)work->data); },
                [](uv_work_t* work, int status) { (void)status; mem_free(work); }) == 0) return;
    }
    mem_free(request);
    curl_easy_cleanup(curl);
}

// ── transfers on the offload pool ──────────────────────────────────────

static size_t mail_lines_cb(char* data, size_t size, size_t count, void* user) {
    MailTransfer* t = (MailTransfer*)user;
    strbuf_append_str_n(t->lines, data, size * count);
    return size * count;
}

// Streams a POP3 payload into a file under `spill_dir` once it passes the threshold.
static size_t mail_body_cb(char* data, size_t size, size_t count, void* user) {
    MailTransfer* t = (MailTransfer*)user;
    size_t n = size * count;
    if (!t->file && t->spill_dir && t->body->length + n > MAIL_SPILL_THRESHOLD) {
        char path[1024];
        for (unsigned attempt = 1; attempt < 64 && !t->file; attempt++) {
            snprintf(path, sizeof(path), "%s/pop3-payload-%u-%u.bin", t->spill_dir,
                     (unsigned)(uintptr_t)t & 0xffffu, attempt);
            t->file = fopen(path, "wbx");
        }
        if (!t->file) {
            t->spill_failed = true;
            return 0;
        }
        t->path = mem_strdup(path, MEM_CAT_NETWORK);
        if (t->body->length && fwrite(t->body->str, 1, t->body->length, t->file) != t->body->length) {
            t->spill_failed = true;
            return 0;
        }
        strbuf_reset(t->body);
    }
    if (t->file) {
        if (fwrite(data, 1, n, t->file) != n) {
            t->spill_failed = true;
            return 0;
        }
        return n;
    }
    strbuf_append_str_n(t->body, data, n);
    return n;
}

static size_t mail_upload_cb(char* buffer, size_t size, size_t count, void* user) {
    MailTransfer* t = (MailTransfer*)user;
    size_t room = size * count;
    size_t left = t->upload_length - t->upload_pos;
    size_t n = left < room ? left : room;
    memcpy(buffer, t->upload + t->upload_pos, n);
    t->upload_pos += n;
    return n;
}

static int mail_xferinfo_cb(void* user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    const int* cancel = ((MailTransfer*)user)->cancel;
    return cancel && __atomic_load_n(cancel, __ATOMIC_ACQUIRE) ? 1 : 0;
}

static MailTransfer* mail_transfer_new(CURL* curl, bool owns_curl) {
    MailTransfer* t = (MailTransfer*)mem_calloc(1, sizeof(MailTransfer), MEM_CAT_NETWORK);
    if (!t) return NULL;
    t->curl = curl;
    t->owns_curl = owns_curl;
    t->lines = strbuf_new_cap(512);
    t->body = strbuf_new_cap(1024);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, mail_lines_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, t);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mail_body_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, t);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, mail_xferinfo_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, t);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, t->errbuf);
    return t;
}

// The handle outlives the transfer record: point its callbacks away from it.
static void mail_transfer_detach(MailTransfer* t, char* errbuf) {
    CURL* curl = t->curl;
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, mail_discard_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, NULL);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    t->owns_curl = false;
}

static void mail_transfer_free(MailTransfer* t) {
    if (!t) return;
    if (t->owns_curl && t->curl) mail_curl_dispose(t->curl);
    if (t->file) fclose(t->file);
    if (t->lines) strbuf_free(t->lines);
    if (t->body) strbuf_free(t->body);
    mem_free(t->path);
    mem_free(t);
}

static void mail_transfer_run(LambdaOffload* job, void* data) {
    MailTransfer* t = (MailTransfer*)data;
    t->cancel = lambda_offload_cancel_flag(job);
    t->code = curl_easy_perform(t->curl);
}

static void mail_transfer_orphan(void* data) {
    mail_transfer_free((MailTransfer*)data);
}

// Runs the transfer while the task parks; ItemNull, or the cancellation error.
static Item mail_transfer_execute(MailTransfer* t) {
    static const LambdaOffloadOps ops = {mail_transfer_run, NULL, mail_transfer_orphan};
    return lambda_task_offload(&ops, t, 0);
}

// The last reply line starting with one of the prefixes, without its CRLF.
static bool mail_last_line(const StrBuf* lines, const char* const* prefixes, int count,
                           const char** line, size_t* length) {
    const char* s = lines->str;
    size_t n = lines->length;
    bool found = false;
    for (size_t i = 0; i < n;) {
        size_t end = i;
        while (end < n && s[end] != '\n') end++;
        size_t content = end;
        if (content > i && s[content - 1] == '\r') content--;
        for (int k = 0; k < count; k++) {
            size_t p = strlen(prefixes[k]);
            if (content - i >= p && memcmp(s + i, prefixes[k], p) == 0) {
                *line = s + i;
                *length = content - i;
                found = true;
            }
        }
        i = end + 1;
    }
    return found;
}

static bool mail_smtp_failure_code(const char* s, size_t n) {
    return n >= 4 && (s[0] == '4' || s[0] == '5') && isdigit((unsigned char)s[1]) &&
        isdigit((unsigned char)s[2]) && (s[3] == ' ' || s[3] == '-');
}

// The server's last refusal among its reply lines, which hold only what the
// server sent (the header callback never sees the client's commands, so no
// credential is here): IMAP `NO`/`BAD` without its tag, POP3 `-ERR`, or an
// SMTP 4xx/5xx reply with its continuation lines joined. A refused login is
// reported with it, since servers say why there (an app password needed,
// a blocked sign-in).
static bool mail_failure_reply(const StrBuf* lines, StrBuf* out) {
    const char* s = lines->str;
    size_t n = lines->length;
    bool found = false;
    for (size_t i = 0; i < n;) {
        size_t end = i;
        while (end < n && s[end] != '\n') end++;
        size_t content = end;
        if (content > i && s[content - 1] == '\r') content--;
        const char* line = s + i;
        size_t length = content - i;
        const char* space = (const char*)memchr(line, ' ', length);
        size_t rest = space ? length - (size_t)(space + 1 - line) : 0;
        bool imap = space && ((rest >= 3 && !memcmp(space + 1, "NO ", 3)) || (rest == 2 && !memcmp(space + 1, "NO", 2)) ||
                              (rest >= 4 && !memcmp(space + 1, "BAD ", 4)));
        bool pop3 = length >= 4 && !memcmp(line, "-ERR", 4);
        bool smtp = mail_smtp_failure_code(line, length);
        if (imap || pop3 || smtp) {
            // a continuation line ("535-…") joins the reply it continues
            bool continues = smtp && found && out->length >= 4 && !memcmp(out->str, line, 3) &&
                mail_smtp_failure_code(out->str, out->length) && out->str[3] == '-';
            if (!continues) strbuf_reset(out);
            else strbuf_append_char(out, ' ');
            if (imap) strbuf_append_str_n(out, space + 1, rest);
            else strbuf_append_str_n(out, line, length);
            found = true;
        }
        i = end + 1;
    }
    return found;
}

// A failed transfer as an error: the server's reply when it gave one, else curl's.
static Item mail_transfer_error(const char* fn, const MailTransfer* t, const char* const* prefixes, int count) {
    const char* line = NULL;
    size_t length = 0;
    if (t->spill_failed) return mail_error(ERR_FILE_WRITE_ERROR, fn, "cannot write the payload into the 'to' directory");
    if (t->code == CURLE_LOGIN_DENIED) {
        StrBuf* reply = strbuf_new_cap(128);
        Item error = reply && mail_failure_reply(t->lines, reply)
            ? mail_error(ERR_NETWORK_ERROR, fn, "login denied: %.*s", (int)(reply->length > 300 ? 300 : reply->length), reply->str)
            : mail_error(ERR_NETWORK_ERROR, fn, "login denied");
        if (reply) strbuf_free(reply);
        return error;
    }
    if (prefixes && mail_last_line(t->lines, prefixes, count, &line, &length))
        return mail_error(ERR_IO_ERROR, fn, "%.*s", (int)length, line);
    const char* reason = t->errbuf[0] ? t->errbuf : curl_easy_strerror(t->code);
    LambdaErrorCode code = t->code == CURLE_OPERATION_TIMEDOUT ? ERR_NETWORK_TIMEOUT : ERR_NETWORK_ERROR;
    return mail_error(code, fn, "%s", reason);
}

// ── sessions ───────────────────────────────────────────────────────────

static char mail_session_brand;

static Item mail_handle_get(void* data, Item key) { (void)data; (void)key; return ItemNull; }
static void mail_handle_set(void* data, Item key, Item value) {
    (void)data; (void)key; (void)value;
    log_error("io.mail: sessions are immutable");
}
static int64_t mail_handle_count(void* data) { (void)data; return 0; }
static SymbolKeyList* mail_handle_keys(void* data) { (void)data; return NULL; }
static Item mail_handle_at(void* data, int64_t index) { (void)data; (void)index; return ItemNull; }
// the owning task releases the session; the handle owns nothing
static void mail_handle_destroy(void* data) { (void)data; }
static void mail_handle_trace(void* data, gc_heap* gc) { (void)data; (void)gc; }

static VMapVtable mail_handle_vtable = {
    mail_handle_get, mail_handle_set, mail_handle_count, mail_handle_keys,
    mail_handle_at, mail_handle_at, mail_handle_destroy, mail_handle_trace,
};

static MailSession* mail_session_of(Item item) {
    if (get_type_id(item) != LMD_TYPE_VMAP || !item.vmap || item.vmap->host_type != &mail_session_brand) return NULL;
    return (MailSession*)item.vmap->host_data;   // NULL once closed
}

// The network resources; an operation in flight keeps them until it ends.
static void mail_session_dispose(MailSession* s) {
    if (s->idle_pending && s->curl && !s->broken) {
        // a server inside IDLE expects DONE before curl's own LOGOUT; best effort
        size_t sent = 0;
        curl_easy_send(s->curl, "DONE\r\n", 6, &sent);
        s->idle_pending = false;
    }
    lambda_socket_waiter_close(s->waiter);
    s->waiter = NULL;
    mail_curl_dispose(s->curl);
    s->curl = NULL;
    imap_reader_free(s->reader);
    s->reader = NULL;
    imap_parser_free(s->parser);
    s->parser = NULL;
}

static void mail_session_close(MailSession* s) {
    if (s->closed) return;
    s->closed = true;
    if (get_type_id(s->handle) == LMD_TYPE_VMAP && s->handle.vmap) {
        s->handle.vmap->data = NULL;
        s->handle.vmap->host_data = NULL;
    }
    if (s->handle_rooted && context && context->heap) heap_unregister_gc_root(&s->handle.item);
    s->handle_rooted = false;
    s->handle = ItemNull;
    if (s->busy) {
        // wake a socket wait now; the operation disposes the rest as it ends
        lambda_socket_waiter_close(s->waiter);
        s->waiter = NULL;
        return;
    }
    mail_session_dispose(s);
}

static void mail_session_free(MailSession* s) {
    mem_free(s->url);
    mem_free(s);
}

static void mail_owner_done(LambdaTask* task, Item result, void* data) {
    (void)task; (void)result; (void)data;
}

// The task that opened the session ended, or was torn down (ML3).
static void mail_owner_release(void* data) {
    MailSession* s = (MailSession*)data;
    mail_session_close(s);
    if (s->busy) s->release_when_idle = true;
    else mail_session_free(s);
}

static MailSession* mail_begin(const char* fn, Item handle, Item* error) {
    MailSession* s = mail_session_of(handle);
    if (!s || s->closed) {
        *error = mail_error(ERR_INVALID_STATE, fn, "not an open mail session");
        return NULL;
    }
    if (s->busy) {
        *error = mail_error(ERR_INVALID_STATE, fn, "the session is busy with another command");
        return NULL;
    }
    if (s->broken) {
        *error = mail_error(ERR_NETWORK_ERROR, fn, "the session's connection failed earlier; open a new one");
        return NULL;
    }
    s->busy = true;
    return s;
}

// Ends an operation; `s` may be freed by this call.
static void mail_end(MailSession* s) {
    s->busy = false;
    if (s->closed) mail_session_dispose(s);
    if (s->release_when_idle) mail_session_free(s);
}

// ── the IMAP engine ────────────────────────────────────────────────────

static int mail_imap_send(MailSession* s, const char* fn, const char* data, size_t length, Item* error) {
    size_t done = 0;
    while (done < length) {
        if (s->closed) {
            *error = mail_error(ERR_INVALID_STATE, fn, "the session was closed");
            return -1;
        }
        size_t n = 0;
        CURLcode rc = curl_easy_send(s->curl, data + done, length - done, &n);
        if (rc == CURLE_AGAIN) {
            int ready = lambda_socket_waiter_wait(s->waiter, true, s->timeout_ms, error);
            if (ready < 0) return -1;
            if (ready == 0) {
                *error = mail_error(ERR_NETWORK_TIMEOUT, fn, "timed out sending to the server");
                return -1;
            }
            continue;
        }
        if (rc != CURLE_OK) {
            *error = mail_error(ERR_NETWORK_ERROR, fn, "send failed: %s", curl_easy_strerror(rc));
            return -1;
        }
        done += n;
    }
    return 0;
}

// 1: a response in *out; 0: wait_ms passed with no data; -1: *error. A
// network or framing failure also marks the session broken.
static int mail_imap_read(MailSession* s, const char* fn, int64_t wait_ms, ImapResponse* out, Item* error) {
    char chunk[MAIL_RECV_CHUNK];
    for (;;) {
        int got = imap_reader_next(s->reader, out);
        if (got == 1) return 1;
        if (got < 0) {
            s->broken = true;
            *error = mail_error(ERR_PARSE_ERROR, fn, "%s", imap_reader_error(s->reader));
            return -1;
        }
        if (s->closed) {
            *error = mail_error(ERR_INVALID_STATE, fn, "the session was closed");
            return -1;
        }
        size_t n = 0;
        // TLS may hold decrypted bytes the socket no longer shows: read before waiting
        CURLcode rc = curl_easy_recv(s->curl, chunk, sizeof(chunk), &n);
        if (rc == CURLE_AGAIN) {
            int ready = lambda_socket_waiter_wait(s->waiter, false, wait_ms, error);
            if (ready < 0) return -1;
            if (ready == 0) return 0;
            continue;
        }
        if (rc != CURLE_OK || n == 0) {
            s->broken = true;
            *error = rc != CURLE_OK ? mail_error(ERR_NETWORK_ERROR, fn, "receive failed: %s", curl_easy_strerror(rc))
                                    : mail_error(ERR_NETWORK_ERROR, fn, "the server closed the connection");
            return -1;
        }
        imap_reader_feed(s->reader, chunk, n);
    }
}

static void mail_note_capabilities(MailSession* s, const ImapNode* node) {
    if (!node || node->kind != IMAP_NODE_ATOM) return;
    bool caps = strcasecmp(node->text, "CAPABILITY") == 0;
    bool enabled = strcasecmp(node->text, "ENABLED") == 0;
    for (const ImapNode* n = node->next; (caps || enabled) && n; n = n->next) {
        if (n->kind != IMAP_NODE_ATOM) continue;
        if (caps && strcasecmp(n->text, "LITERAL+") == 0) s->nonsync_limit = SIZE_MAX;
        else if (caps && strcasecmp(n->text, "LITERAL-") == 0 && s->nonsync_limit < 4096) s->nonsync_limit = 4096;
        else if (enabled && strcasecmp(n->text, "UTF8=ACCEPT") == 0) s->utf8 = true;
    }
}

static Item mail_nodes_array(const ImapResponse* r, const ImapNode* first);

// Atoms are symbols, literals binary (or a file path under `to`), NIL null.
static Item mail_node_item(const ImapResponse* r, const ImapNode* node) {
    switch (node->kind) {
    case IMAP_NODE_ATOM: {
        Symbol* symbol = heap_create_symbol(node->text, node->length);
        return symbol ? (Item){.item = y2it(symbol)} : ItemError;
    }
    case IMAP_NODE_NUMBER:
        if (node->number <= (uint64_t)INT53_MAX) return (Item){.item = i2it((int64_t)node->number)};
        // MODSEQ, X-GM-MSGID: full width, or digits past int64
        if (node->number <= (uint64_t)INT64_MAX) return box_int64_value((int64_t)node->number);
        return io_fs_make_string(node->text, node->length);
    case IMAP_NODE_STRING:
        return io_fs_make_string(node->text, node->length);
    case IMAP_NODE_LITERAL: {
        const ImapLiteral* literal = &r->literals[node->literal];
        if (literal->path) return io_fs_make_string(literal->path, strlen(literal->path));
        // S2.2.2v2: empty bytes are absent
        if (literal->length == 0) return ItemNull;
        Binary* bytes = heap_binary_from_bytes(r->bytes + literal->offset, (int64_t)literal->length);
        return bytes ? (Item){.item = x2it(bytes)} : ItemError;
    }
    case IMAP_NODE_NIL:
        return ItemNull;
    case IMAP_NODE_LIST:
        return mail_nodes_array(r, node->first);
    }
    return ItemNull;
}

static Item mail_nodes_array(const ImapResponse* r, const ImapNode* first) {
    RootFrame roots(2);
    Rooted<Array*> list(roots, array());
    Rooted<Item> value(roots, ItemNull);
    for (const ImapNode* node = first; node; node = node->next) {
        value.set(mail_node_item(r, node));
        if (get_type_id(value.get()) == LMD_TYPE_ERROR) return value.get();
        array_push_verbatim(list.get(), value.get());
    }
    return (Item){.array = list.get()};
}

// An untagged response: its tokens, or [status, code, text] for a status response.
static Item mail_response_item(const ImapResponse* r, const ImapParsed* p) {
    if (!p->status) return mail_nodes_array(r, p->data);
    RootFrame roots(4);
    Rooted<Array*> list(roots, array());
    Rooted<Item> status(roots, ItemNull), code(roots, ItemNull), text(roots, ItemNull);
    Symbol* symbol = heap_create_symbol(p->status, strlen(p->status));
    if (!symbol) return ItemError;
    status.set((Item){.item = y2it(symbol)});
    if (p->code) code.set(mail_nodes_array(r, p->code));
    text.set(io_fs_make_string(p->text, p->text_length));
    if (get_type_id(code.get()) == LMD_TYPE_ERROR) return code.get();
    if (get_type_id(text.get()) == LMD_TYPE_ERROR) return text.get();
    array_push_verbatim(list.get(), status.get());
    array_push_verbatim(list.get(), code.get());
    array_push_verbatim(list.get(), text.get());
    return (Item){.array = list.get()};
}

// Reads responses until `tag` completes, or with want_continuation until the
// server asks for a literal's bytes. Untagged responses go to `responses`.
// 2: continuation; 1: OK; 0: NO or BAD, *error holding the server's reply;
// -1: *error.
static int mail_imap_await(MailSession* s, const char* fn, const char* tag, bool want_continuation,
                           Rooted<Array*>* responses, Rooted<Item>* code, Rooted<Item>* text, Item* error) {
    size_t tag_length = strlen(tag);
    for (;;) {
        ImapResponse response;
        int got = mail_imap_read(s, fn, s->timeout_ms, &response, error);
        if (got == 0) {
            s->broken = true;
            *error = mail_error(ERR_NETWORK_TIMEOUT, fn, "timed out waiting for the server");
            return -1;
        }
        if (got < 0) {
            // cancelled with the command on the wire: the next use reads its rest
            if (!s->broken && !s->closed) snprintf(s->drain_tag, sizeof(s->drain_tag), "%s", tag);
            return -1;
        }
        ImapParsed parsed;
        if (imap_parse(s->parser, &response, &parsed) < 0) {
            s->broken = true;
            *error = mail_error(ERR_PARSE_ERROR, fn, "%s", imap_parser_error(s->parser));
            return -1;
        }
        if (parsed.kind == IMAP_RESPONSE_CONTINUATION) {
            if (want_continuation) return 2;
            continue;
        }
        if (parsed.kind == IMAP_RESPONSE_UNTAGGED) {
            mail_note_capabilities(s, parsed.status ? parsed.code : parsed.data);
            if (responses) {
                Item item = mail_response_item(&response, &parsed);
                if (get_type_id(item) == LMD_TYPE_ERROR) {
                    *error = item;
                    return -1;
                }
                array_push_verbatim(responses->get(), item);
            }
            continue;
        }
        // a tag of an earlier abandoned command is not ours to report
        if (parsed.tag_length != tag_length || memcmp(parsed.tag, tag, tag_length) != 0) continue;
        mail_note_capabilities(s, parsed.code);
        if (strcmp(parsed.status, "OK") == 0) {
            if (code) code->set(parsed.code ? mail_nodes_array(&response, parsed.code) : ItemNull);
            if (text) text->set(io_fs_make_string(parsed.text, parsed.text_length));
            return 1;
        }
        // the reply without its tag: "NO [TRYCREATE] Mailbox doesn't exist"
        size_t from = tag_length + 1;
        size_t end = response.text_length;
        while (end > from && (response.text[end - 1] == '\n' || response.text[end - 1] == '\r')) end--;
        *error = mail_error(ERR_IO_ERROR, fn, "%.*s", (int)(end > from ? end - from : 0), response.text + from);
        return 0;
    }
}

// Before a new command: finish what a cancelled one left on the wire.
static int mail_imap_settle(MailSession* s, const char* fn, Rooted<Array*>* responses, Item* error) {
    if (s->idle_pending) {
        s->idle_pending = false;
        if (mail_imap_send(s, fn, "DONE\r\n", 6, error) < 0) {
            s->broken = true;
            return -1;
        }
    }
    if (s->drain_tag[0]) {
        char tag[sizeof(s->drain_tag)];
        memcpy(tag, s->drain_tag, sizeof(tag));
        s->drain_tag[0] = '\0';
        Item ignored = ItemNull;
        int r = mail_imap_await(s, fn, tag, false, responses, NULL, NULL, &ignored);
        if (r < 0) {
            *error = ignored;
            return -1;
        }
    }
    return 0;
}

// Sends an encoded command and reads to its tagged completion: 1 OK, 0 NO/BAD, -1 failure.
static int mail_imap_run(MailSession* s, const char* fn, const ImapCommand* command, const char* tag,
                         Rooted<Array*>* responses, Rooted<Item>* code, Rooted<Item>* text, Item* error) {
    const char* bytes = command->bytes->str;
    size_t total = command->bytes->length;
    size_t start = 0;
    for (int i = 0; i <= command->wait_count; i++) {
        size_t stop = i < command->wait_count ? command->waits[i] : total;
        if (mail_imap_send(s, fn, bytes + start, stop - start, error) < 0) {
            s->broken = true;   // a partial command leaves the protocol state unknown
            return -1;
        }
        start = stop;
        if (i == command->wait_count) break;
        // a synchronizing literal: the server must ask for its bytes
        int r = mail_imap_await(s, fn, tag, true, responses, code, text, error);
        if (r != 2) return r;
    }
    return mail_imap_await(s, fn, tag, false, responses, code, text, error);
}

static void mail_next_tag(MailSession* s, char* tag, size_t size) {
    snprintf(tag, size, "L%u", ++s->next_tag);
}

static bool mail_encode_arg(ImapCommand* command, Item value, int depth, const char* fn, Item* error) {
    TypeId type = get_type_id(value);
    bool ok = true;
    switch (type) {
    case LMD_TYPE_NULL:
        imap_command_nil(command);
        return true;
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64: {
        int64_t n = 0;
        if (!lambda_item_to_int64_exact(value, &n) || n < 0) {
            *error = mail_error(ERR_TYPE_MISMATCH, fn, "a number argument must be a non-negative int");
            return false;
        }
        imap_command_number(command, (uint64_t)n);
        return true;
    }
    case LMD_TYPE_SYMBOL:
        ok = imap_command_atom(command, value.get_chars(), value.get_len());
        break;
    case LMD_TYPE_STRING:
        ok = imap_command_string(command, value.get_chars(), value.get_len());
        break;
    case LMD_TYPE_BINARY: {
        Binary* bytes = value.get_safe_binary();
        ok = imap_command_literal(command, bytes ? (const char*)binary_data(bytes) : "",
                                  bytes ? binary_length(bytes) : 0);
        break;
    }
    case LMD_TYPE_ARRAY:
    case LMD_TYPE_ARRAY_NUM: {
        if (depth >= MAIL_ARG_DEPTH) {
            *error = mail_error(ERR_TYPE_MISMATCH, fn, "arguments nested too deeply");
            return false;
        }
        imap_command_list_begin(command);
        int64_t count = fn_len(value);
        for (int64_t i = 0; i < count; i++) {
            if (!mail_encode_arg(command, item_at(value, i), depth + 1, fn, error)) return false;
        }
        ok = imap_command_list_end(command);
        break;
    }
    default:
        *error = mail_error(ERR_TYPE_MISMATCH, fn, "cannot send a %s value to an IMAP server", get_type_name(type));
        return false;
    }
    if (!ok) *error = mail_error(ERR_TYPE_MISMATCH, fn, "%s", command->error);
    return ok;
}

static bool mail_is_list(Item value) {
    TypeId type = get_type_id(value);
    return type == LMD_TYPE_ARRAY || type == LMD_TYPE_ARRAY_NUM;
}

// {status, code, text, responses}
static Item mail_result_map(Rooted<Item>* code, Rooted<Item>* text, Rooted<Array*>* responses) {
    static const char* names[] = {"status", "code", "text", "responses"};
    static const TypeId types[] = {LMD_TYPE_SYMBOL, LMD_TYPE_ANY, LMD_TYPE_STRING, LMD_TYPE_ARRAY};
    RootFrame roots(2);
    Rooted<Item> status(roots, ItemNull);
    Rooted<Map*> map(roots, (Map*)NULL);
    Symbol* ok = heap_create_symbol("ok", 2);
    if (!ok) return ItemError;
    status.set((Item){.item = y2it(ok)});
    if (get_type_id(text->get()) != LMD_TYPE_STRING) text->set(io_fs_make_string("", 0));
    map.set(map_alloc_for_type(runtime_result_shape(names, types, 4), NULL, 0));
    if (!map.get()) return ItemError;
    Item values[] = {status.get(), code->get(), text->get(), (Item){.array = responses->get()}};
    map_fill_items(map.get(), values, 4);
    return (Item){.map = map.get()};
}

// The `to` option as a writable directory, mem-owned; NULL when absent.
static bool mail_spill_dir(const char* fn, Item options, SysFunc id, char** out, Item* error) {
    *out = NULL;
    IoFsOptionMap opts;
    if (!io_fs_options_open(options, fn, id, &opts, error)) return false;
    Item to = io_fs_option_get(&opts, "to");
    if (get_type_id(to) == LMD_TYPE_NULL) return true;
    StrBuf* path = lambda_get_local_path_from_item(to);
    if (!path) {
        *error = mail_error(ERR_TYPE_MISMATCH, fn, "option 'to' must name a local directory");
        return false;
    }
    bool usable = file_is_dir(path->str) && file_is_writable(path->str);
    if (usable) *out = mem_dup_n(path->str, path->length, MEM_CAT_NETWORK);
    strbuf_free(path);
    if (!usable) {
        *error = mail_error(ERR_FILE_ACCESS_DENIED, fn, "option 'to' must name a writable directory");
        return false;
    }
    return *out != NULL;
}

static Item mail_imap_command(MailSession* s, const char* fn, Item command, Item args, Item options) {
    Item error = ItemNull;
    char* spill = NULL;
    if (!mail_spill_dir(fn, options, SYSPROC_IO_MAIL_COMMAND, &spill, &error)) return error;
    ImapCommand encoded;
    if (!imap_command_init(&encoded, s->nonsync_limit, s->utf8)) {
        mem_free(spill);
        return mail_error(ERR_INTERNAL_ERROR, fn, "out of memory");
    }
    char tag[16];
    mail_next_tag(s, tag, sizeof(tag));
    bool ok = imap_command_begin(&encoded, tag, command.get_chars(), command.get_len());
    if (!ok) error = mail_error(ERR_TYPE_MISMATCH, fn, "%s", encoded.error);
    int64_t count = get_type_id(args) == LMD_TYPE_NULL ? 0 : fn_len(args);
    for (int64_t i = 0; ok && i < count; i++) ok = mail_encode_arg(&encoded, item_at(args, i), 0, fn, &error);
    if (ok && !imap_command_end(&encoded)) {
        ok = false;
        error = mail_error(ERR_TYPE_MISMATCH, fn, "%s", encoded.error);
    }
    RootFrame roots(3);
    Rooted<Array*> responses(roots, array());
    Rooted<Item> code(roots, ItemNull), text(roots, ItemNull);
    Item result = error;
    if (ok && mail_imap_settle(s, fn, &responses, &error) == 0) {
        if (spill) imap_reader_set_spill(s->reader, spill, MAIL_SPILL_THRESHOLD);
        int r = mail_imap_run(s, fn, &encoded, tag, &responses, &code, &text, &error);
        if (s->reader) imap_reader_set_spill(s->reader, NULL, 0);
        result = r == 1 ? mail_result_map(&code, &text, &responses) : error;
    } else if (ok) {
        result = error;
    }
    imap_command_free(&encoded);
    mem_free(spill);
    return result;
}

// ── POP3 (curl transfers) ──────────────────────────────────────────────

// RFC 1939: these replies run to a "." line; the others are one line.
static bool pop3_multiline(const char* text, size_t length, bool has_args) {
    size_t word = 0;
    while (word < length && text[word] != ' ') word++;
    bool argued = has_args || word < length;
    if ((word == 4 && strncasecmp(text, "RETR", 4) == 0) || (word == 3 && strncasecmp(text, "TOP", 3) == 0) ||
        (word == 4 && strncasecmp(text, "CAPA", 4) == 0)) return true;
    if ((word == 4 && strncasecmp(text, "LIST", 4) == 0) || (word == 4 && strncasecmp(text, "UIDL", 4) == 0))
        return !argued;
    return false;
}

static Item mail_pop3_command(MailSession* s, const char* fn, Item command, Item args, Item options) {
    Item error = ItemNull;
    char* spill = NULL;
    if (!mail_spill_dir(fn, options, SYSPROC_IO_MAIL_COMMAND, &spill, &error)) return error;
    StrBuf* text = strbuf_new_cap(64);
    strbuf_append_str_n(text, command.get_chars(), command.get_len());
    int64_t count = get_type_id(args) == LMD_TYPE_NULL ? 0 : fn_len(args);
    bool ok = !has_line_break(text->str) && text->length > 0;
    if (!ok) error = mail_error(ERR_TYPE_MISMATCH, fn, "a POP3 command is one line of text");
    for (int64_t i = 0; ok && i < count; i++) {
        Item arg = item_at(args, i);
        TypeId type = get_type_id(arg);
        int64_t n = 0;
        strbuf_append_char(text, ' ');
        if ((type == LMD_TYPE_INT || type == LMD_TYPE_INT64) && lambda_item_to_int64_exact(arg, &n) && n >= 0) {
            strbuf_append_int64(text, n);
        } else if (is_text_type_id(type)) {
            const char* chars = arg.get_chars();
            size_t length = arg.get_len();
            for (size_t k = 0; ok && k < length; k++) ok = (unsigned char)chars[k] > 0x20 && chars[k] != 0x7f;
            if (ok) strbuf_append_str_n(text, chars, length);
            else error = mail_error(ERR_TYPE_MISMATCH, fn, "a POP3 argument may not hold spaces or control characters");
        } else {
            ok = false;
            error = mail_error(ERR_TYPE_MISMATCH, fn, "a POP3 argument is a non-negative int or text");
        }
    }
    if (!ok) {
        strbuf_free(text);
        mem_free(spill);
        return error;
    }
    MailTransfer* t = mail_transfer_new(s->curl, false);
    if (!t) {
        strbuf_free(text);
        mem_free(spill);
        return mail_error(ERR_INTERNAL_ERROR, fn, "out of memory");
    }
    t->spill_dir = spill;
    curl_easy_setopt(s->curl, CURLOPT_URL, s->url);
    curl_easy_setopt(s->curl, CURLOPT_CUSTOMREQUEST, text->str);
    curl_easy_setopt(s->curl, CURLOPT_NOBODY, pop3_multiline(text->str, text->length, count > 0) ? 0L : 1L);
    Item outcome = mail_transfer_execute(t);
    curl_easy_setopt(s->curl, CURLOPT_CUSTOMREQUEST, NULL);
    mail_transfer_detach(t, s->errbuf);
    strbuf_free(text);
    if (t->file) {
        fclose(t->file);
        t->file = NULL;
    }
    static const char* const prefixes[] = {"+OK", "-ERR"};
    Item result;
    if (get_type_id(outcome) == LMD_TYPE_ERROR) {
        // cancelled mid-command: the connection may hold the rest of a reply
        s->broken = true;
        result = outcome;
    } else if (t->code != CURLE_OK) {
        const char* line = NULL;
        size_t length = 0;
        bool refused = mail_last_line(t->lines, prefixes + 1, 1, &line, &length);
        if (!refused) s->broken = true;
        result = mail_transfer_error(fn, t, prefixes + 1, 1);
    } else {
        RootFrame roots(3);
        Rooted<Array*> responses(roots, array());
        Rooted<Item> code(roots, ItemNull), reply(roots, ItemNull);
        const char* line = NULL;
        size_t length = 0;
        if (mail_last_line(t->lines, prefixes, 1, &line, &length)) {
            size_t skip = length > 3 && line[3] == ' ' ? 4 : 3;
            reply.set(io_fs_make_string(line + skip, length - skip));
        }
        Item payload = ItemNull;
        if (t->path) payload = io_fs_make_string(t->path, strlen(t->path));
        else if (t->body->length) {
            Binary* bytes = heap_binary_from_bytes(t->body->str, (int64_t)t->body->length);
            payload = bytes ? (Item){.item = x2it(bytes)} : ItemError;
        }
        if (get_type_id(payload) == LMD_TYPE_ERROR) result = payload;
        else {
            if (get_type_id(payload) != LMD_TYPE_NULL) array_push_verbatim(responses.get(), payload);
            result = mail_result_map(&code, &reply, &responses);
        }
    }
    mail_transfer_free(t);
    mem_free(spill);
    return result;
}

// ── lambda.io.mail ─────────────────────────────────────────────────────

static Item mail_publish(MailSession* s, const char* fn) {
    LambdaTask* owner = lambda_task_running();
    if (!owner || !lambda_task_on_complete(owner, mail_owner_done, s, mail_owner_release)) {
        mail_session_dispose(s);
        mail_session_free(s);
        return mail_error(ERR_INVALID_STATE, fn, "a mail session needs a running procedure");
    }
    VMap* handle = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    if (!handle) {
        mail_session_close(s);
        return mail_error(ERR_INTERNAL_ERROR, fn, "out of memory");
    }
    handle->type_id = LMD_TYPE_VMAP;
    handle->vtable = &mail_handle_vtable;
    handle->host_type = &mail_session_brand;
    handle->host_data = s;
    handle->data = s;
    s->handle = (Item){.vmap = handle};
    heap_register_gc_root(&s->handle.item);
    s->handle_rooted = true;
    return s->handle;
}

extern "C" Item pn_io_mail_open(Item url, Item options) {
    const char* fn = "io.mail.open";
    Item error = ItemNull;
    MailTarget target = {};
    if (!mail_parse_target(fn, url, false, &target, &error)) return error;
    IoFsOptionMap opts;
    MailAuth auth = {};
    if (!io_fs_options_open(options, fn, SYSPROC_IO_MAIL_OPEN, &opts, &error) ||
        !mail_auth_read(fn, &opts, &target, &auth, &error)) {
        mail_auth_free(&auth);
        mail_target_free(&target);
        return error;
    }
    CURL* curl = mail_curl_new(&target, &auth);
    MailTransfer* t = curl ? mail_transfer_new(curl, true) : NULL;
    if (!t) {
        if (curl) curl_easy_cleanup(curl);
        mail_auth_free(&auth);
        mail_target_free(&target);
        return mail_error(ERR_INTERNAL_ERROR, fn, "cannot create a curl handle");
    }
    if (target.protocol == MAIL_IMAP) {
        // connect, secure and log in, then stop: this file speaks IMAP (ML1)
        curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 1L);
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "NOOP");
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }
    log_debug("io.mail: open %s as %s", target.url, auth.user);
    Item outcome = mail_transfer_execute(t);
    int64_t timeout_ms = auth.timeout_s * 1000;
    mail_auth_free(&auth);
    if (get_type_id(outcome) == LMD_TYPE_ERROR || t->code != CURLE_OK) {
        static const char* const refusals[] = {"-ERR"};
        Item failure = get_type_id(outcome) == LMD_TYPE_ERROR ? outcome
            : mail_transfer_error(fn, t, target.protocol == MAIL_POP3 ? refusals : NULL, 1);
        mail_transfer_free(t);
        mail_target_free(&target);
        return failure;
    }
    MailSession* s = (MailSession*)mem_calloc(1, sizeof(MailSession), MEM_CAT_NETWORK);
    if (!s) {
        mail_transfer_free(t);
        mail_target_free(&target);
        return mail_error(ERR_INTERNAL_ERROR, fn, "out of memory");
    }
    s->protocol = target.protocol;
    s->curl = curl;
    s->url = target.url;
    target.url = NULL;
    s->timeout_ms = timeout_ms;
    s->handle = ItemNull;
    mail_transfer_detach(t, s->errbuf);
    if (s->protocol == MAIL_POP3) curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);
    mail_transfer_free(t);
    if (s->protocol == MAIL_IMAP) {
        curl_socket_t socket = CURL_SOCKET_BAD;
        curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket);
        s->socket = socket;
        s->waiter = socket != CURL_SOCKET_BAD ? lambda_socket_waiter_new((uint64_t)socket) : NULL;
        s->reader = imap_reader_new();
        s->parser = imap_parser_new();
        if (!s->waiter || !s->reader || !s->parser) {
            mail_session_dispose(s);
            mail_session_free(s);
            return mail_error(ERR_NETWORK_ERROR, fn, "cannot take over the IMAP connection");
        }
    }
    Item handle = mail_publish(s, fn);
    if (get_type_id(handle) == LMD_TYPE_ERROR || s->protocol != MAIL_IMAP) return handle;
    // the capabilities after login decide how literals are sent
    RootFrame roots(1);
    Rooted<Item> rooted(roots, handle);
    s->busy = true;
    ImapCommand capability;
    bool ok = imap_command_init(&capability, 0, false);
    char tag[16];
    mail_next_tag(s, tag, sizeof(tag));
    ok = ok && imap_command_begin(&capability, tag, "CAPABILITY", 10) && imap_command_end(&capability);
    int r = ok ? mail_imap_run(s, fn, &capability, tag, NULL, NULL, NULL, &error) : -1;
    imap_command_free(&capability);
    s->busy = false;
    if (r != 1) {
        if (!ok) error = mail_error(ERR_INTERNAL_ERROR, fn, "out of memory");
        mail_session_close(s);
        return error;
    }
    return rooted.get();
}

static Item mail_command(Item handle, Item command, Item args, Item options) {
    const char* fn = "io.mail.command";
    if (!is_text_type_id(get_type_id(command))) return mail_error(ERR_TYPE_MISMATCH, fn, "the command must be text");
    if (get_type_id(args) != LMD_TYPE_NULL && !mail_is_list(args))
        return mail_error(ERR_TYPE_MISMATCH, fn, "the arguments must be an array");
    Item error = ItemNull;
    MailSession* s = mail_begin(fn, handle, &error);
    if (!s) return error;
    Item result = s->protocol == MAIL_IMAP ? mail_imap_command(s, fn, command, args, options)
                                           : mail_pop3_command(s, fn, command, args, options);
    mail_end(s);
    return result;
}

extern "C" Item pn_io_mail_command2(Item session, Item command) {
    return mail_command(session, command, ItemNull, ItemNull);
}

extern "C" Item pn_io_mail_command3(Item session, Item command, Item args) {
    return mail_command(session, command, args, ItemNull);
}

extern "C" Item pn_io_mail_command4(Item session, Item command, Item args, Item options) {
    return mail_command(session, command, args, options);
}

// IDLE (RFC 2177): parks until the server reports a change or `seconds` pass.
static int mail_imap_idle(MailSession* s, const char* fn, int64_t seconds, Rooted<Array*>* responses, Item* error) {
    if (mail_imap_settle(s, fn, responses, error) < 0) return -1;
    char tag[16];
    mail_next_tag(s, tag, sizeof(tag));
    char line[32];
    int n = snprintf(line, sizeof(line), "%s IDLE\r\n", tag);
    if (mail_imap_send(s, fn, line, (size_t)n, error) < 0) {
        s->broken = true;
        return -1;
    }
    int r = mail_imap_await(s, fn, tag, true, responses, NULL, NULL, error);
    if (r == 1) return 0;
    if (r != 2) return -1;
    s->idle_pending = true;     // until DONE is sent
    uint64_t deadline = time_now_ms() + (uint64_t)seconds * 1000;
    for (;;) {
        uint64_t now = time_now_ms();
        if (now >= deadline) break;
        ImapResponse response;
        int got = mail_imap_read(s, fn, (int64_t)(deadline - now), &response, error);
        if (got < 0) {
            // cancelled while idling: the next use sends DONE and reads the rest
            if (!s->broken && !s->closed) snprintf(s->drain_tag, sizeof(s->drain_tag), "%s", tag);
            return -1;
        }
        if (got == 0) break;
        ImapParsed parsed;
        if (imap_parse(s->parser, &response, &parsed) < 0) {
            s->broken = true;
            *error = mail_error(ERR_PARSE_ERROR, fn, "%s", imap_parser_error(s->parser));
            return -1;
        }
        if (parsed.kind != IMAP_RESPONSE_UNTAGGED) continue;
        Item item = mail_response_item(&response, &parsed);
        if (get_type_id(item) == LMD_TYPE_ERROR) {
            *error = item;
            return -1;
        }
        array_push_verbatim(responses->get(), item);
        break;      // the first change ends the wait; DONE flushes the rest
    }
    s->idle_pending = false;
    if (mail_imap_send(s, fn, "DONE\r\n", 6, error) < 0) {
        s->broken = true;
        return -1;
    }
    r = mail_imap_await(s, fn, tag, false, responses, NULL, NULL, error);
    return r == 1 ? 0 : -1;
}

extern "C" Item pn_io_mail_idle(Item session, Item seconds_item) {
    const char* fn = "io.mail.idle";
    int64_t seconds = 0;
    if (!lambda_item_to_int64_exact(seconds_item, &seconds) || seconds < 1 || seconds > MAIL_IDLE_MAX_S)
        return mail_error(ERR_TYPE_MISMATCH, fn, "seconds must be an int from 1 to %d", MAIL_IDLE_MAX_S);
    Item error = ItemNull;
    MailSession* s = mail_begin(fn, session, &error);
    if (!s) return error;
    if (s->protocol != MAIL_IMAP) {
        mail_end(s);
        return mail_error(ERR_INVALID_OPERATION, fn, "IDLE is an IMAP command");
    }
    RootFrame roots(1);
    Rooted<Array*> responses(roots, array());
    int r = mail_imap_idle(s, fn, seconds, &responses, &error);
    mail_end(s);
    return r == 0 ? (Item){.array = responses.get()} : error;
}

// ── SMTP (one curl transfer) ───────────────────────────────────────────

// The RCPT replies are the final reply lines just before DATA's 354, or the
// last ones when the server refused every recipient before DATA.
static void smtp_rejected(const StrBuf* lines, struct curl_slist* recipients, int count, bool* rejected) {
    const char* finals[256];
    int final_count = 0;
    const char* s = lines->str;
    size_t n = lines->length;
    for (size_t i = 0; i < n;) {
        size_t end = i;
        while (end < n && s[end] != '\n') end++;
        bool final = end - i >= 4 && s[i] >= '0' && s[i] <= '9' && s[i + 3] == ' ';
        if (final && final_count < 256) finals[final_count++] = s + i;
        i = end + 1;
    }
    int data = final_count;
    for (int i = final_count - 1; i >= 0; i--) {
        if (memcmp(finals[i], "354", 3) == 0) {
            data = i;
            break;
        }
    }
    int first = data - count;
    struct curl_slist* rcpt = recipients;
    for (int k = 0; k < count && rcpt; k++, rcpt = rcpt->next) {
        int index = first + k;
        rejected[k] = index >= 0 && index < final_count && finals[index][0] >= '4';
    }
}

extern "C" Item pn_io_mail_send(Item url, Item options, Item message) {
    const char* fn = "io.mail.send";
    Item error = ItemNull;
    MailTarget target = {};
    if (!mail_parse_target(fn, url, true, &target, &error)) return error;
    TypeId message_type = get_type_id(message);
    if (message_type != LMD_TYPE_STRING && message_type != LMD_TYPE_BINARY) {
        mail_target_free(&target);
        return mail_error(ERR_TYPE_MISMATCH, fn, "the message must be text or binary");
    }
    IoFsOptionMap opts;
    MailAuth auth = {};
    char* from = NULL;
    if (!io_fs_options_open(options, fn, SYSPROC_IO_MAIL_SEND, &opts, &error) ||
        !mail_auth_read(fn, &opts, &target, &auth, &error) ||
        !io_fs_option_text(&opts, "from", &from, &error)) {
        mail_auth_free(&auth);
        mail_target_free(&target);
        mem_free(from);
        return error;
    }
    struct curl_slist* recipients = NULL;
    int count = 0;
    Item to = io_fs_option_get(&opts, "to");
    bool ok = from && from[0] && !has_line_break(from);
    if (!ok) error = mail_error(ERR_TYPE_MISMATCH, fn, "option 'from' must be one address");
    int64_t to_count = mail_is_list(to) ? fn_len(to) : (is_text_type_id(get_type_id(to)) ? 1 : 0);
    if (ok && to_count == 0) {
        ok = false;
        error = mail_error(ERR_TYPE_MISMATCH, fn, "option 'to' must name at least one recipient");
    }
    for (int64_t i = 0; ok && i < to_count; i++) {
        Item address = mail_is_list(to) ? item_at(to, i) : to;
        if (!is_text_type_id(get_type_id(address))) {
            ok = false;
            error = mail_error(ERR_TYPE_MISMATCH, fn, "each recipient must be text");
            break;
        }
        char* copy = mem_dup_n(address.get_chars(), address.get_len(), MEM_CAT_NETWORK);
        if (!copy || has_line_break(copy) || !copy[0]) {
            mem_free(copy);
            ok = false;
            error = mail_error(ERR_TYPE_MISMATCH, fn, "a recipient must be one address");
            break;
        }
        struct curl_slist* next = curl_slist_append(recipients, copy);
        mem_free(copy);
        if (!next) {
            ok = false;
            error = mail_error(ERR_INTERNAL_ERROR, fn, "out of memory");
            break;
        }
        recipients = next;
        count++;
    }
    CURL* curl = ok ? mail_curl_new(&target, &auth) : NULL;
    MailTransfer* t = curl ? mail_transfer_new(curl, true) : NULL;
    mail_auth_free(&auth);
    mail_target_free(&target);
    if (!t) {
        if (ok) error = mail_error(ERR_INTERNAL_ERROR, fn, "cannot create a curl handle");
        if (curl) curl_easy_cleanup(curl);
        curl_slist_free_all(recipients);
        mem_free(from);
        return error;
    }
    // the message bytes outlive this frame on the worker: copy them (D5.3.3)
    Binary* bytes = message_type == LMD_TYPE_BINARY ? message.get_safe_binary() : NULL;
    const char* data = bytes ? (const char*)binary_data(bytes) : message.get_chars();
    size_t length = bytes ? binary_length(bytes) : message.get_len();
    char* copy = mem_dup_n(data ? data : "", length, MEM_CAT_NETWORK);
    t->upload = copy;
    t->upload_length = length;
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, from);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT_ALLOWFAILS, 1L);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, mail_upload_cb);
    curl_easy_setopt(curl, CURLOPT_READDATA, t);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)length);
    Item outcome = mail_transfer_execute(t);
    bool* rejected = (bool*)mem_calloc((size_t)count, sizeof(bool), MEM_CAT_NETWORK);
    Item result;
    if (get_type_id(outcome) == LMD_TYPE_ERROR) {
        result = outcome;
    } else if (t->code != CURLE_OK) {
        static const char* const replies[] = {"4", "5"};
        result = mail_transfer_error(fn, t, replies, 2);
    } else {
        if (rejected) smtp_rejected(t->lines, recipients, count, rejected);
        RootFrame roots(2);
        Rooted<Array*> list(roots, array());
        Rooted<Item> address(roots, ItemNull);
        int k = 0;
        for (struct curl_slist* r = recipients; r; r = r->next, k++) {
            if (!rejected || !rejected[k]) continue;
            address.set(io_fs_make_string(r->data, strlen(r->data)));
            array_push_verbatim(list.get(), address.get());
        }
        result = (Item){.array = list.get()};
    }
    mem_free(rejected);
    mail_transfer_free(t);
    mem_free(copy);
    curl_slist_free_all(recipients);
    mem_free(from);
    return result;
}
