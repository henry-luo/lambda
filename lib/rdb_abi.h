/**
 * @file rdb_abi.h
 * @brief RDB driver ABI shared by the host and RDB driver modules.
 *
 * Plain C records only: no lib functions, no host types. The in-host SQLite
 * driver and every driver in the `rdb-drivers` Jube module implement the same
 * RdbDriver table, reaching the host only through RdbHostAPI (RDB3, D7.3.3).
 * Records here are ABI: they evolve additively behind RDB_DRIVER_API_VERSION
 * and the struct_size fields.
 *
 * Design: vibe/Lambda_IO_RDB.md §13.
 */

#ifndef LIB_RDB_ABI_H
#define LIB_RDB_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RDB_DRIVER_API_VERSION 2
#define RDB_HOST_API_VERSION   1

/* ══════════════════════════════════════════════════════════════════════
 * Return codes
 * ══════════════════════════════════════════════════════════════════════ */

#define RDB_OK          0
#define RDB_ERROR       (-1)
#define RDB_CONN_LOST   (-2)   /* the server dropped the session; only the host closes it */
#define RDB_UNSUPPORTED (-3)   /* optional op absent for this backend */
#define RDB_ROW         100
#define RDB_DONE        101

/* ══════════════════════════════════════════════════════════════════════
 * Column type enum
 * ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    RDB_TYPE_NULL = 0,
    RDB_TYPE_INT,
    RDB_TYPE_FLOAT,
    RDB_TYPE_STRING,
    RDB_TYPE_DECIMAL,
    RDB_TYPE_DATETIME,
    RDB_TYPE_JSON,
    RDB_TYPE_BOOL,
    RDB_TYPE_BLOB,
    RDB_TYPE_UNKNOWN
} RdbType;

/* ══════════════════════════════════════════════════════════════════════
 * Schema metadata (allocated through RdbHostAPI.meta_alloc)
 * ══════════════════════════════════════════════════════════════════════ */

/** column metadata */
typedef struct {
    const char* name;           /* column name */
    const char* type_decl;      /* raw declared type, e.g. "VARCHAR(255)" */
    RdbType     type;           /* normalised type enum */
    bool        nullable;       /* allows NULL? */
    bool        primary_key;    /* part of primary key? */
    int         pk_index;       /* position in composite PK (0 if not PK) */
} RdbColumn;

/** index metadata */
typedef struct {
    const char* name;           /* index name */
    bool        unique;         /* unique index? */
    int         column_count;
    const char** columns;       /* column names */
} RdbIndex;

/** trigger metadata */
typedef struct {
    const char* name;           /* trigger name */
    const char* timing;         /* "BEFORE", "AFTER", or "INSTEAD OF" */
    const char* event;          /* "INSERT", "UPDATE", or "DELETE" */
} RdbTrigger;

/** SQL function metadata (database-level, not per-table) */
typedef struct {
    const char* name;           /* function name */
    const char* type;           /* "scalar", "aggregate", or "window" */
    int         narg;           /* number of arguments (-1 = variadic) */
    bool        builtin;        /* true for built-in functions */
} RdbFunction;

/** foreign key metadata */
typedef struct {
    const char* column;         /* FK column in this table */
    const char* ref_table;      /* referenced table name */
    const char* ref_column;     /* referenced column name */
    const char* link_name;      /* derived navigation name (stripped _id suffix) */
} RdbForeignKey;

/** table / view metadata */
typedef struct {
    const char* name;           /* table or view name */
    bool        is_view;        /* true for views */
    int         column_count;
    RdbColumn*  columns;
    int         index_count;
    RdbIndex*   indexes;
    int         fk_count;
    RdbForeignKey* foreign_keys;    /* outgoing FKs */
    int         reverse_fk_count;
    RdbForeignKey* reverse_fks;     /* incoming FKs from other tables (host-derived) */
    int         trigger_count;
    RdbTrigger* triggers;
} RdbTable;

/** database schema (all tables + views) */
typedef struct {
    int         table_count;
    RdbTable*   tables;
    int         function_count;
    RdbFunction* functions;     /* database-level */
} RdbSchema;

/* ══════════════════════════════════════════════════════════════════════
 * Parameters and cells
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    RdbType type;
    union {
        int64_t     int_val;
        double      float_val;
        const char* str_val;
        bool        bool_val;
    };
} RdbParam;

/** single cell value from current row. `type` is the payload representation
    (NULL, INT, FLOAT, BOOL, STRING or BLOB), not the column's logical type:
    decimal, datetime and JSON text arrive as STRING and the host decodes them
    by the declared column type (RDB6). */
