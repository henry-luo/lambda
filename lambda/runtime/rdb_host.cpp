#include "rdb_host.h"

#include "async.h"
#include "runtime-state.h"
#include "../jube/jube_registry.h"
#include "../../lib/log.h"
#include "../../lib/rdb.h"

static void rdb_host_close_row(void* user) {
    rdb_conn_life_close((RdbConnLife*)user);
}

static int rdb_host_authorize(const char* driver, const RdbTarget* targets, int count) {
    // the JA16 policy model (realm/permission gates) is still undesigned
    // (Jube ADR open item 8); every target is logged for audit meanwhile
    for (int i = 0; i < count; i++) {
        const RdbTarget* t = &targets[i];
        if (t->kind == RDB_PEER_TCP) {
            log_debug("rdb-registry: %s target tcp %s:%d", driver, t->host, t->port);
        } else {
            log_debug("rdb-registry: %s target kind=%d '%s'", driver, (int)t->kind, t->path);
        }
    }
    return RDB_OK;
}

static int rdb_host_add(RdbConnLife* life, const char* redacted_uri, uint32_t* out_rid,
                        void** out_owner) {
    *out_rid = 0;
    *out_owner = NULL;
    RuntimeResourceTable* table = context ? runtime_resource_table_context_ensure(context) : NULL;
    if (!table) {
        // tools without an evaluator (format conversion) own the call themselves
        log_debug("rdb-registry: no runtime owns '%s'", redacted_uri);
        return RDB_OK;
    }
    static const RuntimeResourceDescriptor* descriptor =
        runtime_resource_descriptor_from_legacy_name("RdbConnection");
    uint32_t rid = runtime_resource_table_add_native_owned(table, NULL, descriptor,
        rdb_host_close_row, life);
    if (!rid) {
        log_error("rdb-registry: cannot record connection '%s'", redacted_uri);
        return RDB_ERROR;
    }
    log_debug("rdb-registry: rid=%u '%s'", rid, redacted_uri);
    *out_rid = rid;
    *out_owner = table;
    return RDB_OK;
}

static void rdb_host_remove(void* owner, uint32_t rid) {
    // removing the row runs rdb_host_close_row: the table is the close authority
    if (owner) runtime_resource_table_remove((RuntimeResourceTable*)owner, rid);
}

static const RdbRegistryHooks rdb_host_hooks = {
    rdb_host_authorize,
    rdb_host_add,
    rdb_host_remove,
};

void rdb_host_install(void) {
    rdb_set_registry_hooks(&rdb_host_hooks);
    rdb_set_driver_resolver(jube_rdb_resolve_driver);
}
