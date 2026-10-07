// rdb-drivers Jube module descriptor (RDB1–RDB3): registers the PostgreSQL
// and MySQL/MariaDB drivers behind lib/rdb.h. It exposes nothing to scripts.
#include "rdb_module.h"
#include "../../jube/jube.h"

#include <inttypes.h>

const RdbHostAPI* rdb_mod_host = NULL;

void rdb_mod_log(int level, const char* fmt, ...) {
    if (!rdb_mod_host || !rdb_mod_host->log) return;
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    rdb_mod_host->log(level, message);
}

void rdb_mod_buf_append(RdbModBuf* b, const char* s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (b->len + n + 1 > cap) cap *= 2;
        b->data = (char*)realloc(b->data, cap);
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void rdb_mod_buf_puts(RdbModBuf* b, const char* s) {
    rdb_mod_buf_append(b, s, strlen(s));
}

void rdb_mod_buf_printf(RdbModBuf* b, const char* fmt, ...) {
    char small[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(small, sizeof(small), fmt, args);
    va_end(args);
    if (n < 0) return;
    if ((size_t)n < sizeof(small)) {
        rdb_mod_buf_append(b, small, (size_t)n);
        return;
    }
    char* big = (char*)malloc((size_t)n + 1);
    va_start(args, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, args);
    va_end(args);
    rdb_mod_buf_append(b, big, (size_t)n);
    free(big);
}

void rdb_mod_buf_free(RdbModBuf* b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}

char* rdb_mod_strdup(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char* copy = (char*)malloc(n + 1);
    memcpy(copy, s, n + 1);
    return copy;
}

void rdb_mod_wipe(void* p, size_t n) {
    volatile unsigned char* bytes = (volatile unsigned char*)p;
    while (n--) *bytes++ = 0;
}

char* rdb_mod_param_text(const RdbParam* param) {
    char buf[64];
    switch (param->type) {
        case RDB_TYPE_NULL:
            return NULL;
        case RDB_TYPE_INT:
            snprintf(buf, sizeof(buf), "%" PRId64, param->int_val);
            return rdb_mod_strdup(buf);
        case RDB_TYPE_FLOAT:
            snprintf(buf, sizeof(buf), "%.17g", param->float_val);
            return rdb_mod_strdup(buf);
        case RDB_TYPE_BOOL:
            return rdb_mod_strdup(param->bool_val ? "1" : "0");
        default:
            return rdb_mod_strdup(param->str_val ? param->str_val : "");
    }
}

void* rdb_mod_meta_calloc(const RdbModMeta* m, size_t count, size_t size) {
    return m->host->meta_alloc(m->meta, (count ? count : 1) * size);
}

const char* rdb_mod_meta_dup(const RdbModMeta* m, const char* s) {
    return m->host->meta_strdup(m->meta, s ? s : "");
}

const char* rdb_mod_meta_word(const RdbModMeta* m, const char* word) {
    return rdb_mod_meta_dup(m, word);
}

bool rdb_mod_parse_tls_mode(const char* name, RdbTlsMode* out) {
    static const struct { const char* name; RdbTlsMode mode; } modes[] = {
        { "disable", RDB_TLS_DISABLE },       { "disabled", RDB_TLS_DISABLE },
        { "allow", RDB_TLS_PREFER },          { "prefer", RDB_TLS_PREFER },
        { "preferred", RDB_TLS_PREFER },      { "require", RDB_TLS_REQUIRE },
        { "required", RDB_TLS_REQUIRE },      { "verify-ca", RDB_TLS_VERIFY_CA },
        { "verify_ca", RDB_TLS_VERIFY_CA },   { "verify-full", RDB_TLS_VERIFY_FULL },
        { "verify_identity", RDB_TLS_VERIFY_FULL },
    };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        if (strcasecmp(name, modes[i].name) == 0) {
            *out = modes[i].mode;
            return true;
        }
    }
    return false;
}

static int rdb_drivers_init(const JubeHostAPI* host) {
    (void)host;
    return rdb_mysql_module_init();
}

static void rdb_drivers_shutdown(void) {
    // the host closed every registered connection before this point (§13.5.2)
    rdb_mysql_module_shutdown();
}

static const RdbDriver* const rdb_drivers[] = { &rdb_pg_driver, &rdb_mysql_driver };

static const JubeModuleDef rdb_drivers_module = {
    JUBE_ABI_VERSION,
    sizeof(JubeModuleDef),
    "rdb-drivers",
    "0.1.0",
    "PostgreSQL and MySQL/MariaDB drivers for lib/rdb",
    NULL,
    0,
    NULL,
    0,
    NULL,
    0,
    rdb_drivers_init,
    rdb_drivers_shutdown,
    NULL,           // interface_decl
    NULL,           // type_bindings
    0,
    NULL,           // runtime_reset
    NULL,           // heap_cleanup
    NULL,           // requirements
    NULL,           // globals
    0,
    NULL,           // runtime_attach
    NULL,           // runtime_reset_session
    NULL,           // runtime_detach
    NULL,           // dependencies
    0,
    rdb_drivers,
    2,
};

extern "C" JUBE_MODULE_EXPORT const JubeModuleDef* jube_module(void) { return &rdb_drivers_module; }
