#include "storage_command.h"
#include "private_storage.h"
#include "../../../lib/confined_process.h"
#include "../../../lib/hex.h"
#include "../../../lib/strbuf.h"
#include "../../../lib/str.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int storage_error(int error) {
    char message[128];
    int count = snprintf(message, sizeof(message), "{\"system_error\":%d}\n", error);
    if (count > 0 && (size_t)count < sizeof(message)) confined_process_emit(2, message, count);
    return 2;
}
static int storage_status(const SecurityPrivateStorageConfig* config) {
    SecurityPrivateStorageStatus status;
    if (!security_private_storage_inspect(config, &status)) return storage_error(errno);
    StrBuf* output = strbuf_new_cap(2048);
    if (!output) return storage_error(ENOMEM);
    char id[33]; hex_encode(status.reset_id, 16, id);
    strbuf_append_format(output, "{\"live_sessions\":%u,\"abandoned_sessions\":%u,\"reset_pending\":%s,\"reset_id\":\"%s\",\"quota_generation\":%llu,\"custom_quota\":%s,\"usage\":[",
        status.live_sessions, status.abandoned_sessions, status.reset_pending ? "true" : "false", status.reset_pending ? id : "",
        (unsigned long long)status.quota_generation, status.custom_quota ? "true" : "false");
    for (unsigned i = 0; i < SECURITY_PRIVATE_COUNT; i++) {
        const SecurityPrivateStorageUsage* usage = &status.usage[i];
        const SecurityPrivateStorageLimit* limit = &status.limits[i];
        strbuf_append_format(output, "%s{\"kind\":\"%s\",\"bytes\":%llu,\"entries\":%u,\"max_bytes\":%llu,\"max_entries\":%u,\"over_limit\":%s}",
            i ? "," : "", security_private_storage_kind_name((SecurityPrivateStorageKind)i), (unsigned long long)usage->bytes, usage->entries,
            (unsigned long long)limit->max_bytes, limit->max_entries, usage->over_limit ? "true" : "false");
    }
    strbuf_append_str(output, "]}\n");
    bool emitted = confined_process_emit(1, output->str, output->length);
    strbuf_free(output); return emitted ? 0 : storage_error(EIO);
}
static bool storage_uint(const char* text, uint64_t* value) {
    const char* end = nullptr;
    return text && *text && *text >= '0' && *text <= '9' && str_to_uint64(text, strlen(text), value, &end) && end && !*end;
}
int security_storage_command(SecurityGrantStore* store, int argc, char** argv) {
    bool status = argc == 3 && argv && !strcmp(argv[0], "status");
    bool begin = argc == 4 && argv && !strcmp(argv[0], "reset-data") && !strcmp(argv[3], "--confirm-data-loss");
    bool resume = argc == 5 && argv && !strcmp(argv[0], "resume-reset") && !strcmp(argv[4], "--confirm-data-loss");
    bool quota = argc == 7 && argv && !strcmp(argv[0], "quota");
    bool defaults = argc == 4 && argv && !strcmp(argv[0], "quota-defaults");
    uint64_t generation = 0, bytes = 0, entries = 0;
    int quota_kind = -1;
    if (quota) {
        for (unsigned i = 0; i < SECURITY_PRIVATE_COUNT; i++)
            if (!strcmp(argv[4], security_private_storage_kind_name((SecurityPrivateStorageKind)i))) quota_kind = (int)i;
    }
    uint32_t kind = (status || begin || resume || quota || defaults) ? (!strcmp(argv[1], "app") ? SECURITY_PRINCIPAL_APP :
        !strcmp(argv[1], "script") ? SECURITY_PRINCIPAL_SCRIPT : 0) : 0;
    uint8_t principal[32], id[16];
    if (!kind || strlen(argv[2]) != 64 || !hex_decode(argv[2], 64, principal, nullptr) ||
            (resume && (strlen(argv[3]) != 32 || !hex_decode(argv[3], 32, id, nullptr))) ||
            ((quota || defaults) && !storage_uint(argv[3], &generation)) ||
            (quota && (quota_kind < 0 || !storage_uint(argv[5], &bytes) || !bytes ||
                !storage_uint(argv[6], &entries) || !entries || entries > SECURITY_PRIVATE_MAX_ENTRIES ||
                (quota_kind == SECURITY_PRIVATE_TEMP && entries < 3)))) {
        const char* usage = "Usage: lambda-safety-host.exe storage status app|script PRINCIPAL_HEX | "
            "reset-data app|script PRINCIPAL_HEX --confirm-data-loss | "
            "resume-reset app|script PRINCIPAL_HEX RESET_HEX --confirm-data-loss | "
            "quota app|script PRINCIPAL_HEX GENERATION data|cache|temp BYTES ENTRIES | "
            "quota-defaults app|script PRINCIPAL_HEX GENERATION\n";
        confined_process_emit(2, usage, strlen(usage)); return 2;
    }
    // Remembered records identify the management target; they never become a
    // script client or authority to execute the remembered code/root binding.
    SecurityGrantSnapshot snapshot = {};
    SecurityStoreResult result = security_grant_store_inspect(store, kind, principal, &snapshot);
    if (result != SECURITY_STORE_OK) return storage_error(result == SECURITY_STORE_MISSING ? ENOENT : EACCES);
    SecurityPrivateStorageConfig config = {}; config.store = store; config.binding = snapshot.binding;
    security_grant_snapshot_destroy(&snapshot);
    security_private_storage_default_limits(config.limits);
    if (status) return storage_status(&config);
    if (quota || defaults) {
        SecurityPrivateStorageStatus current;
        if (!security_private_storage_inspect(&config, &current)) return storage_error(errno);
        if (quota) current.limits[quota_kind] = {bytes, (uint32_t)entries};
        uint64_t next;
        if (!security_private_storage_set_quota(&config, quota ? current.limits : nullptr, generation, &next)) return storage_error(errno);
        return storage_status(&config);
    }
    if (begin && !security_private_storage_reset_begin(&config, id)) return storage_error(errno);
    bool complete = false;
    // The service includes a possible interrupted staging entry. No grant lease is held
    // across the command; every bounded cleanup step takes its own owner lock.
    const uint32_t step_entries = 64;
    const uint32_t max_steps = (SECURITY_PRIVATE_SCAN_ENTRIES + step_entries - 1) / step_entries;
    for (uint32_t step = 0; !complete && step < max_steps; step++)
        if (!security_private_storage_reset_step(&config, id, step_entries, &complete)) return storage_error(errno);
    return complete ? storage_status(&config) : storage_error(EFBIG);
}