typedef struct {
    RdbType type;
    bool    is_null;
    union {
        int64_t     int_val;
        double      float_val;
        struct {
            const char* str_val;  /* valid until next step() or finalize() */
            int         str_len;
        };
        bool        bool_val;
    };
} RdbValue;

/** result column description; name valid until finalize() */
typedef struct {
    const char* name;
    RdbType     type;           /* declared type, or RDB_TYPE_UNKNOWN for expressions */
} RdbColumnDesc;

/* ══════════════════════════════════════════════════════════════════════
 * Dialect + capabilities (RDB5)
 * ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    RDB_PLACEHOLDER_QNUM = 0,   /* ?1, ?2 (SQLite) */
    RDB_PLACEHOLDER_DOLLAR,     /* $1, $2 (PostgreSQL, DuckDB) */
    RDB_PLACEHOLDER_QMARK,      /* ?      (MySQL; positional) */
} RdbPlaceholderStyle;

typedef struct {
    RdbPlaceholderStyle placeholder;
    char                ident_quote;    /* '"' or '`' */
    const char*         like_escape;    /* ESCAPE literal for '\'; NULL: "'\'" */
} RdbDialect;

#define RDB_CAP_COLUMNAR          (1ull << 0)   /* native result_schema/fetch_batch */
#define RDB_CAP_CANCEL            (1ull << 1)   /* cancel() interrupts in-flight work */
#define RDB_CAP_STATEMENT_TIMEOUT (1ull << 2)   /* set_timeout() is server-enforced */
#define RDB_CAP_STREAMING         (1ull << 3)   /* rows/batches stream, not fully buffered */

/* ══════════════════════════════════════════════════════════════════════
 * Connection targets and info (JA16.1–JA16.3)
 * ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    RDB_PEER_NONE = 0,
    RDB_PEER_TCP,
    RDB_PEER_UNIX_SOCKET,
    RDB_PEER_FILE,
    RDB_PEER_MEMORY,
} RdbPeerKind;

/** one concrete endpoint a URI resolves to; authorised before connecting */
typedef struct {
    RdbPeerKind kind;
    int         port;           /* TCP only */
    char        host[256];      /* TCP host name or address */
    char        path[1024];     /* file path or Unix socket path */
} RdbTarget;

/** facts about a live connection, reported at registration */
typedef struct {
    RdbTarget peer;             /* must equal one authorised target */
    int64_t   socket_fd;        /* -1 when no socket (files, memory) */
    bool      tls;
    int64_t   backend_id;       /* PG backend pid, MySQL thread id; 0 if none */
    char      server_version[64];
} RdbConnInfo;

typedef struct {
    uint32_t struct_size;
    bool     readonly;
    int64_t  connect_timeout_ms;    /* 0 = backend default */
} RdbOpenOptions;

/* ══════════════════════════════════════════════════════════════════════
 * Host TLS bridge (RDB11): the host owns the network socket and the TLS
 * session; the client library talks plaintext to a private local endpoint
 * ══════════════════════════════════════════════════════════════════════ */

typedef enum {
    RDB_TLS_DISABLE = 0,        /* plaintext upstream */
    RDB_TLS_PREFER,             /* TLS when the server offers it */
    RDB_TLS_REQUIRE,            /* TLS or fail; certificate not verified */
    RDB_TLS_VERIFY_CA,          /* TLS + chain verified against the CA bundle */
    RDB_TLS_VERIFY_FULL,        /* ... and the server name matches */
} RdbTlsMode;

typedef enum {
    RDB_WIRE_POSTGRES = 1,      /* SSLRequest before the startup packet */
    RDB_WIRE_MYSQL,             /* TLS upgrade inside the connection handshake */
} RdbWireProtocol;

typedef struct {
    uint32_t        struct_size;
    RdbWireProtocol protocol;
    RdbTlsMode      tls;
    const char*     ca_file;            /* NULL: the host CA bundle */
    const char*     server_name;        /* SNI + verify-full name; NULL: target host */
    int64_t         connect_timeout_ms; /* 0: host default */
} RdbTunnelSpec;

/* ══════════════════════════════════════════════════════════════════════
 * Arrow C Data Interface (verbatim from the Arrow specification)
 * ══════════════════════════════════════════════════════════════════════ */

#ifndef ARROW_C_DATA_INTERFACE
#define ARROW_C_DATA_INTERFACE

#define ARROW_FLAG_DICTIONARY_ORDERED 1
#define ARROW_FLAG_NULLABLE 2
#define ARROW_FLAG_MAP_KEYS_SORTED 4

