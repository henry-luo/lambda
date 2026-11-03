#include "update_wire.h"
#include "../../runtime/transpiler.hpp"
#include "../../runtime/interp.hpp"
#include "../../runtime/runtime-state.h"
#include "../../runtime/lambda-root-frame.hpp"
#include "../../runtime/security_context.h"
#include "../../io/mark_builder.hpp"
#include "../../input/input.hpp"
#include "../../core/mark_reader.hpp"
#include "../../core/lambda-decimal.hpp"
#include "../../../lib/confined_process.h"
#include "../../../lib/process_sandbox.h"
#include "../../../lib/digest.h"
#include "../../../lib/host_paths.h"
#include "../../../lib/mem.h"
#include "../../../lib/time_util.h"
#include "../../../temp/generated/update_verifier_sources.h"
#include <errno.h>
#include <unistd.h>

static const char verifier_entry[] = "/lambda-update-verifier/package/security/tuf/_host.ls";
static const char stage_entry[] = "/lambda-update-verifier/package/security/tuf/_stage_host.ls";
static const UpdateVerifierSource* verifier_source(const char* path) {
    if (path) for (const auto& source : update_verifier_sources) if (!strcmp(source.path, path)) return &source;
    return nullptr;
}
static SecurityIOResult verifier_read(void*, const char* path, bool source, uint64_t limit,
        MemCategory category, char** bytes, size_t* size) {
    *bytes = nullptr; *size = 0;
    const UpdateVerifierSource* embedded = source ? verifier_source(path) : nullptr;
    if (!embedded || embedded->size > limit) return {SECURITY_DENIED, EACCES, false, {}};
    *bytes = mem_dup_n((const char*)embedded->bytes, embedded->size, category);
    if (!*bytes) return {SECURITY_DENIED, ENOMEM, false, {}};
    *size = embedded->size; return {SECURITY_ALLOWED, 0, false, {}};
}
static SecurityIOResult verifier_write(void*, const char*, const void*, size_t, bool, bool) {
    return {SECURITY_DENIED, EACCES, false, {}};
}
static SecurityIOResult verifier_canonical(void*, const char* path, char** out) {
    *out = verifier_source(path) ? mem_strdup(path, MEM_CAT_EVAL) : nullptr;
    return {*out ? SECURITY_ALLOWED : SECURITY_DENIED, *out ? 0 : EACCES, false, {}};
}
static SecurityIOResult verifier_kind(void*, const char* path, int32_t* kind) {
    if (!verifier_source(path)) return {SECURITY_DENIED, EACCES, false, {}};
    *kind = SECURITY_OBJECT_FILE; return {SECURITY_ALLOWED, 0, false, {}};
}
static bool verifier_text(Item item, char* out, size_t capacity) {
    if (!is_text_type_id(get_type_id(item))) return false;
    size_t size = item.get_len(); const char* bytes = item.get_chars();
    if (size >= capacity || memchr(bytes, 0, size)) return false;
    memcpy(out, bytes, size); out[size] = 0; return true;
}
static bool verifier_count(ItemReader item, uint64_t ceiling, uint64_t* out) {
    // Archive sizes use u64 and mixed arithmetic can promote totals to integer.
    // Exact conversion precedes the cap; neither carrier may silently truncate.
    if (item.isUInt64()) *out = item.asUInt64();
    else if (item.isInt() && item.asInt() >= 0) *out = (uint64_t)item.asInt();
    else {
        Item value = item.item();
        if (get_type_id(value) != LMD_TYPE_DECIMAL || value.get_decimal()->storage_kind != DECIMAL_BIGINT ||
            !decimal_to_uint64_exact(value, out)) return false;
    }
    return *out <= ceiling;
}
static bool verifier_rows(Item items, UpdateWireMessage* message) {
    if (!MarkReader(items).getRoot().isArray()) return false;
    ArrayReader array = MarkReader(items).getRoot().asArray();
    if (array.length() < 1 || array.length() > SECURITY_UPDATE_METADATA_ROLES) return false;
    message->count = (uint32_t)array.length();
    message->entries = (SecurityUpdateMetadata*)mem_calloc(message->count, sizeof(*message->entries), MEM_CAT_SYSTEM);
    if (!message->entries) return false;
    for (uint32_t i = 0; i < message->count; i++) {
        MapReader row = array.get(i).asMap(); SecurityUpdateMetadata* entry = &message->entries[i];
        Item bytes = row.get("bytes").item();
        if (!verifier_text(row.get("role").item(), entry->role, sizeof(entry->role)) || get_type_id(bytes) != LMD_TYPE_BINARY) return false;
        Binary* binary = bytes.get_safe_binary();
        if (!binary || binary_length(binary) > SECURITY_UPDATE_METADATA_ROLE_LIMIT) return false;
        entry->size = (uint32_t)binary_length(binary);
        entry->bytes = (uint8_t*)mem_dup_n((const char*)binary_data(binary), entry->size, MEM_CAT_SYSTEM);
        if (!entry->bytes) return false;
    }
    return true;
}
static Item verifier_call(Runtime* runtime, Item exports, const char* name, const Item* arguments, int argc) {
    Item callable = MapReader::fromItem(exports).get(name).item();
    return get_type_id(callable) == LMD_TYPE_FUNC ? interp_call_runtime_function(runtime, callable.function, arguments, argc) : ItemError;
}
static bool verifier_action(Runtime* runtime, Item exports, Item session, UpdateWireMessage* message) {
    RootFrame roots(2); Rooted<Item> action(roots, MapReader::fromItem(session).get("action").item());
    Rooted<Item> description(roots, ItemNull);
    MapReader fields = MapReader::fromItem(action.get()); char kind[32];
    if (!verifier_text(fields.get("kind").item(), kind, sizeof(kind))) return false;
    if (!strcmp(kind, "checkpoint")) {
        message->kind = UPDATE_WIRE_CHECKPOINT; return verifier_rows(fields.get("entries").item(), message);
    }
    if (!strcmp(kind, "fetch")) {
        message->kind = UPDATE_WIRE_FETCH;
        if (!verifier_count(fields.get("limit"), UPDATE_WIRE_BODY_LIMIT, &message->limit) ||
            !verifier_text(fields.get("path").item(), message->path, sizeof(message->path))) return false;
        char phase[32];
        if (!verifier_text(MapReader::fromItem(session).get("phase").item(), phase, sizeof(phase))) return false;
        message->artifact = !strcmp(phase, "artifact"); return true;
    }
    if (strcmp(kind, "done")) return false;
    message->kind = UPDATE_WIRE_DONE; char status[32];
    if (!verifier_text(fields.get("status").item(), status, sizeof(status))) return false;
    if (!strcmp(status, "unchanged")) { message->status = UPDATE_WIRE_UNCHANGED; return true; }
    if (strcmp(status, "verified")) return false;
    Item bytes = fields.get("bytes").item();
    if (get_type_id(bytes) != LMD_TYPE_BINARY) return false;
    Binary* binary = bytes.get_safe_binary();
    if (!binary || !digest_sha256(binary_data(binary), binary_length(binary), message->digest)) return false;
    RootSpan arguments(1); arguments.items()[0] = fields.get("description").item();
    description.set(verifier_call(runtime, exports, "description", arguments.items(), 1));
    if (get_type_id(description.get()) != LMD_TYPE_STRING || description.get().get_len() > 262144) return false;
    message->status = UPDATE_WIRE_VERIFIED; message->size = (uint32_t)description.get().get_len();
    message->bytes = (uint8_t*)mem_dup_n(description.get().get_chars(), message->size, MEM_CAT_SYSTEM);
    return message->bytes != nullptr;
}
static int verifier_execute(Runtime* runtime, UpdateWire* wire) {
    Input* module = run_script_mir(runtime, nullptr, (char*)verifier_entry);
    if (!module || get_type_id(module->root) != LMD_TYPE_MAP) return 3;
    RootFrame roots(2); Rooted<Item> exports(roots, module->root); Rooted<Item> session(roots, ItemNull);
    UpdateWireMessage initial = {};
    if (!security_update_wire_receive(wire, update_wire_kind_mask(UPDATE_WIRE_INIT), &initial)) return 3;
    wire->deadline = time_now_ms() + initial.limit;
    // One bounded process owns these arena arguments until all retained Lambda
    // state has been released. Never free an input while the state references it.
    Input* inputs = InputManager::create_input(nullptr);
    if (!inputs) { security_update_wire_free(&initial); return 3; }
    MarkBuilder builder(inputs); ArrayBuilder cache = builder.array();
    for (uint32_t i = 0; i < initial.count; i++) {
        SecurityUpdateMetadata* entry = &initial.entries[i];
        cache.append(builder.map().put("role", entry->role).put("bytes", {.item = x2it(builder.createBinary(entry->bytes, entry->size))}).final());
    }
    RootSpan arguments(3); arguments.items()[0] = cache.final();
    arguments.items()[1] = builder.createStringItem(initial.utc); arguments.items()[2] = builder.createStringItem(initial.path);
    session.set(verifier_call(runtime, exports.get(), "begin_refresh", arguments.items(), 3));
    security_update_wire_free(&initial);
    for (unsigned step = 0; step < 256; step++) {
        if (get_type_id(session.get()) == LMD_TYPE_ERROR) return 3;
        UpdateWireMessage action = {};
        bool ready = verifier_action(runtime, exports.get(), session.get(), &action);
        bool done = action.kind == UPDATE_WIRE_DONE;
        bool sent = ready && security_update_wire_send(wire, &action);
        security_update_wire_free(&action);
        if (!sent) return 3;
        if (done) return 0;
        UpdateWireMessage response = {};
        if (!security_update_wire_receive(wire, update_wire_kind_mask(UPDATE_WIRE_RESPONSE), &response)) return 3;
        Item payload = builder.map().put("status", {.item = y2it(builder.createSymbol(response.status == UPDATE_WIRE_OK ? "ok" : "not_found"))})
            .put("bytes", {.item = x2it(builder.createBinary(response.bytes, response.size))}).final();
        arguments.items()[0] = session.get(); arguments.items()[1] = payload;
        session.set(verifier_call(runtime, exports.get(), "advance", arguments.items(), 2));
        security_update_wire_free(&response);
    }
    return 3;
}
static int stage_execute(Runtime* runtime, UpdateWire* wire) {
    Input* module = run_script_mir(runtime, nullptr, (char*)stage_entry);
    if (!module || get_type_id(module->root) != LMD_TYPE_MAP) return 3;
    RootFrame roots(2); Rooted<Item> exports(roots, module->root); Rooted<Item> session(roots, ItemNull);
    UpdateWireMessage initial = {};
    if (!security_update_wire_receive(wire, update_wire_kind_mask(UPDATE_WIRE_STAGE_INIT), &initial)) return 3;
    wire->deadline = time_now_ms() + initial.limit;
    Input* inputs = InputManager::create_input(nullptr);
    if (!inputs) { security_update_wire_free(&initial); return 3; }
    MarkBuilder builder(inputs); RootSpan arguments(1);
    arguments.items()[0] = {.item = x2it(builder.createBinary(initial.bytes, initial.size))};
    session.set(verifier_call(runtime, exports.get(), "begin", arguments.items(), 1));
    security_update_wire_free(&initial);
    for (uint32_t step = 0; step <= 4096; step++) {
        if (get_type_id(session.get()) != LMD_TYPE_MAP) return 3;
        MapReader action = MapReader::fromItem(MapReader::fromItem(session.get()).get("action").item());
        char kind[16]; UpdateWireMessage message = {};
        if (!verifier_text(action.get("kind").item(), kind, sizeof(kind))) return 3;
        if (!strcmp(kind, "done")) {
            uint64_t count = 0;
            Item digest = action.get("digest").item();
            if (!verifier_count(action.get("entries"), 4096, &count) ||
                !verifier_count(action.get("bytes"), UPDATE_WIRE_BODY_LIMIT, &message.limit) ||
                get_type_id(digest) != LMD_TYPE_BINARY || digest.get_len() != 32) return 3;
            message.kind = UPDATE_WIRE_STAGE_END; message.status = (uint32_t)count;
            memcpy(message.digest, binary_data(digest.get_safe_binary()), 32);
            return security_update_wire_send(wire, &message) ? 0 : 3;
        }
        if (strcmp(kind, "entry") || !verifier_text(action.get("path").item(), message.path, sizeof(message.path))) return 3;
        Item bytes = action.get("bytes").item(), directory = action.get("directory").item(), executable = action.get("executable").item();
        if (get_type_id(bytes) != LMD_TYPE_BINARY || bytes.get_len() > UPDATE_WIRE_STAGE_MEMBER_LIMIT ||
            get_type_id(directory) != LMD_TYPE_BOOL || get_type_id(executable) != LMD_TYPE_BOOL) return 3;
        message.kind = UPDATE_WIRE_STAGE_ENTRY; message.artifact = directory.bool_val; message.status = executable.bool_val;
        // Send borrows the rooted session's bytes synchronously; never free them
        // through the wire message owner or advance the VM during transfer.
        message.bytes = (uint8_t*)binary_data(bytes.get_safe_binary()); message.size = (uint32_t)bytes.get_len();
        if (!security_update_wire_send(wire, &message)) return 3;
        UpdateWireMessage response = {};
        if (!security_update_wire_receive(wire, update_wire_kind_mask(UPDATE_WIRE_STAGE_ACK), &response)) return 3;
        bool acknowledged = response.status == UPDATE_WIRE_OK && !response.size;
        security_update_wire_free(&response);
        if (!acknowledged) return 3;
        arguments.items()[0] = session.get();
        session.set(verifier_call(runtime, exports.get(), "advance", arguments.items(), 1));
    }
    return 3;
}
extern "C" __attribute__((visibility("default")))
int lambda_safety_worker_entry_v1(int fd, uint64_t session_id, const char* entry, const char*) {
    bool stage = entry && !strcmp(entry, "lambda-package-stage-v1");
    if (!entry || (!stage && strcmp(entry, "lambda-update-verifier-v1")) || !session_id ||
        !confined_process_worker_handles(fd) || !process_sandbox_is_applied(getpid())) return 2;
    IpcStream* stream = ipc_stream_adopt(fd); if (!stream) return 2;
    UpdateWire wire = {stream, session_id, 0, 0, time_now_ms() + 5000, nullptr, nullptr};
    UpdateWireMessage ready = {}; ready.kind = UPDATE_WIRE_READY;
    if (!security_update_wire_send(&wire, &ready)) { ipc_stream_close(stream); return 2; }
    host_paths_set(HOST_PATH_HOME, "/lambda-update-verifier");
    Runtime runtime = {}; runtime_init(&runtime);
    EvalContext* owner = runtime_get_eval_context(&runtime);
    SecurityIOClient client = {1, nullptr, verifier_read, verifier_write, verifier_canonical, verifier_kind};
    client.authority_stamp = [](void*, SecurityAuthorityStamp* stamp) -> SecurityIOResult {
        *stamp = {1, 1}; return {SECURITY_ALLOWED, 0, false, {}}; };
    int status = 2;
    if (owner && eval_context_bind_security_io(owner, &client) && eval_context_bind_security_directory(owner, "/lambda-update-verifier")) {
        lambda_tier_set(LAMBDA_TIER_INTERP); status = stage ? stage_execute(&runtime, &wire) : verifier_execute(&runtime, &wire);
    }
    runtime_cleanup(&runtime); ipc_stream_close(stream); InputManager::destroy_global(); return status;
}
