#include "update_command.h"
#include "../../../lib/confined_process.h"
#include "../../../lib/escape.h"
#include "../../../lib/mem.h"
#include "../../../lib/strbuf.h"
#include <errno.h>
#include <stdio.h>

static int update_command_result(SecurityUpdateResult result) {
    char message[128];
    int count = snprintf(message, sizeof(message), "{\"result\":\"%s\"}\n", security_update_result_name(result));
    if (count > 0 && (size_t)count < sizeof(message)) confined_process_emit(result == SECURITY_UPDATE_OK ? 1 : 2, message, count);
    return result == SECURITY_UPDATE_OK ? 0 : 2;
}
static const char* update_mode_name(SecurityUpdateMode mode) {
    return mode == SECURITY_UPDATE_ENABLED ? "enabled" : mode == SECURITY_UPDATE_PAUSED ? "paused" : "disabled";
}
static void update_command_text(StrBuf* buffer, const char* text) {
    escape_append_json_string(buffer, text, strlen(text), true, false);
}
static int update_command_status(SecurityUpdateControl* control) {
    SecurityUpdateSnapshot snapshot = {};
    SecurityUpdateResult result = security_update_inspect(control, &snapshot);
    if (result != SECURITY_UPDATE_OK) return update_command_result(result);
    StrBuf* output = strbuf_new_cap(4096);
    if (!output) { security_update_snapshot_free(&snapshot); return update_command_result(SECURITY_UPDATE_IO_ERROR); }
    // Preallocate the full bounded display before appending; StrBuf appenders
    // otherwise cannot report a later allocation failure to their caller.
    bool ready = strbuf_ensure_cap(output, 256 + (size_t)snapshot.count * 4096);
    if (ready) {
        strbuf_append_format(output, "{\"mode\":\"%s\",\"pause_until\":%llu,\"offline\":%s,\"packages\":[",
            update_mode_name(snapshot.mode), (unsigned long long)snapshot.pause_until, snapshot.offline ? "true" : "false");
        for (uint32_t i = 0; i < snapshot.count; i++) {
            SecurityUpdatePackageInfo* info = &snapshot.packages[i];
            if (i) strbuf_append_char(output, ',');
            strbuf_append_str(output, "{\"package\":"); update_command_text(output, info->spec.package);
            strbuf_append_str(output, ",\"endpoint\":"); update_command_text(output, info->spec.metadata_base);
            strbuf_append_str(output, ",\"channel\":"); update_command_text(output, info->spec.channel);
            strbuf_append_format(output, ",\"mode\":\"%s\",\"pause_until\":%llu,\"next_poll\":%llu}",
                update_mode_name(info->mode), (unsigned long long)info->pause_until, (unsigned long long)info->next_poll);
        }
        strbuf_append_str(output, "],\"delivery_available\":false}\n");
        ready = confined_process_emit(1, output->str, output->length);
    }
    strbuf_free(output); security_update_snapshot_free(&snapshot);
    return ready ? 0 : update_command_result(SECURITY_UPDATE_IO_ERROR);
}

int security_update_command(SecurityGrantStore* store, int argc, char** argv,
        uint64_t (*clock)(void*), void* context) {
    if (argc < 0 || (argc && !argv)) return update_command_result(SECURITY_UPDATE_INVALID);
    if (argc == 1 && !strcmp(argv[0], "init")) return update_command_result(security_update_initialize(store));
    bool status = argc == 1 && !strcmp(argv[0], "status");
    bool offline = argc == 2 && !strcmp(argv[0], "offline") && (!strcmp(argv[1], "on") || !strcmp(argv[1], "off"));
    bool mode = (argc == 2 || argc == 3) && (!strcmp(argv[0], "enable") || !strcmp(argv[0], "restore") ||
        !strcmp(argv[0], "disable") || !strcmp(argv[0], "pause"));
    uint64_t until = 0;
    if (mode && argc == 3) {
        char* end = nullptr; errno = 0;
        until = strtoull(argv[2], &end, 10);
        if (strcmp(argv[0], "pause") || errno || !end || *end || end == argv[2] || argv[2][0] == '-') mode = false;
    }
    if (!status && !offline && !mode) {
        const char* usage = "Usage: lambda-safety-host.exe updates init|status|offline on|off|"
            "enable --all|PACKAGE|restore --all|PACKAGE|disable --all|PACKAGE|pause --all|PACKAGE [UNTIL_UTC]\n";
        confined_process_emit(2, usage, strlen(usage)); return 2;
    }
    SecurityUpdateResult result;
    SecurityUpdateControl* control = security_update_open(store, clock, context, &result);
    if (!control) return update_command_result(result);
    int exit_code;
    if (status) exit_code = update_command_status(control);
    else {
        if (offline) result = security_update_set_offline(control, !strcmp(argv[1], "on"));
        else {
            SecurityUpdateMode value = !strcmp(argv[0], "pause") ? SECURITY_UPDATE_PAUSED :
                !strcmp(argv[0], "disable") ? SECURITY_UPDATE_DISABLED : SECURITY_UPDATE_ENABLED;
            result = security_update_set_mode(control, !strcmp(argv[1], "--all") ? nullptr : argv[1], value, until);
        }
        exit_code = update_command_result(result);
    }
    security_update_close(control); return exit_code;
}