struct ArrowSchema {
    // Array type description
    const char* format;
    const char* name;
    const char* metadata;
    int64_t flags;
    int64_t n_children;
    struct ArrowSchema** children;
    struct ArrowSchema* dictionary;

    // Release callback
    void (*release)(struct ArrowSchema*);
    // Opaque producer-specific data
    void* private_data;
};

struct ArrowArray {
    // Array data description
    int64_t length;
    int64_t null_count;
    int64_t offset;
    int64_t n_buffers;
    int64_t n_children;
    const void** buffers;
    struct ArrowArray** children;
    struct ArrowArray* dictionary;

    // Release callback
    void (*release)(struct ArrowArray*);
    // Opaque producer-specific data
    void* private_data;
};

#endif  // ARROW_C_DATA_INTERFACE

/* ══════════════════════════════════════════════════════════════════════
 * Host services offered to drivers
 * ══════════════════════════════════════════════════════════════════════ */

#define RDB_LOG_ERROR 0
#define RDB_LOG_INFO  1
#define RDB_LOG_DEBUG 2

typedef struct RdbHostAPI {
    uint32_t struct_size;
    uint32_t api_version;
    /* schema metadata arena: zeroed allocation and string copy */
    void* (*meta_alloc)(void* meta, size_t size);
    char* (*meta_strdup)(void* meta, const char* s);
    /* every native connection registers before first use (JA16.1). A refusal
       (RDB_ERROR) obliges the driver to close it and fail the open. */
    int   (*conn_register)(void* open_ctx, void* native_conn, const RdbConnInfo* info);
    /* drivers never call log_* directly (D7.3.3); messages must be redacted */
    void  (*log)(int level, const char* message);
    /* CA bundle for TLS when the URI names none; NULL when unknown (RDB9) */
    const char* (*ca_bundle_path)(void);
    /* RDB11: open a host-owned upstream to an authorised target and return
       the local endpoint the client library connects to in plaintext:
       PostgreSQL gets a socket directory (".s.PGSQL.<port>" inside), MySQL a
       socket path. The tunnel lives until the host closes the connection. */
    int   (*tunnel_open)(void* open_ctx, const RdbTarget* target, const RdbTunnelSpec* spec,
                         char* out_endpoint, size_t cap);
    /* RDB12: SHA-256 for Lambda-side auth plugins; false on failure */
    bool  (*sha256)(const void* data, size_t len, uint8_t out[32]);
} RdbHostAPI;

/* ══════════════════════════════════════════════════════════════════════
 * Driver table
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct RdbDriver {
    uint32_t struct_size;
    uint32_t api_version;               /* RDB_DRIVER_API_VERSION */
    const char* name;                   /* "sqlite", "postgresql", ... */
    const RdbDialect* dialect;
    uint64_t caps;                      /* RDB_CAP_* */

    /* connection management, host-driven (JA16.1–JA16.3) */
    int  (*resolve_targets)(const char* uri, RdbTarget* out, int cap, int* out_count);
    int  (*open)(const RdbHostAPI* host, void* open_ctx, const char* uri,
                 const RdbOpenOptions* opts, void** out_conn);
    void (*close)(void* conn);          /* called by the host only */
    int  (*ping)(void* conn);           /* optional */
    int  (*reset)(void* conn);          /* optional: session reset before reuse */
    int  (*cancel)(void* conn);         /* optional; callable from any thread */
    int  (*set_timeout)(void* conn, int64_t statement_ms);   /* optional */

    /* schema */
    int  (*load_schema)(void* conn, const RdbHostAPI* host, void* meta, RdbSchema* out);

    /* row access */
    int  (*prepare)(void* conn, const char* sql, void** out_stmt);
    int  (*bind_param)(void* stmt, int index, const RdbParam* param);
    int  (*step)(void* stmt);           /* RDB_ROW / RDB_DONE / RDB_ERROR / RDB_CONN_LOST */
    int  (*column_count)(void* stmt);
    int  (*column_desc)(void* stmt, int col, RdbColumnDesc* out);
    RdbValue (*column_value)(void* stmt, int col);
    void (*finalize)(void* stmt);
    const char* (*error_msg)(void* conn);   /* must not echo credentials (RDB9) */

    /* column batches, optional (RDB7); batches outlive the statement */
    int  (*result_schema)(void* stmt, struct ArrowSchema* out);
    int  (*fetch_batch)(void* stmt, int64_t max_rows, struct ArrowArray* out);
} RdbDriver;

/* the prefix every driver must provide; later fields are size-gated */
#define RDB_DRIVER_MIN_SIZE (offsetof(RdbDriver, error_msg) + sizeof(void*))

#ifdef __cplusplus
}
#endif

#endif /* LIB_RDB_ABI_H */
